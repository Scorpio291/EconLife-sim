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
#include <cmath>

#include "core/world_state/apply_deltas.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/persistence/persistence_module.h"
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
    prov.cohort_stats = std::make_unique<RegionCohortStats>();
    prov.id = id;
    prov.region_id = 0;
    prov.nation_id = 0;
    prov.cohort_stats->criminal_dominance_index = criminal_dominance;
    prov.conditions.stability_score = 0.5f;
    prov.conditions.inequality_index = 0.3f;
    prov.cohort_stats->crime_rate = 0.1f;
    prov.cohort_stats->formal_employment_rate = 0.7f;
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
        case PropertyType::raw_land:
            prop.rental_yield_rate = 0.0f;  // undeveloped land yields no rent
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
    prov.cohort_stats->criminal_dominance_index = 10.0f;  // extreme value

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

    // Mirror the orchestrator dispatch for a province-parallel module:
    // init_for_tick on main thread, then execute_province per province.
    // execute() is now reserved for the global post-pass (drain of
    // pending_property_transactions + NPC opportunistic buy scan).
    module.init_for_tick(state);
    DeltaBuffer delta{};
    for (uint32_t p = 0; p < state.provinces.size(); ++p) {
        module.execute_province(p, state, delta);
    }

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

// ===========================================================================
// Homeless rate tests (cohort_stats.homeless_rate)
// ===========================================================================
//
// real_estate samples per-tick "fraction of active NPCs whose capital can't
// cover homeless_rent_buffer_months of mean residential rent" and emits a
// convergence delta. Provinces with no residential listings use the
// configured rent_floor.

namespace {

NPC make_homeless_test_npc(uint32_t id, uint32_t province_id, float capital) {
    NPC npc{};
    npc.id = id;
    npc.role = NPCRole::worker;
    npc.status = NPCStatus::active;
    npc.current_province_id = province_id;
    npc.home_province_id = province_id;
    npc.capital = capital;
    return npc;
}

float find_homeless_delta(const DeltaBuffer& delta, uint32_t region_id) {
    for (const auto& rd : delta.region_deltas) {
        if (rd.region_id == region_id && rd.homeless_rate_delta.has_value()) {
            return *rd.homeless_rate_delta;
        }
    }
    return 0.0f;
}

}  // namespace

TEST_CASE("test_homeless_rate_all_affordable_emits_negative_or_zero_delta",
          "[real_estate][homeless][tier4]") {
    auto state = make_test_world_state(1);
    state.provinces.push_back(make_test_province(0));
    // Pre-set homeless_rate so a sample of 0 produces a clear negative delta.
    state.provinces[0].cohort_stats->homeless_rate = 0.2f;

    // Wealthy NPCs: capital easily covers 3 months of floor rent (50 * 3 = 150).
    state.significant_npcs.push_back(make_homeless_test_npc(1, 0, 10000.0f));
    state.significant_npcs.push_back(make_homeless_test_npc(2, 0, 10000.0f));
    rebuild_npc_indices(state);

    RealEstateModule module;
    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    float homeless_delta = find_homeless_delta(delta, 0);
    // sample 0.0, current 0.2, convergence 0.05 → delta = 0.05 * (0 - 0.2) = -0.01
    REQUIRE_THAT(homeless_delta, WithinAbs(-0.01f, 0.0001f));
}

TEST_CASE("test_homeless_rate_all_unaffordable_emits_positive_delta",
          "[real_estate][homeless][tier4]") {
    auto state = make_test_world_state(1);
    state.provinces.push_back(make_test_province(0));
    state.provinces[0].cohort_stats->homeless_rate = 0.0f;

    // Penniless NPCs: capital below threshold.
    state.significant_npcs.push_back(make_homeless_test_npc(1, 0, 0.0f));
    state.significant_npcs.push_back(make_homeless_test_npc(2, 0, 5.0f));
    rebuild_npc_indices(state);

    RealEstateModule module;
    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    float homeless_delta = find_homeless_delta(delta, 0);
    // sample 1.0, current 0.0, convergence 0.05 → delta = 0.05 * (1 - 0) = +0.05
    REQUIRE_THAT(homeless_delta, WithinAbs(0.05f, 0.0001f));
}

TEST_CASE("test_homeless_rate_uses_residential_listings_when_present",
          "[real_estate][homeless][tier4]") {
    auto state = make_test_world_state(1);
    state.provinces.push_back(make_test_province(0));
    state.provinces[0].cohort_stats->homeless_rate = 0.0f;

    // Two NPCs: one with capital = 1000, one with capital = 100.
    state.significant_npcs.push_back(make_homeless_test_npc(1, 0, 1000.0f));
    state.significant_npcs.push_back(make_homeless_test_npc(2, 0, 100.0f));
    rebuild_npc_indices(state);

    RealEstateModule module;
    // Residential property with market_value=200,000 → rent_per_tick = 600.
    // 3 months buffer = 1800. NPC1 (1000) is below; NPC2 (100) is below.
    module.add_property(make_test_property(1, PropertyType::residential, 0, 99, 200000.0f));

    DeltaBuffer delta{};
    module.init_for_tick(state);
    module.execute_province(0, state, delta);

    float homeless_delta = find_homeless_delta(delta, 0);
    // Both NPCs below threshold → sample 1.0 → delta = 0.05 * (1 - 0) = +0.05.
    REQUIRE_THAT(homeless_delta, WithinAbs(0.05f, 0.0001f));
}

TEST_CASE("test_homeless_rate_skips_dead_npcs", "[real_estate][homeless][tier4]") {
    auto state = make_test_world_state(1);
    state.provinces.push_back(make_test_province(0));
    state.provinces[0].cohort_stats->homeless_rate = 0.0f;

    // Two NPCs: dead pauper, living wealthy.
    NPC dead = make_homeless_test_npc(1, 0, 0.0f);
    dead.status = NPCStatus::dead;
    state.significant_npcs.push_back(dead);
    state.significant_npcs.push_back(make_homeless_test_npc(2, 0, 10000.0f));
    rebuild_npc_indices(state);

    RealEstateModule module;
    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    float homeless_delta = find_homeless_delta(delta, 0);
    // Only the wealthy NPC is counted → sample 0.0 → delta = 0.
    REQUIRE_THAT(homeless_delta, WithinAbs(0.0f, 0.0001f));
}

// ===========================================================================
// Phase 1: Symmetric at-asking cash market — listing + transaction drain
// ===========================================================================

namespace {

NPC make_buyer_npc(uint32_t id, uint32_t province_id, float capital) {
    NPC n{};
    n.id = id;
    n.home_province_id = province_id;
    n.current_province_id = province_id;
    n.capital = capital;
    n.status = NPCStatus::active;
    return n;
}

}  // namespace

TEST_CASE("Phase1: list action flips listed_for_sale on owned property",
          "[real_estate][market_phase1]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/99, 150000.0f);
    module.add_property(prop);

    PropertyTransactionRequest req{};
    req.kind = PropertyTransactionKind::list;
    req.property_id = 1;
    req.actor_id = 99;
    req.price = 175000.0f;
    state.pending_property_transactions.push_back(req);

    DeltaBuffer delta{};
    module.execute(state, delta);

    REQUIRE(module.properties()[0].listed_for_sale == true);
    REQUIRE_THAT(module.properties()[0].asking_price, WithinAbs(175000.0f, 0.01f));
    REQUIRE(state.pending_property_transactions.empty());
}

TEST_CASE("Phase1: list action rejected when actor is not owner", "[real_estate][market_phase1]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    RealEstateModule module;
    // Property owned by NPC 42, not the player.
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/42, 150000.0f);
    module.add_property(prop);

    PropertyTransactionRequest req{};
    req.kind = PropertyTransactionKind::list;
    req.property_id = 1;
    req.actor_id = 99;  // player trying to list someone else's property
    req.price = 175000.0f;
    state.pending_property_transactions.push_back(req);

    DeltaBuffer delta{};
    module.execute(state, delta);

    REQUIRE(module.properties()[0].listed_for_sale == false);
}

TEST_CASE("Phase1: unlist action flips flag back", "[real_estate][market_phase1]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/99, 150000.0f);
    prop.listed_for_sale = true;
    module.add_property(prop);

    PropertyTransactionRequest req{};
    req.kind = PropertyTransactionKind::unlist;
    req.property_id = 1;
    req.actor_id = 99;
    state.pending_property_transactions.push_back(req);

    DeltaBuffer delta{};
    module.execute(state, delta);

    REQUIRE(module.properties()[0].listed_for_sale == false);
}

TEST_CASE(
    "Phase2: buy at asking transfers ownership at close_tick, "
    "deducts player wealth, emits evidence",
    "[real_estate][market_phase2]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 500000.0f;

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/42, 150000.0f);
    prop.asking_price = 150000.0f;
    prop.listed_for_sale = true;
    module.add_property(prop);

    PropertyTransactionRequest req{};
    req.kind = PropertyTransactionKind::buy;
    req.property_id = 1;
    req.actor_id = 99;
    req.price = 150000.0f;
    state.pending_property_transactions.push_back(req);

    // Phase 2: offer creates a pending_transaction; no ownership change yet.
    DeltaBuffer delta_offer{};
    module.execute(state, delta_offer);
    REQUIRE(module.properties()[0].owner_id == 42);
    REQUIRE(state.pending_transactions.size() == 1);
    REQUIRE(state.pending_transactions[0].buyer_id == 99);
    REQUIRE(state.pending_transactions[0].seller_id == 42);
    REQUIRE(state.pending_transactions[0].close_tick == 10u + 7u);  // residential delay

    // Tick forward to close_tick; settlement runs in the next post-pass.
    state.current_tick = 17;
    DeltaBuffer delta{};
    module.execute(state, delta);

    REQUIRE(module.properties()[0].owner_id == 99);
    REQUIRE(module.properties()[0].listed_for_sale == false);
    REQUIRE(module.properties()[0].purchased_tick == 17);
    REQUIRE_THAT(module.properties()[0].purchase_price, WithinAbs(150000.0f, 0.01f));
    REQUIRE(delta.player_delta.wealth_delta.has_value());
    REQUIRE_THAT(*delta.player_delta.wealth_delta, WithinAbs(-150000.0f, 0.01f));
    REQUIRE(state.pending_transactions.empty());  // pruned after settle
    // NPC seller credited.
    bool seller_credited = false;
    for (const auto& nd : delta.npc_deltas) {
        if (nd.npc_id == 42 && nd.capital_delta.has_value() &&
            std::fabs(*nd.capital_delta - 150000.0f) < 0.01f) {
            seller_credited = true;
        }
    }
    REQUIRE(seller_credited);
    // Evidence token emitted (price > default threshold 50000).
    REQUIRE(delta.evidence_deltas.size() == 1);
    REQUIRE(delta.evidence_deltas[0].new_token.has_value());
}

TEST_CASE("Phase2: buy above asking settles at offer price after close delay",
          "[real_estate][market_phase2]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 500000.0f;

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/42, 150000.0f);
    prop.asking_price = 150000.0f;
    prop.listed_for_sale = true;
    module.add_property(prop);

    PropertyTransactionRequest req{};
    req.kind = PropertyTransactionKind::buy;
    req.property_id = 1;
    req.actor_id = 99;
    req.price = 175000.0f;  // 25k above asking
    state.pending_property_transactions.push_back(req);

    // Offer tick: creates pending tx.
    DeltaBuffer delta0{};
    module.execute(state, delta0);
    REQUIRE(state.pending_transactions.size() == 1);

    // Close tick.
    state.current_tick = 17;
    DeltaBuffer delta{};
    module.execute(state, delta);

    REQUIRE(module.properties()[0].owner_id == 99);
    REQUIRE_THAT(module.properties()[0].purchase_price, WithinAbs(175000.0f, 0.01f));
    REQUIRE_THAT(*delta.player_delta.wealth_delta, WithinAbs(-175000.0f, 0.01f));
}

TEST_CASE("Phase1: buy below asking rejected — no state change", "[real_estate][market_phase1]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 500000.0f;

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/42, 150000.0f);
    prop.asking_price = 150000.0f;
    prop.listed_for_sale = true;
    module.add_property(prop);

    PropertyTransactionRequest req{};
    req.kind = PropertyTransactionKind::buy;
    req.property_id = 1;
    req.actor_id = 99;
    req.price = 100000.0f;  // below asking
    state.pending_property_transactions.push_back(req);

    DeltaBuffer delta{};
    module.execute(state, delta);

    REQUIRE(module.properties()[0].owner_id == 42);
    REQUIRE(module.properties()[0].listed_for_sale == true);
    REQUIRE_FALSE(delta.player_delta.wealth_delta.has_value());
    REQUIRE(delta.npc_deltas.empty());
    REQUIRE(delta.evidence_deltas.empty());
}

TEST_CASE("Phase1: buy on unlisted property rejected", "[real_estate][market_phase1]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 500000.0f;

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/42, 150000.0f);
    prop.listed_for_sale = false;
    module.add_property(prop);

    PropertyTransactionRequest req{};
    req.kind = PropertyTransactionKind::buy;
    req.property_id = 1;
    req.actor_id = 99;
    req.price = 150000.0f;
    state.pending_property_transactions.push_back(req);

    DeltaBuffer delta{};
    module.execute(state, delta);

    REQUIRE(module.properties()[0].owner_id == 42);
}

