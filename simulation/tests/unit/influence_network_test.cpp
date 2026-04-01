#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/config/package_config.h"
#include "modules/influence_network/influence_network_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

namespace {
const InfluenceNetworkConfig default_cfg{};
}

TEST_CASE("InfluenceNetwork: trust classification threshold", "[influence_network][tier10]") {
    REQUIRE(InfluenceNetworkModule::is_trust_based(0.5f, default_cfg.trust_classification_threshold) == true);
    REQUIRE(InfluenceNetworkModule::is_trust_based(0.3f, default_cfg.trust_classification_threshold) == false);
    REQUIRE(InfluenceNetworkModule::is_trust_based(0.4f, default_cfg.trust_classification_threshold) == false);  // not strictly greater
}

TEST_CASE("InfluenceNetwork: fear classification requires low trust",
          "[influence_network][tier10]") {
    // fear > 0.35 AND trust < 0.20
    REQUIRE(InfluenceNetworkModule::is_fear_based(0.4f, 0.1f, default_cfg.fear_classification_threshold, default_cfg.fear_trust_ceiling) == true);
    REQUIRE(InfluenceNetworkModule::is_fear_based(0.4f, 0.3f, default_cfg.fear_classification_threshold, default_cfg.fear_trust_ceiling) == false);  // trust too high
    REQUIRE(InfluenceNetworkModule::is_fear_based(0.2f, 0.1f, default_cfg.fear_classification_threshold, default_cfg.fear_trust_ceiling) == false);  // fear too low
}

TEST_CASE("InfluenceNetwork: classify relationship trust priority", "[influence_network][tier10]") {
    // Trust takes priority over fear
    auto type = InfluenceNetworkModule::classify_relationship(
        0.5f, 0.5f, true, default_cfg.trust_classification_threshold,
        default_cfg.fear_classification_threshold, default_cfg.fear_trust_ceiling);
    REQUIRE(type == InfluenceType::trust_based);
}

TEST_CASE("InfluenceNetwork: classify fear-based", "[influence_network][tier10]") {
    auto type = InfluenceNetworkModule::classify_relationship(
        0.1f, 0.5f, false, default_cfg.trust_classification_threshold,
        default_cfg.fear_classification_threshold, default_cfg.fear_trust_ceiling);
    REQUIRE(type == InfluenceType::fear_based);
}

TEST_CASE("InfluenceNetwork: classify movement-based", "[influence_network][tier10]") {
    auto type = InfluenceNetworkModule::classify_relationship(
        0.2f, 0.1f, true, default_cfg.trust_classification_threshold,
        default_cfg.fear_classification_threshold, default_cfg.fear_trust_ceiling);
    REQUIRE(type == InfluenceType::movement_based);
}

TEST_CASE("InfluenceNetwork: classify obligation fallback", "[influence_network][tier10]") {
    auto type = InfluenceNetworkModule::classify_relationship(
        0.2f, 0.1f, false, default_cfg.trust_classification_threshold,
        default_cfg.fear_classification_threshold, default_cfg.fear_trust_ceiling);
    REQUIRE(type == InfluenceType::obligation_based);
}

TEST_CASE("InfluenceNetwork: composite health with diversity bonus",
          "[influence_network][tier10]") {
    float score = InfluenceNetworkModule::compute_composite_health(
        5, 3, 4, 2, default_cfg.health_target_count, default_cfg.trust_weight,
        default_cfg.obligation_weight, default_cfg.fear_weight, default_cfg.movement_weight,
        default_cfg.diversity_bonus);
    // trust: min(1, 5/10)*0.35 = 0.175
    // fear: min(1, 3/10)*0.20 = 0.06
    // obligation: min(1, 4/10)*0.25 = 0.10
    // movement: min(1, 2/10)*0.20 = 0.04
    // subtotal = 0.375 + diversity 0.05 = 0.425
    REQUIRE_THAT(score, WithinAbs(0.425f, 0.01f));
}

TEST_CASE("InfluenceNetwork: composite health no diversity bonus", "[influence_network][tier10]") {
    float score = InfluenceNetworkModule::compute_composite_health(
        5, 0, 4, 2, default_cfg.health_target_count, default_cfg.trust_weight,
        default_cfg.obligation_weight, default_cfg.fear_weight, default_cfg.movement_weight,
        default_cfg.diversity_bonus);
    // trust: 0.175, fear: 0, obligation: 0.10, movement: 0.04
    // subtotal = 0.315 (no diversity bonus since fear=0)
    REQUIRE_THAT(score, WithinAbs(0.315f, 0.01f));
}

