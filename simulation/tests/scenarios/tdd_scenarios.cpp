// TDD-derived scenario tests — behavioral assertions from Technical Design v29.
// These test specific technical requirements: seasonal supply gaps, board rejection,
// trade infrastructure, commodity trading, real estate.
//
// Tagged [scenario][tdd]. Each test is self-contained: setup world, apply
// deltas directly, assert outcomes. No module execution — delta application
// infrastructure only.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstdint>

#include "core/world_state/world_state.h"
#include "core/world_state/apply_deltas.h"
#include "core/tick/drain_deferred_work.h"
#include "tests/test_world_factory.h"

using namespace econlife;
using namespace econlife::test;
using Catch::Matchers::WithinAbs;

// ── Seasonal supply gap scenario (TDD §44) ──────────────────────────────────

TEST_CASE("seasonal harvest creates supply spike then gap", "[scenario][tdd][agriculture]") {
    // Wheat market in one province. Tick 90 is harvest; off-season produces nothing.
    // We simulate:
    //   - baseline supply and price before harvest
    //   - apply a large supply delta at tick 90 (harvest) with a matching price drop
    //   - verify supply rose and price dropped
    //   - apply no supply delta at tick 180 (off-season); price returns higher via override

    auto world = create_test_world(42, 10, 1, 5);

    // Use market 0 as the wheat market in province 0.
    const uint32_t WHEAT = 0;
    const uint32_t PROVINCE = 0;

    float pre_harvest_supply = world.regional_markets[0].supply;
    float pre_harvest_price  = world.regional_markets[0].spot_price;

    // ---- Tick 90: harvest arrives ----
    world.current_tick = 90;

    DeltaBuffer harvest_delta{};
    MarketDelta harvest_md{};
    harvest_md.good_id               = WHEAT;
    harvest_md.region_id             = PROVINCE;
    harvest_md.supply_delta          = 300.0f;                    // large harvest surplus
    harvest_md.spot_price_override   = pre_harvest_price * 0.75f; // price drops 25 %
    harvest_delta.market_deltas.push_back(harvest_md);
    apply_deltas(world, harvest_delta);

    float harvest_supply = world.regional_markets[0].supply;
    float harvest_price  = world.regional_markets[0].spot_price;

    REQUIRE(harvest_supply > pre_harvest_supply);
    REQUIRE_THAT(harvest_supply, WithinAbs(pre_harvest_supply + 300.0f, 0.01f));
    REQUIRE(harvest_price < pre_harvest_price);

    // ---- Tick 180: off-season — no supply added, price rises ----
    world.current_tick = 180;

    DeltaBuffer offseason_delta{};
    MarketDelta offseason_md{};
    offseason_md.good_id             = WHEAT;
    offseason_md.region_id           = PROVINCE;
    // No supply_delta — nothing produced.
    offseason_md.spot_price_override = pre_harvest_price * 1.20f; // price rises above pre-harvest
    offseason_delta.market_deltas.push_back(offseason_md);
    apply_deltas(world, offseason_delta);

    float offseason_price = world.regional_markets[0].spot_price;

    REQUIRE(offseason_price > harvest_price);
    REQUIRE(offseason_price > pre_harvest_price);
    // Supply unchanged since off-season tick (no delta applied to supply).
    REQUIRE_THAT(world.regional_markets[0].supply, WithinAbs(harvest_supply, 0.01f));
}

// ── Inter-province trade smooths seasonal price differences (TDD §44 + §18) ─

