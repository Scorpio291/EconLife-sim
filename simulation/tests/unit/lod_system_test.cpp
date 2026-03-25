#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "modules/lod_system/lod_system_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

TEST_CASE("LOD: lod1 production calculation", "[lod_system][tier11]") {
    // 100 * 1.2 * (1.0 - 0.1) * 0.8 = 100 * 1.2 * 0.9 * 0.8 = 86.4
    float prod = LodSystemModule::compute_lod1_production(100.0f, 1.2f, 0.1f, 0.8f);
    REQUIRE_THAT(prod, WithinAbs(86.4f, 0.1f));
}

TEST_CASE("LOD: lod1 consumption calculation", "[lod_system][tier11]") {
    // 50 * 1.5 * 1.1 = 82.5
    float cons = LodSystemModule::compute_lod1_consumption(50.0f, 1.5f, 1.1f);
    REQUIRE_THAT(cons, WithinAbs(82.5f, 0.1f));
}

TEST_CASE("LOD: lod2 price modifier scarcity", "[lod_system][tier11]") {
    // consumption=1500, production=1000: ratio=1.5
    float mod = LodSystemModule::compute_lod2_price_modifier(1500.0f, 1000.0f, 1.0f);
    REQUIRE_THAT(mod, WithinAbs(1.5f, 0.01f));
}

TEST_CASE("LOD: lod2 price modifier surplus", "[lod_system][tier11]") {
    // consumption=500, production=1000: ratio=0.5 (at min)
    float mod = LodSystemModule::compute_lod2_price_modifier(500.0f, 1000.0f, 1.0f);
    REQUIRE_THAT(mod, WithinAbs(0.5f, 0.01f));
}

TEST_CASE("LOD: lod2 price modifier clamped max", "[lod_system][tier11]") {
    float mod = LodSystemModule::compute_lod2_price_modifier(5000.0f, 100.0f, 1.0f);
    REQUIRE_THAT(mod, WithinAbs(2.0f, 0.01f));
}

TEST_CASE("LOD: lod2 supply floor prevents div by zero", "[lod_system][tier11]") {
    float mod = LodSystemModule::compute_lod2_price_modifier(100.0f, 0.0f, 1.0f);
    // ratio = 100/1 = 100, clamped to 2.0
    REQUIRE_THAT(mod, WithinAbs(2.0f, 0.01f));
}

TEST_CASE("LOD: smoothing prevents sharp jump", "[lod_system][tier11]") {
    // lerp(1.0, 2.0, 0.30) = 1.0 + 0.30*(2.0-1.0) = 1.30
    float result = LodSystemModule::compute_smoothed_modifier(1.0f, 2.0f, 0.30f);
    REQUIRE_THAT(result, WithinAbs(1.30f, 0.01f));
}

TEST_CASE("LOD: smoothing same values", "[lod_system][tier11]") {
    float result = LodSystemModule::compute_smoothed_modifier(1.5f, 1.5f, 0.30f);
    REQUIRE_THAT(result, WithinAbs(1.5f, 0.01f));
}

TEST_CASE("LOD: monthly tick check", "[lod_system][tier11]") {
    REQUIRE(LodSystemModule::is_monthly_tick(0) == true);
    REQUIRE(LodSystemModule::is_monthly_tick(30) == true);
    REQUIRE(LodSystemModule::is_monthly_tick(15) == false);
}

TEST_CASE("LOD: annual tick check", "[lod_system][tier11]") {
    REQUIRE(LodSystemModule::is_annual_tick(0) == true);
    REQUIRE(LodSystemModule::is_annual_tick(365) == true);
    REQUIRE(LodSystemModule::is_annual_tick(180) == false);
}

TEST_CASE("LOD: constants match spec", "[lod_system][tier11]") {
    REQUIRE(LodSystemModule::TICKS_PER_MONTH == 30);
    REQUIRE(LodSystemModule::TICKS_PER_YEAR == 365);
    REQUIRE_THAT(LodSystemModule::LOD2_MIN_MODIFIER, WithinAbs(0.50f, 0.001f));
    REQUIRE_THAT(LodSystemModule::LOD2_MAX_MODIFIER, WithinAbs(2.00f, 0.001f));
    REQUIRE_THAT(LodSystemModule::LOD2_SMOOTHING_RATE, WithinAbs(0.30f, 0.001f));
}
