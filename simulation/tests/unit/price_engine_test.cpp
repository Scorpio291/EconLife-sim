// Price engine module unit tests.
// All tests tagged [price_engine][tier3].
//
// Tests verify the 3-step supply-demand equilibrium algorithm:
//   Step 1: Equilibrium from supply/demand ratio with floor/ceiling clamps
//   Step 2: Sticky price adjustment with max change cap
//   Step 3: LOD 2 global commodity modifier

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"
#include "modules/price_engine/price_engine_module.h"
#include "modules/price_engine/price_engine_types.h"
#include "core/config/package_config.h"
#include "modules/trade_infrastructure/trade_types.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// ---------------------------------------------------------------------------
// Test helpers — create minimal WorldState and supporting structures
// ---------------------------------------------------------------------------

namespace {

// Create a minimal WorldState suitable for price engine tests.
WorldState make_test_world_state() {
    WorldState state{};
    state.current_tick = 1;
    state.world_seed = 42;
    state.player.reset();
    state.lod2_price_index.reset();
    state.ticks_this_session = 1;
    state.game_mode = GameMode::standard;
    state.current_schema_version = 1;
    state.network_health_dirty = false;
    return state;
}

// Create a RegionalMarket with sensible defaults.
// base_price is proxied via equilibrium_price (RegionalMarket has no base_price field).
RegionalMarket make_test_market(uint32_t good_id, uint32_t province_id, float supply,
                                float demand_buffer, float spot_price, float equilibrium_price) {
    RegionalMarket market{};
    market.good_id = good_id;
    market.province_id = province_id;
    market.spot_price = spot_price;
    market.equilibrium_price = equilibrium_price;
    market.adjustment_rate = PriceEngineConfig{}.default_price_adjustment_rate;
    market.supply = supply;
    market.demand_buffer = demand_buffer;
    market.import_price_ceiling = 0.0f;
    market.export_price_floor = 0.0f;
    return market;
}

// Find the MarketDelta for a given good_id in the delta buffer.
const MarketDelta* find_market_delta(const DeltaBuffer& delta, uint32_t good_id,
                                     uint32_t province_id) {
    for (const auto& md : delta.market_deltas) {
        if (md.good_id == good_id && md.region_id == province_id) {
            return &md;
        }
    }
    return nullptr;
}

}  // anonymous namespace

// ===========================================================================
// Step 1: Equilibrium Price Tests
// ===========================================================================

TEST_CASE("test_equilibrium_price_from_supply_demand_ratio", "[price_engine][tier3]") {
    // demand/supply ratio drives equilibrium price.
    // base_price (proxy: equilibrium_price) = 10.0
    // supply = 100, demand = 200 => ratio = 2.0
    // eq_price = 10.0 * 2.0 = 20.0
    // ceiling = 10.0 * 3.0 = 30.0, so 20.0 is within bounds.
    RegionalMarket market = make_test_market(1, 0, 100.0f, 200.0f, 10.0f, 10.0f);

    PriceEngineModule mod;
    float eq = mod.compute_equilibrium_price(market);
    REQUIRE_THAT(eq, WithinAbs(20.0f, 0.001f));
}

TEST_CASE("test_price_floor_clamp_via_export_price_floor", "[price_engine][tier3]") {
    // When export_price_floor is set, it overrides the default floor.
    // base_price = 10.0, supply = 1000, demand = 1 => ratio = 0.001
    // Raw eq = 10.0 * 0.001 = 0.01 (very low)
    // export_price_floor = 5.0 => clamped to 5.0
    RegionalMarket market = make_test_market(1, 0, 1000.0f, 1.0f, 10.0f, 10.0f);
    market.export_price_floor = 5.0f;

    PriceEngineModule mod;
    float eq = mod.compute_equilibrium_price(market);
    REQUIRE_THAT(eq, WithinAbs(5.0f, 0.001f));
}

TEST_CASE("test_price_floor_clamp_default_coefficient", "[price_engine][tier3]") {
    // When no export_price_floor set (0.0), use base_price * export_floor_coeff (0.40).
    // base_price = 10.0, floor = 10.0 * 0.40 = 4.0
    // supply = 1000, demand = 1 => ratio = 0.001
    // Raw eq = 10.0 * 0.001 = 0.01, clamped to 4.0
    RegionalMarket market = make_test_market(1, 0, 1000.0f, 1.0f, 10.0f, 10.0f);

    PriceEngineModule mod;
    float eq = mod.compute_equilibrium_price(market);
    REQUIRE_THAT(eq, WithinAbs(4.0f, 0.001f));
}

