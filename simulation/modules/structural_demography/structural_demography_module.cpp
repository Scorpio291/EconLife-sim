#include "modules/structural_demography/structural_demography_module.h"

#include <algorithm>
#include <cmath>

#include "core/world_gen/era_catalog.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"

namespace econlife {

bool StructuralDemographyModule::regime_active(std::string_view regime) const {
    return regime_in(cfg_.active_regimes, regime);
}

void StructuralDemographyModule::execute_province(uint32_t province_idx, const WorldState& state,
                                                  DeltaBuffer& province_delta) {
    if (province_idx >= state.provinces.size())
        return;
    const EraDefinition* era = state.era_catalog.by_index(state.technology.current_era);
    if (era == nullptr || !regime_active(era->economic_regime))
        return;

    const Province& prov = state.provinces[province_idx];
    if (!prov.cohort_stats)
        return;
    const RegionCohortStats& cs = *prov.cohort_stats;
    const uint32_t population = cs.total_population;
    if (population == 0)
        return;

    stress_state_dirty_ = true;

    // Structural stress is a slow variable — secular cycles run centuries — so it is
    // recomputed once a year, on the same cadence as the harvest it answers to.
    const uint32_t tpy = kTicksPerYear;
    const bool annual = state.current_tick > 0 && state.current_tick % tpy == 0;
    if (!annual)
        return;

    // --- The three terms -------------------------------------------------------------
    // Popular immiseration and the young men to act on it. The working-age fraction is
    // published by population_aging; everyone else is either too young or too old, and
    // it is the young who march.
    const float youth_share = std::clamp(1.0f - cs.working_age_fraction, 0.0f, 1.0f);
    const float mmp = mass_mobilisation(cs.subsistence_surplus_ratio, youth_share);

    // Elite overproduction: the stratum the society is carrying against the stratum this
    // year's harvest can pay for. The gap is people raised to expect a place that no
    // longer exists.
    const float emp = elite_overproduction(cs.specialist_fraction, cs.supported_specialist_fraction,
                                           cfg_);

    // Fiscal exhaustion: an empty granary is a polity with nothing left to distribute,
    // and a distrusted one cannot promise next year's instead.
    const float need_per_tick = static_cast<float>(population);  // in per-capita food units
    const float target_store =
        SubsistenceConfig{}.granary_reserve_years * need_per_tick * static_cast<float>(tpy) *
        SubsistenceConfig{}.per_capita_food_per_tick;
    const float granary_cover =
        target_store > 0.0f ? std::min(1.0f, cs.food_store / target_store) : 0.0f;
    const float sfd = fiscal_distress(granary_cover, prov.community.institutional_trust);

    const float psi = political_stress(mmp, emp, sfd);

    RegionDelta rd{};
    rd.region_id = prov.region_id;
    rd.political_stress_replacement = psi;

    // --- What the stress actually DOES -----------------------------------------------
    // It kills. Factional conflict is published as an extra annual death fraction in
    // real units and applied by population_aging as an independent competing risk, in
    // the same tick, exactly as warfare's battle dead are — but kept separate from them,
    // because one is a war between polities and this is a polity coming apart inside.
    rd.faction_death_fraction_replacement = conflict_death_fraction(psi, cfg_);

    // It eats. Every claimant without a place raises followers, and armed followers eat
    // like soldiers rather than like peasants. Only the EXTRA over their ordinary ration
    // is drawn here — they were already counted as mouths in the harvest balance — so
    // nothing is consumed twice. This is why factional politics is expensive rather than
    // free, and why a society in this state cannot rebuild its reserves.
    const float surplus_claimants =
        std::max(0.0f, cs.specialist_fraction - cs.supported_specialist_fraction) *
        static_cast<float>(population);
    const float retinue_people =
        surplus_claimants * (1.0f + std::max(0.0f, cfg_.followers_per_surplus_claimant));
    const float extra_ration = std::max(0.0f, cfg_.retinue_ration_mult - 1.0f);
    const float retinue_food = retinue_people * extra_ration *
                               SubsistenceConfig{}.per_capita_food_per_tick *
                               static_cast<float>(tpy);
    if (retinue_food > 0.0f)
        rd.food_store_delta = -std::min(retinue_food, std::max(0.0f, cs.food_store));

    // It erodes the ability of anyone to govern. Stability feeds back into births and
    // deaths, so a society under structural stress becomes harder to hold together in
    // the ordinary demographic sense too.
    if (psi > 0.0f)
        rd.stability_delta = -cfg_.stability_erosion_at_unit_stress * psi;

    province_delta.region_deltas.push_back(rd);
}

void StructuralDemographyModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Regime exit: stop publishing and clear the last commons values exactly once, so no
    // stale stress or death fraction survives into a market era.
    const EraDefinition* era = state.era_catalog.by_index(state.technology.current_era);
    if (era != nullptr && regime_active(era->economic_regime))
        return;
    if (!stress_state_dirty_)
        return;
    stress_state_dirty_ = false;
    for (const auto& prov : state.provinces) {
        if (!prov.cohort_stats)
            continue;
        RegionDelta rd{};
        rd.region_id = prov.region_id;
        rd.political_stress_replacement = 0.0f;
        rd.faction_death_fraction_replacement = 0.0f;
        delta.region_deltas.push_back(rd);
    }
}

}  // namespace econlife
