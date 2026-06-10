#include "modules/obligation_network/obligation_network_module.h"

#include <algorithm>
#include <cmath>

#include "core/world_state/apply_deltas.h"  // lookup_npc_by_id
#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"

namespace econlife {

// ---------------------------------------------------------------------------
// Static utility implementations
// ---------------------------------------------------------------------------

float ObligationNetworkModule::compute_demand_growth(float creditor_urgency,
                                                     float escalation_rate_base,
                                                     float player_wealth_factor) {
    return creditor_urgency * escalation_rate_base * (1.0f + player_wealth_factor);
}

float ObligationNetworkModule::compute_player_wealth_factor(float visible_net_worth,
                                                            float wealth_reference_scale,
                                                            float max_wealth_factor) {
    if (visible_net_worth <= 0.0f)
        return 0.0f;
    float factor = visible_net_worth / wealth_reference_scale;
    return std::min(factor, max_wealth_factor);
}

float ObligationNetworkModule::compute_creditor_urgency(const float* motivation_weights,
                                                        size_t count) {
    float max_weight = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        if (motivation_weights[i] > max_weight) {
            max_weight = motivation_weights[i];
        }
    }
    return max_weight;
}

ObligationStatus ObligationNetworkModule::evaluate_escalation(float current_demand,
                                                              float original_value,
                                                              float escalation_threshold,
                                                              float critical_threshold) {
    if (original_value <= 0.0f)
        return ObligationStatus::open;
    float ratio = current_demand / original_value;

    if (ratio > critical_threshold)
        return ObligationStatus::critical;
    if (ratio > escalation_threshold)
        return ObligationStatus::escalated;
    return ObligationStatus::open;
}

bool ObligationNetworkModule::should_trigger_hostile(ObligationStatus status, float risk_tolerance,
                                                     float hostile_threshold) {
    return status == ObligationStatus::critical && risk_tolerance > hostile_threshold;
}

