#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
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

TEST_CASE("PopulationAging: constants match spec", "[population_aging][tier11]") {
    REQUIRE_THAT(PopulationAgingModule::COHORT_INCOME_UPDATE_RATE, WithinAbs(0.05f, 0.001f));
    REQUIRE_THAT(PopulationAgingModule::COHORT_EMPLOYMENT_UPDATE_RATE, WithinAbs(0.02f, 0.001f));
    REQUIRE(PopulationAgingModule::TICKS_PER_MONTH == 30);
    REQUIRE(PopulationAgingModule::TICKS_PER_YEAR == 365);
}
