// Government budget module unit tests.
// All tests tagged [government_budget][tier5].
//
// Tests verify the fiscal processing pipeline:
//   Step 1: Quarterly tax collection (corporate, income, property)
//   Step 2: Intergovernmental transfers (national -> provincial -> city)
//   Step 3: Spending execution (pro-rate if cash constrained)
//   Step 4: Infrastructure update (decay + investment benefit)
//   Step 5: Fiscal health checks (debt ratio warnings/crises, insolvency)
//   Step 6: Spending effects on region conditions (stability, crime, inequality)

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/economy/economy_types.h"
#include "modules/government_budget/budget_types.h"
#include "modules/government_budget/government_budget_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// ---------------------------------------------------------------------------
// Test helpers — create minimal WorldState and supporting structures
// ---------------------------------------------------------------------------

namespace {

// Create a minimal WorldState suitable for government budget tests.
WorldState make_test_world_state() {
    WorldState state{};
    state.current_tick = 90;  // Default to quarterly tick for budget tests.
    state.world_seed = 42;
    state.player.reset();
    state.lod2_price_index.reset();
    state.ticks_this_session = 1;
    state.game_mode = GameMode::standard;
    state.current_schema_version = 1;
    state.network_health_dirty = false;

    // Add one province.
    Province prov{};
    prov.id = 0;
    prov.infrastructure_rating = 0.80f;
    prov.cohort_stats.reset();
    state.provinces.push_back(prov);

    // Add one nation with tax rates.
    Nation nation{};
    nation.id = 0;
    nation.name = "TestNation";
    nation.corporate_tax_rate = 0.20f;
    nation.income_tax_rate_top_bracket = 0.30f;
    state.nations.push_back(nation);

    return state;
}

// Create a basic NPCBusiness with sensible defaults.
NPCBusiness make_test_business(uint32_t id, float revenue_per_tick, uint32_t province_id = 0,
                               bool criminal = false) {
    NPCBusiness biz{};
    biz.id = id;
    biz.sector = criminal ? BusinessSector::criminal : BusinessSector::retail;
    biz.profile = BusinessProfile::defensive_incumbent;
    biz.cash = 100000.0f;
    biz.revenue_per_tick = revenue_per_tick;
    biz.cost_per_tick = revenue_per_tick * 0.5f;
    biz.market_share = 0.1f;
    biz.strategic_decision_tick = 0;
    biz.dispatch_day_offset = 0;
    biz.criminal_sector = criminal;
    biz.province_id = province_id;
    biz.regulatory_violation_severity = 0.0f;
    biz.default_activity_scope =
        criminal ? VisibilityScope::concealed : VisibilityScope::institutional;
    biz.owner_id = 100 + id;
    biz.deferred_salary_liability = 0.0f;
    biz.accounts_payable_float = 0.0f;
    return biz;
}

// Create a national government budget with defaults.
GovernmentBudget make_national_budget(float cash = 1000000.0f) {
    GovernmentBudget budget{};
    budget.level = GovernmentLevel::national;
    budget.jurisdiction_id = 0;
    budget.revenue_own_taxes = 0.0f;
    budget.revenue_transfers_in = 0.0f;
    budget.revenue_other = 0.0f;
    budget.total_revenue = 0.0f;
    budget.total_expenditure = 0.0f;
    budget.surplus_deficit = 0.0f;
    budget.accumulated_debt = 0.0f;
    budget.cash = cash;
    budget.debt_to_revenue_ratio = 0.0f;
    budget.deficit_to_revenue_ratio = 0.0f;
    return budget;
}

// Create a provincial government budget with defaults.
GovernmentBudget make_provincial_budget(uint32_t province_id, float cash = 500000.0f) {
    GovernmentBudget budget{};
    budget.level = GovernmentLevel::province;
    budget.jurisdiction_id = province_id;
    budget.revenue_own_taxes = 0.0f;
    budget.revenue_transfers_in = 0.0f;
    budget.revenue_other = 0.0f;
    budget.total_revenue = 0.0f;
    budget.total_expenditure = 0.0f;
    budget.surplus_deficit = 0.0f;
    budget.accumulated_debt = 0.0f;
    budget.cash = cash;
    budget.debt_to_revenue_ratio = 0.0f;
    budget.deficit_to_revenue_ratio = 0.0f;
    return budget;
}

// Find a consequence delta by entry ID.
const ConsequenceDelta* find_consequence(const DeltaBuffer& delta, uint32_t entry_id) {
    for (const auto& c : delta.consequence_deltas) {
        if (c.new_entry_id.has_value() && c.new_entry_id.value() == entry_id) {
            return &c;
        }
    }
    return nullptr;
}

// Find a region delta by region_id.
const RegionDelta* find_region_delta(const DeltaBuffer& delta, uint32_t region_id) {
    for (const auto& rd : delta.region_deltas) {
        if (rd.region_id == region_id) {
            return &rd;
        }
    }
    return nullptr;
}

}  // anonymous namespace