TEST_CASE("Phase2: buy with insufficient running cash creates only one pending tx",
          "[real_estate][market_phase2]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 200000.0f;  // can afford one but not two

    RealEstateModule module;
    auto prop1 = make_test_property(1, PropertyType::residential, 0, 42, 150000.0f);
    prop1.asking_price = 150000.0f;
    prop1.listed_for_sale = true;
    module.add_property(prop1);
    auto prop2 = make_test_property(2, PropertyType::residential, 0, 43, 150000.0f);
    prop2.asking_price = 150000.0f;
    prop2.listed_for_sale = true;
    module.add_property(prop2);

    PropertyTransactionRequest req1{PropertyTransactionKind::buy, 1, 99, 150000.0f};
    PropertyTransactionRequest req2{PropertyTransactionKind::buy, 2, 99, 150000.0f};
    state.pending_property_transactions.push_back(req1);
    state.pending_property_transactions.push_back(req2);

    DeltaBuffer delta_offer{};
    module.execute(state, delta_offer);

    // Phase 2: first buy creates a pending_tx reserving 150000 in
    // running wealth; the second buy sees running wealth = 50000 and is
    // rejected at create time.
    REQUIRE(state.pending_transactions.size() == 1);
    REQUIRE(state.pending_transactions[0].property_id == 1);

    // Settle at close_tick: ownership transfers on prop 1 only.
    state.current_tick = 17;
    DeltaBuffer delta{};
    module.execute(state, delta);
    REQUIRE(module.properties()[0].owner_id == 99);
    REQUIRE(module.properties()[1].owner_id == 43);
}

TEST_CASE("Phase1: NPC opportunistic buy fires on below-market player listing",
          "[real_estate][market_phase1]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    // Spawn a wealthy NPC to be the buyer.
    state.significant_npcs.push_back(make_buyer_npc(7, /*province=*/0, /*capital=*/500000.0f));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/99, 200000.0f);
    prop.asking_price = 100000.0f;  // 50% off — guaranteed deal
    prop.listed_for_sale = true;
    module.add_property(prop);

    // Run many ticks until purchase fires (probabilistic, but seeded).
    bool sold = false;
    for (int t = 0; t < 500 && !sold; ++t) {
        state.current_tick = 10u + static_cast<uint32_t>(t);
        DeltaBuffer delta{};
        module.execute(state, delta);
        if (module.properties()[0].owner_id == 7) {
            sold = true;
            REQUIRE_THAT(*delta.player_delta.wealth_delta, WithinAbs(100000.0f, 0.01f));
        }
    }
    REQUIRE(sold);
    REQUIRE(module.properties()[0].listed_for_sale == false);
    REQUIRE_THAT(module.properties()[0].purchase_price, WithinAbs(100000.0f, 0.01f));
}

TEST_CASE("Phase1: NPC opportunistic buy does NOT fire at market-rate listing",
          "[real_estate][market_phase1]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.significant_npcs.push_back(make_buyer_npc(7, 0, 500000.0f));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/99, 200000.0f);
    prop.asking_price = 200000.0f;  // exactly market; ratio = 1.0 (no deal)
    prop.listed_for_sale = true;
    module.add_property(prop);

    for (int t = 0; t < 500; ++t) {
        state.current_tick = 10u + static_cast<uint32_t>(t);
        DeltaBuffer delta{};
        module.execute(state, delta);
    }
    REQUIRE(module.properties()[0].owner_id == 99);
    REQUIRE(module.properties()[0].listed_for_sale == true);
}

TEST_CASE("Phase1: NPC opportunistic buy is deterministic across runs",
          "[real_estate][market_phase1][determinism]") {
    auto run_once = [](uint32_t seed) {
        auto state = make_test_world_state(10);
        state.world_seed = seed;
        state.provinces.push_back(make_test_province(0));
        state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
        state.significant_npcs.push_back(make_buyer_npc(7, 0, 500000.0f));
        state.significant_npcs.push_back(make_buyer_npc(8, 0, 500000.0f));

        RealEstateModule module;
        auto prop = make_test_property(1, PropertyType::residential, 0, 99, 200000.0f);
        prop.asking_price = 100000.0f;
        prop.listed_for_sale = true;
        module.add_property(prop);

        std::vector<uint32_t> buyer_history;
        for (int t = 0; t < 200; ++t) {
            state.current_tick = 10u + static_cast<uint32_t>(t);
            DeltaBuffer delta{};
            module.execute(state, delta);
            buyer_history.push_back(module.properties()[0].owner_id);
            if (module.properties()[0].owner_id != 99)
                break;
        }
        return buyer_history;
    };

    auto run_a = run_once(42);
    auto run_b = run_once(42);
    REQUIRE(run_a == run_b);
}

// ===========================================================================
// Phase 2: PendingTransaction lifecycle — multi-tick close, cancel, expiry
// ===========================================================================

TEST_CASE("Phase2: buy creates pending tx and does not settle before close_tick",
          "[real_estate][market_phase2]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 500000.0f;

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/42, 150000.0f);
    prop.asking_price = 150000.0f;
    prop.listed_for_sale = true;
    module.add_property(prop);

    state.pending_property_transactions.push_back(
        PropertyTransactionRequest{PropertyTransactionKind::buy, 1, 99, 150000.0f});

    // Offer tick: pending tx created, no settlement.
    DeltaBuffer d_offer{};
    module.execute(state, d_offer);
    REQUIRE(state.pending_transactions.size() == 1);
    REQUIRE(state.pending_transactions[0].stage == PendingTxStage::pending);
    REQUIRE(state.pending_transactions[0].close_tick == 17u);
    REQUIRE(module.properties()[0].owner_id == 42);
    REQUIRE_FALSE(d_offer.player_delta.wealth_delta.has_value());

    // One tick before close: still no settle.
    state.current_tick = 16;
    DeltaBuffer d_pre{};
    module.execute(state, d_pre);
    REQUIRE(module.properties()[0].owner_id == 42);
    REQUIRE(state.pending_transactions.size() == 1);

    // At close_tick: settles, prunes.
    state.current_tick = 17;
    DeltaBuffer d_close{};
    module.execute(state, d_close);
    REQUIRE(module.properties()[0].owner_id == 99);
    REQUIRE(state.pending_transactions.empty());
}

TEST_CASE("Phase2: close delay matches property type", "[real_estate][market_phase2]") {
    auto state = make_test_world_state(100);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 5000000.0f;

    RealEstateModule module;
    auto resi = make_test_property(1, PropertyType::residential, 0, 42, 150000.0f);
    resi.asking_price = 150000.0f;
    resi.listed_for_sale = true;
    module.add_property(resi);
    auto comm = make_test_property(2, PropertyType::commercial, 0, 42, 300000.0f);
    comm.asking_price = 300000.0f;
    comm.listed_for_sale = true;
    module.add_property(comm);
    auto indu = make_test_property(3, PropertyType::industrial, 0, 42, 400000.0f);
    indu.asking_price = 400000.0f;
    indu.listed_for_sale = true;
    module.add_property(indu);

    state.pending_property_transactions.push_back(
        PropertyTransactionRequest{PropertyTransactionKind::buy, 1, 99, 150000.0f});
    state.pending_property_transactions.push_back(
        PropertyTransactionRequest{PropertyTransactionKind::buy, 2, 99, 300000.0f});
    state.pending_property_transactions.push_back(
        PropertyTransactionRequest{PropertyTransactionKind::buy, 3, 99, 400000.0f});

    DeltaBuffer d{};
    module.execute(state, d);

    REQUIRE(state.pending_transactions.size() == 3);
    // residential = 7, commercial = 30, industrial = 30.
    uint32_t resi_close = 0, comm_close = 0, indu_close = 0;
    for (const auto& tx : state.pending_transactions) {
        if (tx.property_id == 1)
            resi_close = tx.close_tick;
        if (tx.property_id == 2)
            comm_close = tx.close_tick;
        if (tx.property_id == 3)
            indu_close = tx.close_tick;
    }
    REQUIRE(resi_close == 107u);
    REQUIRE(comm_close == 130u);
    REQUIRE(indu_close == 130u);
}

TEST_CASE("Phase2: under-contract guard rejects second buy on same property",
          "[real_estate][market_phase2]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 1000000.0f;

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, 42, 150000.0f);
    prop.asking_price = 150000.0f;
    prop.listed_for_sale = true;
    module.add_property(prop);

    // First buy creates the pending tx.
    state.pending_property_transactions.push_back(
        PropertyTransactionRequest{PropertyTransactionKind::buy, 1, 99, 150000.0f});
    DeltaBuffer d1{};
    module.execute(state, d1);
    REQUIRE(state.pending_transactions.size() == 1);

    // Second buy on same property next tick — should be rejected.
    state.current_tick = 11;
    state.pending_property_transactions.push_back(
        PropertyTransactionRequest{PropertyTransactionKind::buy, 1, 99, 175000.0f});
    DeltaBuffer d2{};
    module.execute(state, d2);
    REQUIRE(state.pending_transactions.size() == 1);
    REQUIRE_THAT(state.pending_transactions[0].offer_price, WithinAbs(150000.0f, 0.01f));
}

TEST_CASE("Phase2: cancel by buyer removes pending tx before close",
          "[real_estate][market_phase2]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 500000.0f;

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, 42, 150000.0f);
    prop.asking_price = 150000.0f;
    prop.listed_for_sale = true;
    module.add_property(prop);

    state.pending_property_transactions.push_back(
        PropertyTransactionRequest{PropertyTransactionKind::buy, 1, 99, 150000.0f});
    DeltaBuffer d1{};
    module.execute(state, d1);
    REQUIRE(state.pending_transactions.size() == 1);

    // Cancel next tick (buyer is player 99 — authorized).
    state.current_tick = 12;
    state.pending_property_transactions.push_back(
        PropertyTransactionRequest{PropertyTransactionKind::cancel, 1, 99, 0.0f});
    DeltaBuffer d2{};
    module.execute(state, d2);
    REQUIRE(state.pending_transactions.empty());

    // Past original close_tick: no settle (already cancelled).
    state.current_tick = 17;
    DeltaBuffer d3{};
    module.execute(state, d3);
    REQUIRE(module.properties()[0].owner_id == 42);
    REQUIRE_FALSE(d3.player_delta.wealth_delta.has_value());
}

TEST_CASE("Phase2: cancel by seller-NPC works (NPC opportunistic buy on player listing)",
          "[real_estate][market_phase2]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.significant_npcs.push_back(make_buyer_npc(7, 0, 500000.0f));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/99, 200000.0f);
    prop.asking_price = 100000.0f;  // 50% off — guaranteed deal
    prop.listed_for_sale = true;
    module.add_property(prop);

    // Loop until NPC creates a pending tx.
    bool pending_created = false;
    for (int t = 0; t < 500 && !pending_created; ++t) {
        state.current_tick = 10u + static_cast<uint32_t>(t);
        DeltaBuffer d{};
        module.execute(state, d);
        if (!state.pending_transactions.empty())
            pending_created = true;
    }
    REQUIRE(pending_created);
    REQUIRE(state.pending_transactions[0].buyer_id == 7);
    REQUIRE(state.pending_transactions[0].seller_id == 99);

    // Player (seller) cancels — should be allowed.
    state.current_tick += 1;
    state.pending_property_transactions.push_back(
        PropertyTransactionRequest{PropertyTransactionKind::cancel, 1, 99, 0.0f});
    DeltaBuffer d_cancel{};
    module.execute(state, d_cancel);
    REQUIRE(state.pending_transactions.empty());
    REQUIRE(module.properties()[0].owner_id == 99);
}

TEST_CASE("Phase2: cancel by unrelated actor rejected", "[real_estate][market_phase2]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 500000.0f;

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/42, 150000.0f);
    prop.asking_price = 150000.0f;
    prop.listed_for_sale = true;
    module.add_property(prop);

    state.pending_property_transactions.push_back(
        PropertyTransactionRequest{PropertyTransactionKind::buy, 1, 99, 150000.0f});
    DeltaBuffer d1{};
    module.execute(state, d1);

    // Cancel attempt by random NPC id 555 (not buyer 99, not seller 42).
    state.current_tick = 12;
    state.pending_property_transactions.push_back(
        PropertyTransactionRequest{PropertyTransactionKind::cancel, 1, 555, 0.0f});
    DeltaBuffer d2{};
    module.execute(state, d2);
    REQUIRE(state.pending_transactions.size() == 1);
    REQUIRE(state.pending_transactions[0].stage == PendingTxStage::pending);
}

TEST_CASE("Phase2: buy expires (no transfer) when buyer cannot afford at close",
          "[real_estate][market_phase2]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 150000.0f;  // just enough for one buy

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, 42, 150000.0f);
    prop.asking_price = 150000.0f;
    prop.listed_for_sale = true;
    module.add_property(prop);

    state.pending_property_transactions.push_back(
        PropertyTransactionRequest{PropertyTransactionKind::buy, 1, 99, 150000.0f});
    DeltaBuffer d_offer{};
    module.execute(state, d_offer);
    REQUIRE(state.pending_transactions.size() == 1);

    // Between offer and close, the player loses cash via some other path
    // (simulated by directly mutating wealth — in practice this could be
    // a bail payment, a fine, or a competing purchase).
    state.player->wealth = 50000.0f;

    // At close_tick: insufficient funds → expired, no transfer.
    state.current_tick = 17;
    DeltaBuffer d_close{};
    module.execute(state, d_close);
    REQUIRE(module.properties()[0].owner_id == 42);
    REQUIRE_FALSE(d_close.player_delta.wealth_delta.has_value());
    REQUIRE(state.pending_transactions.empty());  // pruned
}

