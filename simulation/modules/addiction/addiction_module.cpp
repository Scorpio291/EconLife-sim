#include "addiction_module.h"

#include <algorithm>
#include <cmath>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"

namespace econlife {

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

    const auto& province = state.provinces[province_idx];
    float addiction_rate_delta = 0.0f;

    // Process NPCs in id ascending order
    auto npc_ids = province.significant_npc_ids;
    std::sort(npc_ids.begin(), npc_ids.end());

    for (uint32_t npc_id : npc_ids) {
        // Find NPC
        const NPC* npc = nullptr;
        for (const auto& n : state.significant_npcs) {
            if (n.id == npc_id) {
                npc = &n;
                break;
            }
        }
        if (!npc)
            continue;
        if (npc->status != NPCStatus::active)
            continue;

        // Check module-internal addiction state
        auto it = addiction_states_.find(npc_id);
        if (it == addiction_states_.end())
            continue;
        if (it->second.stage == AddictionStage::none)
            continue;

        AddictionState current = it->second;
        AddictionStage old_stage = current.stage;

        // Increment craving
        current.craving =
            std::clamp(current.craving + craving_increment(current.stage, cfg_), 0.0f, 1.0f);

        // Tolerance buildup for casual
        if (current.stage == AddictionStage::casual && current.consecutive_use_ticks > 0) {
            current.tolerance =
                std::clamp(current.tolerance + cfg_.tolerance_per_use_casual, 0.0f, 1.0f);
        }

        // Stage transition
        AddictionStage new_stage = compute_next_stage(current, cfg_);
        current.stage = new_stage;

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
        if (spend > 0.0f) {
            NPCDelta npc_delta;
            npc_delta.npc_id = npc_id;
            npc_delta.capital_delta = -spend;
            province_delta.npc_deltas.push_back(npc_delta);
        }

        // Update internal state
        it->second = current;
    }

    // Province-level addiction rate delta
    if (std::abs(addiction_rate_delta) > 1e-6f) {
        RegionDelta rdelta;
        rdelta.region_id = province_idx;
        rdelta.addiction_rate_delta = addiction_rate_delta;
        // High addiction rate degrades regional stability
        // Stability penalty proportional to how much the rate increased
        if (addiction_rate_delta > 0.0f) {
            rdelta.stability_delta =
                -addiction_rate_delta * cfg_.grievance_per_addict_fraction;
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