TEST_CASE("test_price_ceiling_clamp_via_import_price_ceiling", "[price_engine][tier3]") {
    // When import_price_ceiling is set, it overrides the default ceiling.
    // base_price = 10.0, supply = 1, demand = 1000 => ratio = 1000
    // Raw eq = 10.0 * 1000 = 10000 (very high)
    // import_price_ceiling = 15.0 => clamped to 15.0
    RegionalMarket market = make_test_market(1, 0, 1.0f, 1000.0f, 10.0f, 10.0f);
    market.import_price_ceiling = 15.0f;

    PriceEngineModule mod;
    float eq = mod.compute_equilibrium_price(market);
    REQUIRE_THAT(eq, WithinAbs(15.0f, 0.001f));
}

TEST_CASE("test_price_ceiling_clamp_default_coefficient", "[price_engine][tier3]") {
    // When no import_price_ceiling set (0.0), use base_price * import_ceiling_coeff (3.0).
    // base_price = 10.0, ceiling = 10.0 * 3.0 = 30.0
    // supply = 1, demand = 1000 => ratio = 1000
    // Raw eq = 10.0 * 1000 = 10000, clamped to 30.0
    RegionalMarket market = make_test_market(1, 0, 1.0f, 1000.0f, 10.0f, 10.0f);

    PriceEngineModule mod;
    float eq = mod.compute_equilibrium_price(market);
    REQUIRE_THAT(eq, WithinAbs(30.0f, 0.001f));
}

TEST_CASE("test_zero_supply_uses_supply_floor", "[price_engine][tier3]") {
    // Zero supply should use SUPPLY_FLOOR (0.01) to prevent division by zero.
    // base_price = 10.0, supply = 0.0, demand = 5.0
    // effective_supply = max(0.0, 0.01) = 0.01
    // ratio = 5.0 / 0.01 = 500.0
    // Raw eq = 10.0 * 500.0 = 5000.0, clamped to ceiling = 30.0
    RegionalMarket market = make_test_market(1, 0, 0.0f, 5.0f, 10.0f, 10.0f);

    PriceEngineModule mod;
    float eq = mod.compute_equilibrium_price(market);
    REQUIRE_THAT(eq, WithinAbs(30.0f, 0.001f));
}

// ===========================================================================
// Step 2: Sticky Adjustment Tests
// ===========================================================================

TEST_CASE("test_sticky_adjustment_moves_toward_equilibrium", "[price_engine][tier3]") {
    // spot = 10.0, eq = 20.0, rate = 0.10
    // gap = 20.0 - 10.0 = 10.0
    // adjustment = 10.0 * 0.10 = 1.0
    // max_change = 0.25 * 10.0 = 2.5 (1.0 < 2.5, not capped)
    // new_spot = 10.0 + 1.0 = 11.0
    PriceEngineModule mod;
    float new_spot = mod.compute_sticky_adjustment(10.0f, 20.0f, 0.10f);
    REQUIRE_THAT(new_spot, WithinAbs(11.0f, 0.001f));
}

TEST_CASE("test_sticky_adjustment_downward", "[price_engine][tier3]") {
    // spot = 20.0, eq = 10.0, rate = 0.10
    // gap = 10.0 - 20.0 = -10.0
    // adjustment = -10.0 * 0.10 = -1.0
    // max_change = 0.25 * 20.0 = 5.0 (|-1.0| < 5.0, not capped)
    // new_spot = 20.0 + (-1.0) = 19.0
    PriceEngineModule mod;
    float new_spot = mod.compute_sticky_adjustment(20.0f, 10.0f, 0.10f);
    REQUIRE_THAT(new_spot, WithinAbs(19.0f, 0.001f));
}

TEST_CASE("test_max_price_change_per_tick_cap", "[price_engine][tier3]") {
    // spot = 10.0, eq = 100.0, rate = 0.50 (aggressive rate for testing)
    // gap = 100.0 - 10.0 = 90.0
    // adjustment = 90.0 * 0.50 = 45.0
    // max_change = 0.25 * 10.0 = 2.5 (45.0 > 2.5, CAPPED)
    // new_spot = 10.0 + 2.5 = 12.5
    PriceEngineModule mod;
    float new_spot = mod.compute_sticky_adjustment(10.0f, 100.0f, 0.50f);
    REQUIRE_THAT(new_spot, WithinAbs(12.5f, 0.001f));
}