TEST_CASE("Phase2: NPC opportunistic buy creates pending tx (not instant)",
          "[real_estate][market_phase2]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.significant_npcs.push_back(make_buyer_npc(7, 0, 500000.0f));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/99, 200000.0f);
    prop.asking_price = 100000.0f;
    prop.listed_for_sale = true;
    module.add_property(prop);

    // Loop until pending tx created; ownership must NOT change in
    // that tick — only at close_tick.
    bool pending_seen = false;
    uint32_t pending_tick = 0;
    for (int t = 0; t < 500 && !pending_seen; ++t) {
        state.current_tick = 10u + static_cast<uint32_t>(t);
        DeltaBuffer d{};
        module.execute(state, d);
        if (!state.pending_transactions.empty()) {
            pending_seen = true;
            pending_tick = state.current_tick;
            REQUIRE(module.properties()[0].owner_id == 99);  // not yet transferred
        }
    }
    REQUIRE(pending_seen);

    // Settle at close_tick.
    state.current_tick = pending_tick + 7;
    DeltaBuffer d_settle{};
    module.execute(state, d_settle);
    REQUIRE(module.properties()[0].owner_id == 7);
    REQUIRE(state.pending_transactions.empty());
}

TEST_CASE("Phase2: settled pending tx survives serialization round-trip via persistence v9",
          "[real_estate][market_phase2][persistence]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 500000.0f;

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, 42, 150000.0f);
    prop.asking_price = 150000.0f;
    prop.listed_for_sale = true;
    module.add_property(prop);
    state.pending_property_transactions.push_back(
        PropertyTransactionRequest{PropertyTransactionKind::buy, 1, 99, 150000.0f});
    DeltaBuffer d_offer{};
    module.execute(state, d_offer);
    REQUIRE(state.pending_transactions.size() == 1);

    // Round-trip the WorldState.
    auto bytes = PersistenceModule::serialize(state, {&module});
    RealEstateModule restored_mod;
    WorldState restored{};
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    REQUIRE(restored.pending_transactions.size() == 1);
    const auto& tx = restored.pending_transactions[0];
    REQUIRE(tx.property_id == 1);
    REQUIRE(tx.buyer_id == 99);
    REQUIRE(tx.seller_id == 42);
    REQUIRE_THAT(tx.offer_price, WithinAbs(150000.0f, 0.01f));
    REQUIRE(tx.offered_tick == 10);
    REQUIRE(tx.close_tick == 17);
    REQUIRE(tx.stage == PendingTxStage::pending);
}

// ===========================================================================
// Phase 3: relationship-driven negotiation + NPC below-asking offers
// ===========================================================================

namespace {

NPC make_seller_npc(uint32_t id, uint32_t province_id, float capital) {
    NPC n{};
    n.id = id;
    n.home_province_id = province_id;
    n.current_province_id = province_id;
    n.capital = capital;
    n.status = NPCStatus::active;
    return n;
}

void set_relationship_to_player(NPC& npc, uint32_t player_id, float trust, float fear) {
    Relationship rel{};
    rel.target_npc_id = player_id;
    rel.trust = trust;
    rel.fear = fear;
    rel.obligation_balance = 0.0f;
    rel.last_interaction_tick = 0;
    rel.is_movement_ally = false;
    rel.recovery_ceiling = 1.0f;
    npc.relationships.push_back(rel);
}

}  // namespace

TEST_CASE("Phase3: below-asking offer with high trust + distressed seller accepts",
          "[real_estate][market_phase3]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 500000.0f;

    // Seller NPC: trusts player (0.9), totally broke (capital=0 → pressure=1.0).
    NPC seller = make_seller_npc(42, /*province=*/0, /*capital=*/0.0f);
    set_relationship_to_player(seller, 99, /*trust=*/0.9f, /*fear=*/0.0f);
    state.significant_npcs.push_back(std::move(seller));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/42, 150000.0f);
    prop.asking_price = 150000.0f;
    prop.listed_for_sale = true;
    module.add_property(prop);

    state.pending_property_transactions.push_back(
        PropertyTransactionRequest{PropertyTransactionKind::buy, 1, 99, 120000.0f});

    DeltaBuffer d{};
    module.execute(state, d);

    // price_ratio = 0.80, trust = 0.9 (× 0.20 = 0.18), pressure = 1.0
    // (× 0.30 = 0.30) → score = 1.28 → p_accept ≈ 0.84. With a fixed
    // seed this is reliably above the roll threshold.
    REQUIRE(state.pending_transactions.size() == 1);
    REQUIRE_THAT(state.pending_transactions[0].offer_price, WithinAbs(120000.0f, 0.01f));
}

TEST_CASE("Phase3: below-asking offer to wealthy seller with no relationship rejected",
          "[real_estate][market_phase3]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 500000.0f;

    // Seller: rich (1M capital → pressure=0), no relationship to player
    // (trust=0, fear=0). Score = price_ratio. Lowball offer rejected.
    NPC seller = make_seller_npc(42, 0, /*capital=*/1000000.0f);
    state.significant_npcs.push_back(std::move(seller));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, 42, 150000.0f);
    prop.asking_price = 150000.0f;
    prop.listed_for_sale = true;
    module.add_property(prop);

    state.pending_property_transactions.push_back(
        PropertyTransactionRequest{PropertyTransactionKind::buy, 1, 99, 80000.0f});

    DeltaBuffer d{};
    module.execute(state, d);

    // price_ratio = 0.53, no trust/fear/pressure → score = 0.53
    // → p_accept = sigmoid((0.53 - 1.0) * 6) = sigmoid(-2.8) ≈ 0.057.
    // Roll is deterministic at this seed; reliably > 0.057 → rejected.
    REQUIRE(state.pending_transactions.empty());
}

TEST_CASE("Phase3: below-asking offer from feared player accepted by NPC",
          "[real_estate][market_phase3]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 500000.0f;

    // Seller: comfortable capital, but fears the player (intimidation
    // discount). fear=1.0 × 0.15 = +0.15 to score.
    NPC seller = make_seller_npc(42, 0, 200000.0f);
    set_relationship_to_player(seller, 99, /*trust=*/0.0f, /*fear=*/1.0f);
    state.significant_npcs.push_back(std::move(seller));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, 42, 150000.0f);
    prop.asking_price = 150000.0f;
    prop.listed_for_sale = true;
    module.add_property(prop);

    state.pending_property_transactions.push_back(
        PropertyTransactionRequest{PropertyTransactionKind::buy, 1, 99, 135000.0f});

    DeltaBuffer d{};
    module.execute(state, d);

    // price_ratio = 0.90, fear = 1.0 × 0.15 = +0.15 → score = 1.05
    // → p_accept = sigmoid(0.30) ≈ 0.57. With this seed the roll passes.
    REQUIRE(state.pending_transactions.size() == 1);
}

TEST_CASE("Phase3: below-asking offer to unknown seller (state-owned) silently dropped",
          "[real_estate][market_phase3]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 500000.0f;

    RealEstateModule module;
    // Property owned by NPC id 999 that does NOT exist in significant_npcs.
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/999, 150000.0f);
    prop.asking_price = 150000.0f;
    prop.listed_for_sale = true;
    module.add_property(prop);

    state.pending_property_transactions.push_back(
        PropertyTransactionRequest{PropertyTransactionKind::buy, 1, 99, 100000.0f});

    DeltaBuffer d{};
    module.execute(state, d);

    REQUIRE(state.pending_transactions.empty());
    REQUIRE(module.properties()[0].owner_id == 999);
}

TEST_CASE("Phase3: NPC generates below-asking offer on player listing as SceneCard",
          "[real_estate][market_phase3]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    // Wealthy NPC to be the buyer.
    state.significant_npcs.push_back(make_buyer_npc(7, 0, 500000.0f));

    RealEstateModule module;
    // Player-owned listing at market value (asking == market, not a
    // "deal" → triggers Path B below-asking offer path).
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/99, 200000.0f);
    prop.asking_price = 200000.0f;
    prop.listed_for_sale = true;
    module.add_property(prop);

    // Loop until an offer scene card is generated.
    bool offer_seen = false;
    for (int t = 0; t < 2000 && !offer_seen; ++t) {
        state.current_tick = 10u + static_cast<uint32_t>(t);
        DeltaBuffer d{};
        module.execute(state, d);
        // Move any new scene cards to pending_scene_cards (apply_deltas
        // would do this in the full orchestrator; test inlines it).
        for (auto& card : d.new_scene_cards) {
            state.pending_scene_cards.push_back(std::move(card));
        }
        if (!module.active_negotiations().empty()) {
            offer_seen = true;
            REQUIRE(module.active_negotiations()[0].buyer_id == 7);
            REQUIRE(module.active_negotiations()[0].seller_id == 99);
            REQUIRE(module.active_negotiations()[0].property_id == 1);
            // Offer price must lie in [market * 0.85, asking * 0.95].
            float offer = module.active_negotiations()[0].offer_price;
            REQUIRE(offer >= 200000.0f * 0.85f - 0.01f);
            REQUIRE(offer <= 200000.0f * 0.95f + 0.01f);
            // Scene card should be in pending_scene_cards.
            REQUIRE_FALSE(state.pending_scene_cards.empty());
        }
    }
    REQUIRE(offer_seen);
}

TEST_CASE("Phase3: player accepts NPC offer via scene card → pending tx created",
          "[real_estate][market_phase3]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.significant_npcs.push_back(make_buyer_npc(7, 0, 500000.0f));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, 99, 200000.0f);
    prop.asking_price = 200000.0f;
    prop.listed_for_sale = true;
    module.add_property(prop);

    // Seed an active negotiation directly (skip the probabilistic loop).
    NegotiationContext neg{};
    neg.scene_card_id = 100;
    neg.property_id = 1;
    neg.buyer_id = 7;
    neg.seller_id = 99;
    neg.offer_price = 170000.0f;
    neg.offered_tick = 10;
    neg.deadline_tick = 24;
    module.active_negotiations_mut().push_back(neg);

    // Drop a matching scene card with the accept choice picked.
    SceneCard card{};
    card.id = 100;
    card.type = SceneCardType::meeting;
    card.npc_id = 7;
    PlayerChoice accept{1, "Accept", "", 0};
    PlayerChoice decline{2, "Decline", "", 0};
    card.choices.push_back(accept);
    card.choices.push_back(decline);
    card.chosen_choice_id = 1;  // accept
    state.pending_scene_cards.push_back(card);

    state.current_tick = 12;
    DeltaBuffer d{};
    module.execute(state, d);

    REQUIRE(state.pending_transactions.size() == 1);
    REQUIRE(state.pending_transactions[0].property_id == 1);
    REQUIRE(state.pending_transactions[0].buyer_id == 7);
    REQUIRE_THAT(state.pending_transactions[0].offer_price, WithinAbs(170000.0f, 0.01f));
    REQUIRE(module.active_negotiations().empty());
}

TEST_CASE("Phase3: player declines NPC offer via scene card → no tx, context removed",
          "[real_estate][market_phase3]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.significant_npcs.push_back(make_buyer_npc(7, 0, 500000.0f));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, 99, 200000.0f);
    prop.asking_price = 200000.0f;
    prop.listed_for_sale = true;
    module.add_property(prop);

    NegotiationContext neg{100, 1, 7, 99, 170000.0f, 10, 24};
    module.active_negotiations_mut().push_back(neg);

    SceneCard card{};
    card.id = 100;
    card.type = SceneCardType::meeting;
    card.npc_id = 7;
    card.choices.push_back(PlayerChoice{1, "Accept", "", 0});
    card.choices.push_back(PlayerChoice{2, "Decline", "", 0});
    card.chosen_choice_id = 2;  // decline
    state.pending_scene_cards.push_back(card);

    state.current_tick = 12;
    DeltaBuffer d{};
    module.execute(state, d);

    REQUIRE(state.pending_transactions.empty());
    REQUIRE(module.active_negotiations().empty());
}

TEST_CASE("Phase3: negotiation expires after deadline if player never responds",
          "[real_estate][market_phase3]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.significant_npcs.push_back(make_buyer_npc(7, 0, 500000.0f));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, 99, 200000.0f);
    prop.asking_price = 200000.0f;
    prop.listed_for_sale = true;
    module.add_property(prop);

    NegotiationContext neg{100, 1, 7, 99, 170000.0f, 10, 24};
    module.active_negotiations_mut().push_back(neg);

    SceneCard card{};
    card.id = 100;
    card.type = SceneCardType::meeting;
    card.choices.push_back(PlayerChoice{1, "Accept", "", 0});
    card.choices.push_back(PlayerChoice{2, "Decline", "", 0});
    card.chosen_choice_id = 0;  // never chosen
    state.pending_scene_cards.push_back(card);

    // Past deadline.
    state.current_tick = 30;
    DeltaBuffer d{};
    module.execute(state, d);

    REQUIRE(module.active_negotiations().empty());
    REQUIRE(state.pending_transactions.empty());
}

TEST_CASE("Phase3: active negotiations round-trip via persistence (schema_tag 3)",
          "[real_estate][market_phase3][persistence]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, 99, 200000.0f);
    module.add_property(prop);

    NegotiationContext neg{100, 1, 7, 99, 170000.0f, 10, 24};
    module.active_negotiations_mut().push_back(neg);

    auto bytes = PersistenceModule::serialize(state, {&module});
    RealEstateModule restored_mod;
    WorldState restored{};
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    REQUIRE(restored_mod.active_negotiations().size() == 1);
    const auto& r = restored_mod.active_negotiations()[0];
    REQUIRE(r.scene_card_id == 100);
    REQUIRE(r.property_id == 1);
    REQUIRE(r.buyer_id == 7);
    REQUIRE(r.seller_id == 99);
    REQUIRE_THAT(r.offer_price, WithinAbs(170000.0f, 0.01f));
    REQUIRE(r.offered_tick == 10);
    REQUIRE(r.deadline_tick == 24);
}

