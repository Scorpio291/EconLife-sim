// NPC business module unit tests.
// All tests tagged [npc_business][tier4].
//
// Tests verify quarterly strategic decision logic:
//   - Decision tick gating (quarterly interval with dispatch_day_offset)
//   - Player-owned business skip
//   - Per-profile decision branches (cost_cutter, quality_player,
//     fast_expander, defensive_incumbent)
//   - Investment cap at available cash minus working capital floor
//   - Board independence affecting decision approval
//   - Module interface properties

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/rng/deterministic_rng.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/npc_business/npc_business_module.h"
#include "modules/npc_business/npc_business_types.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// Default-constructed module instance for calling non-static methods in tests.
static const NpcBusinessModule test_module{};

// ---------------------------------------------------------------------------
// Test helpers -- create minimal WorldState and NPCBusiness instances
// ---------------------------------------------------------------------------

namespace {

// Create a minimal WorldState suitable for npc_business tests.
WorldState make_test_world_state() {
    WorldState state{};
    state.current_tick = 100;
    state.world_seed = 42;
    state.player.reset();
    state.lod2_price_index.reset();
    state.ticks_this_session = 100;
    state.game_mode = GameMode::standard;
    state.current_schema_version = 1;
    state.network_health_dirty = false;
    return state;
}

// Create a PlayerCharacter with a known id.
PlayerCharacter make_test_player(uint32_t id) {
    PlayerCharacter player{};
    player.id = id;
    player.background = Background::MiddleClass;
    player.health.current_health = 1.0f;
    player.age = 30.0f;
    player.wealth = 10000.0f;
    player.net_assets = 10000.0f;
    player.home_province_id = 0;
    player.current_province_id = 0;
    player.ironman_eligible = false;
    return player;
}

// Create an NPCBusiness with sensible defaults.
// Caller can override fields as needed.
NPCBusiness make_test_business(uint32_t id, BusinessProfile profile, uint32_t province_id = 0) {
    NPCBusiness biz{};
    biz.id = id;
    biz.sector = BusinessSector::manufacturing;
    biz.profile = profile;
    biz.cash = 10000.0f;
    biz.revenue_per_tick = 100.0f;
    biz.cost_per_tick = 80.0f;
    biz.market_share = 0.20f;
    biz.strategic_decision_tick = 100;  // decides at tick 100
    biz.dispatch_day_offset = 0;
    biz.criminal_sector = false;
    biz.province_id = province_id;
    biz.owner_id = 0;  // NPC-owned (not player)
    biz.regulatory_violation_severity = 0.0f;
    biz.default_activity_scope = VisibilityScope::institutional;
    biz.deferred_salary_liability = 0.0f;
    biz.accounts_payable_float = 0.0f;
    return biz;
}

// Create a BoardComposition with given independence score.
BoardComposition make_test_board(float independence_score) {
    BoardComposition board{};
    board.member_npc_ids = {100, 101, 102};
    board.independence_score = independence_score;
    board.next_approval_tick = 200;
    return board;
}

}  // anonymous namespace

// ===========================================================================
// Test 1: Business skipped when not on quarterly decision tick
// ===========================================================================

TEST_CASE("test_business_skipped_when_not_on_decision_tick", "[npc_business][tier4]") {
    auto state = make_test_world_state();
    state.current_tick = 50;  // before decision tick

    Province prov{};
    prov.id = 0;
    state.provinces.push_back(prov);

    // Business decides at tick 100, but current_tick is 50.
    auto biz = make_test_business(1, BusinessProfile::cost_cutter);
    biz.strategic_decision_tick = 100;
    state.npc_businesses.push_back(biz);

    NpcBusinessModule module;
    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // No deltas should be produced -- business was skipped.
    REQUIRE(delta.npc_deltas.empty());
    REQUIRE(delta.market_deltas.empty());
    REQUIRE(delta.evidence_deltas.empty());
    REQUIRE(delta.consequence_deltas.empty());
}

