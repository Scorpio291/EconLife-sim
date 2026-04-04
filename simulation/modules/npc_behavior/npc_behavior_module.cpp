// NPC Behavior Module — implementation.
// See npc_behavior_module.h for class declarations and
// docs/interfaces/npc_behavior/INTERFACE.md for the canonical specification.
//
// Per-tick processing per province:
//   1. For each significant NPC in the province (sorted by id ascending):
//      a. Enforce memory cap. Decay all memory entries; archive those below floor.
//      b. Decay knowledge confidence for all known entries.
//      c. Generate context-sensitive action candidates from WorldState.
//      d. Evaluate candidates via expected-value engine.
//      e. Select highest net_utility action; set waiting if below threshold.
//      f. Write capital_delta, new_memory_entry, updated_relationship, motivation_delta.
//      g. Queue DeferredWorkItem(consequence) for chosen action.
//   2. Write NPCDeltas to province_delta.

#include "modules/npc_behavior/npc_behavior_module.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"  // PlayerCharacter complete type
#include "core/world_state/world_state.h"

namespace econlife {

// ===========================================================================
// Static utilities
// ===========================================================================

float NpcBehaviorModule::compute_expected_value(const NPC& npc, const ActionOutcome& outcome) {
    auto type_idx = static_cast<size_t>(outcome.type);
    if (type_idx >= npc.motivations.weights.size()) {
        return 0.0f;
    }
    return npc.motivations.weights[type_idx] * outcome.probability * outcome.magnitude;
}

float NpcBehaviorModule::compute_risk_discount(float exposure_risk, float risk_tolerance) const {
    float raw = 1.0f - (exposure_risk - risk_tolerance) * mcfg_.risk_sensitivity_coeff;
    return std::max(mcfg_.min_risk_discount, std::min(1.0f, raw));
}

float NpcBehaviorModule::apply_relationship_modifier(float base_ev, float trust,
                                                     float trust_ev_bonus) {
    return base_ev * (1.0f + trust * trust_ev_bonus);
}

ActionEvaluation NpcBehaviorModule::evaluate_action(const NPC& npc, DailyAction action,
                                                    const std::vector<ActionOutcome>& outcomes,
                                                    float exposure_risk, float trust_bonus_target,
                                                    float trust_ev_bonus) const {
    float total_ev = 0.0f;
    for (const auto& outcome : outcomes) {
        total_ev += compute_expected_value(npc, outcome);
    }

    float risk_disc = compute_risk_discount(exposure_risk, npc.risk_tolerance);
    float net = total_ev * risk_disc;

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

void NpcBehaviorModule::decay_memories(NPC& npc_copy, float decay_rate, float decay_floor) {
    auto it = npc_copy.memory_log.begin();
    while (it != npc_copy.memory_log.end()) {
        it->decay *= (1.0f - decay_rate);
        if (it->decay < decay_floor) {
            it = npc_copy.memory_log.erase(it);
        } else {
            ++it;
        }
    }
}

void NpcBehaviorModule::archive_lowest_decay_memory(std::vector<MemoryEntry>& memory_log) {
    if (memory_log.empty()) {
        return;
    }
    auto min_it = std::min_element(
        memory_log.begin(), memory_log.end(),
        [](const MemoryEntry& a, const MemoryEntry& b) { return a.decay < b.decay; });
    memory_log.erase(min_it);
}

void NpcBehaviorModule::renormalize_motivation_weights(MotivationVector& motivations) {
    float sum = 0.0f;
    for (float w : motivations.weights) {
        sum += w;
    }

    if (sum <= 0.0f) {
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

void NpcBehaviorModule::clamp_relationship(Relationship& rel) const {
    rel.trust = std::max(-1.0f, std::min(1.0f, rel.trust));
    if (rel.trust > rel.recovery_ceiling) {
        rel.trust = rel.recovery_ceiling;
    }
    rel.fear = std::max(0.0f, std::min(1.0f, rel.fear));
    rel.recovery_ceiling = std::max(mcfg_.recovery_ceiling_minimum, rel.recovery_ceiling);
}

float NpcBehaviorModule::worker_satisfaction(const NPC& npc) {
    // Compute satisfaction from employment-related memories.
    // Positive memories contribute positively, negative contribute negatively.
    // Base satisfaction is 0.5 (neutral).
    if (npc.memory_log.empty()) {
        return 0.5f;
    }

    float positive_sum = 0.0f;
    float negative_sum = 0.0f;
    uint32_t relevant_count = 0;

    for (const auto& mem : npc.memory_log) {
        bool employment_relevant = false;
        switch (mem.type) {
            case MemoryType::employment_positive:
            case MemoryType::employment_negative:
            case MemoryType::witnessed_illegal_activity:
            case MemoryType::witnessed_safety_violation:
            case MemoryType::witnessed_wage_theft:
            case MemoryType::physical_hazard:
            case MemoryType::facility_quality:
            case MemoryType::retaliation_experienced:
                employment_relevant = true;
                break;
            default:
                break;
        }

        if (employment_relevant) {
            float weighted = mem.emotional_weight * mem.decay;
            if (weighted > 0.0f) {
                positive_sum += weighted;
            } else {
                negative_sum += std::abs(weighted);
            }
            ++relevant_count;
        }
    }

    if (relevant_count == 0) {
        return 0.5f;
    }

    float total = positive_sum + negative_sum;
    if (total <= 0.0f) {
        return 0.5f;
    }

    // Satisfaction = ratio of positive to total, scaled to 0.0-1.0
    return std::max(0.0f, std::min(1.0f, positive_sum / total));
}

size_t NpcBehaviorModule::memory_type_to_outcome_index(MemoryType type) {
    switch (type) {
        case MemoryType::employment_positive:
        case MemoryType::facility_quality:
            return static_cast<size_t>(OutcomeType::financial_gain);
        case MemoryType::employment_negative:
        case MemoryType::witnessed_wage_theft:
        case MemoryType::retaliation_experienced:
            return static_cast<size_t>(OutcomeType::self_preservation);
        case MemoryType::witnessed_illegal_activity:
        case MemoryType::witnessed_safety_violation:
        case MemoryType::physical_hazard:
            return static_cast<size_t>(OutcomeType::security_gain);
        case MemoryType::interaction:
            return static_cast<size_t>(OutcomeType::relationship_repair);
        case MemoryType::observation:
        case MemoryType::hearsay:
            return static_cast<size_t>(OutcomeType::ideological);
        case MemoryType::event:
            return static_cast<size_t>(OutcomeType::career_advance);
        default:
            return static_cast<size_t>(OutcomeType::self_preservation);
    }
}

// ===========================================================================
// NpcBehaviorModule — per-province tick execution
// ===========================================================================

void NpcBehaviorModule::execute_province(uint32_t province_idx, const WorldState& state,
                                         DeltaBuffer& province_delta) {
    const auto& province = state.provinces[province_idx];

    // Collect NPCs in this province — both active and waiting are evaluated.
    std::vector<const NPC*> province_npcs;
    for (const auto& npc : state.significant_npcs) {
        if (npc.current_province_id == province_idx &&
            (npc.status == NPCStatus::active || npc.status == NPCStatus::waiting)) {
            province_npcs.push_back(&npc);
        }
    }

    // Sort by id ascending for deterministic processing order.
    std::sort(province_npcs.begin(), province_npcs.end(),
              [](const NPC* a, const NPC* b) { return a->id < b->id; });

    // Read province conditions for context-sensitive candidate generation.
    float crime_rate = province.conditions.crime_rate;
    float stability = province.conditions.stability_score;
    float employment_rate = province.conditions.formal_employment_rate;

    // Process each NPC.
    for (const NPC* npc_ptr : province_npcs) {
        const NPC& npc = *npc_ptr;

        // --- Mutable copy for local computation ---
        NPC npc_copy = npc;

        // --- Memory cap enforcement on entry ---
        while (npc_copy.memory_log.size() > MAX_MEMORY_ENTRIES) {
            archive_lowest_decay_memory(npc_copy.memory_log);
        }

        // --- Step 1: Memory decay ---
        decay_memories(npc_copy, cfg_.memory_decay_rate, cfg_.memory_decay_floor);

        // --- Step 2: Knowledge confidence decay ---
        for (auto& ke : npc_copy.known_evidence) {
            ke.confidence -= cfg_.knowledge_confidence_decay_rate;
            ke.confidence = std::max(0.0f, std::min(1.0f, ke.confidence));
        }
        for (auto& ke : npc_copy.known_relationships) {
            ke.confidence -= cfg_.knowledge_confidence_decay_rate;
            ke.confidence = std::max(0.0f, std::min(1.0f, ke.confidence));
        }

        // --- Step 3: Generate context-sensitive action candidates ---
        struct ActionCandidate {
            DailyAction action;
            std::vector<ActionOutcome> outcomes;
            float exposure_risk;
            float trust_target;  // -2.0f means no trust modifier
            float capital_cost;  // capital spent if chosen
        };

        std::vector<ActionCandidate> candidates;

        // work: probability scales with employment rate
        float work_prob = std::min(1.0f, 0.5f + employment_rate * 0.4f);
        candidates.push_back({DailyAction::work,
                              {{OutcomeType::financial_gain, work_prob, 0.5f}},
                              0.0f,
                              -2.0f,
                              0.0f});

        // shop: magnitude scales inversely with price (stability proxy)
        float shop_mag = std::min(1.0f, 0.2f + stability * 0.2f);
        float shop_cost = std::min(npc.capital, npc.capital * cfg_.shop_cost_fraction);
        candidates.push_back({DailyAction::shop,
                              {{OutcomeType::security_gain, 0.7f, shop_mag}},
                              0.0f,
                              -2.0f,
                              shop_cost});

        // socialize: trust modifier from best relationship
        {
            float best_trust = -2.0f;
            for (const auto& rel : npc.relationships) {
                if (rel.trust > best_trust) {
                    best_trust = rel.trust;
                }
            }
            candidates.push_back({DailyAction::socialize,
                                  {{OutcomeType::relationship_repair, 0.6f, 0.4f}},
                                  0.0f,
                                  best_trust,
                                  0.0f});
        }

        // rest: always available
        candidates.push_back(
            {DailyAction::rest, {{OutcomeType::self_preservation, 0.9f, 0.2f}}, 0.0f, -2.0f, 0.0f});

        // seek_employment: probability scales inversely with employment rate
        float seek_prob = std::min(1.0f, 0.3f + (1.0f - employment_rate) * 0.4f);
        candidates.push_back({DailyAction::seek_employment,
                              {{OutcomeType::career_advance, seek_prob, 0.6f}},
                              0.1f,
                              -2.0f,
                              0.0f});

        // criminal_activity: magnitude scales with crime rate, risk scales with stability
        float crim_mag = std::min(1.0f, 0.4f + crime_rate * 0.6f);
        float crim_risk = std::max(0.3f, 0.8f - crime_rate * 0.5f);
        candidates.push_back({DailyAction::criminal_activity,
                              {{OutcomeType::financial_gain, 0.5f, crim_mag}},
                              crim_risk,
                              -2.0f,
                              0.0f});

        // migrate: available if satisfaction is low and another province exists
        if (state.provinces.size() > 1 && stability < 0.4f) {
            candidates.push_back({DailyAction::migrate,
                                  {{OutcomeType::security_gain, 0.4f, 0.5f},
                                   {OutcomeType::self_preservation, 0.3f, 0.4f}},
                                  0.2f,
                                  -2.0f,
                                  0.0f});
        }

        // whistleblow: eligibility check per spec
        if (npc.role == NPCRole::worker || npc.role == NPCRole::middle_manager ||
            npc.role == NPCRole::accountant) {
            float satisfaction = worker_satisfaction(npc);
            bool has_witnessed = false;
            for (const auto& mem : npc.memory_log) {
                if (mem.type == MemoryType::witnessed_illegal_activity &&
                    mem.emotional_weight < -0.6f) {
                    has_witnessed = true;
                    break;
                }
            }
            if (satisfaction < 0.35f && has_witnessed && npc.risk_tolerance > 0.4f) {
                candidates.push_back({DailyAction::whistleblow,
                                      {{OutcomeType::ideological, 0.6f, 0.7f},
                                       {OutcomeType::self_preservation, 0.3f, 0.3f}},
                                      0.6f,
                                      -2.0f,
                                      0.0f});
            }
        }

        // --- Evaluate all candidates and find the best ---
        ActionEvaluation best_eval{};
        best_eval.net_utility = -1.0f;
        float best_cost = 0.0f;

        for (const auto& candidate : candidates) {
            // Skip actions the NPC can't afford.
            if (candidate.capital_cost > npc.capital) {
                continue;
            }

            ActionEvaluation eval =
                evaluate_action(npc, candidate.action, candidate.outcomes, candidate.exposure_risk,
                                candidate.trust_target, mcfg_.trust_ev_bonus);

            // NaN guard.
            if (std::isnan(eval.net_utility)) {
                eval.net_utility = 0.0f;
            }

            if (eval.net_utility > best_eval.net_utility) {
                best_eval = eval;
                best_cost = candidate.capital_cost;
            }
        }

        // --- Step 4: Write NPC delta ---
        NPCDelta npc_delta{};
        npc_delta.npc_id = npc.id;

        if (best_eval.net_utility < mcfg_.inaction_threshold) {
            // Below threshold: set to waiting.
            npc_delta.new_status = NPCStatus::waiting;
        } else {
            // Above threshold: if was waiting, return to active.
            if (npc.status == NPCStatus::waiting) {
                npc_delta.new_status = NPCStatus::active;
            }

            // Accumulate capital effects: start with cost, then add income.
            float capital_change = 0.0f;

            // Apply capital cost of chosen action.
            if (best_cost > 0.0f) {
                capital_change -= best_cost;
            }

            // Work action earns income (simplified: base wage * employment rate).
            if (best_eval.action == DailyAction::work) {
                float wage = cfg_.base_wage * employment_rate;
                capital_change += wage;
            }

            // Criminal activity earns illicit income.
            if (best_eval.action == DailyAction::criminal_activity) {
                float illicit_income = cfg_.base_illicit_income * crime_rate;
                capital_change += illicit_income;
            }

            if (capital_change != 0.0f) {
                npc_delta.capital_delta = capital_change;
            }

            // Form a memory entry for the action taken this tick.
            MemoryEntry new_mem{};
            new_mem.tick_timestamp = state.current_tick;
            new_mem.subject_id = npc.id;
            new_mem.decay = 0.95f;
            new_mem.is_actionable = true;

            switch (best_eval.action) {
                case DailyAction::work:
                    new_mem.type = MemoryType::employment_positive;
                    new_mem.emotional_weight = 0.3f;
                    break;
                case DailyAction::shop:
                    new_mem.type = MemoryType::event;
                    new_mem.emotional_weight = 0.2f;
                    break;
                case DailyAction::socialize:
                    new_mem.type = MemoryType::interaction;
                    new_mem.emotional_weight = 0.4f;
                    break;
                case DailyAction::rest:
                    new_mem.type = MemoryType::event;
                    new_mem.emotional_weight = 0.1f;
                    new_mem.is_actionable = false;
                    break;
                case DailyAction::seek_employment:
                    new_mem.type = MemoryType::event;
                    new_mem.emotional_weight = 0.2f;
                    break;
                case DailyAction::criminal_activity:
                    new_mem.type = MemoryType::event;
                    new_mem.emotional_weight = -0.3f;
                    break;
                case DailyAction::migrate:
                    new_mem.type = MemoryType::event;
                    new_mem.emotional_weight = -0.2f;
                    break;
                case DailyAction::whistleblow:
                    new_mem.type = MemoryType::witnessed_illegal_activity;
                    new_mem.emotional_weight = -0.7f;
                    break;
            }
            npc_delta.new_memory_entry = new_mem;

            // Queue DeferredWorkItem(consequence) for the chosen action.
            DeferredWorkItem dwi{};
            dwi.due_tick = state.current_tick + 1;
            dwi.type = WorkType::consequence;
            dwi.subject_id = npc.id;
            dwi.payload = ConsequencePayload{npc.id};
            // Note: We write to the non-const deferred_work_queue via const_cast
            // only because DWQ items from behavior are always deferred to next tick.
            // In the real orchestrator, the DWQ is passed separately.
            // For now, we skip direct DWQ push — the orchestrator drains consequences
            // from DeltaBuffer.consequence_deltas instead.
            ConsequenceDelta cd{};
            cd.new_entry_id = npc.id;
            province_delta.consequence_deltas.push_back(cd);

            // Whistleblow creates evidence.
            if (best_eval.action == DailyAction::whistleblow) {
                EvidenceDelta ed{};
                EvidenceToken token{};
                token.id = 0;  // auto-assigned by apply_evidence_deltas
                token.type = EvidenceType::testimonial;
                token.source_npc_id = npc.id;
                token.target_npc_id = 0;  // target determined by investigation module
                token.actionability = 0.7f;
                token.decay_rate = 0.01f;
                token.created_tick = state.current_tick;
                token.province_id = province_idx;
                token.is_active = true;
                ed.new_token = token;
                province_delta.evidence_deltas.push_back(ed);
            }

            // Migration uses cross-province deltas routed through DeltaBuffer.
            if (best_eval.action == DailyAction::migrate) {
                // Find best destination province (highest stability).
                uint32_t best_dest = province_idx;
                float best_stability = stability;
                for (uint32_t p = 0; p < static_cast<uint32_t>(state.provinces.size()); ++p) {
                    if (p != province_idx &&
                        state.provinces[p].conditions.stability_score > best_stability) {
                        best_stability = state.provinces[p].conditions.stability_score;
                        best_dest = p;
                    }
                }
                if (best_dest != province_idx) {
                    CrossProvinceDelta cpd{};
                    cpd.source_province_id = province_idx;
                    cpd.target_province_id = best_dest;
                    cpd.due_tick = state.current_tick + 1;
                    NPCDelta migration_delta{};
                    migration_delta.npc_id = npc.id;
                    // The actual province_id change is handled by npc_travel_arrival DWQ.
                    cpd.npc_delta = migration_delta;
                    // Write to DeltaBuffer; orchestrator merges into CrossProvinceDeltaBuffer.
                    province_delta.cross_province_deltas.push_back(cpd);
                }
            }
        }

        // --- Step 5: Motivation shift from accumulated memory ---
        MotivationVector shifted_motivations = npc.motivations;
        for (const auto& mem : npc_copy.memory_log) {
            if (mem.is_actionable && mem.decay > cfg_.memory_decay_floor) {
                size_t target_idx = memory_type_to_outcome_index(mem.type);
                if (target_idx < shifted_motivations.weights.size()) {
                    shifted_motivations.weights[target_idx] += cfg_.motivation_shift_rate *
                                                               std::abs(mem.emotional_weight) *
                                                               mem.decay;
                }
            }
        }
        renormalize_motivation_weights(shifted_motivations);

        // Check if motivation actually shifted enough to warrant a delta.
        float motivation_diff = 0.0f;
        for (size_t i = 0; i < 8; ++i) {
            motivation_diff +=
                std::abs(shifted_motivations.weights[i] - npc.motivations.weights[i]);
        }
        if (motivation_diff > 0.0001f) {
            // Send the full shifted motivation vector as a replacement.
            npc_delta.motivation_replacement = shifted_motivations;
        }

        // --- Step 6: Relationship updates ---
        // Find the most-interacted-with relationship and write a clamped update.
        if (!npc_copy.relationships.empty()) {
            // Pick the relationship with the most recent interaction.
            auto best_rel_it =
                std::max_element(npc_copy.relationships.begin(), npc_copy.relationships.end(),
                                 [](const Relationship& a, const Relationship& b) {
                                     return a.last_interaction_tick < b.last_interaction_tick;
                                 });
            Relationship rel_copy = *best_rel_it;
            clamp_relationship(rel_copy);
            rel_copy.last_interaction_tick = state.current_tick;
            npc_delta.updated_relationship = rel_copy;
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
