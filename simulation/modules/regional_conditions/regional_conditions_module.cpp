#include "regional_conditions_module.h"

#include <algorithm>
#include <cmath>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"

namespace econlife {

float RegionalConditionsModule::compute_stability_recovery(float current_stability,
                                                           uint32_t instability_events) {
    constexpr RegionalConditionsConfig kDefaults{};
    float recovery = kDefaults.stability_recovery_rate * (1.0f - current_stability);
    float degradation = static_cast<float>(instability_events) * kDefaults.event_stability_impact;
    float result = current_stability + recovery - degradation;
    return std::clamp(result, 0.0f, 1.0f);
}

float RegionalConditionsModule::compute_criminal_dominance(float criminal_revenue,
                                                           float total_revenue) {
    if (total_revenue <= 0.0f)
        return 0.0f;
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

void RegionalConditionsModule::execute_province(uint32_t province_idx, const WorldState& state,
                                                DeltaBuffer& province_delta) {
    if (province_idx >= state.provinces.size())
        return;

    const auto& province = state.provinces[province_idx];
    const auto& conditions = province.conditions;
    const auto& community = province.community;
    const auto& demographics = province.demographics;

    RegionDelta rdelta;
    rdelta.region_id = province_idx;

    // --- Stability ---
    // Degraded by community response stage; recovers passively toward 1.0.
    uint32_t instability_events = 0;
    if (community.response_stage >= 3)
        instability_events++;
    if (community.response_stage >= 5)
        instability_events++;

    float new_stability =
        compute_stability_recovery(conditions.stability_score, instability_events);
    rdelta.stability_delta = new_stability - conditions.stability_score;

    // --- Inequality ---
    // Weighted aggregation from income distribution fractions.
    // High income concentration (large high-income fraction, small low-income fraction)
    // pushes inequality upward. Use a simple Gini proxy: spread between high and low fraction.
    {
        float raw_inequality = demographics.income_high_fraction - demographics.income_low_fraction;
        // Clamp raw signal to [0, 1], then compute delta toward it at slow convergence rate.
        float target_inequality = std::clamp(raw_inequality, 0.0f, 1.0f);
        // Nudge current inequality toward the target by a small fraction per tick.
        constexpr float INEQUALITY_CONVERGENCE_RATE = 0.001f;
        rdelta.inequality_delta =
            INEQUALITY_CONVERGENCE_RATE * (target_inequality - conditions.inequality_index);
    }

    // --- Crime rate ---
    // Criminal dominance drives crime rate upward; stability suppresses it.
    // Drift toward (criminal_dominance * (1 - stability)) at slow rate.
    {
        float target_crime =
            conditions.criminal_dominance_index * (1.0f - conditions.stability_score);
        constexpr float CRIME_CONVERGENCE_RATE = 0.002f;
        rdelta.crime_rate_delta = CRIME_CONVERGENCE_RATE * (target_crime - conditions.crime_rate);
    }

    // --- Addiction rate ---
    // Addiction rate drifts toward the province's observed crime rate as a proxy
    // for substance availability; decays naturally at a slow base rate.
    {
        constexpr float ADDICTION_DECAY_RATE = 0.0005f;
        constexpr float ADDICTION_CRIME_COUPLING = 0.001f;
        rdelta.addiction_rate_delta = ADDICTION_CRIME_COUPLING * conditions.crime_rate -
                                      ADDICTION_DECAY_RATE * conditions.addiction_rate;
    }

    // --- Criminal dominance ---
    // Criminal revenue proxy: crime_rate as fraction of all economic activity.
    // Recalculate criminal dominance from current conditions.
    {
        float criminal_revenue_proxy = conditions.crime_rate;
        // Total revenue proxy = 1.0 (normalised); criminal share is the crime rate itself.
        float new_dominance = compute_criminal_dominance(criminal_revenue_proxy, 1.0f);
        constexpr float DOMINANCE_CONVERGENCE_RATE = 0.002f;
        rdelta.criminal_dominance_delta =
            DOMINANCE_CONVERGENCE_RATE * (new_dominance - conditions.criminal_dominance_index);
    }

    // --- Social cohesion ---
    // Cohesion is eroded by high grievance and boosted by low crime and high stability.
    {
        constexpr float COHESION_GRIEVANCE_WEIGHT = -0.001f;
        constexpr float COHESION_STABILITY_WEIGHT = 0.001f;
        rdelta.cohesion_delta = COHESION_GRIEVANCE_WEIGHT * community.grievance_level +
                                COHESION_STABILITY_WEIGHT * conditions.stability_score;
    }

    // --- Grievance ---
    // Grievance rises with high inequality and crime; decays passively.
    // Community response stage amplifies grievance above stage 2.
    {
        constexpr float GRIEVANCE_DECAY_RATE = 0.0005f;
        constexpr float GRIEVANCE_INEQUALITY_WEIGHT = 0.001f;
        constexpr float GRIEVANCE_CRIME_WEIGHT = 0.001f;
        constexpr float GRIEVANCE_STAGE_WEIGHT = 0.002f;
        float stage_contribution =
            (community.response_stage > 2)
                ? static_cast<float>(community.response_stage - 2) * GRIEVANCE_STAGE_WEIGHT
                : 0.0f;
        rdelta.grievance_delta = GRIEVANCE_INEQUALITY_WEIGHT * conditions.inequality_index +
                                 GRIEVANCE_CRIME_WEIGHT * conditions.crime_rate +
                                 stage_contribution -
                                 GRIEVANCE_DECAY_RATE * community.grievance_level;
    }

    // --- Institutional trust ---
    // Eroded by corruption and high crime; boosted by high stability.
    {
        constexpr float TRUST_CORRUPTION_WEIGHT = -0.001f;
        constexpr float TRUST_CRIME_WEIGHT = -0.001f;
        constexpr float TRUST_STABILITY_WEIGHT = 0.0005f;
        rdelta.institutional_trust_delta =
            TRUST_CORRUPTION_WEIGHT * province.political.corruption_index +
            TRUST_CRIME_WEIGHT * conditions.crime_rate +
            TRUST_STABILITY_WEIGHT * conditions.stability_score;
    }

    province_delta.region_deltas.push_back(rdelta);
}

void RegionalConditionsModule::execute(const WorldState& state, DeltaBuffer& delta) {
    for (uint32_t i = 0; i < state.provinces.size(); ++i) {
        execute_province(i, state, delta);
    }
}

}  // namespace econlife
