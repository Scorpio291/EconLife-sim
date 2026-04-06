#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "modules/trust_updates/trust_updates_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

TEST_CASE("TrustUpdates: positive delta applied", "[trust_updates][tier10]") {
    float result = TrustUpdatesModule::apply_trust_delta(0.50f, 0.10f, 1.0f);
    REQUIRE_THAT(result, WithinAbs(0.60f, 0.01f));
}

TEST_CASE("TrustUpdates: negative delta applied", "[trust_updates][tier10]") {
    float result = TrustUpdatesModule::apply_trust_delta(0.50f, -0.20f, 1.0f);
    REQUIRE_THAT(result, WithinAbs(0.30f, 0.01f));
}

TEST_CASE("TrustUpdates: trust clamped to upper bound", "[trust_updates][tier10]") {
    float result = TrustUpdatesModule::apply_trust_delta(0.80f, 0.60f, 1.0f);
    REQUIRE_THAT(result, WithinAbs(1.0f, 0.01f));
}

TEST_CASE("TrustUpdates: trust clamped to lower bound", "[trust_updates][tier10]") {
    float result = TrustUpdatesModule::apply_trust_delta(-0.50f, -0.80f, 1.0f);
    REQUIRE_THAT(result, WithinAbs(-1.0f, 0.01f));
}

TEST_CASE("TrustUpdates: trust gain capped by recovery ceiling", "[trust_updates][tier10]") {
    float result = TrustUpdatesModule::apply_trust_delta(0.30f, 0.30f, 0.48f);
    REQUIRE_THAT(result, WithinAbs(0.48f, 0.01f));
}

TEST_CASE("TrustUpdates: default ceiling allows full recovery", "[trust_updates][tier10]") {
    float result = TrustUpdatesModule::apply_trust_delta(0.70f, 0.20f, 1.0f);
    REQUIRE_THAT(result, WithinAbs(0.90f, 0.01f));
}

TEST_CASE("TrustUpdates: catastrophic loss sets ceiling", "[trust_updates][tier10]") {
    // Delta = -0.70, result = 0.10 (at floor)
    REQUIRE(TrustUpdatesModule::is_catastrophic_loss(-0.70f, 0.10f) == true);
    float ceiling = TrustUpdatesModule::compute_recovery_ceiling(0.80f);
    // max(0.80 * 0.60, 0.15) = 0.48
    REQUIRE_THAT(ceiling, WithinAbs(0.48f, 0.01f));
}

TEST_CASE("TrustUpdates: second catastrophic loss lowers ceiling", "[trust_updates][tier10]") {
    float existing = 0.48f;
    float new_ceiling = TrustUpdatesModule::compute_recovery_ceiling(0.40f);
    // max(0.40 * 0.60, 0.15) = 0.24
    float final_ceiling = TrustUpdatesModule::update_recovery_ceiling(existing, new_ceiling);
    // min(0.48, 0.24) = 0.24
    REQUIRE_THAT(final_ceiling, WithinAbs(0.24f, 0.01f));
}

TEST_CASE("TrustUpdates: recovery ceiling minimum enforced", "[trust_updates][tier10]") {
    float ceiling = TrustUpdatesModule::compute_recovery_ceiling(0.10f);
    // max(0.10 * 0.60, 0.15) = max(0.06, 0.15) = 0.15
    REQUIRE_THAT(ceiling, WithinAbs(0.15f, 0.01f));
}

TEST_CASE("TrustUpdates: significant change detection", "[trust_updates][tier10]") {
    REQUIRE(TrustUpdatesModule::is_significant_change(0.15f) == true);
    REQUIRE(TrustUpdatesModule::is_significant_change(-0.12f) == true);
    REQUIRE(TrustUpdatesModule::is_significant_change(0.05f) == false);
    REQUIRE(TrustUpdatesModule::is_significant_change(-0.08f) == false);
}

TEST_CASE("TrustUpdates: non-catastrophic loss no ceiling change", "[trust_updates][tier10]") {
    // Delta = -0.30 (above -0.55 threshold), result = 0.50 (above floor)
    REQUIRE(TrustUpdatesModule::is_catastrophic_loss(-0.30f, 0.50f) == false);
}

TEST_CASE("TrustUpdates: negative delta does not trigger ceiling cap", "[trust_updates][tier10]") {
    // Recovery ceiling only caps gains, not losses
    float result = TrustUpdatesModule::apply_trust_delta(0.40f, -0.30f, 0.48f);
    REQUIRE_THAT(result, WithinAbs(0.10f, 0.01f));
}

TEST_CASE("TrustUpdates: config defaults match spec", "[trust_updates][tier10]") {
    constexpr TrustUpdatesConfig cfg{};
    REQUIRE_THAT(cfg.catastrophic_trust_loss_threshold, WithinAbs(-0.55f, 0.001f));
    REQUIRE_THAT(cfg.catastrophic_trust_floor, WithinAbs(0.10f, 0.001f));
    REQUIRE_THAT(cfg.recovery_ceiling_factor, WithinAbs(0.60f, 0.001f));
    REQUIRE_THAT(cfg.recovery_ceiling_minimum, WithinAbs(0.15f, 0.001f));
    REQUIRE_THAT(cfg.significant_change_threshold, WithinAbs(0.10f, 0.001f));
}
