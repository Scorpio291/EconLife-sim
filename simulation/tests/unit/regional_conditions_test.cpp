#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"
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

// --- Execute-path smoke coverage ------------------------------------------
// The cases above exercise only the static helpers. These drive the actual
// tick entry point on a minimal world, proving execute_province reads
// province state and emits a RegionDelta (guards against a silent no-op).

namespace {
WorldState make_one_province_world() {
    WorldState w{};
    w.current_tick = 1;
    w.world_seed = 1;
    Province p{};
    p.id = 0;
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    p.conditions.stability_score = 0.80f;
    p.conditions.inequality_index = 0.30f;
    p.cohort_stats->crime_rate = 0.10f;
    p.cohort_stats->criminal_dominance_index = 0.10f;
    w.provinces.push_back(std::move(p));
    return w;
}
}  // namespace

TEST_CASE("RegionalConditions: execute_province emits a RegionDelta",
          "[regional_conditions][tier11]") {
    auto world = make_one_province_world();
    RegionalConditionsModule module;

    DeltaBuffer delta{};
    module.execute_province(0, world, delta);

    REQUIRE(delta.region_deltas.size() == 1);
    const RegionDelta& rd = delta.region_deltas[0];
    REQUIRE(rd.region_id == 0);
    // Stability at 0.80 with no instability events recovers toward 1.0:
    // compute_stability_recovery(0.80, 0) - 0.80 = +0.0002.
    REQUIRE(rd.stability_delta.has_value());
    REQUIRE_THAT(*rd.stability_delta, WithinAbs(0.0002f, 1e-5f));
    // Crime/grievance signals are populated from cohort_stats, not left unset.
    REQUIRE(rd.crime_rate_delta.has_value());
    REQUIRE(rd.grievance_delta.has_value());
}

TEST_CASE("RegionalConditions: execute is deterministic across runs",
          "[regional_conditions][tier11][determinism]") {
    auto run = []() {
        auto world = make_one_province_world();
        RegionalConditionsModule module;
        DeltaBuffer delta{};
        module.execute(world, delta);
        return delta.region_deltas.at(0);
    };
    RegionDelta a = run();
    RegionDelta b = run();
    REQUIRE_THAT(a.stability_delta.value(), WithinAbs(b.stability_delta.value(), 0.0f));
    REQUIRE_THAT(a.crime_rate_delta.value(), WithinAbs(b.crime_rate_delta.value(), 0.0f));
    REQUIRE_THAT(a.grievance_delta.value(), WithinAbs(b.grievance_delta.value(), 0.0f));
}