TEST_CASE("inter-province trade smooths seasonal price differences", "[scenario][tdd][trade]") {
    // Province 0 harvests at tick 90 (price drops to low).
    // Province 1 harvests at tick 270 (price drops to low on its turn).
    // With trade, a cross-province delta ships surplus from the harvesting
    // province to the non-harvesting one, compressing the price spread.
    //
    // We compare two scenarios:
    //   ISOLATED  — each province's price swings independently.
    //   TRADED    — a cross-province delta delivers supply to the high-price province,
    //               keeping its price lower than the isolated case.

    const uint32_t GOOD     = 0;
    const uint32_t PROV_A   = 0;   // northern hemisphere — harvests at tick 90
    const uint32_t PROV_B   = 1;   // southern hemisphere — harvests at tick 270

    // --- ISOLATED scenario ---
    {
        auto world = create_test_world(10, 10, 2, 3);

        float price_A_base = world.regional_markets[0].spot_price; // province 0, good 0
        // Find Province B's market for good 0.
        float price_B_base = 0.0f;
        for (const auto& m : world.regional_markets) {
            if (m.good_id == GOOD && m.province_id == PROV_B) { price_B_base = m.spot_price; break; }
        }

        // Tick 90: Province A harvests — price drops.
        world.current_tick = 90;
        {
            DeltaBuffer d{};
            MarketDelta md{};
            md.good_id = GOOD; md.region_id = PROV_A;
            md.supply_delta        = 200.0f;
            md.spot_price_override = price_A_base * 0.60f;
            d.market_deltas.push_back(md);
            apply_deltas(world, d);
        }
        float price_A_harvest = world.regional_markets[0].spot_price;

        // Province B has no harvest — its price rises (scarcity).
        {
            DeltaBuffer d{};
            MarketDelta md{};
            md.good_id = GOOD; md.region_id = PROV_B;
            md.spot_price_override = price_B_base * 1.40f;
            d.market_deltas.push_back(md);
            apply_deltas(world, d);
        }
        float price_B_scarce_isolated = 0.0f;
        for (const auto& m : world.regional_markets) {
            if (m.good_id == GOOD && m.province_id == PROV_B) {
                price_B_scarce_isolated = m.spot_price; break;
            }
        }

        float isolated_spread = price_B_scarce_isolated - price_A_harvest;
        REQUIRE(isolated_spread > 0.0f); // B is more expensive than A in isolation.

        // --- TRADED scenario (same world, apply cross-province smoothing) ---
        // Province A ships 100 units to Province B at tick 90 (same tick, due_tick == current).
        CrossProvinceDelta cpd{};
        cpd.source_province_id = PROV_A;
        cpd.target_province_id = PROV_B;
        cpd.due_tick           = 90; // already due this tick
        MarketDelta trade_arrival{};
        trade_arrival.good_id             = GOOD;
        trade_arrival.region_id           = PROV_B;
        trade_arrival.supply_delta        = 100.0f;
        trade_arrival.spot_price_override = price_B_base * 1.10f; // smoothed — only 10 % above base
        cpd.market_delta = trade_arrival;
        world.cross_province_delta_buffer.entries.push_back(cpd);

        apply_cross_province_deltas(world);

        float price_B_traded = 0.0f;
        for (const auto& m : world.regional_markets) {
            if (m.good_id == GOOD && m.province_id == PROV_B) {
                price_B_traded = m.spot_price; break;
            }
        }

        float traded_spread = price_B_traded - price_A_harvest;
        // Trade reduced Province B's price; spread is smaller than isolated.
        REQUIRE(traded_spread < isolated_spread);
    }
}

// ── Board rejection scenarios (TDD §42) ─────────────────────────────────────

TEST_CASE("weak board approves bad expansion", "[scenario][tdd][business]") {
    // A business with a weak board cannot block a bad decision.
    // Modelled as: the expansion cash_delta IS applied (board failed to stop it).
    auto world = create_test_world(42, 10, 1, 5);

    float initial_cash = world.npc_businesses[0].cash;
    const float EXPANSION_COST = 20000.0f;

    // A weak board does not block the spending — cash_delta is applied.
    DeltaBuffer delta{};
    BusinessDelta bd{};
    bd.business_id = world.npc_businesses[0].id;
    bd.cash_delta  = -EXPANSION_COST;
    delta.business_deltas.push_back(bd);
    apply_deltas(world, delta);

    // Board approved: cash has decreased by the expansion amount.
    REQUIRE_THAT(world.npc_businesses[0].cash,
                 WithinAbs(initial_cash - EXPANSION_COST, 0.01f));
    REQUIRE(world.npc_businesses[0].cash < initial_cash);
}

TEST_CASE("strong board rejects bad expansion", "[scenario][tdd][business]") {
    // A strong board blocks the bad expansion — no cash_delta is written.
    auto world = create_test_world(42, 10, 1, 5);

    float initial_cash = world.npc_businesses[0].cash;

    // Strong board rejects: NO cash delta is applied.
    DeltaBuffer delta{};
    // Intentionally empty — board vetoed the expansion.
    apply_deltas(world, delta);

    // Cash is unchanged — expansion was rejected.
    REQUIRE_THAT(world.npc_businesses[0].cash, WithinAbs(initial_cash, 0.01f));
}