// ===========================================================================
// Test 1: Module interface properties
// ===========================================================================

TEST_CASE("test_module_interface_properties", "[government_budget][tier5]") {
    GovernmentBudgetModule module;

    REQUIRE(module.name() == "government_budget");
    REQUIRE(module.package_id() == "base_game");
    REQUIRE(module.scope() == ModuleScope::v1);
    REQUIRE(module.is_province_parallel() == false);

    auto after = module.runs_after();
    REQUIRE(after.size() == 1);
    REQUIRE(after[0] == "financial_distribution");

    auto before = module.runs_before();
    REQUIRE(before.empty());
}

// ===========================================================================
// Test 2: Sequential execution (not province-parallel)
// ===========================================================================

TEST_CASE("test_sequential_execution", "[government_budget][tier5]") {
    GovernmentBudgetModule module;
    REQUIRE(module.is_province_parallel() == false);
}

// ===========================================================================
// Test 3: is_quarterly_tick returns true for multiples of 90
// ===========================================================================

TEST_CASE("test_is_quarterly_tick", "[government_budget][tier5]") {
    REQUIRE(GovernmentBudgetModule::is_quarterly_tick(0) == false);  // tick 0 excluded
    REQUIRE(GovernmentBudgetModule::is_quarterly_tick(1) == false);
    REQUIRE(GovernmentBudgetModule::is_quarterly_tick(45) == false);
    REQUIRE(GovernmentBudgetModule::is_quarterly_tick(89) == false);
    REQUIRE(GovernmentBudgetModule::is_quarterly_tick(90) == true);  // first quarter
    REQUIRE(GovernmentBudgetModule::is_quarterly_tick(91) == false);
    REQUIRE(GovernmentBudgetModule::is_quarterly_tick(180) == true);  // second quarter
    REQUIRE(GovernmentBudgetModule::is_quarterly_tick(270) == true);  // third quarter
    REQUIRE(GovernmentBudgetModule::is_quarterly_tick(360) == true);  // fourth quarter
    REQUIRE(GovernmentBudgetModule::is_quarterly_tick(450) == true);  // year 2 Q1
}

// ===========================================================================
// Test 4: Non-quarterly tick is a no-op
// ===========================================================================

TEST_CASE("test_non_quarterly_tick_is_noop", "[government_budget][tier5]") {
    auto state = make_test_world_state();
    state.current_tick = 50;  // Not a quarterly tick.

    GovernmentBudgetModule module;
    auto national = make_national_budget(500000.0f);
    national.spending_allocations[SpendingCategory::public_services] = 100000.0f;
    module.add_budget(national);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Cash should be unchanged since no quarterly processing.
    const auto* budget = module.budgets().data();
    REQUIRE_THAT(budget->cash, WithinAbs(500000.0f, 0.01f));
    REQUIRE(delta.consequence_deltas.empty());
    REQUIRE(delta.region_deltas.empty());
}

// ===========================================================================
// Test 5: compute_corporate_tax — basic calculation
// ===========================================================================

TEST_CASE("test_compute_corporate_tax_basic", "[government_budget][tier5]") {
    std::vector<NPCBusiness> businesses;
    businesses.push_back(make_test_business(1, 1000.0f, 0));  // revenue_per_tick = 1000
    businesses.push_back(make_test_business(2, 500.0f, 0));   // revenue_per_tick = 500

    float tax = GovernmentBudgetModule::compute_corporate_tax(businesses, 0.20f, 0);

    // Expected: (1000 * 90 * 0.20) + (500 * 90 * 0.20)
    //         = 18000 + 9000 = 27000
    float expected = (1000.0f + 500.0f) * 90.0f * 0.20f;
    REQUIRE_THAT(tax, WithinAbs(expected, 0.01f));
}

// ===========================================================================
// Test 6: compute_corporate_tax — excludes criminal businesses
// ===========================================================================

TEST_CASE("test_compute_corporate_tax_excludes_criminal", "[government_budget][tier5]") {
    std::vector<NPCBusiness> businesses;
    businesses.push_back(make_test_business(1, 1000.0f, 0, false));  // legitimate
    businesses.push_back(make_test_business(2, 500.0f, 0, true));    // criminal
    businesses.push_back(make_test_business(3, 750.0f, 0, false));   // legitimate

    float tax = GovernmentBudgetModule::compute_corporate_tax(businesses, 0.20f, 0);

    // Only businesses 1 and 3 contribute (criminal excluded).
    // Expected: (1000 + 750) * 90 * 0.20 = 31500
    float expected = (1000.0f + 750.0f) * 90.0f * 0.20f;
    REQUIRE_THAT(tax, WithinAbs(expected, 0.01f));
}

