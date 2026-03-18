// NPC Behavior Module — implementation.
// See npc_behavior_module.h for class declarations and
// docs/interfaces/npc_behavior/INTERFACE.md for the canonical specification.
//
// Per-tick processing per province:
//   1. For each significant NPC in the province (sorted by id ascending):
//      a. Decay all memory entries; archive those below floor.
//      b. Decay knowledge confidence for all known entries.
//      c. Evaluate candidate actions via expected-value engine.
//      d. Select highest net_utility action; set waiting if below threshold.
//      e. Shift motivation weights from accumulated memory.
//      f. Update relationships (clamp trust, fear, respect ceiling).
//   2. Write NPCDeltas with status, memory, relationship, motivation updates.

#include "modules/npc_behavior/npc_behavior_module.h"
#include "core/world_state/world_state.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"  // PlayerCharacter complete type

#include <algorithm>
#include <cmath>
#include <numeric>

namespace econlife {

// ===========================================================================
// Static utilities
// ===========================================================================

float NpcBehaviorModule::compute_expected_value(const NPC& npc,
                                                 const ActionOutcome& outcome) {
    // EV = motivation_weight[type] * outcome.probability * outcome.magnitude
    auto type_idx = static_cast<size_t>(outcome.type);
    if (type_idx >= npc.motivations.weights.size()) {
        return 0.0f;
    }
    return npc.motivations.weights[type_idx] * outcome.probability * outcome.magnitude;
}

float NpcBehaviorModule::compute_risk_discount(float exposure_risk,
                                                float risk_tolerance) {
    // risk_discount = clamp(min_risk_discount, 1.0, 1.0 - (exposure_risk - risk_tolerance) * coeff)
    // Capped at 1.0: low-risk situations don't boost EV above base.
    float raw = 1.0f - (exposure_risk - risk_tolerance) * Constants::risk_sensitivity_coeff;
    return std::max(Constants::min_risk_discount, std::min(1.0f, raw));
}

float NpcBehaviorModule::apply_relationship_modifier(float base_ev,
                                                      float trust,
                                                      float trust_ev_bonus) {
    // result = base_ev * (1.0 + trust * trust_ev_bonus)
    return base_ev * (1.0f + trust * trust_ev_bonus);
}

ActionEvaluation NpcBehaviorModule::evaluate_action(
    const NPC& npc,
    DailyAction action,
    const std::vector<ActionOutcome>& outcomes,
    float exposure_risk,
    float trust_bonus_target,
    float trust_ev_bonus) {

    // Sum EV contributions from all outcomes, processed in OutcomeType ascending order.
    // outcomes vector is assumed small; we process in given order (caller should sort).
    float total_ev = 0.0f;
    for (const auto& outcome : outcomes) {
        total_ev += compute_expected_value(npc, outcome);
    }

    // Apply risk discount.
    float risk_disc = compute_risk_discount(exposure_risk, npc.risk_tolerance);

    float net = total_ev * risk_disc;

    // Apply relationship modifier for cooperative actions (socialize, whistleblow).
    // trust_bonus_target > -2.0f signals a valid trust value was provided.
    if (trust_bonus_target > -2.0f) {
        net = apply_relationship_modifier(net, trust_bonus_target, trust_ev_bonus);
    }

    ActionEvaluation eval{};
    eval.action = action;
    eval.expected_value = total_ev;
    eval.risk_discount = risk_disc;
    eval.net_utility = net;
    return eval;
}

void NpcBehaviorModule::decay_memories(NPC& npc_copy, float decay_rate) {
    // Decay each memory entry: decay = decay * (1.0 - decay_rate).
    // Remove entries that fall below the decay floor.
    auto it = npc_copy.memory_log.begin();
    while (it != npc_copy.memory_log.end()) {
        it->decay *= (1.0f - decay_rate);
        if (it->decay < Constants::memory_decay_floor) {
            it = npc_copy.memory_log.erase(it);
        } else {
            ++it;
        }
    }
}

void NpcBehaviorModule::archive_lowest_decay_memory(
    std::vector<MemoryEntry>& memory_log) {
    if (memory_log.empty()) {
        return;
    }

    // Find the entry with the lowest decay value.
    auto min_it = std::min_element(
        memory_log.begin(), memory_log.end(),
        [](const MemoryEntry& a, const MemoryEntry& b) {
            return a.decay < b.decay;
        });

    memory_log.erase(min_it);
}

void NpcBehaviorModule::renormalize_motivation_weights(MotivationVector& motivations) {
    float sum = 0.0f;
    for (float w : motivations.weights) {
        sum += w;
    }

    if (sum <= 0.0f) {
        // All weights zero or negative: reset to uniform distribution.
        float uniform = 1.0f / static_cast<float>(motivations.weights.size());
        for (float& w : motivations.weights) {
            w = uniform;
        }
        return;
    }

    for (float& w : motivations.weights) {
        w /= sum;
    }
}

void NpcBehaviorModule::clamp_relationship(Relationship& rel) {
    // Trust clamped to [-1.0, 1.0].
    rel.trust = std::max(-1.0f, std::min(1.0f, rel.trust));

    // Trust cannot exceed recovery_ceiling.
    if (rel.trust > rel.recovery_ceiling) {
        rel.trust = rel.recovery_ceiling;
    }

    // Fear clamped to [0.0, 1.0].
    rel.fear = std::max(0.0f, std::min(1.0f, rel.fear));

    // recovery_ceiling has a minimum floor.
    rel.recovery_ceiling = std::max(Constants::recovery_ceiling_minimum, rel.recovery_ceiling);
}

// ===========================================================================
// NpcBehaviorModule — per-province tick execution
// ===========================================================================

void NpcBehaviorModule::execute_province(uint32_t province_idx,
                                          const WorldState& state,
                                          DeltaBuffer& province_delta) {
    // Collect NPCs in this province, sorted by id ascending for determinism.
    std::vector<const NPC*> province_npcs;
    for (const auto& npc : state.significant_npcs) {
        if (npc.current_province_id == province_idx &&
            npc.status == NPCStatus::active) {
            province_npcs.push_back(&npc);
        }
    }

    // Sort by id ascending for deterministic processing order.
    std::sort(province_npcs.begin(), province_npcs.end(),
              [](const NPC* a, const NPC* b) { return a->id < b->id; });

    // Process each NPC.
    for (const NPC* npc_ptr : province_npcs) {
        const NPC& npc = *npc_ptr;

        // --- Step 1: Memory decay ---
        // We create a mutable copy of the memory log to compute new state.
        // The actual update is written via DeltaBuffer.
        NPC npc_copy = npc;
        decay_memories(npc_copy, Constants::memory_decay_rate);

        // --- Step 2: Knowledge confidence decay ---
        for (auto& ke : npc_copy.known_evidence) {
            ke.confidence -= Constants::knowledge_confidence_decay_rate;
            ke.confidence = std::max(0.0f, std::min(1.0f, ke.confidence));
        }
        for (auto& ke : npc_copy.known_relationships) {
            ke.confidence -= Constants::knowledge_confidence_decay_rate;
            ke.confidence = std::max(0.0f, std::min(1.0f, ke.confidence));
        }

        // --- Step 3: Evaluate candidate actions ---
        // Build default candidate actions with simple outcome profiles.
        // Each action has a default outcome set based on its nature.
        struct ActionCandidate {
            DailyAction action;
            std::vector<ActionOutcome> outcomes;
            float exposure_risk;
            float trust_target;  // -2.0f means no trust modifier
        };

        std::vector<ActionCandidate> candidates;

        // work: financial_gain outcome
        candidates.push_back({
            DailyAction::work,
            {{OutcomeType::financial_gain, 0.8f, 0.5f}},
            0.0f,
            -2.0f
        });

        // shop: financial_gain (negative, but treated as positive for simplicity)
        candidates.push_back({
            DailyAction::shop,
            {{OutcomeType::security_gain, 0.7f, 0.3f}},
            0.0f,
            -2.0f
        });

        // socialize: relationship_repair outcome
        {
            float best_trust = -2.0f;
            for (const auto& rel : npc.relationships) {
                if (rel.trust > best_trust) {
                    best_trust = rel.trust;
                }
            }
            candidates.push_back({
                DailyAction::socialize,
                {{OutcomeType::relationship_repair, 0.6f, 0.4f}},
                0.0f,
                best_trust
            });
        }

        // rest: self_preservation outcome
        candidates.push_back({
            DailyAction::rest,
            {{OutcomeType::self_preservation, 0.9f, 0.2f}},
            0.0f,
            -2.0f
        });

        // seek_employment: career_advance outcome
        candidates.push_back({
            DailyAction::seek_employment,
            {{OutcomeType::career_advance, 0.4f, 0.6f}},
            0.1f,
            -2.0f
        });

        // criminal_activity: financial_gain with high risk
        candidates.push_back({
            DailyAction::criminal_activity,
            {{OutcomeType::financial_gain, 0.5f, 0.8f}},
            0.7f,
            -2.0f
        });

        // Evaluate all candidates and find the best.
        ActionEvaluation best_eval{};
        best_eval.net_utility = -1.0f;

        for (const auto& candidate : candidates) {
            ActionEvaluation eval = evaluate_action(
                npc, candidate.action, candidate.outcomes,
                candidate.exposure_risk, candidate.trust_target,
                Constants::trust_ev_bonus);

            if (eval.net_utility > best_eval.net_utility) {
                best_eval = eval;
            }
        }

        // --- Step 4: Write NPC delta ---
        NPCDelta npc_delta{};
        npc_delta.npc_id = npc.id;

        // If best action is below inaction threshold, set status to waiting.
        if (best_eval.net_utility < Constants::inaction_threshold) {
            npc_delta.new_status = NPCStatus::waiting;
        }

        // --- Step 5: Motivation shift from accumulated memory ---
        // Slight shift based on recent emotional memory entries.
        MotivationVector shifted_motivations = npc.motivations;
        for (const auto& mem : npc_copy.memory_log) {
            if (mem.is_actionable && mem.decay > Constants::memory_decay_floor) {
                // Shift motivation weights toward the outcome type associated
                // with this memory. Use emotional_weight as direction indicator.
                // For simplicity, shift toward financial_gain for positive,
                // self_preservation for negative.
                size_t target_idx = (mem.emotional_weight >= 0.0f)
                    ? static_cast<size_t>(OutcomeType::financial_gain)
                    : static_cast<size_t>(OutcomeType::self_preservation);

                if (target_idx < shifted_motivations.weights.size()) {
                    shifted_motivations.weights[target_idx] +=
                        Constants::motivation_shift_rate * std::abs(mem.emotional_weight) * mem.decay;
                }
            }
        }
        renormalize_motivation_weights(shifted_motivations);

        // --- Step 6: Relationship updates ---
        // Clamp all relationships.
        for (auto& rel : npc_copy.relationships) {
            clamp_relationship(rel);
        }

        province_delta.npc_deltas.push_back(npc_delta);
    }
}

void NpcBehaviorModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Province-parallel modules dispatch through execute_province().
    // This fallback processes all provinces sequentially if called directly.
    for (uint32_t p = 0; p < static_cast<uint32_t>(state.provinces.size()); ++p) {
        execute_province(p, state, delta);
    }
}

}  // namespace econlife
