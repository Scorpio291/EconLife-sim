#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <limits>

#include "core/rng/deterministic_rng.h"
#include "core/world_state/apply_deltas.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"
#include "modules/population_aging/population_aging_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

TEST_CASE("PopulationAging: income convergence", "[population_aging][tier11]") {
    // current=100, target=150, rate=0.05: 100 + 0.05*(150-100) = 102.5
    float result = PopulationAgingModule::compute_income_convergence(100.0f, 150.0f, 0.05f);
    REQUIRE_THAT(result, WithinAbs(102.5f, 0.1f));
}

TEST_CASE("PopulationAging: income convergence at target", "[population_aging][tier11]") {
    float result = PopulationAgingModule::compute_income_convergence(150.0f, 150.0f, 0.05f);
    REQUIRE_THAT(result, WithinAbs(150.0f, 0.01f));
}

TEST_CASE("PopulationAging: employment convergence", "[population_aging][tier11]") {
    // 0.60 + 0.02*(0.80-0.60) = 0.60 + 0.004 = 0.604
    float result = PopulationAgingModule::compute_employment_convergence(0.60f, 0.80f, 0.02f);
    REQUIRE_THAT(result, WithinAbs(0.604f, 0.001f));
}

TEST_CASE("PopulationAging: employment clamped", "[population_aging][tier11]") {
    float result = PopulationAgingModule::compute_employment_convergence(0.99f, 1.10f, 0.05f);
    REQUIRE(result <= 1.0f);
    REQUIRE(result >= 0.0f);
}

TEST_CASE("PopulationAging: education drift capped", "[population_aging][tier11]") {
    float result = PopulationAgingModule::compute_education_drift(0.50f, 0.80f, 0.01f);
    // Diff=0.30, capped to 0.01 -> 0.51
    REQUIRE_THAT(result, WithinAbs(0.51f, 0.001f));
}

TEST_CASE("PopulationAging: education drift negative", "[population_aging][tier11]") {
    float result = PopulationAgingModule::compute_education_drift(0.50f, 0.30f, 0.01f);
    // Diff=-0.20, capped to -0.01 -> 0.49
    REQUIRE_THAT(result, WithinAbs(0.49f, 0.001f));
}

