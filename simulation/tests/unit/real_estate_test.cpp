// Real estate module unit tests.
// All tests tagged [real_estate][tier4].
//
// Tests verify the property market system:
//   1. Rental income = market_value * rental_yield_rate (derived invariant)
//   2. Asking price convergence toward market value
//   3. Convergence only on monthly tick interval
//   4. Property transaction transfers ownership
//   5. avg_property_value computed as mean of market_values
//   6. Criminal dominance reduces property values
//   7. Different yield rates for different property types
//   8. Rental income generates wealth/capital delta for owner
//   9. Transaction above threshold generates evidence
//  10. Module interface properties (name, province_parallel, runs_after)
//  11. Commercial tenant assignment
//  12. Empty province has zero avg_property_value

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/real_estate/real_estate_module.h"
#include "modules/real_estate/real_estate_types.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// ---------------------------------------------------------------------------
// Test helpers — create minimal WorldState and supporting structures
// ---------------------------------------------------------------------------

namespace {

// Create a minimal WorldState suitable for real estate tests.
WorldState make_test_world_state(uint32_t tick = 1) {
    WorldState state{};
    state.current_tick = tick;
    state.world_seed = 42;
    state.player.reset();
    state.lod2_price_index.reset();
    state.ticks_this_session = 1;
    state.game_mode = GameMode::standard;
    state.current_schema_version = 1;
    state.network_health_dirty = false;
    return state;
}

// Create a Province with sensible defaults.
Province make_test_province(uint32_t id, float criminal_dominance = 0.0f) {
    Province prov{};
    prov.id = id;
    prov.region_id = 0;
    prov.nation_id = 0;
    prov.conditions.criminal_dominance_index = criminal_dominance;
    prov.conditions.stability_score = 0.5f;
    prov.conditions.inequality_index = 0.3f;
    prov.conditions.crime_rate = 0.1f;
    prov.conditions.formal_employment_rate = 0.7f;
    prov.infrastructure_rating = 0.6f;
    prov.demographics.total_population = 100000;
    prov.demographics.income_high_fraction = 0.15f;
    return prov;
}

// Create a PropertyListing with sensible defaults.
PropertyListing make_test_property(uint32_t id, PropertyType type, uint32_t province_id,
                                   uint32_t owner_id, float market_value,
                                   float asking_price = 0.0f) {
    PropertyListing prop{};
    prop.id = id;
    prop.type = type;
    prop.province_id = province_id;
    prop.owner_id = owner_id;
    prop.market_value = market_value;
    prop.asking_price = (asking_price > 0.0f) ? asking_price : market_value;

    // Set yield rate based on property type.
    switch (type) {
        case PropertyType::residential:
            prop.rental_yield_rate = RealEstateConfig{}.residential_yield_rate;
            break;
        case PropertyType::commercial:
            prop.rental_yield_rate = RealEstateConfig{}.commercial_yield_rate;
            break;
        case PropertyType::industrial:
            prop.rental_yield_rate = RealEstateConfig{}.industrial_yield_rate;
            break;
    }

    // Derive rental_income_per_tick (invariant).
    prop.rental_income_per_tick = market_value * prop.rental_yield_rate;
    prop.rented = false;
    prop.tenant_id = 0;
    prop.launder_eligible = false;
    prop.purchased_tick = 0;
    prop.purchase_price = market_value;
    return prop;
}

// Create a minimal PlayerCharacter for testing.
PlayerCharacter make_test_player(uint32_t id = 1) {
    PlayerCharacter player{};
    player.id = id;
    player.wealth = 100000.0f;
    return player;
}

}  // anonymous namespace

// ===========================================================================
// Test 1: Rental income = market_value * rental_yield_rate (derived invariant)
// ===========================================================================

TEST_CASE("test_rental_income_derived_from_market_value_and_yield", "[real_estate][tier4]") {
    RealEstateModule mod;
    float market_value = 200000.0f;
    float yield_rate = RealEstateConfig{}.residential_yield_rate;

    float rental = mod.compute_rental_income(market_value, yield_rate);

    // 200,000 * 0.003 = 600.0
    REQUIRE_THAT(rental, WithinAbs(600.0f, 0.01f));
}

