#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/config/package_config.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/protection_rackets/protection_rackets_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

// ============================================================================
// Static utility tests
// ============================================================================

TEST_CASE("ProtectionRackets: demand scales with revenue", "[protection_rackets][tier8]") {
    // Business A: revenue 10000, rate 0.08 -> demand 800
    float demand_a = ProtectionRacketsModule::compute_demand_per_tick(10000.0f, 0.08f);
    REQUIRE_THAT(demand_a, WithinAbs(800.0f, 0.01f));

    // Business B: revenue 5000, rate 0.08 -> demand 400
    float demand_b = ProtectionRacketsModule::compute_demand_per_tick(5000.0f, 0.08f);
    REQUIRE_THAT(demand_b, WithinAbs(400.0f, 0.01f));
}

TEST_CASE("ProtectionRackets: demand zero for zero revenue", "[protection_rackets][tier8]") {
    float demand = ProtectionRacketsModule::compute_demand_per_tick(0.0f, 0.08f);
    REQUIRE_THAT(demand, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("ProtectionRackets: grievance accumulates per tick", "[protection_rackets][tier8]") {
    // demand 800, rate 0.00001 -> grievance 0.008 per tick
    float grievance = ProtectionRacketsModule::compute_grievance_contribution(800.0f, 0.00001f);
    REQUIRE_THAT(grievance, WithinAbs(0.008f, 0.0001f));
}

TEST_CASE("ProtectionRackets: refusal probability formula", "[protection_rackets][tier8]") {
    // defensive_incumbent, dominance 0.5, violation 0.1
    // base 0.40 + (1.0 - 0.5) * 0.30 - 0.1 * 0.10 = 0.40 + 0.15 - 0.01 = 0.54
    float prob =
        ProtectionRacketsModule::compute_refusal_probability(true, 0.5f, 0.1f, 0.40f, 0.20f);
    REQUIRE_THAT(prob, WithinAbs(0.54f, 0.01f));
}

TEST_CASE("ProtectionRackets: high dominance suppresses refusal", "[protection_rackets][tier8]") {
    // dominance 0.8 -> (1.0 - 0.8) * 0.30 = 0.06
    float prob_high_dom =
        ProtectionRacketsModule::compute_refusal_probability(false, 0.8f, 0.0f, 0.40f, 0.20f);
    // base 0.20 + 0.06 = 0.26

    float prob_low_dom =
        ProtectionRacketsModule::compute_refusal_probability(false, 0.2f, 0.0f, 0.40f, 0.20f);
    // base 0.20 + 0.24 = 0.44

    REQUIRE(prob_high_dom < prob_low_dom);
}

TEST_CASE("ProtectionRackets: defensive incumbent has higher base refusal",
          "[protection_rackets][tier8]") {
    float incumbent =
        ProtectionRacketsModule::compute_refusal_probability(true, 0.5f, 0.0f, 0.40f, 0.20f);
    float non_incumbent =
        ProtectionRacketsModule::compute_refusal_probability(false, 0.5f, 0.0f, 0.40f, 0.20f);
    REQUIRE(incumbent > non_incumbent);
}

TEST_CASE("ProtectionRackets: escalation stage thresholds", "[protection_rackets][tier8]") {
    constexpr ProtectionRacketsConfig cfg{};
    auto stage = [&](uint32_t t) {
        return ProtectionRacketsModule::determine_escalation_stage(
            t, cfg.warning_threshold, cfg.property_damage_threshold, cfg.violence_threshold,
            cfg.abandonment_threshold);
    };
    REQUIRE(stage(0) == RacketEscalationStage::demand_issued);
    REQUIRE(stage(4) == RacketEscalationStage::demand_issued);
    REQUIRE(stage(5) == RacketEscalationStage::warning);
    REQUIRE(stage(14) == RacketEscalationStage::warning);
    REQUIRE(stage(15) == RacketEscalationStage::property_damage);
    REQUIRE(stage(29) == RacketEscalationStage::property_damage);
    REQUIRE(stage(30) == RacketEscalationStage::violence);
    REQUIRE(stage(59) == RacketEscalationStage::violence);
    REQUIRE(stage(60) == RacketEscalationStage::abandonment);
    REQUIRE(stage(100) == RacketEscalationStage::abandonment);
}

TEST_CASE("ProtectionRackets: business can pay check", "[protection_rackets][tier8]") {
    REQUIRE(ProtectionRacketsModule::can_business_pay(10000.0f, 800.0f) == true);
    REQUIRE(ProtectionRacketsModule::can_business_pay(100.0f, 800.0f) == false);
    REQUIRE(ProtectionRacketsModule::can_business_pay(800.0f, 800.0f) == true);
}

TEST_CASE("ProtectionRackets: violence LE multiplier applied", "[protection_rackets][tier8]") {
    float multiplied = ProtectionRacketsModule::compute_violence_le_multiplier(0.005f, 3.0f);
    REQUIRE_THAT(multiplied, WithinAbs(0.015f, 0.001f));
}

TEST_CASE("ProtectionRackets: demand rate default is 0.08", "[protection_rackets][tier8]") {
    REQUIRE_THAT(ProtectionRacketsConfig{}.demand_rate, WithinAbs(0.08f, 0.001f));
}

TEST_CASE("ProtectionRackets: grievance per demand unit default is 0.00001",
          "[protection_rackets][tier8]") {
    REQUIRE_THAT(ProtectionRacketsConfig{}.grievance_per_demand_unit,
                 WithinAbs(0.00001f, 0.000001f));
}

TEST_CASE("ProtectionRackets: refusal probability clamped to [0,1]",
          "[protection_rackets][tier8]") {
    // Extreme values
    float prob =
        ProtectionRacketsModule::compute_refusal_probability(true, 0.0f, 0.0f, 0.40f, 0.20f);
    // 0.40 + 0.30 = 0.70
    REQUIRE(prob <= 1.0f);
    REQUIRE(prob >= 0.0f);
}

TEST_CASE("ProtectionRackets: violation severity reduces refusal", "[protection_rackets][tier8]") {
    float with_violation =
        ProtectionRacketsModule::compute_refusal_probability(false, 0.5f, 0.5f, 0.40f, 0.20f);
    float without_violation =
        ProtectionRacketsModule::compute_refusal_probability(false, 0.5f, 0.0f, 0.40f, 0.20f);
    REQUIRE(with_violation < without_violation);
}

TEST_CASE("ProtectionRackets: property damage severity default is 0.4",
          "[protection_rackets][tier8]") {
    REQUIRE_THAT(ProtectionRacketsConfig{}.property_damage_severity, WithinAbs(0.4f, 0.001f));
}

TEST_CASE("ProtectionRackets: warning memory weight default is -0.5",
          "[protection_rackets][tier8]") {
    REQUIRE_THAT(ProtectionRacketsConfig{}.memory_emotional_weight_warning,
                 WithinAbs(-0.5f, 0.001f));
}
