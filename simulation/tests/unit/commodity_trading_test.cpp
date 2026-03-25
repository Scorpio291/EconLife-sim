// Commodity trading module unit tests.
// All tests tagged [commodity_trading][tier4].
//
// Tests verify:
//   - Long/short position P&L (profit and loss scenarios)
//   - exit_tick lifecycle (0 while open, set on close)
//   - Market impact for large vs. small positions
//   - Capital gains tax flagging
//   - Position open/close lifecycle
//   - Module interface properties
//   - Multiple positions for the same good

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"
#include "modules/commodity_trading/commodity_trading_module.h"
#include "modules/commodity_trading/commodity_trading_types.h"
#include "modules/economy/financial_types.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

namespace {

// Create a minimal WorldState suitable for commodity trading tests.
WorldState make_test_world_state() {
    WorldState state{};
    state.current_tick = 100;
    state.world_seed = 42;
    state.player = nullptr;
    state.lod2_price_index = nullptr;
    state.ticks_this_session = 1;
    state.game_mode = GameMode::standard;
    state.current_schema_version = 1;
    state.network_health_dirty = false;
    return state;
}

// Create a RegionalMarket with specified parameters.
RegionalMarket make_test_market(uint32_t good_id, uint32_t province_id, float supply,
                                float spot_price) {
    RegionalMarket market{};
    market.good_id = good_id;
    market.province_id = province_id;
    market.spot_price = spot_price;
    market.supply = supply;
    market.demand_buffer = 100.0f;
    market.equilibrium_price = spot_price;
    market.adjustment_rate = 0.10f;
    market.import_price_ceiling = 0.0f;
    market.export_price_floor = 0.0f;
    return market;
}

// Create a CommodityPosition with sensible defaults.
CommodityPosition make_test_position(uint32_t id, uint32_t actor_id, uint32_t good_id,
                                     uint32_t province_id, PositionType type, float quantity,
                                     float entry_price, uint32_t opened_tick) {
    CommodityPosition pos{};
    pos.id = id;
    pos.actor_id = actor_id;
    pos.good_id = good_id;
    pos.province_id = province_id;
    pos.position_type = type;
    pos.quantity = quantity;
    pos.entry_price = entry_price;
    pos.current_value = 0.0f;
    pos.opened_tick = opened_tick;
    pos.exit_tick = 0;
    pos.realised_pnl = 0.0f;
    return pos;
}

}  // anonymous namespace

// ===========================================================================
// Test 1: Long position P&L when price rises (profit)
// ===========================================================================

TEST_CASE("test_long_position_pnl_price_rises_profit", "[commodity_trading][tier4]") {
    // Long position: buy at 50, sell at 70, quantity 100.
    // P&L = (70 - 50) * 100 = 2000.
    float pnl =
        CommodityTradingModule::compute_pnl(PositionType::long_position, 50.0f, 70.0f, 100.0f);
    REQUIRE_THAT(pnl, WithinAbs(2000.0f, 0.001f));
}

// ===========================================================================
// Test 2: Long position P&L when price falls (loss)
// ===========================================================================

TEST_CASE("test_long_position_pnl_price_falls_loss", "[commodity_trading][tier4]") {
    // Long position: buy at 50, sell at 30, quantity 100.
    // P&L = (30 - 50) * 100 = -2000.
    float pnl =
        CommodityTradingModule::compute_pnl(PositionType::long_position, 50.0f, 30.0f, 100.0f);
    REQUIRE_THAT(pnl, WithinAbs(-2000.0f, 0.001f));
}

// ===========================================================================
// Test 3: Short position P&L when price falls (profit)
// ===========================================================================

TEST_CASE("test_short_position_pnl_price_falls_profit", "[commodity_trading][tier4]") {
    // Short position: sell at 100, buy back at 80, quantity 50.
    // P&L = (100 - 80) * 50 = 1000.
    float pnl =
        CommodityTradingModule::compute_pnl(PositionType::short_position, 100.0f, 80.0f, 50.0f);
    REQUIRE_THAT(pnl, WithinAbs(1000.0f, 0.001f));
}

// ===========================================================================
// Test 4: Short position P&L when price rises (loss)
// ===========================================================================

