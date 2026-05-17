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

bool LegalProcessModule::should_arrest(float evidence_weight, float arrest_evidence_threshold) {
    return evidence_weight >= arrest_evidence_threshold;
}

bool LegalProcessModule::should_dismiss(float evidence_weight, float dismissal_evidence_threshold) {
    return evidence_weight < dismissal_evidence_threshold;
}

bool LegalProcessModule::should_charge(uint32_t current_tick, uint32_t stage_entered_tick,
                                       uint32_t investigation_to_charge_ticks,
                                       float evidence_weight, float charge_evidence_threshold) {
    if (current_tick < stage_entered_tick)
        return false;
    if ((current_tick - stage_entered_tick) < investigation_to_charge_ticks)
        return false;
    return evidence_weight >= charge_evidence_threshold;
}

bool LegalProcessModule::should_proceed_to_trial(uint32_t current_tick,
                                                 uint32_t stage_entered_tick,
                                                 uint32_t charge_to_trial_ticks) {
    if (current_tick < stage_entered_tick)
        return false;
    return (current_tick - stage_entered_tick) >= charge_to_trial_ticks;
}

bool LegalProcessModule::is_custodial(CaseSeverity severity,
                                      uint32_t custodial_sentence_severity_floor) {
    // severity_value = enum + 1 (minor=1, moderate=2, ..., capital=6).
    uint32_t severity_value = static_cast<uint32_t>(severity) + 1u;
    return severity_value >= custodial_sentence_severity_floor;
}

bool LegalProcessModule::is_parole_eligible(uint32_t current_tick, uint32_t release_tick,
                                            uint32_t sentence_ticks,
                                            float parole_eligibility_fraction) {
    if (sentence_ticks == 0)
        return false;
    // release_tick is the absolute tick when sentence ends. The fraction of
    // sentence served is (sentence_ticks - remaining) / sentence_ticks.
    // Parole-eligible once served >= parole_eligibility_fraction.
    uint32_t remaining = (release_tick > current_tick) ? (release_tick - current_tick) : 0u;
    float served_fraction =
        1.0f - (static_cast<float>(remaining) / static_cast<float>(sentence_ticks));
    return served_fraction >= parole_eligibility_fraction;
}

float LegalProcessModule::compute_fine_amount(CaseSeverity severity,
                                              float fine_amount_per_severity) {
    return fine_amount_per_severity * static_cast<float>(static_cast<uint32_t>(severity) + 1u);
}

void LegalProcessModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Tick-level RNG seed: world_seed mixed with current tick, forked per case.
    DeterministicRNG tick_rng(state.world_seed ^ static_cast<uint64_t>(state.current_tick));

    // Drain cross-module seed queue: any module observing a triggering event
    // (e.g. investigator_engine's meter reaching raid_imminent) emits a
    // LegalCaseSeedDelta which apply_deltas routes into
    // state.pending_legal_case_seeds. We instantiate a fresh LegalCase per
    // seed at the investigation stage with a freshly-allocated case id, then
    // clear the queue via const_cast — same carve-out player_actions uses
    // for state.deferred_work_queue. State must be const otherwise (WorldState
    // invariant). The queue is documented as "must be empty at save time".
    if (!state.pending_legal_case_seeds.empty()) {
        // Allocate case ids starting from max(existing id) + 1 so the new
        // cases sort after pre-existing ones in deterministic order.
        uint32_t next_id = 1;
        for (const auto& c : cases_) {
            if (c.id >= next_id)
                next_id = c.id + 1;
        }
        for (const auto& seed : state.pending_legal_case_seeds) {
            LegalCase c{};
            c.id = next_id++;
            c.defendant_npc_id = seed.defendant_npc_id;
            c.is_player_case = (seed.defendant_npc_id == 0);
            c.severity = static_cast<CaseSeverity>(seed.severity);
            c.stage = LegalCaseStage::investigation;
            c.evidence_weight = seed.initial_evidence_weight;
            c.opened_tick = state.current_tick;
            c.stage_entered_tick = state.current_tick;
            // Investigator hint: stash on prosecutor slot until investigator
            // assignment is fully wired. The real prosecutor_npc_id is set
            // by the charge filing stage in a follow-up.
            c.prosecutor_npc_id = seed.lead_investigator_id;
            cases_.push_back(c);
        }
        auto& mutable_queue =
            const_cast<std::vector<LegalCaseSeedDelta>&>(state.pending_legal_case_seeds);
        mutable_queue.clear();
    }

    std::sort(cases_.begin(), cases_.end(),
              [](const LegalCase& a, const LegalCase& b) { return a.id < b.id; });

    for (auto& lcase : cases_) {
        // Terminal stages: case is closed for this tick. acquitted and pardoned
        // never re-open within this lifetime; fined cases retain double-jeopardy
        // protection but otherwise no longer transition; paroled defendants are
        // released from custody (NPC status flip already emitted on transition).
        if (lcase.stage == LegalCaseStage::acquitted ||
            lcase.stage == LegalCaseStage::pardoned ||
            lcase.stage == LegalCaseStage::fined ||
            lcase.stage == LegalCaseStage::paroled)
            continue;

        // --- Stage: investigation ---
        // Auto-advance to arrested once evidence_weight is high enough.
        // Spec also asks for InvestigatorMeter.status == raid_imminent
        // ("critical") at the same time, but InvestigatorMeter records are
        // not on WorldState yet (their host lives inside investigator_engine
        // private state). When that wiring lands, gate this transition on
        // the meter too.
        if (lcase.stage == LegalCaseStage::investigation) {
            if (should_arrest(lcase.evidence_weight, cfg_.arrest_evidence_threshold)) {
                lcase.stage = LegalCaseStage::arrested;
                lcase.stage_entered_tick = state.current_tick;

                // Emit a public_arrest_record EvidenceToken (documentary type
                // since shared_types has no dedicated public_arrest variant).
                // Weight = case evidence + arrest_exposure_hit (additive
                // PR damage of public arrest, per player.h:159 "exposure
                // is the set of awareness entries, not a scalar").
                float public_weight = std::clamp(
                    lcase.evidence_weight + cfg_.arrest_exposure_hit, 0.0f, 1.0f);
                EvidenceDelta arrest_record;
                arrest_record.new_token = EvidenceToken{0,
                                                        EvidenceType::documentary,
                                                        lcase.prosecutor_npc_id,
                                                        lcase.defendant_npc_id,
                                                        public_weight,
                                                        0.0f,  // public record does not decay
                                                        state.current_tick,
                                                        /*province_id=*/0u,
                                                        true};
                delta.evidence_deltas.push_back(arrest_record);

                // Auto-bail for player defendants with enough wealth on hand.
                // PlayerCharacter cash check is intentionally not made here —
                // we emit the wealth_delta unconditionally and let downstream
                // clamping handle insolvent edge cases. Future work: gate on
                // actual cash availability.
                if (lcase.is_player_case) {
                    lcase.bail_posted = true;
                    PlayerDelta pd{};
                    pd.wealth_delta = -cfg_.bail_amount;
                    delta.player_delta.merge_from(std::move(pd));
                }

                ConsequenceDelta cons;
                cons.new_entry_id = lcase.id;
                delta.consequence_deltas.push_back(cons);
                continue;  // one transition per tick per case
            }
        }

        // --- Stage: arrested ---
        // Dismiss if evidence drops below dismissal threshold; otherwise
        // charge after investigation_to_charge_ticks elapses.
        if (lcase.stage == LegalCaseStage::arrested) {
            if (should_dismiss(lcase.evidence_weight, cfg_.dismissal_evidence_threshold)) {
                lcase.stage = LegalCaseStage::acquitted;
                lcase.stage_entered_tick = state.current_tick;
                lcase.double_jeopardy_until =
                    state.current_tick + cfg_.double_jeopardy_cooldown;
                if (lcase.defendant_npc_id > 0) {
                    NPCDelta npc_delta;
                    npc_delta.npc_id = lcase.defendant_npc_id;
                    npc_delta.new_status = NPCStatus::active;
                    delta.npc_deltas.push_back(npc_delta);
                }
                continue;
            }
            if (should_charge(state.current_tick, lcase.stage_entered_tick,
                              cfg_.investigation_to_charge_ticks, lcase.evidence_weight,
                              cfg_.charge_evidence_threshold)) {
                lcase.stage = LegalCaseStage::charged;
                lcase.stage_entered_tick = state.current_tick;
                continue;
            }
        }

        // --- Stage: charged ---
        // Same dismissal check as arrested; otherwise proceed to trial.
        if (lcase.stage == LegalCaseStage::charged) {
            if (should_dismiss(lcase.evidence_weight, cfg_.dismissal_evidence_threshold)) {
                lcase.stage = LegalCaseStage::acquitted;
                lcase.stage_entered_tick = state.current_tick;
                lcase.double_jeopardy_until =
                    state.current_tick + cfg_.double_jeopardy_cooldown;
                continue;
            }
            if (should_proceed_to_trial(state.current_tick, lcase.stage_entered_tick,
                                        cfg_.charge_to_trial_ticks)) {
                lcase.stage = LegalCaseStage::trial;
                lcase.stage_entered_tick = state.current_tick;
                // Fall through into trial handling below for the same tick.
            } else {
                continue;
            }
        }

        // --- Stage: trial ---
        if (lcase.stage == LegalCaseStage::trial) {
            lcase.conviction_probability =
                compute_conviction_probability(lcase.evidence_weight, lcase.defense_quality, 1.0f,
                                               1.0f, cfg_.defense_quality_factor);

            // Evidence-presentation deltas (preserved from prior implementation).
            for (const auto& token : state.evidence_pool) {
                if (token.is_active && token.target_npc_id == lcase.defendant_npc_id) {
                    EvidenceDelta presented_ev;
                    presented_ev.retired_token_id = token.id;
                    presented_ev.new_token = EvidenceToken{0,
                                                           EvidenceType::documentary,
                                                           lcase.prosecutor_npc_id,
                                                           lcase.defendant_npc_id,
                                                           token.actionability,
                                                           0.0f,
                                                           state.current_tick,
                                                           token.province_id,
                                                           true};
                    delta.evidence_deltas.push_back(presented_ev);
                }
            }

            DeterministicRNG case_rng = tick_rng.fork(lcase.id);
            bool convicted = case_rng.next_float() < lcase.conviction_probability;
            if (convicted) {
                lcase.stage = LegalCaseStage::convicted;
                lcase.stage_entered_tick = state.current_tick;
                lcase.sentence_ticks =
                    compute_sentence_ticks(lcase.severity, cfg_.ticks_per_severity);
                lcase.release_tick = state.current_tick + lcase.sentence_ticks;
                lcase.double_jeopardy_until =
                    lcase.release_tick + cfg_.double_jeopardy_cooldown;
                ConsequenceDelta cons;
                cons.new_entry_id = lcase.id;
                delta.consequence_deltas.push_back(cons);
                // Fall through into convicted handling for the same tick so
                // the imprisonment/fine branch resolves immediately.
            } else {
                lcase.stage = LegalCaseStage::acquitted;
                lcase.stage_entered_tick = state.current_tick;
                lcase.double_jeopardy_until =
                    state.current_tick + cfg_.double_jeopardy_cooldown;
                continue;
            }
        }

        // --- Stage: convicted ---
        // Branch on severity: custodial -> imprisoned, otherwise -> fined.
        if (lcase.stage == LegalCaseStage::convicted) {
            if (is_custodial(lcase.severity, cfg_.custodial_sentence_severity_floor)) {
                lcase.stage = LegalCaseStage::imprisoned;
                lcase.stage_entered_tick = state.current_tick;
                if (lcase.defendant_npc_id > 0) {
                    NPCDelta npc_delta;
                    npc_delta.npc_id = lcase.defendant_npc_id;
                    npc_delta.new_status = NPCStatus::imprisoned;
                    delta.npc_deltas.push_back(npc_delta);
                }
            } else {
                lcase.stage = LegalCaseStage::fined;
                lcase.stage_entered_tick = state.current_tick;
                float fine = compute_fine_amount(lcase.severity, cfg_.fine_amount_per_severity);
                if (lcase.is_player_case) {
                    PlayerDelta pd{};
                    pd.wealth_delta = -fine;
                    delta.player_delta.merge_from(std::move(pd));
                }
                // No NPCStatus change for fined defendants — they stay active.
                continue;
            }
        }

        // --- Stage: imprisoned ---
        // Parole eligibility once parole_eligibility_fraction of sentence has
        // been served. Mandatory release at release_tick.
        if (lcase.stage == LegalCaseStage::imprisoned) {
            bool eligible = is_parole_eligible(state.current_tick, lcase.release_tick,
                                               lcase.sentence_ticks,
                                               cfg_.parole_eligibility_fraction);
            bool mandatory_release = state.current_tick >= lcase.release_tick;
            if (eligible || mandatory_release) {
                lcase.stage = LegalCaseStage::paroled;
                lcase.stage_entered_tick = state.current_tick;
                if (lcase.defendant_npc_id > 0) {
                    NPCDelta npc_delta;
                    npc_delta.npc_id = lcase.defendant_npc_id;
                    npc_delta.new_status = NPCStatus::active;
                    delta.npc_deltas.push_back(npc_delta);
                }
            }
        }
    }
}

