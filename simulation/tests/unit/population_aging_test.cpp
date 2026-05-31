#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

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
    REQUIRE(rd.grievance_delta.has_value());
    // grievance = GRIEVANCE_UNEMPLOYMENT_WEIGHT(0.003) * income_low_fraction(0.40).
    REQUIRE_THAT(*rd.grievance_delta, WithinAbs(0.0012f, 1e-5f));
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
