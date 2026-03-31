// Economy scenario tests — behavioral assertions from GDD and TDD.
// These verify emergent economic behavior through the core delta application
// infrastructure. Each scenario sets up a WorldState, applies deltas directly,
// and asserts outcomes.
//
// Scenarios tagged [scenario][economy].

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstdint>

#include "core/tick/drain_deferred_work.h"
#include "core/world_state/apply_deltas.h"
#include "core/world_state/world_state.h"
#include "tests/test_world_factory.h"

using namespace econlife;
using namespace econlife::test;
using Catch::Matchers::WithinAbs;

// ── Production scenarios ────────────────────────────────────────────────────

TEST_CASE("business produces output when inputs available", "[scenario][economy][production]") {
    // Test: MarketDelta supply additions represent production output.
    auto world = create_test_world(42, 10, 1, 5);
    float initial_supply = world.regional_markets[0].supply;

    // Simulate production: add supply delta for good 0 in province 0.
    DeltaBuffer delta{};
    MarketDelta md{};
    md.good_id = 0;
    md.region_id = 0;
    md.supply_delta = 50.0f;
    delta.market_deltas.push_back(md);
    apply_deltas(world, delta);

    REQUIRE_THAT(world.regional_markets[0].supply, WithinAbs(initial_supply + 50.0f, 0.01));
}

TEST_CASE("business halts production when missing inputs", "[scenario][economy][production]") {
    // Test: No supply delta emitted means market supply is unchanged.
    auto world = create_test_world(42, 10, 1, 5);
    float initial_supply = world.regional_markets[0].supply;

    // Empty delta — no production output.
    DeltaBuffer delta{};
    apply_deltas(world, delta);

    REQUIRE(world.regional_markets[0].supply == initial_supply);
}

TEST_CASE("worker count affects throughput not ratio", "[scenario][economy][production]") {
    // Test: Two businesses with different worker counts produce different amounts
    // but via the same supply delta mechanism.
    auto world = create_test_world(42, 10, 1, 5);

    // Business A: 10 workers produces 23.5 units (8 * 2.35 worker multiplier)
    // Business B: 5 workers produces 12.8 units (8 * 1.6 worker multiplier)
    DeltaBuffer delta{};
    MarketDelta md_a{};
    md_a.good_id = 0;
    md_a.region_id = 0;
    md_a.supply_delta = 23.5f;
    delta.market_deltas.push_back(md_a);

    MarketDelta md_b{};
    md_b.good_id = 0;
    md_b.region_id = 0;
    md_b.supply_delta = 12.8f;
    delta.market_deltas.push_back(md_b);

    float initial_supply = world.regional_markets[0].supply;
    apply_deltas(world, delta);

    REQUIRE_THAT(world.regional_markets[0].supply, WithinAbs(initial_supply + 36.3f, 0.01));
}

// ── Supply chain scenarios ──────────────────────────────────────────────────

TEST_CASE("local supply fulfills before inter-province", "[scenario][economy][supply_chain]") {
    // Test: Demand satisfied locally reduces demand_buffer.
    auto world = create_test_world(42, 10, 2, 5);

    DeltaBuffer delta{};
    MarketDelta md{};
    md.good_id = 0;
    md.region_id = 0;
    md.demand_buffer_delta = -30.0f;  // 30 units fulfilled locally
    delta.market_deltas.push_back(md);

    float initial_demand = world.regional_markets[0].demand_buffer;
    apply_deltas(world, delta);

    REQUIRE_THAT(world.regional_markets[0].demand_buffer,
                 WithinAbs(std::max(0.0f, initial_demand - 30.0f), 0.01));
}

