#pragma once

#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "population_aging_types.h"

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class PopulationAgingModule : public ITickModule {
   public:
    explicit PopulationAgingModule(const PopulationAgingConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "population_aging"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    std::vector<std::string_view> runs_after() const override { return {"healthcare"}; }
    bool is_province_parallel() const noexcept override { return true; }
    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override;
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Static utilities for testing ---
    static float compute_income_convergence(float current_income, float target_wage, float rate);
    static float compute_employment_convergence(float current_rate, float target_rate, float rate);
    static float compute_education_drift(float current_level, float province_level,
                                         float max_drift);
    static float compute_gini_coefficient(const std::vector<float>& sorted_incomes);
    static bool is_monthly_tick(uint32_t current_tick);

    // Time calibration constants
    static constexpr uint32_t TICKS_PER_MONTH = 30;
    static constexpr uint32_t TICKS_PER_YEAR = 365;

   private:
    PopulationAgingConfig cfg_;
};

}  // namespace econlife