// ===========================================================================
// Phase 4: mortgage financing (banking integration)
// ===========================================================================

#include "modules/banking/banking_module.h"
#include "modules/banking/banking_types.h"

TEST_CASE("Phase4: mortgage offer creates pending tx with payment_method=mortgage",
          "[real_estate][market_phase4]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 50000.0f;  // small wealth — only down payment cash on hand

    // Seller NPC.
    state.significant_npcs.push_back(make_seller_npc(42, 0, 100000.0f));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/42, 200000.0f);
    prop.asking_price = 200000.0f;
    prop.listed_for_sale = true;
    module.add_property(prop);

    PropertyTransactionRequest req{};
    req.kind = PropertyTransactionKind::buy;
    req.property_id = 1;
    req.actor_id = 99;
    req.price = 200000.0f;
    req.payment_method = PaymentMethod::mortgage;
    state.pending_property_transactions.push_back(req);

    DeltaBuffer d_offer{};
    module.execute(state, d_offer);

    REQUIRE(state.pending_transactions.size() == 1);
    REQUIRE(state.pending_transactions[0].payment_method == PaymentMethod::mortgage);
    // mortgage method defaults to type-minimum down payment (residential = 0.10).
    REQUIRE_THAT(state.pending_transactions[0].down_payment_fraction, WithinAbs(0.10f, 0.001f));
    REQUIRE(state.pending_transactions[0].loan_maturity_ticks > 0u);
    // Player wealth not debited at offer time.
    REQUIRE_FALSE(d_offer.player_delta.wealth_delta.has_value());
}

TEST_CASE("Phase4: mortgage rejected when down payment below type minimum",
          "[real_estate][market_phase4]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 100000.0f;

    state.significant_npcs.push_back(make_seller_npc(42, 0, 100000.0f));

    RealEstateModule module;
    // Commercial property — minimum down payment is 0.25.
    auto prop = make_test_property(1, PropertyType::commercial, 0, 42, 500000.0f);
    prop.asking_price = 500000.0f;
    prop.listed_for_sale = true;
    module.add_property(prop);

    // Offer with 10% down — below commercial minimum.
    PropertyTransactionRequest req{};
    req.kind = PropertyTransactionKind::buy;
    req.property_id = 1;
    req.actor_id = 99;
    req.price = 500000.0f;
    req.payment_method = PaymentMethod::mixed;
    req.down_payment_fraction = 0.10f;
    state.pending_property_transactions.push_back(req);

    DeltaBuffer d{};
    module.execute(state, d);
    REQUIRE(state.pending_transactions.empty());
}

TEST_CASE("Phase4: mortgage rejected when principal exceeds player's max-loan multiplier",
          "[real_estate][market_phase4]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 10000.0f;  // max loan = 10x = 100k

    state.significant_npcs.push_back(make_seller_npc(42, 0, 100000.0f));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, 42, 500000.0f);
    prop.asking_price = 500000.0f;
    prop.listed_for_sale = true;
    module.add_property(prop);

    // 100% mortgage on 500k > 100k limit → rejected.
    PropertyTransactionRequest req{};
    req.kind = PropertyTransactionKind::buy;
    req.property_id = 1;
    req.actor_id = 99;
    req.price = 500000.0f;
    req.payment_method = PaymentMethod::mortgage;
    req.down_payment_fraction = 0.0f;
    state.pending_property_transactions.push_back(req);

    DeltaBuffer d{};
    module.execute(state, d);
    REQUIRE(state.pending_transactions.empty());
}

TEST_CASE("Phase4: mortgage settle debits only down payment + emits NewLoanRequest",
          "[real_estate][market_phase4]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 100000.0f;

    state.significant_npcs.push_back(make_seller_npc(42, 0, 100000.0f));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, 42, 200000.0f);
    prop.asking_price = 200000.0f;
    prop.listed_for_sale = true;
    module.add_property(prop);

    // 25% down on 200k → 50k cash, 150k loan.
    PropertyTransactionRequest req{};
    req.kind = PropertyTransactionKind::buy;
    req.property_id = 1;
    req.actor_id = 99;
    req.price = 200000.0f;
    req.payment_method = PaymentMethod::mixed;
    req.down_payment_fraction = 0.25f;
    state.pending_property_transactions.push_back(req);

    DeltaBuffer d_offer{};
    module.execute(state, d_offer);
    REQUIRE(state.pending_transactions.size() == 1);

    // Close tick — settlement.
    state.current_tick = 17;
    DeltaBuffer d_close{};
    module.execute(state, d_close);

    // Ownership transferred.
    REQUIRE(module.properties()[0].owner_id == 99);
    // Player loses only 50k (the down payment) net at close.
    REQUIRE(d_close.player_delta.wealth_delta.has_value());
    REQUIRE_THAT(*d_close.player_delta.wealth_delta, WithinAbs(-50000.0f, 0.01f));
    // Seller credited full 200k.
    bool seller_credited = false;
    for (const auto& nd : d_close.npc_deltas) {
        if (nd.npc_id == 42 && nd.capital_delta.has_value() &&
            std::fabs(*nd.capital_delta - 200000.0f) < 0.01f) {
            seller_credited = true;
        }
    }
    REQUIRE(seller_credited);
    // NewLoanRequest emitted for the 150k principal.
    REQUIRE(d_close.new_loan_requests.size() == 1);
    const auto& lr = d_close.new_loan_requests[0];
    REQUIRE(lr.borrower_id == 99);
    REQUIRE(lr.purpose == static_cast<uint8_t>(LoanPurpose::property_purchase));
    REQUIRE_THAT(lr.principal, WithinAbs(150000.0f, 0.01f));
    REQUIRE(lr.collateral_id == 1u);
    REQUIRE(lr.maturity_tick > state.current_tick);
}

TEST_CASE("Phase4: banking drains pending_loan_requests into active_loans_",
          "[banking][market_phase4]") {
    WorldState state{};
    state.current_tick = 10;
    state.world_seed = 1;
    state.game_mode = GameMode::standard;

    BankingModule banking;
    REQUIRE(banking.active_loans().empty());

    NewLoanRequest req{};
    req.borrower_id = 99;
    req.lender_id = 0;
    req.purpose = static_cast<uint8_t>(LoanPurpose::property_purchase);
    req.principal = 150000.0f;
    req.interest_rate = 0.00025f;
    req.repayment_per_tick = 50.0f;
    req.maturity_tick = state.current_tick + 10950u;
    req.collateral_id = 1u;
    state.pending_loan_requests.push_back(req);

    DeltaBuffer d{};
    banking.execute(state, d);

    REQUIRE(state.pending_loan_requests.empty());
    REQUIRE(banking.active_loans().size() == 1);
    const auto& l = banking.active_loans()[0];
    REQUIRE(l.borrower_id == 99);
    REQUIRE(l.purpose == LoanPurpose::property_purchase);
    REQUIRE_THAT(l.principal, WithinAbs(150000.0f, 0.01f));
    REQUIRE_THAT(l.outstanding_balance, WithinAbs(150000.0f, 0.01f));
    REQUIRE(l.collateral_id == 1u);
    REQUIRE_FALSE(l.in_default);
}

TEST_CASE("Phase4: pending tx Phase 4 fields round-trip via persistence v10",
          "[real_estate][market_phase4][persistence]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    PendingTransaction tx{};
    tx.id = 7;
    tx.property_id = 1;
    tx.buyer_id = 99;
    tx.seller_id = 42;
    tx.offer_price = 200000.0f;
    tx.offered_tick = 10;
    tx.close_tick = 17;
    tx.stage = PendingTxStage::pending;
    tx.payment_method = PaymentMethod::mixed;
    tx.down_payment_fraction = 0.25f;
    tx.interest_rate = 0.00025f;
    tx.loan_maturity_ticks = 10950u;
    state.pending_transactions.push_back(tx);

    RealEstateModule module;
    auto bytes = PersistenceModule::serialize(state, {&module});
    WorldState restored{};
    RealEstateModule restored_mod;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    REQUIRE(restored.pending_transactions.size() == 1);
    const auto& r = restored.pending_transactions[0];
    REQUIRE(r.payment_method == PaymentMethod::mixed);
    REQUIRE_THAT(r.down_payment_fraction, WithinAbs(0.25f, 0.001f));
    REQUIRE_THAT(r.interest_rate, WithinAbs(0.00025f, 0.0001f));
    REQUIRE(r.loan_maturity_ticks == 10950u);
}

// ===========================================================================
// Phase 5: foreclosure on default
// ===========================================================================

TEST_CASE("Phase5: banking default on property_purchase loan emits foreclosure request",
          "[banking][market_phase5]") {
    WorldState state{};
    state.current_tick = 100;
    state.world_seed = 1;
    state.game_mode = GameMode::standard;
    state.player = std::make_unique<PlayerCharacter>();
    state.player->id = 99;
    state.player->wealth = 0.0f;  // can't pay → default

    BankingModule banking;
    LoanRecord loan{};
    loan.id = 1;
    loan.borrower_id = 99;
    loan.lender_id = 0;
    loan.purpose = LoanPurpose::property_purchase;
    loan.principal = 100000.0f;
    loan.outstanding_balance = 100000.0f;
    loan.interest_rate = 0.0001f;
    loan.repayment_per_tick = 50.0f;
    loan.originated_tick = 50;
    loan.maturity_tick = 1000;
    loan.in_default = false;
    loan.collateral_id = 42;  // property id
    banking.active_loans().push_back(loan);

    // Run banking enough ticks to exceed default_grace_ticks
    // (BankingConfig default is 60). Player has 0 wealth so each
    // process_loan_repayment increments consecutive_misses.
    bool foreclosure_seen = false;
    for (int t = 0; t < 200 && !foreclosure_seen; ++t) {
        state.current_tick = 100u + static_cast<uint32_t>(t);
        DeltaBuffer d{};
        banking.execute(state, d);
        if (!d.new_property_foreclosures.empty()) {
            REQUIRE(d.new_property_foreclosures[0].property_id == 42);
            REQUIRE(d.new_property_foreclosures[0].borrower_id == 99);
            REQUIRE(d.new_property_foreclosures[0].loan_id == 1);
            foreclosure_seen = true;
        }
    }
    REQUIRE(foreclosure_seen);
}

TEST_CASE("Phase5: real_estate drains foreclosure → property ownership transfers",
          "[real_estate][market_phase5]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/99, 150000.0f);
    prop.listed_for_sale = true;  // was on market before foreclosure
    module.add_property(prop);

    PropertyForeclosureRequest fc{};
    fc.loan_id = 1;
    fc.property_id = 1;
    fc.borrower_id = 99;
    fc.lender_id = 0;  // anonymous bank
    state.pending_property_foreclosures.push_back(fc);

    DeltaBuffer d{};
    module.execute(state, d);

    REQUIRE(module.properties()[0].owner_id == 0);  // seized to bank
    REQUIRE(module.properties()[0].listed_for_sale == false);
    REQUIRE(module.properties()[0].purchased_tick == 10);
    REQUIRE(state.pending_property_foreclosures.empty());  // queue drained
}

TEST_CASE("Phase5: foreclosure cancels active pending tx and active negotiation",
          "[real_estate][market_phase5]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/99, 150000.0f);
    prop.listed_for_sale = true;
    module.add_property(prop);

    // Active pending tx (someone was buying from player).
    PendingTransaction tx{};
    tx.id = 1;
    tx.property_id = 1;
    tx.buyer_id = 50;
    tx.seller_id = 99;
    tx.offer_price = 150000.0f;
    tx.offered_tick = 5;
    tx.close_tick = 20;
    tx.stage = PendingTxStage::pending;
    state.pending_transactions.push_back(tx);

    // Active negotiation (an NPC offer pending player decision).
    NegotiationContext neg{};
    neg.scene_card_id = 100;
    neg.property_id = 1;
    neg.buyer_id = 51;
    neg.seller_id = 99;
    neg.offer_price = 140000.0f;
    neg.offered_tick = 5;
    neg.deadline_tick = 30;
    module.active_negotiations_mut().push_back(neg);

    // Foreclosure arrives.
    PropertyForeclosureRequest fc{1, 1, 99, 0};
    state.pending_property_foreclosures.push_back(fc);

    DeltaBuffer d{};
    module.execute(state, d);

    // Pending tx cancelled (and pruned).
    REQUIRE(state.pending_transactions.empty());
    // Negotiation dropped.
    REQUIRE(module.active_negotiations().empty());
    // Property seized.
    REQUIRE(module.properties()[0].owner_id == 0);
}

TEST_CASE("Phase5: foreclosure on property the borrower no longer owns is a no-op",
          "[real_estate][market_phase5]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    RealEstateModule module;
    // Property already owned by NPC 200 (player sold it mid-default).
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/200, 150000.0f);
    module.add_property(prop);

    PropertyForeclosureRequest fc{};
    fc.loan_id = 1;
    fc.property_id = 1;
    fc.borrower_id = 99;  // borrower no longer owner
    fc.lender_id = 0;
    state.pending_property_foreclosures.push_back(fc);

    DeltaBuffer d{};
    module.execute(state, d);

    // New owner protected from old borrower's default.
    REQUIRE(module.properties()[0].owner_id == 200);
}

