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

    // Annual death PROBABILITY from a composed annual death RATE (expected deaths per
    // person-year): the Poisson first-arrival p = 1 - exp(-rate). This is where cohort
    // mortality is bounded, and the bound is physical — p rises toward 100% and can
    // never exceed it, however large the rate gets. No cap anywhere in the rate chain
    // (grounding doctrine: "a probability arrives as 1 - exp(-rate)").
    // A non-finite rate yields 0 (crash sentinel only — a NaN must not annihilate a
    // cohort); +inf yields exactly 1.0, which is the correct limit.
    static float annual_probability_from_rate(float annual_rate);

    // Maladaptation multiplier on the mortality rate: how far a population's
    // adaptation falls short of what its world demands (world_hazard / hardiness, both
    // Earth-normalized, so an adapted people sits at exactly 1.0). UNCAPPED by design —
    // a wholly-unadapted people faces an arbitrarily high hazard *rate*; it is the
    // resulting *probability* that saturates. hardiness_floor is the divide-by-zero
    // sentinel, not an outcome bound.
    static float hazard_rate_multiplier(float world_hazard, float hardiness, float hardiness_floor);

    // Chronic fertility multiplier (<= 1.0) from ambient radiation (M6a chronic split):
    // a distinct channel from the background mortality scalar — radiation kills AND
    // suppresses births. Planetary; never wanes. Scaled by the `radiation` dial.
    static float radiation_fertility_factor(float radiation_dial,
                                            const PopulationAgingConfig& cfg);

    // Time calibration constants
    static constexpr uint32_t TICKS_PER_MONTH = 30;
    static constexpr uint32_t TICKS_PER_YEAR = kTicksPerYear;  // canonical (shared_types.h)

   private:
    PopulationAgingConfig cfg_;
};

}  // namespace econlife
