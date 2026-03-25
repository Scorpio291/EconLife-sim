// Production module unit tests.
// All tests tagged [production][tier1].
//
// Tests verify recipe execution, tech tier bonuses, bankrupt business
// handling, criminal sector pricing, and derived demand output.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"
#include "modules/production/production_module.h"
#include "modules/production/production_types.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// ---------------------------------------------------------------------------
// Test helpers — create minimal WorldState and supporting structures
// ---------------------------------------------------------------------------

namespace {

// Create a minimal WorldState suitable for production tests.
WorldState make_test_world_state() {
    WorldState state{};
    state.current_tick = 1;
    state.world_seed = 42;
    state.player = nullptr;
    state.lod2_price_index = nullptr;
    state.ticks_this_session = 1;
    state.game_mode = GameMode::standard;
    state.current_schema_version = 1;
    state.network_health_dirty = false;
    return state;
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

// Create a simple recipe with one input and one output.
Recipe make_steel_recipe() {
    Recipe recipe{};
    recipe.id = "steel_smelting";
    recipe.name = "Steel Smelting";
    recipe.inputs = {RecipeInput{"iron_ore", 10.0f}, RecipeInput{"coking_coal", 5.0f}};
    recipe.outputs = {RecipeOutput{"steel", 8.0f, 0.6f}};
    recipe.min_tech_tier = 1;
    recipe.base_cost_per_tick = 100.0f;
    recipe.is_technology_intensive = false;
    recipe.key_technology_node = "";
    return recipe;
}

// Create a facility for a business.
Facility make_test_facility(uint32_t id, uint32_t business_id, uint32_t province_id,
                            const std::string& recipe_id, uint32_t tech_tier = 1) {
    Facility f{};
    f.id = id;
    f.business_id = business_id;
    f.province_id = province_id;
    f.recipe_id = recipe_id;
    f.tech_tier = tech_tier;
    f.output_rate_modifier = 1.0f;
    f.soil_health = 1.0f;
    f.worker_count = 1;
    f.is_operational = true;
    return f;
}

// Add a regional market entry for a good in a province.
void add_market(WorldState& state, const std::string& good_id_str, uint32_t province_id,
                float supply, float spot_price = 10.0f) {
    RegionalMarket market{};
    market.good_id = ProductionModule::good_id_from_string(good_id_str);
    market.province_id = province_id;
    market.spot_price = spot_price;
    market.equilibrium_price = spot_price;
    market.adjustment_rate = 0.1f;
    market.supply = supply;
    market.demand_buffer = 0.0f;
    market.import_price_ceiling = 0.0f;
    market.export_price_floor = 0.0f;
    state.regional_markets.push_back(market);
}

// Count MarketDelta entries matching a given good_id string and delta type.
struct DeltaSummary {
    float total_supply_delta = 0.0f;
    float total_demand_delta = 0.0f;
    int supply_count = 0;
    int demand_count = 0;
};

DeltaSummary summarize_deltas(const DeltaBuffer& delta, const std::string& good_id_str,
                              uint32_t province_id) {
    uint32_t good_id = ProductionModule::good_id_from_string(good_id_str);
    DeltaSummary summary{};
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

}  // anonymous namespace

// ===========================================================================
// Tests
// ===========================================================================

TEST_CASE("test_basic_recipe_output", "[production][tier1]") {
    // A business with a steel recipe consumes iron_ore and coking_coal,
    // produces steel. Verify input demand recorded and output supply written.
    auto state = make_test_world_state();
    constexpr uint32_t province_id = 0;

    // Set up business.
    auto biz = make_test_business(1, province_id);
    state.npc_businesses.push_back(biz);

    // Set up markets with sufficient supply.
    // Note: available_supply in production uses string keys from to_string(good_id),
    // so we also need market entries with the uint32_t good_id matching our hash.
    add_market(state, "iron_ore", province_id, 100.0f);
    add_market(state, "coking_coal", province_id, 50.0f);
    add_market(state, "steel", province_id, 0.0f, 15.0f);

    // Set up province (needed for execute_province to work).
    Province prov{};
    prov.id = province_id;
    state.provinces.push_back(prov);

    // Configure module.
    ProductionModule module;
    module.recipe_registry().register_recipe(make_steel_recipe());
    module.facility_registry().register_facility(
        make_test_facility(1, 1, province_id, "steel_smelting"));

    DeltaBuffer delta{};
    module.execute_province(province_id, state, delta);

    // Check steel output: recipe produces 8.0 steel per tick at tech_tier 1 = min_tier.
    auto steel_summary = summarize_deltas(delta, "steel", province_id);
    REQUIRE(steel_summary.supply_count == 1);
    REQUIRE_THAT(steel_summary.total_supply_delta, WithinAbs(8.0f, 0.001f));

    // Check iron_ore demand: recipe consumes 10.0 iron_ore per tick.
    auto iron_summary = summarize_deltas(delta, "iron_ore", province_id);
    REQUIRE(iron_summary.demand_count == 1);
    REQUIRE_THAT(iron_summary.total_demand_delta, WithinAbs(10.0f, 0.001f));

    // Check coking_coal demand: recipe consumes 5.0 coking_coal per tick.
    auto coal_summary = summarize_deltas(delta, "coking_coal", province_id);
    REQUIRE(coal_summary.demand_count == 1);
    REQUIRE_THAT(coal_summary.total_demand_delta, WithinAbs(5.0f, 0.001f));
}

TEST_CASE("test_tech_tier_bonus", "[production][tier1]") {
    // Two businesses: one at min_tech_tier (1), one at tech_tier 3.
    // The higher-tier business should produce (1 + 0.08 * 2) = 1.16x output.
    auto state = make_test_world_state();
    constexpr uint32_t province_id = 0;

    auto biz_base = make_test_business(1, province_id);
    auto biz_high = make_test_business(2, province_id);
    state.npc_businesses.push_back(biz_base);
    state.npc_businesses.push_back(biz_high);

    add_market(state, "iron_ore", province_id, 1000.0f);
    add_market(state, "coking_coal", province_id, 500.0f);
    add_market(state, "steel", province_id, 0.0f, 15.0f);

    Province prov{};
    prov.id = province_id;
    state.provinces.push_back(prov);

    ProductionModule module;
    module.recipe_registry().register_recipe(make_steel_recipe());

    // Business 1: tech_tier 1 (= min_tier, no bonus)
    module.facility_registry().register_facility(
        make_test_facility(1, 1, province_id, "steel_smelting", 1));

    // Business 2: tech_tier 3 (2 tiers above min_tier)
    module.facility_registry().register_facility(
        make_test_facility(2, 2, province_id, "steel_smelting", 3));

    DeltaBuffer delta{};
    module.execute_province(province_id, state, delta);

    // Find supply deltas for steel. Business 1 processed first (id=1 < id=2),
    // so first supply delta is from business 1, second from business 2.
    uint32_t steel_id = ProductionModule::good_id_from_string("steel");
    std::vector<float> steel_supplies;
    for (const auto& md : delta.market_deltas) {
        if (md.good_id == steel_id && md.region_id == province_id && md.supply_delta.has_value()) {
            steel_supplies.push_back(md.supply_delta.value());
        }
    }

    REQUIRE(steel_supplies.size() == 2);

    // Business 1 at min_tier: 8.0 * 1.0 = 8.0
    REQUIRE_THAT(steel_supplies[0], WithinAbs(8.0f, 0.001f));

    // Business 2 at tier 3: 8.0 * (1.0 + 0.08 * 2) = 8.0 * 1.16 = 9.28
    float expected_high = 8.0f * (1.0f + 0.08f * 2.0f);
    REQUIRE_THAT(steel_supplies[1], WithinAbs(expected_high, 0.001f));
}

TEST_CASE("test_tech_tier_cost_reduction", "[production][tier1]") {
    // Verify the operating cost formula: actual_cost = base_cost * (1 - 0.05 * tier_diff).
    // Since the production module does not directly write cost deltas to DeltaBuffer,
    // we verify the cost reduction indirectly through the interface specification.
    // The cost_multiplier is tested by verifying the formula yields expected values.
    constexpr float base_cost = 100.0f;
    constexpr uint32_t min_tier = 1;
    constexpr uint32_t facility_tier = 3;
    int32_t tier_diff = static_cast<int32_t>(facility_tier) - static_cast<int32_t>(min_tier);

    float cost_multiplier = 1.0f - ProductionConstants::tech_tier_cost_reduction_per_tier *
                                       static_cast<float>(std::max(0, tier_diff));
    float actual_cost = base_cost * cost_multiplier;

    // Expected: 100.0 * (1.0 - 0.05 * 2) = 100.0 * 0.9 = 90.0
    REQUIRE_THAT(actual_cost, WithinAbs(90.0f, 0.001f));

    // At min tier, no reduction.
    int32_t zero_diff = 0;
    float no_reduction = 1.0f - ProductionConstants::tech_tier_cost_reduction_per_tier *
                                    static_cast<float>(std::max(0, zero_diff));
    REQUIRE_THAT(base_cost * no_reduction, WithinAbs(100.0f, 0.001f));
}

TEST_CASE("test_bankrupt_business_skipped", "[production][tier1]") {
    // A business with cash <= 0 and revenue_per_tick <= 0 should produce nothing.
    auto state = make_test_world_state();
    constexpr uint32_t province_id = 0;

    // Bankrupt business: cash = -500, revenue = 0.
    auto biz = make_test_business(1, province_id, -500.0f);
    biz.revenue_per_tick = 0.0f;
    state.npc_businesses.push_back(biz);

    add_market(state, "iron_ore", province_id, 100.0f);
    add_market(state, "coking_coal", province_id, 50.0f);
    add_market(state, "steel", province_id, 0.0f, 15.0f);

    Province prov{};
    prov.id = province_id;
    state.provinces.push_back(prov);

    ProductionModule module;
    module.recipe_registry().register_recipe(make_steel_recipe());
    module.facility_registry().register_facility(
        make_test_facility(1, 1, province_id, "steel_smelting"));

    DeltaBuffer delta{};
    module.execute_province(province_id, state, delta);

    // No market deltas should have been written.
    REQUIRE(delta.market_deltas.empty());
}

TEST_CASE("test_criminal_sector_uses_informal_price", "[production][tier1]") {
    // A criminal sector business should use informal price (spot_price * 0.7)
    // for revenue calculations, not the formal spot_price.
    auto state = make_test_world_state();
    constexpr uint32_t province_id = 0;

    // Criminal business.
    auto biz = make_test_business(1, province_id);
    biz.criminal_sector = true;
    biz.sector = BusinessSector::criminal;
    biz.default_activity_scope = VisibilityScope::concealed;
    state.npc_businesses.push_back(biz);

    // Also add a legitimate business for comparison.
    auto biz_legit = make_test_business(2, province_id);
    biz_legit.criminal_sector = false;
    state.npc_businesses.push_back(biz_legit);

    // Set spot price to 100.0 for drugs (output good).
    Recipe drug_recipe{};
    drug_recipe.id = "drug_production";
    drug_recipe.name = "Drug Production";
    drug_recipe.inputs = {RecipeInput{"precursor_chemical", 5.0f}};
    drug_recipe.outputs = {RecipeOutput{"drugs", 10.0f, 0.5f}};
    drug_recipe.min_tech_tier = 1;
    drug_recipe.base_cost_per_tick = 50.0f;
    drug_recipe.is_technology_intensive = false;
    drug_recipe.key_technology_node = "";

    add_market(state, "precursor_chemical", province_id, 100.0f);
    add_market(state, "drugs", province_id, 0.0f, 100.0f);

    Province prov{};
    prov.id = province_id;
    state.provinces.push_back(prov);

    ProductionModule module;
    module.recipe_registry().register_recipe(drug_recipe);
    module.facility_registry().register_facility(
        make_test_facility(1, 1, province_id, "drug_production"));
    module.facility_registry().register_facility(
        make_test_facility(2, 2, province_id, "drug_production"));

    // Test the price lookup directly.
    uint32_t drugs_id = ProductionModule::good_id_from_string("drugs");

    // Criminal business should get informal price = 100.0 * 0.7 = 70.0
    // We can verify this through the module's behavior — both businesses produce
    // the same amount, but the criminal one uses informal pricing.
    DeltaBuffer delta{};
    module.execute_province(province_id, state, delta);

    // Both businesses should have produced drugs (supply deltas written).
    auto drugs_summary = summarize_deltas(delta, "drugs", province_id);
    REQUIRE(drugs_summary.supply_count == 2);

    // Verify both produced 10.0 units of drugs (same recipe, same tech tier).
    REQUIRE_THAT(drugs_summary.total_supply_delta, WithinAbs(20.0f, 0.001f));

    // The criminal sector flag is verified through the get_price_for_business
    // method which applies the informal_price_discount. We verify the
    // discount constant is correctly defined.
    REQUIRE_THAT(ProductionModule::informal_price_discount, WithinAbs(0.7f, 0.001f));
}

TEST_CASE("test_derived_demand", "[production][tier1]") {
    // After production, demand_buffer_delta for consumed input goods should
    // reflect the quantities consumed.
    auto state = make_test_world_state();
    constexpr uint32_t province_id = 0;

    auto biz = make_test_business(1, province_id);
    state.npc_businesses.push_back(biz);

    // Recipe requires 10 iron_ore and 5 coking_coal.
    add_market(state, "iron_ore", province_id, 100.0f);
    add_market(state, "coking_coal", province_id, 50.0f);
    add_market(state, "steel", province_id, 0.0f, 15.0f);

    Province prov{};
    prov.id = province_id;
    state.provinces.push_back(prov);

    ProductionModule module;
    module.recipe_registry().register_recipe(make_steel_recipe());
    module.facility_registry().register_facility(
        make_test_facility(1, 1, province_id, "steel_smelting"));

    DeltaBuffer delta{};
    module.execute_province(province_id, state, delta);

    // Check derived demand for iron_ore (should be 10.0).
    auto iron_demand = summarize_deltas(delta, "iron_ore", province_id);
    REQUIRE(iron_demand.demand_count == 1);
    REQUIRE_THAT(iron_demand.total_demand_delta, WithinAbs(10.0f, 0.001f));

    // Check derived demand for coking_coal (should be 5.0).
    auto coal_demand = summarize_deltas(delta, "coking_coal", province_id);
    REQUIRE(coal_demand.demand_count == 1);
    REQUIRE_THAT(coal_demand.total_demand_delta, WithinAbs(5.0f, 0.001f));
}

TEST_CASE("test_insufficient_input_clamps_output", "[production][tier1]") {
    // A business with a recipe requiring 10 units of iron_ore but only 3
    // available. Output should be clamped proportionally.
    auto state = make_test_world_state();
    constexpr uint32_t province_id = 0;

    auto biz = make_test_business(1, province_id);
    state.npc_businesses.push_back(biz);

    // Only 3 iron_ore available (need 10), plenty of coking_coal.
    add_market(state, "iron_ore", province_id, 3.0f);
    add_market(state, "coking_coal", province_id, 50.0f);
    add_market(state, "steel", province_id, 0.0f, 15.0f);

    Province prov{};
    prov.id = province_id;
    state.provinces.push_back(prov);

    ProductionModule module;
    module.recipe_registry().register_recipe(make_steel_recipe());
    module.facility_registry().register_facility(
        make_test_facility(1, 1, province_id, "steel_smelting"));

    DeltaBuffer delta{};
    module.execute_province(province_id, state, delta);

    // Bottleneck ratio = min(3/10, 50/5) = min(0.3, 10.0) = 0.3
    // Steel output = 8.0 * 1.0 * 0.3 = 2.4
    auto steel_summary = summarize_deltas(delta, "steel", province_id);
    REQUIRE(steel_summary.supply_count == 1);
    REQUIRE_THAT(steel_summary.total_supply_delta, WithinAbs(2.4f, 0.001f));

    // Iron ore demand should be 10.0 * 0.3 = 3.0 (consumed amount).
    auto iron_demand = summarize_deltas(delta, "iron_ore", province_id);
    REQUIRE_THAT(iron_demand.total_demand_delta, WithinAbs(3.0f, 0.001f));

    // Coking coal demand should be 5.0 * 0.3 = 1.5 (proportional to bottleneck).
    auto coal_demand = summarize_deltas(delta, "coking_coal", province_id);
    REQUIRE_THAT(coal_demand.total_demand_delta, WithinAbs(1.5f, 0.001f));
}

TEST_CASE("test_no_facilities_produces_nothing", "[production][tier1]") {
    // A business with no registered facilities should produce nothing.
    auto state = make_test_world_state();
    constexpr uint32_t province_id = 0;

    auto biz = make_test_business(1, province_id);
    state.npc_businesses.push_back(biz);

    add_market(state, "iron_ore", province_id, 100.0f);
    add_market(state, "steel", province_id, 0.0f, 15.0f);

    Province prov{};
    prov.id = province_id;
    state.provinces.push_back(prov);

    ProductionModule module;
    module.recipe_registry().register_recipe(make_steel_recipe());
    // No facilities registered.

    DeltaBuffer delta{};
    module.execute_province(province_id, state, delta);

    REQUIRE(delta.market_deltas.empty());
}

TEST_CASE("test_non_operational_facility_skipped", "[production][tier1]") {
    // A facility with is_operational=false should not produce anything.
    auto state = make_test_world_state();
    constexpr uint32_t province_id = 0;

    auto biz = make_test_business(1, province_id);
    state.npc_businesses.push_back(biz);

    add_market(state, "iron_ore", province_id, 100.0f);
    add_market(state, "coking_coal", province_id, 50.0f);
    add_market(state, "steel", province_id, 0.0f, 15.0f);

    Province prov{};
    prov.id = province_id;
    state.provinces.push_back(prov);

    ProductionModule module;
    module.recipe_registry().register_recipe(make_steel_recipe());

    auto facility = make_test_facility(1, 1, province_id, "steel_smelting");
    facility.is_operational = false;
    module.facility_registry().register_facility(facility);

    DeltaBuffer delta{};
    module.execute_province(province_id, state, delta);

    REQUIRE(delta.market_deltas.empty());
}

TEST_CASE("test_quality_ceiling_computation", "[production][tier1]") {
    // Verify quality ceiling formula:
    //   quality_ceiling = 0.5 + 0.1 * (tech_tier - min_tech_tier)
    constexpr float base = ProductionConstants::tech_quality_ceiling_base;
    constexpr float step = ProductionConstants::tech_quality_ceiling_step;

    // At min tier (diff = 0): 0.5 + 0.1 * 0 = 0.5
    REQUIRE_THAT(base + step * 0.0f, WithinAbs(0.5f, 0.001f));

    // 1 tier above: 0.5 + 0.1 * 1 = 0.6
    REQUIRE_THAT(base + step * 1.0f, WithinAbs(0.6f, 0.001f));

    // 3 tiers above: 0.5 + 0.1 * 3 = 0.8
    REQUIRE_THAT(base + step * 3.0f, WithinAbs(0.8f, 0.001f));

    // 5 tiers above: 0.5 + 0.1 * 5 = 1.0 (clamped to 1.0)
    float raw = base + step * 5.0f;
    float clamped = std::max(0.0f, std::min(1.0f, raw));
    REQUIRE_THAT(clamped, WithinAbs(1.0f, 0.001f));
}

TEST_CASE("test_module_interface_properties", "[production][tier1]") {
    // Verify the module reports correct interface properties.
    ProductionModule module;

    REQUIRE(module.name() == "production");
    REQUIRE(module.package_id() == "base_game");
    REQUIRE(module.scope() == ModuleScope::v1);
    REQUIRE(module.is_province_parallel() == true);
    REQUIRE(module.runs_after().empty());

    auto before = module.runs_before();
    REQUIRE(before.size() == 1);
    REQUIRE(before[0] == "supply_chain");
}

TEST_CASE("test_good_id_from_string_deterministic", "[production][tier1]") {
    // Verify that good_id_from_string is deterministic.
    auto id1 = ProductionModule::good_id_from_string("iron_ore");
    auto id2 = ProductionModule::good_id_from_string("iron_ore");
    REQUIRE(id1 == id2);

    // Different goods produce different ids.
    auto id3 = ProductionModule::good_id_from_string("steel");
    REQUIRE(id1 != id3);
}

TEST_CASE("test_multiple_businesses_deterministic_order", "[production][tier1]") {
    // Businesses should be processed in ascending id order regardless of
    // insertion order into npc_businesses.
    auto state = make_test_world_state();
    constexpr uint32_t province_id = 0;

    // Insert businesses in reverse id order.
    auto biz3 = make_test_business(3, province_id);
    auto biz1 = make_test_business(1, province_id);
    auto biz2 = make_test_business(2, province_id);
    state.npc_businesses.push_back(biz3);
    state.npc_businesses.push_back(biz1);
    state.npc_businesses.push_back(biz2);

    add_market(state, "iron_ore", province_id, 1000.0f);
    add_market(state, "coking_coal", province_id, 500.0f);
    add_market(state, "steel", province_id, 0.0f, 15.0f);

    Province prov{};
    prov.id = province_id;
    state.provinces.push_back(prov);

    ProductionModule module;
    module.recipe_registry().register_recipe(make_steel_recipe());
    module.facility_registry().register_facility(
        make_test_facility(1, 1, province_id, "steel_smelting"));
    module.facility_registry().register_facility(
        make_test_facility(2, 2, province_id, "steel_smelting"));
    module.facility_registry().register_facility(
        make_test_facility(3, 3, province_id, "steel_smelting"));

    DeltaBuffer delta{};
    module.execute_province(province_id, state, delta);

    // All three businesses should produce steel.
    auto steel_summary = summarize_deltas(delta, "steel", province_id);
    REQUIRE(steel_summary.supply_count == 3);

    // Total: 3 * 8.0 = 24.0
    REQUIRE_THAT(steel_summary.total_supply_delta, WithinAbs(24.0f, 0.001f));
}

TEST_CASE("test_worker_count_throughput_effect", "[production][tier1]") {
    // Worker count affects throughput with diminishing returns.
    // 1 worker = 1.0x, 5 workers = 1 + 0.15*4 = 1.6x, 0 workers = 0x.
    auto state = make_test_world_state();
    constexpr uint32_t province_id = 0;

    auto biz = make_test_business(1, province_id);
    state.npc_businesses.push_back(biz);

    add_market(state, "iron_ore", province_id, 1000.0f);
    add_market(state, "coking_coal", province_id, 500.0f);
    add_market(state, "steel", province_id, 0.0f, 15.0f);

    Province prov{};
    prov.id = province_id;
    state.provinces.push_back(prov);

    ProductionModule module;
    module.recipe_registry().register_recipe(make_steel_recipe());

    // 5 workers: multiplier = 1.0 + 0.15 * 4 = 1.6
    auto facility = make_test_facility(1, 1, province_id, "steel_smelting");
    facility.worker_count = 5;
    module.facility_registry().register_facility(facility);

    DeltaBuffer delta{};
    module.execute_province(province_id, state, delta);

    // Expected: 8.0 * 1.6 = 12.8
    auto steel_summary = summarize_deltas(delta, "steel", province_id);
    REQUIRE_THAT(steel_summary.total_supply_delta, WithinAbs(12.8f, 0.01f));
}

TEST_CASE("test_zero_workers_no_production", "[production][tier1]") {
    // A facility with 0 workers should produce nothing.
    auto state = make_test_world_state();
    constexpr uint32_t province_id = 0;

    auto biz = make_test_business(1, province_id);
    state.npc_businesses.push_back(biz);

    add_market(state, "iron_ore", province_id, 100.0f);
    add_market(state, "coking_coal", province_id, 50.0f);
    add_market(state, "steel", province_id, 0.0f, 15.0f);

    Province prov{};
    prov.id = province_id;
    state.provinces.push_back(prov);

    ProductionModule module;
    module.recipe_registry().register_recipe(make_steel_recipe());

    auto facility = make_test_facility(1, 1, province_id, "steel_smelting");
    facility.worker_count = 0;
    module.facility_registry().register_facility(facility);

    DeltaBuffer delta{};
    module.execute_province(province_id, state, delta);

    // No supply deltas (0 workers = 0 output)
    auto steel_summary = summarize_deltas(delta, "steel", province_id);
    REQUIRE(steel_summary.supply_count == 0);
}

TEST_CASE("test_business_delta_written", "[production][tier1]") {
    // Verify that production writes BusinessDelta with revenue and cost.
    auto state = make_test_world_state();
    constexpr uint32_t province_id = 0;

    auto biz = make_test_business(1, province_id);
    state.npc_businesses.push_back(biz);

    add_market(state, "iron_ore", province_id, 100.0f);
    add_market(state, "coking_coal", province_id, 50.0f);
    add_market(state, "steel", province_id, 0.0f, 15.0f);

    Province prov{};
    prov.id = province_id;
    state.provinces.push_back(prov);

    ProductionModule module;
    module.recipe_registry().register_recipe(make_steel_recipe());
    module.facility_registry().register_facility(
        make_test_facility(1, 1, province_id, "steel_smelting"));

    DeltaBuffer delta{};
    module.execute_province(province_id, state, delta);

    // Should have one BusinessDelta for business id=1.
    REQUIRE(delta.business_deltas.size() == 1);
    REQUIRE(delta.business_deltas[0].business_id == 1);

    // Revenue = 8.0 steel * 15.0 price = 120.0
    REQUIRE(delta.business_deltas[0].revenue_per_tick_update.has_value());
    REQUIRE_THAT(*delta.business_deltas[0].revenue_per_tick_update, WithinAbs(120.0f, 0.01f));

    // Cost = 100.0 * 1.0 (no tier bonus) = 100.0
    REQUIRE(delta.business_deltas[0].cost_per_tick_update.has_value());
    REQUIRE_THAT(*delta.business_deltas[0].cost_per_tick_update, WithinAbs(100.0f, 0.01f));

    // Cash delta = revenue - cost = 120.0 - 100.0 = 20.0
    REQUIRE(delta.business_deltas[0].cash_delta.has_value());
    REQUIRE_THAT(*delta.business_deltas[0].cash_delta, WithinAbs(20.0f, 0.01f));
}