TEST_CASE("test_short_position_pnl_price_rises_loss", "[commodity_trading][tier4]") {
    // Short position: sell at 100, buy back at 120, quantity 50.
    // P&L = (100 - 120) * 50 = -1000.
    float pnl =
        CommodityTradingModule::compute_pnl(PositionType::short_position, 100.0f, 120.0f, 50.0f);
    REQUIRE_THAT(pnl, WithinAbs(-1000.0f, 0.001f));
}

// ===========================================================================
// Test 5: exit_tick is 0 while open, set to current_tick on close
// ===========================================================================

TEST_CASE("test_exit_tick_lifecycle", "[commodity_trading][tier4]") {
    CommodityTradingModule module;

    auto pos = make_test_position(1, 10, 5, 0, PositionType::long_position, 100.0f, 50.0f, 10);
    module.open_position(pos);

    // While open, exit_tick must be 0.
    REQUIRE(module.positions().size() == 1);
    REQUIRE(module.positions()[0].exit_tick == 0);
    REQUIRE_THAT(module.positions()[0].realised_pnl, WithinAbs(0.0f, 0.001f));

    // Close the position at tick 100 with exit_price 70.
    SettlementResult result = module.close_position(1, 70.0f, 100);
    REQUIRE(result.position_closed == true);

    // After close, exit_tick should be set to the tick of closure.
    REQUIRE(module.positions()[0].exit_tick == 100);
    REQUIRE(module.positions()[0].realised_pnl != 0.0f);
}

// ===========================================================================
// Test 6: Market impact computed for large position
// ===========================================================================

TEST_CASE("test_market_impact_large_position", "[commodity_trading][tier4]") {
    // Position quantity = 200, market supply = 1000.
    // Fraction = 200/1000 = 0.20 (above threshold of 0.05).
    // Excess = 200 - (1000 * 0.05) = 200 - 50 = 150.
    // Impact magnitude = 0.01 * 150 = 1.5.
    // Long position => demand_impact = 1.5, supply_impact = 0.
    auto pos = make_test_position(1, 10, 5, 0, PositionType::long_position, 200.0f, 50.0f, 10);

    MarketImpact impact = CommodityTradingModule::compute_market_impact(pos, 1000.0f);
    REQUIRE(impact.good_id_hash == 5);
    REQUIRE_THAT(impact.demand_impact, WithinAbs(1.5f, 0.001f));
    REQUIRE_THAT(impact.supply_impact, WithinAbs(0.0f, 0.001f));
}

// ===========================================================================
// Test 7: No market impact for small position (below threshold)
// ===========================================================================

TEST_CASE("test_no_market_impact_small_position", "[commodity_trading][tier4]") {
    // Position quantity = 40, market supply = 1000.
    // Fraction = 40/1000 = 0.04 (below threshold of 0.05).
    // No impact expected.
    auto pos = make_test_position(1, 10, 5, 0, PositionType::long_position, 40.0f, 50.0f, 10);

    MarketImpact impact = CommodityTradingModule::compute_market_impact(pos, 1000.0f);
    REQUIRE_THAT(impact.demand_impact, WithinAbs(0.0f, 0.001f));
    REQUIRE_THAT(impact.supply_impact, WithinAbs(0.0f, 0.001f));
}

// ===========================================================================
// Test 8: Capital gains tax flagged on profitable close
// ===========================================================================

TEST_CASE("test_capital_gains_tax_on_profit", "[commodity_trading][tier4]") {
    CommodityTradingModule module;

    auto pos = make_test_position(1, 10, 5, 0, PositionType::long_position, 100.0f, 50.0f, 10);
    module.open_position(pos);

    // Close at 70 => P&L = (70 - 50) * 100 = 2000 (profit).
    // Tax = max(0, 2000) * 0.15 = 300.
    SettlementResult result = module.close_position(1, 70.0f, 100);
    REQUIRE(result.position_closed == true);
    REQUIRE_THAT(result.realized_pnl, WithinAbs(2000.0f, 0.001f));
    REQUIRE_THAT(result.capital_gains_tax, WithinAbs(300.0f, 0.001f));
}

// ===========================================================================
// Test 9: No tax on loss
// ===========================================================================