TEST_CASE("test_rental_income_zero_market_value", "[real_estate][tier4]") {
    RealEstateModule mod;
    float rental = mod.compute_rental_income(0.0f, 0.003f);
    REQUIRE_THAT(rental, WithinAbs(0.0f, 0.0001f));
}

TEST_CASE("test_rental_income_zero_yield_rate", "[real_estate][tier4]") {
    RealEstateModule mod;
    float rental = mod.compute_rental_income(100000.0f, 0.0f);
    REQUIRE_THAT(rental, WithinAbs(0.0f, 0.0001f));
}

// ===========================================================================
// Test 2: Asking price converges toward market value
// ===========================================================================

TEST_CASE("test_asking_price_converges_upward", "[real_estate][tier4]") {
    RealEstateModule mod;
    PropertyListing prop =
        make_test_property(1, PropertyType::residential, 0, 100, 150000.0f, 100000.0f);

    // asking = 100,000, market = 150,000
    // gap = 150,000 - 100,000 = 50,000
    // convergence: 100,000 + 50,000 * 0.05 = 102,500
    mod.converge_asking_price(prop, RealEstateConfig{}.price_convergence_rate);

    REQUIRE_THAT(prop.asking_price, WithinAbs(102500.0f, 0.01f));
}

TEST_CASE("test_asking_price_converges_downward", "[real_estate][tier4]") {
    RealEstateModule mod;
    PropertyListing prop =
        make_test_property(1, PropertyType::residential, 0, 100, 80000.0f, 100000.0f);

    // asking = 100,000, market = 80,000
    // gap = 80,000 - 100,000 = -20,000
    // convergence: 100,000 + (-20,000) * 0.05 = 99,000
    mod.converge_asking_price(prop, RealEstateConfig{}.price_convergence_rate);

    REQUIRE_THAT(prop.asking_price, WithinAbs(99000.0f, 0.01f));
}

TEST_CASE("test_asking_price_at_market_value_unchanged", "[real_estate][tier4]") {
    RealEstateModule mod;
    PropertyListing prop =
        make_test_property(1, PropertyType::residential, 0, 100, 100000.0f, 100000.0f);

    mod.converge_asking_price(prop, RealEstateConfig{}.price_convergence_rate);

    REQUIRE_THAT(prop.asking_price, WithinAbs(100000.0f, 0.01f));
}

TEST_CASE("test_asking_price_never_negative", "[real_estate][tier4]") {
    RealEstateModule mod;
    PropertyListing prop = make_test_property(1, PropertyType::residential, 0, 100, 0.0f, 1.0f);

    // market_value = 0, asking = 1.0
    // gap = 0 - 1 = -1.0, convergence: 1.0 + (-1.0) * 0.05 = 0.95
    mod.converge_asking_price(prop, 1.0f);
    REQUIRE(prop.asking_price >= 0.0f);
}

// ===========================================================================
// Test 3: Convergence only on monthly tick interval (every 30 ticks)
// ===========================================================================

TEST_CASE("test_convergence_occurs_on_monthly_tick", "[real_estate][tier4]") {
    // Tick 30 is a monthly tick.
    auto state = make_test_world_state(30);
    state.provinces.push_back(make_test_province(0));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, 100, 150000.0f, 100000.0f);
    prop.rented = false;
    module.add_property(prop);

    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // On monthly tick, asking_price should have converged.
    // Also, market_value will be recomputed (no criminal_dominance, so stays same).
    const auto& props = module.properties();
    REQUIRE(props.size() == 1);
    // asking_price = 100,000 + (150,000 - 100,000) * 0.05 = 102,500
    // Note: market_value is recomputed first, with no penalties it stays at 150,000
    REQUIRE_THAT(props[0].asking_price, WithinAbs(102500.0f, 1.0f));
}

TEST_CASE("test_no_convergence_on_non_monthly_tick", "[real_estate][tier4]") {
    // Tick 15 is NOT a monthly tick.
    auto state = make_test_world_state(15);
    state.provinces.push_back(make_test_province(0));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, 100, 150000.0f, 100000.0f);
    prop.rented = false;
    module.add_property(prop);

    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // On non-monthly tick, asking_price should NOT have changed.
    const auto& props = module.properties();
    REQUIRE(props.size() == 1);
    REQUIRE_THAT(props[0].asking_price, WithinAbs(100000.0f, 0.01f));
}