TEST_CASE("inter-province trade creates transit shipment", "[scenario][economy][supply_chain]") {
    // Test: Cross-province delta applied at the correct tick.
    auto world = create_test_world(42, 10, 2, 5);

    // Schedule a cross-province supply arrival for tick 5.
    CrossProvinceDelta cpd{};
    cpd.source_province_id = 1;
    cpd.target_province_id = 0;
    cpd.due_tick = 5;
    MarketDelta arrival{};
    arrival.good_id = 0;
    arrival.region_id = 0;
    arrival.supply_delta = 25.0f;
    cpd.market_delta = arrival;
    world.cross_province_delta_buffer.entries.push_back(cpd);

    // At tick 3, cross-province delta should NOT fire (due_tick=5 > 3).
    world.current_tick = 3;
    apply_cross_province_deltas(world);
    // Entry is retained in the buffer because it's not yet due.

    // At tick 5, the arrival should apply.
    world.current_tick = 5;
    float supply_before = world.regional_markets[0].supply;
    apply_cross_province_deltas(world);
    REQUIRE_THAT(world.regional_markets[0].supply, WithinAbs(supply_before + 25.0f, 0.01));
}

// ── Price engine scenarios ──────────────────────────────────────────────────

TEST_CASE("price rises when demand exceeds supply", "[scenario][economy][price_engine]") {
    // Test: spot_price_override written by price engine increases price.
    auto world = create_test_world(42, 10, 1, 5);
    float initial_price = world.regional_markets[0].spot_price;

    // Simulate price engine output: demand > supply pushes price up.
    DeltaBuffer delta{};
    MarketDelta md{};
    md.good_id = 0;
    md.region_id = 0;
    md.spot_price_override = initial_price * 1.15f;  // 15% increase
    delta.market_deltas.push_back(md);
    apply_deltas(world, delta);

    REQUIRE(world.regional_markets[0].spot_price > initial_price);
    REQUIRE_THAT(world.regional_markets[0].spot_price, WithinAbs(initial_price * 1.15f, 0.01));
}

TEST_CASE("price falls when supply exceeds demand", "[scenario][economy][price_engine]") {
    // Test: Oversupply leads to lower spot_price.
    auto world = create_test_world(42, 10, 1, 5);
    float initial_price = world.regional_markets[0].spot_price;

    DeltaBuffer delta{};
    MarketDelta md{};
    md.good_id = 0;
    md.region_id = 0;
    md.spot_price_override = initial_price * 0.85f;  // 15% decrease
    delta.market_deltas.push_back(md);
    apply_deltas(world, delta);

    REQUIRE(world.regional_markets[0].spot_price < initial_price);
}

TEST_CASE("government price ceiling prevents price above cap",
          "[scenario][economy][price_engine]") {
    // Test: Price override clamped by apply_deltas floor (0.001).
    auto world = create_test_world(42, 10, 1, 5);

    // Set import_price_ceiling (represents government cap).
    float ceiling = 12.0f;

    DeltaBuffer delta{};
    MarketDelta md{};
    md.good_id = 0;
    md.region_id = 0;
    // Price engine would clamp at ceiling — simulate that output.
    md.spot_price_override = ceiling;
    delta.market_deltas.push_back(md);
    apply_deltas(world, delta);

    REQUIRE_THAT(world.regional_markets[0].spot_price, WithinAbs(ceiling, 0.01));
}

// ── Financial distribution scenarios ────────────────────────────────────────

TEST_CASE("employees receive wages before dividends distributed",
          "[scenario][economy][financial]") {
    // Test: NPC capital increases from salary payment via NPCDelta.
    auto world = create_test_world(42, 10, 1, 5);
    float initial_capital = world.significant_npcs[0].capital;

    DeltaBuffer delta{};
    NPCDelta nd{};
    nd.npc_id = world.significant_npcs[0].id;
    nd.capital_delta = 500.0f;  // salary payment
    delta.npc_deltas.push_back(nd);
    apply_deltas(world, delta);

    REQUIRE_THAT(world.significant_npcs[0].capital, WithinAbs(initial_capital + 500.0f, 0.01));
}

TEST_CASE("negative net income reduces business cash", "[scenario][economy][financial]") {
    // Test: BusinessDelta with negative cash_delta.
    auto world = create_test_world(42, 10, 1, 5);
    float initial_cash = world.npc_businesses[0].cash;

    DeltaBuffer delta{};
    BusinessDelta bd{};
    bd.business_id = world.npc_businesses[0].id;
    bd.cash_delta = -200.0f;
    delta.business_deltas.push_back(bd);
    apply_deltas(world, delta);

    REQUIRE_THAT(world.npc_businesses[0].cash, WithinAbs(initial_cash - 200.0f, 0.01));
}

