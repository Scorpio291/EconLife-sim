#include "addiction_module.h"
#include "core/world_state/world_state.h"
#include "core/world_state/delta_buffer.h"
#include <algorithm>
#include <cmath>

namespace econlife {

float AddictionModule::craving_increment(AddictionStage stage) {
    switch (stage) {
        case AddictionStage::casual:    return CASUAL_CRAVING_INC;
        case AddictionStage::regular:   return REGULAR_CRAVING_INC;
        case AddictionStage::dependent: return DEPENDENT_CRAVING_INC;
        case AddictionStage::active:    return ACTIVE_CRAVING_INC;
        case AddictionStage::recovery:  return -CRAVING_DECAY_RATE_RECOVERY;
        default: return 0.0f;
    }
}

AddictionStage AddictionModule::compute_next_stage(const AddictionState& state) {
    switch (state.stage) {
        case AddictionStage::none:
            return AddictionStage::none;

        case AddictionStage::casual:
            if (state.consecutive_use_ticks >= REGULAR_USE_THRESHOLD &&
                state.craving >= CASUAL_TO_REGULAR_CRAVING) {
                return AddictionStage::regular;
            }
            return AddictionStage::casual;

        case AddictionStage::regular:
            if (state.consecutive_use_ticks >= DEPENDENCY_THRESHOLD &&
                state.tolerance >= DEPENDENCY_TOLERANCE_FLOOR &&
                state.craving >= REGULAR_TO_DEPENDENT_CRAVING) {
                return AddictionStage::dependent;
            }
            return AddictionStage::regular;

        case AddictionStage::dependent:
            if (state.craving >= ACTIVE_CRAVING_THRESHOLD &&
                state.consecutive_use_ticks >= ACTIVE_DURATION_TICKS) {
                return AddictionStage::active;
            }
            return AddictionStage::dependent;

        case AddictionStage::active:
            if (state.clean_ticks >= RECOVERY_ATTEMPT_THRESHOLD) {
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

float AddictionModule::compute_withdrawal_damage(AddictionStage stage, uint32_t supply_gap_ticks) {
    if (stage < AddictionStage::dependent) return 0.0f;
    if (supply_gap_ticks == 0) return 0.0f;
    return WITHDRAWAL_HEALTH_HIT;
}

float AddictionModule::compute_work_efficiency(AddictionStage stage) {
    switch (stage) {
        case AddictionStage::dependent: return DEPENDENT_WORK_EFFICIENCY;
        case AddictionStage::active:    return ACTIVE_WORK_EFFICIENCY;
        case AddictionStage::terminal:  return TERMINAL_WORK_EFFICIENCY;
        default: return 1.0f;
    }
}

float AddictionModule::compute_addiction_rate_delta(AddictionStage old_stage,
                                                    AddictionStage new_stage) {
    auto is_counted = [](AddictionStage s) {
        return s == AddictionStage::dependent || s == AddictionStage::active ||
               s == AddictionStage::terminal;
    };
    float delta = 0.0f;
    if (!is_counted(old_stage) && is_counted(new_stage)) {
        delta += RATE_DELTA_PER_ACTIVE_NPC;
    } else if (is_counted(old_stage) && !is_counted(new_stage)) {
        delta -= RATE_DELTA_PER_ACTIVE_NPC;
    }
    return delta;
}

bool AddictionModule::is_recovery_complete(uint32_t clean_ticks, float relapse_probability) {
    return clean_ticks >= FULL_RECOVERY_TICKS &&
           relapse_probability < RECOVERY_SUCCESS_THRESHOLD;
}

void AddictionModule::execute_province(uint32_t province_idx,
                                        const WorldState& state,
                                        DeltaBuffer& province_delta) {
    if (province_idx >= state.provinces.size()) return;

    const auto& province = state.provinces[province_idx];
    float addiction_rate_delta = 0.0f;

    // Process NPCs in id ascending order
    auto npc_ids = province.significant_npc_ids;
    std::sort(npc_ids.begin(), npc_ids.end());

    for (uint32_t npc_id : npc_ids) {
        // Find NPC
        const NPC* npc = nullptr;
        for (const auto& n : state.significant_npcs) {
            if (n.id == npc_id) { npc = &n; break; }
        }
        if (!npc) continue;
        if (npc->status != NPCStatus::active) continue;

        // Check module-internal addiction state
        auto it = addiction_states_.find(npc_id);
        if (it == addiction_states_.end()) continue;
        if (it->second.stage == AddictionStage::none) continue;

        AddictionState current = it->second;
        AddictionStage old_stage = current.stage;

        // Increment craving
        current.craving = std::clamp(
            current.craving + craving_increment(current.stage), 0.0f, 1.0f);

        // Tolerance buildup for casual
        if (current.stage == AddictionStage::casual && current.consecutive_use_ticks > 0) {
            current.tolerance = std::clamp(
                current.tolerance + TOLERANCE_PER_USE_CASUAL, 0.0f, 1.0f);
        }

        // Stage transition
        AddictionStage new_stage = compute_next_stage(current);
        current.stage = new_stage;

        // Province rate delta
        addiction_rate_delta += compute_addiction_rate_delta(old_stage, new_stage);

        // Update internal state
        it->second = current;
    }

    // Province-level addiction rate delta
    if (std::abs(addiction_rate_delta) > 1e-6f) {
        RegionDelta rdelta;
        rdelta.region_id = province_idx;
        rdelta.addiction_rate_delta = addiction_rate_delta;
        province_delta.region_deltas.push_back(rdelta);
    }
}

void AddictionModule::execute(const WorldState& state, DeltaBuffer& delta) {
    for (uint32_t i = 0; i < state.provinces.size(); ++i) {
        execute_province(i, state, delta);
    }
}

}  // namespace econlife
