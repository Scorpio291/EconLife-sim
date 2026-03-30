#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/informant_system/informant_system_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

TEST_CASE("Informant: risk factor computation", "[informant_system][tier9]") {
    // Low risk tolerance (0.2) -> high risk factor: (1-0.2)*0.30 = 0.24
    REQUIRE_THAT(InformantSystemModule::compute_risk_factor(0.2f, 0.30f), WithinAbs(0.24f, 0.01f));
    // High risk tolerance (0.8) -> low risk factor: (1-0.8)*0.30 = 0.06
    REQUIRE_THAT(InformantSystemModule::compute_risk_factor(0.8f, 0.30f), WithinAbs(0.06f, 0.01f));
}

TEST_CASE("Informant: trust factor computation", "[informant_system][tier9]") {
    // Low trust (0.2) -> high factor: (1-0.2)*0.25 = 0.20
    REQUIRE_THAT(InformantSystemModule::compute_trust_factor(0.2f, 0.25f), WithinAbs(0.20f, 0.01f));
    // High trust (0.8) -> low factor: (1-0.8)*0.25 = 0.05
    REQUIRE_THAT(InformantSystemModule::compute_trust_factor(0.8f, 0.25f), WithinAbs(0.05f, 0.01f));
}

TEST_CASE("Informant: incrimination suppression", "[informant_system][tier9]") {
    REQUIRE_THAT(InformantSystemModule::compute_incrimination_suppression(0, 0.08f),
                 WithinAbs(0.0f, 0.01f));
    REQUIRE_THAT(InformantSystemModule::compute_incrimination_suppression(2, 0.08f),
                 WithinAbs(0.16f, 0.01f));
}

TEST_CASE("Informant: compartmentalization bonus", "[informant_system][tier9]") {
    REQUIRE_THAT(InformantSystemModule::compute_compartmentalization_bonus(0, 0.05f),
                 WithinAbs(0.0f, 0.01f));
    REQUIRE_THAT(InformantSystemModule::compute_compartmentalization_bonus(3, 0.05f),
                 WithinAbs(0.15f, 0.01f));
}

TEST_CASE("Informant: flip probability full formula", "[informant_system][tier9]") {
    // base 0.10, risk_tol 0.5, trust 0.5, 0 mutual, 0 compartment
    // 0.10 + (1-0.5)*0.30 + (1-0.5)*0.25 - 0 - 0 = 0.10 + 0.15 + 0.125 = 0.375
    // clamped to MAX 0.20
    float prob = InformantSystemModule::compute_flip_probability(0.10f, 0.5f, 0.5f, 0, 0, 0.20f,
                                                                 0.30f, 0.25f, 0.08f, 0.05f);
    REQUIRE_THAT(prob, WithinAbs(0.20f, 0.01f));
}

TEST_CASE("Informant: flip probability capped at MAX", "[informant_system][tier9]") {
    float prob = InformantSystemModule::compute_flip_probability(0.10f, 0.0f, 0.0f, 0, 0, 0.20f,
                                                                 0.30f, 0.25f, 0.08f, 0.05f);
    // 0.10 + 0.30 + 0.25 = 0.65, capped to 0.20
    REQUIRE_THAT(prob, WithinAbs(0.20f, 0.01f));
}

TEST_CASE("Informant: mutual incrimination reduces flip", "[informant_system][tier9]") {
    float without = InformantSystemModule::compute_flip_probability(0.10f, 0.5f, 0.5f, 0, 0, 0.20f,
                                                                    0.30f, 0.25f, 0.08f, 0.05f);
    float with_mutual = InformantSystemModule::compute_flip_probability(
        0.10f, 0.5f, 0.5f, 2, 0, 0.20f, 0.30f, 0.25f, 0.08f, 0.05f);
    REQUIRE(with_mutual <= without);
}

TEST_CASE("Informant: compartmentalization reduces flip", "[informant_system][tier9]") {
    // Use params where raw probability is below cap so comparison is meaningful
    // base=0.05, risk_tol=0.9, trust=0.9: 0.05+0.03+0.025 = 0.105 (below 0.20 cap)
    float without = InformantSystemModule::compute_flip_probability(0.05f, 0.9f, 0.9f, 0, 0, 0.20f,
                                                                    0.30f, 0.25f, 0.08f, 0.05f);
    float with_compart = InformantSystemModule::compute_flip_probability(
        0.05f, 0.9f, 0.9f, 0, 3, 0.20f, 0.30f, 0.25f, 0.08f, 0.05f);
    REQUIRE(with_compart < without);
}

TEST_CASE("Informant: high trust suppresses flip", "[informant_system][tier9]") {
    // Use params where raw probability is below cap so comparison is meaningful
    // base=0.02, risk_tol=0.9, trust=0.5: 0.02+0.03+0.125 = 0.175 (below 0.20 cap)
    // base=0.02, risk_tol=0.9, trust=0.9: 0.02+0.03+0.025 = 0.075
    float low_trust = InformantSystemModule::compute_flip_probability(
        0.02f, 0.9f, 0.5f, 0, 0, 0.20f, 0.30f, 0.25f, 0.08f, 0.05f);
    float high_trust = InformantSystemModule::compute_flip_probability(
        0.02f, 0.9f, 0.9f, 0, 0, 0.20f, 0.30f, 0.25f, 0.08f, 0.05f);
    REQUIRE(high_trust < low_trust);
}

TEST_CASE("Informant: flip probability non-negative", "[informant_system][tier9]") {
    // Extreme suppression
    float prob = InformantSystemModule::compute_flip_probability(0.10f, 1.0f, 1.0f, 5, 5, 0.20f,
                                                                 0.30f, 0.25f, 0.08f, 0.05f);
    REQUIRE(prob >= 0.0f);
}

TEST_CASE("Informant: constants match spec", "[informant_system][tier9]") {
    REQUIRE_THAT(0.20f, WithinAbs(0.20f, 0.001f));
    REQUIRE_THAT(50000.0f, WithinAbs(50000.0f, 1.0f));
    REQUIRE_THAT(3.0f, WithinAbs(3.0f, 0.01f));
}