TEST_CASE("PopulationAging: gini coefficient equal incomes", "[population_aging][tier11]") {
    std::vector<float> incomes = {100.0f, 100.0f, 100.0f, 100.0f};
    float gini = PopulationAgingModule::compute_gini_coefficient(incomes);
    REQUIRE_THAT(gini, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("PopulationAging: gini coefficient unequal incomes", "[population_aging][tier11]") {
    std::vector<float> incomes = {10.0f, 20.0f, 30.0f, 100.0f};
    float gini = PopulationAgingModule::compute_gini_coefficient(incomes);
    REQUIRE(gini > 0.0f);
    REQUIRE(gini <= 1.0f);
}

TEST_CASE("PopulationAging: gini empty returns zero", "[population_aging][tier11]") {
    std::vector<float> incomes;
    REQUIRE_THAT(PopulationAgingModule::compute_gini_coefficient(incomes), WithinAbs(0.0f, 0.001f));
}

TEST_CASE("PopulationAging: monthly tick check", "[population_aging][tier11]") {
    REQUIRE(PopulationAgingModule::is_monthly_tick(0) == true);
    REQUIRE(PopulationAgingModule::is_monthly_tick(30) == true);
    REQUIRE(PopulationAgingModule::is_monthly_tick(15) == false);
}

TEST_CASE("PopulationAging: config defaults match spec", "[population_aging][tier11]") {
    constexpr PopulationAgingConfig cfg{};
    REQUIRE_THAT(cfg.cohort_income_update_rate, WithinAbs(0.05f, 0.001f));
    REQUIRE_THAT(cfg.cohort_employment_update_rate, WithinAbs(0.02f, 0.001f));
    REQUIRE(PopulationAgingModule::TICKS_PER_MONTH == 30);
    REQUIRE(PopulationAgingModule::TICKS_PER_YEAR == 365);
}

// --- Execute-path smoke coverage ------------------------------------------
// Drives the tick entry point. population_aging runs on a monthly cadence,
// so these also lock in the gate: a delta on a monthly tick, none off-month.

namespace {
// A one-province world at an annual tick with a populated working cohort, set up
// to exercise births/deaths under a given subsistence food surplus.
WorldState make_annual_cohort_world(float surplus) {
    WorldState w{};
    w.current_tick = PopulationAgingModule::TICKS_PER_YEAR;  // annual births/deaths fire
    w.world_seed = 1;
    Province p{};
    p.id = 0;
    p.conditions.stability_score = 0.9f;  // high stability so births occur
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    p.cohort_stats->subsistence_surplus_ratio = surplus;
    p.cohort_stats->sick_rate = 0.0f;
    p.cohort_stats->addiction_rate = 0.0f;
    PopulationCohort workers{};
    workers.size = 100000;
    p.cohort_stats->cohorts[DemographicGroup::working_urban_mid] = workers;
    w.provinces.push_back(std::move(p));
    return w;
}

WorldState make_one_province_world(uint32_t tick) {
    WorldState w{};
    w.current_tick = tick;
    w.world_seed = 1;
    Province p{};
    p.id = 0;
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    p.demographics.income_low_fraction = 0.40f;
    p.demographics.income_high_fraction = 0.20f;
    p.demographics.education_level = 0.50f;
    p.conditions.inequality_index = 0.30f;
    w.provinces.push_back(std::move(p));
    return w;
}
}  // namespace

TEST_CASE("PopulationAging: generational hardiness — soft people on a harsh world",
          "[population_aging][hardiness][tier11]") {
    PopulationAgingModule module;
    auto run = [&](float hardiness) {
        WorldState w = make_annual_cohort_world(/*surplus=*/1.5f);  // fed: deaths are hazard-driven
        w.hazard_settings = deathworld_hazard();                   // a harsh world
        w.provinces[0].cohort_stats->hardiness = hardiness;
        DeltaBuffer d{};
        module.execute_province(0, w, d);
        REQUIRE(d.cohort_stats_deltas.size() == 1);
        return d.cohort_stats_deltas[0];
    };
    const float native = hazard_mortality_from_settings(deathworld_hazard());  // adapted (~1.07)
    auto soft_d = run(0.2f);       // garden-bred, unadapted
    auto native_d = run(native);   // native, adapted

    // The unadapted soft people suffer more mortality -> fewer survivors than natives.
    // (Unchanged intent; the GAP is now larger than it was, because the maladaptation
    // ratio of ~5.4 here is no longer pinned to the retired 3x cap.)
    CHECK(soft_d.total_population < native_d.total_population);
    // Hardiness drifts toward the world's hazard level (up from 0.2 toward ~1.07).
    CHECK(soft_d.hardiness > 0.2f);
    CHECK(soft_d.hardiness < native);  // but only partway in one year (generational)
}

TEST_CASE("PopulationAging: hazard mortality is a RATE that saturates at 1.0 (no 3x rail)",
          "[population_aging][hardiness][tier11]") {
    // UPDATED 2026-07-26 — this test used to pin cfg.hazard_mortality_min == 0.15 and
    // cfg.hazard_mortality_max == 3.0 and assert survivors remained a majority *because
    // of the 3x cap*. Those config fields are gone: the maladaptation term is now an
    // uncapped multiplier on the annual mortality RATE, and the bound is physical — the
    // annual probability arrives as p = 1 - exp(-rate), which rises toward 100% and can
    // never exceed it. Same intent (mortality is bounded, not arbitrary), grounded cause.
    constexpr PopulationAgingConfig cfg{};
    using PA = PopulationAgingModule;

    // 1) The multiplier is uncapped. Garden-bred hardiness 0.19 on a deathworld
    //    (world_hazard ~1.076) is exactly where the old band bound hardest: the true
    //    ratio is ~5.7, formerly pinned to 3.0 — a 1.9x softening of the culling.
    const float deathworld = hazard_mortality_from_settings(deathworld_hazard());
    const float soft_mult = PA::hazard_rate_multiplier(deathworld, 0.19f, cfg.hardiness_floor);
    CHECK(soft_mult > 5.0f);  // was pinned at 3.0
    CHECK_THAT(soft_mult, WithinAbs(deathworld / 0.19f, 1e-4f));
    // hardiness_floor is only the divide-by-zero sentinel.
    CHECK_THAT(PA::hazard_rate_multiplier(1.0f, 0.0f, cfg.hardiness_floor),
               WithinAbs(1.0f / cfg.hardiness_floor, 1e-4f));

    // 2) The probability is what saturates: monotone in the rate, never above certainty.
    CHECK(PA::annual_probability_from_rate(0.0f) == 0.0f);
    CHECK_THAT(PA::annual_probability_from_rate(0.0088f), WithinAbs(0.0087614f, 1e-6f));
    float prev = 0.0f;
    for (float rate : {0.05f, 0.5f, 2.0f, 10.0f}) {
        const float p = PA::annual_probability_from_rate(rate);
        CHECK(p > prev);   // strictly increasing in the rate
        CHECK(p <= 1.0f);  // never exceeds certainty
        prev = p;
    }
    CHECK_THAT(PA::annual_probability_from_rate(1.0e6f), WithinAbs(1.0f, 1e-6f));  // -> 100%
    // Non-finite input is a crash sentinel, not a massacre.
    CHECK(PA::annual_probability_from_rate(std::numeric_limits<float>::quiet_NaN()) == 0.0f);

    // 3) End to end. A pathologically unadapted people on a deathworld now takes the
    //    full rate: hardiness 0.01 sits below hardiness_floor, so the multiplier is
    //    world_hazard/0.10 = 10.76 -> rate = 0.008 * 1.1 * 10.76 = 0.0947/person-year
    //    -> p = 9.0%/yr, against 2.6%/yr under the retired 3x cap. Still bounded: the
    //    cohort is culled hard, not annihilated, and population never goes negative.
    PopulationAgingModule module;
    WorldState w = make_annual_cohort_world(/*surplus=*/1.5f);  // fed: deaths are hazard-driven
    w.hazard_settings = deathworld_hazard();
    w.provinces[0].cohort_stats->hardiness = 0.01f;  // pathologically unadapted
    uint32_t before = 0;
    for (const auto& [g, c] : w.provinces[0].cohort_stats->cohorts) {
        (void)g;
        before += c.size;  // the seeded stats.total_population field is not populated
    }
    REQUIRE(before == 100000u);
    DeltaBuffer d{};
    module.execute_province(0, w, d);
    REQUIRE(d.cohort_stats_deltas.size() == 1);
    const uint32_t after = d.cohort_stats_deltas[0].total_population;
    CHECK(after > before / 2);  // bounded: a majority survives one brutal year
    CHECK(after < 95000u);      // and the culling is no longer softened by the cap
                                // (the capped form left ~98,900 of 100,000)
}

TEST_CASE("PopulationAging: Earth-normal mortality survives the rate reform unchanged",
          "[population_aging][hardiness][tier11]") {
    // The rate->probability reform must not move the long-horizon baseline. An
    // Earth-normal world with an Earth-adapted people is the anchor: world_hazard is
    // Earth-normalized to exactly 1.0 and cohort hardiness defaults to 1.0, so the
    // maladaptation multiplier is exactly 1.0 and the composed rate is unchanged. Only
    // the rate -> probability conversion differs, and at Earth-normal rates that is a
    // sub-percent effect:
    //     rate = base_annual_death_rate 0.008 * (1 + (1 - stability 0.9)) = 0.0088/yr
    //     was (rate used directly AS a probability): p = 0.008800
    //     now  p = 1 - exp(-0.0088)                  = 0.0087614   (-0.45% relative)
    constexpr PopulationAgingConfig cfg{};
    const float rate = cfg.base_annual_death_rate * 1.1f;
    const float p = PopulationAgingModule::annual_probability_from_rate(rate);
    CHECK_THAT(p, WithinAbs(0.0087614f, 1e-6f));
    CHECK(p < rate);                    // the rate form is very slightly gentler
    CHECK((rate - p) / rate < 0.005f);  // by under 0.5% of deaths
    // Retirees take the same rate x4; still under 2%.
    const float retiree_rate = rate * cfg.retiree_mortality_multiplier;
    const float p_ret = PopulationAgingModule::annual_probability_from_rate(retiree_rate);
    CHECK((retiree_rate - p_ret) / retiree_rate < 0.02f);

    // End to end on an Earth-normal, exactly-fed province of 100,000. The figure moved
    // when CHILD MORTALITY arrived (R4A): roughly half of children born did not reach
    // fifteen before modern medicine, and the newborns this year are exposed to that rate
    // in the same year they are born.
    //
    // The quantity-quality response is deliberately NEUTRAL here — the youth multiplier
    // is derived so that Earth-normal pre-modern survival is exactly the 0.5 the birth
    // rate is calibrated against — so the difference is the children who now die and the
    // PRE-MODERN birth rate that replaces them. Both are the mechanism working:
    //   was (modern rates, no child mortality, no age ladder):  100,151
    //   now (40 births per 1000, children dying at 5.25x):      101,581
    // A pre-modern society is not near-stationary year to year; it is a high-birth,
    // high-death demography whose NET is near zero over the long run, which is what the
    // multi-millennium runs actually show.
    PopulationAgingModule module;
    WorldState w = make_annual_cohort_world(/*surplus=*/1.0f);  // exactly fed: neutral food
    REQUIRE_THAT(hazard_mortality_from_settings(w.hazard_settings), WithinAbs(1.0f, 1e-6f));
    DeltaBuffer d{};
    module.execute_province(0, w, d);
    REQUIRE(d.cohort_stats_deltas.size() == 1);
    const uint32_t after = d.cohort_stats_deltas[0].total_population;
    CHECK(after >= 101500u);
    CHECK(after <= 101650u);
}

TEST_CASE("PopulationAging: half of children do not reach fifteen, until medicine",
          "[population_aging][tier11][transition]") {
    // The datum the youth multiplier is derived from, and the thing that makes a
    // demographic transition possible at all: with the young dying at the same rate as
    // the middle-aged there is nothing for medicine to fix.
    constexpr PopulationAgingConfig cfg{};
    const float premodern_youth_rate = cfg.base_annual_death_rate * 1.1f *
                                       cfg.youth_mortality_multiplier;
    const float survival = PopulationAgingModule::child_survival(premodern_youth_rate, cfg);
    CHECK_THAT(survival, WithinAbs(0.5f, 0.02f));  // ~half, as the record has it

    // Medicine cuts the rate; survival rises toward one.
    const float modern = PopulationAgingModule::child_survival(premodern_youth_rate * 0.1f, cfg);
    CHECK(modern > 0.9f);
    CHECK(modern < 1.0f);
}

TEST_CASE("PopulationAging: families target surviving children, so births fall as they live",
          "[population_aging][tier11][transition]") {
    // Galor's quantity-quality substitution, and the single mechanism the research
    // flagged as the highest-value fix for a population that keeps rising into a
    // collapse. Neutral at the pre-modern norm, falling sharply as survival improves.
    constexpr PopulationAgingConfig cfg{};
    CHECK_THAT(PopulationAgingModule::desired_births_factor(cfg.reference_child_survival, cfg),
               WithinAbs(1.0f, 1e-4f));

    const float modern = PopulationAgingModule::desired_births_factor(0.9f, cfg);
    CHECK(modern < 1.0f);
    // 0.5 -> 0.9 survival cuts desired births by about 44% through this channel alone.
    CHECK_THAT(1.0f - modern, WithinAbs(0.44f, 0.02f));

    // And a world harsher than the norm has MORE children, which is the same law read
    // the other way and why high-mortality societies had high fertility.
    CHECK(PopulationAgingModule::desired_births_factor(0.3f, cfg) > 1.0f);
}

TEST_CASE("PopulationAging: subsistence surplus drives the Malthusian loop",
          "[population_aging][subsistence][tier11]") {
    PopulationAgingModule module;
    auto annual_population = [&](float surplus) {
        WorldState w = make_annual_cohort_world(surplus);
        DeltaBuffer d{};
        module.execute_province(0, w, d);
        REQUIRE(d.cohort_stats_deltas.size() == 1);
        return d.cohort_stats_deltas[0].total_population;
    };

    const uint32_t fed = annual_population(1.0f);      // exactly fed: neutral
    const uint32_t surplus = annual_population(1.5f);  // surplus: population grows
    const uint32_t famine = annual_population(0.5f);   // deficit: population is culled

    CHECK(surplus > fed);    // a food surplus lifts births
    CHECK(famine < fed);     // a deficit raises mortality
    CHECK(famine < surplus);
}

TEST_CASE("PopulationAging: execute_province emits a RegionDelta on a monthly tick",
          "[population_aging][tier11]") {
    auto world = make_one_province_world(/*tick=*/PopulationAgingModule::TICKS_PER_MONTH);
    PopulationAgingModule module;

    DeltaBuffer delta{};
    module.execute_province(0, world, delta);

    REQUIRE(delta.region_deltas.size() == 1);
    const RegionDelta& rd = delta.region_deltas[0];
    REQUIRE(rd.region_id == 0);
    REQUIRE(rd.stability_delta.has_value());
    REQUIRE(rd.inequality_delta.has_value());
    // Grievance is no longer written here — it has a single owner
    // (community_response). This module previously injected a constant
    // income_low_fraction grievance pump every tick.
    REQUIRE_FALSE(rd.grievance_delta.has_value());
}

TEST_CASE("PopulationAging: execute_province is a no-op off the monthly cadence",
          "[population_aging][tier11]") {
    // Tick 1 is not a multiple of TICKS_PER_MONTH (30): the module must skip.
    auto world = make_one_province_world(/*tick=*/1);
    PopulationAgingModule module;

    DeltaBuffer delta{};
    module.execute_province(0, world, delta);

    REQUIRE(delta.region_deltas.empty());
}

// ===========================================================================
// Background-population cohort lifecycle (built out from the former stub)
// ===========================================================================
namespace {
void seed_two_cohorts(RegionCohortStats& cs) {
    PopulationCohort a;
    a.group = DemographicGroup::working_urban_mid;
    a.size = 1000;
    a.median_income = 100.0f;
    a.education_level = 0.50f;
    a.employment_rate = 0.60f;
    PopulationCohort b;
    b.group = DemographicGroup::working_rural_low;
    b.size = 500;
    b.median_income = 50.0f;
    b.education_level = 0.40f;
    b.employment_rate = 0.50f;
    cs.cohorts[a.group] = a;
    cs.cohorts[b.group] = b;
    cs.regional_wage_anchor = 150.0f;
    cs.formal_employment_rate = 0.80f;
}
}  // namespace

TEST_CASE("PopulationAging: monthly cohort income + employment convergence",
          "[population_aging][tier11]") {
    auto world = make_one_province_world(/*tick=*/PopulationAgingModule::TICKS_PER_MONTH);
    seed_two_cohorts(*world.provinces[0].cohort_stats);

    PopulationAgingModule module;
    DeltaBuffer delta{};
    module.execute_province(0, world, delta);
    REQUIRE(delta.cohort_stats_deltas.size() == 1);
    apply_deltas(world, delta);

    const auto& cohorts = world.provinces[0].cohort_stats->cohorts;
    const auto& a = cohorts.at(DemographicGroup::working_urban_mid);
    // income: 100 + 0.05*(150-100) = 102.5 ; employment: 0.6 + 0.02*(0.8-0.6) = 0.604
    REQUIRE_THAT(a.median_income, WithinAbs(102.5f, 1e-3f));
    REQUIRE_THAT(a.employment_rate, WithinAbs(0.604f, 1e-4f));
    // total_population preserved on a monthly tick (no births/deaths), gini > 0.
    REQUIRE(world.provinces[0].cohort_stats->total_population == 1500u);
    REQUIRE(world.provinces[0].cohort_stats->gini_coefficient > 0.0f);
}

TEST_CASE("PopulationAging: annual education drift is capped", "[population_aging][tier11]") {
    auto world = make_one_province_world(/*tick=*/PopulationAgingModule::TICKS_PER_YEAR);
    world.provinces[0].demographics.education_level = 0.80f;  // target well above cohort 0.50
    seed_two_cohorts(*world.provinces[0].cohort_stats);

    PopulationAgingModule module;
    DeltaBuffer delta{};
    module.execute_province(0, world, delta);
    REQUIRE(delta.cohort_stats_deltas.size() == 1);
    apply_deltas(world, delta);

    // education moved by at most max_education_drift_per_year (0.01).
    float edu = world.provinces[0]
                    .cohort_stats->cohorts.at(DemographicGroup::working_urban_mid)
                    .education_level;
    REQUIRE(edu <= 0.50f + 0.01f + 1e-5f);
    REQUIRE(edu > 0.50f);  // drifted up toward 0.80
}

TEST_CASE("PopulationAging: empty cohorts produce no cohort delta", "[population_aging][tier11]") {
    auto world = make_one_province_world(/*tick=*/PopulationAgingModule::TICKS_PER_MONTH);
    // cohorts left empty.
    PopulationAgingModule module;
    DeltaBuffer delta{};
    module.execute_province(0, world, delta);
    REQUIRE(delta.cohort_stats_deltas.empty());
}

TEST_CASE("PopulationAging: better healthcare yields more net births",
          "[population_aging][tier11]") {
    PopulationAgingModule module;

    auto run = [&](float sick_rate) -> uint32_t {
        auto world = make_one_province_world(/*tick=*/PopulationAgingModule::TICKS_PER_YEAR);
        auto& cs = *world.provinces[0].cohort_stats;
        seed_two_cohorts(cs);
        cs.sick_rate = sick_rate;
        world.provinces[0].conditions.stability_score = 0.80f;
        DeltaBuffer delta{};
        module.execute_province(0, world, delta);
        apply_deltas(world, delta);
        return world.provinces[0].cohort_stats->total_population;
    };

    uint32_t healthy = run(0.10f);  // low sickness -> high birth survival
    uint32_t sick = run(0.90f);     // high sickness -> low birth survival
    REQUIRE(healthy > sick);
}

// ===========================================================================
// Significant-NPC aging (annual age advancement + natural death)
// ===========================================================================

TEST_CASE("PopulationAging: natural death probability gating", "[population_aging][tier11]") {
    // Below lifespan -> zero.
    REQUIRE_THAT(PopulationAgingModule::compute_natural_death_probability(40.0f, 80.0f, 0.10f),
                 WithinAbs(0.0f, 1e-6f));
    // At lifespan -> base prob.
    REQUIRE_THAT(PopulationAgingModule::compute_natural_death_probability(80.0f, 80.0f, 0.10f),
                 WithinAbs(0.10f, 1e-4f));
    // Well past lifespan -> clamps to 1.0.
    REQUIRE_THAT(PopulationAgingModule::compute_natural_death_probability(10000.0f, 80.0f, 0.10f),
                 WithinAbs(1.0f, 1e-4f));
}

TEST_CASE("PopulationAging: annual tick advances NPC age", "[population_aging][tier11]") {
    auto world = make_one_province_world(/*tick=*/PopulationAgingModule::TICKS_PER_YEAR);
    NPC npc{};
    npc.id = 5;
    npc.status = NPCStatus::active;
    npc.current_province_id = 0;
    npc.age_years = 40.0f;
    world.significant_npcs.push_back(npc);

    PopulationAgingModule module;
    DeltaBuffer delta{};
    module.execute_province(0, world, delta);

    bool found = false;
    for (const auto& nd : delta.npc_deltas) {
        if (nd.npc_id == 5) {
            REQUIRE(nd.age_delta.has_value());
            REQUIRE_THAT(*nd.age_delta, WithinAbs(1.0f, 1e-6f));
            REQUIRE_FALSE(nd.new_status.has_value());  // 40 << lifespan, no death
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("PopulationAging: NPC far past lifespan dies", "[population_aging][tier11]") {
    auto world = make_one_province_world(/*tick=*/PopulationAgingModule::TICKS_PER_YEAR);
    NPC npc{};
    npc.id = 9;
    npc.status = NPCStatus::active;
    npc.current_province_id = 0;
    npc.age_years = 10000.0f;  // death probability clamps to 1.0
    world.significant_npcs.push_back(npc);

    PopulationAgingModule module;
    DeltaBuffer delta{};
    module.execute_province(0, world, delta);
    apply_deltas(world, delta);

    REQUIRE(world.significant_npcs[0].status == NPCStatus::dead);
}

TEST_CASE("PopulationAging: NPC aging only fires on annual ticks", "[population_aging][tier11]") {
    auto world = make_one_province_world(/*tick=*/PopulationAgingModule::TICKS_PER_MONTH);  // 30
    NPC npc{};
    npc.id = 1;
    npc.status = NPCStatus::active;
    npc.current_province_id = 0;
    npc.age_years = 40.0f;
    world.significant_npcs.push_back(npc);

    PopulationAgingModule module;
    DeltaBuffer delta{};
    module.execute_province(0, world, delta);
    // Tick 30 is monthly but not annual: no NPC age delta.
    for (const auto& nd : delta.npc_deltas)
        REQUIRE(nd.npc_id != 1);
}

TEST_CASE("PopulationAging: disease epidemics — episodic, scaled by disease dial and crowding",
          "[population_aging][tier11]") {
    PopulationAgingConfig cfg{};

    // A disease-free world (dial 0) never has outbreaks.
    for (uint32_t s = 0; s < 200; ++s) {
        DeterministicRNG rng(s);
        CHECK(PopulationAgingModule::epidemic_mortality_factor(0.0f, 0.5f, rng, cfg) == 1.0f);
    }

    // Outbreak probability rises with the disease dial AND with urban crowding.
    auto outbreak_rate = [&](float disease, float density) {
        int hits = 0;
        const int N = 4000;
        for (int s = 0; s < N; ++s) {
            DeterministicRNG rng(static_cast<uint64_t>(s) * 2654435761ull + 1u);
            if (PopulationAgingModule::epidemic_mortality_factor(disease, density, rng, cfg) > 1.0f)
                ++hits;
        }
        return static_cast<double>(hits) / N;
    };
    const double low = outbreak_rate(0.3f, 0.0f);
    const double high_disease = outbreak_rate(1.0f, 0.0f);
    const double high_density = outbreak_rate(1.0f, 1.0f);
    CHECK(low > 0.0);
    CHECK(high_disease > low);           // a plaguier world outbreaks more
    CHECK(high_density > high_disease);  // crowded towns are disease vectors

    // When an outbreak strikes, the mortality spike is the deterministic formula
    // 1 + severity * disease * (1 + density) * reached, where `reached` is the share of
    // the population the wave actually infects: attack_rate x susceptible. This helper
    // reports a FIRST wave (susceptible = 1.0), so reached == attack_rate.
    //
    // UPDATED for R3B (recurrent waves). The spike used to be independent of how many
    // people had ever met the disease, which made every plague identical and made the
    // Black Death a single blip — where the record shows England still losing people a
    // century later, because plague came back to a population that had partly lost its
    // resistance to a new generation.
    const float expected =
        1.0f + cfg.epidemic_severity * 1.0f * (1.0f + 1.0f) * cfg.epidemic_attack_rate;
    bool saw_outbreak = false;
    for (uint32_t s = 0; s < 500 && !saw_outbreak; ++s) {
        DeterministicRNG rng(s);
        const float f = PopulationAgingModule::epidemic_mortality_factor(1.0f, 1.0f, rng, cfg);
        if (f > 1.0f) {
            CHECK_THAT(f, WithinAbs(expected, 1e-4f));
            saw_outbreak = true;
        }
    }
    CHECK(saw_outbreak);
}

TEST_CASE("PopulationAging: geology disasters — episodic, scaled by the geology dial",
          "[population_aging][tier11]") {
    PopulationAgingConfig cfg{};

    // A geologically calm world (dial 0) never has disasters.
    for (uint32_t s = 0; s < 200; ++s) {
        DeterministicRNG rng(s);
        CHECK(PopulationAgingModule::disaster_mortality_factor(0.0f, rng, cfg) == 1.0f);
    }

    // Disaster probability rises with the geology dial (NOT density — a quake hits all).
    auto rate = [&](float geology) {
        int hits = 0;
        const int N = 5000;
        for (int s = 0; s < N; ++s) {
            DeterministicRNG rng(static_cast<uint64_t>(s) * 40503ull + 7u);
            if (PopulationAgingModule::disaster_mortality_factor(geology, rng, cfg) > 1.0f)
                ++hits;
        }
        return static_cast<double>(hits) / N;
    };
    CHECK(rate(1.0f) > rate(0.3f));
    CHECK(rate(0.3f) > 0.0);

    // The disaster spike is the deterministic formula 1 + severity * geology.
    const float expected = 1.0f + cfg.geology_disaster_severity * 1.0f;
    bool saw = false;
    for (uint32_t s = 0; s < 500 && !saw; ++s) {
        DeterministicRNG rng(s);
        const float f = PopulationAgingModule::disaster_mortality_factor(1.0f, rng, cfg);
        if (f > 1.0f) {
            CHECK_THAT(f, WithinAbs(expected, 1e-4f));
            saw = true;
        }
    }
    CHECK(saw);
}

TEST_CASE("PopulationAging: radiation chronically depresses fertility (planetary)",
          "[population_aging][tier11]") {
    PopulationAgingConfig cfg{};
    CHECK(PopulationAgingModule::radiation_fertility_factor(0.0f, cfg) == 1.0f);
    const float earth = PopulationAgingModule::radiation_fertility_factor(0.2f, cfg);
    const float hot = PopulationAgingModule::radiation_fertility_factor(1.0f, cfg);
    CHECK(earth < 1.0f);
    CHECK(hot < earth);  // a more irradiated world is less fertile
    CHECK(hot >= 0.0f);
    CHECK_THAT(hot, WithinAbs(1.0f - cfg.radiation_fertility_penalty, 1e-4f));
}

// ===========================================================================
// THE WAGE VALVE — growth is an equilibrium, not a cap.
//
// Fixed land means more people cut the marginal product of labour: real wages
// fall, delaying marriage and depressing fertility while raising mortality.
// England shows no real-wage trend 1200-1800 despite population tripling —
// growth was pinned near zero by this valve. Measured here it ran ~0.18%/yr
// against a real ~0.04%/yr, and that speed let population outrun the
// institutions a society needs, so the learned stratum never formed.
// ===========================================================================

TEST_CASE("population: a society exactly at subsistence stops growing on its own",
          "[population_aging][tier11][wage]") {
    // w = 1 is the crossing point: both the fertility and mortality responses are
    // neutral there, so the population neither runs away nor dies out. Nothing
    // imposes this — it falls out of where the two power laws meet.
    PopulationAgingModule mod;
    WorldState w = make_annual_cohort_world(/*surplus=*/1.0f);
    const uint64_t before = w.provinces[0].cohort_stats->cohorts[
        DemographicGroup::working_urban_mid].size;

    DeltaBuffer d{};
    mod.execute_province(0, w, d);
    apply_deltas(w, d);

    uint64_t after = 0;
    for (const auto& [g, c] : w.provinces[0].cohort_stats->cohorts) {
        (void)g;
        after += c.size;
    }
    // Within a few percent of where it started after a year at exact subsistence.
    const double drift = std::abs(static_cast<double>(after) - static_cast<double>(before)) /
                         static_cast<double>(before);
    CHECK(drift < 0.05);
}

TEST_CASE("population: growth answers to how well fed people are, in both directions",
          "[population_aging][tier11][wage]") {
    // Slack in the food supply brings marriage forward and growth follows; pressing
    // on the land kills before outright famine does. The valve is symmetric — which
    // is what makes the growth RATE emergent rather than chosen.
    PopulationAgingModule mod;

    auto year_change = [&](float surplus) {
        WorldState w = make_annual_cohort_world(surplus);
        uint64_t before = 0;
        for (const auto& [g, c] : w.provinces[0].cohort_stats->cohorts) {
            (void)g;
            before += c.size;
        }
        DeltaBuffer d{};
        mod.execute_province(0, w, d);
        apply_deltas(w, d);
        uint64_t after = 0;
        for (const auto& [g, c] : w.provinces[0].cohort_stats->cohorts) {
            (void)g;
            after += c.size;
        }
        return static_cast<double>(after) - static_cast<double>(before);
    };

    const double fed = year_change(1.6f);      // room to spare
    const double subsistence = year_change(1.0f);
    const double hungry = year_change(0.7f);   // pressing on the land

    CHECK(fed > subsistence);
    CHECK(hungry < subsistence);
    // Chronic hunger SLOWS growth rather than reversing it. Outright decline is the
    // acute famine path, which fires when the granary is actually empty — the two are
    // deliberately separate, because historically famine mortality was a weak long-run
    // check (its demographic bite is mostly lost births plus emigration, with fast
    // rebound) while chronic undernutrition is what held pre-industrial growth near zero
    // for centuries.
    //
    // The single-year numbers are large in both directions now that births run at the
    // pre-modern 40 per 1000 rather than the modern 12: a pre-modern demography is
    // high-birth and high-death, and it is the NET over centuries that is near zero. So
    // the claim to test is the ORDERING, not a small absolute change.
    // This fixture exercises the PREVENTIVE check alone — the granary is not empty, so
    // the mortality valve is neutral and only fertility answers. That is the right
    // channel to be dominant: in England real wages moved the birth rate far more than
    // they moved the death rate. At w = 0.7 the fertility elasticity of 0.4 gives
    // 0.7^0.4 = 0.87, so about 13% fewer births — which is what shows up here.
    CHECK(hungry > 0.0);               // still growing, just more slowly
    CHECK(hungry < 0.95 * subsistence);  // hunger has taken a real bite out of it
}

// ===========================================================================
// THE URBAN GRAVEYARD — a town is a standing flow, not a stock.
//
// Before sanitation, towns buried more people than they christened: the midden
// sat next to the well and every child met every disease before it was five.
// London's burials exceeded its baptisms in almost every year of the 17th and
// 18th centuries, and it doubled anyway — entirely on migrants walking in from
// the countryside. So a town holds its size only while the land around it has
// both spare grain and spare people to send, which is why pre-industrial
// urbanisation sat near a tenth of the population however rich the society got.
// ===========================================================================

namespace {

// A commons-era province with a town and a countryside, both working-age.
WorldState make_commons_town_world(uint32_t urban, uint32_t rural) {
    WorldState w{};
    w.current_tick = PopulationAgingModule::TICKS_PER_YEAR;  // annual births/deaths fire
    w.world_seed = 1;
    w.era_catalog.load_builtin_default();
    w.technology.current_era = 5;  // feudal — a commons regime
    Province p{};
    p.id = 0;
    p.conditions.stability_score = 0.9f;
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    p.cohort_stats->subsistence_surplus_ratio = 1.0f;  // exactly fed: the valve is neutral
    p.cohort_stats->food_store = 1000.0f;              // granary stocked: no acute famine
    // A town is limited by what the countryside can SPARE (the non-farming stratum) and
    // by what it can FEED (haulage). These cases exercise the haulage limit, so the
    // stratum is left slack; the stratum limit has its own case below.
    p.cohort_stats->specialist_fraction = 1.0f;
    PopulationCohort town{};
    town.size = urban;
    PopulationCohort land{};
    land.size = rural;
    p.cohort_stats->cohorts[DemographicGroup::working_urban_mid] = town;
    p.cohort_stats->cohorts[DemographicGroup::working_rural_mid] = land;
    p.cohort_stats->urban_population = static_cast<float>(urban);
    p.cohort_stats->total_population = urban + rural;
    w.provinces.push_back(std::move(p));
    return w;
}

uint64_t cohort_size(const WorldState& w, DemographicGroup g) {
    const auto& c = w.provinces[0].cohort_stats->cohorts;
    auto it = c.find(g);
    return it == c.end() ? 0u : it->second.size;
}

}  // namespace

TEST_CASE("population: a town buries more of its people than the countryside does",
          "[population_aging][tier11][graveyard]") {
    // Same province, same year, same everything except where people live. The town
    // pays a death rate the fields do not, because crowding is its own cause of
    // death. Every other hazard in the province hits both alike, so the gap is the
    // graveyard and nothing else.
    PopulationAgingModule mod;
    WorldState w = make_commons_town_world(/*urban=*/100000, /*rural=*/100000);
    w.provinces[0].cohort_stats->urban_capacity = 100000.0f;  // no migration pressure either way

    DeltaBuffer d{};
    mod.execute_province(0, w, d);
    apply_deltas(w, d);

    CHECK(cohort_size(w, DemographicGroup::working_urban_mid) <
          cohort_size(w, DemographicGroup::working_rural_mid));
}

TEST_CASE("population: crowding is what kills, so a hamlet is not a city",
          "[population_aging][tier11][graveyard]") {
    // The penalty rises with the size of the town and saturates — it approaches the
    // full rate as the town grows and never exceeds it, so nothing caps it.
    PopulationAgingConfig cfg{};
    const float hamlet = PopulationAgingModule::urban_crowding_rate(200.0f, 1.0f, cfg);
    const float town = PopulationAgingModule::urban_crowding_rate(10000.0f, 1.0f, cfg);
    const float city = PopulationAgingModule::urban_crowding_rate(1000000.0f, 1.0f, cfg);

    CHECK(hamlet < town);
    CHECK(town < city);
    CHECK(city < cfg.urban_crowding_death_rate);              // approached, never reached
    CHECK_THAT(town, WithinAbs(cfg.urban_crowding_death_rate * 0.5f, 1e-6f));  // half-saturation
    CHECK(hamlet < 0.05f * cfg.urban_crowding_death_rate);    // a village is barely a town
}

TEST_CASE("population: medicine is what closes the grave",
          "[population_aging][tier11][graveyard]") {
    // The urban penalty is released by exactly the technology that ended the plagues —
    // sewers and germ theory. This is why urbanisation could not break past its
    // pre-modern tenth until the 19th century, and then did so everywhere at once.
    PopulationAgingConfig cfg{};
    const float medieval = PopulationAgingModule::urban_crowding_rate(200000.0f, 1.0f, cfg);
    const float modern = PopulationAgingModule::urban_crowding_rate(200000.0f, 0.1f, cfg);
    CHECK(modern < medieval);
    CHECK_THAT(modern, WithinAbs(medieval * 0.1f, 1e-6f));
}

TEST_CASE("population: children are born where their parents live",
          "[population_aging][tier11][graveyard]") {
    // This used to be a flat half-and-half split, which drove ANY society toward a 50%
    // urban composition no matter what its land could feed — the town's share was
    // decided by the split, not by anything in the world. A countryside holding nine
    // tenths of the working-age population must bear about nine tenths of the children.
    PopulationAgingModule mod;
    WorldState w = make_commons_town_world(/*urban=*/10000, /*rural=*/90000);
    w.provinces[0].cohort_stats->subsistence_surplus_ratio = 1.5f;  // slack: births happen
    w.provinces[0].cohort_stats->urban_capacity = 10000.0f;         // migration neutral

    DeltaBuffer d{};
    mod.execute_province(0, w, d);
    apply_deltas(w, d);

    const double town_born = static_cast<double>(cohort_size(w, DemographicGroup::youth_urban));
    const double land_born = static_cast<double>(cohort_size(w, DemographicGroup::youth_rural));
    REQUIRE(town_born + land_born > 0.0);
    CHECK(land_born > town_born * 5.0);
    CHECK_THAT(town_born / (town_born + land_born), WithinAbs(0.10, 0.01));
}

TEST_CASE("population: people walk toward bread, and back when it runs out",
          "[population_aging][tier11][graveyard]") {
    // Migration is conserved head for head — the town grows by emptying the
    // countryside, and empties back onto it when the catchment fails. Nobody is
    // created or destroyed by moving.
    PopulationAgingModule mod;

    auto run = [&](float capacity) {
        WorldState w = make_commons_town_world(/*urban=*/10000, /*rural=*/90000);
        w.provinces[0].cohort_stats->urban_capacity = capacity;
        const uint64_t before = cohort_size(w, DemographicGroup::working_urban_mid) +
                                cohort_size(w, DemographicGroup::working_rural_mid);
        DeltaBuffer d{};
        mod.execute_province(0, w, d);
        apply_deltas(w, d);
        struct R {
            uint64_t town, land, before;
        };
        return R{cohort_size(w, DemographicGroup::working_urban_mid),
                 cohort_size(w, DemographicGroup::working_rural_mid), before};
    };

    const auto plenty = run(60000.0f);  // grain for six times the town it has
    const auto famine = run(0.0f);      // the catchment can feed nobody

    CHECK(plenty.town > 10000u);  // the countryside empties into the town
    CHECK(plenty.land < 90000u);
    CHECK(famine.town < 10000u);  // and the town empties back onto the land
    CHECK(famine.land > plenty.land);

    // Conserved: what the town gained the countryside lost. The two working cohorts also
    // shed people to RETIREMENT now that cohorts age (about a forty-seventh a year), and
    // to deaths, so the pair does not stay exactly flat — but nothing leaves via
    // migration that does not arrive.
    const double moved_total = static_cast<double>(plenty.town + plenty.land);
    CHECK(moved_total < static_cast<double>(plenty.before) * 1.001);
    CHECK(moved_total > static_cast<double>(plenty.before) * 0.95);
}

TEST_CASE("population: a town with no countryside to draw on cannot hold its size",
          "[population_aging][tier11][graveyard]") {
    // The whole point of the graveyard: with grain enough to feed everyone and nobody
    // dying of anything unusual, a town that has nowhere to recruit from still loses
    // people, and loses them faster than the same town would in a world where crowding
    // did not kill. Towns are sustained by the countryside, not by themselves.
    auto run_decade = [](float crowding_rate) {
        PopulationAgingConfig cfg{};
        cfg.urban_crowding_death_rate = crowding_rate;
        PopulationAgingModule mod(cfg);
        WorldState w = make_commons_town_world(/*urban=*/200000, /*rural=*/0);
        for (int year = 0; year < 10; ++year) {
            auto& cs = *w.provinces[0].cohort_stats;
            cs.urban_capacity = static_cast<float>(cs.total_population);  // grain is never
                                                                         // the constraint
            cs.food_store = 1000.0f;
            DeltaBuffer d{};
            mod.execute_province(0, w, d);
            apply_deltas(w, d);
            w.current_tick += PopulationAgingModule::TICKS_PER_YEAR;
        }
        uint64_t urban = 0;
        for (const auto& [g, c] : w.provinces[0].cohort_stats->cohorts) {
            if (g == DemographicGroup::youth_urban || g == DemographicGroup::working_urban_low ||
                g == DemographicGroup::working_urban_mid ||
                g == DemographicGroup::working_urban_high || g == DemographicGroup::retiree_urban)
                urban += c.size;
        }
        return urban;
    };

    const uint64_t with_graveyard = run_decade(PopulationAgingConfig{}.urban_crowding_death_rate);
    const uint64_t without = run_decade(0.0f);

    CHECK(with_graveyard < without);      // crowding is what drains it
    CHECK(with_graveyard < 200000u);      // and it drains: the town cannot replace itself
}

TEST_CASE("population: a town cannot be larger than the countryside can spare",
          "[population_aging][tier11][graveyard]") {
    // Grain in the granary is not enough on its own. Somebody has to grow it, and every
    // hand that walks to town is a hand out of the fields — so the harvest itself sets
    // how many people a society can afford to have doing something other than farming.
    // With grain to spare but only a twentieth of the population freed from the land,
    // the town shrinks toward that twentieth rather than filling up.
    PopulationAgingModule mod;
    WorldState w = make_commons_town_world(/*urban=*/20000, /*rural=*/80000);
    w.provinces[0].cohort_stats->urban_capacity = 100000.0f;  // grain for everyone
    w.provinces[0].cohort_stats->specialist_fraction = 0.05f;  // but only 5% can be spared

    const uint64_t before = cohort_size(w, DemographicGroup::working_urban_mid);
    DeltaBuffer d{};
    mod.execute_province(0, w, d);
    apply_deltas(w, d);

    CHECK(cohort_size(w, DemographicGroup::working_urban_mid) < before);
    // The countryside receives them. It is also shedding retirees now that cohorts age,
    // so the comparison is against a control with no migration pressure rather than
    // against the starting headcount.
    WorldState control = make_commons_town_world(/*urban=*/20000, /*rural=*/80000);
    control.provinces[0].cohort_stats->urban_capacity = 20000.0f;   // town already at
    control.provinces[0].cohort_stats->specialist_fraction = 0.20f;  // both limits
    DeltaBuffer cd{};
    mod.execute_province(0, control, cd);
    apply_deltas(control, cd);
    CHECK(cohort_size(w, DemographicGroup::working_rural_mid) >
          cohort_size(control, DemographicGroup::working_rural_mid));
}

// ===========================================================================
// PLAGUE COMES BACK — one blip is not what the record shows.
//
// England fell from 4.8M in 1348 to 2.6M by 1351 and then KEPT FALLING, to a
// nadir of 1.9M around 1450 — a hundred years after the Black Death. Recovery
// took that long not because one plague was so severe but because plague
// returned: 1361, 1369, 1375, 1390, 1400, and on into the 17th century.
//
// Each wave was milder than the last because it found fewer people who had
// never had it, and each interval was long enough for a new generation of
// susceptibles to be born. Neither the interval nor the declining lethality is
// written anywhere here — both fall out of one stock being drawn down and
// refilled.
// ===========================================================================

TEST_CASE("population: the second wave is milder because it finds fewer susceptibles",
          "[population_aging][tier11][plague]") {
    // The whole mechanism in one comparison. Same disease, same crowding, same roll —
    // only the share of people who have never met it differs.
    PopulationAgingConfig cfg{};
    auto severity_at = [&](float susceptible) {
        DeterministicRNG rng(1);
        // Find a seed that actually produces an outbreak, then compare severities.
        for (int seed = 1; seed < 500; ++seed) {
            DeterministicRNG r(static_cast<uint64_t>(seed));
            auto y = PopulationAgingModule::plague_year(/*disease=*/1.0f, /*urban=*/0.2f,
                                                        susceptible, r, cfg);
            if (y.outbreak)
                return y.mortality_factor;
        }
        return 1.0f;
    };
    const float first_wave = severity_at(1.0f);   // nobody has had it
    const float later_wave = severity_at(0.35f);  // most survivors carry resistance

    CHECK(first_wave > 1.0f);
    CHECK(later_wave > 1.0f);
    CHECK(later_wave < first_wave);
}

TEST_CASE("population: a wave spends the susceptibles it reaches",
          "[population_aging][tier11][plague]") {
    // Survivors of an infection do not take it again, so the stock falls by exactly what
    // the wave reached. This is the drawdown that makes the NEXT wave milder.
    PopulationAgingConfig cfg{};
    for (int seed = 1; seed < 500; ++seed) {
        DeterministicRNG rng(static_cast<uint64_t>(seed));
        auto y = PopulationAgingModule::plague_year(1.0f, 0.2f, /*susceptible=*/1.0f, rng, cfg);
        if (!y.outbreak)
            continue;
        CHECK(y.susceptible_after < 1.0f);
        CHECK_THAT(y.susceptible_after, WithinAbs(1.0f - cfg.epidemic_attack_rate, 1e-4f));
        return;
    }
    FAIL("no outbreak in 500 draws at disease = 1.0");
}

TEST_CASE("population: a new generation restores what the plague took",
          "[population_aging][tier11][plague]") {
    // The recurrence interval is not a number anywhere — it is how long it takes a
    // population to replace itself. At a pre-modern life expectancy of ~35 years, about a
    // thirtieth of the population is new each year, and the new have never met the
    // disease. That is why plague returned to England six times in fifty years.
    PopulationAgingConfig cfg{};
    float susceptible = 0.30f;  // just after a heavy wave
    int years_to_two_thirds = 0;
    for (int year = 1; year <= 200; ++year) {
        DeterministicRNG rng(0xD15EA5Eull);  // a seed that yields no outbreak: turnover only
        auto y = PopulationAgingModule::plague_year(/*disease=*/0.0f, 0.0f, susceptible, rng, cfg);
        susceptible = y.susceptible_after;
        if (susceptible >= 0.667f) {
            years_to_two_thirds = year;
            break;
        }
    }
    INFO("years for susceptibility to climb 0.30 -> 0.67: " << years_to_two_thirds);
    CHECK(years_to_two_thirds > 10);   // a generation, not a season
    CHECK(years_to_two_thirds < 60);   // but within a human lifetime, so plague DOES return
}

TEST_CASE("population: on a disease-free world plague never comes",
          "[population_aging][tier11][plague]") {
    // The world spectrum still governs: a world with no disease load has no waves at all,
    // and its susceptible stock simply stays full.
    PopulationAgingConfig cfg{};
    DeterministicRNG rng(7);
    auto y = PopulationAgingModule::plague_year(/*disease=*/0.0f, 0.5f, 1.0f, rng, cfg);
    CHECK_FALSE(y.outbreak);
    CHECK_THAT(y.mortality_factor, WithinAbs(1.0f, 1e-9f));
    CHECK_THAT(y.susceptible_after, WithinAbs(1.0f, 1e-9f));
}
