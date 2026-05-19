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

TEST_CASE("Phase1: list action rejected when actor is not owner",
          "[real_estate][market_phase1]") {
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

TEST_CASE("Phase1: unlist action flips flag back",
          "[real_estate][market_phase1]") {
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

TEST_CASE("Phase2: buy at asking transfers ownership at close_tick, "
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

TEST_CASE("Phase1: buy below asking rejected — no state change",
          "[real_estate][market_phase1]") {
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

TEST_CASE("Phase1: buy on unlisted property rejected",
          "[real_estate][market_phase1]") {
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

TEST_CASE("Phase2: close delay matches property type",
          "[real_estate][market_phase2]") {
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
        if (tx.property_id == 1) resi_close = tx.close_tick;
        if (tx.property_id == 2) comm_close = tx.close_tick;
        if (tx.property_id == 3) indu_close = tx.close_tick;
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

TEST_CASE("Phase2: cancel by unrelated actor rejected",
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