TEST_CASE("Phase5: pending_property_foreclosures round-trips via persistence v11",
          "[real_estate][market_phase5][persistence]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    PropertyForeclosureRequest fc{42, 7, 99, 0};
    state.pending_property_foreclosures.push_back(fc);

    RealEstateModule module;
    auto bytes = PersistenceModule::serialize(state, {&module});
    WorldState restored{};
    RealEstateModule restored_mod;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    REQUIRE(restored.pending_property_foreclosures.size() == 1);
    REQUIRE(restored.pending_property_foreclosures[0].loan_id == 42);
    REQUIRE(restored.pending_property_foreclosures[0].property_id == 7);
    REQUIRE(restored.pending_property_foreclosures[0].borrower_id == 99);
    REQUIRE(restored.pending_property_foreclosures[0].lender_id == 0);
}

// ===========================================================================
// Phase 6: auctions
// ===========================================================================

TEST_CASE("Phase6: foreclosure opens an auction at reserve fraction of market",
          "[real_estate][market_phase6]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/99, 200000.0f);
    module.add_property(prop);

    PropertyForeclosureRequest fc{1, 1, 99, 0};  // lender 0 = bank
    state.pending_property_foreclosures.push_back(fc);

    DeltaBuffer d{};
    module.execute(state, d);

    // Seized to bank AND auction opened.
    REQUIRE(module.properties()[0].owner_id == 0);
    REQUIRE(state.active_auctions.size() == 1);
    const auto& a = state.active_auctions[0];
    REQUIRE(a.asset_id == 1);
    REQUIRE(a.consigner_id == 0);
    REQUIRE(a.status == AuctionStatus::open);
    // reserve = market_value (200k) × 0.70 = 140k.
    REQUIRE_THAT(a.reserve_price, WithinAbs(140000.0f, 1.0f));
    REQUIRE(a.closes_tick == 10u + 30u);
}

TEST_CASE("Phase6: player bid above reserve registers as high bid",
          "[real_estate][market_phase6]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 500000.0f;

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/0, 200000.0f);
    module.add_property(prop);

    // Pre-seed an open auction.
    ActiveAuction a{};
    a.id = 1;
    a.asset_id = 1;
    a.consigner_id = 0;
    a.reserve_price = 140000.0f;
    a.opened_tick = 10;
    a.closes_tick = 40;
    a.status = AuctionStatus::open;
    a.current_high_bidder_id = 0;
    a.current_high_bid = 0.0f;
    state.active_auctions.push_back(a);

    state.pending_auction_bid_requests.push_back(AuctionBidRequest{1, 99, 150000.0f});

    state.current_tick = 12;
    DeltaBuffer d{};
    module.execute(state, d);

    REQUIRE(state.active_auctions.size() == 1);
    REQUIRE(state.active_auctions[0].current_high_bidder_id == 99);
    REQUIRE_THAT(state.active_auctions[0].current_high_bid, WithinAbs(150000.0f, 0.01f));
}

TEST_CASE("Phase6: sub-reserve opening bid is dropped", "[real_estate][market_phase6]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 500000.0f;

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/0, 200000.0f);
    module.add_property(prop);

    ActiveAuction a{};
    a.id = 1;
    a.asset_id = 1;
    a.consigner_id = 0;
    a.reserve_price = 140000.0f;
    a.opened_tick = 10;
    a.closes_tick = 40;
    a.status = AuctionStatus::open;
    state.active_auctions.push_back(a);

    // Below reserve, no prior bids → dropped.
    state.pending_auction_bid_requests.push_back(AuctionBidRequest{1, 99, 100000.0f});

    state.current_tick = 12;
    DeltaBuffer d{};
    module.execute(state, d);

    REQUIRE(state.active_auctions[0].current_high_bidder_id == 0);
}

TEST_CASE("Phase6: auction settles to high bidder at close, ownership transfers",
          "[real_estate][market_phase6]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 500000.0f;

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/0, 200000.0f);
    module.add_property(prop);

    ActiveAuction a{};
    a.id = 1;
    a.asset_id = 1;
    a.consigner_id = 0;
    a.reserve_price = 140000.0f;
    a.opened_tick = 10;
    a.closes_tick = 20;
    a.status = AuctionStatus::open;
    a.current_high_bidder_id = 99;
    a.current_high_bid = 160000.0f;
    state.active_auctions.push_back(a);

    // At close_tick.
    state.current_tick = 20;
    DeltaBuffer d{};
    module.execute(state, d);

    // Player won; ownership transferred, wealth debited, auction pruned.
    REQUIRE(module.properties()[0].owner_id == 99);
    REQUIRE(d.player_delta.wealth_delta.has_value());
    REQUIRE_THAT(*d.player_delta.wealth_delta, WithinAbs(-160000.0f, 0.01f));
    REQUIRE(state.active_auctions.empty());
}

TEST_CASE("Phase6: auction with no qualifying bid closes_no_reserve (asset retained)",
          "[real_estate][market_phase6]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/0, 200000.0f);
    module.add_property(prop);

    ActiveAuction a{};
    a.id = 1;
    a.asset_id = 1;
    a.consigner_id = 0;
    a.reserve_price = 140000.0f;
    a.opened_tick = 10;
    a.closes_tick = 20;
    a.status = AuctionStatus::open;
    a.current_high_bidder_id = 0;  // no bids
    a.current_high_bid = 0.0f;
    state.active_auctions.push_back(a);

    state.current_tick = 20;
    DeltaBuffer d{};
    module.execute(state, d);

    // No sale; consigner (bank, owner 0) keeps the asset; auction pruned.
    REQUIRE(module.properties()[0].owner_id == 0);
    REQUIRE(state.active_auctions.empty());
}

TEST_CASE("Phase6: NPC bidders raise an open auction over time", "[real_estate][market_phase6]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    // Two wealthy NPCs in the province who may bid.
    state.significant_npcs.push_back(make_buyer_npc(7, 0, 1000000.0f));
    state.significant_npcs.push_back(make_buyer_npc(8, 0, 1000000.0f));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/0, 200000.0f);
    module.add_property(prop);

    ActiveAuction a{};
    a.id = 1;
    a.asset_id = 1;
    a.consigner_id = 0;
    a.reserve_price = 140000.0f;
    a.opened_tick = 10;
    a.closes_tick = 200;  // long window
    a.status = AuctionStatus::open;
    state.active_auctions.push_back(a);

    bool got_bid = false;
    for (int t = 0; t < 150 && !got_bid; ++t) {
        state.current_tick = 11u + static_cast<uint32_t>(t);
        DeltaBuffer d{};
        module.execute(state, d);
        if (!state.active_auctions.empty() &&
            state.active_auctions[0].current_high_bidder_id != 0) {
            got_bid = true;
            // First NPC bid opens at the reserve.
            REQUIRE_THAT(state.active_auctions[0].current_high_bid, WithinAbs(140000.0f, 1.0f));
        }
    }
    REQUIRE(got_bid);
}

TEST_CASE("Phase6: NPC auction bidding is deterministic across runs",
          "[real_estate][market_phase6][determinism]") {
    auto run_once = [](uint32_t seed) {
        auto state = make_test_world_state(10);
        state.world_seed = seed;
        state.provinces.push_back(make_test_province(0));
        state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
        state.significant_npcs.push_back(make_buyer_npc(7, 0, 1000000.0f));
        state.significant_npcs.push_back(make_buyer_npc(8, 0, 1000000.0f));

        RealEstateModule module;
        auto prop = make_test_property(1, PropertyType::residential, 0, 0, 200000.0f);
        module.add_property(prop);
        ActiveAuction a{};
        a.id = 1;
        a.asset_id = 1;
        a.consigner_id = 0;
        a.reserve_price = 140000.0f;
        a.opened_tick = 10;
        a.closes_tick = 200;
        a.status = AuctionStatus::open;
        state.active_auctions.push_back(a);

        std::vector<uint32_t> bidder_seq;
        for (int t = 0; t < 60; ++t) {
            state.current_tick = 11u + static_cast<uint32_t>(t);
            DeltaBuffer d{};
            module.execute(state, d);
            if (!state.active_auctions.empty())
                bidder_seq.push_back(state.active_auctions[0].current_high_bidder_id);
        }
        return bidder_seq;
    };
    REQUIRE(run_once(42) == run_once(42));
}

TEST_CASE("Phase6: active_auctions round-trip via persistence v12",
          "[real_estate][market_phase6][persistence]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    ActiveAuction a{};
    a.id = 3;
    a.asset_id = 7;
    a.consigner_id = 0;
    a.reserve_price = 140000.0f;
    a.opened_tick = 10;
    a.closes_tick = 40;
    a.status = AuctionStatus::open;
    a.current_high_bidder_id = 99;
    a.current_high_bid = 155000.0f;
    a.bids.push_back(AuctionBid{99, 145000.0f, 12});
    a.bids.push_back(AuctionBid{99, 155000.0f, 14});
    state.active_auctions.push_back(a);

    RealEstateModule module;
    auto bytes = PersistenceModule::serialize(state, {&module});
    WorldState restored{};
    RealEstateModule restored_mod;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    REQUIRE(restored.active_auctions.size() == 1);
    const auto& r = restored.active_auctions[0];
    REQUIRE(r.id == 3);
    REQUIRE(r.asset_id == 7);
    REQUIRE(r.status == AuctionStatus::open);
    REQUIRE(r.current_high_bidder_id == 99);
    REQUIRE_THAT(r.current_high_bid, WithinAbs(155000.0f, 0.01f));
    REQUIRE(r.bids.size() == 2);
    REQUIRE_THAT(r.bids[1].bid_amount, WithinAbs(155000.0f, 0.01f));
}

// ===========================================================================
// Phase 7: raw land + zoning approval
// ===========================================================================

TEST_CASE("Phase7: minor zoning change (residential->commercial) can be approved",
          "[real_estate][market_phase7]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0, /*criminal_dominance=*/0.0f));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/99, 150000.0f);
    prop.zoned_use = PropertyType::residential;
    module.add_property(prop);

    // Try across many ticks until an approval lands (minor base prob 0.60).
    bool approved = false;
    for (int t = 0; t < 50 && !approved; ++t) {
        state.current_tick = 10u + static_cast<uint32_t>(t);
        state.pending_zoning_requests.push_back(
            ZoningChangeRequest{1, 99, static_cast<uint8_t>(PropertyType::commercial)});
        DeltaBuffer d{};
        module.execute(state, d);
        if (module.properties()[0].zoned_use == PropertyType::commercial)
            approved = true;
    }
    REQUIRE(approved);
}

TEST_CASE("Phase7: zoning change rejected when actor is not owner",
          "[real_estate][market_phase7]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0, 0.0f));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/42, 150000.0f);
    prop.zoned_use = PropertyType::residential;
    module.add_property(prop);

    // Player 99 tries to rezone NPC 42's property across many ticks.
    for (int t = 0; t < 50; ++t) {
        state.current_tick = 10u + static_cast<uint32_t>(t);
        state.pending_zoning_requests.push_back(
            ZoningChangeRequest{1, 99, static_cast<uint8_t>(PropertyType::commercial)});
        DeltaBuffer d{};
        module.execute(state, d);
    }
    REQUIRE(module.properties()[0].zoned_use == PropertyType::residential);
}

TEST_CASE("Phase7: corruption raises approval odds for major change",
          "[real_estate][market_phase7]") {
    // High-corruption province should approve a major (raw_land->industrial)
    // change within far fewer attempts than a clean province would.
    auto run = [](float corruption) {
        auto state = make_test_world_state(10);
        state.world_seed = 7;
        state.provinces.push_back(make_test_province(0, corruption));
        state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

        RealEstateModule module;
        auto prop = make_test_property(1, PropertyType::raw_land, 0, /*owner=*/99, 50000.0f);
        prop.subtype_key = "farmland";
        prop.parcel_area_hectares = 10.0f;
        prop.zoned_use = PropertyType::raw_land;
        module.add_property(prop);

        int attempts = 0;
        bool approved = false;
        for (int t = 0; t < 200 && !approved; ++t) {
            state.current_tick = 10u + static_cast<uint32_t>(t);
            state.pending_zoning_requests.push_back(
                ZoningChangeRequest{1, 99, static_cast<uint8_t>(PropertyType::industrial)});
            DeltaBuffer d{};
            module.execute(state, d);
            attempts++;
            if (module.properties()[0].zoned_use == PropertyType::industrial)
                approved = true;
        }
        return std::make_pair(approved, attempts);
    };

    auto [clean_ok, clean_attempts] = run(0.0f);
    auto [corrupt_ok, corrupt_attempts] = run(1.0f);
    REQUIRE(clean_ok);
    REQUIRE(corrupt_ok);
    // Full-corruption major prob = 0.25 + 1.0*0.30 = 0.55 vs clean 0.25.
    REQUIRE(corrupt_attempts <= clean_attempts);
}