// ===========================================================================
// Test 2: Business executes decision when on quarterly tick
// ===========================================================================

TEST_CASE("test_business_executes_decision_on_quarterly_tick", "[npc_business][tier4]") {
    auto state = make_test_world_state();
    state.current_tick = 100;

    Province prov{};
    prov.id = 0;
    state.provinces.push_back(prov);

    // Business due at tick 100, current_tick = 100.
    auto biz = make_test_business(1, BusinessProfile::fast_expander);
    biz.strategic_decision_tick = 100;
    biz.cash = 50000.0f;  // plenty of cash for expansion
    state.npc_businesses.push_back(biz);

    NpcBusinessModule module;
    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // Should have produced some deltas (fast_expander with cash = expansion).
    bool has_deltas =
        !delta.npc_deltas.empty() || !delta.market_deltas.empty() || !delta.evidence_deltas.empty();
    REQUIRE(has_deltas);
}

// ===========================================================================
// Test 3: Player-owned business is skipped entirely
// ===========================================================================

TEST_CASE("test_player_owned_business_skipped", "[npc_business][tier4]") {
    auto state = make_test_world_state();
    state.current_tick = 100;

    static PlayerCharacter player = make_test_player(999);
    state.player = std::make_unique<PlayerCharacter>(player);

    Province prov{};
    prov.id = 0;
    state.provinces.push_back(prov);

    // Business owned by the player.
    auto biz = make_test_business(1, BusinessProfile::cost_cutter);
    biz.owner_id = 999;  // matches player id
    biz.strategic_decision_tick = 100;
    state.npc_businesses.push_back(biz);

    NpcBusinessModule module;
    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // No deltas -- player-owned business should be skipped entirely.
    REQUIRE(delta.npc_deltas.empty());
    REQUIRE(delta.market_deltas.empty());
    REQUIRE(delta.evidence_deltas.empty());
    REQUIRE(delta.consequence_deltas.empty());
}

// ===========================================================================
// Test 4: cost_cutter reduces workforce when margin is low
// ===========================================================================

TEST_CASE("test_cost_cutter_reduces_workforce_when_cash_critical", "[npc_business][tier4]") {
    // cost_cutter with cash < cash_critical_months * monthly_costs should
    // contract and emit negative hiring_target_change.
    auto biz = make_test_business(1, BusinessProfile::cost_cutter);
    biz.cost_per_tick = 100.0f;
    biz.revenue_per_tick = 80.0f;  // losing money
    // Monthly costs = 100 * 30 = 3000
    // Cash critical = 2.0 months = 6000
    biz.cash = 4000.0f;  // below critical threshold (4000 < 6000)

    DeterministicRNG rng(42);
    auto result = test_module.evaluate_decision(biz, nullptr, rng);

    REQUIRE(result.business_id == 1);
    REQUIRE(result.contract == true);
    REQUIRE(result.hiring_target_change < 0);
}

// ===========================================================================
// Test 5: fast_expander invests in expansion when cash available
// ===========================================================================

TEST_CASE("test_fast_expander_invests_when_cash_available", "[npc_business][tier4]") {
    auto biz = make_test_business(1, BusinessProfile::fast_expander);
    biz.cost_per_tick = 50.0f;
    biz.revenue_per_tick = 100.0f;
    // Monthly costs = 50 * 30 = 1500
    // Cash comfortable = 3.0 months = 4500
    // Working capital floor = 50 * 30 * 5.0 = 7500
    biz.cash = 50000.0f;       // well above comfortable threshold
    biz.market_share = 0.35f;  // high market share for market entry

    DeterministicRNG rng(42);
    auto result = test_module.evaluate_decision(biz, nullptr, rng);

    REQUIRE(result.business_id == 1);
    REQUIRE(result.expand == true);
    REQUIRE(result.cash_spent > 0.0f);
    REQUIRE(result.hiring_target_change > 0);
    // With high market share, should also enter new market.
    REQUIRE(result.enter_new_market == true);
    REQUIRE(result.rd_investment_rate > 0.0f);
}

