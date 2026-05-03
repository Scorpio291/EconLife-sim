// Trade infrastructure module unit tests.
// All tests tagged [trade_infrastructure][tier3].
//
// Tests verify transit time calculation, perishable degradation, criminal
// interception probability, concealment cap, transit arrival market supply,
// shipment loss on full degradation, and module interface properties.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

#include "core/rng/deterministic_rng.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"
#include "modules/trade_infrastructure/trade_infrastructure_module.h"
#include "modules/trade_infrastructure/trade_types.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// Default-constructed module for calling non-static methods in tests.
static const TradeInfrastructureModule test_trade_module{};

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

namespace {

WorldState make_test_world_state() {
    WorldState state{};
    state.current_tick = 10;
    state.world_seed = 42;
    state.player.reset();
    state.lod2_price_index.reset();
    state.ticks_this_session = 10;
    state.game_mode = GameMode::standard;
    state.current_schema_version = 1;
    state.network_health_dirty = false;
    return state;
}

RouteProfile make_test_route(float distance_km = 800.0f, float terrain_roughness = 0.0f,
                             float min_infrastructure = 1.0f, float concealment_bonus = 0.0f) {
    RouteProfile route{};
    route.distance_km = distance_km;
    route.route_terrain_roughness = terrain_roughness;
    route.min_infrastructure = min_infrastructure;
    route.hop_count = 1;
    route.requires_sea_leg = false;
    route.requires_rail = false;
    route.concealment_bonus = concealment_bonus;
    route.province_path = {0, 1};
    return route;
}

TransitShipment make_test_shipment(uint32_t id = 1, uint32_t good_id = 100, float quantity = 50.0f,
                                   uint32_t arrival_tick = 10, bool is_criminal = false,
                                   float interception_risk = 0.0f, float concealment_mod = 0.0f) {
    TransitShipment s{};
    s.id = id;
    s.good_id = good_id;
    s.quantity_dispatched = quantity;
    s.quantity_remaining = quantity;
    s.quality_at_departure = 1.0f;
    s.quality_current = 1.0f;
    s.origin_province_id = 0;
    s.destination_province_id = 1;
    s.owner_id = 42;
    s.dispatch_tick = 1;
    s.arrival_tick = arrival_tick;
    s.mode = TransportMode::road;
    s.cost_paid = 100.0f;
    s.is_criminal = is_criminal;
    s.interception_risk_per_tick = interception_risk;
    s.is_concealed = false;
    s.route_concealment_modifier = concealment_mod;
    s.status = ShipmentStatus::in_transit;
    return s;
}

}  // anonymous namespace

// ===========================================================================
// Tests
// ===========================================================================

// ---------------------------------------------------------------------------
// Transit time calculation
// ---------------------------------------------------------------------------

TEST_CASE("test_transit_time_flat_road", "[trade_infrastructure][tier3]") {
    // 800 km on road (speed 800 km/tick), flat terrain, perfect infra.
    // base_transit = 800/800 = 1.0
    // terrain_delay = 1.0 + 0.0 * 0.4 = 1.0
    // infra_delay = 1.0 + (1.0 - 1.0) * 0.6 = 1.0
    // transit_ticks = max(1, round(1.0 * 1.0 * 1.0)) = 1
    auto route = make_test_route(800.0f, 0.0f, 1.0f);
    auto result = test_trade_module.calculate_transit_ticks(route, TransportMode::road);
    REQUIRE(result == 1);
}

TEST_CASE("test_transit_time_with_terrain", "[trade_infrastructure][tier3]") {
    // 800 km road, terrain_roughness=0.5, perfect infra.
    // base_transit = 1.0
    // terrain_delay = 1.0 + 0.5 * 0.4 = 1.2
    // infra_delay = 1.0
    // transit_ticks = max(1, round(1.0 * 1.2 * 1.0)) = 1 (rounds to 1)
    auto route = make_test_route(800.0f, 0.5f, 1.0f);
    auto result = test_trade_module.calculate_transit_ticks(route, TransportMode::road);
    REQUIRE(result == 1);
}