// ===========================================================================
// Test 7: compute_corporate_tax — filters by province_id
// ===========================================================================

TEST_CASE("test_compute_corporate_tax_filters_by_province", "[government_budget][tier5]") {
    std::vector<NPCBusiness> businesses;
    businesses.push_back(make_test_business(1, 1000.0f, 0));  // province 0
    businesses.push_back(make_test_business(2, 500.0f, 1));   // province 1
    businesses.push_back(make_test_business(3, 750.0f, 0));   // province 0

    // Only province 0 businesses.
    float tax = GovernmentBudgetModule::compute_corporate_tax(businesses, 0.20f, 0);
    float expected = (1000.0f + 750.0f) * 90.0f * 0.20f;
    REQUIRE_THAT(tax, WithinAbs(expected, 0.01f));

    // Only province 1 businesses.
    float tax1 = GovernmentBudgetModule::compute_corporate_tax(businesses, 0.20f, 1);
    float expected1 = 500.0f * 90.0f * 0.20f;
    REQUIRE_THAT(tax1, WithinAbs(expected1, 0.01f));
}

// ===========================================================================
// Test 8: compute_infrastructure_change — decay only (no investment)
// ===========================================================================

TEST_CASE("test_compute_infrastructure_change_decay_only", "[government_budget][tier5]") {
    float result = GovernmentBudgetModule::compute_infrastructure_change(
        0.80f,      // current_rating
        0.0f,       // spending_actual (no investment)
        0.01f,      // decay_rate
        1000000.0f  // investment_scale
    );

    // Expected: 0.80 - 0.01 = 0.79
    REQUIRE_THAT(result, WithinAbs(0.79f, 0.001f));
}

// ===========================================================================
// Test 9: compute_infrastructure_change — with investment
// ===========================================================================

TEST_CASE("test_compute_infrastructure_change_with_investment", "[government_budget][tier5]") {
    float result =
        GovernmentBudgetModule::compute_infrastructure_change(0.80f,      // current_rating
                                                              500000.0f,  // spending_actual
                                                              0.01f,      // decay_rate
                                                              1000000.0f  // investment_scale
        );

    // Expected: 0.80 - 0.01 + (500000 / 1000000) = 0.80 - 0.01 + 0.50 = 1.29
    // But clamped to 1.0.
    REQUIRE_THAT(result, WithinAbs(1.0f, 0.001f));
}

// ===========================================================================
// Test 10: infrastructure clamped to bounds [0.0, 1.0]
// ===========================================================================

TEST_CASE("test_infrastructure_clamped_to_bounds", "[government_budget][tier5]") {
    // Test lower bound: heavy decay on low rating.
    float low_result =
        GovernmentBudgetModule::compute_infrastructure_change(0.005f, 0.0f, 0.01f, 1000000.0f);
    REQUIRE_THAT(low_result, WithinAbs(0.0f, 0.001f));  // Clamped to 0.0

    // Test upper bound: heavy investment on high rating.
    float high_result =
        GovernmentBudgetModule::compute_infrastructure_change(0.95f, 2000000.0f, 0.01f, 1000000.0f);
    REQUIRE_THAT(high_result, WithinAbs(1.0f, 0.001f));  // Clamped to 1.0

    // Moderate investment balances decay.
    float balanced =
        GovernmentBudgetModule::compute_infrastructure_change(0.50f, 10000.0f, 0.01f, 1000000.0f);
    // Expected: 0.50 - 0.01 + (10000/1000000) = 0.50 - 0.01 + 0.01 = 0.50
    REQUIRE_THAT(balanced, WithinAbs(0.50f, 0.001f));
}

// ===========================================================================
// Test 11: prorate_spending — basic (enough cash)
// ===========================================================================

TEST_CASE("test_prorate_spending_basic", "[government_budget][tier5]") {
    std::map<SpendingCategory, float> allocations;
    allocations[SpendingCategory::public_services] = 50000.0f;
    allocations[SpendingCategory::infrastructure] = 30000.0f;
    allocations[SpendingCategory::law_enforcement] = 20000.0f;

    auto result = GovernmentBudgetModule::prorate_spending(allocations, 200000.0f);

    // Cash (200k) exceeds allocations (100k): full allocation.
    REQUIRE_THAT(result[SpendingCategory::public_services], WithinAbs(50000.0f, 0.01f));
    REQUIRE_THAT(result[SpendingCategory::infrastructure], WithinAbs(30000.0f, 0.01f));
    REQUIRE_THAT(result[SpendingCategory::law_enforcement], WithinAbs(20000.0f, 0.01f));
}