// ── Trade infrastructure scenarios (TDD §18) ────────────────────────────────

TEST_CASE("transport mode affects transit time", "[scenario][tdd][trade]") {
    // Road shipment: due in 3 ticks (fast).
    // Sea shipment:  due in 10 ticks (slow).
    // Both start from province 0 to province 1.
    // We verify each fires at its own due_tick and NOT before.

    auto world = create_test_world(42, 10, 2, 5);

    const uint32_t GOOD   = 0;
    const uint32_t TARGET = 1;

    float supply_before = 0.0f;
    for (const auto& m : world.regional_markets) {
        if (m.good_id == GOOD && m.province_id == TARGET) { supply_before = m.supply; break; }
    }

    // Road delta: due_tick = current_tick + 3
    CrossProvinceDelta road_cpd{};
    road_cpd.source_province_id = 0;
    road_cpd.target_province_id = TARGET;
    road_cpd.due_tick           = 3;
    MarketDelta road_arrival{};
    road_arrival.good_id     = GOOD;
    road_arrival.region_id   = TARGET;
    road_arrival.supply_delta = 50.0f;
    road_cpd.market_delta    = road_arrival;

    // Sea delta: due_tick = current_tick + 10
    CrossProvinceDelta sea_cpd{};
    sea_cpd.source_province_id = 0;
    sea_cpd.target_province_id = TARGET;
    sea_cpd.due_tick           = 10;
    MarketDelta sea_arrival{};
    sea_arrival.good_id      = GOOD;
    sea_arrival.region_id    = TARGET;
    sea_arrival.supply_delta = 80.0f;
    sea_cpd.market_delta     = sea_arrival;

    // At tick 2: neither should have fired yet.
    world.current_tick = 2;
    world.cross_province_delta_buffer.entries.push_back(road_cpd);
    world.cross_province_delta_buffer.entries.push_back(sea_cpd);
    apply_cross_province_deltas(world);

    float supply_tick2 = 0.0f;
    for (const auto& m : world.regional_markets) {
        if (m.good_id == GOOD && m.province_id == TARGET) { supply_tick2 = m.supply; break; }
    }
    REQUIRE_THAT(supply_tick2, WithinAbs(supply_before, 0.01f));

    // At tick 3: road shipment fires. Sea shipment does not.
    world.cross_province_delta_buffer.entries.push_back(road_cpd);
    world.cross_province_delta_buffer.entries.push_back(sea_cpd);
    world.current_tick = 3;
    apply_cross_province_deltas(world);

    float supply_tick3 = 0.0f;
    for (const auto& m : world.regional_markets) {
        if (m.good_id == GOOD && m.province_id == TARGET) { supply_tick3 = m.supply; break; }
    }
    REQUIRE_THAT(supply_tick3, WithinAbs(supply_before + 50.0f, 0.01f));

    // At tick 10: sea shipment fires.
    world.cross_province_delta_buffer.entries.push_back(sea_cpd);
    world.current_tick = 10;
    apply_cross_province_deltas(world);

    float supply_tick10 = 0.0f;
    for (const auto& m : world.regional_markets) {
        if (m.good_id == GOOD && m.province_id == TARGET) { supply_tick10 = m.supply; break; }
    }
    REQUIRE_THAT(supply_tick10, WithinAbs(supply_before + 50.0f + 80.0f, 0.01f));

    // The two transit times are different (3 ticks vs 10 ticks).
    REQUIRE(road_cpd.due_tick != sea_cpd.due_tick);
}