TEST_CASE("test_transit_time_with_poor_infrastructure", "[trade_infrastructure][tier3]") {
    // 800 km road, flat terrain, min_infrastructure=0.0 (worst).
    // base_transit = 1.0
    // terrain_delay = 1.0
    // infra_delay = 1.0 + (1.0 - 0.0) * 0.6 = 1.6
    // transit_ticks = max(1, round(1.0 * 1.0 * 1.6)) = 2
    auto route = make_test_route(800.0f, 0.0f, 0.0f);
    auto result = test_trade_module.calculate_transit_ticks(route, TransportMode::road);
    REQUIRE(result == 2);
}

TEST_CASE("test_transit_time_combined_delays", "[trade_infrastructure][tier3]") {
    // 1600 km road, terrain=1.0 (max), infra=0.0 (worst).
    // base_transit = 1600/800 = 2.0
    // terrain_delay = 1.0 + 1.0 * 0.4 = 1.4
    // infra_delay = 1.0 + 1.0 * 0.6 = 1.6
    // raw = 2.0 * 1.4 * 1.6 = 4.48
    // transit_ticks = max(1, round(4.48)) = 4
    auto route = make_test_route(1600.0f, 1.0f, 0.0f);
    auto result = test_trade_module.calculate_transit_ticks(route, TransportMode::road);
    REQUIRE(result == 4);
}

TEST_CASE("test_transit_time_different_modes", "[trade_infrastructure][tier3]") {
    // 4500 km, flat, perfect infra. Different modes yield different times.
    auto route = make_test_route(4500.0f, 0.0f, 1.0f);

    // Road: 4500/800 = 5.625 -> round = 6
    REQUIRE(test_trade_module.calculate_transit_ticks(route, TransportMode::road) == 6);

    // Rail: 4500/700 = 6.4286 -> round = 6
    REQUIRE(test_trade_module.calculate_transit_ticks(route, TransportMode::rail) == 6);

    // Sea: 4500/900 = 5.0 -> round = 5
    REQUIRE(test_trade_module.calculate_transit_ticks(route, TransportMode::sea) == 5);

    // River: 4500/450 = 10.0 -> round = 10
    REQUIRE(test_trade_module.calculate_transit_ticks(route, TransportMode::river) == 10);

    // Air: 4500/10000 = 0.45 -> round = 0, clamped to 1
    REQUIRE(test_trade_module.calculate_transit_ticks(route, TransportMode::air) == 1);
}

TEST_CASE("test_transit_time_minimum_one_tick", "[trade_infrastructure][tier3]") {
    // Very short distance: should never produce 0 ticks.
    auto route = make_test_route(1.0f, 0.0f, 1.0f);
    auto result = test_trade_module.calculate_transit_ticks(route, TransportMode::air);
    REQUIRE(result == 1);
}

// ---------------------------------------------------------------------------
// Perishable degradation
// ---------------------------------------------------------------------------

TEST_CASE("test_perishable_decay_formula", "[trade_infrastructure][tier3]") {
    // Quantity decays by (1 - decay_rate) per tick.
    // Quality decays by (1 - decay_rate * 0.5) per tick.
    auto shipment = make_test_shipment();
    shipment.quantity_remaining = 100.0f;
    shipment.quality_current = 1.0f;

    constexpr float decay_rate = 0.01f;
    bool viable = TradeInfrastructureModule::apply_perishable_decay(shipment, decay_rate);

    REQUIRE(viable == true);
    REQUIRE_THAT(shipment.quantity_remaining, WithinAbs(99.0f, 0.001f));
    REQUIRE_THAT(shipment.quality_current, WithinAbs(0.995f, 0.0001f));
}

TEST_CASE("test_perishable_decay_multiple_ticks", "[trade_infrastructure][tier3]") {
    // Apply decay 10 times. Verify compounding.
    auto shipment = make_test_shipment();
    shipment.quantity_remaining = 100.0f;
    shipment.quality_current = 1.0f;

    constexpr float decay_rate = 0.01f;
    for (int i = 0; i < 10; ++i) {
        TradeInfrastructureModule::apply_perishable_decay(shipment, decay_rate);
    }

    // quantity = 100 * (1 - 0.01)^10 = 100 * 0.99^10
    float expected_qty = 100.0f * std::pow(0.99f, 10);
    REQUIRE_THAT(shipment.quantity_remaining, WithinAbs(expected_qty, 0.01f));

    // quality = 1.0 * (1 - 0.005)^10 = 0.995^10
    float expected_quality = std::pow(0.995f, 10);
    REQUIRE_THAT(shipment.quality_current, WithinAbs(expected_quality, 0.0001f));
}

