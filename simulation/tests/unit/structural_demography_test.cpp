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

TEST_CASE("structural_demography: what mobilises people is getting POORER, not being poor",
          "[structural_demography][psi][contagion]") {
    // Turchin's variable is the real wage against trend, and Davies' J-curve is the
    // standard finding: revolutions follow REVERSALS after improvement, not steady
    // poverty. This distinction decides whether a society can fall at all — a population
    // whose numbers track its food supply is never absolutely starving, so measured
    // against subsistence this term was exactly zero for an entire 12,000-year climb and
    // the multiplicative index was zero with it.
    using SD = StructuralDemographyModule;

    // A poor society that has always been poor is not mobilised. A rich one that has just
    // halved its living standards is.
    CHECK_THAT(SD::mass_mobilisation(/*wage=*/0.6f, /*reference=*/0.6f, 0.4f),
               WithinAbs(0.0f, 1e-9f));
    // Halved living standards, 40% of the population young: 0.5 x 0.4.
    CHECK_THAT(SD::mass_mobilisation(/*wage=*/1.2f, /*reference=*/2.4f, 0.4f),
               WithinAbs(0.2f, 1e-6f));

    // Deeper reversals mobilise more, and it still takes young people to act on it.
    CHECK(SD::mass_mobilisation(0.6f, 1.2f, 0.4f) > SD::mass_mobilisation(0.9f, 1.2f, 0.4f));
    CHECK(SD::mass_mobilisation(0.6f, 1.2f, 0.4f) > SD::mass_mobilisation(0.6f, 1.2f, 0.1f));
    // Getting BETTER off mobilises nobody, however poor in absolute terms.
    CHECK_THAT(SD::mass_mobilisation(1.4f, 1.2f, 0.4f), WithinAbs(0.0f, 1e-9f));
}

TEST_CASE("structural_demography: what people expect catches up over a generation",
          "[structural_demography][psi][contagion]") {
    // About thirty years — which is how long anybody remembers being better off. Slow
    // enough that a reversal registers as immiseration for decades, fast enough that a
    // society which stays poor eventually stops rioting about it.
    const auto c = cfg();
    float ref = 2.0f;
    int years = 0;
    for (; years < 500; ++years) {
        ref = StructuralDemographyModule::wage_reference_year(ref, /*wage=*/1.0f, c);
        if (ref <= 1.1f)
            break;
    }
    INFO("years for expectations to fall from 2.0 toward a wage of 1.0: " << years);
    CHECK(years > 20);
    CHECK(years < 200);
    // And it moves toward the wage from below just as readily.
    CHECK(StructuralDemographyModule::wage_reference_year(1.0f, 2.0f, c) > 1.0f);
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
    mod.execute(w, d);
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
    mod.execute(w, d);
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
    mod.execute(w, d);
    REQUIRE(d.region_deltas.size() == 1);
    const RegionDelta& rd = d.region_deltas[0];
    CHECK_THAT(rd.political_stress_replacement.value_or(-1.0f), WithinAbs(0.0f, 1e-9f));
    CHECK_THAT(rd.faction_death_fraction_replacement.value_or(-1.0f), WithinAbs(0.0f, 1e-9f));
    CHECK_FALSE(rd.stability_delta.has_value());
    CHECK_FALSE(rd.food_store_delta.has_value());
}

TEST_CASE("structural_demography: stress is a property of the POLITY, not the province",
          "[structural_demography][psi][contagion]") {
    // A polity is one political unit. Rome's third-century crisis was not confined to a
    // province and neither was the late Ming's, so the index is measured over the whole
    // state: its pooled treasury, its combined stratum, the misery of all its people.
    //
    // This is also what makes a collapse deep rather than chronic. Measured per province
    // in isolation the three legs almost never rose together — the multiplicative form
    // correctly demands that they do — so structural collapse stayed a bleed of a
    // twentieth of a percent a year and never ended anything.
    StructuralDemographyModule mod;
    WorldState w = stressed_world(/*wage=*/1.4f, /*held=*/0.10f, /*supported=*/0.10f,
                                  /*food_store=*/1.0e9f, /*trust=*/0.8f);
    // A second province in the SAME polity, in deep crisis.
    Province p{};
    p.id = 1;
    p.region_id = 1;
    p.community.institutional_trust = 0.1f;
    p.conditions.stability_score = 0.4f;
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    p.cohort_stats->total_population = 2000000;  // twice the comfortable one
    p.cohort_stats->working_age_fraction = 0.6f;
    p.cohort_stats->subsistence_surplus_ratio = 0.5f;
    p.cohort_stats->specialist_fraction = 0.18f;
    p.cohort_stats->supported_specialist_fraction = 0.01f;
    p.cohort_stats->food_store = 0.0f;
    p.cohort_stats->polity_id = 0;  // same state
    w.provinces.push_back(std::move(p));

    DeltaBuffer d{};
    mod.execute(w, d);
    REQUIRE(d.region_deltas.size() == 2);

    // The comfortable province is in a failing state, and is therefore itself in trouble
    // — which is the whole point. A province cannot be serene inside a collapsing empire.
    REQUIRE(d.region_deltas[0].political_stress_replacement.has_value());
    REQUIRE(d.region_deltas[1].political_stress_replacement.has_value());
    CHECK(*d.region_deltas[0].political_stress_replacement > 0.0f);
    // Both members read the same state-wide stress.
    CHECK_THAT(*d.region_deltas[0].political_stress_replacement,
               WithinAbs(*d.region_deltas[1].political_stress_replacement, 1e-6f));
}

