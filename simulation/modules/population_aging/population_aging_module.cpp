#include "population_aging_module.h"
#include "core/world_state/world_state.h"
#include "core/world_state/delta_buffer.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace econlife {

float PopulationAgingModule::compute_income_convergence(float current_income, float target_wage,
                                                         float rate) {
    return current_income + rate * (target_wage - current_income);
}

float PopulationAgingModule::compute_employment_convergence(float current_rate, float target_rate,
                                                             float rate) {
    return std::clamp(current_rate + rate * (target_rate - current_rate), 0.0f, 1.0f);
}

float PopulationAgingModule::compute_education_drift(float current_level, float province_level,
                                                      float max_drift) {
    float diff = province_level - current_level;
    float drift = std::clamp(diff, -max_drift, max_drift);
    return std::clamp(current_level + drift, 0.0f, 1.0f);
}

float PopulationAgingModule::compute_gini_coefficient(const std::vector<float>& sorted_incomes) {
    if (sorted_incomes.empty()) return 0.0f;

    float n = static_cast<float>(sorted_incomes.size());
    float total_income = 0.0f;
    for (float inc : sorted_incomes) total_income += inc;

    if (total_income <= 0.0f) return 0.0f;

    float weighted_sum = 0.0f;
    for (size_t i = 0; i < sorted_incomes.size(); ++i) {
        float rank = 2.0f * static_cast<float>(i) - static_cast<float>(sorted_incomes.size()) + 1.0f;
        weighted_sum += rank * sorted_incomes[i];
    }

    float gini = weighted_sum / (n * total_income);
    return std::clamp(gini, 0.0f, 1.0f);
}

bool PopulationAgingModule::is_monthly_tick(uint32_t current_tick) {
    return (current_tick % TICKS_PER_MONTH) == 0;
}

void PopulationAgingModule::execute_province(uint32_t province_idx,
                                              const WorldState& state,
                                              DeltaBuffer& province_delta) {
    if (province_idx >= state.provinces.size()) return;

    // Monthly cadence for cohort convergence
    if (!is_monthly_tick(state.current_tick)) return;

    // Province-level processing would update cohort incomes and employment
    // toward regional targets. Full implementation deferred to orchestrator pass.
}

void PopulationAgingModule::execute(const WorldState& state, DeltaBuffer& delta) {
    for (uint32_t i = 0; i < state.provinces.size(); ++i) {
        execute_province(i, state, delta);
    }
}

}  // namespace econlife