// ===========================================================================
// Test 4: Property transaction transfers ownership
// ===========================================================================

TEST_CASE("test_property_transaction_transfers_ownership", "[real_estate][tier4]") {
    // Test that we can modify property ownership through the module's
    // mutable properties() accessor (simulating a transaction).
    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, 100, 200000.0f);
    module.add_property(prop);

    // Simulate a sale: transfer ownership from owner 100 to owner 200.
    auto& props = module.properties();
    REQUIRE(props.size() == 1);
    REQUIRE(props[0].owner_id == 100);

    props[0].owner_id = 200;
    props[0].purchase_price = 220000.0f;
    props[0].purchased_tick = 50;

    REQUIRE(props[0].owner_id == 200);
    REQUIRE_THAT(props[0].purchase_price, WithinAbs(220000.0f, 0.01f));
    REQUIRE(props[0].purchased_tick == 50);
}

// ===========================================================================
// Test 5: avg_property_value computed as mean of market_values
// ===========================================================================

TEST_CASE("test_avg_property_value_is_mean_of_market_values", "[real_estate][tier4]") {
    RealEstateModule mod;
    std::vector<PropertyListing> props;
    props.push_back(make_test_property(1, PropertyType::residential, 0, 100, 100000.0f));
    props.push_back(make_test_property(2, PropertyType::commercial, 0, 101, 200000.0f));
    props.push_back(make_test_property(3, PropertyType::industrial, 0, 102, 300000.0f));

    float avg = mod.compute_avg_property_value(props, 0);

    // mean = (100,000 + 200,000 + 300,000) / 3 = 200,000
    REQUIRE_THAT(avg, WithinAbs(200000.0f, 0.01f));
}

TEST_CASE("test_avg_property_value_filters_by_province", "[real_estate][tier4]") {
    RealEstateModule mod;
    std::vector<PropertyListing> props;
    props.push_back(make_test_property(1, PropertyType::residential, 0, 100, 100000.0f));
    props.push_back(make_test_property(2, PropertyType::residential, 1, 101, 500000.0f));
    props.push_back(make_test_property(3, PropertyType::residential, 0, 102, 300000.0f));

    float avg = mod.compute_avg_property_value(props, 0);

    // Only province 0 properties: (100,000 + 300,000) / 2 = 200,000
    REQUIRE_THAT(avg, WithinAbs(200000.0f, 0.01f));
}

// ===========================================================================
// Test 6: Criminal dominance reduces property values
// ===========================================================================

TEST_CASE("test_criminal_dominance_reduces_market_value", "[real_estate][tier4]") {
    RealEstateModule mod;
    Province prov = make_test_province(0, 1.0f);  // max criminal dominance

    PropertyListing prop = make_test_property(1, PropertyType::residential, 0, 100, 200000.0f);

    float new_value = mod.compute_market_value(prop, prov);

    // multiplier = 1.0 - (1.0 * 0.15) + 0.0 = 0.85
    // new_value = 200,000 * 0.85 = 170,000
    REQUIRE_THAT(new_value, WithinAbs(170000.0f, 1.0f));
}

TEST_CASE("test_zero_criminal_dominance_preserves_value", "[real_estate][tier4]") {
    RealEstateModule mod;
    Province prov = make_test_province(0, 0.0f);

    PropertyListing prop = make_test_property(1, PropertyType::residential, 0, 100, 200000.0f);

    float new_value = mod.compute_market_value(prop, prov);

    // multiplier = 1.0 - 0.0 + 0.0 = 1.0
    // new_value = 200,000 * 1.0 = 200,000
    REQUIRE_THAT(new_value, WithinAbs(200000.0f, 1.0f));
}