TEST_CASE("test_max_price_change_cap_downward", "[price_engine][tier3]") {
    // spot = 100.0, eq = 10.0, rate = 0.50
    // gap = 10.0 - 100.0 = -90.0
    // adjustment = -90.0 * 0.50 = -45.0
    // max_change = 0.25 * 100.0 = 25.0 (|-45.0| > 25.0, CAPPED to -25.0)
    // new_spot = 100.0 + (-25.0) = 75.0
    PriceEngineModule mod;
    float new_spot = mod.compute_sticky_adjustment(100.0f, 10.0f, 0.50f);
    REQUIRE_THAT(new_spot, WithinAbs(75.0f, 0.001f));
}

TEST_CASE("test_spot_price_never_negative", "[price_engine][tier3]") {
    // spot = 1.0, eq = 0.0 (weird but valid), rate = 0.50
    // gap = 0.0 - 1.0 = -1.0
    // adjustment = -1.0 * 0.50 = -0.5
    // max_change = 0.25 * 1.0 = 0.25 (|-0.5| > 0.25, CAPPED to -0.25)
    // new_spot = 1.0 + (-0.25) = 0.75 (positive, OK)
    PriceEngineModule mod;
    float result = mod.compute_sticky_adjustment(1.0f, 0.0f, 0.50f);
    REQUIRE(result >= 0.0f);

    // More extreme: spot = 0.01, eq = 0.0, rate = 1.0
    // gap = -0.01, adjustment = -0.01 * 1.0 = -0.01
    // max_change = 0.25 * 0.01 = 0.0025 (|-0.01| > 0.0025, CAPPED to -0.0025)
    // new_spot = 0.01 - 0.0025 = 0.0075 (positive)
    float result2 = mod.compute_sticky_adjustment(0.01f, 0.0f, 1.0f);
    REQUIRE(result2 >= 0.0f);

    // Spot = 0.0 should stay at 0.0.
    float result3 = mod.compute_sticky_adjustment(0.0f, 0.0f, 0.10f);
    REQUIRE_THAT(result3, WithinAbs(0.0f, 0.0001f));
}

// ===========================================================================
// Step 3: LOD 2 Modifier Tests
// ===========================================================================

TEST_CASE("test_lod2_modifier_applied", "[price_engine][tier3]") {
    // When lod2_price_index has an entry for the good_id, the modifier
    // should be returned.
    GlobalCommodityPriceIndex index{};
    index.last_updated_tick = 0;
    index.lod2_price_modifier[42] = 1.5f;

    float modifier = PriceEngineModule::get_lod2_modifier(42, &index);
    REQUIRE_THAT(modifier, WithinAbs(1.5f, 0.001f));
}

TEST_CASE("test_lod2_modifier_missing_is_identity", "[price_engine][tier3]") {
    // When lod2_price_index exists but has no entry for the good_id,
    // modifier should be 1.0 (identity).
    GlobalCommodityPriceIndex index{};
    index.last_updated_tick = 0;
    index.lod2_price_modifier[99] = 2.0f;  // different good_id

    float modifier = PriceEngineModule::get_lod2_modifier(42, &index);
    REQUIRE_THAT(modifier, WithinAbs(1.0f, 0.001f));
}

TEST_CASE("test_lod2_modifier_null_index_is_identity", "[price_engine][tier3]") {
    // When lod2_price_index is nullptr, modifier should be 1.0.
    float modifier = PriceEngineModule::get_lod2_modifier(42, nullptr);
    REQUIRE_THAT(modifier, WithinAbs(1.0f, 0.001f));
}

// ===========================================================================
// Integration Tests — full execute_province path
// ===========================================================================

