#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/alternative_identity/alternative_identity_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

TEST_CASE("AltIdentity: documentation quality decays", "[alternative_identity][tier9]") {
    float decayed = AlternativeIdentityModule::decay_documentation_quality(0.50f, 0.001f);
    REQUIRE_THAT(decayed, WithinAbs(0.499f, 0.001f));
}

TEST_CASE("AltIdentity: quality does not go below zero", "[alternative_identity][tier9]") {
    float decayed = AlternativeIdentityModule::decay_documentation_quality(0.0005f, 0.001f);
    REQUIRE_THAT(decayed, WithinAbs(0.0f, 0.001f));
}

TEST_CASE("AltIdentity: documentation quality builds", "[alternative_identity][tier9]") {
    float built = AlternativeIdentityModule::build_documentation_quality(0.50f, 0.005f);
    REQUIRE_THAT(built, WithinAbs(0.505f, 0.001f));
}

TEST_CASE("AltIdentity: quality does not exceed 1.0", "[alternative_identity][tier9]") {
    float built = AlternativeIdentityModule::build_documentation_quality(0.998f, 0.005f);
    REQUIRE_THAT(built, WithinAbs(1.0f, 0.001f));
}

TEST_CASE("AltIdentity: burn triggered below threshold", "[alternative_identity][tier9]") {
    REQUIRE(AlternativeIdentityModule::should_burn_identity(0.05f, 0.10f) == true);
    REQUIRE(AlternativeIdentityModule::should_burn_identity(0.15f, 0.10f) == false);
    REQUIRE(AlternativeIdentityModule::should_burn_identity(0.10f, 0.10f) == false);
}

TEST_CASE("AltIdentity: witness discovery confidence", "[alternative_identity][tier9]") {
    REQUIRE_THAT(AlternativeIdentityModule::compute_witness_discovery_confidence(),
                 WithinAbs(0.70f, 0.01f));
}

TEST_CASE("AltIdentity: forensic discovery confidence", "[alternative_identity][tier9]") {
    REQUIRE_THAT(AlternativeIdentityModule::compute_forensic_discovery_confidence(),
                 WithinAbs(0.55f, 0.01f));
}

TEST_CASE("AltIdentity: decay rate constant is 0.001", "[alternative_identity][tier9]") {
    REQUIRE_THAT(AlternativeIdentityModule::DOCUMENTATION_DECAY_RATE, WithinAbs(0.001f, 0.0001f));
}

TEST_CASE("AltIdentity: build rate constant is 0.005", "[alternative_identity][tier9]") {
    REQUIRE_THAT(AlternativeIdentityModule::DOCUMENTATION_BUILD_RATE, WithinAbs(0.005f, 0.0001f));
}

TEST_CASE("AltIdentity: burn threshold constant is 0.10", "[alternative_identity][tier9]") {
    REQUIRE_THAT(AlternativeIdentityModule::BURN_THRESHOLD, WithinAbs(0.10f, 0.001f));
}

TEST_CASE("AltIdentity: multiple ticks of decay", "[alternative_identity][tier9]") {
    float quality = 0.50f;
    for (int i = 0; i < 100; ++i) {
        quality = AlternativeIdentityModule::decay_documentation_quality(quality, 0.001f);
    }
    REQUIRE_THAT(quality, WithinAbs(0.40f, 0.001f));
}

TEST_CASE("AltIdentity: identity status enum values", "[alternative_identity][tier9]") {
    REQUIRE(static_cast<uint8_t>(IdentityStatus::active) == 0);
    REQUIRE(static_cast<uint8_t>(IdentityStatus::dormant) == 1);
    REQUIRE(static_cast<uint8_t>(IdentityStatus::burned) == 2);
    REQUIRE(static_cast<uint8_t>(IdentityStatus::retired) == 3);
}
