#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "modules/regional_conditions/regional_conditions_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

TEST_CASE("RegionalConditions: stability recovery toward one", "[regional_conditions][tier11]") {
    // 0.80 + 0.001 * (1.0 - 0.80) - 0 = 0.80 + 0.0002 = 0.8002
    float result = RegionalConditionsModule::compute_stability_recovery(0.80f, 0);
    REQUIRE_THAT(result, WithinAbs(0.8002f, 0.001f));
}

TEST_CASE("RegionalConditions: stability degraded by events", "[regional_conditions][tier11]") {
    float result = RegionalConditionsModule::compute_stability_recovery(0.80f, 1);
    // 0.80 + 0.0002 - 0.05 = 0.7502
    REQUIRE_THAT(result, WithinAbs(0.7502f, 0.001f));
}

TEST_CASE("RegionalConditions: stability clamped to zero", "[regional_conditions][tier11]") {
    float result = RegionalConditionsModule::compute_stability_recovery(0.05f, 3);
    // 0.05 + 0.001*0.95 - 0.15 = 0.05 + 0.00095 - 0.15 < 0 -> clamped to 0
    REQUIRE(result >= 0.0f);
}

TEST_CASE("RegionalConditions: stability clamped to one", "[regional_conditions][tier11]") {
    float result = RegionalConditionsModule::compute_stability_recovery(1.0f, 0);
    REQUIRE(result <= 1.0f);
}

TEST_CASE("RegionalConditions: criminal dominance ratio", "[regional_conditions][tier11]") {
    REQUIRE_THAT(RegionalConditionsModule::compute_criminal_dominance(200.0f, 1000.0f),
                 WithinAbs(0.20f, 0.01f));
}

TEST_CASE("RegionalConditions: criminal dominance zero total", "[regional_conditions][tier11]") {
    REQUIRE_THAT(RegionalConditionsModule::compute_criminal_dominance(100.0f, 0.0f),
                 WithinAbs(0.0f, 0.01f));
}

TEST_CASE("RegionalConditions: drought recovery", "[regional_conditions][tier11]") {
    float result = RegionalConditionsModule::compute_drought_recovery(0.50f, 0.005f);
    REQUIRE_THAT(result, WithinAbs(0.505f, 0.001f));
}

TEST_CASE("RegionalConditions: drought recovery capped at 1.0", "[regional_conditions][tier11]") {
    float result = RegionalConditionsModule::compute_drought_recovery(0.998f, 0.005f);
    REQUIRE_THAT(result, WithinAbs(1.0f, 0.01f));
}

TEST_CASE("RegionalConditions: inequality from gini", "[regional_conditions][tier11]") {
    REQUIRE_THAT(RegionalConditionsModule::compute_inequality_from_gini(0.45f),
                 WithinAbs(0.45f, 0.001f));
}

TEST_CASE("RegionalConditions: config defaults match spec", "[regional_conditions][tier11]") {
    constexpr RegionalConditionsConfig cfg{};
    REQUIRE_THAT(cfg.stability_recovery_rate, WithinAbs(0.001f, 0.0001f));
    REQUIRE_THAT(cfg.event_stability_impact, WithinAbs(0.05f, 0.001f));
    REQUIRE_THAT(cfg.drought_recovery_rate, WithinAbs(0.005f, 0.001f));
}
