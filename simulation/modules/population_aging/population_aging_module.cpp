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
void process_births_deaths(std::map<DemographicGroup, PopulationCohort>& cohorts, float stability,
                           float sick_rate, float addiction_rate, float food_surplus,
                           float hazard_mortality, const PopulationAgingConfig& cfg) {
    uint64_t total = 0;
    for (const auto& [g, c] : cohorts) {
        (void)g;
        total += c.size;
    }

    // Food coupling (the Malthusian loop). All factors are NEUTRAL at surplus == 1.0,
    // so market eras (where surplus is always 1.0) are unchanged. A surplus lifts
    // births toward a cap AND relieves mortality (well-fed survival); a deficit
    // (surplus < 1) raises mortality.
    const float surplus = std::clamp(food_surplus, 0.0f, 10.0f);
    const float birth_food_factor = std::clamp(surplus, 0.0f, cfg.food_surplus_birth_cap);
    float famine_mortality_factor;
    if (surplus < 1.0f) {
        famine_mortality_factor = 1.0f + cfg.food_deficit_mortality_strength * (1.0f - surplus);
    } else {
        famine_mortality_factor = std::max(
            cfg.food_mortality_floor, 1.0f - cfg.food_surplus_mortality_relief * (surplus - 1.0f));
    }

    // Births: survival scales with stability and healthcare (proxied by the
    // inverse of sick_rate, since HealthcareProfile is not on WorldState).
    float healthcare_proxy = std::clamp(1.0f - sick_rate, 0.0f, 1.0f);
    float birth_rate = cfg.base_annual_birth_rate * std::clamp(stability, 0.0f, 1.0f) *
                       healthcare_proxy * birth_food_factor;
    auto births = static_cast<uint32_t>(
        std::llround(static_cast<double>(total) * static_cast<double>(birth_rate)));
    cohorts[DemographicGroup::youth_urban].size += births / 2u;
    cohorts[DemographicGroup::youth_rural].size += births - births / 2u;

    // Deaths: per-cohort mortality rises with instability, addiction, and famine,
    // and is multiplied for retiree cohorts.
    float mortality_env = (1.0f + (1.0f - std::clamp(stability, 0.0f, 1.0f))) *
                          (1.0f + std::clamp(addiction_rate, 0.0f, 1.0f)) * famine_mortality_factor *
                          hazard_mortality;  // per-setting world hazards (1.0 = earthlike)
    for (auto& [g, c] : cohorts) {
        if (c.size == 0)
            continue;
        float mort = cfg.base_annual_death_rate * mortality_env;
        if (is_retiree_group(g))
            mort *= cfg.retiree_mortality_multiplier;
        auto deaths = static_cast<uint32_t>(
            std::llround(static_cast<double>(c.size) * static_cast<double>(mort)));
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
                const bool commons =
                    era && (era->economic_regime == "subsistence" || era->economic_regime == "barter");

                // Pre-market demographics are food-driven: floor the effective
                // stability in commons regimes so the modern political "stability"
                // proxy (which a subsistence world tanks for lacking markets) doesn't
                // crush births — surplus drives the dynamics instead.
                float eff_stability = province.conditions.stability_score;
                if (commons)
                    eff_stability = std::max(eff_stability, cfg_.commons_stability_floor);

                // The world's hazard pressure (disease/predators/radiation/atmosphere/
                // geology/gravity-falls), Earth-normalized.
                const float world_hazard = hazard_mortality_from_settings(state.hazard_settings);
                // Generational hardiness: mortality scales with how far the population's
                // adaptation falls short of what the world demands. A people matched to
                // their world (hardiness == world_hazard) is neutral; a soft people on a
                // hard world (hardiness << world_hazard) is culled until they adapt; an
                // over-hardened people survive better. So harshness is relative to the
                // adapted population.
                float hazard_mortality = std::clamp(
                    world_hazard / std::max(cs.hardiness, cfg_.hardiness_floor), 0.15f, 3.0f);

                process_births_deaths(next, eff_stability, cs.sick_rate, cs.addiction_rate,
                                      cs.subsistence_surplus_ratio, hazard_mortality, cfg_);

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