// ─── Persistence helpers (schema v7) ────────────────────────────────────────
//
// Format (little-endian):
//   u32 schema_tag (2 — v7 initial layout was 1; v2 adds stage_entered_tick
//                       and bail_posted)
//   u32 count
//   for each LegalCase:
//     u32 id, u32 defendant_npc_id, u32 prosecutor_npc_id, u32 judge_npc_id
//     u8 stage, u8 severity
//     f32 evidence_weight, f32 defense_quality, f32 conviction_probability
//     u32 opened_tick, u32 stage_entered_tick
//     u32 sentence_ticks, u32 release_tick, u32 double_jeopardy_until
//     u8 is_player_case, u8 bail_posted

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
    put_u32(out, 2u);
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
        put_u32(out, c.stage_entered_tick);
        put_u32(out, c.sentence_ticks);
        put_u32(out, c.release_tick);
        put_u32(out, c.double_jeopardy_until);
        out.push_back(c.is_player_case ? 1u : 0u);
        out.push_back(c.bail_posted ? 1u : 0u);
    }
}

bool LegalProcessModule::deserialize_state(const uint8_t* data, size_t size) {
    Reader r{data, size};
    // v1 of this tag predates the state-machine work; new fields were added
    // (stage_entered_tick, bail_posted). V1 of EconLife is pre-release so
    // we don't migrate — reject tag 1 outright and require fresh saves.
    if (r.u32() != 2u)
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
        c.stage_entered_tick = r.u32();
        c.sentence_ticks = r.u32();
        c.release_tick = r.u32();
        c.double_jeopardy_until = r.u32();
        c.is_player_case = (r.u8() != 0);
        c.bail_posted = (r.u8() != 0);
        if (r.error)
            return false;
        cases_.push_back(c);
    }
    return !r.error;
}

}  // namespace econlife
