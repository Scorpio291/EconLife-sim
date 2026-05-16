#include "legal_process_module.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/rng/deterministic_rng.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"

namespace econlife {

float LegalProcessModule::compute_conviction_probability(float evidence_weight,
                                                         float defense_quality, float judge_bias,
                                                         float witness_reliability,
                                                         float defense_quality_factor) {
    float prob = evidence_weight * (1.0f - defense_quality * defense_quality_factor) * judge_bias *
                 witness_reliability;
    return std::clamp(prob, 0.0f, 1.0f);
}

uint32_t LegalProcessModule::compute_sentence_ticks(CaseSeverity severity,
                                                    uint32_t ticks_per_level) {
    return (static_cast<uint32_t>(severity) + 1) * ticks_per_level;
}

bool LegalProcessModule::is_double_jeopardy_active(uint32_t current_tick, uint32_t cooldown_until) {
    return current_tick < cooldown_until;
}

LegalCaseStage LegalProcessModule::advance_stage(LegalCaseStage current, bool conviction) {
    switch (current) {
        case LegalCaseStage::investigation:
            return LegalCaseStage::arrested;
        case LegalCaseStage::arrested:
            return LegalCaseStage::charged;
        case LegalCaseStage::charged:
            return LegalCaseStage::trial;
        case LegalCaseStage::trial:
            return conviction ? LegalCaseStage::convicted : LegalCaseStage::acquitted;
        case LegalCaseStage::convicted:
            return LegalCaseStage::imprisoned;
        case LegalCaseStage::imprisoned:
            return LegalCaseStage::paroled;
        default:
            return current;
    }
}

float LegalProcessModule::compute_evidence_weight(const std::vector<float>& token_actionabilities) {
    float total = 0.0f;
    for (float a : token_actionabilities) {
        total += a;
    }
    return std::clamp(total, 0.0f, 1.0f);
}

void LegalProcessModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Tick-level RNG seed: world_seed mixed with current tick, forked per case.
    DeterministicRNG tick_rng(state.world_seed ^ static_cast<uint64_t>(state.current_tick));

    std::sort(cases_.begin(), cases_.end(),
              [](const LegalCase& a, const LegalCase& b) { return a.id < b.id; });

    for (auto& lcase : cases_) {
        if (lcase.stage == LegalCaseStage::acquitted || lcase.stage == LegalCaseStage::pardoned)
            continue;

        if (lcase.stage == LegalCaseStage::trial) {
            lcase.conviction_probability =
                compute_conviction_probability(lcase.evidence_weight, lcase.defense_quality, 1.0f,
                                               1.0f, cfg_.defense_quality_factor);

            // EvidenceDelta: evidence is presented at trial — each active token
            // associated with this case is flagged as presented (actionability reduced
            // by presentation; the record is now part of the public court record).
            for (const auto& token : state.evidence_pool) {
                if (token.is_active && token.target_npc_id == lcase.defendant_npc_id) {
                    EvidenceDelta presented_ev;
                    presented_ev.retired_token_id = token.id;  // retire from active pool
                    // Also emit a documentary token representing the court record
                    presented_ev.new_token = EvidenceToken{0,
                                                           EvidenceType::documentary,
                                                           lcase.prosecutor_npc_id,
                                                           lcase.defendant_npc_id,
                                                           token.actionability,
                                                           0.0f,  // court record does not decay
                                                           state.current_tick,
                                                           token.province_id,
                                                           true};
                    delta.evidence_deltas.push_back(presented_ev);
                }
            }

            // Probabilistic conviction — RNG forked per case for determinism.
            DeterministicRNG case_rng = tick_rng.fork(lcase.id);
            bool convicted = case_rng.next_float() < lcase.conviction_probability;
            lcase.stage = advance_stage(lcase.stage, convicted);

            if (convicted) {
                lcase.sentence_ticks =
                    compute_sentence_ticks(lcase.severity, cfg_.ticks_per_severity);
                lcase.release_tick = state.current_tick + lcase.sentence_ticks;
                lcase.double_jeopardy_until = lcase.release_tick + cfg_.double_jeopardy_cooldown;

                if (lcase.defendant_npc_id > 0) {
                    NPCDelta npc_delta;
                    npc_delta.npc_id = lcase.defendant_npc_id;
                    npc_delta.new_status = NPCStatus::imprisoned;
                    delta.npc_deltas.push_back(npc_delta);
                }

                ConsequenceDelta cons;
                cons.new_entry_id = lcase.id;
                delta.consequence_deltas.push_back(cons);
            }
        }

        if (lcase.stage == LegalCaseStage::imprisoned && state.current_tick >= lcase.release_tick) {
            lcase.stage = LegalCaseStage::paroled;
            if (lcase.defendant_npc_id > 0) {
                NPCDelta npc_delta;
                npc_delta.npc_id = lcase.defendant_npc_id;
                npc_delta.new_status = NPCStatus::active;
                delta.npc_deltas.push_back(npc_delta);
            }
        }
    }
}

// ─── Persistence helpers (schema v7) ────────────────────────────────────────
//
// Format (little-endian):
//   u32 schema_tag (1)
//   u32 count
//   for each LegalCase:
//     u32 id, u32 defendant_npc_id, u32 prosecutor_npc_id, u32 judge_npc_id
//     u8 stage, u8 severity
//     f32 evidence_weight, f32 defense_quality, f32 conviction_probability
//     u32 opened_tick, u32 sentence_ticks, u32 release_tick, u32 double_jeopardy_until
//     u8 is_player_case

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

void LegalProcessModule::serialize_state(std::vector<uint8_t>& out) const {
    put_u32(out, 1u);
    put_u32(out, static_cast<uint32_t>(cases_.size()));
    for (const auto& c : cases_) {
        put_u32(out, c.id);
        put_u32(out, c.defendant_npc_id);
        put_u32(out, c.prosecutor_npc_id);
        put_u32(out, c.judge_npc_id);
        out.push_back(static_cast<uint8_t>(c.stage));
        out.push_back(static_cast<uint8_t>(c.severity));
        put_f32(out, c.evidence_weight);
        put_f32(out, c.defense_quality);
        put_f32(out, c.conviction_probability);
        put_u32(out, c.opened_tick);
        put_u32(out, c.sentence_ticks);
        put_u32(out, c.release_tick);
        put_u32(out, c.double_jeopardy_until);
        out.push_back(c.is_player_case ? 1u : 0u);
    }
}

bool LegalProcessModule::deserialize_state(const uint8_t* data, size_t size) {
    Reader r{data, size};
    if (r.u32() != 1u)
        return false;
    uint32_t count = r.u32();
    cases_.clear();
    cases_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        LegalCase c{};
        c.id = r.u32();
        c.defendant_npc_id = r.u32();
        c.prosecutor_npc_id = r.u32();
        c.judge_npc_id = r.u32();
        c.stage = static_cast<LegalCaseStage>(r.u8());
        c.severity = static_cast<CaseSeverity>(r.u8());
        c.evidence_weight = r.f32();
        c.defense_quality = r.f32();
        c.conviction_probability = r.f32();
        c.opened_tick = r.u32();
        c.sentence_ticks = r.u32();
        c.release_tick = r.u32();
        c.double_jeopardy_until = r.u32();
        c.is_player_case = (r.u8() != 0);
        if (r.error)
            return false;
        cases_.push_back(c);
    }
    return !r.error;
}

}  // namespace econlife