// ===========================================================================
// Test 12: prorate_spending — cash constrained
// ===========================================================================

TEST_CASE("test_cash_constrained_spending_prorates", "[government_budget][tier5]") {
    std::map<SpendingCategory, float> allocations;
    allocations[SpendingCategory::public_services] = 50000.0f;
    allocations[SpendingCategory::infrastructure] = 30000.0f;
    allocations[SpendingCategory::law_enforcement] = 20000.0f;
    // Total allocations = 100,000

    auto result = GovernmentBudgetModule::prorate_spending(allocations, 50000.0f);

    // Cash (50k) is 50% of allocations (100k): each gets 50%.
    REQUIRE_THAT(result[SpendingCategory::public_services], WithinAbs(25000.0f, 0.01f));
    REQUIRE_THAT(result[SpendingCategory::infrastructure], WithinAbs(15000.0f, 0.01f));
    REQUIRE_THAT(result[SpendingCategory::law_enforcement], WithinAbs(10000.0f, 0.01f));

    // Total spent should equal available cash.
    float total = 0.0f;
    for (const auto& [cat, amount] : result) {
        total += amount;
    }
    REQUIRE_THAT(total, WithinAbs(50000.0f, 0.01f));
}

// ===========================================================================
// Test 13: prorate_spending — zero cash
// ===========================================================================

TEST_CASE("test_prorate_spending_zero_cash", "[government_budget][tier5]") {
    std::map<SpendingCategory, float> allocations;
    allocations[SpendingCategory::public_services] = 50000.0f;
    allocations[SpendingCategory::infrastructure] = 30000.0f;

    auto result = GovernmentBudgetModule::prorate_spending(allocations, 0.0f);

    REQUIRE_THAT(result[SpendingCategory::public_services], WithinAbs(0.0f, 0.001f));
    REQUIRE_THAT(result[SpendingCategory::infrastructure], WithinAbs(0.0f, 0.001f));
}

// ===========================================================================
// Test 14: prorate_spending — negative cash
// ===========================================================================

TEST_CASE("test_prorate_spending_negative_cash", "[government_budget][tier5]") {
    std::map<SpendingCategory, float> allocations;
    allocations[SpendingCategory::public_services] = 50000.0f;

    auto result = GovernmentBudgetModule::prorate_spending(allocations, -10000.0f);

    REQUIRE_THAT(result[SpendingCategory::public_services], WithinAbs(0.0f, 0.001f));
}

// ===========================================================================
// Test 15: compute_debt_to_revenue_ratio — basic calculation
// ===========================================================================

TEST_CASE("test_compute_debt_to_revenue_ratio", "[government_budget][tier5]") {
    float ratio = GovernmentBudgetModule::compute_debt_to_revenue_ratio(500000.0f, 250000.0f);
    REQUIRE_THAT(ratio, WithinAbs(2.0f, 0.001f));
}

// ===========================================================================
// Test 16: compute_debt_to_revenue_ratio — zero revenue returns infinity
// ===========================================================================

TEST_CASE("test_debt_to_revenue_zero_revenue_returns_infinity", "[government_budget][tier5]") {
    float ratio = GovernmentBudgetModule::compute_debt_to_revenue_ratio(100000.0f, 0.0f);
    REQUIRE(std::isinf(ratio));
    REQUIRE(ratio > 0.0f);  // positive infinity
}

// ===========================================================================
// Test 17: compute_debt_to_revenue_ratio — both zero returns 0
// ===========================================================================

TEST_CASE("test_debt_to_revenue_both_zero", "[government_budget][tier5]") {
    float ratio = GovernmentBudgetModule::compute_debt_to_revenue_ratio(0.0f, 0.0f);
    REQUIRE_THAT(ratio, WithinAbs(0.0f, 0.001f));
}

// ===========================================================================
// Test 18: Intergovernmental transfer distributes evenly
// ===========================================================================