TEST_CASE("test_no_capital_gains_tax_on_loss", "[commodity_trading][tier4]") {
    CommodityTradingModule module;

    auto pos = make_test_position(1, 10, 5, 0, PositionType::long_position, 100.0f, 50.0f, 10);
    module.open_position(pos);

    // Close at 30 => P&L = (30 - 50) * 100 = -2000 (loss).
    // Tax = max(0, -2000) * 0.15 = 0.
    SettlementResult result = module.close_position(1, 30.0f, 100);
    REQUIRE(result.position_closed == true);
    REQUIRE_THAT(result.realized_pnl, WithinAbs(-2000.0f, 0.001f));
    REQUIRE_THAT(result.capital_gains_tax, WithinAbs(0.0f, 0.001f));
}

// ===========================================================================
// Test 10: Open and close position lifecycle
// ===========================================================================

TEST_CASE("test_open_close_position_lifecycle", "[commodity_trading][tier4]") {
    CommodityTradingModule module;

    // Open a long position.
    auto pos = make_test_position(1, 10, 5, 0, PositionType::long_position, 100.0f, 50.0f, 10);
    module.open_position(pos);

    REQUIRE(module.positions().size() == 1);
    REQUIRE(module.positions()[0].id == 1);
    REQUIRE(module.positions()[0].exit_tick == 0);
    REQUIRE_THAT(module.positions()[0].realised_pnl, WithinAbs(0.0f, 0.001f));

    // Close the position.
    SettlementResult result = module.close_position(1, 60.0f, 200);

    REQUIRE(result.position_closed == true);
    REQUIRE(result.holder_npc_id == 10);
    REQUIRE_THAT(result.realized_pnl, WithinAbs(1000.0f, 0.001f));  // (60-50)*100

    // Position should now reflect the closure.
    REQUIRE(module.positions()[0].exit_tick == 200);
    REQUIRE_THAT(module.positions()[0].realised_pnl, WithinAbs(1000.0f, 0.001f));

    // Attempting to close again should be rejected (duplicate close).
    SettlementResult dup_result = module.close_position(1, 80.0f, 300);
    REQUIRE(dup_result.position_closed == false);

    // Original closure values should be preserved.
    REQUIRE(module.positions()[0].exit_tick == 200);
    REQUIRE_THAT(module.positions()[0].realised_pnl, WithinAbs(1000.0f, 0.001f));
}

// ===========================================================================
// Test 11: Module interface properties (sequential, runs_after, name)
// ===========================================================================

TEST_CASE("test_module_interface_properties", "[commodity_trading][tier4]") {
    CommodityTradingModule module;

    REQUIRE(module.name() == "commodity_trading");
    REQUIRE(module.package_id() == "base_game");
    REQUIRE(module.scope() == ModuleScope::v1);
    REQUIRE(module.is_province_parallel() == false);

    auto after = module.runs_after();
    REQUIRE(after.size() == 1);
    REQUIRE(after[0] == "price_engine");

    auto before = module.runs_before();
    REQUIRE(before.size() == 1);
    REQUIRE(before[0] == "npc_behavior");
}

// ===========================================================================
// Test 12: Multiple positions for same good
// ===========================================================================

TEST_CASE("test_multiple_positions_same_good", "[commodity_trading][tier4]") {
    CommodityTradingModule module;

    // Open two long positions for the same good (id=5, province=0).
    auto pos1 = make_test_position(1, 10, 5, 0, PositionType::long_position, 100.0f, 50.0f, 10);
    auto pos2 = make_test_position(2, 20, 5, 0, PositionType::short_position, 80.0f, 60.0f, 15);
    module.open_position(pos1);
    module.open_position(pos2);

    REQUIRE(module.positions().size() == 2);

    // Verify sorted by id ascending.
    REQUIRE(module.positions()[0].id == 1);
    REQUIRE(module.positions()[1].id == 2);

    // Close position 1: long, entry=50, exit=70 => P&L = (70-50)*100 = 2000.
    SettlementResult r1 = module.close_position(1, 70.0f, 100);
    REQUIRE(r1.position_closed == true);
    REQUIRE_THAT(r1.realized_pnl, WithinAbs(2000.0f, 0.001f));
    REQUIRE(r1.holder_npc_id == 10);

    // Close position 2: short, entry=60, exit=40 => P&L = (60-40)*80 = 1600.
    SettlementResult r2 = module.close_position(2, 40.0f, 100);
    REQUIRE(r2.position_closed == true);
    REQUIRE_THAT(r2.realized_pnl, WithinAbs(1600.0f, 0.001f));
    REQUIRE(r2.holder_npc_id == 20);

    // Both should have tax computed on positive gains.
    REQUIRE_THAT(r1.capital_gains_tax,
                 WithinAbs(2000.0f * CommodityTradingConstants::capital_gains_tax_rate, 0.001f));
    REQUIRE_THAT(r2.capital_gains_tax,
                 WithinAbs(1600.0f * CommodityTradingConstants::capital_gains_tax_rate, 0.001f));
}