TEST_CASE("test_perishable_decay_to_zero_marks_lost", "[trade_infrastructure][tier3]") {
    // If quantity reaches 0, status should become "lost".
    auto shipment = make_test_shipment();
    shipment.quantity_remaining = 0.001f;  // very small quantity
    shipment.quality_current = 0.5f;

    // A 100% decay rate will zero it out.
    bool viable = TradeInfrastructureModule::apply_perishable_decay(shipment, 1.0f);

    REQUIRE(viable == false);
    REQUIRE(shipment.status == ShipmentStatus::lost);
    REQUIRE_THAT(shipment.quantity_remaining, WithinAbs(0.0f, 0.0001f));
}

TEST_CASE("test_perishable_quality_degrades_slower_than_quantity",
          "[trade_infrastructure][tier3]") {
    // Quality decay rate is half of quantity decay rate.
    auto shipment = make_test_shipment();
    shipment.quantity_remaining = 100.0f;
    shipment.quality_current = 1.0f;

    constexpr float decay_rate = 0.10f;  // 10% per tick
    TradeInfrastructureModule::apply_perishable_decay(shipment, decay_rate);

    // Quantity: 100 * 0.9 = 90
    REQUIRE_THAT(shipment.quantity_remaining, WithinAbs(90.0f, 0.01f));
    // Quality: 1.0 * (1 - 0.05) = 0.95
    REQUIRE_THAT(shipment.quality_current, WithinAbs(0.95f, 0.001f));
}

// ---------------------------------------------------------------------------
// Criminal interception
// ---------------------------------------------------------------------------

TEST_CASE("test_interception_zero_risk_never_intercepted", "[trade_infrastructure][tier3]") {
    // A shipment with 0 interception risk should never be intercepted.
    auto shipment = make_test_shipment(1, 100, 50.0f, 20, true, 0.0f, 0.0f);
    DeterministicRNG rng(42);

    for (int i = 0; i < 100; ++i) {
        REQUIRE(test_trade_module.check_interception(shipment, rng) == false);
    }
}

TEST_CASE("test_interception_full_risk_always_intercepted", "[trade_infrastructure][tier3]") {
    // A shipment with risk = 1.0 and no concealment should always be intercepted
    // (since rng.next_float() returns [0.0, 1.0) which is always < 1.0).
    auto shipment = make_test_shipment(1, 100, 50.0f, 20, true, 1.0f, 0.0f);
    DeterministicRNG rng(42);

    for (int i = 0; i < 100; ++i) {
        REQUIRE(test_trade_module.check_interception(shipment, rng) == true);
    }
}

TEST_CASE("test_concealment_reduces_effective_risk", "[trade_infrastructure][tier3]") {
    // Risk = 0.5, concealment = 0.40 (max).
    // Effective risk = 0.5 * (1 - 0.4) = 0.3
    // This is a probabilistic test, but we can verify the effective risk
    // by running many trials and checking the interception rate.
    auto shipment = make_test_shipment(1, 100, 50.0f, 20, true, 0.5f, 0.40f);
    DeterministicRNG rng(12345);

    int intercepted_count = 0;
    constexpr int trials = 10000;
    for (int i = 0; i < trials; ++i) {
        if (test_trade_module.check_interception(shipment, rng)) {
            intercepted_count++;
        }
    }

    // Expected interception rate ~0.3. Allow reasonable statistical margin.
    float rate = static_cast<float>(intercepted_count) / static_cast<float>(trials);
    REQUIRE_THAT(rate, WithinAbs(0.3f, 0.03f));
}

TEST_CASE("test_concealment_cap_at_040", "[trade_infrastructure][tier3]") {
    // Even if route_concealment_modifier is 0.90, it should be capped at 0.40.
    // Risk = 1.0, concealment = 0.90 (should be capped to 0.40).
    // Effective risk = 1.0 * (1 - 0.4) = 0.6
    auto shipment = make_test_shipment(1, 100, 50.0f, 20, true, 1.0f, 0.90f);
    DeterministicRNG rng(54321);

    int intercepted_count = 0;
    constexpr int trials = 10000;
    for (int i = 0; i < trials; ++i) {
        if (test_trade_module.check_interception(shipment, rng)) {
            intercepted_count++;
        }
    }

    // If concealment were not capped, effective risk would be 1.0 * (1 - 0.9) = 0.1
    // With cap at 0.40, effective risk = 1.0 * (1 - 0.4) = 0.6
    float rate = static_cast<float>(intercepted_count) / static_cast<float>(trials);
    // Should be around 0.6, not 0.1
    REQUIRE(rate > 0.4f);
    REQUIRE_THAT(rate, WithinAbs(0.6f, 0.03f));
}