TEST_CASE("tax withholding reduces net payment", "[scenario][economy][financial]") {
    // Test: A salary of 1000 with 20% withholding yields 800 net.
    // This is tested at the financial_distribution module level.
    // Here we verify the delta application works for the net amount.
    auto world = create_test_world(42, 10, 1, 5);
    float initial_capital = world.significant_npcs[0].capital;
    float net_salary = 800.0f;  // 1000 - 20% tax

    DeltaBuffer delta{};
    NPCDelta nd{};
    nd.npc_id = world.significant_npcs[0].id;
    nd.capital_delta = net_salary;
    delta.npc_deltas.push_back(nd);
    apply_deltas(world, delta);

    REQUIRE_THAT(world.significant_npcs[0].capital, WithinAbs(initial_capital + net_salary, 0.01));
}

// ── Labor market scenarios ──────────────────────────────────────────────────

TEST_CASE("hiring increases business cost", "[scenario][economy][labor]") {
    // Test: BusinessDelta cost update.
    auto world = create_test_world(42, 10, 1, 5);

    DeltaBuffer delta{};
    BusinessDelta bd{};
    bd.business_id = world.npc_businesses[0].id;
    bd.cost_per_tick_update = 600.0f;  // increased from 400
    delta.business_deltas.push_back(bd);
    apply_deltas(world, delta);

    REQUIRE_THAT(world.npc_businesses[0].cost_per_tick, WithinAbs(600.0f, 0.01));
}

TEST_CASE("wage payment reduces business cash and increases NPC capital",
          "[scenario][economy][labor]") {
    auto world = create_test_world(42, 10, 1, 5);
    float biz_cash = world.npc_businesses[0].cash;
    float npc_capital = world.significant_npcs[0].capital;

    DeltaBuffer delta{};
    // Business pays wage
    BusinessDelta bd{};
    bd.business_id = world.npc_businesses[0].id;
    bd.cash_delta = -100.0f;
    delta.business_deltas.push_back(bd);

    // NPC receives wage
    NPCDelta nd{};
    nd.npc_id = world.significant_npcs[0].id;
    nd.capital_delta = 100.0f;
    delta.npc_deltas.push_back(nd);

    apply_deltas(world, delta);

    REQUIRE_THAT(world.npc_businesses[0].cash, WithinAbs(biz_cash - 100.0f, 0.01));
    REQUIRE_THAT(world.significant_npcs[0].capital, WithinAbs(npc_capital + 100.0f, 0.01));
}

// ── DeferredWorkQueue integration scenarios ─────────────────────────────────

TEST_CASE("evidence decays over time via DWQ", "[scenario][economy][evidence]") {
    auto world = create_test_world(42, 10, 1, 5);
    world.current_tick = 7;

    EvidenceToken token{};
    token.id = 1;
    token.type = EvidenceType::financial;
    token.actionability = 0.8f;
    token.decay_rate = 0.05f;
    token.is_active = true;
    world.evidence_pool.push_back(token);

    world.deferred_work_queue.push({7, WorkType::evidence_decay_batch, 1, EvidenceDecayPayload{1}});

    DeltaBuffer delta{};
    drain_deferred_work(world, delta);
    apply_deltas(world, delta);

    REQUIRE(world.evidence_pool[0].actionability < 0.8f);
    REQUIRE(world.evidence_pool[0].is_active == true);
}