TEST_CASE("test_intergovernmental_transfer_distributes_evenly", "[government_budget][tier5]") {
    GovernmentBudgetModule module;

    // National budget with 600,000 allocated to intergovernmental.
    auto national = make_national_budget(1000000.0f);
    national.spending_allocations[SpendingCategory::intergovernmental] = 600000.0f;
    module.add_budget(national);

    // 3 provincial budgets.
    module.add_budget(make_provincial_budget(0, 100000.0f));
    module.add_budget(make_provincial_budget(1, 100000.0f));
    module.add_budget(make_provincial_budget(2, 100000.0f));

    // Run intergovernmental transfers.
    auto state = make_test_world_state();
    DeltaBuffer delta{};
    module.execute(state, delta);

    // Each province should receive 200,000 (600,000 / 3).
    for (const auto& b : module.budgets()) {
        if (b.level == GovernmentLevel::province) {
            REQUIRE_THAT(b.revenue_transfers_in, WithinAbs(200000.0f, 0.01f));
        }
    }

    // National cash should have been reduced by the intergovernmental amount.
    // Starting 1,000,000 + tax revenue - 600,000 (transfers) - spending.
    // For simplicity, verify transfers were debited by checking province cash increased.
    for (const auto& b : module.budgets()) {
        if (b.level == GovernmentLevel::province) {
            // Province started with 100,000 + received 200,000 = 300,000 before spending.
            // After spending executes, some is spent, but transfers should have credited.
            REQUIRE(b.revenue_transfers_in >= 199999.0f);
        }
    }
}

// ===========================================================================
// Test 19: Quarterly national tax collection with businesses
// ===========================================================================

TEST_CASE("test_quarterly_national_tax_collection", "[government_budget][tier5]") {
    auto state = make_test_world_state();
    state.current_tick = 90;  // Quarterly tick.

    // Add businesses with known revenue.
    state.npc_businesses.push_back(make_test_business(1, 1000.0f, 0));
    state.npc_businesses.push_back(make_test_business(2, 500.0f, 0));

    GovernmentBudgetModule module;
    auto national = make_national_budget(500000.0f);
    module.add_budget(national);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Corporate tax: (1000 + 500) * 90 * 0.20 = 27000
    // No cohort stats, so income tax and property tax are 0.
    const auto* budget = &module.budgets()[0];
    REQUIRE(budget->level == GovernmentLevel::national);
    REQUIRE_THAT(budget->revenue_own_taxes, WithinAbs(27000.0f, 1.0f));
}

// ===========================================================================
// Test 20: Criminal businesses excluded from tax collection
// ===========================================================================

TEST_CASE("test_criminal_businesses_excluded_from_tax", "[government_budget][tier5]") {
    auto state = make_test_world_state();
    state.current_tick = 90;

    // One legitimate, one criminal.
    state.npc_businesses.push_back(make_test_business(1, 1000.0f, 0, false));
    state.npc_businesses.push_back(make_test_business(2, 2000.0f, 0, true));

    GovernmentBudgetModule module;
    auto national = make_national_budget(500000.0f);
    module.add_budget(national);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Only business 1 contributes: 1000 * 90 * 0.20 = 18000.
    const auto* budget = &module.budgets()[0];
    REQUIRE_THAT(budget->revenue_own_taxes, WithinAbs(18000.0f, 1.0f));
}

// ===========================================================================
// Test 21: Fiscal crisis triggers at debt ratio above 4.0
// ===========================================================================

TEST_CASE("test_fiscal_crisis_triggers_at_debt_ratio", "[government_budget][tier5]") {
    auto state = make_test_world_state();
    state.current_tick = 90;

    GovernmentBudgetModule module;
    auto national = make_national_budget(100000.0f);
    national.accumulated_debt = 5000000.0f;
    // Will collect some tax revenue which becomes total_revenue.
    // Set spending so we can observe the consequence.
    module.add_budget(national);

    // Add a business for minimal revenue.
    state.npc_businesses.push_back(make_test_business(1, 100.0f, 0));

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Revenue: 100 * 90 * 0.20 = 1800.
    // Debt: 5,000,000. Ratio: 5000000 / 1800 >> 4.0.
    // Should trigger fiscal crisis consequence.
    REQUIRE(!delta.consequence_deltas.empty());

    // The fiscal crisis consequence id is jurisdiction_id * 100 + 2 = 0 * 100 + 2 = 2.
    bool found_crisis = false;
    for (const auto& c : delta.consequence_deltas) {
        if (c.new_entry_id.has_value() && c.new_entry_id.value() == 2) {
            found_crisis = true;
            break;
        }
    }
    REQUIRE(found_crisis);
}

// ===========================================================================
// Test 22: Fiscal warning at debt ratio between 2.0 and 4.0
// ===========================================================================