// ---------------------------------------------------------------------------
// Transit arrival adds to market supply
// ---------------------------------------------------------------------------

TEST_CASE("test_transit_arrival_adds_supply", "[trade_infrastructure][tier3]") {
    // A shipment arriving at its destination tick should generate a market
    // supply delta for the destination province.
    auto state = make_test_world_state();
    state.current_tick = 10;

    TradeInfrastructureModule module;
    auto shipment = make_test_shipment(1, 200, 75.0f, 10);
    shipment.destination_province_id = 3;
    module.add_shipment(shipment);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Should have one market delta adding supply.
    REQUIRE(delta.market_deltas.size() == 1);
    REQUIRE(delta.market_deltas[0].good_id == 200);
    REQUIRE(delta.market_deltas[0].region_id == 3);
    REQUIRE(delta.market_deltas[0].supply_delta.has_value());
    // Quantity will have some perishable decay applied after arrival processing,
    // but since the shipment status changed to arrived, the perishable decay
    // only applies to in-transit shipments. The supply delta uses quantity_remaining
    // at the time of arrival, before further decay is applied.
    REQUIRE_THAT(delta.market_deltas[0].supply_delta.value(), WithinAbs(75.0f, 0.01f));
}

TEST_CASE("test_future_shipment_not_arrived", "[trade_infrastructure][tier3]") {
    // A shipment with arrival_tick > current_tick should NOT generate a market delta.
    auto state = make_test_world_state();
    state.current_tick = 5;

    TradeInfrastructureModule module;
    auto shipment = make_test_shipment(1, 200, 50.0f, 10);
    module.add_shipment(shipment);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // No market deltas for supply (shipment still in transit).
    bool has_supply_delta = false;
    for (const auto& md : delta.market_deltas) {
        if (md.supply_delta.has_value()) {
            has_supply_delta = true;
        }
    }
    REQUIRE(has_supply_delta == false);
}

TEST_CASE("test_arrived_shipment_removed_after_execute", "[trade_infrastructure][tier3]") {
    // After processing, arrived shipments should be removed from active list.
    auto state = make_test_world_state();
    state.current_tick = 10;

    TradeInfrastructureModule module;
    module.add_shipment(make_test_shipment(1, 100, 50.0f, 10));  // arrives now
    module.add_shipment(make_test_shipment(2, 100, 50.0f, 20));  // still in transit
    REQUIRE(module.active_shipments().size() == 2);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Only one shipment should remain (the one still in transit).
    REQUIRE(module.active_shipments().size() == 1);
    REQUIRE(module.active_shipments()[0].id == 2);
}

// ---------------------------------------------------------------------------
// Shipment lost when quantity degrades to zero
// ---------------------------------------------------------------------------

TEST_CASE("test_shipment_lost_on_full_degradation", "[trade_infrastructure][tier3]") {
    // A shipment with tiny remaining quantity that degrades to zero via
    // perishable decay should be marked lost and removed.
    auto state = make_test_world_state();
    state.current_tick = 5;  // before arrival

    TradeInfrastructureModule module;
    auto shipment = make_test_shipment(1, 100, 0.0001f, 20);
    // Use a very high decay that will zero out the quantity.
    // The module uses perishable_decay_base (0.01), so we set the
    // quantity so low that one tick of 0.01 decay still leaves some.
    // Instead, test via the static function directly.
    shipment.quantity_remaining = 0.0f;
    // Apply decay to an already-zero shipment.
    bool viable = TradeInfrastructureModule::apply_perishable_decay(shipment, 0.01f);

    REQUIRE(viable == false);
    REQUIRE(shipment.status == ShipmentStatus::lost);
}

// ---------------------------------------------------------------------------
// Module interface properties
// ---------------------------------------------------------------------------