TEST_CASE("test_execute_province_writes_market_deltas", "[price_engine][tier3]") {
    // Set up a province with one market and verify execute_province produces
    // correct MarketDelta entries.
    auto state = make_test_world_state();
    constexpr uint32_t province_id = 0;

    Province prov{};
    prov.id = province_id;
    state.provinces.push_back(prov);

    // Market: base_price=10, supply=100, demand=100, spot=10
    // Balanced market: ratio = 100/100 = 1.0, eq = 10.0 * 1.0 = 10.0
    // Spot already at eq => adjustment = 0 => new_spot = 10.0
    state.regional_markets.push_back(
        make_test_market(1, province_id, 100.0f, 100.0f, 10.0f, 10.0f));

    PriceEngineModule module;
    DeltaBuffer delta{};
    module.execute_province(province_id, state, delta);

    REQUIRE(delta.market_deltas.size() == 1);
    const auto& md = delta.market_deltas[0];
    REQUIRE(md.good_id == 1);
    REQUIRE(md.region_id == province_id);
    REQUIRE(md.spot_price_override.has_value());
    // equilibrium_price is no longer overridden — it's kept as stable base reference.
    REQUIRE_FALSE(md.equilibrium_price_override.has_value());
    REQUIRE_THAT(md.spot_price_override.value(), WithinAbs(10.0f, 0.001f));
}

TEST_CASE("test_execute_province_high_demand", "[price_engine][tier3]") {
    // High demand scenario: price should increase.
    auto state = make_test_world_state();
    constexpr uint32_t province_id = 0;

    Province prov{};
    prov.id = province_id;
    state.provinces.push_back(prov);

    // base_price=10, supply=50, demand=100 => ratio=2.0, eq=20.0
    // spot=10, gap=10, adj=1.0, new_spot=11.0
    state.regional_markets.push_back(make_test_market(1, province_id, 50.0f, 100.0f, 10.0f, 10.0f));

    PriceEngineModule module;
    DeltaBuffer delta{};
    module.execute_province(province_id, state, delta);

    REQUIRE(delta.market_deltas.size() == 1);
    const auto& md = delta.market_deltas[0];
    // equilibrium_price is no longer overridden — stable base reference.
    REQUIRE_THAT(md.spot_price_override.value(), WithinAbs(11.0f, 0.001f));
}

TEST_CASE("test_execute_province_with_lod2_modifier", "[price_engine][tier3]") {
    // Verify LOD 2 modifier is applied in the full execution path.
    auto state = make_test_world_state();
    constexpr uint32_t province_id = 0;

    Province prov{};
    prov.id = province_id;
    state.provinces.push_back(prov);

    // Balanced market: eq = 10.0, spot at eq => new_spot before LOD2 = 10.0
    // LOD2 modifier = 1.2 => new_spot = 10.0 * 1.2 = 12.0
    state.regional_markets.push_back(
        make_test_market(1, province_id, 100.0f, 100.0f, 10.0f, 10.0f));

    GlobalCommodityPriceIndex lod2_index{};
    lod2_index.last_updated_tick = 0;
    lod2_index.lod2_price_modifier[1] = 1.2f;
    state.lod2_price_index = std::make_unique<GlobalCommodityPriceIndex>(lod2_index);

    PriceEngineModule module;
    DeltaBuffer delta{};
    module.execute_province(province_id, state, delta);

    REQUIRE(delta.market_deltas.size() == 1);
    REQUIRE_THAT(delta.market_deltas[0].spot_price_override.value(), WithinAbs(12.0f, 0.001f));
}

TEST_CASE("test_execute_province_multiple_goods_canonical_order", "[price_engine][tier3]") {
    // Multiple goods should be processed in good_id ascending order.
    // Verify deltas appear in that order.
    auto state = make_test_world_state();
    constexpr uint32_t province_id = 0;

    Province prov{};
    prov.id = province_id;
    state.provinces.push_back(prov);

    // Insert markets in reverse good_id order.
    state.regional_markets.push_back(
        make_test_market(30, province_id, 100.0f, 100.0f, 10.0f, 10.0f));
    state.regional_markets.push_back(make_test_market(10, province_id, 100.0f, 100.0f, 5.0f, 5.0f));
    state.regional_markets.push_back(make_test_market(20, province_id, 100.0f, 100.0f, 8.0f, 8.0f));

    PriceEngineModule module;
    DeltaBuffer delta{};
    module.execute_province(province_id, state, delta);

    REQUIRE(delta.market_deltas.size() == 3);
    REQUIRE(delta.market_deltas[0].good_id == 10);
    REQUIRE(delta.market_deltas[1].good_id == 20);
    REQUIRE(delta.market_deltas[2].good_id == 30);
}

