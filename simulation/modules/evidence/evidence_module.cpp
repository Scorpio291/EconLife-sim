#include "modules/evidence/evidence_module.h"

#include "core/world_state/world_state.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"
#include "core/tick/deferred_work.h"

#include <algorithm>
#include <cmath>

namespace econlife {

// ---------------------------------------------------------------------------
// Static utility implementations
// ---------------------------------------------------------------------------

float EvidenceModule::compute_decay_amount(float base_decay_rate, bool is_credible,
                                            float discredit_multiplier, uint32_t batch_interval) {
    float multiplier = is_credible ? 1.0f : discredit_multiplier;
    return base_decay_rate * multiplier * static_cast<float>(batch_interval);
}

float EvidenceModule::apply_actionability_decay(float current_actionability,
                                                 float decay_amount, float actionability_floor) {
    float result = current_actionability - decay_amount;
    return std::max(result, actionability_floor);
}

bool EvidenceModule::evaluate_holder_credibility(float public_credibility,
                                                  float credibility_threshold) {
    return public_credibility >= credibility_threshold;
}

float EvidenceModule::normalize_trust_to_factor(float trust) {
    // Map trust from [-1.0, 1.0] range to [trust_factor_min, trust_factor_max].
    // Negative trust maps to minimum factor.
    if (trust <= 0.0f) return Constants::trust_factor_min;
    // Positive trust: linear map from (0, 1] -> [trust_factor_min, trust_factor_max]
    float factor = Constants::trust_factor_min +
                   trust * (Constants::trust_factor_max - Constants::trust_factor_min);
    return std::min(std::max(factor, Constants::trust_factor_min), Constants::trust_factor_max);
}

float EvidenceModule::compute_propagation_confidence(float sharer_confidence,
                                                      float relationship_trust) {
    float trust_factor = normalize_trust_to_factor(relationship_trust);
    float result = sharer_confidence * trust_factor;
    return std::max(0.0f, std::min(1.0f, result));
}

bool EvidenceModule::can_share_with_player(float trust, float share_threshold) {
    return trust >= share_threshold;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void EvidenceModule::process_decay_batches(const WorldState& state, DeltaBuffer& delta) {
    // Process evidence tokens sorted by id ascending for determinism.
    // Decay batch fires every batch_interval ticks per token.
    for (const auto& token : state.evidence_pool) {
        if (!token.is_active) continue;

        // Batch fires every batch_interval ticks since creation.
        uint32_t ticks_since_creation = state.current_tick - token.created_tick;
        if (ticks_since_creation == 0) continue;
        if (ticks_since_creation % Constants::batch_interval != 0) continue;

        // Evaluate holder credibility using social_capital as proxy.
        bool is_credible = true;  // default to credible
        for (const auto& npc : state.significant_npcs) {
            if (npc.id == token.source_npc_id) {
                float credibility = std::min(npc.social_capital / 100.0f, 1.0f);
                is_credible = evaluate_holder_credibility(credibility,
                                                          Constants::credibility_threshold);
                break;
            }
        }

        float decay = compute_decay_amount(Constants::base_decay_rate, is_credible,
                                            Constants::discredit_decay_multiplier,
                                            Constants::batch_interval);

        float new_actionability = apply_actionability_decay(
            token.actionability, decay, Constants::actionability_floor);

        float actionability_change = new_actionability - token.actionability;
        if (std::abs(actionability_change) > 0.0001f) {
            EvidenceDelta ed;
            ed.actionability_delta = actionability_change;
            delta.evidence_deltas.push_back(ed);
        }

        // Schedule next decay batch for this token via DeferredWorkQueue.
        // DWQ is conceptually write-only during a tick step; const_cast is
        // intentional here — same pattern as npc_behavior cross-province push.
        DeferredWorkItem dwi{};
        dwi.due_tick   = state.current_tick + Constants::batch_interval;
        dwi.type       = WorkType::evidence_decay_batch;
        dwi.subject_id = token.id;
        dwi.payload    = EvidenceDecayPayload{token.id};
        const_cast<WorldState&>(state).deferred_work_queue.push(dwi);
    }
}

void EvidenceModule::create_evidence_from_businesses(const WorldState& state,
                                                      DeltaBuffer& delta) {
    // Create evidence tokens from criminal businesses and regulatory violations.
    // Process businesses sorted by id ascending for determinism.
    std::vector<const NPCBusiness*> sorted_businesses;
    for (const auto& biz : state.npc_businesses) {
        sorted_businesses.push_back(&biz);
    }
    std::sort(sorted_businesses.begin(), sorted_businesses.end(),
              [](const NPCBusiness* a, const NPCBusiness* b) { return a->id < b->id; });

    for (const auto* biz : sorted_businesses) {
        if (biz->criminal_sector) {
            // Criminal business generates physical evidence token.
            EvidenceToken new_token{};
            new_token.id = next_token_id_++;
            new_token.type = EvidenceType::physical;
            new_token.source_npc_id = biz->owner_id;
            new_token.target_npc_id = biz->owner_id;
            new_token.actionability = Constants::criminal_evidence_actionability;
            new_token.decay_rate = Constants::base_decay_rate;
            new_token.created_tick = state.current_tick;
            new_token.province_id = biz->province_id;
            new_token.is_active = true;

            EvidenceDelta ed;
            ed.new_token = new_token;
            delta.evidence_deltas.push_back(ed);

            // Write observation memory to the source NPC — they are aware that
            // incriminating evidence has been generated by their activity.
            MemoryEntry mem{};
            mem.tick_timestamp  = state.current_tick;
            mem.type            = MemoryType::observation;
            mem.subject_id      = new_token.id;
            mem.emotional_weight = -0.3f;
            mem.decay           = 0.9f;
            mem.is_actionable   = false;

            NPCDelta npc_d{};
            npc_d.npc_id          = biz->owner_id;
            npc_d.new_memory_entry = mem;
            delta.npc_deltas.push_back(npc_d);
        } else if (biz->regulatory_violation_severity > 0.0f) {
            // Regulatory violation generates documentary evidence.
            EvidenceToken new_token{};
            new_token.id = next_token_id_++;
            new_token.type = EvidenceType::documentary;
            new_token.source_npc_id = biz->owner_id;
            new_token.target_npc_id = biz->owner_id;
            new_token.actionability = Constants::violation_evidence_actionability *
                                       biz->regulatory_violation_severity;
            new_token.decay_rate = Constants::base_decay_rate;
            new_token.created_tick = state.current_tick;
            new_token.province_id = biz->province_id;
            new_token.is_active = true;

            EvidenceDelta ed;
            ed.new_token = new_token;
            delta.evidence_deltas.push_back(ed);

            // Write observation memory to the source NPC.
            MemoryEntry mem{};
            mem.tick_timestamp  = state.current_tick;
            mem.type            = MemoryType::observation;
            mem.subject_id      = new_token.id;
            mem.emotional_weight = -0.3f;
            mem.decay           = 0.9f;
            mem.is_actionable   = false;

            NPCDelta npc_d{};
            npc_d.npc_id          = biz->owner_id;
            npc_d.new_memory_entry = mem;
            delta.npc_deltas.push_back(npc_d);
        }
    }
}

// ---------------------------------------------------------------------------
// execute — main tick entry point
// ---------------------------------------------------------------------------

void EvidenceModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Step 1: Process decay batches for existing tokens.
    process_decay_batches(state, delta);

    // Step 2: Create new evidence from business activities.
    create_evidence_from_businesses(state, delta);
}

}  // namespace econlife