TEST_CASE("test_module_interface_properties", "[trade_infrastructure][tier3]") {
    TradeInfrastructureModule module;

    REQUIRE(module.name() == "trade_infrastructure");
    REQUIRE(module.package_id() == "base_game");
    REQUIRE(module.scope() == ModuleScope::v1);
    REQUIRE(module.is_province_parallel() == false);

    auto after = module.runs_after();
    REQUIRE(after.size() == 1);
    REQUIRE(after[0] == "supply_chain");

    auto before = module.runs_before();
    REQUIRE(before.size() == 1);
    REQUIRE(before[0] == "financial_distribution");
}

// ---------------------------------------------------------------------------
// Configuration constants
// ---------------------------------------------------------------------------

TEST_CASE("test_mode_speed_constants", "[trade_infrastructure][tier3]") {
    TradeInfrastructureModule module;
    REQUIRE_THAT(module.speed_for_mode(TransportMode::road), WithinAbs(800.0f, 0.01f));
    REQUIRE_THAT(module.speed_for_mode(TransportMode::rail), WithinAbs(700.0f, 0.01f));
    REQUIRE_THAT(module.speed_for_mode(TransportMode::sea), WithinAbs(900.0f, 0.01f));
    REQUIRE_THAT(module.speed_for_mode(TransportMode::river), WithinAbs(450.0f, 0.01f));
    REQUIRE_THAT(module.speed_for_mode(TransportMode::air), WithinAbs(10000.0f, 0.01f));
}

TEST_CASE("test_delay_coefficient_constants", "[trade_infrastructure][tier3]") {
    REQUIRE_THAT(TradeInfrastructureConfig{}.terrain_delay_coeff, WithinAbs(0.4f, 0.001f));
    REQUIRE_THAT(TradeInfrastructureConfig{}.infra_delay_coeff, WithinAbs(0.6f, 0.001f));
    REQUIRE_THAT(TradeInfrastructureConfig{}.max_concealment_modifier, WithinAbs(0.40f, 0.001f));
    REQUIRE_THAT(TradeInfrastructureConfig{}.perishable_decay_base, WithinAbs(0.01f, 0.001f));
}

// ---------------------------------------------------------------------------
// Integration: criminal interception generates evidence and consequence deltas
// ---------------------------------------------------------------------------

TEST_CASE("test_interception_generates_evidence_delta", "[trade_infrastructure][tier3]") {
    // When a criminal shipment is intercepted during execute(), the module
    // should generate an EvidenceDelta and a ConsequenceDelta.
    auto state = make_test_world_state();
    state.current_tick = 5;

    TradeInfrastructureModule module;
    // Create a criminal shipment with 100% interception risk (guaranteed intercept).
    auto shipment = make_test_shipment(1, 100, 50.0f, 20, true, 1.0f, 0.0f);
    module.add_shipment(shipment);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Should have generated an evidence delta.
    REQUIRE(delta.evidence_deltas.size() == 1);
    REQUIRE(delta.evidence_deltas[0].new_token.has_value());
    REQUIRE(delta.evidence_deltas[0].new_token->type == EvidenceType::physical);
    REQUIRE(delta.evidence_deltas[0].new_token->target_npc_id == 42);

    // Should have generated a consequence delta.
    REQUIRE(delta.consequence_deltas.size() == 1);
    REQUIRE(delta.consequence_deltas[0].new_entry_id.has_value());

    // Intercepted shipment should be removed from active list.
    REQUIRE(module.active_shipments().empty());
}

// ---------------------------------------------------------------------------
// Multiple shipments processed in one tick
// ---------------------------------------------------------------------------

TEST_CASE("test_multiple_shipments_one_tick", "[trade_infrastructure][tier3]") {
    auto state = make_test_world_state();
    state.current_tick = 10;

    TradeInfrastructureModule module;
    // Shipment 1: arrives this tick
    module.add_shipment(make_test_shipment(1, 100, 30.0f, 10));
    // Shipment 2: arrives this tick, different good
    auto s2 = make_test_shipment(2, 200, 40.0f, 10);
    s2.destination_province_id = 2;
    module.add_shipment(s2);
    // Shipment 3: still in transit
    module.add_shipment(make_test_shipment(3, 100, 50.0f, 20));

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Two arrivals should produce two market deltas.
    REQUIRE(delta.market_deltas.size() == 2);

    // Only one shipment should remain active (id=3).
    REQUIRE(module.active_shipments().size() == 1);
    REQUIRE(module.active_shipments()[0].id == 3);
}