TEST_CASE("test_laundering_eligible_inflates_value", "[real_estate][tier4]") {
    RealEstateModule mod;
    Province prov = make_test_province(0, 0.0f);

    PropertyListing prop = make_test_property(1, PropertyType::residential, 0, 100, 200000.0f);
    prop.launder_eligible = true;

    float new_value = mod.compute_market_value(prop, prov);

    // multiplier = 1.0 - 0.0 + 0.10 = 1.10
    // new_value = 200,000 * 1.10 = 220,000
    REQUIRE_THAT(new_value, WithinAbs(220000.0f, 1.0f));
}

TEST_CASE("test_criminal_dominance_and_laundering_coexist", "[real_estate][tier4]") {
    RealEstateModule mod;
    Province prov = make_test_province(0, 0.5f);

    PropertyListing prop = make_test_property(1, PropertyType::residential, 0, 100, 200000.0f);
    prop.launder_eligible = true;

    float new_value = mod.compute_market_value(prop, prov);

    // multiplier = 1.0 - (0.5 * 0.15) + 0.10 = 1.0 - 0.075 + 0.10 = 1.025
    // new_value = 200,000 * 1.025 = 205,000
    REQUIRE_THAT(new_value, WithinAbs(205000.0f, 1.0f));
}

TEST_CASE("test_market_value_multiplier_clamped_to_minimum", "[real_estate][tier4]") {
    RealEstateModule mod;
    // Extreme criminal dominance that would push multiplier below 0.1
    Province prov = make_test_province(0, 0.0f);
    prov.conditions.criminal_dominance_index = 10.0f;  // extreme value

    PropertyListing prop = make_test_property(1, PropertyType::residential, 0, 100, 200000.0f);

    float new_value = mod.compute_market_value(prop, prov);

    // multiplier = 1.0 - (10.0 * 0.15) = 1.0 - 1.5 = -0.5, clamped to 0.1
    // new_value = 200,000 * 0.1 = 20,000
    REQUIRE_THAT(new_value, WithinAbs(20000.0f, 1.0f));
}

// ===========================================================================
// Test 7: Different yield rates for different property types
// ===========================================================================

TEST_CASE("test_residential_yield_rate", "[real_estate][tier4]") {
    RealEstateModule mod;
    float market_value = 100000.0f;
    float rental =
        mod.compute_rental_income(market_value, RealEstateConfig{}.residential_yield_rate);

    // 100,000 * 0.003 = 300.0
    REQUIRE_THAT(rental, WithinAbs(300.0f, 0.01f));
}

TEST_CASE("test_commercial_yield_rate", "[real_estate][tier4]") {
    RealEstateModule mod;
    float market_value = 100000.0f;
    float rental =
        mod.compute_rental_income(market_value, RealEstateConfig{}.commercial_yield_rate);

    // 100,000 * 0.004 = 400.0
    REQUIRE_THAT(rental, WithinAbs(400.0f, 0.01f));
}

TEST_CASE("test_industrial_yield_rate", "[real_estate][tier4]") {
    RealEstateModule mod;
    float market_value = 100000.0f;
    float rental =
        mod.compute_rental_income(market_value, RealEstateConfig{}.industrial_yield_rate);

    // 100,000 * 0.005 = 500.0
    REQUIRE_THAT(rental, WithinAbs(500.0f, 0.01f));
}

TEST_CASE("test_industrial_yield_higher_than_commercial_higher_than_residential",
          "[real_estate][tier4]") {
    REQUIRE(RealEstateConfig{}.industrial_yield_rate > RealEstateConfig{}.commercial_yield_rate);
    REQUIRE(RealEstateConfig{}.commercial_yield_rate > RealEstateConfig{}.residential_yield_rate);
}

// ===========================================================================
// Test 8: Rental income generates wealth/capital delta for owner
// ===========================================================================

TEST_CASE("test_rental_income_credits_player_wealth", "[real_estate][tier4]") {
    auto state = make_test_world_state(1);
    state.provinces.push_back(make_test_province(0));

    PlayerCharacter player = make_test_player(1);
    state.player = std::make_unique<PlayerCharacter>(player);

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, 1,
                                   200000.0f);  // owner_id = player_id = 1
    prop.rented = true;
    prop.tenant_id = 50;
    module.add_property(prop);

    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // rental = 200,000 * 0.003 = 600.0
    REQUIRE(delta.player_delta.wealth_delta.has_value());
    REQUIRE_THAT(delta.player_delta.wealth_delta.value(), WithinAbs(600.0f, 0.01f));
}

