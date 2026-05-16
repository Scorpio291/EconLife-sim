#include "informant_system_module.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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
