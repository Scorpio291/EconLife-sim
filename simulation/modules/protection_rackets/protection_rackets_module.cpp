#include "protection_rackets_module.h"

#include <algorithm>
#include <cmath>

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

void ProtectionRacketsModule::init_for_tick(const WorldState& /*state*/) {
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
                RacketEscalationStage old_stage = racket.escalation_stage;
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

}  // namespace econlife