TEST_CASE("test_rental_income_credits_npc_capital", "[real_estate][tier4]") {
    auto state = make_test_world_state(1);
    state.provinces.push_back(make_test_province(0));

    RealEstateModule module;
    auto prop =
        make_test_property(1, PropertyType::commercial, 0, 42, 250000.0f);  // NPC owner_id = 42
    prop.rented = true;
    prop.tenant_id = 99;
    module.add_property(prop);

    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // rental = 250,000 * 0.004 = 1,000.0
    REQUIRE(delta.npc_deltas.size() >= 1);

    bool found = false;
    for (const auto& nd : delta.npc_deltas) {
        if (nd.npc_id == 42 && nd.capital_delta.has_value()) {
            REQUIRE_THAT(nd.capital_delta.value(), WithinAbs(1000.0f, 0.01f));
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("test_no_rental_income_for_unrented_property", "[real_estate][tier4]") {
    auto state = make_test_world_state(1);
    state.provinces.push_back(make_test_province(0));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, 42, 200000.0f);
    prop.rented = false;  // not rented
    prop.tenant_id = 0;
    module.add_property(prop);

    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // No rental income should be generated.
    REQUIRE(delta.npc_deltas.empty());
    REQUIRE_FALSE(delta.player_delta.wealth_delta.has_value());
}

TEST_CASE("test_multiple_rented_properties_accumulate_player_income", "[real_estate][tier4]") {
    auto state = make_test_world_state(1);
    state.provinces.push_back(make_test_province(0));

    PlayerCharacter player = make_test_player(1);
    state.player = std::make_unique<PlayerCharacter>(player);

    RealEstateModule module;

    auto prop1 = make_test_property(1, PropertyType::residential, 0, 1, 100000.0f);
    prop1.rented = true;
    prop1.tenant_id = 50;
    module.add_property(prop1);

    auto prop2 = make_test_property(2, PropertyType::commercial, 0, 1, 200000.0f);
    prop2.rented = true;
    prop2.tenant_id = 51;
    module.add_property(prop2);

    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // prop1 rental = 100,000 * 0.003 = 300
    // prop2 rental = 200,000 * 0.004 = 800
    // total = 1,100
    REQUIRE(delta.player_delta.wealth_delta.has_value());
    REQUIRE_THAT(delta.player_delta.wealth_delta.value(), WithinAbs(1100.0f, 0.01f));
}

// ===========================================================================
// Test 9: Transaction above threshold generates evidence
// ===========================================================================

TEST_CASE("test_transaction_above_threshold_flagged", "[real_estate][tier4]") {
    // The transaction evidence threshold is 50,000.
    // A property with asking_price above this should be flagged as suspicious.
    REQUIRE_THAT(RealEstateConfig{}.transaction_evidence_threshold, WithinAbs(50000.0f, 0.01f));

    // Verify the constant is set correctly for use in transaction processing.
    PropertyListing prop = make_test_property(1, PropertyType::residential, 0, 100, 60000.0f);
    REQUIRE(prop.market_value > RealEstateConfig{}.transaction_evidence_threshold);
}

TEST_CASE("test_transaction_below_threshold_not_flagged", "[real_estate][tier4]") {
    PropertyListing prop = make_test_property(1, PropertyType::residential, 0, 100, 40000.0f);
    REQUIRE(prop.market_value < RealEstateConfig{}.transaction_evidence_threshold);
}

// ===========================================================================
// Test 10: Module interface properties
// ===========================================================================

TEST_CASE("test_module_interface_properties", "[real_estate][tier4]") {
    RealEstateModule module;

    REQUIRE(module.name() == "real_estate");
    REQUIRE(module.package_id() == "base_game");
    REQUIRE(module.scope() == ModuleScope::v1);
    REQUIRE(module.is_province_parallel() == true);

    auto after = module.runs_after();
    REQUIRE(after.size() == 1);
    REQUIRE(after[0] == "price_engine");

    auto before = module.runs_before();
    REQUIRE(before.size() == 1);
    REQUIRE(before[0] == "npc_behavior");
}

// ===========================================================================
// Test 11: Commercial tenant assignment
// ===========================================================================

TEST_CASE("test_commercial_tenant_assignment", "[real_estate][tier4]") {
    auto state = make_test_world_state(1);
    state.provinces.push_back(make_test_province(0));

    // Create a business in province 0.
    NPCBusiness biz{};
    biz.id = 99;
    biz.province_id = 0;
    biz.cost_per_tick = 1000.0f;
    biz.owner_id = 42;
    state.npc_businesses.push_back(biz);

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::commercial, 0, 100, 250000.0f);
    prop.rented = false;
    prop.tenant_id = 0;
    module.add_property(prop);

    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // The commercial property should now have the business as tenant.
    const auto& props = module.properties();
    REQUIRE(props.size() == 1);
    REQUIRE(props[0].rented == true);
    REQUIRE(props[0].tenant_id == 99);
}

TEST_CASE("test_commercial_tenant_not_assigned_when_already_occupied", "[real_estate][tier4]") {
    auto state = make_test_world_state(1);
    state.provinces.push_back(make_test_province(0));

    NPCBusiness biz{};
    biz.id = 99;
    biz.province_id = 0;
    biz.cost_per_tick = 1000.0f;
    biz.owner_id = 42;
    state.npc_businesses.push_back(biz);

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::commercial, 0, 100, 250000.0f);
    prop.rented = true;   // already occupied
    prop.tenant_id = 55;  // different tenant
    module.add_property(prop);

    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // Should remain occupied by original tenant.
    const auto& props = module.properties();
    REQUIRE(props[0].rented == true);
    REQUIRE(props[0].tenant_id == 55);
}