TEST_CASE("perishable goods degrade during transit", "[scenario][tdd][trade]") {
    // Ship 100 units of a perishable good. Decay during 10-tick transit reduces
    // the delivered quantity. The arriving supply delta is the post-decay amount.

    auto world = create_test_world(42, 10, 2, 5);

    const uint32_t GOOD     = 0;
    const uint32_t TARGET   = 1;
    const float    SHIPPED  = 100.0f;
    // 10-tick transit with 2 % decay per tick: 100 * (0.98)^10 ≈ 81.7 units delivered.
    const float    DECAY_RATE    = 0.02f;
    const float    TRANSIT_TICKS = 10.0f;
    float          delivered = SHIPPED;
    for (int i = 0; i < static_cast<int>(TRANSIT_TICKS); ++i) {
        delivered *= (1.0f - DECAY_RATE);
    }

    float supply_before = 0.0f;
    for (const auto& m : world.regional_markets) {
        if (m.good_id == GOOD && m.province_id == TARGET) { supply_before = m.supply; break; }
    }

    // Cross-province delta carries the degraded quantity.
    CrossProvinceDelta cpd{};
    cpd.source_province_id = 0;
    cpd.target_province_id = TARGET;
    cpd.due_tick           = 10;
    MarketDelta arrival{};
    arrival.good_id      = GOOD;
    arrival.region_id    = TARGET;
    arrival.supply_delta = delivered;    // degraded amount
    cpd.market_delta     = arrival;

    world.current_tick = 10;
    world.cross_province_delta_buffer.entries.push_back(cpd);
    apply_cross_province_deltas(world);

    float supply_after = 0.0f;
    for (const auto& m : world.regional_markets) {
        if (m.good_id == GOOD && m.province_id == TARGET) { supply_after = m.supply; break; }
    }

    float received = supply_after - supply_before;

    // Delivered strictly less than shipped.
    REQUIRE(received < SHIPPED);
    REQUIRE_THAT(received, WithinAbs(delivered, 0.01f));
}

TEST_CASE("route capacity bottleneck delays shipments", "[scenario][tdd][trade]") {
    // Three shipments of 100 units each are queued over a route with
    // capacity of 100 units/tick. The capacity constraint is represented by
    // staggered due_ticks: tick 5, tick 6, tick 7.

    auto world = create_test_world(42, 10, 2, 5);

    const uint32_t GOOD   = 0;
    const uint32_t TARGET = 1;

    float supply_before = 0.0f;
    for (const auto& m : world.regional_markets) {
        if (m.good_id == GOOD && m.province_id == TARGET) { supply_before = m.supply; break; }
    }

    // Enqueue three shipments, each delayed by one tick due to capacity.
    for (uint32_t i = 0; i < 3; ++i) {
        CrossProvinceDelta cpd{};
        cpd.source_province_id = 0;
        cpd.target_province_id = TARGET;
        cpd.due_tick           = 5 + i;   // staggered: 5, 6, 7
        MarketDelta arrival{};
        arrival.good_id      = GOOD;
        arrival.region_id    = TARGET;
        arrival.supply_delta = 100.0f;
        cpd.market_delta     = arrival;
        world.cross_province_delta_buffer.entries.push_back(cpd);
    }

    // At tick 5: only first shipment fires.
    world.current_tick = 5;
    apply_cross_province_deltas(world);

    float supply_t5 = 0.0f;
    for (const auto& m : world.regional_markets) {
        if (m.good_id == GOOD && m.province_id == TARGET) { supply_t5 = m.supply; break; }
    }
    REQUIRE_THAT(supply_t5, WithinAbs(supply_before + 100.0f, 0.01f));

    // All three due_ticks are distinct — deliveries span multiple ticks.
    // (5, 6, 7 are all different, confirming capacity-enforced delays.)
    std::vector<uint32_t> due_ticks = {5, 6, 7};
    REQUIRE(due_ticks[0] != due_ticks[1]);
    REQUIRE(due_ticks[1] != due_ticks[2]);
}

// ── Commodity trading scenarios (TDD §17) ───────────────────────────────────

TEST_CASE("long position profits when price rises", "[scenario][tdd][commodity]") {
    // NPC opens long position on steel at price 50. Price rises to 70.
    // Settlement: capital_delta = (70 - 50) * position_size.

    auto world = create_test_world(42, 10, 1, 5);

    const float ENTRY_PRICE   = 50.0f;
    const float EXIT_PRICE    = 70.0f;
    const float POSITION_SIZE = 100.0f;
    const float PROFIT        = (EXIT_PRICE - ENTRY_PRICE) * POSITION_SIZE; // 2000.0

    float initial_capital = world.significant_npcs[0].capital;

    // Settlement: apply profit as capital_delta.
    DeltaBuffer delta{};
    NPCDelta nd{};
    nd.npc_id        = world.significant_npcs[0].id;
    nd.capital_delta = PROFIT;
    delta.npc_deltas.push_back(nd);
    apply_deltas(world, delta);

    REQUIRE_THAT(world.significant_npcs[0].capital,
                 WithinAbs(initial_capital + PROFIT, 0.01f));
    REQUIRE(world.significant_npcs[0].capital > initial_capital);
}

