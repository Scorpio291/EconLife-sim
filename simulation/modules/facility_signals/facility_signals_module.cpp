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

InvestigatorMeterStatus FacilitySignalsModule::evaluate_investigator_status(float current_level) {
    if (current_level >= Constants::raid_threshold)
        return InvestigatorMeterStatus::raid_imminent;
    if (current_level >= Constants::formal_inquiry_threshold)
        return InvestigatorMeterStatus::formal_inquiry;
    if (current_level >= Constants::surveillance_threshold)
        return InvestigatorMeterStatus::surveillance;
    return InvestigatorMeterStatus::inactive;
}

RegulatorMeterStatus FacilitySignalsModule::evaluate_regulator_status(float current_level) {
    if (current_level >= Constants::enforcement_threshold)
        return RegulatorMeterStatus::enforcement_action;
    if (current_level >= Constants::audit_threshold)
        return RegulatorMeterStatus::formal_audit;
    if (current_level >= Constants::notice_threshold)
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
// Province-parallel execution
// ---------------------------------------------------------------------------

void FacilitySignalsModule::execute_province(uint32_t province_idx, const WorldState& state,
                                             DeltaBuffer& province_delta) {
    if (province_idx >= state.provinces.size())
        return;

    const Province& province = state.provinces[province_idx];

    // Default weights used when facility type weights not available
    FacilityTypeSignalWeights default_weights{Constants::default_weight, Constants::default_weight,
                                              Constants::default_weight, Constants::default_weight};

    // Karst mitigation bonus for this province
    float karst_bonus = province.has_karst ? Constants::karst_mitigation_bonus : 0.0f;

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

    // Aggregate criminal net_signal for LE meter fill
    float criminal_signal_sum = 0.0f;

    for (const NPCBusiness* biz : province_businesses) {
        // Find or create signal entry in module-internal storage
        FacilitySignals* sig = nullptr;
        for (auto& fs : facility_signals_) {
            if (fs.business_id == biz->id) {
                sig = &fs;
                break;
            }
        }
        if (!sig) {
            facility_signals_.push_back(FacilitySignals{});
            sig = &facility_signals_.back();
            sig->facility_id = biz->id;
            sig->business_id = biz->id;
            sig->power_consumption_anomaly = 0.0f;
            sig->chemical_waste_signature = 0.0f;
            sig->foot_traffic_visibility = 0.0f;
            sig->olfactory_signature = 0.0f;
            sig->scrutiny_mitigation = 0.0f;
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

        // Accumulate criminal signal for LE meter
        if (biz->criminal_sector && sig->net_signal > 0.0f) {
            criminal_signal_sum += sig->net_signal;
        }
    }

    // --- Phase 2: Update LE investigator meters ---
    float regional_signal = criminal_signal_sum / Constants::facility_count_normalizer;
    float base_le_fill_rate = compute_le_fill_rate(
        regional_signal, Constants::detection_to_fill_rate_scale, Constants::fill_rate_max);

    std::vector<const NPC*> le_npcs;
    for (const auto& npc : state.significant_npcs) {
        if (npc.role == NPCRole::law_enforcement && npc.current_province_id == province.id &&
            npc.status == NPCStatus::active) {
            le_npcs.push_back(&npc);
        }
    }
    std::sort(le_npcs.begin(), le_npcs.end(),
              [](const NPC* a, const NPC* b) { return a->id < b->id; });

    for (const NPC* le_npc : le_npcs) {
        float fill_rate = base_le_fill_rate;

        if (criminal_signal_sum <= 0.0f) {
            fill_rate = -Constants::meter_decay_rate;
        }

        NPCDelta delta;
        delta.npc_id = le_npc->id;
        delta.motivation_delta = fill_rate;
        province_delta.npc_deltas.push_back(delta);
    }

    // --- Phase 3: Update regulator scrutiny meters ---
    std::vector<const NPC*> reg_npcs;
    for (const auto& npc : state.significant_npcs) {
        if (npc.role == NPCRole::regulator && npc.current_province_id == province.id &&
            npc.status == NPCStatus::active) {
            reg_npcs.push_back(&npc);
        }
    }
    std::sort(reg_npcs.begin(), reg_npcs.end(),
              [](const NPC* a, const NPC* b) { return a->id < b->id; });

    // Regulators read chemical_waste and foot_traffic dimensions only
    float regulatory_signal_sum = 0.0f;
    for (const NPCBusiness* biz : province_businesses) {
        for (const auto& fs : facility_signals_) {
            if (fs.business_id == biz->id) {
                float reg_signal =
                    (fs.chemical_waste_signature + fs.foot_traffic_visibility) * 0.5f;
                float effective_mit = fs.scrutiny_mitigation + karst_bonus;
                effective_mit = std::clamp(effective_mit, 0.0f, 1.0f);
                float net_reg = std::max(0.0f, reg_signal - effective_mit);
                regulatory_signal_sum += net_reg;
                break;
            }
        }
    }

    float reg_fill_rate =
        compute_le_fill_rate(regulatory_signal_sum / Constants::facility_count_normalizer,
                             Constants::detection_to_fill_rate_scale, Constants::fill_rate_max);

    for (const NPC* reg_npc : reg_npcs) {
        float fill = (regulatory_signal_sum > 0.0f) ? reg_fill_rate : -Constants::meter_decay_rate;

        NPCDelta delta;
        delta.npc_id = reg_npc->id;
        delta.motivation_delta = fill;
        province_delta.npc_deltas.push_back(delta);
    }
}

void FacilitySignalsModule::execute(const WorldState& state, DeltaBuffer& delta) {
    for (uint32_t i = 0; i < state.provinces.size(); ++i) {
        execute_province(i, state, delta);
    }
}

}  // namespace econlife
