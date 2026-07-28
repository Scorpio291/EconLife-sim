#include "population_aging_module.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "core/rng/deterministic_rng.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"

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
    if (sorted_incomes.empty())
        return 0.0f;

    float n = static_cast<float>(sorted_incomes.size());
    float total_income = 0.0f;
    for (float inc : sorted_incomes)
        total_income += inc;

    if (total_income <= 0.0f)
        return 0.0f;

    float weighted_sum = 0.0f;
    for (size_t i = 0; i < sorted_incomes.size(); ++i) {
        float rank =
            2.0f * static_cast<float>(i) - static_cast<float>(sorted_incomes.size()) + 1.0f;
        weighted_sum += rank * sorted_incomes[i];
    }

    float gini = weighted_sum / (n * total_income);
    return std::clamp(gini, 0.0f, 1.0f);
}

bool PopulationAgingModule::is_monthly_tick(uint32_t current_tick) {
    return (current_tick % TICKS_PER_MONTH) == 0;
}

bool PopulationAgingModule::is_annual_tick(uint32_t current_tick) {
    return (current_tick % TICKS_PER_YEAR) == 0;
}

float PopulationAgingModule::epidemic_mortality_factor(float disease_dial, float urban_fraction,
                                                       DeterministicRNG& rng,
                                                       const PopulationAgingConfig& cfg) {
    const float d = std::clamp(disease_dial, 0.0f, 1.0f);
    if (d <= 0.0f)
        return 1.0f;
    const float density = std::clamp(urban_fraction, 0.0f, 1.0f);
    // Outbreak hazard rate rises with the world's disease load AND crowding (towns
    // are disease vectors — so disease brakes urbanization). The annual probability
    // is the Poisson arrival 1 - exp(-rate): physically in [0,1) with no cap.
    const float rate = cfg.epidemic_base_rate * d * (1.0f + cfg.epidemic_density_weight * density);
    const float p = 1.0f - std::exp(-std::max(0.0f, rate));
    if (rng.next_float() >= p)
        return 1.0f;  // no outbreak this year
    // Outbreak: a mortality spike scaled by the disease load and the crowding.
    return 1.0f + cfg.epidemic_severity * d * (1.0f + density);
}

float PopulationAgingModule::disaster_mortality_factor(float geology_dial, DeterministicRNG& rng,
                                                       const PopulationAgingConfig& cfg) {
    const float g = std::clamp(geology_dial, 0.0f, 1.0f);
    if (g <= 0.0f)
        return 1.0f;
    // Poisson arrival: probability = 1 - exp(-rate); physical, uncapped.
    const float p = 1.0f - std::exp(-std::max(0.0f, cfg.geology_disaster_base_rate * g));
    if (rng.next_float() >= p)
        return 1.0f;  // no disaster this year
    return 1.0f + cfg.geology_disaster_severity * g;  // quake/storm/wildfire mortality spike
}

float PopulationAgingModule::annual_probability_from_rate(float annual_rate) {
    // Poisson first-arrival: with a hazard rate of `annual_rate` deaths per person-year,
    // the chance of dying at least once during the year is 1 - exp(-rate). Physically
    // self-bounding in [0, 1) — the reason no cap is needed on the rate.
    if (std::isnan(annual_rate))
        return 0.0f;  // crash sentinel only: a NaN rate must not annihilate a cohort
    if (annual_rate <= 0.0f)
        return 0.0f;
    return 1.0f - std::exp(-annual_rate);  // +inf -> exactly 1.0
}

float PopulationAgingModule::hazard_rate_multiplier(float world_hazard, float hardiness,
                                                    float hardiness_floor) {
    // std::max is the divide-by-zero sentinel, NOT an outcome bound: the ratio itself is
    // deliberately uncapped (see annual_probability_from_rate for the physical bound).
    return world_hazard / std::max(hardiness, hardiness_floor);
}

float PopulationAgingModule::radiation_fertility_factor(float radiation_dial,
                                                        const PopulationAgingConfig& cfg) {
    const float r = std::clamp(radiation_dial, 0.0f, 1.0f);
    return std::max(0.0f, 1.0f - cfg.radiation_fertility_penalty * r);
}

