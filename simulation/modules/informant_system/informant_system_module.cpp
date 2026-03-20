#include "informant_system_module.h"
#include "core/world_state/world_state.h"
#include "core/world_state/player.h"
#include <algorithm>
#include <cmath>

namespace econlife {

float InformantSystemModule::compute_risk_factor(float risk_tolerance) {
    return (1.0f - risk_tolerance) * RISK_FACTOR_SCALE;
}

float InformantSystemModule::compute_trust_factor(float trust) {
    return (1.0f - trust) * TRUST_FACTOR_SCALE;
}

float InformantSystemModule::compute_incrimination_suppression(uint32_t obligation_count) {
    return static_cast<float>(obligation_count) * INCRIMINATION_SUPPRESSION;
}

float InformantSystemModule::compute_compartmentalization_bonus(uint32_t level) {
    return static_cast<float>(level) * COMPARTMENT_BONUS_PER_LEVEL;
}

float InformantSystemModule::compute_flip_probability(
    float base_flip_rate, float risk_tolerance, float trust,
    uint32_t mutual_incrimination_count, uint32_t compartmentalization_level)
{
    float risk = compute_risk_factor(risk_tolerance);
    float trust_f = compute_trust_factor(trust);
    float incrim = compute_incrimination_suppression(mutual_incrimination_count);
    float compart = compute_compartmentalization_bonus(compartmentalization_level);
    float prob = base_flip_rate + risk + trust_f - incrim - compart;
    return std::clamp(prob, 0.0f, MAX_FLIP_PROBABILITY);
}

void InformantSystemModule::execute(const WorldState& state, DeltaBuffer& delta) {
    std::sort(records_.begin(), records_.end(),
              [](const InformantRecord& a, const InformantRecord& b) { return a.npc_id < b.npc_id; });

    for (auto& rec : records_) {
        if (rec.status != InformantStatus::not_cooperating) continue;

        const NPC* npc = nullptr;
        for (const auto& n : state.significant_npcs) {
            if (n.id == rec.npc_id) { npc = &n; break; }
        }
        if (!npc || npc->status != NPCStatus::imprisoned) continue;

        float trust = 0.0f;
        for (const auto& rel : npc->relationships) {
            if (state.player && rel.target_npc_id == state.player->id) {
                trust = rel.trust;
                break;
            }
        }

        uint32_t mutual_count = 0;
        for (const auto& obl : state.obligation_network) {
            if (obl.is_active &&
                (obl.creditor_npc_id == npc->id || obl.debtor_npc_id == npc->id) &&
                obl.favor_type == FavorType::criminal_cooperation) {
                mutual_count++;
            }
        }

        rec.flip_probability = compute_flip_probability(
            rec.base_flip_rate, npc->risk_tolerance, trust,
            mutual_count, rec.compartmentalization_level);

        if (rec.flip_probability >= MAX_FLIP_PROBABILITY * 0.8f) {
            rec.status = InformantStatus::cooperating;
            rec.cooperation_start_tick = state.current_tick;

            for (const auto& ke : npc->known_evidence) {
                EvidenceDelta ev;
                ev.new_token = EvidenceToken{
                    0, (ke.type == KnowledgeType::identity_link)
                        ? EvidenceType::financial : EvidenceType::testimonial,
                    npc->id, ke.subject_id,
                    0.50f, 0.003f,
                    state.current_tick, npc->current_province_id, true
                };
                delta.evidence_deltas.push_back(ev);
            }
        }
    }
}

}  // namespace econlife
