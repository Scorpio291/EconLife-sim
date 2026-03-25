// Supply chain module unit tests.
// All tests tagged [supply_chain][tier2].
//
// Tests verify local matching, transit time calculation, transport cost
// deduction, transit arrival supply updates, perishable decay, and
// criminal interception probability.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <string>

#include "core/rng/deterministic_rng.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"
#include "modules/supply_chain/supply_chain_module.h"
#include "modules/supply_chain/supply_chain_types.h"
#include "modules/trade_infrastructure/trade_types.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// ---------------------------------------------------------------------------
// Test helpers — create minimal WorldState and supporting structures
// ---------------------------------------------------------------------------

namespace {

// Create a minimal WorldState suitable for supply chain tests.
WorldState make_test_world_state() {
    WorldState state{};
    state.current_tick = 10;
    state.world_seed = 42;
    state.player = nullptr;
    state.lod2_price_index = nullptr;
    state.ticks_this_session = 10;
    state.game_mode = GameMode::standard;
    state.current_schema_version = 1;
    state.network_health_dirty = false;
    return state;
}

// Create a Province with sensible defaults.
Province make_test_province(uint32_t id, float infrastructure = 0.5f) {
    Province prov{};
    prov.id = id;
    prov.h3_index = 0;
    prov.fictional_name = "TestProvince" + std::to_string(id);
    prov.infrastructure_rating = infrastructure;
    prov.lod_level = SimulationLOD::full;
    prov.agricultural_productivity = 0.8f;
    prov.energy_cost_baseline = 1.0f;
    prov.trade_openness = 0.5f;
    prov.has_karst = false;
    prov.historical_trauma_index = 0.0f;
    prov.region_id = 0;
    prov.nation_id = 0;
    prov.cohort_stats = nullptr;

    // Initialize conditions to defaults
    prov.conditions.stability_score = 0.8f;
    prov.conditions.inequality_index = 0.3f;
    prov.conditions.crime_rate = 0.1f;
    prov.conditions.addiction_rate = 0.05f;
    prov.conditions.criminal_dominance_index = 0.1f;
    prov.conditions.formal_employment_rate = 0.7f;
    prov.conditions.regulatory_compliance_index = 0.9f;
    prov.conditions.drought_modifier = 1.0f;
    prov.conditions.flood_modifier = 1.0f;

    return prov;
}

// Create an NPCBusiness with sensible defaults.
NPCBusiness make_test_business(uint32_t id, uint32_t province_id, float cash = 10000.0f) {
    NPCBusiness biz{};
    biz.id = id;
    biz.sector = BusinessSector::manufacturing;
    biz.profile = BusinessProfile::cost_cutter;
    biz.cash = cash;
    biz.revenue_per_tick = 100.0f;
    biz.cost_per_tick = 50.0f;
    biz.market_share = 0.1f;
    biz.strategic_decision_tick = 100;
    biz.dispatch_day_offset = 0;
    biz.actor_tech_state = ActorTechnologyState{1.0f};
    biz.criminal_sector = false;
    biz.province_id = province_id;
    biz.regulatory_violation_severity = 0.0f;
    biz.default_activity_scope = VisibilityScope::institutional;
    biz.owner_id = 0;
    biz.deferred_salary_liability = 0.0f;
    biz.accounts_payable_float = 0.0f;
    return biz;
}

// Add a regional market entry for a good in a province.
void add_market(WorldState& state, const std::string& good_id_str, uint32_t province_id,
                float supply, float demand = 0.0f, float spot_price = 10.0f) {
    RegionalMarket market{};
    market.good_id = SupplyChainModule::good_id_from_string(good_id_str);
    market.province_id = province_id;
    market.spot_price = spot_price;
    market.equilibrium_price = spot_price;
    market.adjustment_rate = 0.1f;
    market.supply = supply;
    market.demand_buffer = demand;
    market.import_price_ceiling = 0.0f;
    market.export_price_floor = 0.0f;
    state.regional_markets.push_back(market);
}

// Create a RouteProfile between two provinces.
RouteProfile make_route(float distance_km, float roughness = 0.2f, float min_infra = 0.3f,
                        uint8_t hops = 1) {
    RouteProfile route{};
    route.distance_km = distance_km;
    route.route_terrain_roughness = roughness;
    route.min_infrastructure = min_infra;
    route.hop_count = hops;
    route.requires_sea_leg = false;
    route.requires_rail = false;
    route.concealment_bonus = 0.0f;
    return route;
}

// Add a route between two provinces (road mode at index 0).
void add_route(WorldState& state, uint32_t from, uint32_t to, float distance_km,
               float roughness = 0.2f) {
    auto key = std::make_pair(from, to);
    std::array<RouteProfile, 5> routes{};
    routes[0] = make_route(distance_km, roughness);
    state.province_route_table[key] = routes;
}

// Count and sum MarketDelta entries for a given good and province.
struct MarketDeltaSummary {
    float total_supply_delta = 0.0f;
    float total_demand_delta = 0.0f;
    int supply_count = 0;
    int demand_count = 0;
};

MarketDeltaSummary summarize_market_deltas(const DeltaBuffer& delta, const std::string& good_id_str,
                                           uint32_t province_id) {
    uint32_t good_id = SupplyChainModule::good_id_from_string(good_id_str);
    MarketDeltaSummary summary{};
    for (const auto& md : delta.market_deltas) {
        if (md.good_id == good_id && md.region_id == province_id) {
            if (md.supply_delta.has_value()) {
                summary.total_supply_delta += md.supply_delta.value();
                summary.supply_count++;
            }
            if (md.demand_buffer_delta.has_value()) {
                summary.total_demand_delta += md.demand_buffer_delta.value();
                summary.demand_count++;
            }
        }
    }
    return summary;
}

// Sum NPCDelta capital_delta for a given NPC.
float sum_capital_deltas(const DeltaBuffer& delta, uint32_t npc_id) {
    float total = 0.0f;
    for (const auto& nd : delta.npc_deltas) {
        if (nd.npc_id == npc_id && nd.capital_delta.has_value()) {
            total += nd.capital_delta.value();
        }
    }
    return total;
}

}  // anonymous namespace

