#include "modules/structural_demography/structural_demography_module.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>
#include <vector>

#include "core/world_gen/era_catalog.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"

namespace econlife {

bool StructuralDemographyModule::regime_active(std::string_view regime) const {
    return regime_in(cfg_.active_regimes, regime);
}

namespace {

// Everything a polity is, summed over the provinces that belong to it.
struct PolityTotals {
    double population = 0.0;
    double wage_weighted = 0.0;      // population-weighted surplus ratio
    double reference_weighted = 0.0;  // population-weighted wage the people are used to
    double youth_weighted = 0.0;    // population-weighted youth share
    double held_weighted = 0.0;     // population-weighted stratum held
    double supported_weighted = 0.0;  // population-weighted stratum supported
    double trust_weighted = 0.0;    // population-weighted institutional trust
    double food_store = 0.0;
    double target_store = 0.0;
};

}  // namespace

void StructuralDemographyModule::execute(const WorldState& state, DeltaBuffer& delta) {
    const EraDefinition* era = state.era_catalog.by_index(state.technology.current_era);
    const bool active = era != nullptr && regime_active(era->economic_regime);

    if (!active) {
        // Regime exit: stop publishing and clear the last commons values exactly once, so
        // no stale stress, death fraction or refugee flow survives into a market era.
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
            rd.refugee_flow_replacement = 0.0f;
            delta.region_deltas.push_back(rd);
        }
        return;
    }

    const uint32_t n = static_cast<uint32_t>(state.provinces.size());
    if (n == 0)
        return;
    stress_state_dirty_ = true;

    // Structural stress is a slow variable — secular cycles run centuries — so it is
    // recomputed once a year, on the same cadence as the harvest it answers to.
    const uint32_t tpy = kTicksPerYear;
    if (!(state.current_tick > 0 && state.current_tick % tpy == 0))
        return;

    const SubsistenceConfig sub{};
    const float per_capita = sub.per_capita_food_per_tick;
    const float reserve_years = sub.granary_reserve_years;

    // --- STRESS IS A PROPERTY OF THE POLITY, NOT THE PROVINCE (R5) --------------------
    // A polity is one political unit. Its elite overproduction, its treasury and the
    // misery of its people are facts about the whole of it: Rome's third-century crisis
    // was not confined to one province, and neither was the late Ming's.
    //
    // Measured per province in isolation, the three legs of the index almost never rose
    // together — the multiplicative form correctly demands that they do — so structural
    // collapse stayed a chronic bleed of a twentieth of a percent a year instead of ever
    // ending anything. Aggregated over the polity it is the state that fails, all at
    // once, everywhere.
    //
    // It also supplies the nonlinearity a collapse needs: a rich province genuinely
    // carries a poor one, because the treasury is pooled — right up until it cannot, and
    // then the whole structure goes together.
    std::map<uint32_t, PolityTotals> polities;  // ordered: determinism
    for (uint32_t i = 0; i < n; ++i) {
        const auto& prov = state.provinces[i];
        if (!prov.cohort_stats)
            continue;
        const RegionCohortStats& cs = *prov.cohort_stats;
        const double pop = static_cast<double>(cs.total_population);
        if (pop <= 0.0)
            continue;
        PolityTotals& t = polities[cs.polity_id];
        t.population += pop;
        t.wage_weighted += pop * static_cast<double>(cs.subsistence_surplus_ratio);
        t.reference_weighted += pop * static_cast<double>(cs.wage_reference);
        t.youth_weighted +=
            pop * static_cast<double>(std::clamp(1.0f - cs.working_age_fraction, 0.0f, 1.0f));
        t.held_weighted += pop * static_cast<double>(cs.specialist_fraction);
        t.supported_weighted += pop * static_cast<double>(cs.supported_specialist_fraction);
        t.trust_weighted += pop * static_cast<double>(prov.community.institutional_trust);
        t.food_store += static_cast<double>(std::max(0.0f, cs.food_store));
        t.target_store +=
            static_cast<double>(reserve_years * per_capita * static_cast<float>(tpy)) * pop;
    }

    std::map<uint32_t, float> psi_of_polity;
    for (const auto& [pid, t] : polities) {
        if (t.population <= 0.0)
            continue;
        const auto wage = static_cast<float>(t.wage_weighted / t.population);
        const auto reference = static_cast<float>(t.reference_weighted / t.population);
        const auto youth = static_cast<float>(t.youth_weighted / t.population);
        const auto held = static_cast<float>(t.held_weighted / t.population);
        const auto supported = static_cast<float>(t.supported_weighted / t.population);
        const auto trust = static_cast<float>(t.trust_weighted / t.population);
        const auto cover = t.target_store > 0.0
                               ? static_cast<float>(std::min(1.0, t.food_store / t.target_store))
                               : 0.0f;
        psi_of_polity[pid] = political_stress(mass_mobilisation(wage, reference, youth),
                                              elite_overproduction(held, supported, cfg_),
                                              fiscal_distress(cover, trust));
    }

