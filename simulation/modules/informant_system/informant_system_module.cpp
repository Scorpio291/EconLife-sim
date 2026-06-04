#include "informant_system_module.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

#include "core/rng/deterministic_rng.h"
#include "core/world_state/apply_deltas.h"  // lookup_npc_by_id
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

float InformantSystemModule::compute_flip_probability(
    float base_flip_rate, float risk_tolerance, float trust, uint32_t mutual_incrimination_count,
    uint32_t compartmentalization_level, float max_flip_probability, float risk_factor_scale,
    float trust_factor_scale, float incrimination_suppression, float compartment_bonus_per_level) {
    float risk = compute_risk_factor(risk_tolerance, risk_factor_scale);
    float trust_f = compute_trust_factor(trust, trust_factor_scale);
    float incrim =
        compute_incrimination_suppression(mutual_incrimination_count, incrimination_suppression);
    float compart =
        compute_compartmentalization_bonus(compartmentalization_level, compartment_bonus_per_level);
    float prob = base_flip_rate + risk + trust_f - incrim - compart;
    return std::clamp(prob, 0.0f, max_flip_probability);
}

void InformantSystemModule::process_countermeasures(const WorldState& state, DeltaBuffer& delta) {
    if (state.pending_informant_countermeasures.empty())
        return;

    auto find_record = [&](uint32_t npc_id) -> InformantRecord* {
        for (auto& r : records_)
            if (r.npc_id == npc_id)
                return &r;
        return nullptr;
    };

    for (const auto& cm : state.pending_informant_countermeasures) {
        const NPC* npc = lookup_npc_by_id(state, cm.informant_npc_id);
        uint32_t province = npc ? npc->current_province_id : 0;
        InformantRecord* rec = find_record(cm.informant_npc_id);

        switch (cm.countermeasure) {
            case 0:  // pay_silence
                // Fails if the player cannot afford it (record/NPC unchanged).
                if (state.player && state.player->wealth >= cfg_.pay_silence_cost) {
                    if (rec) {
                        rec->status = InformantStatus::silenced;
                        rec->flip_probability = 0.0f;
                    }
                    PlayerDelta pd{};
                    pd.wealth_delta = -cfg_.pay_silence_cost;
                    delta.player_delta.merge_from(std::move(pd));
                    // Mutual-incrimination obligation: the silenced informant now
                    // holds a whistleblower_silenced favor over the player.
                    ObligationNode o{};
                    o.id = 0;  // apply_deltas assigns
                    o.creditor_npc_id = cm.informant_npc_id;
                    o.debtor_npc_id = state.player->id;
                    o.favor_type = FavorType::whistleblower_silenced;
                    o.weight = 0.5f;
                    o.created_tick = state.current_tick;
                    o.is_active = true;
                    delta.new_obligation_nodes.push_back(o);
                    // Payment leaves a financial trail.
                    EvidenceDelta ev{};
                    ev.new_token = EvidenceToken{0,
                                                 EvidenceType::financial,
                                                 state.player->id,
                                                 cm.informant_npc_id,
                                                 0.40f,
                                                 0.002f,
                                                 state.current_tick,
                                                 province,
                                                 true};
                    delta.evidence_deltas.push_back(ev);
                }
                break;
            case 1: {  // threaten_silence
                NPCDelta nd{};
                nd.npc_id = cm.informant_npc_id;
                nd.risk_tolerance_delta = 0.10f;
                nd.new_memory_entry = MemoryEntry{state.current_tick,
                                                  MemoryType::employment_negative,
                                                  state.player ? state.player->id : 0u,
                                                  -0.7f,
                                                  0.003f,
                                                  true};
                delta.npc_deltas.push_back(nd);
                break;
            }
            case 2:  // relocate_witness
                if (rec) {
                    rec->status = InformantStatus::relocated;
                    rec->flip_probability = cfg_.base_flip_rate * 0.2f;
                }
                {
                    EvidenceDelta ev{};
                    ev.new_token = EvidenceToken{0,
                                                 EvidenceType::physical,
                                                 state.player ? state.player->id : 0u,
                                                 cm.informant_npc_id,
                                                 0.30f,
                                                 0.002f,
                                                 state.current_tick,
                                                 province,
                                                 true};
                    delta.evidence_deltas.push_back(ev);
                }
                break;
            case 3: {  // eliminate
                if (rec)
                    rec->status = InformantStatus::eliminated;
                NPCDelta nd{};
                nd.npc_id = cm.informant_npc_id;
                nd.new_status = NPCStatus::dead;
                delta.npc_deltas.push_back(nd);
                // Violence leaves high-actionability physical evidence.
                EvidenceDelta ev{};
                ev.new_token = EvidenceToken{0,
                                             EvidenceType::physical,
                                             state.player ? state.player->id : 0u,
                                             cm.informant_npc_id,
                                             0.80f,
                                             0.001f,
                                             state.current_tick,
                                             province,
                                             true};
                delta.evidence_deltas.push_back(ev);
                break;
            }
            default:
                break;
        }
    }

    const_cast<std::vector<InformantCountermeasureAction>&>(state.pending_informant_countermeasures)
        .clear();
}

void InformantSystemModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Drain any player countermeasures issued this tick (pay/threaten/relocate/
    // eliminate) before recruitment/flip processing.
    process_countermeasures(state, delta);

    // Self-seed informant records for imprisoned criminal NPCs not yet tracked.
    // The recruitment signal is NPCStatus::imprisoned on a criminal-role NPC —
    // a WorldState fact this module observes directly (legal_process sets it
    // upstream, and informant_system runs_after legal_process), so no
    // cross-module producer or seed queue is needed; the flip lifecycle below
    // already gates on `imprisoned`. Without
    // this, records_ was only ever populated by deserialize_state and the
    // whole subsystem was dead in a fresh game. Deterministic: significant_npcs
    // are scanned in order and deduped by npc_id.
    {
        std::set<uint32_t> tracked;
        for (const auto& rec : records_)
            tracked.insert(rec.npc_id);
        auto is_criminal_role = [](NPCRole r) {
            return r == NPCRole::criminal_operator || r == NPCRole::criminal_enforcer ||
                   r == NPCRole::fixer;
        };
        for (const auto& npc : state.significant_npcs) {
            if (npc.status != NPCStatus::imprisoned || !is_criminal_role(npc.role))
                continue;
            if (tracked.count(npc.id))
                continue;
            InformantRecord rec{};
            rec.npc_id = npc.id;
            rec.status = InformantStatus::not_cooperating;
            rec.flip_probability = 0.0f;
            rec.base_flip_rate = cfg_.base_flip_rate;
            rec.arrest_tick = state.current_tick;
            rec.cooperation_start_tick = 0;
            // Peripheral roles (enforcer/fixer) know less of the org's
            // operations -> more compartmentalized -> lower flip probability.
            rec.compartmentalization_level = (npc.role == NPCRole::criminal_operator) ? 0u : 1u;
            records_.push_back(std::move(rec));
            tracked.insert(npc.id);
        }
    }

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

        const NPC* npc = lookup_npc_by_id(state, rec.npc_id);
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
            rec.compartmentalization_level, cfg_.max_flip_probability, cfg_.risk_factor_scale,
            cfg_.trust_factor_scale, cfg_.incrimination_suppression,
            cfg_.compartment_bonus_per_level);

        // Probabilistic flip decision — RNG forked per-NPC for determinism.
        DeterministicRNG npc_rng = tick_rng.fork(rec.npc_id);
        if (npc_rng.next_float() < rec.flip_probability) {
            rec.status = InformantStatus::cooperating;
            rec.cooperation_start_tick = state.current_tick;

            // Disclosure: emit type-mapped evidence tokens for every piece of
            // known_evidence above the confidence threshold, and accumulate the
            // investigation pressure each disclosure adds. Knowledge entries are
            // already in a deterministic order on the NPC.
            float total_meter_fill = 0.0f;
            uint32_t primary_subject = 0;
            float primary_subject_weight = -1.0f;
            std::map<uint32_t, float> subject_weight;
            for (const auto& ke : npc->known_evidence) {
                if (ke.confidence <= cfg_.disclosure_confidence_threshold)
                    continue;

                // Knowledge-type-specific token mapping (INTERFACE.md):
                //   identity_link -> financial, activity -> testimonial,
                //   evidence_token -> documentary; relationship knowledge is
                //   relational hearsay -> testimonial.
                EvidenceType token_type = EvidenceType::testimonial;
                switch (ke.type) {
                    case KnowledgeType::identity_link:
                        token_type = EvidenceType::financial;
                        break;
                    case KnowledgeType::evidence_token:
                        token_type = EvidenceType::documentary;
                        break;
                    case KnowledgeType::activity:
                    case KnowledgeType::relationship:
                    default:
                        token_type = EvidenceType::testimonial;
                        break;
                }

                float actionability = ke.confidence * cfg_.cooperation_actionability_scale;
                EvidenceDelta ev;
                ev.new_token = EvidenceToken{0,
                                             token_type,
                                             npc->id,
                                             ke.subject_id,
                                             actionability,
                                             0.003f,
                                             state.current_tick,
                                             npc->current_province_id,
                                             true};
                delta.evidence_deltas.push_back(ev);

                total_meter_fill += actionability * cfg_.meter_fill_per_disclosure;
                float& w = subject_weight[ke.subject_id];
                w += actionability;
                if (w > primary_subject_weight) {
                    primary_subject_weight = w;
                    primary_subject = ke.subject_id;
                }
            }

            // InvestigatorMeter fill on disclosure: deliver the accumulated
            // pressure to the lead law-enforcement NPC in the informant's
            // province (lowest active LE id = designated investigator),
            // targeting the most-incriminated subject. drain_deferred_work
            // stages/escalates the meter and opens a case at raid_imminent.
            if (total_meter_fill > 0.0f && primary_subject != 0 &&
                npc->current_province_id < state.npc_indices_by_province.size()) {
                const NPC* lead_le = nullptr;
                for (uint32_t idx : state.npc_indices_by_province[npc->current_province_id]) {
                    const NPC& cand = state.significant_npcs[idx];
                    if (cand.role != NPCRole::law_enforcement || cand.status != NPCStatus::active)
                        continue;
                    if (!lead_le || cand.id < lead_le->id)
                        lead_le = &cand;
                }
                if (lead_le) {
                    NPCDelta meter_delta;
                    meter_delta.npc_id = lead_le->id;
                    meter_delta.investigator_meter_fill_delta = total_meter_fill;
                    meter_delta.investigator_meter_target = primary_subject;
                    delta.npc_deltas.push_back(meter_delta);
                }
            }

            // NPCDelta: reliability update — informant incurs implicit capital cost
            // (legal fees, witness protection costs, relocation expenses proxy)
            NPCDelta reliability_delta;
            reliability_delta.npc_id = rec.npc_id;
            reliability_delta.capital_delta =
                -cfg_.pay_silence_cost * 0.10f;  // 10% of silence cost
            delta.npc_deltas.push_back(reliability_delta);
        }
    }
}