TEST_CASE("structural_demography: refugees leave for somewhere better, conserved",
          "[structural_demography][psi][contagion]") {
    // How a collapse crosses a border. When the food fails those who can walk do, and
    // everywhere they arrive they are more mouths on somebody else's land — whose surplus
    // falls under the weight, whose stress rises, and which begins exporting in turn.
    StructuralDemographyModule mod;
    WorldState w = stressed_world(/*wage=*/0.4f, /*held=*/0.10f, /*supported=*/0.02f,
                                  /*food_store=*/0.0f, /*trust=*/0.3f);  // province 0: famine
    Province p{};
    p.id = 1;
    p.region_id = 1;
    p.community.institutional_trust = 0.8f;
    p.conditions.stability_score = 0.8f;
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    p.cohort_stats->total_population = 1000000;
    p.cohort_stats->working_age_fraction = 0.6f;
    p.cohort_stats->subsistence_surplus_ratio = 1.5f;  // fed: somewhere worth walking to
    p.cohort_stats->food_store = 1.0e9f;
    p.cohort_stats->polity_id = 1;
    w.provinces.push_back(std::move(p));
    // Link them both ways.
    ProvinceLink a_to_b{};
    a_to_b.neighbor_h3 = static_cast<H3Index>(0x2);
    w.provinces[0].links.push_back(a_to_b);
    w.provinces[0].h3_index = static_cast<H3Index>(0x1);
    w.provinces[1].h3_index = static_cast<H3Index>(0x2);

    DeltaBuffer d{};
    mod.execute(w, d);
    REQUIRE(d.region_deltas.size() == 2);
    REQUIRE(d.region_deltas[0].refugee_flow_replacement.has_value());
    REQUIRE(d.region_deltas[1].refugee_flow_replacement.has_value());
    const float out = *d.region_deltas[0].refugee_flow_replacement;
    const float in = *d.region_deltas[1].refugee_flow_replacement;

    CHECK(out < 0.0f);  // the starving province empties
    CHECK(in > 0.0f);   // the fed one receives them
    // Conserved: nobody is created or destroyed by walking.
    CHECK_THAT(out + in, WithinAbs(0.0f, 1.0f));
}

TEST_CASE("structural_demography: with nowhere better to go, a province starves in place",
          "[structural_demography][psi][contagion]") {
    // The condition that matters as much as the flight itself. The Migration Period
    // happened because there WAS somewhere to go; a province whose neighbours are all
    // worse off keeps its people, and they die where they stand.
    StructuralDemographyModule mod;
    WorldState w = stressed_world(/*wage=*/0.4f, /*held=*/0.10f, /*supported=*/0.02f,
                                  /*food_store=*/0.0f, /*trust=*/0.3f);
    Province p{};
    p.id = 1;
    p.region_id = 1;
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    p.cohort_stats->total_population = 1000000;
    p.cohort_stats->working_age_fraction = 0.6f;
    p.cohort_stats->subsistence_surplus_ratio = 0.3f;  // even worse off than here
    p.cohort_stats->polity_id = 1;
    w.provinces.push_back(std::move(p));
    ProvinceLink a_to_b{};
    a_to_b.neighbor_h3 = static_cast<H3Index>(0x2);
    w.provinces[0].links.push_back(a_to_b);
    w.provinces[0].h3_index = static_cast<H3Index>(0x1);
    w.provinces[1].h3_index = static_cast<H3Index>(0x2);

    DeltaBuffer d{};
    mod.execute(w, d);
    REQUIRE(d.region_deltas.size() == 2);
    CHECK_THAT(d.region_deltas[0].refugee_flow_replacement.value_or(-1.0f),
               WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("structural_demography: inert in market eras", "[structural_demography][psi]") {
    StructuralDemographyModule mod;
    WorldState w = society_coming_apart();
    w.technology.current_era = 8;  // modern (market regime)

    DeltaBuffer d{};
    mod.execute(w, d);
    CHECK(d.region_deltas.empty());
}