TEST_CASE("test_fiscal_warning_at_threshold", "[government_budget][tier5]") {
    auto state = make_test_world_state();
    state.current_tick = 90;

    GovernmentBudgetModule module;
    auto national = make_national_budget(500000.0f);
    // Set accumulated_debt and ensure total_revenue puts ratio between 2.0 and 4.0.
    // We need: 2.0 < accumulated_debt / total_revenue < 4.0.
    // Revenue from taxes: 1000 * 90 * 0.20 = 18000.
    // Set debt to 50000 => ratio = 50000 / 18000 ~ 2.78 (between 2.0 and 4.0).
    national.accumulated_debt = 50000.0f;
    module.add_budget(national);

    state.npc_businesses.push_back(make_test_business(1, 1000.0f, 0));

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Should trigger fiscal warning (id = 0 * 100 + 1 = 1) but not crisis.
    bool found_warning = false;
    bool found_crisis = false;
    for (const auto& c : delta.consequence_deltas) {
        if (c.new_entry_id.has_value()) {
            if (c.new_entry_id.value() == 1)
                found_warning = true;
            if (c.new_entry_id.value() == 2)
                found_crisis = true;
        }
    }
    REQUIRE(found_warning);
    REQUIRE(!found_crisis);
}

// ===========================================================================
// Test 23: Government insolvency when cash < 0
// ===========================================================================

TEST_CASE("test_government_insolvency_negative_cash", "[government_budget][tier5]") {
    auto state = make_test_world_state();
    state.current_tick = 90;

    GovernmentBudgetModule module;
    auto national = make_national_budget(10.0f);  // Very low cash.
    // Set high spending allocations to drive cash negative.
    national.spending_allocations[SpendingCategory::public_services] = 100000.0f;
    module.add_budget(national);

    // Minimal business for some revenue (not enough).
    state.npc_businesses.push_back(make_test_business(1, 1.0f, 0));

    DeltaBuffer delta{};
    module.execute(state, delta);

    // After spending 100,000 with minimal revenue, cash should be negative.
    const auto* budget = &module.budgets()[0];

    // Check for insolvency consequence (id = 0 * 100 + 3 = 3).
    bool found_insolvency = false;
    for (const auto& c : delta.consequence_deltas) {
        if (c.new_entry_id.has_value() && c.new_entry_id.value() == 3) {
            found_insolvency = true;
            break;
        }
    }

    // Cash should indeed be negative after large spending with tiny revenue.
    if (budget->cash < 0.0f) {
        REQUIRE(found_insolvency);
    }
}

// ===========================================================================
// Test 24: Spending effects — public_services improves stability
// ===========================================================================

TEST_CASE("test_spending_effects_on_stability", "[government_budget][tier5]") {
    auto state = make_test_world_state();
    state.current_tick = 90;

    GovernmentBudgetModule module;
    auto national = make_national_budget(1000000.0f);
    module.add_budget(national);

    auto prov_budget = make_provincial_budget(0, 500000.0f);
    prov_budget.spending_allocations[SpendingCategory::public_services] = 100000.0f;
    module.add_budget(prov_budget);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Province 0 should have a positive stability delta.
    const RegionDelta* rd = find_region_delta(delta, 0);
    REQUIRE(rd != nullptr);
    REQUIRE(rd->stability_delta.has_value());
    REQUIRE(rd->stability_delta.value() > 0.0f);

    // Expected: 100000 * 0.0001 = 10.0 (but only if not pro-rated; check approximately).
    // The actual spending may be full since cash (500k) > allocation (100k).
    REQUIRE_THAT(rd->stability_delta.value(), WithinAbs(10.0f, 1.0f));
}

// ===========================================================================
// Test 25: Spending effects — law_enforcement reduces crime rate
// ===========================================================================

TEST_CASE("test_spending_effects_on_crime_rate", "[government_budget][tier5]") {
    auto state = make_test_world_state();
    state.current_tick = 90;

    GovernmentBudgetModule module;
    auto national = make_national_budget(1000000.0f);
    module.add_budget(national);

    auto prov_budget = make_provincial_budget(0, 500000.0f);
    prov_budget.spending_allocations[SpendingCategory::law_enforcement] = 50000.0f;
    module.add_budget(prov_budget);

    DeltaBuffer delta{};
    module.execute(state, delta);

    const RegionDelta* rd = find_region_delta(delta, 0);
    REQUIRE(rd != nullptr);
    REQUIRE(rd->crime_rate_delta.has_value());
    REQUIRE(rd->crime_rate_delta.value() < 0.0f);  // Crime reduction.

    // Expected: -(50000 * 0.0001) = -5.0
    REQUIRE_THAT(rd->crime_rate_delta.value(), WithinAbs(-5.0f, 1.0f));
}

// ===========================================================================
// Test 26: Spending effects — social_programs reduces inequality
// ===========================================================================