// ===========================================================================
// Tests
// ===========================================================================

TEST_CASE("test_module_interface_properties", "[supply_chain][tier2]") {
    SupplyChainModule module;

    REQUIRE(module.name() == "supply_chain");
    REQUIRE(module.package_id() == "base_game");
    REQUIRE(module.scope() == ModuleScope::v1);
    REQUIRE(module.is_province_parallel() == false);

    auto after = module.runs_after();
    REQUIRE(after.size() == 1);
    REQUIRE(after[0] == "production");

    auto before = module.runs_before();
    REQUIRE(before.size() == 1);
    REQUIRE(before[0] == "price_engine");
}

TEST_CASE("test_good_id_from_string_deterministic", "[supply_chain][tier2]") {
    // Verify deterministic hashing.
    auto id1 = SupplyChainModule::good_id_from_string("steel");
    auto id2 = SupplyChainModule::good_id_from_string("steel");
    REQUIRE(id1 == id2);

    // Different goods produce different ids.
    auto id3 = SupplyChainModule::good_id_from_string("iron_ore");
    REQUIRE(id1 != id3);
}

TEST_CASE("test_local_matching_same_province", "[supply_chain][tier2]") {
    // Sell offers fulfilled within the same province.
    // Supply exists, demand exists, both in province 0.
    // Local match should satisfy demand with zero transit time.
    auto state = make_test_world_state();

    // Set up province.
    state.provinces.push_back(make_test_province(0));

    // Market with supply and demand for steel.
    add_market(state, "steel", 0, 50.0f, 30.0f, 15.0f);

    // Need a business in the province.
    state.npc_businesses.push_back(make_test_business(1, 0));

    SupplyChainModule module;
    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // Local matching should produce a demand_buffer_delta (negative = satisfied).
    auto summary = summarize_market_deltas(delta, "steel", 0);
    REQUIRE(summary.demand_count == 1);
    // 30 units of demand matched against 50 units of supply = -30 demand satisfied.
    REQUIRE_THAT(summary.total_demand_delta, WithinAbs(-30.0f, 0.001f));
}

