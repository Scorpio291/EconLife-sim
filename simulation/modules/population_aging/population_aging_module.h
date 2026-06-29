#pragma once

#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "population_aging_types.h"

namespace econlife {

struct WorldState;
struct DeltaBuffer;
class DeterministicRNG;

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
    static bool is_annual_tick(uint32_t current_tick);
    // Size-weighted mean of cohort median_income (0 if no population).
    static float compute_mean_income(const std::map<DemographicGroup, PopulationCohort>& cohorts);
    // Annual natural-death probability for a significant NPC. 0 below lifespan;
    // past it, base_prob scaled up with years over lifespan. (NPC health is not
    // a WorldState field, so age is the sole driver.)
    static float compute_natural_death_probability(float age, float lifespan, float base_prob);

    // Episodic disease mortality multiplier (>= 1.0) for one province-year (M6a, the
    // first world-classification hazard brought in distinctly). With probability
    // rising in the world's `disease` dial and urban crowding, an outbreak strikes a
    // mortality spike; otherwise 1.0. Pure given the RNG. (Disease is a pre-market
    // population check; medicine releases it in the modern era.)
    static float epidemic_mortality_factor(float disease_dial, float urban_fraction,
                                           DeterministicRNG& rng, const PopulationAgingConfig& cfg);

    // Episodic geology-disaster mortality multiplier (>= 1.0) for one province-year
    // (M6a): quakes/storms/wildfires scaled by the world's `geology` dial — NOT
    // density-dependent (a disaster hits everyone). Pure given the RNG.
    static float disaster_mortality_factor(float geology_dial, DeterministicRNG& rng,
                                           const PopulationAgingConfig& cfg);

    // Time calibration constants
    static constexpr uint32_t TICKS_PER_MONTH = 30;
    static constexpr uint32_t TICKS_PER_YEAR = 365;

   private:
    PopulationAgingConfig cfg_;
};

}  // namespace econlife
