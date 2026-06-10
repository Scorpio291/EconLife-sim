#include "modules/facility_signals/facility_signals_module.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

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

// ---------------------------------------------------------------------------
// Pre-parallel initialization — populate signal entries for all businesses
// ---------------------------------------------------------------------------

void FacilitySignalsModule::init_for_tick(const WorldState& state) {
    std::unordered_set<uint32_t> known;
    known.reserve(facility_signals_.size());
    for (const auto& fs : facility_signals_) {
        known.insert(fs.business_id);
    }
    for (const auto& biz : state.npc_businesses) {
        if (!known.insert(biz.id).second) {
            continue;  // signal entry already exists
        }
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

    // Index the signal entries pre-populated by init_for_tick(). Thread-safe:
    // init_for_tick runs pre-parallel, so the vector does not grow here, and
    // each entry is written only by its business's home-province thread.
    std::unordered_map<uint32_t, FacilitySignals*> signal_by_business;
    signal_by_business.reserve(facility_signals_.size());
    for (auto& fs : facility_signals_) {
        signal_by_business.emplace(fs.business_id, &fs);
    }

    for (const NPCBusiness* biz : province_businesses) {
        auto it = signal_by_business.find(biz->id);
        if (it == signal_by_business.end()) {
            continue;  // Defensive: skip if not pre-populated (should not happen).
        }
        FacilitySignals* sig = it->second;

        // Apply karst bonus to mitigation
        float effective_mitigation = sig->scrutiny_mitigation + karst_bonus;
        effective_mitigation = std::clamp(effective_mitigation, 0.0f, 1.0f);

        // Compute composite (use default weights in V1 bootstrap)
        sig->base_signal_composite = compute_signal_composite(
            sig->power_consumption_anomaly, sig->chemical_waste_signature,
            sig->foot_traffic_visibility, sig->olfactory_signature, default_weights);

        // Bootstrap derivation: until facility-type signal profiles
        // (facility_types.csv) populate the four physical dimensions, businesses
        // carry all-zero dimensions and would emit no signal. Derive the
        // composite from regulatory_violation_severity so the detection pipeline
        // stays live; scrutiny mitigation still attenuates it below.
        if (sig->base_signal_composite <= 0.0f && biz->regulatory_violation_severity > 0.0f) {
            sig->base_signal_composite = std::clamp(biz->regulatory_violation_severity, 0.0f, 1.0f);
        }

        // Compute net signal
        sig->net_signal = compute_net_signal(sig->base_signal_composite, effective_mitigation);

        // Publish onto the business so investigator_engine (Tier 8) reads it
        // from WorldState this same tick. Publish only on change: apply_deltas
        // keeps biz.net_signal equal to the last published value.
        if (sig->net_signal != biz->net_signal) {
            BusinessDelta bd;
            bd.business_id = biz->id;
            bd.net_signal_update = sig->net_signal;
            province_delta.business_deltas.push_back(bd);
        }
    }

    // This module is a signal source only — the investigator/regulator meters
    // are owned and advanced by investigator_engine (see both INTERFACE.md).
}

void FacilitySignalsModule::execute(const WorldState& state, DeltaBuffer& delta) {
    for (uint32_t i = 0; i < state.provinces.size(); ++i) {
        execute_province(i, state, delta);
    }
}

}  // namespace econlife