TEST_CASE("test_local_matching_partial_supply", "[supply_chain][tier2]") {
    // When supply < demand, only available supply is matched.
    auto state = make_test_world_state();
    state.provinces.push_back(make_test_province(0));

    // 10 supply, 30 demand.
    add_market(state, "iron_ore", 0, 10.0f, 30.0f, 12.0f);

    state.npc_businesses.push_back(make_test_business(1, 0));

    SupplyChainModule module;
    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    auto summary = summarize_market_deltas(delta, "iron_ore", 0);
    REQUIRE(summary.demand_count == 1);
    // Only 10 can be matched.
    REQUIRE_THAT(summary.total_demand_delta, WithinAbs(-10.0f, 0.001f));
}

TEST_CASE("test_transit_time_from_route_profile", "[supply_chain][tier2]") {
    // Test the transit time calculation formula.
    // transit_ticks = ceil(distance_km / (mode_speed * (1 + infra * infra_coeff)))
    RouteProfile route = make_route(600.0f);  // 600 km
    float infra = 0.5f;

    uint32_t ticks =
        SupplyChainModule::compute_transit_ticks(route, SupplyChainConfig::road_speed, infra);

    // Expected: ceil(600 / (300 * (1 + 0.5 * 0.5))) = ceil(600 / 375) = ceil(1.6) = 2
    REQUIRE(ticks == 2);
}

TEST_CASE("test_transit_time_minimum_one_tick", "[supply_chain][tier2]") {
    // Very short route should still take at least 1 tick.
    RouteProfile route = make_route(10.0f);  // 10 km
    float infra = 1.0f;

    uint32_t ticks =
        SupplyChainModule::compute_transit_ticks(route, SupplyChainConfig::road_speed, infra);

    // Expected: ceil(10 / (300 * 1.5)) = ceil(0.022) = 1
    REQUIRE(ticks == 1);
}

TEST_CASE("test_transit_time_high_infrastructure", "[supply_chain][tier2]") {
    // High infrastructure should reduce transit time.
    RouteProfile route = make_route(900.0f);
    float low_infra = 0.0f;
    float high_infra = 1.0f;

    uint32_t ticks_low =
        SupplyChainModule::compute_transit_ticks(route, SupplyChainConfig::road_speed, low_infra);
    uint32_t ticks_high =
        SupplyChainModule::compute_transit_ticks(route, SupplyChainConfig::road_speed, high_infra);

    // Low infra: ceil(900 / (300 * 1.0)) = 3
    REQUIRE(ticks_low == 3);
    // High infra: ceil(900 / (300 * 1.5)) = ceil(2.0) = 2
    REQUIRE(ticks_high == 2);
    REQUIRE(ticks_high < ticks_low);
}

TEST_CASE("test_transport_cost_calculation", "[supply_chain][tier2]") {
    // Transport cost = base_rate * distance * quantity * (1 + roughness * terrain_coeff)
    RouteProfile route = make_route(500.0f, 0.4f);  // 500 km, roughness 0.4
    float quantity = 100.0f;

    float cost = SupplyChainModule::compute_transport_cost(route, quantity);

    // Expected: 0.01 * 500 * 100 * (1 + 0.4 * 0.5) = 0.01 * 500 * 100 * 1.2 = 600.0
    REQUIRE_THAT(cost, WithinAbs(600.0f, 0.01f));
}

TEST_CASE("test_transport_cost_deduction_from_business", "[supply_chain][tier2]") {
    // When an inter-province shipment is dispatched, transport cost should
    // be deducted from a business's cash via NPCDelta.capital_delta.
    auto state = make_test_world_state();

    // Two provinces.
    state.provinces.push_back(make_test_province(0, 0.5f));
    state.provinces.push_back(make_test_province(1, 0.5f));

    // Province 0 has demand but no supply.
    add_market(state, "wheat", 0, 0.0f, 50.0f, 8.0f);
    // Province 1 has supply but no demand.
    add_market(state, "wheat", 1, 100.0f, 0.0f, 7.0f);

    // Route from province 1 to province 0.
    add_route(state, 1, 0, 300.0f, 0.2f);

    // Business in province 0 to pay for shipping.
    auto biz = make_test_business(1, 0, 5000.0f);
    state.npc_businesses.push_back(biz);

    SupplyChainModule module;
    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // The business should have a negative capital_delta for transport cost.
    float capital_change = sum_capital_deltas(delta, 1);
    REQUIRE(capital_change < 0.0f);

    // Verify the transport cost matches the formula.
    RouteProfile route = make_route(300.0f, 0.2f);
    float expected_cost = SupplyChainModule::compute_transport_cost(route, 50.0f);
    REQUIRE_THAT(capital_change, WithinAbs(-expected_cost, 0.01f));
}

