#include "trust_updates_module.h"

#include <algorithm>
#include <cmath>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"

namespace econlife {

float TrustUpdatesModule::apply_trust_delta(float current_trust, float delta,
                                            float recovery_ceiling) {
    float new_trust = current_trust + delta;

    // If gain, cap at recovery ceiling
    if (delta > 0.0f && new_trust > recovery_ceiling) {
        new_trust = recovery_ceiling;
    }

    return std::clamp(new_trust, TRUST_MIN, TRUST_MAX);
}

bool TrustUpdatesModule::is_catastrophic_loss(float trust_delta, float resulting_trust) {
    return trust_delta <= CATASTROPHIC_TRUST_LOSS_THRESHOLD ||
           (trust_delta < -0.3f && resulting_trust < CATASTROPHIC_TRUST_FLOOR);
}

float TrustUpdatesModule::compute_recovery_ceiling(float trust_before_loss) {
    return std::max(trust_before_loss * RECOVERY_CEILING_FACTOR, RECOVERY_CEILING_MINIMUM);
}

float TrustUpdatesModule::update_recovery_ceiling(float existing_ceiling, float new_ceiling) {
    return std::min(existing_ceiling, new_ceiling);
}

bool TrustUpdatesModule::is_significant_change(float trust_delta) {
    return std::abs(trust_delta) >= SIGNIFICANT_CHANGE_THRESHOLD;
}

void TrustUpdatesModule::execute_province(uint32_t province_idx, const WorldState& state,
                                          DeltaBuffer& province_delta) {
    if (province_idx >= state.provinces.size())
        return;

    const auto& province = state.provinces[province_idx];

    // Get NPC IDs sorted for deterministic processing
    auto npc_ids = province.significant_npc_ids;
    std::sort(npc_ids.begin(), npc_ids.end());

    for (uint32_t npc_id : npc_ids) {
        // Find NPC in significant_npcs
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

        // Process each relationship in target_npc_id order
        auto rels = npc->relationships;
        std::sort(rels.begin(), rels.end(), [](const Relationship& a, const Relationship& b) {
            return a.target_npc_id < b.target_npc_id;
        });

        for (const auto& rel : rels) {
            // Accumulate trust deltas from memory entries this tick
            float total_delta = 0.0f;
            for (const auto& memory : npc->memory_log) {
                if (memory.tick_timestamp != state.current_tick)
                    continue;
                if (memory.subject_id != rel.target_npc_id)
                    continue;
                total_delta += memory.emotional_weight * 0.1f;
            }

            if (std::abs(total_delta) < 1e-6f)
                continue;

            float resulting = rel.trust + total_delta;
            float new_trust = apply_trust_delta(rel.trust, total_delta, rel.recovery_ceiling);

            NPCDelta npc_delta;
            npc_delta.npc_id = npc_id;

            // Check catastrophic loss and update relationship
            Relationship updated_rel = rel;
            updated_rel.trust = new_trust;

            if (total_delta < 0.0f && is_catastrophic_loss(total_delta, resulting)) {
                float ceiling = compute_recovery_ceiling(rel.trust);
                updated_rel.recovery_ceiling =
                    update_recovery_ceiling(rel.recovery_ceiling, ceiling);
            }

            npc_delta.updated_relationship = updated_rel;

            // Significant change creates memory
            if (is_significant_change(total_delta)) {
                float weight = total_delta > 0 ? 0.3f : -0.4f;
                npc_delta.new_memory_entry = MemoryEntry{state.current_tick,
                                                         MemoryType::interaction,
                                                         rel.target_npc_id,
                                                         weight,
                                                         0.01f,
                                                         false};
            }

            province_delta.npc_deltas.push_back(npc_delta);
        }
    }
}

void TrustUpdatesModule::execute(const WorldState& state, DeltaBuffer& delta) {
    for (uint32_t i = 0; i < state.provinces.size(); ++i) {
        execute_province(i, state, delta);
    }
}

}  // namespace econlife