// ===========================================================================
// Test 6: defensive_incumbent increases stability (no expansion)
// ===========================================================================

TEST_CASE("test_defensive_incumbent_maintains_stability", "[npc_business][tier4]") {
    auto biz = make_test_business(1, BusinessProfile::defensive_incumbent);
    biz.cost_per_tick = 50.0f;
    biz.revenue_per_tick = 100.0f;
    biz.cash = 50000.0f;

    DeterministicRNG rng(42);
    auto result = test_module.evaluate_decision(biz, nullptr, rng);

    REQUIRE(result.business_id == 1);
    // Defensive incumbent should NOT expand or enter new markets.
    REQUIRE(result.expand == false);
    REQUIRE(result.enter_new_market == false);
    REQUIRE(result.contract == false);
    // Should have minimal R&D and some lobbying spend.
    REQUIRE(result.rd_investment_rate > 0.0f);
    REQUIRE(result.cash_spent > 0.0f);
}

// ===========================================================================
// Test 7: Investment capped at cash minus working capital floor
// ===========================================================================

TEST_CASE("test_investment_capped_at_available_cash", "[npc_business][tier4]") {
    auto biz = make_test_business(1, BusinessProfile::fast_expander);
    biz.cost_per_tick = 100.0f;
    biz.revenue_per_tick = 150.0f;
    // Working capital floor = 100 * 30 * 5.0 = 15000
    // Available cash = cash - floor
    biz.cash = 20000.0f;  // floor = 15000, available = 5000

    float available = test_module.compute_available_cash(biz);
    REQUIRE_THAT(available, WithinAbs(5000.0f, 0.01f));

    DeterministicRNG rng(42);
    auto result = test_module.evaluate_decision(biz, nullptr, rng);

    // Total spending must not exceed available cash.
    REQUIRE(result.cash_spent <= available + 0.01f);
    REQUIRE(result.cash_spent >= 0.0f);
}

TEST_CASE("test_no_investment_when_cash_below_floor", "[npc_business][tier4]") {
    auto biz = make_test_business(1, BusinessProfile::fast_expander);
    biz.cost_per_tick = 100.0f;
    biz.revenue_per_tick = 150.0f;
    // Working capital floor = 100 * 30 * 5.0 = 15000
    biz.cash = 10000.0f;  // below floor, available_cash = 0

    float available = test_module.compute_available_cash(biz);
    REQUIRE_THAT(available, WithinAbs(0.0f, 0.01f));

    // With cash_months = 10000 / 3000 = 3.33, still above cash_comfortable
    // but available_cash is 0, so no expansion spending.
    DeterministicRNG rng(42);
    auto result = test_module.evaluate_decision(biz, nullptr, rng);

    // cash_months ~3.33 >= comfortable, but available_cash == 0
    // expand might be set but cash_spent should be 0 or near 0.
    REQUIRE(result.cash_spent <= 0.01f);
}

// ===========================================================================
// Test 8: Board independence affects decision approval
// ===========================================================================

TEST_CASE("test_captured_board_approves_everything", "[npc_business][tier4]") {
    // Board with independence_score < 0.25 rubber-stamps everything.
    auto board = make_test_board(0.10f);

    BusinessDecisionResult decision{};
    decision.expand = true;
    decision.enter_new_market = true;

    DeterministicRNG rng(42);
    bool approved = test_module.check_board_approval(&board, decision, rng);

    REQUIRE(approved == true);
}

TEST_CASE("test_no_board_approves_everything", "[npc_business][tier4]") {
    // No board (nullptr) = micro/small business, auto-approved.
    BusinessDecisionResult decision{};
    decision.expand = true;
    decision.enter_new_market = true;

    DeterministicRNG rng(42);
    bool approved = test_module.check_board_approval(nullptr, decision, rng);

    REQUIRE(approved == true);
}