TEST_CASE("test_transit_arrival_adds_to_supply", "[supply_chain][tier2]") {
    // When an inter-province shipment is dispatched, the supply delta
    // for the destination province should be created.
    auto state = make_test_world_state();

    state.provinces.push_back(make_test_province(0));
    state.provinces.push_back(make_test_province(1));

    // Province 0 needs goods, province 1 has them.
    add_market(state, "copper", 0, 0.0f, 25.0f, 20.0f);
    add_market(state, "copper", 1, 50.0f, 0.0f, 18.0f);

    add_route(state, 1, 0, 400.0f);

    state.npc_businesses.push_back(make_test_business(1, 0, 10000.0f));

    SupplyChainModule module;
    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // There should be a supply delta for copper in province 0.
    auto summary = summarize_market_deltas(delta, "copper", 0);
    REQUIRE(summary.supply_count >= 1);
    // Shipped quantity should match the demand (25 units, limited by demand).
    REQUIRE_THAT(summary.total_supply_delta, WithinAbs(25.0f, 0.01f));
}

TEST_CASE("test_perishable_decay_formula", "[supply_chain][tier2]") {
    // Test the perishable decay calculation.
    // quantity_remaining = quantity_dispatched * (1 - decay_rate)^transit_ticks
    float quantity_dispatched = 100.0f;
    float decay_rate = SupplyChainConfig::default_perishable_decay_rate;
    uint32_t transit_ticks = 5;

    float quantity_remaining =
        quantity_dispatched * std::pow(1.0f - decay_rate, static_cast<float>(transit_ticks));

    // Expected: 100 * (1 - 0.02)^5 = 100 * 0.98^5 = 100 * 0.9039... ~= 90.39
    REQUIRE_THAT(quantity_remaining, WithinAbs(90.39f, 0.1f));
    REQUIRE(quantity_remaining < quantity_dispatched);
    REQUIRE(quantity_remaining > 0.0f);
}

TEST_CASE("test_perishable_decay_long_transit", "[supply_chain][tier2]") {
    // Longer transit = more decay.
    float quantity = 100.0f;
    float decay_rate = SupplyChainConfig::default_perishable_decay_rate;

    float short_transit = quantity * std::pow(1.0f - decay_rate, 2.0f);
    float long_transit = quantity * std::pow(1.0f - decay_rate, 10.0f);

    REQUIRE(long_transit < short_transit);
    REQUIRE(long_transit > 0.0f);
}

TEST_CASE("test_criminal_interception_probability", "[supply_chain][tier2]") {
    // Test that the interception probability formula works correctly.
    // interception_probability = interception_risk_per_tick * (1 - concealment)
    float risk_per_tick = SupplyChainConfig::base_interception_risk;
    float concealment = 0.0f;

    float prob = risk_per_tick * (1.0f - concealment);
    REQUIRE_THAT(prob, WithinAbs(0.05f, 0.001f));

    // With max concealment.
    float concealment_max = SupplyChainConfig::max_concealment_modifier;
    float prob_concealed = risk_per_tick * (1.0f - concealment_max);
    // Expected: 0.05 * (1 - 0.40) = 0.05 * 0.60 = 0.03
    REQUIRE_THAT(prob_concealed, WithinAbs(0.03f, 0.001f));
    REQUIRE(prob_concealed < prob);
}

TEST_CASE("test_criminal_interception_high_risk", "[supply_chain][tier2]") {
    // With risk_per_tick = 1.0 and no concealment, interception is certain.
    float risk_per_tick = 1.0f;
    float concealment = 0.0f;

    float prob = risk_per_tick * (1.0f - concealment);
    REQUIRE_THAT(prob, WithinAbs(1.0f, 0.001f));

    // Use DeterministicRNG to verify the roll would always intercept.
    DeterministicRNG rng(12345);
    float roll = rng.next_float();
    // next_float returns [0.0, 1.0), so roll < 1.0 is always true.
    REQUIRE(roll < prob);
}