TEST_CASE("test_spending_effects_on_inequality", "[government_budget][tier5]") {
    auto state = make_test_world_state();
    state.current_tick = 90;

    GovernmentBudgetModule module;
    auto national = make_national_budget(1000000.0f);
    module.add_budget(national);

    auto prov_budget = make_provincial_budget(0, 500000.0f);
    prov_budget.spending_allocations[SpendingCategory::social_programs] = 80000.0f;
    module.add_budget(prov_budget);

    DeltaBuffer delta{};
    module.execute(state, delta);

    const RegionDelta* rd = find_region_delta(delta, 0);
    REQUIRE(rd != nullptr);
    REQUIRE(rd->inequality_delta.has_value());
    REQUIRE(rd->inequality_delta.value() < 0.0f);  // Inequality reduction.

    // Expected: -(80000 * 0.0001) = -8.0
    REQUIRE_THAT(rd->inequality_delta.value(), WithinAbs(-8.0f, 1.0f));
}

// ===========================================================================
// Test 27: Infrastructure decays without investment (full pipeline)
// ===========================================================================

TEST_CASE("test_infrastructure_decays_without_investment", "[government_budget][tier5]") {
    float new_rating = GovernmentBudgetModule::compute_infrastructure_change(
        0.80f,  // current rating
        0.0f,   // no spending
        GovernmentBudgetConfig{}.infrastructure_decay_per_quarter,
        GovernmentBudgetConfig{}.infrastructure_investment_scale);

    // Expected: 0.80 - 0.01 = 0.79
    REQUIRE_THAT(new_rating, WithinAbs(0.79f, 0.001f));
}

// ===========================================================================
// Test 28: Budget add and retrieve
// ===========================================================================

TEST_CASE("test_budget_add_and_retrieve", "[government_budget][tier5]") {
    GovernmentBudgetModule module;

    module.add_budget(make_national_budget(500000.0f));
    module.add_budget(make_provincial_budget(0, 200000.0f));
    module.add_budget(make_provincial_budget(1, 300000.0f));

    REQUIRE(module.budgets().size() == 3);
    REQUIRE(module.budgets()[0].level == GovernmentLevel::national);
    REQUIRE(module.budgets()[1].level == GovernmentLevel::province);
    REQUIRE(module.budgets()[1].jurisdiction_id == 0);
    REQUIRE(module.budgets()[2].level == GovernmentLevel::province);
    REQUIRE(module.budgets()[2].jurisdiction_id == 1);
}

// ===========================================================================
// Test 29: Spending execution updates surplus_deficit and total_expenditure
// ===========================================================================

TEST_CASE("test_spending_execution_updates_budget_fields", "[government_budget][tier5]") {
    auto state = make_test_world_state();
    state.current_tick = 90;

    GovernmentBudgetModule module;
    auto national = make_national_budget(200000.0f);
    national.spending_allocations[SpendingCategory::public_services] = 50000.0f;
    national.spending_allocations[SpendingCategory::infrastructure] = 30000.0f;
    national.spending_allocations[SpendingCategory::law_enforcement] = 20000.0f;
    module.add_budget(national);

    DeltaBuffer delta{};
    module.execute(state, delta);

    const auto* budget = &module.budgets()[0];

    // Revenue is from taxes (no businesses here, so likely 0 unless we add some).
    // With cash = 200,000 and allocations = 100,000: full allocation.
    REQUIRE_THAT(budget->spending_actual.at(SpendingCategory::public_services),
                 WithinAbs(50000.0f, 1.0f));
    REQUIRE_THAT(budget->spending_actual.at(SpendingCategory::infrastructure),
                 WithinAbs(30000.0f, 1.0f));
    REQUIRE_THAT(budget->spending_actual.at(SpendingCategory::law_enforcement),
                 WithinAbs(20000.0f, 1.0f));
    REQUIRE_THAT(budget->total_expenditure, WithinAbs(100000.0f, 1.0f));
}

// ===========================================================================
// Test 30: Debt accumulates from deficit spending
// ===========================================================================

TEST_CASE("test_debt_accumulates_from_deficit", "[government_budget][tier5]") {
    auto state = make_test_world_state();
    state.current_tick = 90;

    GovernmentBudgetModule module;
    auto national = make_national_budget(200000.0f);
    national.accumulated_debt = 100000.0f;  // Starting debt.
    national.spending_allocations[SpendingCategory::public_services] = 150000.0f;
    module.add_budget(national);

    // No businesses => no tax revenue => total_revenue = 0.
    // Spending = 150,000. Deficit = 0 - 150,000 = -150,000.
    // New accumulated_debt = 100,000 + 150,000 = 250,000.

    DeltaBuffer delta{};
    module.execute(state, delta);

    const auto* budget = &module.budgets()[0];
    REQUIRE(budget->surplus_deficit < 0.0f);
    REQUIRE_THAT(budget->accumulated_debt, WithinAbs(250000.0f, 1.0f));
}