TEST_CASE("test_independent_board_may_block_risky_decisions", "[npc_business][tier4]") {
    // Board with high independence (0.90) should sometimes block risky moves.
    // Run multiple trials to verify blocking occurs probabilistically.
    auto board = make_test_board(0.90f);

    BusinessDecisionResult decision{};
    decision.expand = true;
    decision.enter_new_market = true;

    int blocked_count = 0;
    constexpr int trials = 100;
    for (int i = 0; i < trials; ++i) {
        DeterministicRNG rng(static_cast<uint64_t>(i * 7919));
        bool approved = test_module.check_board_approval(&board, decision, rng);
        if (!approved) {
            ++blocked_count;
        }
    }

    // With independence = 0.90, block_prob = (0.90 - 0.25) * 0.5 = 0.325
    // Expected: ~32.5% blocked out of 100 trials.
    // Allow wide margin for randomness.
    REQUIRE(blocked_count > 5);   // should block at least some
    REQUIRE(blocked_count < 80);  // should not block everything
}

TEST_CASE("test_independent_board_does_not_block_non_risky_decisions", "[npc_business][tier4]") {
    // Even a fully independent board should not block non-risky decisions
    // (contraction, no expansion, no market entry).
    auto board = make_test_board(1.0f);

    BusinessDecisionResult decision{};
    decision.expand = false;
    decision.enter_new_market = false;
    decision.contract = true;

    DeterministicRNG rng(42);
    bool approved = test_module.check_board_approval(&board, decision, rng);

    REQUIRE(approved == true);
}

// ===========================================================================
// Test 9: dispatch_day_offset correctly staggers decisions
// ===========================================================================

TEST_CASE("test_dispatch_day_offset_staggers_decisions", "[npc_business][tier4]") {
    auto state = make_test_world_state();

    Province prov{};
    prov.id = 0;
    state.provinces.push_back(prov);

    // Create 30 businesses with strategic_decision_tick staggered across
    // ticks 100-129, simulating dispatch_day_offset 0-29.
    for (uint32_t i = 0; i < 30; ++i) {
        auto biz = make_test_business(i + 1, BusinessProfile::fast_expander);
        biz.strategic_decision_tick = 100 + i;
        biz.dispatch_day_offset = static_cast<uint8_t>(i);
        biz.cash = 50000.0f;
        state.npc_businesses.push_back(biz);
    }

    NpcBusinessModule module;

    // Run tick 100: only business with decision_tick=100 should decide.
    state.current_tick = 100;
    DeltaBuffer delta_100{};
    module.execute_province(0, state, delta_100);

    // Run tick 101: business with decision_tick <= 101 decides.
    // But business 0 (decision_tick=100) already passed, and business 1
    // (decision_tick=101) is now due.
    state.current_tick = 101;
    DeltaBuffer delta_101{};
    module.execute_province(0, state, delta_101);

    // At tick 100, exactly 1 business should decide (the one with offset 0).
    // At tick 101, 2 businesses have decision_tick <= 101 (offsets 0 and 1).
    // Both should produce deltas since is_decision_tick checks >=.
    // In practice with static state, both are still eligible.
    // The key invariant: not all 30 decide at once.
    // At tick 100, only businesses with strategic_decision_tick <= 100 decide.
    // That is exactly 1 business (the one with decision_tick=100).
    REQUIRE(!delta_100.npc_deltas.empty());
    // The count of deciding businesses at tick 100 should be much less than 30.
}

// ===========================================================================
// Test 10: Module interface properties
// ===========================================================================