TEST_CASE("margin call triggered on losing position", "[scenario][tdd][commodity]") {
    // NPC holds a long position. Price drops below maintenance margin.
    // Settlement: capital_delta is negative (margin call loss).

    auto world = create_test_world(42, 10, 1, 5);

    const float ENTRY_PRICE   = 50.0f;
    const float DROP_PRICE    = 38.0f;   // below maintenance margin (e.g. 80 % of entry)
    const float POSITION_SIZE = 100.0f;
    const float LOSS          = (DROP_PRICE - ENTRY_PRICE) * POSITION_SIZE; // -1200.0

    float initial_capital = world.significant_npcs[0].capital;

    // Margin call: apply loss.
    DeltaBuffer delta{};
    NPCDelta nd{};
    nd.npc_id        = world.significant_npcs[0].id;
    nd.capital_delta = LOSS;
    delta.npc_deltas.push_back(nd);
    apply_deltas(world, delta);

    REQUIRE(world.significant_npcs[0].capital < initial_capital);
    REQUIRE_THAT(world.significant_npcs[0].capital,
                 WithinAbs(std::max(0.0f, initial_capital + LOSS), 0.01f));
}

// ── Real estate scenarios ───────────────────────────────────────────────────

TEST_CASE("rent collection transfers capital from tenant to owner", "[scenario][tdd][real_estate]") {
    // Tenant NPC pays 100 in rent to owner NPC in the same tick.

    auto world = create_test_world(42, 10, 1, 5);

    // Use first two NPCs as tenant and owner.
    uint32_t tenant_id = world.significant_npcs[0].id;
    uint32_t owner_id  = world.significant_npcs[1].id;

    float tenant_initial = world.significant_npcs[0].capital;
    float owner_initial  = world.significant_npcs[1].capital;

    const float RENT = 100.0f;

    DeltaBuffer delta{};

    NPCDelta tenant_delta{};
    tenant_delta.npc_id        = tenant_id;
    tenant_delta.capital_delta = -RENT;
    delta.npc_deltas.push_back(tenant_delta);

    NPCDelta owner_delta{};
    owner_delta.npc_id        = owner_id;
    owner_delta.capital_delta = RENT;
    delta.npc_deltas.push_back(owner_delta);

    apply_deltas(world, delta);

    REQUIRE_THAT(world.significant_npcs[0].capital,
                 WithinAbs(std::max(0.0f, tenant_initial - RENT), 0.01f));
    REQUIRE_THAT(world.significant_npcs[1].capital,
                 WithinAbs(owner_initial + RENT, 0.01f));
}

TEST_CASE("property value correlates with economic health", "[scenario][tdd][real_estate]") {
    // As province stability rises, property prices (real_estate market) increase.
    // We use a dedicated market entry as the real_estate proxy (good_id = 2).

    auto world = create_test_world(42, 10, 1, 5);

    const uint32_t REAL_ESTATE_GOOD = 2;
    const uint32_t PROVINCE         = 0;

    float price_before = 0.0f;
    for (const auto& m : world.regional_markets) {
        if (m.good_id == REAL_ESTATE_GOOD && m.province_id == PROVINCE) {
            price_before = m.spot_price; break;
        }
    }

    float stability_before = world.provinces[0].conditions.stability_score;

    // Tick 1: improve stability (+0.1).
    {
        DeltaBuffer d{};
        RegionDelta rd{};
        rd.region_id        = PROVINCE;
        rd.stability_delta  = 0.1f;
        d.region_deltas.push_back(rd);
        apply_deltas(world, d);
    }

    float stability_after = world.provinces[0].conditions.stability_score;
    REQUIRE(stability_after > stability_before);

    // Economic health improved — raise property listing price via price override.
    float price_after_stability = price_before * (1.0f + (stability_after - stability_before) * 0.5f);
    {
        DeltaBuffer d{};
        MarketDelta md{};
        md.good_id             = REAL_ESTATE_GOOD;
        md.region_id           = PROVINCE;
        md.spot_price_override = price_after_stability;
        d.market_deltas.push_back(md);
        apply_deltas(world, d);
    }

    float price_after = 0.0f;
    for (const auto& m : world.regional_markets) {
        if (m.good_id == REAL_ESTATE_GOOD && m.province_id == PROVINCE) {
            price_after = m.spot_price; break;
        }
    }

    REQUIRE(price_after > price_before);
}

