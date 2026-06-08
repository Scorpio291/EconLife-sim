#include "modules/facility_signals/facility_signals_module.h"

#include <algorithm>
#include <cmath>

#include "core/world_state/player.h"
#include "core/world_state/world_state.h"

namespace econlife {

// ---------------------------------------------------------------------------
// Static utility functions
// ---------------------------------------------------------------------------

float FacilitySignalsModule::compute_signal_composite(float power, float chemical, float traffic,
                                                      float olfactory,
                                                      const FacilityTypeSignalWeights& weights) {
    float composite = weights.w_power_consumption * power + weights.w_chemical_waste * chemical +
                      weights.w_foot_traffic * traffic + weights.w_olfactory * olfactory;

    if (std::isnan(composite))
        return 0.0f;
    return std::clamp(composite, 0.0f, 1.0f);
}

float FacilitySignalsModule::compute_net_signal(float base_composite, float scrutiny_mitigation) {
    float net = base_composite - scrutiny_mitigation;
    if (std::isnan(net))
        return 0.0f;
    return std::max(0.0f, net);
}

float FacilitySignalsModule::compute_le_fill_rate(float regional_signal, float detection_scale,
                                                  float fill_rate_max) {
    float rate = regional_signal * detection_scale;
    if (std::isnan(rate))
        return 0.0f;
    return std::clamp(rate, 0.0f, fill_rate_max);
}

InvestigatorMeterStatus FacilitySignalsModule::evaluate_investigator_status(
    float current_level) const {
    if (current_level >= cfg_.raid_threshold)
        return InvestigatorMeterStatus::raid_imminent;
    if (current_level >= cfg_.formal_inquiry_threshold)
        return InvestigatorMeterStatus::formal_inquiry;
    if (current_level >= cfg_.surveillance_threshold)
        return InvestigatorMeterStatus::surveillance;
    return InvestigatorMeterStatus::inactive;
}

RegulatorMeterStatus FacilitySignalsModule::evaluate_regulator_status(float current_level) const {
    if (current_level >= cfg_.enforcement_threshold)
        return RegulatorMeterStatus::enforcement_action;
    if (current_level >= cfg_.audit_threshold)
        return RegulatorMeterStatus::formal_audit;
    if (current_level >= cfg_.notice_threshold)
        return RegulatorMeterStatus::notice_filed;
    return RegulatorMeterStatus::inactive;
}

float FacilitySignalsModule::apply_corruption_to_fill_rate(float fill_rate,
                                                           float corruption_susceptibility,
                                                           float regional_corruption_coverage) {
    float factor = 1.0f - corruption_susceptibility * regional_corruption_coverage;
    factor = std::clamp(factor, 0.0f, 1.0f);
    return fill_rate * factor;
}

// ---------------------------------------------------------------------------
// Pre-parallel initialization — populate signal entries for all businesses
// ---------------------------------------------------------------------------

void FacilitySignalsModule::init_for_tick(const WorldState& state) {
    for (const auto& biz : state.npc_businesses) {
        bool found = false;
        for (const auto& fs : facility_signals_) {
            if (fs.business_id == biz.id) {
                found = true;
                break;
            }
        }
        if (!found) {
            FacilitySignals fs{};
            fs.facility_id = biz.id;
            fs.business_id = biz.id;
            fs.power_consumption_anomaly = 0.0f;
            fs.chemical_waste_signature = 0.0f;
            fs.foot_traffic_visibility = 0.0f;
            fs.olfactory_signature = 0.0f;
            fs.scrutiny_mitigation = 0.0f;
            facility_signals_.push_back(fs);
        }
    }
}

// ---------------------------------------------------------------------------
// Province-parallel execution
// ---------------------------------------------------------------------------

void FacilitySignalsModule::execute_province(uint32_t province_idx, const WorldState& state,
                                             DeltaBuffer& province_delta) {
    if (province_idx >= state.provinces.size())
        return;

    const Province& province = state.provinces[province_idx];

    // Default weights used when facility type weights not available
    FacilityTypeSignalWeights default_weights{cfg_.default_weight, cfg_.default_weight,
                                              cfg_.default_weight, cfg_.default_weight};

    // Karst mitigation bonus for this province
    float karst_bonus = province.has_karst ? cfg_.karst_mitigation_bonus : 0.0f;

    // --- Phase 1: Compute signal composites for all facilities in this province ---
    // Collect businesses in this province, sorted by id for determinism
    std::vector<const NPCBusiness*> province_businesses;
    for (const auto& biz : state.npc_businesses) {
        if (biz.province_id == province.id) {
            province_businesses.push_back(&biz);
        }
    }
    std::sort(province_businesses.begin(), province_businesses.end(),
              [](const NPCBusiness* a, const NPCBusiness* b) { return a->id < b->id; });

    for (const NPCBusiness* biz : province_businesses) {
        // Find signal entry pre-populated by init_for_tick().
        FacilitySignals* sig = nullptr;
        for (auto& fs : facility_signals_) {
            if (fs.business_id == biz->id) {
                sig = &fs;
                break;
            }
        }
        if (!sig) {
            continue;  // Defensive: skip if not pre-populated (should not happen).
        }

        // Apply karst bonus to mitigation
        float effective_mitigation = sig->scrutiny_mitigation + karst_bonus;
        effective_mitigation = std::clamp(effective_mitigation, 0.0f, 1.0f);

        // Compute composite (use default weights in V1 bootstrap)
        sig->base_signal_composite = compute_signal_composite(
            sig->power_consumption_anomaly, sig->chemical_waste_signature,
            sig->foot_traffic_visibility, sig->olfactory_signature, default_weights);

        // Compute net signal
        sig->net_signal = compute_net_signal(sig->base_signal_composite, effective_mitigation);
    }

    // NOTE: This module is purely a *signal source*. It does not fill any NPC
    // meter. The InvestigatorMeter (criminal — terminates in raid/arrest) and
    // the regulator scrutiny meter (civil) are owned and advanced by
    // `investigator_engine` (runs_after this module), which reads facility
    // net_signals and manages case level/status/transitions. An earlier version
    // wrote the per-tick fill rates into law-enforcement and regulator NPCs'
    // `NPCDelta.motivation_delta` (the financial-gain behavior-weight slot) as a
    // meter stand-in — that polluted those NPCs' motivations and filled nothing
    // the rest of the system reads, so it has been removed.
    //
    // TODO(pipeline): the computed `facility_signals_` net_signals are not yet
    // surfaced to `investigator_engine`, which currently approximates them via
    // `regulatory_violation_severity`. Wiring them through requires a
    // WorldState/delta channel for per-facility signals and is tracked
    // separately.
}

void FacilitySignalsModule::execute(const WorldState& state, DeltaBuffer& delta) {
    for (uint32_t i = 0; i < state.provinces.size(); ++i) {
        execute_province(i, state, delta);
    }
}

}  // namespace econlife
