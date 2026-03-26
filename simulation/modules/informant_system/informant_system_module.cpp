#include "informant_system_module.h"

#include <algorithm>
#include <cmath>

#include "core/rng/deterministic_rng.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"

namespace econlife {

float InformantSystemModule::compute_risk_factor(float risk_tolerance, float risk_factor_scale) {
    return (1.0f - risk_tolerance) * risk_factor_scale;
}

float InformantSystemModule::compute_trust_factor(float trust, float trust_factor_scale) {
    return (1.0f - trust) * trust_factor_scale;
}

float InformantSystemModule::compute_incrimination_suppression(uint32_t obligation_count,
                                                               float incrimination_suppression) {
    return static_cast<float>(obligation_count) * incrimination_suppression;
}

float InformantSystemModule::compute_compartmentalization_bonus(uint32_t level,
                                                                float compartment_bonus) {
    return static_cast<float>(level) * compartment_bonus;
}

float InformantSystemModule::compute_flip_probability(float base_flip_rate, float risk_tolerance,
                                                      float trust,
                                                      uint32_t mutual_incrimination_count,
                                                      uint32_t compartmentalization_level,
                                                      float max_flip_probability,
                                                      float risk_factor_scale,
                                                      float trust_factor_scale,
                                                      float incrimination_suppression,
                                                      float compartment_bonus_per_level) {
    float risk = compute_risk_factor(risk_tolerance, risk_factor_scale);
    float trust_f = compute_trust_factor(trust, trust_factor_scale);
    float incrim = compute_incrimination_suppression(mutual_incrimination_count,
                                                     incrimination_suppression);
    float compart = compute_compartmentalization_bonus(compartmentalization_level,
                                                       compartment_bonus_per_level);
    float prob = base_flip_rate + risk + trust_f - incrim - compart;
    return std::clamp(prob, 0.0f, max_flip_probability);
}

void InformantSystemModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Tick-level RNG seed: world_seed mixed with current tick, then forked per-NPC.
    DeterministicRNG tick_rng(state.world_seed ^ static_cast<uint64_t>(state.current_tick));

    std::sort(
        records_.begin(), records_.end(),
        [](const InformantRecord& a, const InformantRecord& b) { return a.npc_id < b.npc_id; });

    for (auto& rec : records_) {
        // Handle per-tick capital drain for actively cooperating informants
        if (rec.status == InformantStatus::cooperating) {
            NPCDelta ongoing_delta;
            ongoing_delta.npc_id = rec.npc_id;
            ongoing_delta.capital_delta = -cfg_.pay_silence_cost * 0.001f;  // tiny per-tick cost
            delta.npc_deltas.push_back(ongoing_delta);
            continue;
        }

        if (rec.status != InformantStatus::not_cooperating)
            continue;

        const NPC* npc = nullptr;
        for (const auto& n : state.significant_npcs) {
            if (n.id == rec.npc_id) {
                npc = &n;
                break;
            }
        }
        if (!npc || npc->status != NPCStatus::imprisoned)
            continue;

        float trust = 0.0f;
        for (const auto& rel : npc->relationships) {
            if (state.player && rel.target_npc_id == state.player->id) {
                trust = rel.trust;
                break;
            }
        }

        uint32_t mutual_count = 0;
        for (const auto& obl : state.obligation_network) {
            if (obl.is_active && (obl.creditor_npc_id == npc->id || obl.debtor_npc_id == npc->id) &&
                obl.favor_type == FavorType::criminal_cooperation) {
                mutual_count++;
            }
        }

        rec.flip_probability = compute_flip_probability(
            cfg_.base_flip_rate, npc->risk_tolerance, trust, mutual_count,
            rec.compartmentalization_level,
            cfg_.max_flip_probability, cfg_.risk_factor_scale, cfg_.trust_factor_scale,
            cfg_.incrimination_suppression, cfg_.compartment_bonus_per_level);

        // Probabilistic flip decision — RNG forked per-NPC for determinism.
        DeterministicRNG npc_rng = tick_rng.fork(rec.npc_id);
        if (npc_rng.next_float() < rec.flip_probability) {
            rec.status = InformantStatus::cooperating;
            rec.cooperation_start_tick = state.current_tick;

            for (const auto& ke : npc->known_evidence) {
                // EvidenceDelta: testimonial evidence from informant-provided knowledge
                EvidenceDelta ev;
                ev.new_token = EvidenceToken{0,
                                             EvidenceType::testimonial,
                                             npc->id,
                                             ke.subject_id,
                                             0.50f,
                                             0.003f,
                                             state.current_tick,
                                             npc->current_province_id,
                                             true};
                delta.evidence_deltas.push_back(ev);
            }

            // NPCDelta: reliability update — informant incurs implicit capital cost
            // (legal fees, witness protection costs, relocation expenses proxy)
            NPCDelta reliability_delta;
            reliability_delta.npc_id = rec.npc_id;
            reliability_delta.capital_delta = -cfg_.pay_silence_cost * 0.10f;  // 10% of silence cost
            delta.npc_deltas.push_back(reliability_delta);
        }
    }
}

}  // namespace econlife