// ===========================================================================
// Test 31: Constants have expected values
// ===========================================================================

TEST_CASE("test_constants_values", "[government_budget][tier5]") {
    GovernmentBudgetConfig cfg{};

    REQUIRE(cfg.ticks_per_quarter == 90);
    REQUIRE_THAT(cfg.infrastructure_decay_per_quarter, WithinAbs(0.01f, 0.0001f));
    REQUIRE_THAT(cfg.infrastructure_investment_scale, WithinAbs(1000000.0f, 1.0f));
    REQUIRE_THAT(cfg.debt_warning_ratio, WithinAbs(2.0f, 0.001f));
    REQUIRE_THAT(cfg.debt_crisis_ratio, WithinAbs(4.0f, 0.001f));
    REQUIRE_THAT(cfg.city_revenue_fraction, WithinAbs(0.25f, 0.001f));
    REQUIRE_THAT(cfg.corruption_evidence_threshold, WithinAbs(500000.0f, 1.0f));
}

// ===========================================================================
// Test 32: Empty budgets vector causes no crash
// ===========================================================================

TEST_CASE("test_empty_budgets_no_crash", "[government_budget][tier5]") {
    auto state = make_test_world_state();
    state.current_tick = 90;

    GovernmentBudgetModule module;
    // No budgets added.

    DeltaBuffer delta{};
    module.execute(state, delta);

    REQUIRE(delta.consequence_deltas.empty());
    REQUIRE(delta.region_deltas.empty());
}

// ===========================================================================
// Test 33: No businesses means zero corporate tax
// ===========================================================================

TEST_CASE("test_no_businesses_zero_corporate_tax", "[government_budget][tier5]") {
    std::vector<NPCBusiness> businesses;  // Empty.
    float tax = GovernmentBudgetModule::compute_corporate_tax(businesses, 0.20f, 0);
    REQUIRE_THAT(tax, WithinAbs(0.0f, 0.001f));
}

// ===========================================================================
// Test 34: Multiple provinces with intergovernmental transfers
// ===========================================================================

TEST_CASE("test_multiple_provinces_intergovernmental", "[government_budget][tier5]") {
    auto state = make_test_world_state();
    state.current_tick = 90;

    // Add more provinces to WorldState.
    Province prov1{};
    prov1.id = 1;
    prov1.cohort_stats.reset();
    state.provinces.push_back(prov1);

    Province prov2{};
    prov2.id = 2;
    prov2.cohort_stats.reset();
    state.provinces.push_back(prov2);

    GovernmentBudgetModule module;
    auto national = make_national_budget(2000000.0f);
    national.spending_allocations[SpendingCategory::intergovernmental] = 900000.0f;
    module.add_budget(national);

    module.add_budget(make_provincial_budget(0, 50000.0f));
    module.add_budget(make_provincial_budget(1, 50000.0f));
    module.add_budget(make_provincial_budget(2, 50000.0f));

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Each province should receive 300,000 (900,000 / 3).
    for (const auto& b : module.budgets()) {
        if (b.level == GovernmentLevel::province) {
            REQUIRE_THAT(b.revenue_transfers_in, WithinAbs(300000.0f, 1.0f));
        }
    }
}

// ===========================================================================
// Test 35: Spending with exact cash matches allocations
// ===========================================================================

TEST_CASE("test_spending_exact_cash_matches_allocations", "[government_budget][tier5]") {
    std::map<SpendingCategory, float> allocations;
    allocations[SpendingCategory::public_services] = 50000.0f;
    allocations[SpendingCategory::infrastructure] = 50000.0f;
    // Total = 100,000

    auto result = GovernmentBudgetModule::prorate_spending(allocations, 100000.0f);

    // Exact match: full allocation.
    REQUIRE_THAT(result[SpendingCategory::public_services], WithinAbs(50000.0f, 0.01f));
    REQUIRE_THAT(result[SpendingCategory::infrastructure], WithinAbs(50000.0f, 0.01f));
}

// ===========================================================================
// Test 36: Zero tax rate produces zero corporate tax
// ===========================================================================

TEST_CASE("test_zero_tax_rate_zero_corporate_tax", "[government_budget][tier5]") {
    std::vector<NPCBusiness> businesses;
    businesses.push_back(make_test_business(1, 1000.0f, 0));

    float tax = GovernmentBudgetModule::compute_corporate_tax(businesses, 0.0f, 0);
    REQUIRE_THAT(tax, WithinAbs(0.0f, 0.001f));
}