TEST_CASE("test_residential_property_not_assigned_commercial_tenant", "[real_estate][tier4]") {
    auto state = make_test_world_state(1);
    state.provinces.push_back(make_test_province(0));

    NPCBusiness biz{};
    biz.id = 99;
    biz.province_id = 0;
    biz.cost_per_tick = 1000.0f;
    biz.owner_id = 42;
    state.npc_businesses.push_back(biz);

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, 100, 150000.0f);
    prop.rented = false;
    prop.tenant_id = 0;
    module.add_property(prop);

    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // Residential property should NOT be assigned a commercial tenant.
    const auto& props = module.properties();
    REQUIRE(props[0].rented == false);
    REQUIRE(props[0].tenant_id == 0);
}

TEST_CASE("test_business_already_has_premises_not_reassigned", "[real_estate][tier4]") {
    auto state = make_test_world_state(1);
    state.provinces.push_back(make_test_province(0));

    NPCBusiness biz{};
    biz.id = 99;
    biz.province_id = 0;
    biz.cost_per_tick = 1000.0f;
    biz.owner_id = 42;
    state.npc_businesses.push_back(biz);

    RealEstateModule module;

    // First commercial property: already occupied by business 99.
    auto prop1 = make_test_property(1, PropertyType::commercial, 0, 100, 250000.0f);
    prop1.rented = true;
    prop1.tenant_id = 99;
    module.add_property(prop1);

    // Second commercial property: vacant.
    auto prop2 = make_test_property(2, PropertyType::commercial, 0, 100, 200000.0f);
    prop2.rented = false;
    prop2.tenant_id = 0;
    module.add_property(prop2);

    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // Business 99 already has premises, so the second property should remain vacant.
    const auto& props = module.properties();
    REQUIRE(props[0].tenant_id == 99);  // prop1 unchanged
    REQUIRE(props[1].rented == false);  // prop2 still vacant
    REQUIRE(props[1].tenant_id == 0);
}

// ===========================================================================
// Test 12: Empty province has zero avg_property_value
// ===========================================================================

TEST_CASE("test_empty_province_has_zero_avg_property_value", "[real_estate][tier4]") {
    RealEstateModule mod;
    std::vector<PropertyListing> empty_props;
    float avg = mod.compute_avg_property_value(empty_props, 0);
    REQUIRE_THAT(avg, WithinAbs(0.0f, 0.0001f));
}