TEST_CASE("test_execute_fallback_processes_all_provinces", "[price_engine][tier3]") {
    // The execute() fallback should process all provinces sequentially.
    auto state = make_test_world_state();

    Province prov0{};
    prov0.id = 0;
    Province prov1{};
    prov1.id = 1;
    state.provinces.push_back(prov0);
    state.provinces.push_back(prov1);

    state.regional_markets.push_back(make_test_market(1, 0, 100.0f, 100.0f, 10.0f, 10.0f));
    state.regional_markets.push_back(make_test_market(1, 1, 50.0f, 100.0f, 10.0f, 10.0f));

    PriceEngineModule module;
    DeltaBuffer delta{};
    module.execute(state, delta);

    // Should have 2 deltas: one per province.
    REQUIRE(delta.market_deltas.size() == 2);

    // Province 0: balanced => eq=10, spot=10.
    auto* md0 = find_market_delta(delta, 1, 0);
    REQUIRE(md0 != nullptr);
    REQUIRE_THAT(md0->spot_price_override.value(), WithinAbs(10.0f, 0.001f));

    // Province 1: supply shortage => eq=20, spot moves up.
    auto* md1 = find_market_delta(delta, 1, 1);
    REQUIRE(md1 != nullptr);
    // equilibrium_price is no longer overridden — stable base reference.
    REQUIRE_THAT(md1->spot_price_override.value(), WithinAbs(11.0f, 0.001f));
}

TEST_CASE("test_default_adjustment_rate_used_when_market_rate_zero", "[price_engine][tier3]") {
    // If the market's adjustment_rate is zero or negative, the module
    // should fall back to PriceEngineConfig{}.default_price_adjustment_rate.
    auto state = make_test_world_state();
    constexpr uint32_t province_id = 0;

    Province prov{};
    prov.id = province_id;
    state.provinces.push_back(prov);

    // base=10, supply=50, demand=100 => ratio=2, eq=20
    // spot=10, with default rate 0.10: gap=10, adj=1.0, new_spot=11.0
    auto market = make_test_market(1, province_id, 50.0f, 100.0f, 10.0f, 10.0f);
    market.adjustment_rate = 0.0f;  // force fallback
    state.regional_markets.push_back(market);

    PriceEngineModule module;
    DeltaBuffer delta{};
    module.execute_province(province_id, state, delta);

    REQUIRE(delta.market_deltas.size() == 1);
    REQUIRE_THAT(delta.market_deltas[0].spot_price_override.value(), WithinAbs(11.0f, 0.001f));
}

// ===========================================================================
// Module Interface Properties
// ===========================================================================

TEST_CASE("test_module_interface_properties", "[price_engine][tier3]") {
    PriceEngineModule module;

    REQUIRE(module.name() == "price_engine");
    REQUIRE(module.package_id() == "base_game");
    REQUIRE(module.scope() == ModuleScope::v1);
    REQUIRE(module.is_province_parallel() == true);

    auto after = module.runs_after();
    REQUIRE(after.size() == 4);
    REQUIRE(after[0] == "supply_chain");
    REQUIRE(after[1] == "labor_market");
    REQUIRE(after[2] == "seasonal_agriculture");
    REQUIRE(after[3] == "npc_spending");

    auto before = module.runs_before();
    REQUIRE(before.size() == 1);
    REQUIRE(before[0] == "financial_distribution");
}

// ===========================================================================
// Constants Verification
// ===========================================================================

TEST_CASE("test_price_engine_constants", "[price_engine][tier3]") {
    REQUIRE_THAT(PriceEngineConfig{}.supply_floor, WithinAbs(0.01f, 0.0001f));
    REQUIRE_THAT(PriceEngineConfig{}.default_price_adjustment_rate, WithinAbs(0.10f, 0.0001f));
    REQUIRE_THAT(PriceEngineConfig{}.max_price_change_per_tick, WithinAbs(0.25f, 0.0001f));
    REQUIRE_THAT(PriceEngineConfig{}.export_floor_coeff, WithinAbs(0.40f, 0.0001f));
    REQUIRE_THAT(PriceEngineConfig{}.import_ceiling_coeff, WithinAbs(3.0f, 0.0001f));
    REQUIRE_THAT(PriceEngineConfig{}.default_base_price, WithinAbs(1.0f, 0.0001f));
}
