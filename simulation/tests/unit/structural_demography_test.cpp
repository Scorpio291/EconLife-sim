#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/apply_deltas.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"
#include "modules/structural_demography/structural_demography_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

// ===========================================================================
// THE ENDOGENOUS FALL — societies come apart for reasons other than weather.
//
// As a population grows it depresses the real wage while inflating the incomes
// of those at the top, so the number of people raised to expect a place above
// the plough grows faster than the number of such places. Surplus claimants turn
// on one another, the retinues they raise have to be fed, and the fiscal base
// erodes underneath. Rome, the Han, late Ming, France before 1789.
//
// The Political Stress Index is MULTIPLICATIVE, which is the theory's sharpest
// claim: a miserable population under a united elite and a solvent state does
// not bring the state down, and nor does a fractured elite over a contented one.
// ===========================================================================

namespace {

StructuralDemographyConfig cfg() { return StructuralDemographyConfig{}; }

// A commons-era province under whatever structural conditions the test wants.
WorldState stressed_world(float wage, float held, float supported, float food_store,
                          float trust) {
    WorldState w{};
    w.current_tick = kTicksPerYear;  // an annual tick
    w.world_seed = 1;
    w.era_catalog.load_builtin_default();
    w.technology.current_era = 5;  // feudal — a commons regime
    Province p{};
    p.id = 0;
    p.region_id = 0;
    p.community.institutional_trust = trust;
    p.conditions.stability_score = 0.8f;
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    p.cohort_stats->total_population = 1000000;
    p.cohort_stats->working_age_fraction = 0.6f;  // 40% too young or too old
    p.cohort_stats->subsistence_surplus_ratio = wage;
    p.cohort_stats->specialist_fraction = held;
    p.cohort_stats->supported_specialist_fraction = supported;
    p.cohort_stats->food_store = food_store;
    w.provinces.push_back(std::move(p));
    return w;
}

// A society that is hungry, top-heavy and broke, all at once.
WorldState society_coming_apart() {
    return stressed_world(/*wage=*/0.7f, /*held=*/0.15f, /*supported=*/0.02f,
                          /*food_store=*/0.0f, /*trust=*/0.2f);
}

}  // namespace

TEST_CASE("structural_demography: it takes all three at once, or nothing",
          "[structural_demography][psi]") {
    // The theory's sharpest and most falsifiable claim, and the reason most bad years
    // are merely bad years. Take away any one leg and the stress goes to zero, however
    // extreme the other two are.
    const float full = StructuralDemographyModule::political_stress(0.4f, 3.0f, 0.8f);
    CHECK(full > 0.0f);

    CHECK_THAT(StructuralDemographyModule::political_stress(0.0f, 3.0f, 0.8f),
               WithinAbs(0.0f, 1e-9f));  // a well-fed population does not rise
    CHECK_THAT(StructuralDemographyModule::political_stress(0.4f, 0.0f, 0.8f),
               WithinAbs(0.0f, 1e-9f));  // a united elite has nobody to lead it
    CHECK_THAT(StructuralDemographyModule::political_stress(0.4f, 3.0f, 0.0f),
               WithinAbs(0.0f, 1e-9f));  // a solvent, trusted state absorbs the rest
}

TEST_CASE("structural_demography: the hungry and the young are who march",
          "[structural_demography][psi]") {
    // Immiseration alone is not mobilisation: it takes people with the years and the
    // grievance together. A fed population contributes nothing however young it is.
    CHECK(StructuralDemographyModule::mass_mobilisation(0.6f, 0.4f) >
          StructuralDemographyModule::mass_mobilisation(0.9f, 0.4f));
    CHECK(StructuralDemographyModule::mass_mobilisation(0.6f, 0.4f) >
          StructuralDemographyModule::mass_mobilisation(0.6f, 0.1f));
    CHECK_THAT(StructuralDemographyModule::mass_mobilisation(1.2f, 0.4f), WithinAbs(0.0f, 1e-9f));
    CHECK_THAT(StructuralDemographyModule::mass_mobilisation(1.0f, 0.4f), WithinAbs(0.0f, 1e-9f));
}

TEST_CASE("structural_demography: elite overproduction is claimants per remaining place",
          "[structural_demography][psi]") {
    // The heart of it. The stratum has generational inertia (R2C) and so does not shrink
    // when the harvest that pays for it does — the people in the gap were raised to
    // expect something the land no longer provides, and they do not go quietly back to
    // the plough.
    const auto c = cfg();
    CHECK_THAT(StructuralDemographyModule::elite_overproduction(0.10f, 0.10f, c),
               WithinAbs(0.0f, 1e-9f));  // every claimant has a place
    const float mild = StructuralDemographyModule::elite_overproduction(0.12f, 0.10f, c);
    const float severe = StructuralDemographyModule::elite_overproduction(0.15f, 0.02f, c);
    CHECK(mild > 0.0f);
    CHECK(severe > mild);
    // Uncapped by design: a stratum its land cannot support AT ALL is an extreme state
    // and the index should say so. The bound belongs at the far end, in 1 - exp(-rate).
    CHECK(StructuralDemographyModule::elite_overproduction(0.15f, 0.0f, c) > severe);
}