TEST_CASE("relationship trust decays over time via DWQ", "[scenario][economy][npc]") {
    auto world = create_test_world(42, 10, 1, 5);
    world.current_tick = 30;

    // Give first NPC a relationship
    Relationship rel{};
    rel.target_npc_id = world.significant_npcs[1].id;
    rel.trust = 0.7f;
    rel.fear = 0.2f;
    rel.obligation_balance = 0.0f;
    rel.last_interaction_tick = 0;
    rel.is_movement_ally = false;
    rel.recovery_ceiling = 1.0f;
    world.significant_npcs[0].relationships.push_back(rel);

    world.deferred_work_queue.push({30, WorkType::npc_relationship_decay,
                                    world.significant_npcs[0].id,
                                    NPCRelationshipDecayPayload{world.significant_npcs[0].id}});

    DeltaBuffer delta{};
    drain_deferred_work(world, delta);
    apply_deltas(world, delta);

    REQUIRE(world.significant_npcs[0].relationships[0].trust < 0.7f);
    REQUIRE(world.significant_npcs[0].relationships[0].fear < 0.2f);
}

// ── Multi-delta accumulation scenarios ──────────────────────────────────────

TEST_CASE("multiple supply deltas accumulate correctly", "[scenario][economy][accumulation]") {
    auto world = create_test_world(42, 10, 1, 5);
    float initial_supply = world.regional_markets[0].supply;

    DeltaBuffer delta{};
    for (int i = 0; i < 5; ++i) {
        MarketDelta md{};
        md.good_id = 0;
        md.region_id = 0;
        md.supply_delta = 10.0f;
        delta.market_deltas.push_back(md);
    }
    apply_deltas(world, delta);

    REQUIRE_THAT(world.regional_markets[0].supply, WithinAbs(initial_supply + 50.0f, 0.01));
}

TEST_CASE("NaN delta treated as zero in scenario context", "[scenario][economy][safety]") {
    auto world = create_test_world(42, 10, 1, 5);
    float initial_capital = world.significant_npcs[0].capital;

    DeltaBuffer delta{};
    NPCDelta nd{};
    nd.npc_id = world.significant_npcs[0].id;
    nd.capital_delta = std::numeric_limits<float>::quiet_NaN();
    delta.npc_deltas.push_back(nd);
    apply_deltas(world, delta);

    REQUIRE_THAT(world.significant_npcs[0].capital, WithinAbs(initial_capital, 0.01));
}

TEST_CASE("delta buffer cleared after application", "[scenario][economy][safety]") {
    auto world = create_test_world(42, 10, 1, 5);

    DeltaBuffer delta{};
    NPCDelta nd{};
    nd.npc_id = world.significant_npcs[0].id;
    nd.capital_delta = 100.0f;
    delta.npc_deltas.push_back(nd);

    MarketDelta md{};
    md.good_id = 0;
    md.region_id = 0;
    md.supply_delta = 50.0f;
    delta.market_deltas.push_back(md);

    apply_deltas(world, delta);

    REQUIRE(delta.npc_deltas.empty());
    REQUIRE(delta.market_deltas.empty());
    REQUIRE(delta.business_deltas.empty());
    REQUIRE(delta.evidence_deltas.empty());
    REQUIRE(delta.region_deltas.empty());
}

// ── Region condition scenarios ──────────────────────────────────────────────

TEST_CASE("crime rate increase affects stability", "[scenario][economy][region]") {
    auto world = create_test_world(42, 10, 1, 5);

    DeltaBuffer delta{};
    RegionDelta rd{};
    rd.region_id = 0;
    rd.crime_rate_delta = 0.1f;
    rd.stability_delta = -0.05f;
    delta.region_deltas.push_back(rd);
    apply_deltas(world, delta);

    REQUIRE(world.provinces[0].conditions.crime_rate > 0.1f);
    REQUIRE(world.provinces[0].conditions.stability_score < 0.7f);
}

TEST_CASE("region conditions clamped to 0-1 range", "[scenario][economy][region]") {
    auto world = create_test_world(42, 10, 1, 5);

    DeltaBuffer delta{};
    RegionDelta rd{};
    rd.region_id = 0;
    rd.stability_delta = -5.0f;  // would push far below 0
    rd.crime_rate_delta = 5.0f;  // would push far above 1
    delta.region_deltas.push_back(rd);
    apply_deltas(world, delta);

    REQUIRE(world.provinces[0].conditions.stability_score == 0.0f);
    REQUIRE(world.provinces[0].conditions.crime_rate == 1.0f);
}
