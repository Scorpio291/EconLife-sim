#include "influence_network_module.h"

#include <algorithm>
#include <cmath>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"

namespace econlife {

InfluenceNetworkModule::InfluenceNetworkModule(const InfluenceNetworkConfig& cfg) : cfg_(cfg) {}

bool InfluenceNetworkModule::is_trust_based(float trust, float threshold) {
    return trust > threshold;
}

bool InfluenceNetworkModule::is_fear_based(float fear, float trust, float fear_threshold,
                                           float fear_trust_ceiling) {
    return fear > fear_threshold && trust < fear_trust_ceiling;
}

InfluenceType InfluenceNetworkModule::classify_relationship(float trust, float fear,
                                                            bool is_movement_ally,
                                                            float trust_threshold,
                                                            float fear_threshold,
                                                            float fear_trust_ceiling) {
    if (is_trust_based(trust, trust_threshold))
        return InfluenceType::trust_based;
    if (is_fear_based(fear, trust, fear_threshold, fear_trust_ceiling))
        return InfluenceType::fear_based;
    if (is_movement_ally)
        return InfluenceType::movement_based;
    return InfluenceType::obligation_based;
}

float InfluenceNetworkModule::compute_composite_health(
    uint32_t trust_count, uint32_t fear_count, uint32_t obligation_count, uint32_t movement_count,
    uint32_t health_target_count, float trust_weight, float obligation_weight, float fear_weight,
    float movement_weight, float diversity_bonus) {
    float target = static_cast<float>(health_target_count);

    float trust_component = std::min(1.0f, static_cast<float>(trust_count) / target);
    float fear_component = std::min(1.0f, static_cast<float>(fear_count) / target);
    float obligation_component = std::min(1.0f, static_cast<float>(obligation_count) / target);
    float movement_component = std::min(1.0f, static_cast<float>(movement_count) / target);

    float score = trust_weight * trust_component + obligation_weight * obligation_component +
                  fear_weight * fear_component + movement_weight * movement_component;

    // Diversity bonus if all four types present
    if (trust_count > 0 && fear_count > 0 && obligation_count > 0 && movement_count > 0) {
        score += diversity_bonus;
    }

    return std::clamp(score, 0.0f, 1.0f);
}

float InfluenceNetworkModule::compute_obligation_erosion(float obligation_erosion_rate) {
    return -obligation_erosion_rate;
}

bool InfluenceNetworkModule::is_catastrophic_loss(float trust_delta, float resulting_trust,
                                                  float catastrophic_trust_loss_threshold,
                                                  float catastrophic_trust_floor) {
    return trust_delta <= catastrophic_trust_loss_threshold &&
           resulting_trust < catastrophic_trust_floor;
}

float InfluenceNetworkModule::compute_recovery_ceiling(float trust_before, float trust_delta,
                                                       float catastrophic_trust_loss_threshold,
                                                       float catastrophic_trust_floor,
                                                       float recovery_ceiling_factor,
                                                       float recovery_ceiling_minimum) {
    float resulting_trust = trust_before + trust_delta;
    if (!is_catastrophic_loss(trust_delta, resulting_trust, catastrophic_trust_loss_threshold,
                              catastrophic_trust_floor)) {
        return 1.0f;
    }
    return std::max(trust_before * recovery_ceiling_factor, recovery_ceiling_minimum);
}

void InfluenceNetworkModule::execute(const WorldState& state, DeltaBuffer& delta) {
    if (!state.player)
        return;

    // Process obligation trust erosion for active obligations involving the player.
    // Each active obligation erodes the obligation_balance in the corresponding relationship.
    for (const auto& obligation : state.obligation_network) {
        if (!obligation.is_active)
            continue;
        if (obligation.weight <= 0.0f)
            continue;

        // Identify the NPC counterpart to the player in this obligation
        uint32_t counterpart_npc_id = 0;
        if (obligation.debtor_npc_id == state.player->id) {
            counterpart_npc_id = obligation.creditor_npc_id;
        } else if (obligation.creditor_npc_id == state.player->id) {
            counterpart_npc_id = obligation.debtor_npc_id;
        }
        if (counterpart_npc_id == 0)
            continue;

        // Find the current relationship to build an updated copy
        const Relationship* current_rel = nullptr;
        for (const auto& rel : state.player->relationships) {
            if (rel.target_npc_id == counterpart_npc_id) {
                current_rel = &rel;
                break;
            }
        }
        if (!current_rel)
            continue;

        // Apply obligation erosion to obligation_balance and trust
        float erosion = compute_obligation_erosion(cfg_.obligation_erosion_rate);
        Relationship updated_rel = *current_rel;
        updated_rel.obligation_balance =
            std::clamp(updated_rel.obligation_balance + erosion, -1.0f, 1.0f);
        // Small trust decay from sustained obligation weight
        float trust_decay = -cfg_.obligation_erosion_rate * obligation.weight;
        updated_rel.trust = std::clamp(updated_rel.trust + trust_decay, 0.0f, 1.0f);
        updated_rel.last_interaction_tick = state.current_tick;

        NPCDelta nd;
        nd.npc_id = state.player->id;
        nd.updated_relationship = updated_rel;
        delta.npc_deltas.push_back(nd);

        // Counterpart NPC: motivation_delta reflects continued influence hold
        // (fear-based obligations reinforce compliance motivation)
        InfluenceType inf_type = classify_relationship(
            current_rel->trust, current_rel->fear, current_rel->is_movement_ally,
            cfg_.trust_classification_threshold, cfg_.fear_classification_threshold,
            cfg_.fear_trust_ceiling);
        if (inf_type == InfluenceType::fear_based || inf_type == InfluenceType::obligation_based) {
            NPCDelta counterpart_nd;
            counterpart_nd.npc_id = counterpart_npc_id;
            // Small positive motivation_delta: NPC feels compelled to comply
            counterpart_nd.motivation_delta = obligation.weight * 0.01f;
            delta.npc_deltas.push_back(counterpart_nd);
        }
    }

    // Recompute health if dirty
    if (!state.network_health_dirty)
        return;

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
        if (is_trust_based(rel.trust, cfg_.trust_classification_threshold))
            trust_count++;
        if (is_fear_based(rel.fear, rel.trust, cfg_.fear_classification_threshold,
                          cfg_.fear_trust_ceiling))
            fear_count++;
        if (rel.is_movement_ally)
            movement_count++;

        // Write motivation_delta for NPCs in trust-based relationships: they
        // receive a small positive nudge reflecting the player's influence.
        InfluenceType inf_type = classify_relationship(
            rel.trust, rel.fear, rel.is_movement_ally, cfg_.trust_classification_threshold,
            cfg_.fear_classification_threshold, cfg_.fear_trust_ceiling);
        if (inf_type == InfluenceType::trust_based) {
            NPCDelta nd;
            nd.npc_id = rel.target_npc_id;
            nd.motivation_delta = rel.trust * 0.005f;
            delta.npc_deltas.push_back(nd);
        }
    }

    // Obligation counts
    for (const auto& obl : state.obligation_network) {
        if (!obl.is_active)
            continue;
        if (obl.creditor_npc_id == state.player->id || obl.debtor_npc_id == state.player->id) {
            obligation_count++;
        }
    }

    float score = compute_composite_health(
        trust_count, fear_count, obligation_count, movement_count, cfg_.health_target_count,
        cfg_.trust_weight, cfg_.obligation_weight, cfg_.fear_weight, cfg_.movement_weight,
        cfg_.diversity_bonus);

    // Update cached health using shared_types InfluenceNetworkHealth
    cached_health_.overall_score = score;
    cached_health_.network_reach =
        std::min(1.0f, static_cast<float>(sorted_rels.size()) /
                           static_cast<float>(cfg_.health_target_count * 2));
    cached_health_.network_density =
        std::min(1.0f, static_cast<float>(trust_count + fear_count) /
                           std::max(1.0f, static_cast<float>(sorted_rels.size())));
    cached_health_.vulnerability = 0.0f;  // Computed from exposure analysis in later pass
}

}  // namespace econlife