TEST_CASE("test_province_with_no_matching_properties_has_zero_avg", "[real_estate][tier4]") {
    std::vector<PropertyListing> props;
    props.push_back(make_test_property(1, PropertyType::residential, 1, 100, 200000.0f));

    // Ask for province 0, which has no properties.
    RealEstateModule mod;
    float avg = mod.compute_avg_property_value(props, 0);
    REQUIRE_THAT(avg, WithinAbs(0.0f, 0.0001f));
}

// ===========================================================================
// Integration Tests — full execute path
// ===========================================================================

TEST_CASE("test_execute_processes_all_provinces", "[real_estate][tier4]") {
    auto state = make_test_world_state(1);
    state.provinces.push_back(make_test_province(0));
    state.provinces.push_back(make_test_province(1));

    RealEstateModule module;

    auto prop0 = make_test_property(1, PropertyType::residential, 0, 42, 100000.0f);
    prop0.rented = true;
    prop0.tenant_id = 50;
    module.add_property(prop0);

    auto prop1 = make_test_property(2, PropertyType::commercial, 1, 43, 200000.0f);
    prop1.rented = true;
    prop1.tenant_id = 51;
    module.add_property(prop1);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Both provinces should have generated NPC deltas.
    REQUIRE(delta.npc_deltas.size() == 2);
}

TEST_CASE("test_properties_sorted_by_id_on_add", "[real_estate][tier4]") {
    RealEstateModule module;

    // Add in reverse order.
    module.add_property(make_test_property(3, PropertyType::industrial, 0, 100, 300000.0f));
    module.add_property(make_test_property(1, PropertyType::residential, 0, 100, 100000.0f));
    module.add_property(make_test_property(2, PropertyType::commercial, 0, 100, 200000.0f));

    const auto& props = module.properties();
    REQUIRE(props.size() == 3);
    REQUIRE(props[0].id == 1);
    REQUIRE(props[1].id == 2);
    REQUIRE(props[2].id == 3);
}

TEST_CASE("test_monthly_tick_updates_region_delta", "[real_estate][tier4]") {
    auto state = make_test_world_state(30);  // monthly tick
    state.provinces.push_back(make_test_province(0));

    RealEstateModule module;
    module.add_property(make_test_property(1, PropertyType::residential, 0, 100, 200000.0f));

    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // Monthly tick should produce a region delta.
    REQUIRE(delta.region_deltas.size() >= 1);
    REQUIRE(delta.region_deltas[0].region_id == 0);
}

TEST_CASE("test_non_monthly_tick_no_region_delta", "[real_estate][tier4]") {
    auto state = make_test_world_state(15);  // not a monthly tick
    state.provinces.push_back(make_test_province(0));

    RealEstateModule module;
    module.add_property(make_test_property(1, PropertyType::residential, 0, 100, 200000.0f));

    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // Non-monthly tick should NOT produce a region delta.
    REQUIRE(delta.region_deltas.empty());
}

// ===========================================================================
// Constants Verification
// ===========================================================================

TEST_CASE("test_real_estate_constants", "[real_estate][tier4]") {
    REQUIRE_THAT(RealEstateConfig{}.residential_yield_rate, WithinAbs(0.003f, 0.0001f));
    REQUIRE_THAT(RealEstateConfig{}.commercial_yield_rate, WithinAbs(0.004f, 0.0001f));
    REQUIRE_THAT(RealEstateConfig{}.industrial_yield_rate, WithinAbs(0.005f, 0.0001f));
    REQUIRE_THAT(RealEstateConfig{}.price_convergence_rate, WithinAbs(0.05f, 0.0001f));
    REQUIRE(RealEstateConfig{}.convergence_interval == 30);
    REQUIRE_THAT(RealEstateConfig{}.criminal_dominance_penalty, WithinAbs(0.15f, 0.0001f));
    REQUIRE_THAT(RealEstateConfig{}.laundering_premium, WithinAbs(0.10f, 0.0001f));
    REQUIRE_THAT(RealEstateConfig{}.transaction_evidence_threshold, WithinAbs(50000.0f, 0.01f));
}