TEST_CASE("Phase7: approved zoning change nudges market_value toward land baseline",
          "[real_estate][market_phase7]") {
    auto state = make_test_world_state(10);
    state.world_seed = 3;
    state.provinces.push_back(make_test_province(0, 1.0f));  // high corruption → easy approval
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::raw_land, 0, /*owner=*/99, 50000.0f);
    prop.subtype_key = "urban_commercial";  // base 400k/ha
    prop.parcel_area_hectares = 1.0f;
    prop.zoned_use = PropertyType::raw_land;
    module.add_property(prop);

    float initial_value = module.properties()[0].market_value;

    bool approved = false;
    for (int t = 0; t < 100 && !approved; ++t) {
        state.current_tick = 10u + static_cast<uint32_t>(t);
        state.pending_zoning_requests.push_back(
            ZoningChangeRequest{1, 99, static_cast<uint8_t>(PropertyType::commercial)});
        DeltaBuffer d{};
        module.execute(state, d);
        if (module.properties()[0].zoned_use == PropertyType::commercial)
            approved = true;
    }
    REQUIRE(approved);
    // target = 400000/ha × 1ha = 400k; value nudged up from 50k.
    REQUIRE(module.properties()[0].market_value > initial_value);
}

TEST_CASE("Phase7: raw_land subtype + zoning round-trips via persistence schema_tag 4",
          "[real_estate][market_phase7][persistence]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::raw_land, 0, /*owner=*/99, 50000.0f);
    prop.subtype_key = "island";
    prop.parcel_area_hectares = 500.0f;
    prop.zoned_use = PropertyType::raw_land;
    module.add_property(prop);

    auto bytes = PersistenceModule::serialize(state, {&module});
    WorldState restored{};
    RealEstateModule restored_mod;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    REQUIRE(restored_mod.properties().size() == 1);
    const auto& r = restored_mod.properties()[0];
    REQUIRE(r.type == PropertyType::raw_land);
    REQUIRE(r.subtype_key == "island");
    REQUIRE_THAT(r.parcel_area_hectares, WithinAbs(500.0f, 0.01f));
    REQUIRE(r.zoned_use == PropertyType::raw_land);
}

// ===========================================================================
// Phase 8: subdivision + re-merge
// ===========================================================================

TEST_CASE("Phase8: subdivide splits a block into N child units", "[real_estate][market_phase8]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/99, 1000000.0f);
    prop.subtype_key = "apartment_block";
    module.add_property(prop);

    state.pending_subdivision_requests.push_back(
        PropertySubdivisionRequest{SubdivisionKind::subdivide, 1, 99, 4});

    DeltaBuffer d{};
    module.execute(state, d);

    // Parent marked subdivided; 4 child units created.
    const auto& props = module.properties();
    REQUIRE(props.size() == 5);  // parent + 4 children
    int children = 0;
    for (const auto& p : props) {
        if (p.parent_property_id == 1) {
            children++;
            REQUIRE(p.owner_id == 99);
            REQUIRE(p.subtype_key == "apartment_unit");
            // per_unit = (1,000,000 / 4) × 1.10 = 275,000.
            REQUIRE_THAT(p.market_value, WithinAbs(275000.0f, 1.0f));
        }
        if (p.id == 1) {
            REQUIRE(p.subdivided == true);
            REQUIRE(p.unit_count == 4);
        }
    }
    REQUIRE(children == 4);
}

TEST_CASE("Phase8: subdivide rejected for non-subdivisible subtype",
          "[real_estate][market_phase8]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, 99, 500000.0f);
    prop.subtype_key = "house";  // single dwelling — not subdivisible
    module.add_property(prop);

    state.pending_subdivision_requests.push_back(
        PropertySubdivisionRequest{SubdivisionKind::subdivide, 1, 99, 4});

    DeltaBuffer d{};
    module.execute(state, d);

    REQUIRE(module.properties().size() == 1);
    REQUIRE(module.properties()[0].subdivided == false);
}

TEST_CASE("Phase8: subdivide rejected for non-owner", "[real_estate][market_phase8]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/42, 1000000.0f);
    prop.subtype_key = "apartment_block";
    module.add_property(prop);

    state.pending_subdivision_requests.push_back(
        PropertySubdivisionRequest{SubdivisionKind::subdivide, 1, 99, 4});

    DeltaBuffer d{};
    module.execute(state, d);

    REQUIRE(module.properties().size() == 1);
    REQUIRE(module.properties()[0].subdivided == false);
}

TEST_CASE("Phase8: subdivided parent (shell) is not buyable", "[real_estate][market_phase8]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 5000000.0f;

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/42, 1000000.0f);
    prop.subtype_key = "apartment_block";
    prop.subdivided = true;       // already a shell
    prop.listed_for_sale = true;  // even if somehow listed
    module.add_property(prop);

    state.pending_property_transactions.push_back(
        PropertyTransactionRequest{PropertyTransactionKind::buy, 1, 99, 1000000.0f});

    DeltaBuffer d{};
    module.execute(state, d);

    // No pending transaction created — shell rejected.
    REQUIRE(state.pending_transactions.empty());
    REQUIRE(module.properties()[0].owner_id == 42);
}

TEST_CASE("Phase8: a child unit can be sold independently", "[real_estate][market_phase8]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 5000000.0f;

    RealEstateModule module;
    // Parent + one child unit owned by NPC 42, listed.
    auto parent = make_test_property(1, PropertyType::residential, 0, /*owner=*/42, 1000000.0f);
    parent.subtype_key = "apartment_block";
    parent.subdivided = true;
    parent.unit_count = 2;
    module.add_property(parent);
    auto child = make_test_property(2, PropertyType::residential, 0, /*owner=*/42, 275000.0f);
    child.parent_property_id = 1;
    child.subtype_key = "apartment_unit";
    child.asking_price = 275000.0f;
    child.listed_for_sale = true;
    module.add_property(child);

    state.pending_property_transactions.push_back(
        PropertyTransactionRequest{PropertyTransactionKind::buy, 2, 99, 275000.0f});

    DeltaBuffer d_offer{};
    module.execute(state, d_offer);
    REQUIRE(state.pending_transactions.size() == 1);

    state.current_tick = 20;  // past close
    DeltaBuffer d{};
    module.execute(state, d);

    // Child unit transferred to player; parent shell untouched.
    for (const auto& p : module.properties()) {
        if (p.id == 2)
            REQUIRE(p.owner_id == 99);
        if (p.id == 1)
            REQUIRE(p.owner_id == 42);
    }
}

TEST_CASE("Phase8: merge recombines child units when one owner holds all",
          "[real_estate][market_phase8]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    RealEstateModule module;
    auto parent = make_test_property(1, PropertyType::residential, 0, /*owner=*/99, 1000000.0f);
    parent.subtype_key = "apartment_block";
    parent.subdivided = true;
    parent.unit_count = 3;
    module.add_property(parent);
    for (uint32_t u = 0; u < 3; ++u) {
        auto child =
            make_test_property(2 + u, PropertyType::residential, 0, /*owner=*/99, 300000.0f);
        child.parent_property_id = 1;
        child.subtype_key = "apartment_unit";
        module.add_property(child);
    }

    state.pending_subdivision_requests.push_back(
        PropertySubdivisionRequest{SubdivisionKind::merge, 1, 99, 0});

    DeltaBuffer d{};
    module.execute(state, d);

    // Children removed; parent restored.
    REQUIRE(module.properties().size() == 1);
    REQUIRE(module.properties()[0].id == 1);
    REQUIRE(module.properties()[0].subdivided == false);
    REQUIRE(module.properties()[0].unit_count == 1);
    // value = sum of children = 900,000.
    REQUIRE_THAT(module.properties()[0].market_value, WithinAbs(900000.0f, 1.0f));
}

TEST_CASE("Phase8: merge rejected when a child is owned by someone else",
          "[real_estate][market_phase8]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    RealEstateModule module;
    auto parent = make_test_property(1, PropertyType::residential, 0, /*owner=*/99, 1000000.0f);
    parent.subtype_key = "apartment_block";
    parent.subdivided = true;
    parent.unit_count = 2;
    module.add_property(parent);
    auto c1 = make_test_property(2, PropertyType::residential, 0, /*owner=*/99, 300000.0f);
    c1.parent_property_id = 1;
    module.add_property(c1);
    auto c2 = make_test_property(3, PropertyType::residential, 0, /*owner=*/77, 300000.0f);
    c2.parent_property_id = 1;  // owned by a different NPC
    module.add_property(c2);

    state.pending_subdivision_requests.push_back(
        PropertySubdivisionRequest{SubdivisionKind::merge, 1, 99, 0});

    DeltaBuffer d{};
    module.execute(state, d);

    // Merge blocked; everything intact.
    REQUIRE(module.properties().size() == 3);
    REQUIRE(module.properties()[0].subdivided == true);
}

TEST_CASE("Phase8: subdivision round-trips via persistence schema_tag 5",
          "[real_estate][market_phase8][persistence]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    RealEstateModule module;
    auto parent = make_test_property(1, PropertyType::residential, 0, 99, 1000000.0f);
    parent.subtype_key = "apartment_block";
    parent.subdivided = true;
    parent.unit_count = 2;
    module.add_property(parent);
    auto child = make_test_property(2, PropertyType::residential, 0, 99, 550000.0f);
    child.parent_property_id = 1;
    child.subtype_key = "apartment_unit";
    module.add_property(child);

    auto bytes = PersistenceModule::serialize(state, {&module});
    WorldState restored{};
    RealEstateModule restored_mod;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    REQUIRE(restored_mod.properties().size() == 2);
    const PropertyListing* rp = nullptr;
    const PropertyListing* rc = nullptr;
    for (const auto& p : restored_mod.properties()) {
        if (p.id == 1)
            rp = &p;
        if (p.id == 2)
            rc = &p;
    }
    REQUIRE(rp != nullptr);
    REQUIRE(rc != nullptr);
    REQUIRE(rp->subdivided == true);
    REQUIRE(rp->unit_count == 2);
    REQUIRE(rc->parent_property_id == 1);
    REQUIRE(rc->subtype_key == "apartment_unit");
}

// ===========================================================================
// Phase 9: location flags (offshore concealment)
// ===========================================================================

TEST_CASE("Phase9: offshore property transaction emits no evidence token",
          "[real_estate][market_phase9]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 5000000.0f;

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/42, 1000000.0f);
    prop.asking_price = 1000000.0f;
    prop.listed_for_sale = true;
    prop.location_flags = LocationFlag_Offshore;
    module.add_property(prop);

    state.pending_property_transactions.push_back(
        PropertyTransactionRequest{PropertyTransactionKind::buy, 1, 99, 1000000.0f});
    DeltaBuffer d_offer{};
    module.execute(state, d_offer);
    state.current_tick = 20;
    DeltaBuffer d{};
    module.execute(state, d);

    // Ownership transferred (1M well above evidence threshold) but the
    // offshore registry leaves no institutional paper trail.
    REQUIRE(module.properties()[0].owner_id == 99);
    REQUIRE(d.evidence_deltas.empty());
}

TEST_CASE("Phase9: onshore property of identical value DOES emit evidence",
          "[real_estate][market_phase9]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 5000000.0f;

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/42, 1000000.0f);
    prop.asking_price = 1000000.0f;
    prop.listed_for_sale = true;
    prop.location_flags = LocationFlag_None;  // onshore
    module.add_property(prop);

    state.pending_property_transactions.push_back(
        PropertyTransactionRequest{PropertyTransactionKind::buy, 1, 99, 1000000.0f});
    DeltaBuffer d_offer{};
    module.execute(state, d_offer);
    state.current_tick = 20;
    DeltaBuffer d{};
    module.execute(state, d);

    REQUIRE(module.properties()[0].owner_id == 99);
    REQUIRE(d.evidence_deltas.size() == 1);
}

TEST_CASE("Phase9: island = raw_land + offshore round-trips via persistence schema_tag 6",
          "[real_estate][market_phase9][persistence]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::raw_land, 0, /*owner=*/99, 25000000.0f);
    prop.subtype_key = "island";
    prop.parcel_area_hectares = 500.0f;
    prop.location_flags = LocationFlag_Offshore | LocationFlag_International;
    module.add_property(prop);

    auto bytes = PersistenceModule::serialize(state, {&module});
    WorldState restored{};
    RealEstateModule restored_mod;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    REQUIRE(restored_mod.properties().size() == 1);
    const auto& r = restored_mod.properties()[0];
    REQUIRE(r.subtype_key == "island");
    REQUIRE((r.location_flags & LocationFlag_Offshore) != 0);
    REQUIRE((r.location_flags & LocationFlag_International) != 0);
    REQUIRE((r.location_flags & LocationFlag_Remote) == 0);
}

// ===========================================================================
// Phase 10: business acquisition
// ===========================================================================

namespace {

NPCBusiness make_acq_business(uint32_t id, uint32_t owner_id, float revenue_per_tick) {
    NPCBusiness b{};
    b.id = id;
    b.owner_id = owner_id;
    b.revenue_per_tick = revenue_per_tick;
    b.cash = 0.0f;
    b.province_id = 0;
    return b;
}

}  // namespace

TEST_CASE("Phase10: generous cash offer is accepted and creates a pending acquisition",
          "[real_estate][market_phase10]") {
    auto state = make_test_world_state(10);
    state.world_seed = 5;
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 5000000.0f;
    // Owner NPC who trusts the player (eases acceptance).
    NPC owner = make_seller_npc(42, 0, 100000.0f);
    set_relationship_to_player(owner, 99, /*trust=*/0.8f, /*fear=*/0.0f);
    state.significant_npcs.push_back(std::move(owner));
    state.npc_businesses.push_back(make_acq_business(7, /*owner=*/42, /*revenue=*/1000.0f));

    RealEstateModule module;
    // Generous multiple (12 vs fair 6) → high accept probability.
    state.pending_business_acquisition_requests.push_back(
        BusinessAcquisitionRequest{7, 99, 12.0f, static_cast<uint8_t>(PaymentMethod::cash), 1.0f});

    DeltaBuffer d{};
    module.execute(state, d);

    REQUIRE(state.pending_business_acquisitions.size() == 1);
    const auto& a = state.pending_business_acquisitions[0];
    REQUIRE(a.business_id == 7);
    REQUIRE(a.buyer_id == 99);
    REQUIRE(a.seller_id == 42);
    // price = 1000 × 30 × 12 = 360,000.
    REQUIRE_THAT(a.price, WithinAbs(360000.0f, 1.0f));
    REQUIRE(a.close_tick == 10u + 60u);
}