// ===========================================================================
// Additional: Market impact for short position (supply-side pressure)
// ===========================================================================

TEST_CASE("test_market_impact_short_position_supply_pressure", "[commodity_trading][tier4]") {
    // Short position with large quantity => supply impact.
    // Quantity = 150, supply = 1000. Fraction = 0.15 (above 0.05 threshold).
    // Excess = 150 - 50 = 100. Impact = 0.01 * 100 = 1.0.
    auto pos = make_test_position(1, 10, 5, 0, PositionType::short_position, 150.0f, 50.0f, 10);

    MarketImpact impact = CommodityTradingModule::compute_market_impact(pos, 1000.0f);
    REQUIRE_THAT(impact.supply_impact, WithinAbs(1.0f, 0.001f));
    REQUIRE_THAT(impact.demand_impact, WithinAbs(0.0f, 0.001f));
}

// ===========================================================================
// Additional: Execute emits market deltas for open positions
// ===========================================================================

TEST_CASE("test_execute_emits_market_deltas_for_large_positions", "[commodity_trading][tier4]") {
    CommodityTradingModule module;
    auto state = make_test_world_state();

    // Add a province.
    Province prov{};
    prov.id = 0;
    state.provinces.push_back(prov);

    // Add a regional market: good_id=5, province=0, supply=1000, spot=50.
    state.regional_markets.push_back(make_test_market(5, 0, 1000.0f, 50.0f));

    // Open a large long position (200 units = 20% of supply, above 5% threshold).
    auto pos = make_test_position(1, 10, 5, 0, PositionType::long_position, 200.0f, 50.0f, 10);
    module.open_position(pos);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Should have emitted a market delta with demand_buffer_delta.
    REQUIRE(delta.market_deltas.size() == 1);
    REQUIRE(delta.market_deltas[0].good_id == 5);
    REQUIRE(delta.market_deltas[0].region_id == 0);
    REQUIRE(delta.market_deltas[0].demand_buffer_delta.has_value());
    REQUIRE_THAT(delta.market_deltas[0].demand_buffer_delta.value(), WithinAbs(1.5f, 0.001f));
}

// ===========================================================================
// Additional: Execute skips closed positions
// ===========================================================================

TEST_CASE("test_execute_skips_closed_positions", "[commodity_trading][tier4]") {
    CommodityTradingModule module;
    auto state = make_test_world_state();

    Province prov{};
    prov.id = 0;
    state.provinces.push_back(prov);

    state.regional_markets.push_back(make_test_market(5, 0, 1000.0f, 50.0f));

    // Open and immediately close a large position.
    auto pos = make_test_position(1, 10, 5, 0, PositionType::long_position, 200.0f, 50.0f, 10);
    module.open_position(pos);
    module.close_position(1, 70.0f, 50);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Closed position should not emit market deltas.
    REQUIRE(delta.market_deltas.size() == 0);
}

// ===========================================================================
// Additional: Execute updates current_value for open positions
// ===========================================================================

TEST_CASE("test_execute_updates_current_value", "[commodity_trading][tier4]") {
    CommodityTradingModule module;
    auto state = make_test_world_state();

    Province prov{};
    prov.id = 0;
    state.provinces.push_back(prov);

    // Spot price is 75 for good_id=5.
    state.regional_markets.push_back(make_test_market(5, 0, 10000.0f, 75.0f));

    // Open a small position (below impact threshold to avoid market delta noise).
    auto pos = make_test_position(1, 10, 5, 0, PositionType::long_position, 10.0f, 50.0f, 10);
    module.open_position(pos);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // current_value should be quantity * spot_price = 10 * 75 = 750.
    REQUIRE_THAT(module.positions()[0].current_value, WithinAbs(750.0f, 0.001f));
}

// ===========================================================================
// Additional: Closing non-existent position returns empty result
// ===========================================================================

