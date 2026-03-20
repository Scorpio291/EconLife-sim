#include "regional_conditions_module.h"
#include "core/world_state/world_state.h"
#include "core/world_state/delta_buffer.h"
#include <algorithm>
#include <cmath>

namespace econlife {

float RegionalConditionsModule::compute_stability_recovery(float current_stability,
                                                            uint32_t instability_events) {
    float recovery = STABILITY_RECOVERY_RATE * (1.0f - current_stability);
    float degradation = static_cast<float>(instability_events) * EVENT_STABILITY_IMPACT;
    float result = current_stability + recovery - degradation;
    return std::clamp(result, 0.0f, 1.0f);
}

float RegionalConditionsModule::compute_criminal_dominance(float criminal_revenue,
                                                            float total_revenue) {
    if (total_revenue <= 0.0f) return 0.0f;
    return std::clamp(criminal_revenue / total_revenue, 0.0f, 1.0f);
}

float RegionalConditionsModule::compute_drought_recovery(float current_modifier,
                                                          float recovery_rate) {
    float result = current_modifier + recovery_rate;
    return std::clamp(result, 0.0f, 1.0f);
}

float RegionalConditionsModule::compute_inequality_from_gini(float gini_coefficient) {
    return std::clamp(gini_coefficient, 0.0f, 1.0f);
}

void RegionalConditionsModule::execute_province(uint32_t province_idx,
                                                 const WorldState& state,
                                                 DeltaBuffer& province_delta) {
    if (province_idx >= state.provinces.size()) return;

    const auto& province = state.provinces[province_idx];
    const auto& conditions = province.conditions;
    const auto& community = province.community;

    // Stability: recover toward 1.0, degraded by community response stage
    uint32_t instability_events = 0;
    if (community.response_stage >= 3) instability_events++;
    if (community.response_stage >= 5) instability_events++;

    RegionDelta rdelta;
    rdelta.region_id = province_idx;

    float new_stability = compute_stability_recovery(conditions.stability_score, instability_events);
    rdelta.stability_delta = new_stability - conditions.stability_score;

    // Drought/flood recovery
    if (conditions.drought_modifier < 1.0f) {
        // Recovery toward 1.0
        float recovered = compute_drought_recovery(conditions.drought_modifier, DROUGHT_RECOVERY_RATE);
        // Store as delta (not directly available in RegionDelta, handled through conditions update)
    }

    province_delta.region_deltas.push_back(rdelta);
}

void RegionalConditionsModule::execute(const WorldState& state, DeltaBuffer& delta) {
    for (uint32_t i = 0; i < state.provinces.size(); ++i) {
        execute_province(i, state, delta);
    }
}

}  // namespace econlife