float PopulationAgingModule::compute_natural_death_probability(float age, float lifespan,
                                                               float base_prob) {
    if (age < lifespan)
        return 0.0f;
    float over = 1.0f + (age - lifespan) * 0.05f;  // +5% of base per year past lifespan
    return std::clamp(base_prob * over, 0.0f, 1.0f);
}

float PopulationAgingModule::compute_mean_income(
    const std::map<DemographicGroup, PopulationCohort>& cohorts) {
    double weighted = 0.0;
    uint64_t total = 0;
    for (const auto& [group, c] : cohorts) {
        (void)group;
        weighted += static_cast<double>(c.size) * static_cast<double>(c.median_income);
        total += c.size;
    }
    if (total == 0)
        return 0.0f;
    return static_cast<float>(weighted / static_cast<double>(total));
}

namespace {

bool is_retiree_group(DemographicGroup g) {
    return g == DemographicGroup::retiree_urban || g == DemographicGroup::retiree_rural;
}

// Annual births (added to youth cohorts) and per-cohort deaths, applied in
// place to a working copy of the cohort map. Deterministic: integer rounding,
// canonical group order.
// birth_surplus drives fertility (long-run food security: can the land sustainably
// support more people, reserves included?). famine_surplus drives starvation mortality
// (immediate availability, buffered by the granary). They differ in the commons: a
// society at its sustainable ceiling has birth_surplus ~1 (stops growing) but, with full
// granaries, famine_surplus ~1 (nobody starves). Both are 1.0 in market eras (neutral).
// hazard_rate_mult scales the mortality RATE (it is not a probability): see
// PopulationAgingModule::hazard_rate_multiplier.
void process_births_deaths(std::map<DemographicGroup, PopulationCohort>& cohorts, float stability,
                           float sick_rate, float addiction_rate, float birth_surplus,
                           float famine_surplus, float hazard_rate_mult, float fertility_mult,
                           float war_death_fraction, const PopulationAgingConfig& cfg) {
    uint64_t total = 0;
    for (const auto& [g, c] : cohorts) {
        (void)g;
        total += c.size;
    }

    // Food coupling (the Malthusian loop). All factors are NEUTRAL at surplus == 1.0,
    // so market eras (where surplus is always 1.0) are unchanged. A surplus lifts
    // births toward a cap AND relieves mortality (well-fed survival); a deficit
    // (surplus < 1) raises mortality.
    // THE WAGE VALVE. Fertility and mortality both answer to how well fed people are,
    // as power laws in w = consumption/subsistence, so the growth rate is the point
    // where they cross rather than a number imposed anywhere. A society exactly at
    // subsistence (w = 1) has both factors at 1 and stops growing on its own; one with
    // slack grows; one pressing on its land shrinks. This is what pinned real
    // pre-industrial growth near 0.04%/yr while population tripled.
    const float b_surplus = std::clamp(birth_surplus, 0.0f, 10.0f);
    const float birth_food_factor =
        std::clamp(std::pow(std::max(b_surplus, 1e-3f), cfg.wage_fertility_elasticity), 0.0f,
                   cfg.food_surplus_birth_cap);
    const float f_surplus = std::clamp(famine_surplus, 0.0f, 10.0f);
    float famine_mortality_factor;
    if (f_surplus < 1.0f) {
        // Hunger kills long before outright famine: mortality rises as a power law in
        // the shortfall, on top of the acute famine term below.
        famine_mortality_factor =
            std::pow(std::max(f_surplus, 1e-3f), -cfg.wage_mortality_elasticity) +
            cfg.food_deficit_mortality_strength * (1.0f - f_surplus) - 1.0f;
        famine_mortality_factor = std::max(1.0f, famine_mortality_factor);
    } else {
        famine_mortality_factor = std::max(
            cfg.food_mortality_floor, 1.0f - cfg.food_surplus_mortality_relief * (f_surplus - 1.0f));
    }

    // Births: survival scales with stability and healthcare (proxied by the
    // inverse of sick_rate, since HealthcareProfile is not on WorldState).
    float healthcare_proxy = std::clamp(1.0f - sick_rate, 0.0f, 1.0f);
    float birth_rate = cfg.base_annual_birth_rate * std::clamp(stability, 0.0f, 1.0f) *
                       healthcare_proxy * birth_food_factor *
                       std::clamp(fertility_mult, 0.0f, 1.0f);  // radiation depresses fertility (M6a)
    auto births = static_cast<uint32_t>(
        std::llround(static_cast<double>(total) * static_cast<double>(birth_rate)));
    cohorts[DemographicGroup::youth_urban].size += births / 2u;
    cohorts[DemographicGroup::youth_rural].size += births - births / 2u;

    // Deaths. Mortality is composed as an annual HAZARD RATE (expected deaths per
    // person-year) — instability, addiction, famine and the world's hazards each scale
    // the rate — and the annual death PROBABILITY is the Poisson first-arrival
    // 1 - exp(-rate). That conversion is the bound: mortality approaches 100% and can
    // never exceed it, so nothing in the chain needs a cap.
    float mortality_rate_env = (1.0f + (1.0f - std::clamp(stability, 0.0f, 1.0f))) *
                               (1.0f + std::clamp(addiction_rate, 0.0f, 1.0f)) *
                               famine_mortality_factor *
                               hazard_rate_mult;  // per-setting world hazards (1.0 = earthlike)
    // War casualties are an INDEPENDENT competing risk, published by warfare in real
    // units (battle dead / population; G2). Compose SURVIVALS instead of adding
    // probabilities: S = (1 - p_env) * (1 - p_war). This is exactly the addition of the
    // two hazard RATES — 1 - (1-p_env)(1-p_war) == 1 - exp(-(rate_env + rate_war)) with
    // rate_war = -ln(1 - p_war) — so nothing is double-counted, and the publisher's
    // contract stays exact at both ends: with no environmental deaths a cohort loses
    // precisely the published fraction, and a published 1.0 still annihilates it. (The
    // old form added the two probabilities, which could exceed 1.0 and leaned on the
    // cohort-size floor below to stay physical; at Earth-normal rates the two agree to
    // within p_env * p_war, i.e. under 1% of the war term.)
    const float p_war = std::clamp(war_death_fraction, 0.0f, 1.0f);  // domain sentinel: it is
                                                                     // published as a fraction;
                                                                     // warfare owns its size
    for (auto& [g, c] : cohorts) {
        if (c.size == 0)
            continue;
        float death_rate = cfg.base_annual_death_rate * mortality_rate_env;
        if (is_retiree_group(g))
            death_rate *= cfg.retiree_mortality_multiplier;  // frailer bodies: a higher rate
        const float p_env = PopulationAgingModule::annual_probability_from_rate(death_rate);
        const float p_death = 1.0f - (1.0f - p_env) * (1.0f - p_war);
        auto deaths = static_cast<uint32_t>(
            std::llround(static_cast<double>(c.size) * static_cast<double>(p_death)));
        // A cohort cannot lose more people than it has (the remaining physical bound).
        c.size = (deaths >= c.size) ? 0u : (c.size - deaths);
    }
}

}  // namespace

