#include "protection_rackets_module.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/world_state/player.h"
#include "core/world_state/world_state.h"

namespace econlife {

// ============================================================================
// Static utility functions
// ============================================================================

float ProtectionRacketsModule::compute_demand_per_tick(float business_revenue_per_tick,
                                                       float demand_rate) {
    float demand = business_revenue_per_tick * demand_rate;
    return std::max(0.0f, demand);
}

float ProtectionRacketsModule::compute_grievance_contribution(float demand_per_tick,
                                                              float grievance_per_demand_unit) {
    return demand_per_tick * grievance_per_demand_unit;
}

float ProtectionRacketsModule::compute_refusal_probability(bool is_defensive_incumbent,
                                                           float criminal_dominance_index,
                                                           float regulatory_violation_severity,
                                                           float incumbent_refuse_probability,
                                                           float default_refuse_probability) {
    float base = is_defensive_incumbent ? incumbent_refuse_probability : default_refuse_probability;
    float dominance_effect = (1.0f - criminal_dominance_index) * 0.30f;
    float violation_effect = regulatory_violation_severity * 0.10f;
    float probability = base + dominance_effect - violation_effect;
    return std::clamp(probability, 0.0f, 1.0f);
}

RacketEscalationStage ProtectionRacketsModule::determine_escalation_stage(
    uint32_t ticks_overdue, uint32_t warning_threshold, uint32_t property_damage_threshold,
    uint32_t violence_threshold, uint32_t abandonment_threshold) {
    if (ticks_overdue >= abandonment_threshold)
        return RacketEscalationStage::abandonment;
    if (ticks_overdue >= violence_threshold)
        return RacketEscalationStage::violence;
    if (ticks_overdue >= property_damage_threshold)
        return RacketEscalationStage::property_damage;
    if (ticks_overdue >= warning_threshold)
        return RacketEscalationStage::warning;
    return RacketEscalationStage::demand_issued;
}

bool ProtectionRacketsModule::can_business_pay(float business_cash, float demand_per_tick) {
    return business_cash >= demand_per_tick;
}

float ProtectionRacketsModule::compute_violence_le_multiplier(float base_fill_rate,
                                                              float personnel_violence_multiplier) {
    return base_fill_rate * personnel_violence_multiplier;
}

// ============================================================================
// Province-parallel execution
// ============================================================================

void ProtectionRacketsModule::init_for_tick(const WorldState& state) {
    // Drain protection-racket seeds emitted this tick by criminal_operations
    // (Tier 7). Each seed opens a new racket on a legitimate target business,
    // deriving demand from the business's revenue. Runs on the main thread in
    // init_for_tick, before the province-parallel dispatch. Same-tick queue:
    // cleared after draining via the const_cast carve-out (cf. legal_process).
    if (!state.pending_racket_seeds.empty()) {
        for (const auto& seed : state.pending_racket_seeds) {
            // Dedup: one racket per target business.
            bool exists = false;
            for (const auto& r : rackets_) {
                if (r.target_business_id == seed.target_business_id) {
                    exists = true;
                    break;
                }
            }
            if (exists)
                continue;

            const NPCBusiness* target = nullptr;
            for (const auto& biz : state.npc_businesses) {
                if (biz.id == seed.target_business_id) {
                    target = &biz;
                    break;
                }
            }
            if (!target)
                continue;

            ProtectionRacket racket{};
            racket.id = seed.target_business_id;  // unique: one racket per target
            racket.criminal_org_id = seed.criminal_org_id;
            racket.target_business_id = seed.target_business_id;
            racket.demand_per_tick =
                compute_demand_per_tick(target->revenue_per_tick, cfg_.demand_rate);
            racket.status = RacketStatus::active;
            racket.escalation_stage = RacketEscalationStage::demand_issued;
            racket.last_payment_tick = state.current_tick;
            racket.demand_issued_tick = state.current_tick;
            racket.community_grievance_contribution = compute_grievance_contribution(
                racket.demand_per_tick, cfg_.grievance_per_demand_unit);
            rackets_.push_back(std::move(racket));
        }
        const_cast<std::vector<RacketSeedDelta>&>(state.pending_racket_seeds).clear();
    }

    // Sort rackets once on the main thread before parallel dispatch.
    std::sort(rackets_.begin(), rackets_.end(),
              [](const ProtectionRacket& a, const ProtectionRacket& b) { return a.id < b.id; });
}

void ProtectionRacketsModule::execute_province(uint32_t province_idx, const WorldState& state,
                                               DeltaBuffer& province_delta) {
    if (province_idx >= state.provinces.size())
        return;
    const auto& province = state.provinces[province_idx];

    for (auto& racket : rackets_) {
        // Only process rackets in this province
        // Find target business to check province
        const NPCBusiness* target_biz = nullptr;
        for (const auto& biz : state.npc_businesses) {
            if (biz.id == racket.target_business_id) {
                target_biz = &biz;
                break;
            }
        }
        if (!target_biz || target_biz->province_id != province.id)
            continue;

        // Skip lapsed and expelled rackets
        if (racket.status == RacketStatus::lapsed || racket.status == RacketStatus::expelled) {
            continue;
        }

        // Update demand per tick based on current business revenue
        racket.demand_per_tick =
            compute_demand_per_tick(target_biz->revenue_per_tick, cfg_.demand_rate);

        // Clamp NaN/negative demand
        if (std::isnan(racket.demand_per_tick) || racket.demand_per_tick < 0.0f) {
            racket.demand_per_tick = 0.0f;
            continue;
        }

        // Compute grievance contribution
        racket.community_grievance_contribution =
            compute_grievance_contribution(racket.demand_per_tick, cfg_.grievance_per_demand_unit);

        // Process based on status
        if (racket.status == RacketStatus::active) {
            // Active racket: attempt collection
            if (can_business_pay(target_biz->cash, racket.demand_per_tick)) {
                // Payment collected: debit victim business cash
                BusinessDelta victim_delta;
                victim_delta.business_id = racket.target_business_id;
                victim_delta.cash_delta = -racket.demand_per_tick;
                province_delta.business_deltas.push_back(victim_delta);

                // Credit criminal org business cash
                BusinessDelta criminal_delta;
                criminal_delta.business_id = racket.criminal_org_id;
                criminal_delta.cash_delta = racket.demand_per_tick;
                province_delta.business_deltas.push_back(criminal_delta);

                racket.last_payment_tick = state.current_tick;

                // EvidenceDelta: observable protection activity (financial pattern)
                EvidenceDelta ev;
                ev.new_token = EvidenceToken{0,
                                             EvidenceType::financial,
                                             racket.criminal_org_id,
                                             target_biz->owner_id,
                                             0.15f,
                                             0.002f,
                                             state.current_tick,
                                             province.id,
                                             true};
                province_delta.evidence_deltas.push_back(ev);
            }
            // If can't pay: no payment, no escalation; status remains active

            // Accumulate grievance in province
            RegionDelta region_delta;
            region_delta.region_id = province.id;
            region_delta.grievance_delta = racket.community_grievance_contribution;
            province_delta.region_deltas.push_back(region_delta);

        } else if (racket.status == RacketStatus::refused) {
            // Refused racket: advance escalation based on ticks overdue
            uint32_t ticks_overdue = state.current_tick - racket.demand_issued_tick;
            RacketEscalationStage new_stage = determine_escalation_stage(
                ticks_overdue, cfg_.warning_threshold, cfg_.property_damage_threshold,
                cfg_.violence_threshold, cfg_.abandonment_threshold);

            if (new_stage != racket.escalation_stage) {
                racket.escalation_stage = new_stage;

                // Stage transition effects
                switch (new_stage) {
                    case RacketEscalationStage::warning: {
                        // Intimidation: add memory entry to target business owner
                        NPCDelta owner_delta;
                        owner_delta.npc_id = target_biz->owner_id;
                        owner_delta.new_memory_entry =
                            MemoryEntry{state.current_tick,
                                        MemoryType::employment_negative,
                                        racket.criminal_org_id,
                                        cfg_.memory_emotional_weight_warning,
                                        1.0f,
                                        true};
                        province_delta.npc_deltas.push_back(owner_delta);
                        break;
                    }
                    case RacketEscalationStage::property_damage: {
                        // Facility incident consequence (severity 0.4)
                        ConsequenceDelta cons;
                        cons.new_entry_id = racket.target_business_id;
                        province_delta.consequence_deltas.push_back(cons);

                        // Physical evidence token
                        EvidenceDelta ev;
                        ev.new_token = EvidenceToken{0,
                                                     EvidenceType::physical,
                                                     racket.criminal_org_id,
                                                     target_biz->owner_id,
                                                     cfg_.property_damage_severity,
                                                     0.002f,
                                                     state.current_tick,
                                                     province.id,
                                                     true};
                        province_delta.evidence_deltas.push_back(ev);
                        break;
                    }
                    case RacketEscalationStage::violence: {
                        // Personnel violence consequence
                        ConsequenceDelta cons;
                        cons.new_entry_id = racket.target_business_id;
                        province_delta.consequence_deltas.push_back(cons);

                        // Testimonial evidence from witnesses
                        EvidenceDelta ev;
                        ev.new_token = EvidenceToken{0,
                                                     EvidenceType::testimonial,
                                                     racket.criminal_org_id,
                                                     target_biz->owner_id,
                                                     0.50f,
                                                     0.002f,
                                                     state.current_tick,
                                                     province.id,
                                                     true};
                        province_delta.evidence_deltas.push_back(ev);

                        // Note: InvestigatorMeter fill_rate multiplied by 3.0 handled
                        // by investigator_engine when it reads violence evidence
                        break;
                    }
                    case RacketEscalationStage::abandonment: {
                        // Business bankruptcy/exit
                        ConsequenceDelta cons;
                        cons.new_entry_id = racket.target_business_id;
                        province_delta.consequence_deltas.push_back(cons);
                        break;
                    }
                    default:
                        break;
                }
            }

            // Grievance accumulates at warning+ escalation stages
            if (racket.escalation_stage >= RacketEscalationStage::warning) {
                RegionDelta region_delta;
                region_delta.region_id = province.id;
                region_delta.grievance_delta = racket.community_grievance_contribution;
                province_delta.region_deltas.push_back(region_delta);
            }
        }
    }
}

void ProtectionRacketsModule::execute(const WorldState& state, DeltaBuffer& delta) {
    for (uint32_t i = 0; i < state.provinces.size(); ++i) {
        execute_province(i, state, delta);
    }
}

// ─── Persistence helpers ────────────────────────────────────────────────────
//
// Encodes rackets_ as a self-contained byte block. Format (little-endian):
//   u32 schema_tag (1 == this layout)
//   u32 count
//   for each ProtectionRacket:
//     u32 id, u32 criminal_org_id, u32 target_business_id
//     f32 demand_per_tick
//     u8  status, u8 escalation_stage
//     u32 last_payment_tick, u32 demand_issued_tick
//     f32 community_grievance_contribution

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

void ProtectionRacketsModule::serialize_state(std::vector<uint8_t>& out) const {
    put_u32(out, 1u);
    put_u32(out, static_cast<uint32_t>(rackets_.size()));
    for (const auto& r : rackets_) {
        put_u32(out, r.id);
        put_u32(out, r.criminal_org_id);
        put_u32(out, r.target_business_id);
        put_f32(out, r.demand_per_tick);
        out.push_back(static_cast<uint8_t>(r.status));
        out.push_back(static_cast<uint8_t>(r.escalation_stage));
        put_u32(out, r.last_payment_tick);
        put_u32(out, r.demand_issued_tick);
        put_f32(out, r.community_grievance_contribution);
    }
}

bool ProtectionRacketsModule::deserialize_state(const uint8_t* data, size_t size) {
    Reader r{data, size};
    if (r.u32() != 1u)
        return false;
    uint32_t count = r.u32();
    rackets_.clear();
    rackets_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        ProtectionRacket pr{};
        pr.id = r.u32();
        pr.criminal_org_id = r.u32();
        pr.target_business_id = r.u32();
        pr.demand_per_tick = r.f32();
        pr.status = static_cast<RacketStatus>(r.u8());
        pr.escalation_stage = static_cast<RacketEscalationStage>(r.u8());
        pr.last_payment_tick = r.u32();
        pr.demand_issued_tick = r.u32();
        pr.community_grievance_contribution = r.f32();
        if (r.error)
            return false;
        rackets_.push_back(pr);
    }
    return !r.error;
}

}  // namespace econlife