float ObligationNetworkModule::compute_trust_erosion(uint32_t overdue_ticks,
                                                     float trust_erosion_per_tick) {
    return static_cast<float>(overdue_ticks) * trust_erosion_per_tick;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

const NPC* ObligationNetworkModule::find_creditor(const WorldState& state,
                                                  uint32_t creditor_npc_id) const {
    return lookup_npc_by_id(state, creditor_npc_id);
}

// ---------------------------------------------------------------------------
// execute — main tick entry point
// ---------------------------------------------------------------------------

void ObligationNetworkModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Process obligations sorted by id ascending for determinism.
    std::sort(obligation_states_.begin(), obligation_states_.end(),
              [](const ObligationState& a, const ObligationState& b) {
                  return a.obligation_id < b.obligation_id;
              });

    // Compute player wealth factor (shared across all obligations).
    float player_net_worth = 0.0f;
    if (state.player) {
        player_net_worth = state.player->net_assets;
    }
    float wealth_factor = compute_player_wealth_factor(
        player_net_worth, cfg_.wealth_reference_scale, cfg_.max_wealth_factor);

    for (auto& obl : obligation_states_) {
        // Skip resolved obligations.
        if (obl.status == ObligationStatus::fulfilled || obl.status == ObligationStatus::forgiven ||
            obl.status == ObligationStatus::hostile) {
            continue;
        }

        // Find creditor NPC.
        const NPC* creditor = find_creditor(state, obl.creditor_npc_id);
        if (!creditor || creditor->status == NPCStatus::dead ||
            creditor->status == NPCStatus::fled) {
            // Dead/fled creditor: obligation frozen.
            continue;
        }

        // Check if obligation is overdue.
        if (state.current_tick <= obl.deadline_tick) {
            // Not yet overdue — no escalation.
            continue;
        }

        // Compute demand growth.
        float urgency = compute_creditor_urgency(creditor->motivations.weights.data(),
                                                 creditor->motivations.weights.size());
        float growth = compute_demand_growth(urgency, cfg_.escalation_rate_base, wealth_factor);
        obl.current_demand += growth;

        // Evaluate escalation status.
        ObligationStatus new_status =
            evaluate_escalation(obl.current_demand, obl.original_value, cfg_.escalation_threshold,
                                cfg_.critical_threshold);

        // At most one status transition per tick.
        if (new_status > obl.status) {
            // Advance one step at a time.
            ObligationStatus next =
                static_cast<ObligationStatus>(static_cast<uint8_t>(obl.status) + 1);
            EscalationStep step;
            step.tick = state.current_tick;
            step.from_status = obl.status;
            step.to_status = next;
            obl.history.push_back(step);
            obl.status = next;
        }

        // Check hostile trigger.
        bool went_hostile = false;
        if (should_trigger_hostile(obl.status, creditor->risk_tolerance,
                                   cfg_.hostile_action_threshold)) {
            EscalationStep step;
            step.tick = state.current_tick;
            step.from_status = obl.status;
            step.to_status = ObligationStatus::hostile;
            obl.history.push_back(step);
            obl.status = ObligationStatus::hostile;
            went_hostile = true;

            // ConsequenceDelta: register hostile-action consequence entry
            ConsequenceDelta cdelta;
            cdelta.new_consequence =
                make_consequence(obl.obligation_id, ConsequenceCategory::social_consequence,
                                 obl.creditor_npc_id, 0, 0, state.current_tick);
            delta.consequence_deltas.push_back(cdelta);

            // new_obligation_nodes: publish a coercive counter-obligation from
            // creditor to debtor recording the hostile action (criminal_cooperation
            // favor type mirrors the hostile escalation)
            ObligationNode hostile_node;
            hostile_node.id = 0;  // apply_deltas auto-assigns id
            hostile_node.creditor_npc_id = obl.creditor_npc_id;
            hostile_node.debtor_npc_id = (state.player && state.player->id != obl.creditor_npc_id)
                                             ? state.player->id
                                             : obl.creditor_npc_id;  // fallback
            hostile_node.favor_type = FavorType::criminal_cooperation;
            hostile_node.weight =
                std::clamp(obl.current_demand / std::max(1.0f, obl.original_value), 0.0f, 1.0f);
            hostile_node.created_tick = state.current_tick;
            hostile_node.is_active = true;
            delta.new_obligation_nodes.push_back(hostile_node);
        }

        // Trust erosion for overdue obligation, written as an upsert on the
        // player's relationship with the creditor. apply_deltas merges
        // trust/obligation_balance ADDITIVELY into an existing relationship
        // (the struct is initial values only on insert), so send the erosion
        // itself — not the eroded absolute value, which would double-count.
        uint32_t overdue_ticks = state.current_tick - obl.deadline_tick;
        if (overdue_ticks > 0 && state.player) {
            float erosion = compute_trust_erosion(1u, cfg_.trust_erosion_per_tick);

            Relationship rel_delta{};
            rel_delta.target_npc_id = obl.creditor_npc_id;
            rel_delta.trust = erosion;  // negative
            rel_delta.obligation_balance = erosion;
            rel_delta.last_interaction_tick = state.current_tick;
            rel_delta.recovery_ceiling = 1.0f;  // "no change" under ratchet merge

            if (went_hostile) {
                // Hostile escalation lowers the recovery ceiling (§13 floor
                // principle): 60% of post-erosion trust, floored at the 0.15
                // minimum. apply_deltas ratchets the ceiling down on merge.
                float current_trust = 0.0f;
                for (const auto& rel : state.player->relationships) {
                    if (rel.target_npc_id == obl.creditor_npc_id) {
                        current_trust = rel.trust;
                        break;
                    }
                }
                rel_delta.recovery_ceiling = std::max((current_trust + erosion) * 0.60f, 0.15f);
            }

            NPCDelta nd;
            nd.npc_id = state.player->id;
            nd.updated_relationship = rel_delta;
            delta.npc_deltas.push_back(nd);
        }
    }
}

}  // namespace econlife
