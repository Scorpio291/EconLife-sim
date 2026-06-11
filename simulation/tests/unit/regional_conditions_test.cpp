#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/apply_deltas.h"
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
    p.cohort_stats->total_population = 1000;  // populated -> normal (non-zero-pop) path
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
    // Crime signal is populated from cohort_stats. Grievance is no longer
    // written here — it has a single owner (community_response); this module
    // previously also wrote grievance (inequality + crime + a stage feedback
    // pump), an uncoordinated second writer.
    REQUIRE(rd.crime_rate_delta.has_value());
    REQUIRE_FALSE(rd.grievance_delta.has_value());
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
}

// ===========================================================================
// Authoritative aggregation (built out from the slow-convergence proxies)
// ===========================================================================

TEST_CASE("RegionalConditions: new aggregation helpers", "[regional_conditions][tier11]") {
    REQUIRE_THAT(RegionalConditionsModule::compute_population_rate(10, 100),
                 WithinAbs(0.10f, 1e-4f));
    REQUIRE_THAT(RegionalConditionsModule::compute_population_rate(5, 0), WithinAbs(0.0f, 1e-4f));
    // size-weighted employment: (1000*0.6 + 500*0.9)/1500 = 0.70
    REQUIRE_THAT(
        RegionalConditionsModule::compute_formal_employment_rate(1000 * 0.6f + 500 * 0.9f, 1500),
        WithinAbs(0.70f, 1e-4f));
    // compliance: mean of (1.0,0.8,0.6) sum=2.4 / 3 = 0.80; no facilities -> 1.0
    REQUIRE_THAT(RegionalConditionsModule::compute_regulatory_compliance(2.4f, 3),
                 WithinAbs(0.80f, 1e-4f));
    REQUIRE_THAT(RegionalConditionsModule::compute_regulatory_compliance(0.0f, 0),
                 WithinAbs(1.0f, 1e-4f));
    // EMA: 0.9*0.10 + 0.1*0.50 = 0.14
    REQUIRE_THAT(RegionalConditionsModule::compute_dominance_ema(0.10f, 0.50f, 0.1f),
                 WithinAbs(0.14f, 1e-4f));
}

namespace {
WorldState make_pop_world(uint32_t population) {
    WorldState w{};
    w.current_tick = 1;
    w.world_seed = 1;
    Province p{};
    p.id = 0;
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    p.cohort_stats->total_population = population;
    p.conditions.stability_score = 0.8f;
    w.provinces.push_back(std::move(p));
    return w;
}
NPC make_npc(uint32_t id, NPCRole role) {
    NPC n{};
    n.id = id;
    n.role = role;
    n.status = NPCStatus::active;
    n.current_province_id = 0;
    return n;
}
NPCBusiness make_biz(uint32_t id, bool criminal, float revenue, float violation) {
    NPCBusiness b{};
    b.id = id;
    b.province_id = 0;
    b.criminal_sector = criminal;
    b.revenue_per_tick = revenue;
    b.regulatory_violation_severity = violation;
    return b;
}
}  // namespace

TEST_CASE("RegionalConditions: crime_rate from criminal NPC fraction",
          "[regional_conditions][tier11]") {
    auto world = make_pop_world(100);
    for (uint32_t i = 0; i < 10; ++i)
        world.significant_npcs.push_back(make_npc(100 + i, NPCRole::criminal_operator));
    world.significant_npcs.push_back(make_npc(1, NPCRole::worker));

    RegionalConditionsModule module;
    DeltaBuffer delta{};
    module.execute_province(0, world, delta);
    apply_deltas(world, delta);
    // 10 criminals / 100 population = 0.10
    REQUIRE_THAT(world.provinces[0].cohort_stats->crime_rate, WithinAbs(0.10f, 1e-4f));
}

TEST_CASE("RegionalConditions: regulatory compliance excludes criminal facilities",
          "[regional_conditions][tier11]") {
    auto world = make_pop_world(100);
    world.npc_businesses.push_back(make_biz(1, false, 100.0f, 0.0f));  // 1.0
    world.npc_businesses.push_back(make_biz(2, false, 100.0f, 0.2f));  // 0.8
    world.npc_businesses.push_back(make_biz(3, false, 100.0f, 0.4f));  // 0.6
    world.npc_businesses.push_back(make_biz(4, true, 100.0f, 0.9f));   // criminal -> excluded

    RegionalConditionsModule module;
    DeltaBuffer delta{};
    module.execute_province(0, world, delta);
    apply_deltas(world, delta);
    // mean(1.0, 0.8, 0.6) = 0.80
    REQUIRE_THAT(world.provinces[0].conditions.regulatory_compliance_index,
                 WithinAbs(0.80f, 1e-4f));
}

TEST_CASE("RegionalConditions: criminal dominance is the EMA-smoothed revenue ratio",
          "[regional_conditions][tier11]") {
    auto world = make_pop_world(100);
    world.npc_businesses.push_back(make_biz(1, true, 200.0f, 0.0f));   // criminal
    world.npc_businesses.push_back(make_biz(2, false, 800.0f, 0.0f));  // legit
    world.provinces[0].cohort_stats->criminal_dominance_index = 0.0f;

    RegionalConditionsModule module;
    DeltaBuffer delta{};
    module.execute_province(0, world, delta);
    apply_deltas(world, delta);
    // ratio = 200/1000 = 0.20; EMA from 0.0: 0.9*0 + 0.1*0.20 = 0.02
    REQUIRE_THAT(world.provinces[0].cohort_stats->criminal_dominance_index,
                 WithinAbs(0.02f, 1e-4f));
}

TEST_CASE("RegionalConditions: inequality tracks the cohort gini",
          "[regional_conditions][tier11]") {
    auto world = make_pop_world(100);
    world.provinces[0].cohort_stats->gini_coefficient = 0.45f;
    world.provinces[0].conditions.inequality_index = 0.30f;

    RegionalConditionsModule module;
    DeltaBuffer delta{};
    module.execute_province(0, world, delta);
    apply_deltas(world, delta);
    REQUIRE_THAT(world.provinces[0].conditions.inequality_index, WithinAbs(0.45f, 1e-4f));
}

TEST_CASE("RegionalConditions: drought modifier recovers toward 1.0",
          "[regional_conditions][tier11]") {
    auto world = make_pop_world(100);
    world.provinces[0].conditions.drought_modifier = 0.50f;

    RegionalConditionsModule module;
    DeltaBuffer delta{};
    module.execute_province(0, world, delta);
    apply_deltas(world, delta);
    // recovered by drought_recovery_rate (0.005)
    REQUIRE_THAT(world.provinces[0].conditions.drought_modifier, WithinAbs(0.505f, 1e-4f));
}

TEST_CASE("RegionalConditions: zero population uses safe defaults",
          "[regional_conditions][tier11]") {
    auto world = make_pop_world(0);
    world.provinces[0].conditions.stability_score = 0.8f;
    world.provinces[0].cohort_stats->crime_rate = 0.3f;

    RegionalConditionsModule module;
    DeltaBuffer delta{};
    module.execute_province(0, world, delta);
    apply_deltas(world, delta);
    REQUIRE_THAT(world.provinces[0].conditions.stability_score, WithinAbs(0.5f, 1e-4f));  // neutral
    REQUIRE_THAT(world.provinces[0].cohort_stats->crime_rate, WithinAbs(0.0f, 1e-4f));
}
