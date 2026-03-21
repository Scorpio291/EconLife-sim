#include "modules/obligation_network/obligation_network_module.h"

#include "core/world_state/world_state.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"

#include <algorithm>
#include <cmath>

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
    if (visible_net_worth <= 0.0f) return 0.0f;
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
    if (original_value <= 0.0f) return ObligationStatus::open;
    float ratio = current_demand / original_value;

    if (ratio > critical_threshold) return ObligationStatus::critical;
    if (ratio > escalation_threshold) return ObligationStatus::escalated;
    return ObligationStatus::open;
}

bool ObligationNetworkModule::should_trigger_hostile(ObligationStatus status,
                                                      float risk_tolerance,
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
    for (const auto& npc : state.significant_npcs) {
        if (npc.id == creditor_npc_id) return &npc;
    }
    return nullptr;
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
        player_net_worth, Constants::wealth_reference_scale, Constants::max_wealth_factor);

    for (auto& obl : obligation_states_) {
        // Skip resolved obligations.
        if (obl.status == ObligationStatus::fulfilled ||
            obl.status == ObligationStatus::forgiven ||
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
        float urgency = compute_creditor_urgency(
            creditor->motivations.weights.data(),
            creditor->motivations.weights.size());
        float growth = compute_demand_growth(urgency, Constants::escalation_rate_base,
                                              wealth_factor);
        obl.current_demand += growth;

        // Evaluate escalation status.
        ObligationStatus new_status = evaluate_escalation(
            obl.current_demand, obl.original_value,
            Constants::escalation_threshold, Constants::critical_threshold);

        // At most one status transition per tick.
        if (new_status > obl.status) {
            // Advance one step at a time.
            ObligationStatus next = static_cast<ObligationStatus>(
                static_cast<uint8_t>(obl.status) + 1);
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
                                    Constants::hostile_action_threshold)) {
            EscalationStep step;
            step.tick = state.current_tick;
            step.from_status = obl.status;
            step.to_status = ObligationStatus::hostile;
            obl.history.push_back(step);
            obl.status = ObligationStatus::hostile;
            went_hostile = true;

            // ConsequenceDelta: register hostile-action consequence entry
            ConsequenceDelta cdelta;
            cdelta.new_entry_id = obl.obligation_id;
            delta.consequence_deltas.push_back(cdelta);

            // new_obligation_nodes: publish a coercive counter-obligation from
            // creditor to debtor recording the hostile action (criminal_cooperation
            // favor type mirrors the hostile escalation)
            ObligationNode hostile_node;
            hostile_node.id           = 0;  // apply_deltas auto-assigns id
            hostile_node.creditor_npc_id = obl.creditor_npc_id;
            hostile_node.debtor_npc_id   = (state.player &&
                                             state.player->id != obl.creditor_npc_id)
                                            ? state.player->id
                                            : obl.creditor_npc_id;  // fallback
            hostile_node.favor_type   = FavorType::criminal_cooperation;
            hostile_node.weight       = std::clamp(
                obl.current_demand / std::max(1.0f, obl.original_value), 0.0f, 1.0f);
            hostile_node.created_tick = state.current_tick;
            hostile_node.is_active    = true;
            delta.new_obligation_nodes.push_back(hostile_node);
        }

        // Trust erosion for overdue obligation: write proper updated_relationship delta.
        uint32_t overdue_ticks = state.current_tick - obl.deadline_tick;
        if (overdue_ticks > 0) {
            float erosion = compute_trust_erosion(1u, Constants::trust_erosion_per_tick);

            // Find creditor's current relationship with the debtor (player) to
            // build the updated relationship struct.
            const Relationship* current_rel = nullptr;
            if (state.player) {
                for (const auto& rel : state.player->relationships) {
                    if (rel.target_npc_id == obl.creditor_npc_id) {
                        current_rel = &rel;
                        break;
                    }
                }
            }

            if (current_rel && state.player) {
                // Write proper updated_relationship (player's view of creditor) with
                // decreased trust — delta is owned by the player NPC entry
                NPCDelta nd;
                nd.npc_id = state.player->id;
                Relationship updated_rel = *current_rel;
                updated_rel.trust = std::clamp(
                    updated_rel.trust + erosion,  // erosion is negative
                    0.0f, updated_rel.recovery_ceiling);
                updated_rel.obligation_balance = std::clamp(
                    updated_rel.obligation_balance + erosion, -1.0f, 1.0f);
                updated_rel.last_interaction_tick = state.current_tick;
                if (went_hostile) {
                    // Hostile escalation lowers recovery ceiling
                    updated_rel.recovery_ceiling = std::max(
                        updated_rel.trust * 0.60f, 0.15f);
                }
                nd.updated_relationship = updated_rel;
                delta.npc_deltas.push_back(nd);
            } else {
                // No existing relationship record — write creditor motivation_delta
                // as a distrust signal until a relationship is established
                NPCDelta nd;
                nd.npc_id = obl.creditor_npc_id;
                nd.motivation_delta = erosion;
                delta.npc_deltas.push_back(nd);
            }
        }
    }
}

}  // namespace econlife
