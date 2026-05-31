#include "addiction_module.h"

#include <algorithm>
#include <cmath>

#include "core/rng/deterministic_rng.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"

namespace econlife {

namespace {
// Salt mixed into the province RNG seed so addiction's relapse draws never
// share a stream with another module that seeds from the same
// (world_seed, tick, province) triple (e.g. drug_economy's addiction seeding).
constexpr uint64_t kAddictionRngSalt = 0xADD1C7;
}  // namespace

float AddictionModule::craving_increment(AddictionStage stage, const AddictionConfig& cfg) {
    switch (stage) {
        case AddictionStage::casual:
            return cfg.casual_craving_inc;
        case AddictionStage::regular:
            return cfg.regular_craving_inc;
        case AddictionStage::dependent:
            return cfg.dependent_craving_inc;
        case AddictionStage::active:
            return cfg.active_craving_inc;
        case AddictionStage::recovery:
            return -cfg.craving_decay_rate_recovery;
        default:
            return 0.0f;
    }
}

AddictionStage AddictionModule::compute_next_stage(const AddictionState& state,
                                                   const AddictionConfig& cfg) {
    switch (state.stage) {
        case AddictionStage::none:
            return AddictionStage::none;

        case AddictionStage::casual:
            if (state.consecutive_use_ticks >= cfg.regular_use_threshold &&
                state.craving >= cfg.casual_to_regular_craving) {
                return AddictionStage::regular;
            }
            return AddictionStage::casual;

        case AddictionStage::regular:
            if (state.consecutive_use_ticks >= cfg.dependency_threshold &&
                state.tolerance >= cfg.dependency_tolerance_floor &&
                state.craving >= cfg.regular_to_dependent_craving) {
                return AddictionStage::dependent;
            }
            return AddictionStage::regular;

        case AddictionStage::dependent:
            if (state.craving >= cfg.active_craving_threshold &&
                state.consecutive_use_ticks >= cfg.active_duration_ticks) {
                return AddictionStage::active;
            }
            return AddictionStage::dependent;

        case AddictionStage::active:
            if (state.clean_ticks >= cfg.recovery_attempt_threshold) {
                return AddictionStage::recovery;
            }
            return AddictionStage::active;

        case AddictionStage::recovery:
            return AddictionStage::recovery;

        case AddictionStage::terminal:
            return AddictionStage::terminal;

        default:
            return state.stage;
    }
}

float AddictionModule::compute_withdrawal_damage(AddictionStage stage, uint32_t supply_gap_ticks,
                                                 const AddictionConfig& cfg) {
    if (stage < AddictionStage::dependent)
        return 0.0f;
    if (supply_gap_ticks == 0)
        return 0.0f;
    return cfg.withdrawal_health_hit;
}

float AddictionModule::compute_work_efficiency(AddictionStage stage, const AddictionConfig& cfg) {
    switch (stage) {
        case AddictionStage::dependent:
            return cfg.dependent_work_efficiency;
        case AddictionStage::active:
            return cfg.active_work_efficiency;
        case AddictionStage::terminal:
            return cfg.terminal_work_efficiency;
        default:
            return 1.0f;
    }
}

float AddictionModule::compute_addiction_rate_delta(AddictionStage old_stage,
                                                    AddictionStage new_stage,
                                                    const AddictionConfig& cfg) {
    auto is_counted = [](AddictionStage s) {
        return s == AddictionStage::dependent || s == AddictionStage::active ||
               s == AddictionStage::terminal;
    };
    float delta = 0.0f;
    if (!is_counted(old_stage) && is_counted(new_stage)) {
        delta += cfg.rate_delta_per_active_npc;
    } else if (is_counted(old_stage) && !is_counted(new_stage)) {
        delta -= cfg.rate_delta_per_active_npc;
    }
    return delta;
}

bool AddictionModule::is_recovery_complete(uint32_t clean_ticks, float relapse_probability,
                                           const AddictionConfig& cfg) {
    return clean_ticks >= cfg.full_recovery_ticks &&
           relapse_probability < cfg.recovery_success_threshold;
}

void AddictionModule::execute_province(uint32_t province_idx, const WorldState& state,
                                       DeltaBuffer& province_delta) {
    if (province_idx >= state.provinces.size())
        return;

    float addiction_rate_delta = 0.0f;

    // Iterate NPCs in this province via the live bucket index. Replaces the
    // older Province::significant_npc_ids snapshot (built at world generation
    // and not refreshed on cross-province migration) plus linear find_npc.
    if (province_idx >= state.npc_indices_by_province.size())
        return;
    std::vector<uint32_t> npc_indices(state.npc_indices_by_province[province_idx].begin(),
                                      state.npc_indices_by_province[province_idx].end());
    std::sort(npc_indices.begin(), npc_indices.end(), [&](uint32_t a, uint32_t b) {
        return state.significant_npcs[a].id < state.significant_npcs[b].id;
    });

    // Province-level RNG for relapse draws. Forked per npc_id below so each
    // NPC's draw depends only on (world_seed, tick, province, npc_id) — never
    // on iteration order or how many NPCs happen to draw — keeping output
    // identical regardless of core count.
    const uint64_t province_seed = state.world_seed ^
                                   (static_cast<uint64_t>(state.current_tick) << 16) ^
                                   (static_cast<uint64_t>(province_idx) << 8) ^ kAddictionRngSalt;
    const DeterministicRNG province_rng(province_seed);

    for (uint32_t idx : npc_indices) {
        const NPC& npc_ref = state.significant_npcs[idx];
        const NPC* npc = &npc_ref;
        const uint32_t npc_id = npc_ref.id;
        if (npc->status != NPCStatus::active)
            continue;

        // Per-NPC addiction state lives on NPC::addiction_state.
        // Stage `none` means the NPC isn't in the state machine yet
        // (no substance pathway has seeded them); skip without emitting.
        if (npc->addiction_state.stage == AddictionStage::none)
            continue;

        AddictionState current = npc->addiction_state;
        const AddictionStage old_stage = current.stage;

        // A "using" stage means the NPC is actively consuming the substance
        // this tick (everything between casual and active inclusive). recovery
        // and terminal are not using stages.
        auto is_using_stage = [](AddictionStage s) {
            return s == AddictionStage::casual || s == AddictionStage::regular ||
                   s == AddictionStage::dependent || s == AddictionStage::active;
        };

        // Increment craving
        current.craving =
            std::clamp(current.craving + craving_increment(current.stage, cfg_), 0.0f, 1.0f);

        // Tolerance buildup for casual
        if (current.stage == AddictionStage::casual && current.consecutive_use_ticks > 0) {
            current.tolerance =
                std::clamp(current.tolerance + cfg_.tolerance_per_use_casual, 0.0f, 1.0f);
        }

        // Advance the tick counters that drive stage transitions. Without this
        // the state machine could never progress organically: casual->regular
        // needs consecutive_use_ticks >= regular_use_threshold and
        // regular->dependent needs >= dependency_threshold, while recovery
        // completion needs clean_ticks to accumulate. Use ticks accrue while
        // using; clean ticks accrue while in recovery.
        if (is_using_stage(current.stage)) {
            current.consecutive_use_ticks += 1;
            current.clean_ticks = 0;
            // Supply is assumed available this tick. Modelling genuine supply
            // gaps (and the withdrawal damage they cause) requires a substance
            // market-availability signal that does not yet exist — see
            // flagged_issues.md. supply_gap_ticks therefore stays 0 for now.
            current.supply_gap_ticks = 0;
        } else if (current.stage == AddictionStage::recovery) {
            current.clean_ticks += 1;
        }

        // Relapse risk tracks current craving (which decays during recovery).
        current.relapse_probability =
            std::clamp(current.craving * cfg_.relapse_history_weight, 0.0f, 1.0f);

        // Stage transition. recovery has two extra exits the static helper does
        // not model — a stochastic relapse back to active and a deterministic
        // completion to none — so handle recovery here and defer the rest to
        // compute_next_stage.
        AddictionStage new_stage;
        if (current.stage == AddictionStage::recovery) {
            DeterministicRNG npc_rng = province_rng.fork(npc_id);
            if (current.relapse_probability > 0.0f &&
                npc_rng.next_float() < current.relapse_probability) {
                new_stage = AddictionStage::active;  // relapse
                current.clean_ticks = 0;
                current.consecutive_use_ticks = 0;
            } else if (is_recovery_complete(current.clean_ticks, current.relapse_probability,
                                            cfg_)) {
                new_stage = AddictionStage::none;  // full recovery
            } else {
                new_stage = AddictionStage::recovery;
            }
        } else {
            new_stage = compute_next_stage(current, cfg_);
        }

        // Entering recovery: stop the use clock and start the clean clock.
        if (old_stage != AddictionStage::recovery && new_stage == AddictionStage::recovery) {
            current.consecutive_use_ticks = 0;
            current.clean_ticks = 0;
        }

        // Full recovery clears the machine back to the default (stage none).
        if (new_stage == AddictionStage::none) {
            current = AddictionState{};
        } else {
            current.stage = new_stage;
        }

        // Province rate delta
        addiction_rate_delta += compute_addiction_rate_delta(old_stage, new_stage, cfg_);

        // NPCDelta: deduct substance spending from NPC capital
        // Spending scales with addiction severity.
        constexpr float SUBSTANCE_SPEND_CASUAL = 5.0f;
        constexpr float SUBSTANCE_SPEND_REGULAR = 15.0f;
        constexpr float SUBSTANCE_SPEND_DEPENDENT = 30.0f;
        constexpr float SUBSTANCE_SPEND_ACTIVE = 50.0f;
        constexpr float SUBSTANCE_SPEND_TERMINAL = 20.0f;  // reduced capacity to obtain

        float spend = 0.0f;
        switch (new_stage) {
            case AddictionStage::casual:
                spend = SUBSTANCE_SPEND_CASUAL;
                break;
            case AddictionStage::regular:
                spend = SUBSTANCE_SPEND_REGULAR;
                break;
            case AddictionStage::dependent:
                spend = SUBSTANCE_SPEND_DEPENDENT;
                break;
            case AddictionStage::active:
                spend = SUBSTANCE_SPEND_ACTIVE;
                break;
            case AddictionStage::terminal:
                spend = SUBSTANCE_SPEND_TERMINAL;
                break;
            default:
                break;
        }
        // Persist the stepped addiction state through NPCDelta. Bundling
        // capital_delta in the same NPCDelta keeps emissions per NPC to one;
        // apply_deltas applies both fields in a single pass.
        NPCDelta npc_delta;
        npc_delta.npc_id = npc_id;
        if (spend > 0.0f) {
            npc_delta.capital_delta = -spend;
        }
        npc_delta.set_addiction_state = current;

        // One-time memory on crossing into the active stage: addiction now
        // impairs day-to-day function. Emitted exactly once on the
        // non-active -> active transition (per INTERFACE.md).
        if (old_stage != AddictionStage::active && new_stage == AddictionStage::active) {
            MemoryEntry mem{};
            mem.tick_timestamp = state.current_tick;
            mem.type = MemoryType::event;
            mem.subject_id = npc_id;       // a memory about the NPC's own condition
            mem.emotional_weight = -0.5f;  // unfavorable
            mem.decay = 1.0f;
            mem.is_actionable = true;
            npc_delta.new_memory_entry = mem;
        }

        province_delta.npc_deltas.push_back(npc_delta);
    }

    // Province-level addiction rate delta
    if (std::abs(addiction_rate_delta) > 1e-6f) {
        RegionDelta rdelta;
        rdelta.region_id = province_idx;
        rdelta.addiction_rate_delta = addiction_rate_delta;
        // High addiction rate degrades regional stability
        // Stability penalty proportional to how much the rate increased
        if (addiction_rate_delta > 0.0f) {
            rdelta.stability_delta = -addiction_rate_delta * cfg_.grievance_per_addict_fraction;
        }
        province_delta.region_deltas.push_back(rdelta);
    }
}

void AddictionModule::execute(const WorldState& state, DeltaBuffer& delta) {
    for (uint32_t i = 0; i < state.provinces.size(); ++i) {
        execute_province(i, state, delta);
    }
}

}  // namespace econlife