TEST_CASE("structural_demography: an empty granary is only fatal to a distrusted state",
          "[structural_demography][psi]") {
    // A polity with nothing left to distribute can still borrow against next year if it
    // is believed. Fiscal distress is the two together.
    CHECK_THAT(StructuralDemographyModule::fiscal_distress(/*granary=*/0.0f, /*trust=*/1.0f),
               WithinAbs(0.0f, 1e-9f));
    CHECK_THAT(StructuralDemographyModule::fiscal_distress(1.0f, 0.0f), WithinAbs(0.0f, 1e-9f));
    CHECK_THAT(StructuralDemographyModule::fiscal_distress(0.0f, 0.0f), WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("structural_demography: conflict mortality saturates instead of being capped",
          "[structural_demography][psi]") {
    // However extreme the stress, the annual loss approaches total and never exceeds it,
    // because it arrives as a Poisson first-arrival on a hazard rate. No cap anywhere.
    const auto c = cfg();
    CHECK_THAT(StructuralDemographyModule::conflict_death_fraction(0.0f, c),
               WithinAbs(0.0f, 1e-9f));
    const float moderate = StructuralDemographyModule::conflict_death_fraction(1.0f, c);
    const float catastrophic = StructuralDemographyModule::conflict_death_fraction(1000.0f, c);
    CHECK(moderate > 0.0f);
    CHECK(catastrophic > moderate);
    CHECK(catastrophic < 1.0f);
    // At unit stress the annual loss is of the order of the worst civil wars — the
    // Taiping rebellion killed roughly 0.4% of China a year for fourteen years.
    CHECK(moderate < 0.02f);
}

TEST_CASE("structural_demography: a society coming apart kills, eats and destabilises",
          "[structural_demography][psi]") {
    // The stress is not a mood. Every one of its effects is a located flow: people die,
    // grain leaves the granary, and the ability to govern erodes.
    StructuralDemographyModule mod;
    WorldState w = society_coming_apart();

    DeltaBuffer d{};
    mod.execute_province(0, w, d);
    REQUIRE(d.region_deltas.size() == 1);
    const RegionDelta& rd = d.region_deltas[0];

    REQUIRE(rd.political_stress_replacement.has_value());
    CHECK(*rd.political_stress_replacement > 0.0f);
    REQUIRE(rd.faction_death_fraction_replacement.has_value());
    CHECK(*rd.faction_death_fraction_replacement > 0.0f);
    REQUIRE(rd.stability_delta.has_value());
    CHECK(*rd.stability_delta < 0.0f);
}

TEST_CASE("structural_demography: retinues eat from the granary that is actually there",
          "[structural_demography][psi][conservation]") {
    // Armed followers eat like soldiers rather than like peasants, and the difference
    // comes out of the stores. It can never take out more than the granary holds — that
    // is a physical bound on a real stock, not a cap on the mechanism.
    StructuralDemographyModule mod;
    WorldState w = stressed_world(/*wage=*/0.7f, /*held=*/0.15f, /*supported=*/0.02f,
                                  /*food_store=*/5.0e6f, /*trust=*/0.2f);

    DeltaBuffer d{};
    mod.execute_province(0, w, d);
    REQUIRE(d.region_deltas.size() == 1);
    REQUIRE(d.region_deltas[0].food_store_delta.has_value());
    const float drawn = -*d.region_deltas[0].food_store_delta;
    CHECK(drawn > 0.0f);
    CHECK(drawn <= 5.0e6f);

    apply_deltas(w, d);
    CHECK(w.provinces[0].cohort_stats->food_store >= 0.0f);
    CHECK(w.provinces[0].cohort_stats->food_store < 5.0e6f);
}

TEST_CASE("structural_demography: a fed, united, solvent society is left alone",
          "[structural_demography][psi]") {
    // The common case. Most societies most of the time are not coming apart, and the
    // module must cost them nothing — no deaths, no grain, no stability.
    StructuralDemographyModule mod;
    WorldState w = stressed_world(/*wage=*/1.3f, /*held=*/0.10f, /*supported=*/0.10f,
                                  /*food_store=*/1.0e9f, /*trust=*/0.8f);

    DeltaBuffer d{};
    mod.execute_province(0, w, d);
    REQUIRE(d.region_deltas.size() == 1);
    const RegionDelta& rd = d.region_deltas[0];
    CHECK_THAT(rd.political_stress_replacement.value_or(-1.0f), WithinAbs(0.0f, 1e-9f));
    CHECK_THAT(rd.faction_death_fraction_replacement.value_or(-1.0f), WithinAbs(0.0f, 1e-9f));
    CHECK_FALSE(rd.stability_delta.has_value());
    CHECK_FALSE(rd.food_store_delta.has_value());
}

TEST_CASE("structural_demography: inert in market eras", "[structural_demography][psi]") {
    StructuralDemographyModule mod;
    WorldState w = society_coming_apart();
    w.technology.current_era = 8;  // modern (market regime)

    DeltaBuffer d{};
    mod.execute_province(0, w, d);
    CHECK(d.region_deltas.empty());
}
