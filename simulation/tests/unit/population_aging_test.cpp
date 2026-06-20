#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

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
    CHECK(soft_d.total_population < native_d.total_population);
    // Hardiness drifts toward the world's hazard level (up from 0.2 toward ~1.07).
    CHECK(soft_d.hardiness > 0.2f);
    CHECK(soft_d.hardiness < native);  // but only partway in one year (generational)
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