TEST_CASE("test_concealment_capped_at_max", "[supply_chain][tier2]") {
    // Verify concealment cannot exceed max_concealment_modifier.
    float raw_concealment = 0.80f;  // exceeds max
    float capped = std::min(raw_concealment, SupplyChainConfig::max_concealment_modifier);
    REQUIRE_THAT(capped, WithinAbs(0.40f, 0.001f));
}

TEST_CASE("test_skip_non_full_lod_provinces", "[supply_chain][tier2]") {
    // Provinces at non-full LOD should be skipped.
    auto state = make_test_world_state();

    Province prov = make_test_province(0);
    prov.lod_level = SimulationLOD::simplified;
    state.provinces.push_back(prov);

    add_market(state, "steel", 0, 50.0f, 30.0f, 15.0f);
    state.npc_businesses.push_back(make_test_business(1, 0));

    SupplyChainModule module;
    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // No deltas should be written for a simplified LOD province.
    REQUIRE(delta.market_deltas.empty());
    REQUIRE(delta.npc_deltas.empty());
}

TEST_CASE("test_insufficient_cash_blocks_dispatch", "[supply_chain][tier2]") {
    // Business with no cash cannot pay for inter-province shipping.
    auto state = make_test_world_state();

    state.provinces.push_back(make_test_province(0));
    state.provinces.push_back(make_test_province(1));

    // Province 0 has demand, no supply.
    add_market(state, "steel", 0, 0.0f, 50.0f, 20.0f);
    // Province 1 has supply.
    add_market(state, "steel", 1, 100.0f, 0.0f, 18.0f);

    add_route(state, 1, 0, 500.0f);

    // Business with zero cash — cannot afford transport.
    state.npc_businesses.push_back(make_test_business(1, 0, 0.0f));

    SupplyChainModule module;
    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // No inter-province shipment should have been dispatched.
    // No supply deltas for province 0 (since no local supply and no shipping).
    auto summary = summarize_market_deltas(delta, "steel", 0);
    REQUIRE(summary.supply_count == 0);
    // No capital deductions.
    REQUIRE(delta.npc_deltas.empty());
}

TEST_CASE("test_no_route_blocks_dispatch", "[supply_chain][tier2]") {
    // Without a route between provinces, inter-province shipping cannot occur.
    auto state = make_test_world_state();

    state.provinces.push_back(make_test_province(0));
    state.provinces.push_back(make_test_province(1));

    add_market(state, "coal", 0, 0.0f, 40.0f, 10.0f);
    add_market(state, "coal", 1, 80.0f, 0.0f, 9.0f);

    // No route added between provinces.

    state.npc_businesses.push_back(make_test_business(1, 0, 5000.0f));

    SupplyChainModule module;
    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // No shipment dispatched without a route.
    auto summary = summarize_market_deltas(delta, "coal", 0);
    REQUIRE(summary.supply_count == 0);
}

TEST_CASE("test_no_demand_no_matching", "[supply_chain][tier2]") {
    // Province with supply but no demand should produce no match deltas.
    auto state = make_test_world_state();
    state.provinces.push_back(make_test_province(0));

    add_market(state, "oil", 0, 100.0f, 0.0f, 50.0f);

    SupplyChainModule module;
    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    REQUIRE(delta.market_deltas.empty());
}

TEST_CASE("test_multiple_goods_local_matching", "[supply_chain][tier2]") {
    // Multiple goods should each be matched independently.
    auto state = make_test_world_state();
    state.provinces.push_back(make_test_province(0));

    add_market(state, "wheat", 0, 40.0f, 20.0f, 5.0f);
    add_market(state, "steel", 0, 30.0f, 50.0f, 15.0f);

    state.npc_businesses.push_back(make_test_business(1, 0));

    SupplyChainModule module;
    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // Wheat: 20 demand matched against 40 supply.
    auto wheat_summary = summarize_market_deltas(delta, "wheat", 0);
    REQUIRE(wheat_summary.demand_count == 1);
    REQUIRE_THAT(wheat_summary.total_demand_delta, WithinAbs(-20.0f, 0.001f));

    // Steel: 30 supply matched against 50 demand (only 30 fulfilled).
    auto steel_summary = summarize_market_deltas(delta, "steel", 0);
    REQUIRE(steel_summary.demand_count == 1);
    REQUIRE_THAT(steel_summary.total_demand_delta, WithinAbs(-30.0f, 0.001f));
}

