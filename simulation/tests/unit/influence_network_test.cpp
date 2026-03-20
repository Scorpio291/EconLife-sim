#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "modules/influence_network/influence_network_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

TEST_CASE("InfluenceNetwork: trust classification threshold", "[influence_network][tier10]") {
    REQUIRE(InfluenceNetworkModule::is_trust_based(0.5f) == true);
    REQUIRE(InfluenceNetworkModule::is_trust_based(0.3f) == false);
    REQUIRE(InfluenceNetworkModule::is_trust_based(0.4f) == false);  // not strictly greater
}

TEST_CASE("InfluenceNetwork: fear classification requires low trust", "[influence_network][tier10]") {
    // fear > 0.35 AND trust < 0.20
    REQUIRE(InfluenceNetworkModule::is_fear_based(0.4f, 0.1f) == true);
    REQUIRE(InfluenceNetworkModule::is_fear_based(0.4f, 0.3f) == false);  // trust too high
    REQUIRE(InfluenceNetworkModule::is_fear_based(0.2f, 0.1f) == false);  // fear too low
}

TEST_CASE("InfluenceNetwork: classify relationship trust priority", "[influence_network][tier10]") {
    // Trust takes priority over fear
    auto type = InfluenceNetworkModule::classify_relationship(0.5f, 0.5f, true);
    REQUIRE(type == InfluenceType::trust_based);
}

TEST_CASE("InfluenceNetwork: classify fear-based", "[influence_network][tier10]") {
    auto type = InfluenceNetworkModule::classify_relationship(0.1f, 0.5f, false);
    REQUIRE(type == InfluenceType::fear_based);
}

TEST_CASE("InfluenceNetwork: classify movement-based", "[influence_network][tier10]") {
    auto type = InfluenceNetworkModule::classify_relationship(0.2f, 0.1f, true);
    REQUIRE(type == InfluenceType::movement_based);
}

TEST_CASE("InfluenceNetwork: classify obligation fallback", "[influence_network][tier10]") {
    auto type = InfluenceNetworkModule::classify_relationship(0.2f, 0.1f, false);
    REQUIRE(type == InfluenceType::obligation_based);
}

TEST_CASE("InfluenceNetwork: composite health with diversity bonus", "[influence_network][tier10]") {
    float score = InfluenceNetworkModule::compute_composite_health(5, 3, 4, 2);
    // trust: min(1, 5/10)*0.35 = 0.175
    // fear: min(1, 3/10)*0.20 = 0.06
    // obligation: min(1, 4/10)*0.25 = 0.10
    // movement: min(1, 2/10)*0.20 = 0.04
    // subtotal = 0.375 + diversity 0.05 = 0.425
    REQUIRE_THAT(score, WithinAbs(0.425f, 0.01f));
}

TEST_CASE("InfluenceNetwork: composite health no diversity bonus", "[influence_network][tier10]") {
    float score = InfluenceNetworkModule::compute_composite_health(5, 0, 4, 2);
    // trust: 0.175, fear: 0, obligation: 0.10, movement: 0.04
    // subtotal = 0.315 (no diversity bonus since fear=0)
    REQUIRE_THAT(score, WithinAbs(0.315f, 0.01f));
}

TEST_CASE("InfluenceNetwork: composite health all zero", "[influence_network][tier10]") {
    float score = InfluenceNetworkModule::compute_composite_health(0, 0, 0, 0);
    REQUIRE_THAT(score, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("InfluenceNetwork: composite health saturated counts", "[influence_network][tier10]") {
    float score = InfluenceNetworkModule::compute_composite_health(20, 20, 20, 20);
    // All components = 1.0: 0.35 + 0.25 + 0.20 + 0.20 + 0.05 = 1.05, clamped to 1.0
    REQUIRE_THAT(score, WithinAbs(1.0f, 0.01f));
}

TEST_CASE("InfluenceNetwork: obligation erosion rate", "[influence_network][tier10]") {
    float erosion = InfluenceNetworkModule::compute_obligation_erosion();
    REQUIRE_THAT(erosion, WithinAbs(-0.001f, 0.0001f));
}

TEST_CASE("InfluenceNetwork: catastrophic loss detection", "[influence_network][tier10]") {
    // Delta of -0.60, resulting trust 0.05 (below floor 0.10)
    REQUIRE(InfluenceNetworkModule::is_catastrophic_loss(-0.60f, 0.05f) == true);
    // Delta of -0.30, resulting trust 0.50 (above floor)
    REQUIRE(InfluenceNetworkModule::is_catastrophic_loss(-0.30f, 0.50f) == false);
}

TEST_CASE("InfluenceNetwork: recovery ceiling computation", "[influence_network][tier10]") {
    // 0.80 + (-0.75) = 0.05, below floor 0.10 -> catastrophic
    float ceiling = InfluenceNetworkModule::compute_recovery_ceiling(0.80f, -0.75f);
    // max(0.80 * 0.60, 0.15) = 0.48
    REQUIRE_THAT(ceiling, WithinAbs(0.48f, 0.01f));
}

TEST_CASE("InfluenceNetwork: recovery ceiling minimum enforced", "[influence_network][tier10]") {
    float ceiling = InfluenceNetworkModule::compute_recovery_ceiling(0.10f, -0.70f);
    // max(0.10 * 0.60, 0.15) = max(0.06, 0.15) = 0.15
    REQUIRE_THAT(ceiling, WithinAbs(0.15f, 0.01f));
}

TEST_CASE("InfluenceNetwork: constants match spec", "[influence_network][tier10]") {
    REQUIRE_THAT(InfluenceNetworkModule::TRUST_CLASSIFICATION_THRESHOLD, WithinAbs(0.40f, 0.001f));
    REQUIRE_THAT(InfluenceNetworkModule::FEAR_CLASSIFICATION_THRESHOLD, WithinAbs(0.35f, 0.001f));
    REQUIRE_THAT(InfluenceNetworkModule::CATASTROPHIC_TRUST_LOSS_THRESHOLD, WithinAbs(-0.55f, 0.001f));
    REQUIRE_THAT(InfluenceNetworkModule::RECOVERY_CEILING_FACTOR, WithinAbs(0.60f, 0.001f));
    REQUIRE_THAT(InfluenceNetworkModule::RECOVERY_CEILING_MINIMUM, WithinAbs(0.15f, 0.001f));
    REQUIRE(InfluenceNetworkModule::HEALTH_TARGET_COUNT == 10);
}
