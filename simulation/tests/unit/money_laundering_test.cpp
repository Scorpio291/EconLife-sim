#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/config/package_config.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/money_laundering/money_laundering_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

// ============================================================================
// Static utility tests
// ============================================================================

TEST_CASE("MoneyLaundering: compute_transfer_this_tick basic", "[money_laundering][tier8]") {
    float transfer = MoneyLaunderingModule::compute_transfer_this_tick(500.0f, 10000.0f, 0.0f);
    REQUIRE_THAT(transfer, WithinAbs(500.0f, 0.01f));
}

TEST_CASE("MoneyLaundering: transfer capped by remaining dirty amount",
          "[money_laundering][tier8]") {
    // Only 200 remaining, rate is 500
    float transfer = MoneyLaunderingModule::compute_transfer_this_tick(500.0f, 10000.0f, 9800.0f);
    REQUIRE_THAT(transfer, WithinAbs(200.0f, 0.01f));
}

TEST_CASE("MoneyLaundering: transfer zero when fully laundered", "[money_laundering][tier8]") {
    float transfer = MoneyLaunderingModule::compute_transfer_this_tick(500.0f, 10000.0f, 10000.0f);
    REQUIRE_THAT(transfer, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("MoneyLaundering: conversion_loss_deducted", "[money_laundering][tier8]") {
    // Transfer 1000, 5% loss -> receive 950
    float clean = MoneyLaunderingModule::compute_clean_amount(1000.0f, 0.05f);
    REQUIRE_THAT(clean, WithinAbs(950.0f, 0.01f));
}

TEST_CASE("MoneyLaundering: structuring evidence at interval", "[money_laundering][tier8]") {
    // Every 7 ticks
    REQUIRE(MoneyLaunderingModule::should_generate_structuring_evidence(107, 100, 7) == true);
    REQUIRE(MoneyLaunderingModule::should_generate_structuring_evidence(105, 100, 7) == false);
    REQUIRE(MoneyLaunderingModule::should_generate_structuring_evidence(114, 100, 7) == true);
}

TEST_CASE("MoneyLaundering: shell chain evidence at interval", "[money_laundering][tier8]") {
    REQUIRE(MoneyLaunderingModule::should_generate_shell_chain_evidence(130, 100, 30) == true);
    REQUIRE(MoneyLaunderingModule::should_generate_shell_chain_evidence(115, 100, 30) == false);
}

TEST_CASE("MoneyLaundering: crypto evidence scales with investigator skill",
          "[money_laundering][tier8]") {
    // Low skill
    float prob_low =
        MoneyLaunderingModule::compute_crypto_evidence_probability(500.0f, 0.5f, 2.0f, 10.0f);
    // High skill
    float prob_high =
        MoneyLaunderingModule::compute_crypto_evidence_probability(500.0f, 0.5f, 8.0f, 10.0f);
    // High skill should be 4x the low skill probability
    REQUIRE_THAT(prob_high, WithinAbs(prob_low * 4.0f, 0.01f));
}

TEST_CASE("MoneyLaundering: commingling capped by business revenue", "[money_laundering][tier8]") {
    // revenue 5000, fraction 0.40, max 5000
    float cap = MoneyLaunderingModule::compute_commingling_capacity(5000.0f, 0.40f, 5000.0f);
    REQUIRE_THAT(cap, WithinAbs(2000.0f, 0.01f));
}

TEST_CASE("MoneyLaundering: commingling capped by absolute max", "[money_laundering][tier8]") {
    // revenue 50000, fraction 0.40 = 20000, but max is 5000
    float cap = MoneyLaunderingModule::compute_commingling_capacity(50000.0f, 0.40f, 5000.0f);
    REQUIRE_THAT(cap, WithinAbs(5000.0f, 0.01f));
}

TEST_CASE("MoneyLaundering: FIU detects structuring pattern", "[money_laundering][tier8]") {
    // 10 deposits, threshold 8 -> suspicion > 0
    float suspicion = MoneyLaunderingModule::compute_fiu_structuring_suspicion(10, 8);
    REQUIRE(suspicion > 0.35f);  // above FIU_TOKEN_THRESHOLD
}

TEST_CASE("MoneyLaundering: FIU no detection below threshold", "[money_laundering][tier8]") {
    float suspicion = MoneyLaunderingModule::compute_fiu_structuring_suspicion(5, 8);
    REQUIRE_THAT(suspicion, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("MoneyLaundering: operation completes when fully laundered",
          "[money_laundering][tier8]") {
    REQUIRE(MoneyLaunderingModule::is_operation_completed(5000.0f, 5000.0f) == true);
    REQUIRE(MoneyLaunderingModule::is_operation_completed(4999.0f, 5000.0f) == false);
    REQUIRE(MoneyLaunderingModule::is_operation_completed(5001.0f, 5000.0f) == true);
}

TEST_CASE("MoneyLaundering: structuring transfers and generates evidence",
          "[money_laundering][tier8]") {
    // Run 7 ticks of structuring: dirty 10000, rate 500/tick
    // After 7 ticks: laundered_so_far = 3500, 1 evidence token at interval 7
    float laundered = 0.0f;
    int evidence_count = 0;
    for (uint32_t t = 1; t <= 7; ++t) {
        float transfer =
            MoneyLaunderingModule::compute_transfer_this_tick(500.0f, 10000.0f, laundered);
        laundered += transfer;
        if (MoneyLaunderingModule::should_generate_structuring_evidence(t, 0, 7)) {
            evidence_count++;
        }
    }
    REQUIRE_THAT(laundered, WithinAbs(3500.0f, 0.01f));
    REQUIRE(evidence_count == 1);
}

TEST_CASE("MoneyLaundering: zero conversion loss passes through full amount",
          "[money_laundering][tier8]") {
    float clean = MoneyLaunderingModule::compute_clean_amount(1000.0f, 0.0f);
    REQUIRE_THAT(clean, WithinAbs(1000.0f, 0.01f));
}

TEST_CASE("MoneyLaundering: NPC org capacity limit", "[money_laundering][tier8]") {
    // NPC org with cash 100000, capacity multiplier 0.25, ticks per quarter 90
    // Max rate = 0.25 * 100000 / 90 = 277.78
    MoneyLaunderingConfig default_cfg{};
    float max_rate = default_cfg.org_capacity_multiplier * 100000.0f /
                     static_cast<float>(default_cfg.ticks_per_quarter);
    REQUIRE_THAT(max_rate, WithinAbs(277.78f, 0.1f));
}