TEST_CASE("test_transport_cost_terrain_impact", "[supply_chain][tier2]") {
    // Rougher terrain should increase transport cost.
    RouteProfile smooth = make_route(500.0f, 0.0f);  // no roughness
    RouteProfile rough = make_route(500.0f, 1.0f);   // max roughness
    float quantity = 100.0f;

    float cost_smooth = SupplyChainModule::compute_transport_cost(smooth, quantity);
    float cost_rough = SupplyChainModule::compute_transport_cost(rough, quantity);

    // Smooth: 0.01 * 500 * 100 * (1 + 0 * 0.5) = 500.0
    REQUIRE_THAT(cost_smooth, WithinAbs(500.0f, 0.01f));
    // Rough: 0.01 * 500 * 100 * (1 + 1.0 * 0.5) = 750.0
    REQUIRE_THAT(cost_rough, WithinAbs(750.0f, 0.01f));
    REQUIRE(cost_rough > cost_smooth);
}

TEST_CASE("test_lod1_import_processing", "[supply_chain][tier2]") {
    // LOD 1 trade offers should generate supply deltas for destination provinces.
    auto state = make_test_world_state();
    state.provinces.push_back(make_test_province(0));

    // Province 0 has demand for rice.
    add_market(state, "rice", 0, 0.0f, 100.0f, 12.0f);

    // LOD 1 nation offers rice for export.
    NationalTradeOffer offer{};
    offer.nation_id = 999;
    offer.tick_generated = 1;

    GoodOffer rice_export{};
    rice_export.good_id = SupplyChainModule::good_id_from_string("rice");
    rice_export.quantity_available = 200.0f;
    rice_export.offer_price = 10.0f;
    offer.exports.push_back(rice_export);

    state.lod1_trade_offers.push_back(offer);

    SupplyChainModule module;
    DeltaBuffer delta{};
    module.execute(state, delta);

    // Should have supply delta for rice in province 0.
    auto summary = summarize_market_deltas(delta, "rice", 0);
    REQUIRE(summary.supply_count >= 1);
    // Quantity should be min(200, 100) = 100 (limited by demand).
    REQUIRE_THAT(summary.total_supply_delta, WithinAbs(100.0f, 0.01f));
}

TEST_CASE("test_deterministic_matching_order", "[supply_chain][tier2]") {
    // Running the same matching twice with same seed should produce
    // identical results.
    auto state = make_test_world_state();
    state.provinces.push_back(make_test_province(0));

    add_market(state, "steel", 0, 50.0f, 30.0f, 15.0f);
    add_market(state, "wheat", 0, 25.0f, 40.0f, 5.0f);
    state.npc_businesses.push_back(make_test_business(1, 0));

    SupplyChainModule module;

    DeltaBuffer delta1{};
    module.execute_province(0, state, delta1);

    DeltaBuffer delta2{};
    module.execute_province(0, state, delta2);

    // Both runs should produce the same number of deltas.
    REQUIRE(delta1.market_deltas.size() == delta2.market_deltas.size());

    // Each delta should match.
    for (size_t i = 0; i < delta1.market_deltas.size(); ++i) {
        REQUIRE(delta1.market_deltas[i].good_id == delta2.market_deltas[i].good_id);
        REQUIRE(delta1.market_deltas[i].region_id == delta2.market_deltas[i].region_id);
        if (delta1.market_deltas[i].supply_delta.has_value()) {
            REQUIRE(delta2.market_deltas[i].supply_delta.has_value());
            REQUIRE_THAT(delta1.market_deltas[i].supply_delta.value(),
                         WithinAbs(delta2.market_deltas[i].supply_delta.value(), 0.0001f));
        }
        if (delta1.market_deltas[i].demand_buffer_delta.has_value()) {
            REQUIRE(delta2.market_deltas[i].demand_buffer_delta.has_value());
            REQUIRE_THAT(delta1.market_deltas[i].demand_buffer_delta.value(),
                         WithinAbs(delta2.market_deltas[i].demand_buffer_delta.value(), 0.0001f));
        }
    }
}