TEST_CASE("test_close_nonexistent_position", "[commodity_trading][tier4]") {
    CommodityTradingModule module;

    // No positions opened; attempt to close id=999.
    SettlementResult result = module.close_position(999, 50.0f, 100);
    REQUIRE(result.position_closed == false);
    REQUIRE_THAT(result.realized_pnl, WithinAbs(0.0f, 0.001f));
    REQUIRE_THAT(result.capital_gains_tax, WithinAbs(0.0f, 0.001f));
}

// ===========================================================================
// Constants verification
// ===========================================================================

// ===========================================================================
// Settlement: execute() emits NPCDelta for position closed this tick
// ===========================================================================

TEST_CASE("test_execute_emits_npc_delta_for_settlement", "[commodity_trading][tier4]") {
    CommodityTradingModule module;
    auto state = make_test_world_state();

    Province prov{};
    prov.id = 0;
    state.provinces.push_back(prov);
    state.regional_markets.push_back(make_test_market(5, 0, 10000.0f, 50.0f));

    // Open a position, then close it at the current tick.
    auto pos = make_test_position(1, 10, 5, 0, PositionType::long_position, 100.0f, 50.0f, 10);
    module.open_position(pos);

    // Close at tick 100 (matches state.current_tick) with exit price 70.
    // P&L = (70 - 50) * 100 = 2000.
    module.close_position(1, 70.0f, 100);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Should emit an NPCDelta with capital_delta = 2000 for actor_id 10.
    bool found_pnl_delta = false;
    for (const auto& nd : delta.npc_deltas) {
        if (nd.npc_id == 10 && nd.capital_delta.has_value()) {
            REQUIRE_THAT(*nd.capital_delta, WithinAbs(2000.0f, 0.001f));
            found_pnl_delta = true;
        }
    }
    REQUIRE(found_pnl_delta);
}

// ===========================================================================
// Settlement: no NPCDelta for positions closed on earlier ticks
// ===========================================================================

TEST_CASE("test_execute_no_npc_delta_for_old_settlement", "[commodity_trading][tier4]") {
    CommodityTradingModule module;
    auto state = make_test_world_state();
    state.current_tick = 200;

    Province prov{};
    prov.id = 0;
    state.provinces.push_back(prov);
    state.regional_markets.push_back(make_test_market(5, 0, 10000.0f, 50.0f));

    // Open and close at tick 150 (not current tick 200).
    auto pos = make_test_position(1, 10, 5, 0, PositionType::long_position, 100.0f, 50.0f, 10);
    module.open_position(pos);
    module.close_position(1, 70.0f, 150);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Should NOT emit NPCDelta for this old settlement.
    for (const auto& nd : delta.npc_deltas) {
        if (nd.npc_id == 10 && nd.capital_delta.has_value()) {
            FAIL("Should not emit P&L delta for position closed on a previous tick");
        }
    }
}

// ===========================================================================
// Garbage collection: old closed positions are removed
// ===========================================================================

TEST_CASE("test_execute_garbage_collects_old_positions", "[commodity_trading][tier4]") {
    CommodityTradingModule module;
    auto state = make_test_world_state();
    state.current_tick = 200;

    Province prov{};
    prov.id = 0;
    state.provinces.push_back(prov);
    state.regional_markets.push_back(make_test_market(5, 0, 10000.0f, 50.0f));

    // Open and close a position at tick 100 (100 ticks ago, well past 30-tick GC window).
    auto pos = make_test_position(1, 10, 5, 0, PositionType::long_position, 100.0f, 50.0f, 10);
    module.open_position(pos);
    module.close_position(1, 70.0f, 100);

    REQUIRE(module.positions().size() == 1);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Old closed position should be garbage collected.
    REQUIRE(module.positions().size() == 0);
}

TEST_CASE("test_commodity_trading_constants", "[commodity_trading][tier4]") {
    REQUIRE_THAT(CommodityTradingConstants::market_impact_threshold, WithinAbs(0.05f, 0.0001f));
    REQUIRE_THAT(CommodityTradingConstants::market_impact_coefficient, WithinAbs(0.01f, 0.0001f));
    REQUIRE_THAT(CommodityTradingConstants::capital_gains_tax_rate, WithinAbs(0.15f, 0.0001f));
}