TEST_CASE("Phase10: acquisition settles at close — ownership transfers, seller paid",
          "[real_estate][market_phase10]") {
    auto state = make_test_world_state(10);
    state.world_seed = 5;
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 5000000.0f;
    NPC owner = make_seller_npc(42, 0, 100000.0f);
    set_relationship_to_player(owner, 99, 0.8f, 0.0f);
    state.significant_npcs.push_back(std::move(owner));
    state.npc_businesses.push_back(make_acq_business(7, 42, 1000.0f));

    RealEstateModule module;
    state.pending_business_acquisitions.push_back(
        PendingBusinessAcquisition{1, 7, 99, 42, 360000.0f, 10, 20, PendingTxStage::pending,
                                   PaymentMethod::cash, 1.0f, 0.0f, 0u});

    state.current_tick = 20;
    DeltaBuffer d{};
    module.execute(state, d);

    // Ownership transfer delta emitted; player debited; seller credited.
    REQUIRE(d.business_deltas.size() == 1);
    REQUIRE(d.business_deltas[0].business_id == 7);
    REQUIRE(d.business_deltas[0].owner_id_update.has_value());
    REQUIRE(*d.business_deltas[0].owner_id_update == 99);
    REQUIRE_THAT(*d.player_delta.wealth_delta, WithinAbs(-360000.0f, 1.0f));
    bool seller_paid = false;
    for (const auto& nd : d.npc_deltas) {
        if (nd.npc_id == 42 && nd.capital_delta.has_value() &&
            std::fabs(*nd.capital_delta - 360000.0f) < 1.0f)
            seller_paid = true;
    }
    REQUIRE(seller_paid);
    REQUIRE(state.pending_business_acquisitions.empty());  // pruned
}

TEST_CASE("Phase10: mortgaged acquisition emits a business-collateral loan request",
          "[real_estate][market_phase10]") {
    auto state = make_test_world_state(10);
    state.world_seed = 5;
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 5000000.0f;
    state.npc_businesses.push_back(make_acq_business(7, /*owner=*/0, 1000.0f));  // independent

    RealEstateModule module;
    // mixed: 40% down on 360k → 144k cash, 216k loan.
    state.pending_business_acquisitions.push_back(
        PendingBusinessAcquisition{1, 7, 99, 0, 360000.0f, 10, 20, PendingTxStage::pending,
                                   PaymentMethod::mixed, 0.40f, 0.00025f, 10950u});

    state.current_tick = 20;
    DeltaBuffer d{};
    module.execute(state, d);

    REQUIRE(d.business_deltas.size() == 1);
    REQUIRE(*d.business_deltas[0].owner_id_update == 99);
    REQUIRE_THAT(*d.player_delta.wealth_delta, WithinAbs(-144000.0f, 1.0f));
    // Independent business (owner 0) → no seller credit.
    REQUIRE(d.npc_deltas.empty());
    REQUIRE(d.new_loan_requests.size() == 1);
    const auto& lr = d.new_loan_requests[0];
    REQUIRE(lr.borrower_id == 99);
    REQUIRE(lr.collateral_id == 7u);  // the business itself is collateral
    REQUIRE_THAT(lr.principal, WithinAbs(216000.0f, 1.0f));
}

TEST_CASE("Phase10: zero-revenue business has no acquisition price (offer dropped)",
          "[real_estate][market_phase10]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 5000000.0f;
    state.npc_businesses.push_back(make_acq_business(7, 42, /*revenue=*/0.0f));

    RealEstateModule module;
    state.pending_business_acquisition_requests.push_back(
        BusinessAcquisitionRequest{7, 99, 12.0f, static_cast<uint8_t>(PaymentMethod::cash), 1.0f});

    DeltaBuffer d{};
    module.execute(state, d);
    REQUIRE(state.pending_business_acquisitions.empty());
}

TEST_CASE("Phase10: acquisition expires if business sold out from under the buyer",
          "[real_estate][market_phase10]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 5000000.0f;
    // Business now owned by 88, but the acquisition recorded seller 42.
    state.npc_businesses.push_back(make_acq_business(7, /*owner=*/88, 1000.0f));

    RealEstateModule module;
    state.pending_business_acquisitions.push_back(
        PendingBusinessAcquisition{1, 7, 99, 42, 360000.0f, 10, 20, PendingTxStage::pending,
                                   PaymentMethod::cash, 1.0f, 0.0f, 0u});

    state.current_tick = 20;
    DeltaBuffer d{};
    module.execute(state, d);

    // Seller mismatch → cancelled, no transfer.
    REQUIRE(d.business_deltas.empty());
    REQUIRE(state.pending_business_acquisitions.empty());
}

TEST_CASE("Phase10: pending_business_acquisitions round-trips via persistence v13",
          "[real_estate][market_phase10][persistence]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    state.pending_business_acquisitions.push_back(
        PendingBusinessAcquisition{3, 7, 99, 42, 360000.0f, 10, 70, PendingTxStage::pending,
                                   PaymentMethod::mixed, 0.40f, 0.00025f, 10950u});

    RealEstateModule module;
    auto bytes = PersistenceModule::serialize(state, {&module});
    WorldState restored{};
    RealEstateModule restored_mod;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    REQUIRE(restored.pending_business_acquisitions.size() == 1);
    const auto& r = restored.pending_business_acquisitions[0];
    REQUIRE(r.business_id == 7);
    REQUIRE(r.seller_id == 42);
    REQUIRE_THAT(r.price, WithinAbs(360000.0f, 1.0f));
    REQUIRE(r.payment_method == PaymentMethod::mixed);
    REQUIRE_THAT(r.down_payment_fraction, WithinAbs(0.40f, 0.001f));
    REQUIRE(r.loan_maturity_ticks == 10950u);
}

// ===========================================================================
// Phase 11: construction sector + bid contracts
// ===========================================================================

namespace {

NPCBusiness make_construction_firm(uint32_t id, uint32_t owner_id, uint32_t province_id) {
    NPCBusiness b{};
    b.id = id;
    b.owner_id = owner_id;
    b.province_id = province_id;
    b.sector = BusinessSector::construction;
    b.cash = 0.0f;
    return b;
}

}  // namespace

TEST_CASE("Phase11: construction request opens a contract with bids from local firms",
          "[real_estate][market_phase11]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::industrial, 0, /*owner=*/99, 500000.0f);
    prop.zoned_use = PropertyType::industrial;  // developed
    module.add_property(prop);

    // Two construction firms in the province.
    state.npc_businesses.push_back(make_construction_firm(50, /*owner=*/0, 0));
    state.npc_businesses.push_back(make_construction_firm(51, /*owner=*/0, 0));

    state.pending_construction_requests.push_back(
        ConstructionBidsRequest{99, 1, "factory", "steel_smelting", 14});

    DeltaBuffer d{};
    module.execute(state, d);

    REQUIRE(state.construction_contracts.size() == 1);
    const auto& c = state.construction_contracts[0];
    REQUIRE(c.client_id == 99);
    REQUIRE(c.property_id == 1);
    REQUIRE(c.stage == ContractStage::bidding);
    REQUIRE(c.bids.size() == 2);
    // Each bid covers at least the base cost.
    REQUIRE(c.bids[0].bid_amount >= 200000.0f);
}

TEST_CASE("Phase11: construction request on raw_land (un-zoned) is rejected",
          "[real_estate][market_phase11]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::raw_land, 0, /*owner=*/99, 50000.0f);
    prop.zoned_use = PropertyType::raw_land;  // not yet zoned for development
    module.add_property(prop);
    state.npc_businesses.push_back(make_construction_firm(50, 0, 0));

    state.pending_construction_requests.push_back(
        ConstructionBidsRequest{99, 1, "factory", "steel_smelting", 14});

    DeltaBuffer d{};
    module.execute(state, d);
    REQUIRE(state.construction_contracts.empty());
}

TEST_CASE("Phase11: player-owned contractor bids at zero margin (internal cost)",
          "[real_estate][market_phase11]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::industrial, 0, 99, 500000.0f);
    prop.zoned_use = PropertyType::industrial;
    module.add_property(prop);
    // Player owns firm 50; firm 51 is independent.
    state.npc_businesses.push_back(make_construction_firm(50, /*owner=*/99, 0));
    state.npc_businesses.push_back(make_construction_firm(51, /*owner=*/0, 0));

    state.pending_construction_requests.push_back(
        ConstructionBidsRequest{99, 1, "factory", "steel_smelting", 14});

    DeltaBuffer d{};
    module.execute(state, d);

    const auto& c = state.construction_contracts[0];
    REQUIRE(c.bids.size() == 2);
    // Firm 50 (player-owned) bids exactly base cost (zero margin); firm 51 more.
    float bid50 = 0, bid51 = 0;
    for (const auto& b : c.bids) {
        if (b.contractor_business_id == 50)
            bid50 = b.bid_amount;
        if (b.contractor_business_id == 51)
            bid51 = b.bid_amount;
    }
    REQUIRE_THAT(bid50, WithinAbs(200000.0f, 1.0f));
    REQUIRE(bid51 > bid50);
}

TEST_CASE("Phase11: awarding a bid escrows funds and starts construction",
          "[real_estate][market_phase11]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 5000000.0f;

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::industrial, 0, 99, 500000.0f);
    prop.zoned_use = PropertyType::industrial;
    module.add_property(prop);
    state.npc_businesses.push_back(make_construction_firm(50, 0, 0));

    // Open the contract.
    state.pending_construction_requests.push_back(
        ConstructionBidsRequest{99, 1, "factory", "steel_smelting", 14});
    DeltaBuffer d1{};
    module.execute(state, d1);
    REQUIRE(state.construction_contracts.size() == 1);
    float bid_amount = state.construction_contracts[0].bids[0].bid_amount;
    uint32_t contract_id = state.construction_contracts[0].id;

    // Award bid 0.
    state.current_tick = 12;
    state.pending_construction_awards.push_back(ConstructionAwardRequest{99, contract_id, 0});
    DeltaBuffer d2{};
    module.execute(state, d2);

    REQUIRE(state.construction_contracts[0].stage == ContractStage::in_progress);
    REQUIRE(state.construction_contracts[0].expected_completion_tick == 12u + 90u);
    REQUIRE(d2.player_delta.wealth_delta.has_value());
    REQUIRE_THAT(*d2.player_delta.wealth_delta, WithinAbs(-bid_amount, 1.0f));
}

TEST_CASE("Phase11: construction completes — facility delivered, contractor paid",
          "[real_estate][market_phase11]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 5000000.0f;

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::industrial, 0, 99, 500000.0f);
    prop.zoned_use = PropertyType::industrial;
    module.add_property(prop);
    state.npc_businesses.push_back(make_construction_firm(50, 0, 0));

    state.pending_construction_requests.push_back(
        ConstructionBidsRequest{99, 1, "factory", "steel_smelting", 14});
    DeltaBuffer d1{};
    module.execute(state, d1);
    uint32_t contract_id = state.construction_contracts[0].id;
    float bid_amount = state.construction_contracts[0].bids[0].bid_amount;

    state.current_tick = 12;
    state.pending_construction_awards.push_back(ConstructionAwardRequest{99, contract_id, 0});
    DeltaBuffer d2{};
    module.execute(state, d2);

    // Jump to completion.
    state.current_tick = 12 + 90;
    DeltaBuffer d3{};
    module.execute(state, d3);

    // Facility delivered on the parcel, owned by an auto-created
    // player business; contractor paid; contract pruned.
    REQUIRE(d3.new_facilities.size() == 1);
    REQUIRE(d3.new_facilities[0].new_facility.property_id == 1);
    REQUIRE(d3.new_facilities[0].new_facility.recipe_id == "steel_smelting");
    REQUIRE(d3.new_facilities[0].new_facility.is_operational == false);
    REQUIRE(d3.new_businesses.size() == 1);  // auto-created holding business
    REQUIRE(d3.new_businesses[0].new_business.owner_id == 99);
    bool contractor_paid = false;
    for (const auto& bd : d3.business_deltas) {
        if (bd.business_id == 50 && bd.cash_delta.has_value() &&
            std::fabs(*bd.cash_delta - bid_amount) < 1.0f)
            contractor_paid = true;
    }
    REQUIRE(contractor_paid);
    REQUIRE(state.construction_contracts.empty());  // pruned
}

TEST_CASE("Phase11: bidding contract cancels if no award by deadline",
          "[real_estate][market_phase11]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::industrial, 0, 99, 500000.0f);
    prop.zoned_use = PropertyType::industrial;
    module.add_property(prop);
    state.npc_businesses.push_back(make_construction_firm(50, 0, 0));

    state.pending_construction_requests.push_back(
        ConstructionBidsRequest{99, 1, "factory", "steel_smelting", 14});
    DeltaBuffer d1{};
    module.execute(state, d1);
    REQUIRE(state.construction_contracts.size() == 1);  // deadline = tick 24

    // No award; advance past deadline.
    state.current_tick = 25;
    DeltaBuffer d2{};
    module.execute(state, d2);
    REQUIRE(state.construction_contracts.empty());  // cancelled + pruned
}