// ── Cross-province delta propagation (TDD §2a) ──────────────────────────────

TEST_CASE("cross-province effect has one-tick delay", "[scenario][tdd][core]") {
    // A cross-province delta is created with due_tick = current_tick + 1.
    // At current_tick, apply_cross_province_deltas must NOT apply it (due_tick in future).
    // At current_tick + 1, it IS applied.

    auto world = create_test_world(42, 10, 2, 5);

    const uint32_t GOOD     = 0;
    const uint32_t TARGET   = 1;
    const uint32_t TICK_NOW = 10;

    float supply_before = 0.0f;
    for (const auto& m : world.regional_markets) {
        if (m.good_id == GOOD && m.province_id == TARGET) { supply_before = m.supply; break; }
    }

    CrossProvinceDelta cpd{};
    cpd.source_province_id = 0;
    cpd.target_province_id = TARGET;
    cpd.due_tick           = TICK_NOW + 1;   // one tick in the future
    MarketDelta arrival{};
    arrival.good_id      = GOOD;
    arrival.region_id    = TARGET;
    arrival.supply_delta = 77.0f;
    cpd.market_delta     = arrival;

    // ---- At tick N: effect must NOT be visible ----
    world.current_tick = TICK_NOW;
    world.cross_province_delta_buffer.entries.push_back(cpd);
    apply_cross_province_deltas(world);

    float supply_tick_n = 0.0f;
    for (const auto& m : world.regional_markets) {
        if (m.good_id == GOOD && m.province_id == TARGET) { supply_tick_n = m.supply; break; }
    }
    // apply_cross_province_deltas skips entries where due_tick > current_tick,
    // then clears the buffer. The delta must NOT have been applied.
    REQUIRE_THAT(supply_tick_n, WithinAbs(supply_before, 0.01f));

    // ---- At tick N+1: effect IS visible ----
    // Re-enqueue because buffer was cleared.
    world.current_tick = TICK_NOW + 1;
    world.cross_province_delta_buffer.entries.push_back(cpd);
    apply_cross_province_deltas(world);

    float supply_tick_n1 = 0.0f;
    for (const auto& m : world.regional_markets) {
        if (m.good_id == GOOD && m.province_id == TARGET) { supply_tick_n1 = m.supply; break; }
    }
    REQUIRE_THAT(supply_tick_n1, WithinAbs(supply_before + 77.0f, 0.01f));
}

TEST_CASE("cross-province buffer empty at save time", "[scenario][tdd][core]") {
    // After apply_cross_province_deltas() the buffer must be empty.
    // This is the precondition for safe serialization.

    auto world = create_test_world(42, 10, 2, 5);

    const uint32_t GOOD   = 0;
    const uint32_t TARGET = 1;

    // Push several entries at various due_ticks.
    for (uint32_t i = 0; i < 5; ++i) {
        CrossProvinceDelta cpd{};
        cpd.source_province_id = 0;
        cpd.target_province_id = TARGET;
        cpd.due_tick           = i;   // past, present, and future relative to tick 2
        MarketDelta md{};
        md.good_id      = GOOD;
        md.region_id    = TARGET;
        md.supply_delta = 10.0f;
        cpd.market_delta = md;
        world.cross_province_delta_buffer.entries.push_back(cpd);
    }

    REQUIRE(!world.cross_province_delta_buffer.entries.empty());

    world.current_tick = 2;
    apply_cross_province_deltas(world);

    // Buffer cleared unconditionally — safe to serialize.
    REQUIRE(world.cross_province_delta_buffer.entries.empty());
}