TEST_CASE("InfluenceNetwork: composite health all zero", "[influence_network][tier10]") {
    float score = InfluenceNetworkModule::compute_composite_health(
        0, 0, 0, 0, default_cfg.health_target_count, default_cfg.trust_weight,
        default_cfg.obligation_weight, default_cfg.fear_weight, default_cfg.movement_weight,
        default_cfg.diversity_bonus);
    REQUIRE_THAT(score, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("InfluenceNetwork: composite health saturated counts", "[influence_network][tier10]") {
    float score = InfluenceNetworkModule::compute_composite_health(
        20, 20, 20, 20, default_cfg.health_target_count, default_cfg.trust_weight,
        default_cfg.obligation_weight, default_cfg.fear_weight, default_cfg.movement_weight,
        default_cfg.diversity_bonus);
    // All components = 1.0: 0.35 + 0.25 + 0.20 + 0.20 + 0.05 = 1.05, clamped to 1.0
    REQUIRE_THAT(score, WithinAbs(1.0f, 0.01f));
}

TEST_CASE("InfluenceNetwork: obligation erosion rate", "[influence_network][tier10]") {
    float erosion = InfluenceNetworkModule::compute_obligation_erosion(
        default_cfg.obligation_erosion_rate);
    REQUIRE_THAT(erosion, WithinAbs(-0.001f, 0.0001f));
}

TEST_CASE("InfluenceNetwork: catastrophic loss detection", "[influence_network][tier10]") {
    // Delta of -0.60, resulting trust 0.05 (below floor 0.10)
    REQUIRE(InfluenceNetworkModule::is_catastrophic_loss(
                -0.60f, 0.05f, default_cfg.catastrophic_trust_loss_threshold,
                default_cfg.catastrophic_trust_floor) == true);
    // Delta of -0.30, resulting trust 0.50 (above floor)
    REQUIRE(InfluenceNetworkModule::is_catastrophic_loss(
                -0.30f, 0.50f, default_cfg.catastrophic_trust_loss_threshold,
                default_cfg.catastrophic_trust_floor) == false);
}

TEST_CASE("InfluenceNetwork: recovery ceiling computation", "[influence_network][tier10]") {
    // 0.80 + (-0.75) = 0.05, below floor 0.10 -> catastrophic
    float ceiling = InfluenceNetworkModule::compute_recovery_ceiling(
        0.80f, -0.75f, default_cfg.catastrophic_trust_loss_threshold,
        default_cfg.catastrophic_trust_floor, default_cfg.recovery_ceiling_factor,
        default_cfg.recovery_ceiling_minimum);
    // max(0.80 * 0.60, 0.15) = 0.48
    REQUIRE_THAT(ceiling, WithinAbs(0.48f, 0.01f));
}

TEST_CASE("InfluenceNetwork: recovery ceiling minimum enforced", "[influence_network][tier10]") {
    float ceiling = InfluenceNetworkModule::compute_recovery_ceiling(
        0.10f, -0.70f, default_cfg.catastrophic_trust_loss_threshold,
        default_cfg.catastrophic_trust_floor, default_cfg.recovery_ceiling_factor,
        default_cfg.recovery_ceiling_minimum);
    // max(0.10 * 0.60, 0.15) = max(0.06, 0.15) = 0.15
    REQUIRE_THAT(ceiling, WithinAbs(0.15f, 0.01f));
}

TEST_CASE("InfluenceNetwork: config defaults match spec", "[influence_network][tier10]") {
    REQUIRE_THAT(default_cfg.trust_classification_threshold, WithinAbs(0.40f, 0.001f));
    REQUIRE_THAT(default_cfg.fear_classification_threshold, WithinAbs(0.35f, 0.001f));
    REQUIRE_THAT(default_cfg.catastrophic_trust_loss_threshold, WithinAbs(-0.55f, 0.001f));
    REQUIRE_THAT(default_cfg.recovery_ceiling_factor, WithinAbs(0.60f, 0.001f));
    REQUIRE_THAT(default_cfg.recovery_ceiling_minimum, WithinAbs(0.15f, 0.001f));
    REQUIRE(default_cfg.health_target_count == 10);
}