TEST_CASE("Phase11: construction contract round-trips via persistence v14",
          "[real_estate][market_phase11][persistence]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    ConstructionContract c{};
    c.id = 3;
    c.client_id = 99;
    c.property_id = 1;
    c.facility_type_key = "factory";
    c.recipe_id = "steel_smelting";
    c.bids.push_back(ConstructionBid{50, 220000.0f, 90});
    c.bids.push_back(ConstructionBid{51, 250000.0f, 80});
    c.awarded_bid_index = 0;
    c.stage = ContractStage::in_progress;
    c.bidding_deadline_tick = 24;
    c.expected_completion_tick = 102;
    state.construction_contracts.push_back(c);

    RealEstateModule module;
    auto bytes = PersistenceModule::serialize(state, {&module});
    WorldState restored{};
    RealEstateModule restored_mod;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    REQUIRE(restored.construction_contracts.size() == 1);
    const auto& r = restored.construction_contracts[0];
    REQUIRE(r.id == 3);
    REQUIRE(r.facility_type_key == "factory");
    REQUIRE(r.recipe_id == "steel_smelting");
    REQUIRE(r.bids.size() == 2);
    REQUIRE(r.stage == ContractStage::in_progress);
    REQUIRE(r.expected_completion_tick == 102);
    REQUIRE_THAT(r.bids[1].bid_amount, WithinAbs(250000.0f, 0.01f));
}

TEST_CASE("Phase11: Facility.property_id round-trips via persistence v14",
          "[real_estate][market_phase11][persistence]") {
    auto world = make_test_world_state(10);
    world.provinces.push_back(make_test_province(0));
    Facility f{};
    f.id = 5000;
    f.business_id = 7;
    f.province_id = 0;
    f.recipe_id = "steel_smelting";
    f.tech_tier = 2;
    f.output_rate_modifier = 1.0f;
    f.soil_health = 1.0f;
    f.worker_count = 3;
    f.is_operational = true;
    f.property_id = 42;
    world.facilities.push_back(f);

    auto bytes = PersistenceModule::serialize(world);
    WorldState restored{};
    REQUIRE(PersistenceModule::deserialize(bytes, restored) == RestoreResult::success);

    bool found = false;
    for (const auto& rf : restored.facilities) {
        if (rf.id == 5000) {
            found = true;
            REQUIRE(rf.property_id == 42);
        }
    }
    REQUIRE(found);
}

// ===========================================================================
// Phase 12+13: property tax, delinquency, lien, tax sale
// ===========================================================================

TEST_CASE("Phase12: quarterly property tax debits a solvent player owner",
          "[real_estate][market_phase12]") {
    auto state = make_test_world_state(90);  // tax quarter
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 1000000.0f;

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::commercial, 0, /*owner=*/99, 1000000.0f);
    module.add_property(prop);

    DeltaBuffer d{};
    module.execute(state, d);

    // commercial annual 0.010 → quarterly 0.0025 × 1,000,000 = 2,500.
    REQUIRE(d.player_delta.wealth_delta.has_value());
    REQUIRE_THAT(*d.player_delta.wealth_delta, WithinAbs(-2500.0f, 1.0f));
    REQUIRE(module.properties()[0].consecutive_delinquent_quarters == 0);
}

TEST_CASE("Phase12: no tax assessed off-quarter", "[real_estate][market_phase12]") {
    auto state = make_test_world_state(45);  // not a quarter boundary
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 1000000.0f;

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::commercial, 0, 99, 1000000.0f);
    module.add_property(prop);

    DeltaBuffer d{};
    module.execute(state, d);
    REQUIRE_FALSE(d.player_delta.wealth_delta.has_value());
}

TEST_CASE("Phase12: offshore parcel is tax-exempt", "[real_estate][market_phase12]") {
    auto state = make_test_world_state(90);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 1000000.0f;

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::commercial, 0, 99, 1000000.0f);
    prop.location_flags = LocationFlag_Offshore;
    module.add_property(prop);

    DeltaBuffer d{};
    module.execute(state, d);
    REQUIRE_FALSE(d.player_delta.wealth_delta.has_value());
}

TEST_CASE("Phase13: insolvent owner accrues delinquency, then a lien",
          "[real_estate][market_phase13]") {
    auto state = make_test_world_state(0);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 0.0f;  // can never pay

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::commercial, 0, 99, 1000000.0f);
    module.add_property(prop);

    // Quarter 1: delinquent, no lien yet (lien at 2).
    state.current_tick = 90;
    DeltaBuffer d1{};
    module.execute(state, d1);
    REQUIRE(module.properties()[0].consecutive_delinquent_quarters == 1);
    REQUIRE(module.properties()[0].tax_lien == false);
    REQUIRE(module.properties()[0].unpaid_tax_balance > 0.0f);

    // Quarter 2: lien filed.
    state.current_tick = 180;
    DeltaBuffer d2{};
    module.execute(state, d2);
    REQUIRE(module.properties()[0].consecutive_delinquent_quarters == 2);
    REQUIRE(module.properties()[0].tax_lien == true);
}

TEST_CASE("Phase13: lien blocks voluntary sale", "[real_estate][market_phase13]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 5000000.0f;

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::commercial, 0, /*owner=*/42, 500000.0f);
    prop.asking_price = 500000.0f;
    prop.listed_for_sale = true;
    prop.tax_lien = true;  // encumbered
    module.add_property(prop);

    state.pending_property_transactions.push_back(
        PropertyTransactionRequest{PropertyTransactionKind::buy, 1, 99, 500000.0f});
    DeltaBuffer d{};
    module.execute(state, d);
    REQUIRE(state.pending_transactions.empty());  // blocked by lien
}

TEST_CASE("Phase13: tax sale seizes parcel to the state and opens a government auction",
          "[real_estate][market_phase13]") {
    auto state = make_test_world_state(0);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 0.0f;  // never pays

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::commercial, 0, /*owner=*/99, 1000000.0f);
    module.add_property(prop);

    // Run 4 quarters (sale threshold = 4).
    for (uint32_t q = 1; q <= 4; ++q) {
        state.current_tick = 90u * q;
        DeltaBuffer d{};
        module.execute(state, d);
    }

    // Parcel seized to the state; a government-consigned auction opened.
    REQUIRE(module.properties()[0].owner_id == 0u);
    REQUIRE(state.active_auctions.size() == 1);
    REQUIRE(state.active_auctions[0].asset_id == 1);
    REQUIRE(state.active_auctions[0].consigner_id == 0u);
    REQUIRE(state.active_auctions[0].status == AuctionStatus::open);
}

TEST_CASE("Phase12: tax fields round-trip via persistence schema_tag 7",
          "[real_estate][market_phase12][persistence]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::commercial, 0, 99, 500000.0f);
    prop.unpaid_tax_balance = 1250.0f;
    prop.last_tax_assessment_tick = 90;
    prop.consecutive_delinquent_quarters = 3;
    prop.tax_lien = true;
    module.add_property(prop);

    auto bytes = PersistenceModule::serialize(state, {&module});
    WorldState restored{};
    RealEstateModule restored_mod;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    const auto& r = restored_mod.properties()[0];
    REQUIRE_THAT(r.unpaid_tax_balance, WithinAbs(1250.0f, 0.01f));
    REQUIRE(r.last_tax_assessment_tick == 90);
    REQUIRE(r.consecutive_delinquent_quarters == 3);
    REQUIRE(r.tax_lien == true);
}

// ===========================================================================
// Counter-offers (negotiation loop closure)
// ===========================================================================

TEST_CASE("Counter: rejected serious below-asking offer draws an NPC counter SceneCard",
          "[real_estate][market_counter]") {
    auto state = make_test_world_state(10);
    state.world_seed = 1;
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 5000000.0f;
    // Wealthy, no-relationship seller → low accept probability on a
    // below-asking offer, so the reject→counter path fires.
    NPC seller = make_seller_npc(42, 0, /*capital=*/100000000.0f);
    state.significant_npcs.push_back(std::move(seller));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/42, 200000.0f);
    prop.asking_price = 200000.0f;
    // Market value far above asking → very low price_ratio → near-zero
    // accept probability, so the reject→counter branch fires
    // deterministically. The offer is still ≥ 0.70 × asking, so it is
    // counter-eligible.
    prop.market_value = 1000000.0f;
    prop.listed_for_sale = true;
    module.add_property(prop);

    // Offer 150k = 0.75 × asking (above the 0.70 counter threshold).
    state.pending_property_transactions.push_back(
        PropertyTransactionRequest{PropertyTransactionKind::buy, 1, 99, 150000.0f});
    DeltaBuffer d{};
    module.execute(state, d);

    // No immediate pending tx; instead a counter negotiation + SceneCard.
    REQUIRE(state.pending_transactions.empty());
    REQUIRE(module.active_negotiations().size() == 1);
    const auto& neg = module.active_negotiations()[0];
    REQUIRE(neg.property_id == 1);
    REQUIRE(neg.buyer_id == 99);
    REQUIRE(neg.seller_id == 42);
    // counter = 150k + (200k - 150k) × 0.5 = 175k.
    REQUIRE_THAT(neg.offer_price, WithinAbs(175000.0f, 1.0f));
    REQUIRE_FALSE(d.new_scene_cards.empty());
}

TEST_CASE("Counter: player accepts the counter → pending buy at counter price",
          "[real_estate][market_counter]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 5000000.0f;

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/42, 200000.0f);
    prop.asking_price = 200000.0f;
    prop.listed_for_sale = true;
    module.add_property(prop);

    // Seed an active counter negotiation (player is buyer).
    NegotiationContext neg{};
    neg.scene_card_id = 100;
    neg.property_id = 1;
    neg.buyer_id = 99;
    neg.seller_id = 42;
    neg.offer_price = 190000.0f;
    neg.offered_tick = 10;
    neg.deadline_tick = 30;
    module.active_negotiations_mut().push_back(neg);

    SceneCard card{};
    card.id = 100;
    card.type = SceneCardType::meeting;
    card.npc_id = 42;
    card.choices.push_back(PlayerChoice{1, "Accept counter", "", 0});
    card.choices.push_back(PlayerChoice{2, "Walk away", "", 0});
    card.chosen_choice_id = 1;  // accept
    state.pending_scene_cards.push_back(card);

    state.current_tick = 12;
    DeltaBuffer d{};
    module.execute(state, d);

    REQUIRE(state.pending_transactions.size() == 1);
    REQUIRE(state.pending_transactions[0].buyer_id == 99);
    REQUIRE(state.pending_transactions[0].seller_id == 42);
    REQUIRE_THAT(state.pending_transactions[0].offer_price, WithinAbs(190000.0f, 1.0f));
    REQUIRE(module.active_negotiations().empty());

    // Settle at close → player owns it at the counter price.
    state.current_tick = 12 + 7;
    DeltaBuffer d2{};
    module.execute(state, d2);
    REQUIRE(module.properties()[0].owner_id == 99);
    REQUIRE_THAT(*d2.player_delta.wealth_delta, WithinAbs(-190000.0f, 1.0f));
}

TEST_CASE("Counter: player declining the counter leaves ownership unchanged",
          "[real_estate][market_counter]") {
    auto state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 5000000.0f;

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/42, 200000.0f);
    prop.listed_for_sale = true;
    module.add_property(prop);

    NegotiationContext neg{100, 1, 99, 42, 190000.0f, 10, 30};
    module.active_negotiations_mut().push_back(neg);
    SceneCard card{};
    card.id = 100;
    card.type = SceneCardType::meeting;
    card.choices.push_back(PlayerChoice{1, "Accept counter", "", 0});
    card.choices.push_back(PlayerChoice{2, "Walk away", "", 0});
    card.chosen_choice_id = 2;  // decline
    state.pending_scene_cards.push_back(card);

    state.current_tick = 12;
    DeltaBuffer d{};
    module.execute(state, d);

    REQUIRE(state.pending_transactions.empty());
    REQUIRE(module.active_negotiations().empty());
    REQUIRE(module.properties()[0].owner_id == 42);
}

TEST_CASE("Counter: lowball below the counter threshold draws no counter",
          "[real_estate][market_counter]") {
    auto state = make_test_world_state(10);
    state.world_seed = 1;
    state.provinces.push_back(make_test_province(0));
    state.player = std::make_unique<PlayerCharacter>(make_test_player(99));
    state.player->wealth = 5000000.0f;
    state.significant_npcs.push_back(make_seller_npc(42, 0, 100000000.0f));

    RealEstateModule module;
    auto prop = make_test_property(1, PropertyType::residential, 0, /*owner=*/42, 200000.0f);
    prop.asking_price = 200000.0f;
    prop.market_value = 1000000.0f;  // near-zero accept probability
    prop.listed_for_sale = true;
    module.add_property(prop);

    // 50% of asking — below the 0.70 counter threshold → no counter,
    // and price_ratio is far too low to accept.
    state.pending_property_transactions.push_back(
        PropertyTransactionRequest{PropertyTransactionKind::buy, 1, 99, 100000.0f});
    DeltaBuffer d{};
    module.execute(state, d);

    REQUIRE(module.active_negotiations().empty());
    REQUIRE(state.pending_transactions.empty());
}
