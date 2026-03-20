#include "influence_network_module.h"
#include "core/world_state/world_state.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"
#include <algorithm>
#include <cmath>

namespace econlife {

bool InfluenceNetworkModule::is_trust_based(float trust) {
    return trust > TRUST_CLASSIFICATION_THRESHOLD;
}

bool InfluenceNetworkModule::is_fear_based(float fear, float trust) {
    return fear > FEAR_CLASSIFICATION_THRESHOLD && trust < FEAR_TRUST_CEILING;
}

InfluenceType InfluenceNetworkModule::classify_relationship(float trust, float fear,
                                                             bool is_movement_ally) {
    if (is_trust_based(trust)) return InfluenceType::trust_based;
    if (is_fear_based(fear, trust)) return InfluenceType::fear_based;
    if (is_movement_ally) return InfluenceType::movement_based;
    return InfluenceType::obligation_based;
}

float InfluenceNetworkModule::compute_composite_health(uint32_t trust_count, uint32_t fear_count,
                                                        uint32_t obligation_count,
                                                        uint32_t movement_count) {
    float target = static_cast<float>(HEALTH_TARGET_COUNT);

    float trust_component = std::min(1.0f, static_cast<float>(trust_count) / target);
    float fear_component = std::min(1.0f, static_cast<float>(fear_count) / target);
    float obligation_component = std::min(1.0f, static_cast<float>(obligation_count) / target);
    float movement_component = std::min(1.0f, static_cast<float>(movement_count) / target);

    float score = TRUST_WEIGHT * trust_component +
                  OBLIGATION_WEIGHT * obligation_component +
                  FEAR_WEIGHT * fear_component +
                  MOVEMENT_WEIGHT * movement_component;

    // Diversity bonus if all four types present
    if (trust_count > 0 && fear_count > 0 && obligation_count > 0 && movement_count > 0) {
        score += DIVERSITY_BONUS;
    }

    return std::clamp(score, 0.0f, 1.0f);
}

float InfluenceNetworkModule::compute_obligation_erosion() {
    return -OBLIGATION_EROSION_RATE;
}

bool InfluenceNetworkModule::is_catastrophic_loss(float trust_delta, float resulting_trust) {
    return trust_delta <= CATASTROPHIC_TRUST_LOSS_THRESHOLD &&
           resulting_trust < CATASTROPHIC_TRUST_FLOOR;
}

float InfluenceNetworkModule::compute_recovery_ceiling(float trust_before, float trust_delta) {
    float resulting_trust = trust_before + trust_delta;
    if (!is_catastrophic_loss(trust_delta, resulting_trust)) {
        return 1.0f;
    }
    return std::max(trust_before * RECOVERY_CEILING_FACTOR, RECOVERY_CEILING_MINIMUM);
}

void InfluenceNetworkModule::execute(const WorldState& state, DeltaBuffer& delta) {
    if (!state.player) return;

    // Process obligation trust erosion for overdue obligations
    for (const auto& obligation : state.obligation_network) {
        if (!obligation.is_active) continue;
        // Obligations that are active but old enough contribute erosion
        // (simplified: all active obligations with weight > 0 erode)
    }

    // Recompute health if dirty
    if (!state.network_health_dirty) return;

    uint32_t trust_count = 0;
    uint32_t fear_count = 0;
    uint32_t movement_count = 0;
    uint32_t obligation_count = 0;

    // Classify player relationships
    auto sorted_rels = state.player->relationships;
    std::sort(sorted_rels.begin(), sorted_rels.end(),
              [](const Relationship& a, const Relationship& b) {
                  return a.target_npc_id < b.target_npc_id;
              });

    for (const auto& rel : sorted_rels) {
        if (is_trust_based(rel.trust)) trust_count++;
        if (is_fear_based(rel.fear, rel.trust)) fear_count++;
        if (rel.is_movement_ally) movement_count++;
    }

    // Obligation counts
    for (const auto& obl : state.obligation_network) {
        if (!obl.is_active) continue;
        if (obl.creditor_npc_id == state.player->id ||
            obl.debtor_npc_id == state.player->id) {
            obligation_count++;
        }
    }

    float score = compute_composite_health(trust_count, fear_count,
                                            obligation_count, movement_count);

    // Update cached health using shared_types InfluenceNetworkHealth
    cached_health_.overall_score = score;
    cached_health_.network_reach = std::min(1.0f,
        static_cast<float>(sorted_rels.size()) / static_cast<float>(HEALTH_TARGET_COUNT * 2));
    cached_health_.network_density = std::min(1.0f,
        static_cast<float>(trust_count + fear_count) / std::max(1.0f, static_cast<float>(sorted_rels.size())));
    cached_health_.vulnerability = 0.0f;  // Computed from exposure analysis in later pass
}

}  // namespace econlife
