#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/config/package_config.h"
#include "core/world_state/apply_deltas.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/criminal_operations/criminal_operations_module.h"
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

// ============================================================================
// Laundering seeding (criminal_operations -> money_laundering, same tick)
// ============================================================================

TEST_CASE("MoneyLaundering: execute drains a seed into a live operation",
          "[money_laundering][tier8]") {
    WorldState state{};
    state.current_tick = 40;
    LaunderingSeedDelta seed{};
    seed.actor_id = 12;
    seed.dirty_amount = 3000.0f;
    seed.destination_business_id = 5;
    state.pending_laundering_seeds.push_back(seed);

    MoneyLaunderingModule module;
    DeltaBuffer delta{};
    module.execute(state, delta);

    REQUIRE(module.operations().size() == 1);
    const LaunderingOperation& op = module.operations()[0];
    REQUIRE(op.actor_id == 12);
    REQUIRE(op.destination_business_id == 5);
    REQUIRE_THAT(op.dirty_amount, WithinAbs(3000.0f, 1e-2f));
    // Rate is an absolute currency/tick amount: the batch washes over one
    // quarter, so rate = dirty_amount / ticks_per_quarter = 3000 / 90.
    REQUIRE_THAT(op.launder_rate_per_tick, WithinAbs(3000.0f / 90.0f, 1e-2f));
    REQUIRE(op.laundered_so_far > 0.0f);  // first tick already washed some
    REQUIRE(state.pending_laundering_seeds.empty());

    // Idempotent: re-seeding the same actor does not duplicate.
    state.pending_laundering_seeds.push_back(seed);
    DeltaBuffer delta2{};
    module.execute(state, delta2);
    REQUIRE(module.operations().size() == 1);
}

TEST_CASE("MoneyLaundering: end-to-end seed from criminal_operations becomes an operation",
          "[money_laundering][tier8]") {
    WorldState state{};
    state.current_tick = 0;
    Province prov{};
    prov.id = 0;
    prov.cohort_stats = std::make_unique<RegionCohortStats>();
    state.provinces.push_back(std::move(prov));

    NPC op{};
    op.id = 12;
    op.role = NPCRole::criminal_operator;
    op.status = NPCStatus::active;
    op.current_province_id = 0;
    state.significant_npcs.push_back(op);

    NPCBusiness front{};
    front.id = 5;
    front.province_id = 0;
    front.criminal_sector = true;
    front.revenue_per_tick = 100.0f;
    state.npc_businesses.push_back(front);

    CriminalOperationsModule crimops;
    DeltaBuffer delta{};
    crimops.execute(state, delta);
    apply_deltas(state, delta);
    REQUIRE(state.pending_laundering_seeds.size() == 1);

    MoneyLaunderingModule launder;
    DeltaBuffer delta2{};
    launder.execute(state, delta2);
    REQUIRE(launder.operations().size() == 1);
    REQUIRE(launder.operations()[0].actor_id == 12);
    REQUIRE(state.pending_laundering_seeds.empty());
}

// =============================================================================
// crypto / commingling method wiring (helpers were previously unused)
// =============================================================================

TEST_CASE("MoneyLaundering: commingling caps transfer to business cash flow",
          "[money_laundering][tier8]") {
    WorldState state{};
    state.current_tick = 5;
    state.world_seed = 1;
    NPCBusiness front{};
    front.id = 5;
    front.province_id = 0;
    front.revenue_per_tick = 100.0f;
    front.criminal_sector = true;
    state.npc_businesses.push_back(front);

    MoneyLaunderingModule module;
    LaunderingOperation op{};
    op.id = 1;
    op.actor_id = 1;
    op.method = LaunderingMethod::cash_commingling;
    op.dirty_amount = 100000.0f;
    op.launder_rate_per_tick = 1000.0f;  // would move 1000/tick uncapped
    op.conversion_loss_rate = 0.0f;
    op.destination_business_id = 5;
    op.started_tick = 0;
    module.operations_mut().push_back(op);

    DeltaBuffer d{};
    module.execute(state, d);
    // capacity = revenue(100) * commingle_capacity_fraction(0.40) = 40 (< rate_commingle_max).
    REQUIRE_THAT(module.operations()[0].laundered_so_far, WithinAbs(40.0f, 1e-3f));
}

TEST_CASE("MoneyLaundering: crypto-mixing generates probabilistic digital evidence",
          "[money_laundering][tier8]") {
    WorldState state{};
    state.current_tick = 5;
    state.world_seed = 1;

    MoneyLaunderingModule module;
    LaunderingOperation op{};
    op.id = 2;
    op.actor_id = 1;
    op.method = LaunderingMethod::crypto_mixing;
    op.dirty_amount = 100000.0f;
    op.launder_rate_per_tick = 1000.0f;  // high rate -> high crypto exposure prob
    op.conversion_loss_rate = 0.0f;
    op.destination_business_id = 0;  // direct to player wealth
    op.started_tick = 0;
    module.operations_mut().push_back(op);

    DeltaBuffer d{};
    module.execute(state, d);
    bool digital = false;
    for (const auto& ev : d.evidence_deltas)
        if (ev.new_token.has_value() && ev.new_token->type == EvidenceType::digital)
            digital = true;
    REQUIRE(digital);  // prob = 1000*0.5*0.5/10 = 25 (>=1) -> fires
}