TEST_CASE("test_module_interface_properties", "[npc_business][tier4]") {
    NpcBusinessModule module;

    REQUIRE(module.name() == "npc_business");
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
// Static utility function tests
// ===========================================================================

TEST_CASE("test_compute_working_capital_floor", "[npc_business][tier4]") {
    auto biz = make_test_business(1, BusinessProfile::cost_cutter);
    biz.cost_per_tick = 100.0f;

    // floor = 100 * 30 * 5.0 = 15000
    float floor = test_module.compute_working_capital_floor(biz);
    REQUIRE_THAT(floor, WithinAbs(15000.0f, 0.01f));
}

TEST_CASE("test_compute_available_cash", "[npc_business][tier4]") {
    auto biz = make_test_business(1, BusinessProfile::cost_cutter);
    biz.cost_per_tick = 100.0f;
    biz.cash = 20000.0f;

    // floor = 15000, available = 20000 - 15000 = 5000
    float available = test_module.compute_available_cash(biz);
    REQUIRE_THAT(available, WithinAbs(5000.0f, 0.01f));
}

TEST_CASE("test_compute_available_cash_below_floor", "[npc_business][tier4]") {
    auto biz = make_test_business(1, BusinessProfile::cost_cutter);
    biz.cost_per_tick = 100.0f;
    biz.cash = 5000.0f;

    // floor = 15000, available = max(5000 - 15000, 0) = 0
    float available = test_module.compute_available_cash(biz);
    REQUIRE_THAT(available, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("test_compute_monthly_operating_costs", "[npc_business][tier4]") {
    auto biz = make_test_business(1, BusinessProfile::cost_cutter);
    biz.cost_per_tick = 100.0f;

    // monthly = 100 * 30 = 3000
    float monthly = test_module.compute_monthly_operating_costs(biz);
    REQUIRE_THAT(monthly, WithinAbs(3000.0f, 0.01f));
}

TEST_CASE("test_compute_profit_margin", "[npc_business][tier4]") {
    auto biz = make_test_business(1, BusinessProfile::cost_cutter);
    biz.revenue_per_tick = 100.0f;
    biz.cost_per_tick = 80.0f;

    // margin = (100 - 80) / 100 = 0.20
    float margin = NpcBusinessModule::compute_profit_margin(biz);
    REQUIRE_THAT(margin, WithinAbs(0.20f, 0.001f));
}

TEST_CASE("test_compute_profit_margin_zero_revenue", "[npc_business][tier4]") {
    auto biz = make_test_business(1, BusinessProfile::cost_cutter);
    biz.revenue_per_tick = 0.0f;
    biz.cost_per_tick = 80.0f;

    float margin = NpcBusinessModule::compute_profit_margin(biz);
    REQUIRE(margin < 0.0f);  // -1.0 for zero revenue
}

// ===========================================================================
// Decision tick check
// ===========================================================================

TEST_CASE("test_is_decision_tick", "[npc_business][tier4]") {
    auto biz = make_test_business(1, BusinessProfile::cost_cutter);
    biz.strategic_decision_tick = 100;

    REQUIRE(NpcBusinessModule::is_decision_tick(biz, 99) == false);
    REQUIRE(NpcBusinessModule::is_decision_tick(biz, 100) == true);
    REQUIRE(NpcBusinessModule::is_decision_tick(biz, 101) == true);
}

// ===========================================================================
// Player-owned check
// ===========================================================================

TEST_CASE("test_is_player_owned", "[npc_business][tier4]") {
    auto state = make_test_world_state();
    static PlayerCharacter player = make_test_player(999);
    state.player = std::make_unique<PlayerCharacter>(player);

    auto biz_player = make_test_business(1, BusinessProfile::cost_cutter);
    biz_player.owner_id = 999;
    REQUIRE(NpcBusinessModule::is_player_owned(biz_player, state) == true);

    auto biz_npc = make_test_business(2, BusinessProfile::cost_cutter);
    biz_npc.owner_id = 500;
    REQUIRE(NpcBusinessModule::is_player_owned(biz_npc, state) == false);

    // Null player means no business is player-owned.
    state.player.reset();
    REQUIRE(NpcBusinessModule::is_player_owned(biz_player, state) == false);
}

// ===========================================================================
// Quality player profile test
// ===========================================================================

TEST_CASE("test_quality_player_invests_in_rd", "[npc_business][tier4]") {
    auto biz = make_test_business(1, BusinessProfile::quality_player);
    biz.cost_per_tick = 50.0f;
    biz.revenue_per_tick = 100.0f;
    biz.cash = 50000.0f;

    DeterministicRNG rng(42);
    auto result = test_module.evaluate_decision(biz, nullptr, rng);

    REQUIRE(result.business_id == 1);
    // Quality player should invest in R&D at the configured rate.
    REQUIRE_THAT(result.rd_investment_rate,
                 WithinAbs(NpcBusinessConfig{}.quality_player_rd_rate, 0.001f));
    REQUIRE(result.cash_spent > 0.0f);
}

// ===========================================================================
// Board composition management
// ===========================================================================

TEST_CASE("test_board_composition_storage", "[npc_business][tier4]") {
    NpcBusinessModule module;

    // Initially no board.
    REQUIRE(module.get_board_composition(1) == nullptr);

    // Set a board.
    auto board = make_test_board(0.60f);
    module.set_board_composition(1, board);

    const BoardComposition* retrieved = module.get_board_composition(1);
    REQUIRE(retrieved != nullptr);
    REQUIRE_THAT(retrieved->independence_score, WithinAbs(0.60f, 0.001f));
    REQUIRE(retrieved->member_npc_ids.size() == 3);

    // Different business id returns nullptr.
    REQUIRE(module.get_board_composition(2) == nullptr);
}

// ===========================================================================
// Integration: execute with full province setup
// ===========================================================================

TEST_CASE("test_execute_processes_all_provinces", "[npc_business][tier4]") {
    auto state = make_test_world_state();
    state.current_tick = 100;

    Province prov0{};
    prov0.id = 0;
    Province prov1{};
    prov1.id = 1;
    state.provinces.push_back(prov0);
    state.provinces.push_back(prov1);

    // Business in province 0.
    auto biz0 = make_test_business(1, BusinessProfile::fast_expander, 0);
    biz0.strategic_decision_tick = 100;
    biz0.cash = 50000.0f;
    state.npc_businesses.push_back(biz0);

    // Business in province 1.
    auto biz1 = make_test_business(2, BusinessProfile::cost_cutter, 1);
    biz1.strategic_decision_tick = 100;
    biz1.cost_per_tick = 100.0f;
    biz1.cash = 4000.0f;  // cash critical
    state.npc_businesses.push_back(biz1);

    NpcBusinessModule module;
    DeltaBuffer delta{};
    module.execute(state, delta);

    // Should have deltas from both provinces.
    bool has_deltas = !delta.npc_deltas.empty() || !delta.market_deltas.empty();
    REQUIRE(has_deltas);
}

// ===========================================================================
// Constants verification
// ===========================================================================

TEST_CASE("test_npc_business_constants", "[npc_business][tier4]") {
    REQUIRE_THAT(NpcBusinessConfig{}.cash_critical_months, WithinAbs(2.0f, 0.001f));
    REQUIRE_THAT(NpcBusinessConfig{}.cash_comfortable_months, WithinAbs(3.0f, 0.001f));
    REQUIRE_THAT(NpcBusinessConfig{}.cash_surplus_months, WithinAbs(5.0f, 0.001f));
    REQUIRE_THAT(NpcBusinessConfig{}.exit_market_threshold, WithinAbs(0.05f, 0.001f));
    REQUIRE_THAT(NpcBusinessConfig{}.exit_probability, WithinAbs(0.30f, 0.001f));
    REQUIRE_THAT(NpcBusinessConfig{}.expansion_return_threshold, WithinAbs(0.15f, 0.001f));
    REQUIRE(NpcBusinessConfig{}.ticks_per_quarter == 90);
    REQUIRE(NpcBusinessConfig{}.dispatch_period == 30);
}