// ─── Persistence helpers (schema v7) ────────────────────────────────────────
//
// Format (little-endian):
//   u32 schema_tag (1)
//   u32 count
//   for each InformantRecord:
//     u32 npc_id, u8 status, f32 flip_probability, f32 base_flip_rate
//     u32 arrest_tick, u32 cooperation_start_tick, u32 compartmentalization_level

namespace {

void put_u32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void put_f32(std::vector<uint8_t>& out, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    put_u32(out, bits);
}

struct Reader {
    const uint8_t* data;
    size_t size;
    size_t pos = 0;
    bool error = false;
    bool need(size_t n) {
        if (pos + n > size) {
            error = true;
            return false;
        }
        return true;
    }
    uint32_t u32() {
        if (!need(4))
            return 0;
        uint32_t v = data[pos] | (uint32_t(data[pos + 1]) << 8) | (uint32_t(data[pos + 2]) << 16) |
                     (uint32_t(data[pos + 3]) << 24);
        pos += 4;
        return v;
    }
    uint8_t u8() {
        if (!need(1))
            return 0;
        return data[pos++];
    }
    float f32() {
        uint32_t bits = u32();
        float v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }
};

}  // namespace

void InformantSystemModule::serialize_state(std::vector<uint8_t>& out) const {
    put_u32(out, 1u);
    put_u32(out, static_cast<uint32_t>(records_.size()));
    for (const auto& r : records_) {
        put_u32(out, r.npc_id);
        out.push_back(static_cast<uint8_t>(r.status));
        put_f32(out, r.flip_probability);
        put_f32(out, r.base_flip_rate);
        put_u32(out, r.arrest_tick);
        put_u32(out, r.cooperation_start_tick);
        put_u32(out, r.compartmentalization_level);
    }
}

bool InformantSystemModule::deserialize_state(const uint8_t* data, size_t size) {
    Reader r{data, size};
    if (r.u32() != 1u)
        return false;
    uint32_t count = r.u32();
    records_.clear();
    records_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        InformantRecord rec{};
        rec.npc_id = r.u32();
        rec.status = static_cast<InformantStatus>(r.u8());
        rec.flip_probability = r.f32();
        rec.base_flip_rate = r.f32();
        rec.arrest_tick = r.u32();
        rec.cooperation_start_tick = r.u32();
        rec.compartmentalization_level = r.u32();
        if (r.error)
            return false;
        records_.push_back(rec);
    }
    return !r.error;
}

}  // namespace econlife