void PopulationAgingModule::execute_province(uint32_t province_idx, const WorldState& state,
                                             DeltaBuffer& province_delta) {
    if (province_idx >= state.provinces.size())
        return;

    const auto& province = state.provinces[province_idx];

    // --- Significant-NPC aging (annual) ----------------------------------------
    // Advance age one year and roll natural death for NPCs past their lifespan.
    // Retirement role transitions are NOT modelled: there is no `retired`
    // NPCRole/status in V1 (documented gap; see population_aging/INTERFACE.md).
    if (is_annual_tick(state.current_tick)) {
        DeterministicRNG tick_rng(state.world_seed ^ static_cast<uint64_t>(state.current_tick));
        for (const auto& npc : state.significant_npcs) {
            if (npc.status != NPCStatus::active || npc.current_province_id != province.id)
                continue;
            NPCDelta nd;
            nd.npc_id = npc.id;
            nd.age_delta = 1.0f;
            float death_p = compute_natural_death_probability(
                npc.age_years, cfg_.natural_lifespan_years, cfg_.natural_death_annual_prob);
            if (death_p > 0.0f) {
                DeterministicRNG npc_rng = tick_rng.fork(npc.id);
                if (npc_rng.next_float() < death_p)
                    nd.new_status = NPCStatus::dead;
            }
            province_delta.npc_deltas.push_back(nd);
        }
    }

    // --- Background-population cohort lifecycle ---------------------------------
    // Monthly: income + employment convergence. Annual: education drift +
    // births/deaths. Aggregates (total_population, mean_income, gini) recomputed
    // after any change. Skipped entirely for unseeded (empty) or zero-population
    // provinces.
    const bool monthly = is_monthly_tick(state.current_tick);
    const bool annual = is_annual_tick(state.current_tick);
    if (province.cohort_stats && !province.cohort_stats->cohorts.empty() && (monthly || annual)) {
        const RegionCohortStats& cs = *province.cohort_stats;
        uint64_t pop = 0;
        for (const auto& [g, c] : cs.cohorts) {
            (void)g;
            pop += c.size;
        }
        if (pop > 0) {
            std::map<DemographicGroup, PopulationCohort> next = cs.cohorts;
            float new_hardiness = cs.hardiness;  // preserved unless the annual drift updates it

            if (monthly) {
                for (auto& [g, c] : next) {
                    (void)g;
                    if (c.size == 0)
                        continue;
                    c.median_income = compute_income_convergence(
                        c.median_income, cs.regional_wage_anchor, cfg_.cohort_income_update_rate);
                    c.employment_rate =
                        compute_employment_convergence(c.employment_rate, cs.formal_employment_rate,
                                                       cfg_.cohort_employment_update_rate);
                }
            }
            if (annual) {
                for (auto& [g, c] : next) {
                    (void)g;
                    if (c.size == 0)
                        continue;
                    c.education_level = compute_education_drift(
                        c.education_level, province.demographics.education_level,
                        cfg_.max_education_drift_per_year);
                }
                const EraDefinition* era =
                    state.era_catalog.by_index(state.technology.current_era);
                // Pre-market (commons) demographics span the whole agrarian arc — through
                // the money/coinage eras and the feudal/mercantile/industrial regimes —
                // until the modern market economy takes over.
                const bool commons =
                    era && (era->economic_regime == "subsistence" ||
                            era->economic_regime == "barter" || era->economic_regime == "coinage" ||
                            era->economic_regime == "money" || era->economic_regime == "feudal" ||
                            era->economic_regime == "mercantile" ||
                            era->economic_regime == "industrial");

                // Pre-market demographics are FOOD-driven, not politics-driven. A dawn
                // society has no modern institutions, and its intrinsic growth was ~zero
                // (Malthusian stagnation: high fertility balanced by high mortality) —
                // population moved only with the harvest. So commons demographics use a
                // FIXED baseline (the birth==death point), letting the food/reserve
                // signal alone govern growth; otherwise the modern stability proxy leaks
                // a permanent intrinsic growth that overshoots the food ceiling and traps
                // the society at bare subsistence.
                float eff_stability = province.conditions.stability_score;
                if (commons)
                    eff_stability = cfg_.commons_stability_floor;

                // The world's hazard pressure (disease/predators/radiation/atmosphere/
                // geology/gravity-falls), Earth-normalized.
                const float world_hazard = hazard_mortality_from_settings(state.hazard_settings);
                // Generational hardiness: the mortality RATE scales with how far the
                // population's adaptation falls short of what the world demands. A people
                // matched to their world (hardiness == world_hazard) is neutral; a soft
                // people on a hard world (hardiness << world_hazard) is culled until they
                // adapt; an over-hardened people survive better. So harshness is relative
                // to the adapted population.
                //
                // --- Calibration: Earth-normal preserved, the transient un-softened -----
                // The rate coefficient is base_annual_death_rate itself — 0.008 deaths per
                // person-year, a real unit (Earth's crude death rate is ~0.0076/yr) — and
                // the maladaptation term is dimensionless and exactly 1.0 for an adapted
                // people (hazard_mortality_from_settings is Earth-normalized), so no new
                // coefficient is needed and Earth-normal mortality is preserved by
                // construction. Earth-normal, stability 0.9, unaddicted, fed:
                //     rate = 0.008 * (1 + 0.1) * 1.0 * 1.0 = 0.0088 per person-year
                //     was (rate used directly AS a probability): p = 0.008800
                //     now  p = 1 - exp(-0.0088)                  = 0.0087614  (-0.45% rel.)
                //     retirees (x4): rate 0.0352 -> was 0.035200, now 0.034587 (-1.74% rel.)
                // i.e. Earth-normal cohort deaths fall by under 2%; on a 100k cohort that
                // is 876 deaths/yr instead of 880.
                // Where the retired [0.15, 3.0] band actually bound — the multi-decade
                // maladaptation transient that IS the deathworld-colonisation arc — the
                // change is deliberate and large. Garden-bred hardiness 0.19 on a
                // deathworld (world_hazard 1.076) gives a ratio of 5.66, formerly pinned
                // to 3.0 (a 1.9x softening of the culling):
                //     rate = 0.008 * 1.1 * 5.66 = 0.0498 -> p = 4.86%/yr (was 2.64%)
                // and the ceiling is now physical: as the ratio grows p -> 100%, never
                // past it, and it is reached by dying, not by hitting a number.
                float hazard_rate_mult =
                    hazard_rate_multiplier(world_hazard, cs.hardiness, cfg_.hardiness_floor);
                // Medicine (germ theory, sanitation, …) from the tech tree cuts the
                // mortality RATE — halving the rate halves expected deaths. Applied to the
                // rate, before the rate->probability conversion, so (unlike under the old
                // band, which medicine was applied AFTER and could therefore push the
                // result below the supposed minimum anyway) there is no ordering subtlety.
                hazard_rate_mult *=
                    state.tech_effects_for_era(state.technology.current_era).mortality_mult;

                // Disease epidemics (M6a): an episodic mortality spike in the pre-market
                // (commons) arc, scaled by the world's disease dial and urban crowding —
                // the plague dips real population history shows. Medicine releases
                // disease-as-population-check in the modern era (commons == false there).
                if (commons && annual) {
                    const float urban_frac =
                        cs.total_population > 0
                            ? cs.urban_population / static_cast<float>(cs.total_population)
                            : 0.0f;
                    DeterministicRNG epi_rng(state.world_seed ^
                                             (static_cast<uint64_t>(state.current_tick) *
                                              0x9E3779B97F4A7C15ull) ^
                                             (static_cast<uint64_t>(province.id) << 17) ^
                                             0xED1DEC1Cull);
                    hazard_rate_mult *= epidemic_mortality_factor(state.hazard_settings.disease,
                                                                  urban_frac, epi_rng, cfg_);
                    // Geology disasters (quakes/storms/wildfires) — a separate episodic
                    // spike scaled by the geology dial (not density). Independent RNG.
                    DeterministicRNG geo_rng(state.world_seed ^
                                             (static_cast<uint64_t>(state.current_tick) *
                                              0xC2B2AE3D27D4EB4Full) ^
                                             (static_cast<uint64_t>(province.id) << 23) ^
                                             0x6E01060715ull);
                    hazard_rate_mult *=
                        disaster_mortality_factor(state.hazard_settings.geology, geo_rng, cfg_);
                }
                // War casualties (G2): warfare publishes an EXTRA annual death
                // fraction (real units: battle dead / population, Lanchester
                // attrition). Owned by the publisher (reset on regime exit; 0 at
                // peace), applied unconditionally here. Passed as an additive rate,
                // not a multiplier — war kills a fraction of people, it does not
                // scale background mortality.
                const float war_deaths = annual ? cs.war_death_fraction : 0.0f;

                // Fertility tracks the long-run food signal the subsistence module writes
                // (output vs need + reserve upkeep): population grows only when the land
                // can sustainably support more, and stops at the ceiling — leaving the
                // upkeep surplus that funds specialists. Starvation, separately, fires
                // only once the granary itself is exhausted (a real reserve buffer), so a
                // population at its ceiling holds steady instead of either starving or
                // breeding into collapse.
                const float birth_surplus = cs.subsistence_surplus_ratio;
                const float famine_surplus =
                    (commons && cs.food_store <= 0.0f) ? cs.subsistence_surplus_ratio : 1.0f;
                // Radiation chronically depresses fertility (planetary — applies in all
                // eras, never released, unlike the conquerable disease/geology shocks).
                const float fertility_mult =
                    radiation_fertility_factor(state.hazard_settings.radiation, cfg_);
                process_births_deaths(next, eff_stability, cs.sick_rate, cs.addiction_rate,
                                      birth_surplus, famine_surplus, hazard_rate_mult,
                                      fertility_mult, war_deaths, cfg_);

                // Hardiness drifts toward the world's hazard level over generations
                // (adaptation under sustained pressure; softening under ease).
                new_hardiness = cs.hardiness + (world_hazard - cs.hardiness) * cfg_.hardiness_drift_rate;
            }

            // Recompute aggregates over the canonical (sorted) group order.
            CohortStatsDelta cd;
            cd.region_id = province_idx;
            uint32_t new_total = 0;
            std::vector<float> incomes;
            incomes.reserve(next.size());
            for (const auto& [g, c] : next) {
                (void)g;
                new_total += c.size;
                incomes.push_back(c.median_income);
            }
            std::sort(incomes.begin(), incomes.end());
            cd.total_population = new_total;
            cd.mean_income = compute_mean_income(next);
            cd.gini_coefficient = compute_gini_coefficient(incomes);
            cd.hardiness = new_hardiness;
            cd.cohorts = std::move(next);
            province_delta.cohort_stats_deltas.push_back(std::move(cd));
        }
    }

    // --- Province-level demographic stress proxies (legacy monthly signal) -----
    if (!is_monthly_tick(state.current_tick))
        return;

    const auto& demographics = province.demographics;

    RegionDelta rdelta;
    rdelta.region_id = province_idx;

    // --- Stability contribution from demographic changes ---
    // High dependency ratio (old/young relative to working-age) degrades stability
    // by increasing fiscal strain on the province.
    // Proxy: if income_low_fraction dominates, stability faces downward pressure.
    {
        // Compute a simple demographic stress index: high share of low-income
        // cohorts combined with low education drives instability.
        float demographic_stress =
            demographics.income_low_fraction * (1.0f - demographics.education_level);
        constexpr float DEMOGRAPHIC_STABILITY_WEIGHT = -0.002f;
        rdelta.stability_delta = DEMOGRAPHIC_STABILITY_WEIGHT * demographic_stress;
    }

    // --- Inequality contribution from income distribution ---
    // Monthly re-evaluation: the Gini proxy (high minus low income fraction)
    // nudges the province inequality index toward the current demographic signal.
    {
        float income_spread = demographics.income_high_fraction - demographics.income_low_fraction;
        float target_inequality = std::clamp(income_spread, 0.0f, 1.0f);
        constexpr float MONTHLY_INEQUALITY_CONVERGENCE = 0.005f;
        rdelta.inequality_delta = MONTHLY_INEQUALITY_CONVERGENCE *
                                  (target_inequality - province.conditions.inequality_index);
    }

    // --- Grievance ---
    // Owned by community_response. This module previously injected
    // `+0.003 × income_low_fraction` EVERY tick — a constant, never-decaying pump
    // off a static demographic, disconnected from current conditions, which (with
    // regional_conditions' writer) pinned grievance at the ceiling regardless of
    // economic reality. Economic deprivation is now grounded in
    // community_response's material grievance term (unemployment + inequality).
    // Removed; grievance has a single owner.

    // Only push the delta if at least one field was set.
    province_delta.region_deltas.push_back(rdelta);
}

void PopulationAgingModule::execute(const WorldState& state, DeltaBuffer& delta) {
    for (uint32_t i = 0; i < state.provinces.size(); ++i) {
        execute_province(i, state, delta);
    }
}

}  // namespace econlife
