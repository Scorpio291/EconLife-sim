#pragma once

#include <algorithm>
#include <cmath>
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

    // One province-year of plague (M6a, extended by R3B into recurrent waves).
    //
    // An outbreak arrives with a probability rising in the world's `disease` dial and in
    // urban crowding, and its severity scales with how much of the population has never
    // met the disease. That last part is what makes plague RECUR rather than blip:
    // England fell 4.8M (1348) -> 2.6M (1351) and kept falling to 1.9M by 1450, because
    // plague came back in 1361, 1369, 1375, 1390, 1400 and beyond — each wave milder
    // because it found fewer susceptibles, each interval long enough for a new generation
    // of them to be born.
    //
    // Neither the recurrence interval nor the declining lethality is written anywhere:
    // both fall out of `susceptible` being drawn down here and refilled by population
    // turnover. Pure given the RNG. (Disease is a pre-market population check; medicine
    // releases it in the modern era.)
    struct PlagueYear {
        float mortality_factor = 1.0f;    // >= 1.0; multiplies the annual death RATE
        float susceptible_after = 1.0f;   // the stock after this year's wave and turnover
        bool outbreak = false;            // whether a wave struck at all
    };
    static PlagueYear plague_year(float disease_dial, float urban_fraction, float susceptible,
                                  DeterministicRNG& rng, const PopulationAgingConfig& cfg);

    // Severity-only view of the above, kept for callers that do not track the stock
    // (equivalent to a fully susceptible population).
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

    // Chance a child born now reaches fifteen, given the annual death rate the young
    // actually face. Roughly half did not, before modern medicine. Pure/static.
    static float child_survival(float youth_annual_death_rate, const PopulationAgingConfig& cfg) {
        const float rate = std::max(0.0f, youth_annual_death_rate);
        if (std::isnan(rate))
            return 1.0f;  // crash sentinel only
        return std::exp(-rate * std::max(0.0f, cfg.child_survival_years));
    }

    // THE QUANTITY-QUALITY TRANSITION (R4A, Galor). Families target SURVIVING CHILDREN,
    // not births: `desired_births = target_surviving / P(survive)`. When half your
    // children die before fifteen you have many; when almost none do, you have few.
    //
    // Exactly 1.0 at the pre-modern survival this model's base birth rate is calibrated
    // against, above it when a world is harsher than that, and falling as medicine and
    // sanitation take hold — going from ~0.5 to ~0.9 cuts births by about 44% through
    // this channel alone. That substitution is what breaks the Malthusian income->births
    // feedback, and it is why the modern world escaped the trap instead of breeding into
    // every gain it made. Pure/static.
    static float desired_births_factor(float survival, const PopulationAgingConfig& cfg) {
        const float p = std::max(1e-3f, survival);  // divide-by-zero sentinel
        return std::max(0.0f, cfg.reference_child_survival) / p;
    }

    // THE URBAN GRAVEYARD: the EXTRA annual death hazard rate carried by living in a
    // town of `town_size` people, added to (not multiplied into) the background rate —
    // crowding is its own cause of death, the crowd's endemic disease, not an
    // amplification of everything else. Rises with the size of the town and saturates:
    // a hamlet is barely worse than the countryside, a city of 100k carries nearly the
    // whole burden, and the burden approaches the full rate without ever exceeding it,
    // so nothing here needs a cap.
    //
    // `medicine_mortality_mult` is the era's tech mortality multiplier. Sanitation and
    // germ theory are what actually closed the urban grave, so the same technology that
    // ends the plagues releases this — which is why urbanisation could not break past
    // its pre-modern tenth until medicine arrived. Pure/static.
    static float urban_crowding_rate(float town_size, float medicine_mortality_mult,
                                     const PopulationAgingConfig& cfg) {
        const float t = std::max(0.0f, town_size);
        const float crowding = t / (t + std::max(1.0f, cfg.urban_crowding_halfsat));
        return cfg.urban_crowding_death_rate * crowding * std::max(0.0f, medicine_mortality_mult);
    }

    // Time calibration constants
    static constexpr uint32_t TICKS_PER_MONTH = 30;
    static constexpr uint32_t TICKS_PER_YEAR = kTicksPerYear;  // canonical (shared_types.h)

   private:
    PopulationAgingConfig cfg_;
};

}  // namespace econlife