    // --- REFUGEES: how a collapse crosses a border (R5) -------------------------------
    // When the food fails, those who can walk do — and everywhere they arrive they are
    // more mouths on somebody else's land, whose surplus falls under the weight, whose
    // stress rises, and which begins exporting people in turn. That is the cascade, and
    // nothing here models a cascade: it is what conserved flight does on a map.
    //
    // People only leave for somewhere BETTER. A province whose neighbours are all worse
    // off keeps its people and starves in place — the historically correct reading, since
    // the Migration Period happened because there was somewhere to go.
    const auto h3_to_idx = build_h3_to_province_index(state.provinces);
    std::vector<double> flow(n, 0.0);
    for (uint32_t src = 0; src < n; ++src) {
        const auto& prov = state.provinces[src];
        if (!prov.cohort_stats)
            continue;
        const RegionCohortStats& cs = *prov.cohort_stats;
        const double pop = static_cast<double>(cs.total_population);
        if (pop <= 0.0)
            continue;
        const float leaving_share = flight_fraction(cs.subsistence_surplus_ratio, cfg_);
        if (leaving_share <= 0.0f)
            continue;

        // Where to? Reachable neighbours that are better fed, weighted by how much
        // better. Destinations in ascending index order for determinism.
        std::vector<std::pair<uint32_t, double>> dests;
        double total_pull = 0.0;
        for (const auto& link : prov.links) {
            auto it = h3_to_idx.find(link.neighbor_h3);
            if (it == h3_to_idx.end() || it->second == src)
                continue;
            const auto& dst = state.provinces[it->second];
            if (!dst.cohort_stats)
                continue;
            const double pull = static_cast<double>(dst.cohort_stats->subsistence_surplus_ratio) -
                                static_cast<double>(cs.subsistence_surplus_ratio);
            if (pull <= 0.0)
                continue;  // no better off than here: no reason to walk
            dests.emplace_back(it->second, pull);
            total_pull += pull;
        }
        if (dests.empty() || total_pull <= 0.0)
            continue;  // nowhere better to go: the province starves where it stands
        std::sort(dests.begin(), dests.end(),
                  [](const std::pair<uint32_t, double>& a, const std::pair<uint32_t, double>& b) {
                      return a.first < b.first;
                  });

        const double leaving = pop * static_cast<double>(leaving_share);
        flow[src] -= leaving;
        for (const auto& [dst, pull] : dests)
            flow[dst] += leaving * (pull / total_pull);  // conserved: sums to `leaving`
    }

    // --- Publish -----------------------------------------------------------------------
    for (uint32_t i = 0; i < n; ++i) {
        const auto& prov = state.provinces[i];
        if (!prov.cohort_stats)
            continue;
        const RegionCohortStats& cs = *prov.cohort_stats;
        const uint32_t population = cs.total_population;
        if (population == 0)
            continue;

        auto it = psi_of_polity.find(cs.polity_id);
        const float psi = it == psi_of_polity.end() ? 0.0f : it->second;

        RegionDelta rd{};
        rd.region_id = prov.region_id;
        rd.political_stress_replacement = psi;
        rd.refugee_flow_replacement = static_cast<float>(flow[i]);
        // What people are used to catches up with what they are getting, over a
        // generation. Per province, because that is where people actually live.
        rd.wage_reference_replacement =
            wage_reference_year(cs.wage_reference, cs.subsistence_surplus_ratio, cfg_);

        // It kills. Factional conflict is published as an extra annual death fraction in
        // real units and applied by population_aging as an independent competing risk, in
        // the same tick, exactly as warfare's battle dead are — but kept separate from
        // them, because one is a war between polities and this is a polity coming apart
        // inside.
        rd.faction_death_fraction_replacement = conflict_death_fraction(psi, cfg_);

        // It eats. Every claimant without a place raises followers, and armed followers
        // eat like soldiers rather than like peasants. Only the EXTRA over their ordinary
        // ration is drawn here — they were already counted as mouths in the harvest
        // balance — so nothing is consumed twice. The claimants are counted LOCALLY
        // because they are physically here, even though the stress that makes them
        // dangerous is the polity's.
        const float surplus_claimants =
            std::max(0.0f, cs.specialist_fraction - cs.supported_specialist_fraction) *
            static_cast<float>(population);
        const float retinue_people =
            surplus_claimants * (1.0f + std::max(0.0f, cfg_.followers_per_surplus_claimant));
        const float extra_ration = std::max(0.0f, cfg_.retinue_ration_mult - 1.0f);
        const float retinue_food =
            retinue_people * extra_ration * per_capita * static_cast<float>(tpy);
        if (retinue_food > 0.0f)
            rd.food_store_delta = -std::min(retinue_food, std::max(0.0f, cs.food_store));

        // It erodes the ability of anyone to govern. Stability feeds back into births and
        // deaths, so a society under structural stress becomes harder to hold together in
        // the ordinary demographic sense too.
        if (psi > 0.0f)
            rd.stability_delta = -cfg_.stability_erosion_at_unit_stress * psi;

        delta.region_deltas.push_back(rd);
    }
}

}  // namespace econlife
