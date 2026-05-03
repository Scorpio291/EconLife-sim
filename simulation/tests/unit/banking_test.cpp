// Banking module unit tests.
// All tests tagged [banking][tier5].
//
// Tests verify the loan and credit processing pipeline:
//   1. Loan repayment reduces outstanding balance and improves credit score
//   2. Missed payments trigger default after grace period
//   3. Loan application evaluation (credit score, DTI, purpose)
//   4. Interest rate computation with credit risk and collateral
//   5. Default handling (collateral seizure, criminal violence escalation)
//   6. Loan maturity retirement
//   7. Credit score invariants (clamped to [0.0, 1.0])
//   8. Player and NPC loan repayment delta generation
//   9. Deterministic processing order (loans sorted by id ascending)

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/banking/banking_module.h"
#include "modules/banking/banking_types.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// ---------------------------------------------------------------------------
// Test helpers — create minimal WorldState and supporting structures
// ---------------------------------------------------------------------------

namespace {

// Create a minimal WorldState suitable for banking tests.
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

// Create a minimal PlayerCharacter for testing.
PlayerCharacter make_test_player(uint32_t id = 1, float wealth = 10000.0f) {
    PlayerCharacter player{};
    player.id = id;
    player.wealth = wealth;
    return player;
}

// Create an NPC with a given id and capital.
NPC make_test_npc(uint32_t id, float capital = 5000.0f) {
    NPC npc{};
    npc.id = id;
    npc.role = NPCRole::worker;
    npc.capital = capital;
    npc.risk_tolerance = 0.5f;
    npc.home_province_id = 0;
    npc.current_province_id = 0;
    npc.status = NPCStatus::active;
    return npc;
}

// Create a LoanRecord with sensible defaults.
LoanRecord make_test_loan(uint32_t id, uint32_t borrower_id, uint32_t lender_id,
                          LoanPurpose purpose = LoanPurpose::business_capital,
                          float principal = 10000.0f, float outstanding = 10000.0f,
                          float interest_rate = 0.000050f, float repayment_per_tick = 30.0f,
                          uint32_t originated_tick = 0, uint32_t maturity_tick = 500,
                          uint32_t collateral_id = 0) {
    LoanRecord loan{};
    loan.id = id;
    loan.borrower_id = borrower_id;
    loan.lender_id = lender_id;
    loan.purpose = purpose;
    loan.principal = principal;
    loan.outstanding_balance = outstanding;
    loan.interest_rate = interest_rate;
    loan.repayment_per_tick = repayment_per_tick;
    loan.originated_tick = originated_tick;
    loan.maturity_tick = maturity_tick;
    loan.in_default = false;
    loan.collateral_id = collateral_id;
    return loan;
}

}  // anonymous namespace

// ===========================================================================
// Test 1: Successful repayment reduces balance and improves credit
// ===========================================================================

TEST_CASE("test_successful_repayment_reduces_balance_and_improves_credit", "[banking][tier5]") {
    auto state = make_test_world_state(10);
    NPC npc = make_test_npc(100, 5000.0f);
    state.significant_npcs.push_back(npc);

    BankingModule module;

    // Add a borrower credit record.
    BankingModule::BorrowerCredit bc{};
    bc.borrower_id = 100;
    bc.profile.credit_score = 0.50f;
    bc.consecutive_misses = 0;
    module.borrower_credits().push_back(bc);

    // Loan: outstanding 10,000, repayment 30/tick, interest rate 0.00005/tick.
    LoanRecord loan = make_test_loan(1, 100, 200);
    module.active_loans().push_back(loan);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Outstanding balance should be reduced.
    REQUIRE(module.active_loans().size() == 1);
    const auto& processed_loan = module.active_loans()[0];

    // interest = 10,000 * 0.00005 = 0.5
    // principal_payment = 30.0 - 0.5 = 29.5
    // new_balance = 10,000 - 29.5 = 9970.5
    REQUIRE_THAT(processed_loan.outstanding_balance, WithinAbs(9970.5f, 0.1f));

    // Credit score should have improved by 0.002.
    const auto& credits = module.borrower_credits();
    REQUIRE(credits.size() == 1);
    REQUIRE_THAT(credits[0].profile.credit_score, WithinAbs(0.502f, 0.001f));
}

// ===========================================================================
// Test 2: Missed payment triggers default after grace period
// ===========================================================================

TEST_CASE("test_missed_payment_triggers_default_after_grace_period", "[banking][tier5]") {
    auto state = make_test_world_state(10);
    // NPC with zero capital — cannot afford any repayment.
    NPC npc = make_test_npc(100, 0.0f);
    state.significant_npcs.push_back(npc);

    BankingModule module;

    BankingModule::BorrowerCredit bc{};
    bc.borrower_id = 100;
    bc.profile.credit_score = 0.60f;
    bc.consecutive_misses = 0;
    module.borrower_credits().push_back(bc);

    LoanRecord loan = make_test_loan(1, 100, 200);
    module.active_loans().push_back(loan);

    DeltaBuffer delta{};

    // Miss 1: no default yet.
    module.execute(state, delta);
    REQUIRE(module.borrower_credits()[0].consecutive_misses == 1);
    REQUIRE(module.active_loans()[0].in_default == false);

    // Miss 2.
    delta = DeltaBuffer{};
    module.execute(state, delta);
    REQUIRE(module.borrower_credits()[0].consecutive_misses == 2);
    REQUIRE(module.active_loans()[0].in_default == false);

    // Miss 3.
    delta = DeltaBuffer{};
    module.execute(state, delta);
    REQUIRE(module.borrower_credits()[0].consecutive_misses == 3);
    REQUIRE(module.active_loans()[0].in_default == false);

    // Miss 4: exceeds grace period (default_grace_ticks = 3), triggers default.
    delta = DeltaBuffer{};
    module.execute(state, delta);
    REQUIRE(module.borrower_credits()[0].consecutive_misses == 4);
    REQUIRE(module.active_loans()[0].in_default == true);
}

// ===========================================================================
// Test 3: Loan denial on high DTI
// ===========================================================================

TEST_CASE("test_loan_denial_on_high_dti", "[banking][tier5]") {
    // DTI above threshold (0.40) should be denied.
    bool approved = BankingModule::evaluate_loan_application(
        0.60f, 0.50f, LoanPurpose::business_capital, BankingConfig{}.per_tick_denial_dti_threshold);
    REQUIRE(approved == false);
}

TEST_CASE("test_loan_approval_on_acceptable_dti", "[banking][tier5]") {
    // DTI at threshold boundary should be approved.
    bool approved = BankingModule::evaluate_loan_application(
        0.60f, 0.40f, LoanPurpose::business_capital, BankingConfig{}.per_tick_denial_dti_threshold);
    REQUIRE(approved == true);
}

TEST_CASE("test_loan_approval_on_low_dti", "[banking][tier5]") {
    bool approved = BankingModule::evaluate_loan_application(
        0.60f, 0.10f, LoanPurpose::business_capital, BankingConfig{}.per_tick_denial_dti_threshold);
    REQUIRE(approved == true);
}

// ===========================================================================
// Test 4: Loan denial on low credit score
// ===========================================================================

TEST_CASE("test_loan_denial_on_low_credit_score_business", "[banking][tier5]") {
    // Business capital requires 0.35; credit score 0.30 should fail.
    bool approved = BankingModule::evaluate_loan_application(
        0.30f, 0.10f, LoanPurpose::business_capital, BankingConfig{}.per_tick_denial_dti_threshold);
    REQUIRE(approved == false);
}

TEST_CASE("test_loan_denial_on_low_credit_score_property", "[banking][tier5]") {
    // Property purchase requires 0.45; credit score 0.40 should fail.
    bool approved =
        BankingModule::evaluate_loan_application(0.40f, 0.10f, LoanPurpose::property_purchase,
                                                 BankingConfig{}.per_tick_denial_dti_threshold);
    REQUIRE(approved == false);
}

TEST_CASE("test_loan_denial_on_low_credit_score_personal", "[banking][tier5]") {
    // Personal requires 0.25; credit score 0.20 should fail.
    bool approved = BankingModule::evaluate_loan_application(
        0.20f, 0.10f, LoanPurpose::personal, BankingConfig{}.per_tick_denial_dti_threshold);
    REQUIRE(approved == false);
}

TEST_CASE("test_loan_approval_at_exact_minimum_credit_score", "[banking][tier5]") {
    // Exactly at threshold should pass.
    bool approved = BankingModule::evaluate_loan_application(
        0.35f, 0.10f, LoanPurpose::business_capital, BankingConfig{}.per_tick_denial_dti_threshold);
    REQUIRE(approved == true);
}

// ===========================================================================
// Test 5: Interest rate reflects credit risk
// ===========================================================================

TEST_CASE("test_interest_rate_reflects_credit_risk", "[banking][tier5]") {
    // High credit score (0.9): lower rate.
    float rate_high = BankingModule::compute_interest_rate(
        0.9f, false, BankingConfig{}.per_tick_base_interest_rate,
        BankingConfig{}.credit_risk_spread, BankingConfig{}.collateral_rate_discount);

    // Low credit score (0.3): higher rate.
    float rate_low = BankingModule::compute_interest_rate(
        0.3f, false, BankingConfig{}.per_tick_base_interest_rate,
        BankingConfig{}.credit_risk_spread, BankingConfig{}.collateral_rate_discount);

    REQUIRE(rate_low > rate_high);
}

// ===========================================================================
// Test 6: Collateral reduces interest rate
// ===========================================================================

TEST_CASE("test_collateral_reduces_interest_rate", "[banking][tier5]") {
    float rate_unsecured = BankingModule::compute_interest_rate(
        0.6f, false, BankingConfig{}.per_tick_base_interest_rate,
        BankingConfig{}.credit_risk_spread, BankingConfig{}.collateral_rate_discount);
    float rate_secured = BankingModule::compute_interest_rate(
        0.6f, true, BankingConfig{}.per_tick_base_interest_rate, BankingConfig{}.credit_risk_spread,
        BankingConfig{}.collateral_rate_discount);

    REQUIRE(rate_secured < rate_unsecured);
    REQUIRE_THAT(rate_unsecured - rate_secured,
                 WithinAbs(BankingConfig{}.collateral_rate_discount, 0.000001f));
}

// ===========================================================================
// Test 7: Criminal informal default — no credit impact
// ===========================================================================

TEST_CASE("test_criminal_informal_default_no_credit_impact", "[banking][tier5]") {
    auto state = make_test_world_state(10);
    NPC npc = make_test_npc(100, 0.0f);  // no cash
    state.significant_npcs.push_back(npc);

    BankingModule module;

    BankingModule::BorrowerCredit bc{};
    bc.borrower_id = 100;
    bc.profile.credit_score = 0.60f;
    bc.consecutive_misses = 0;
    module.borrower_credits().push_back(bc);

    // Criminal informal loan.
    LoanRecord loan = make_test_loan(1, 100, 200, LoanPurpose::criminal_informal);
    module.active_loans().push_back(loan);

    // Run through grace period + 1 to trigger default.
    DeltaBuffer delta{};
    for (uint32_t i = 0; i <= BankingConfig{}.default_grace_ticks; ++i) {
        delta = DeltaBuffer{};
        module.execute(state, delta);
    }

    // Credit score should remain unchanged (criminal_informal skips credit impact).
    REQUIRE_THAT(module.borrower_credits()[0].profile.credit_score, WithinAbs(0.60f, 0.001f));

    // Loan should be in default.
    REQUIRE(module.active_loans()[0].in_default == true);

    // Should have consequence delta (violence escalation) and evidence delta.
    REQUIRE(delta.consequence_deltas.size() >= 1);
    REQUIRE(delta.evidence_deltas.size() >= 1);
}

// ===========================================================================
// Test 8: Compute interest rate — high credit score
// ===========================================================================

TEST_CASE("test_compute_interest_rate_high_credit", "[banking][tier5]") {
    // credit_score = 1.0: rate = 0.000027 + (1.0 - 1.0) * 0.000082 = 0.000027
    float rate = BankingModule::compute_interest_rate(
        1.0f, false, BankingConfig{}.per_tick_base_interest_rate,
        BankingConfig{}.credit_risk_spread, BankingConfig{}.collateral_rate_discount);
    REQUIRE_THAT(rate, WithinAbs(0.000027f, 0.000001f));
}

// ===========================================================================
// Test 9: Compute interest rate — low credit score
// ===========================================================================

TEST_CASE("test_compute_interest_rate_low_credit", "[banking][tier5]") {
    // credit_score = 0.0: rate = 0.000027 + 1.0 * 0.000082 = 0.000109
    float rate = BankingModule::compute_interest_rate(
        0.0f, false, BankingConfig{}.per_tick_base_interest_rate,
        BankingConfig{}.credit_risk_spread, BankingConfig{}.collateral_rate_discount);
    REQUIRE_THAT(rate, WithinAbs(0.000109f, 0.000001f));
}

// ===========================================================================
// Test 10: Compute max loan amount
// ===========================================================================

TEST_CASE("test_compute_max_loan_amount", "[banking][tier5]") {
    // revenue_per_tick = 100.0
    // max_loan = 100.0 * 365 * 36.0 / 12 = 109,500
    float max_loan =
        BankingModule::compute_max_loan_amount(100.0f, BankingConfig{}.max_loan_multiple_of_income);
    REQUIRE_THAT(max_loan, WithinAbs(109500.0f, 1.0f));
}

TEST_CASE("test_compute_max_loan_amount_zero_revenue", "[banking][tier5]") {
    float max_loan =
        BankingModule::compute_max_loan_amount(0.0f, BankingConfig{}.max_loan_multiple_of_income);
    REQUIRE_THAT(max_loan, WithinAbs(0.0f, 0.001f));
}

// ===========================================================================
// Test 11: Loan maturity retires paid loan
// ===========================================================================

TEST_CASE("test_loan_maturity_retires_paid_loan", "[banking][tier5]") {
    // Tick is at maturity and balance is zero: loan should be retired.
    auto state = make_test_world_state(500);

    BankingModule module;

    LoanRecord loan = make_test_loan(1, 100, 200);
    loan.outstanding_balance = 0.0f;  // fully paid
    loan.maturity_tick = 500;         // maturity reached
    module.active_loans().push_back(loan);

    // Add a second loan that is NOT matured.
    LoanRecord loan2 = make_test_loan(2, 100, 200);
    loan2.outstanding_balance = 5000.0f;
    loan2.maturity_tick = 1000;
    module.active_loans().push_back(loan2);

    // Add an NPC so repayment can be processed for loan2.
    NPC npc = make_test_npc(100, 5000.0f);
    state.significant_npcs.push_back(npc);

    BankingModule::BorrowerCredit bc{};
    bc.borrower_id = 100;
    bc.profile.credit_score = 0.5f;
    module.borrower_credits().push_back(bc);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Loan 1 should have been retired.
    REQUIRE(module.active_loans().size() == 1);
    REQUIRE(module.active_loans()[0].id == 2);
}

TEST_CASE("test_loan_not_retired_before_maturity", "[banking][tier5]") {
    auto state = make_test_world_state(400);  // before maturity_tick = 500

    BankingModule module;

    LoanRecord loan = make_test_loan(1, 100, 200);
    loan.outstanding_balance = 0.0f;
    loan.maturity_tick = 500;
    module.active_loans().push_back(loan);

    NPC npc = make_test_npc(100, 5000.0f);
    state.significant_npcs.push_back(npc);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Not yet at maturity — loan should remain.
    REQUIRE(module.active_loans().size() == 1);
}

TEST_CASE("test_loan_not_retired_with_outstanding_balance", "[banking][tier5]") {
    auto state = make_test_world_state(500);

    BankingModule module;

    LoanRecord loan = make_test_loan(1, 100, 200);
    loan.outstanding_balance = 1000.0f;  // still owed
    loan.maturity_tick = 500;
    module.active_loans().push_back(loan);

    NPC npc = make_test_npc(100, 5000.0f);
    state.significant_npcs.push_back(npc);

    BankingModule::BorrowerCredit bc{};
    bc.borrower_id = 100;
    bc.profile.credit_score = 0.5f;
    module.borrower_credits().push_back(bc);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Loan has outstanding balance — should NOT be retired.
    REQUIRE(module.active_loans().size() == 1);
}

// ===========================================================================
// Test 12: Credit score clamped to valid range [0.0, 1.0]
// ===========================================================================

TEST_CASE("test_credit_score_clamped_to_valid_range_upper", "[banking][tier5]") {
    auto state = make_test_world_state(10);
    NPC npc = make_test_npc(100, 5000.0f);
    state.significant_npcs.push_back(npc);

    BankingModule module;

    // Start with credit_score near maximum.
    BankingModule::BorrowerCredit bc{};
    bc.borrower_id = 100;
    bc.profile.credit_score = 0.999f;
    module.borrower_credits().push_back(bc);

    LoanRecord loan = make_test_loan(1, 100, 200);
    module.active_loans().push_back(loan);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Credit score should be clamped to 1.0 after payment gain.
    REQUIRE(module.borrower_credits()[0].profile.credit_score <= 1.0f);
}

TEST_CASE("test_credit_score_clamped_to_valid_range_lower", "[banking][tier5]") {
    auto state = make_test_world_state(10);
    NPC npc = make_test_npc(100, 0.0f);  // can't pay
    state.significant_npcs.push_back(npc);

    BankingModule module;

    // Start with credit_score near zero.
    BankingModule::BorrowerCredit bc{};
    bc.borrower_id = 100;
    bc.profile.credit_score = 0.005f;
    module.borrower_credits().push_back(bc);

    LoanRecord loan = make_test_loan(1, 100, 200);
    module.active_loans().push_back(loan);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Credit score should be clamped to 0.0 after miss penalty.
    REQUIRE(module.borrower_credits()[0].profile.credit_score >= 0.0f);
}

// ===========================================================================
// Test 13: Player loan repayment reduces wealth
// ===========================================================================

TEST_CASE("test_player_loan_repayment_reduces_wealth", "[banking][tier5]") {
    auto state = make_test_world_state(10);
    PlayerCharacter player = make_test_player(1, 10000.0f);
    state.player = std::make_unique<PlayerCharacter>(player);

    BankingModule module;

    // Player loan.
    LoanRecord loan = make_test_loan(1, 1, 200);  // borrower_id = player id = 1
    loan.repayment_per_tick = 50.0f;
    module.active_loans().push_back(loan);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Player wealth delta should be negative (repayment deducted).
    REQUIRE(delta.player_delta.wealth_delta.has_value());
    REQUIRE_THAT(delta.player_delta.wealth_delta.value(), WithinAbs(-50.0f, 0.01f));
}

// ===========================================================================
// Test 14: NPC loan repayment reduces capital
// ===========================================================================

TEST_CASE("test_npc_loan_repayment_reduces_capital", "[banking][tier5]") {
    auto state = make_test_world_state(10);
    NPC npc = make_test_npc(100, 5000.0f);
    state.significant_npcs.push_back(npc);

    BankingModule module;

    LoanRecord loan = make_test_loan(1, 100, 200);
    loan.repayment_per_tick = 30.0f;
    module.active_loans().push_back(loan);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Should have an NPC delta with negative capital_delta.
    bool found = false;
    for (const auto& nd : delta.npc_deltas) {
        if (nd.npc_id == 100 && nd.capital_delta.has_value()) {
            REQUIRE_THAT(nd.capital_delta.value(), WithinAbs(-30.0f, 0.01f));
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

// ===========================================================================
// Test 15: Partial payment when insufficient funds
// ===========================================================================

TEST_CASE("test_partial_payment_when_insufficient_funds", "[banking][tier5]") {
    auto state = make_test_world_state(10);

    // NPC has some money but less than repayment_per_tick.
    NPC npc = make_test_npc(100, 20.0f);
    state.significant_npcs.push_back(npc);

    BankingModule module;

    BankingModule::BorrowerCredit bc{};
    bc.borrower_id = 100;
    bc.profile.credit_score = 0.60f;
    bc.consecutive_misses = 0;
    module.borrower_credits().push_back(bc);

    LoanRecord loan = make_test_loan(1, 100, 200);
    loan.repayment_per_tick = 30.0f;  // NPC only has 20.0
    module.active_loans().push_back(loan);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // NPC cannot afford full payment: this is a missed payment.
    REQUIRE(module.borrower_credits()[0].consecutive_misses == 1);

    // Credit score should have decreased by miss penalty.
    REQUIRE_THAT(module.borrower_credits()[0].profile.credit_score, WithinAbs(0.59f, 0.001f));

    // Outstanding balance should remain unchanged.
    REQUIRE_THAT(module.active_loans()[0].outstanding_balance, WithinAbs(10000.0f, 0.01f));
}

// ===========================================================================
// Test 16: Multiple loans processed in id order
// ===========================================================================

TEST_CASE("test_multiple_loans_processed_in_id_order", "[banking][tier5]") {
    auto state = make_test_world_state(10);
    NPC npc = make_test_npc(100, 50000.0f);  // enough cash for both
    state.significant_npcs.push_back(npc);

    BankingModule module;

    // Add loans out of id order.
    LoanRecord loan3 = make_test_loan(3, 100, 200);
    loan3.repayment_per_tick = 25.0f;
    module.active_loans().push_back(loan3);

    LoanRecord loan1 = make_test_loan(1, 100, 200);
    loan1.repayment_per_tick = 30.0f;
    module.active_loans().push_back(loan1);

    LoanRecord loan2 = make_test_loan(2, 100, 200);
    loan2.repayment_per_tick = 20.0f;
    module.active_loans().push_back(loan2);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // After execution, loans should be sorted by id ascending.
    const auto& loans = module.active_loans();
    REQUIRE(loans.size() == 3);
    REQUIRE(loans[0].id == 1);
    REQUIRE(loans[1].id == 2);
    REQUIRE(loans[2].id == 3);

    // All three should have had their balances reduced.
    for (const auto& loan : loans) {
        REQUIRE(loan.outstanding_balance < 10000.0f);
    }
}

// ===========================================================================
// Test 17: Min credit score by purpose
// ===========================================================================

TEST_CASE("test_min_credit_score_by_purpose", "[banking][tier5]") {
    REQUIRE_THAT(BankingModule::min_credit_score_for_purpose(LoanPurpose::business_capital),
                 WithinAbs(0.35f, 0.001f));
    REQUIRE_THAT(BankingModule::min_credit_score_for_purpose(LoanPurpose::property_purchase),
                 WithinAbs(0.45f, 0.001f));
    REQUIRE_THAT(BankingModule::min_credit_score_for_purpose(LoanPurpose::personal),
                 WithinAbs(0.25f, 0.001f));
    REQUIRE_THAT(BankingModule::min_credit_score_for_purpose(LoanPurpose::criminal_informal),
                 WithinAbs(0.0f, 0.001f));
}

// ===========================================================================
// Test 18: Module interface properties
// ===========================================================================

TEST_CASE("test_module_interface_properties", "[banking][tier5]") {
    BankingModule module;

    REQUIRE(module.name() == "banking");
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
// Test 19: Sequential execution (not province parallel)
// ===========================================================================

TEST_CASE("test_sequential_execution", "[banking][tier5]") {
    BankingModule module;
    REQUIRE(module.is_province_parallel() == false);
}

// ===========================================================================
// Test 20: Secured loan default triggers collateral seizure
// ===========================================================================

TEST_CASE("test_secured_loan_default_triggers_collateral_seizure", "[banking][tier5]") {
    auto state = make_test_world_state(10);
    NPC npc = make_test_npc(100, 0.0f);
    state.significant_npcs.push_back(npc);

    BankingModule module;

    BankingModule::BorrowerCredit bc{};
    bc.borrower_id = 100;
    bc.profile.credit_score = 0.60f;
    module.borrower_credits().push_back(bc);

    // Secured loan with collateral.
    LoanRecord loan = make_test_loan(1, 100, 200, LoanPurpose::business_capital, 10000.0f, 10000.0f,
                                     0.000050f, 30.0f, 0, 500, 42);  // collateral_id = 42
    module.active_loans().push_back(loan);

    DeltaBuffer delta{};

    // Run through grace period + 1.
    for (uint32_t i = 0; i <= BankingConfig{}.default_grace_ticks; ++i) {
        delta = DeltaBuffer{};
        module.execute(state, delta);
    }

    // Should be in default.
    REQUIRE(module.active_loans()[0].in_default == true);

    // Should have consequence delta for collateral seizure.
    REQUIRE(delta.consequence_deltas.size() >= 1);
}

// ===========================================================================
// Test 21: Unsecured standard loan default — no consequence delta
// ===========================================================================

TEST_CASE("test_unsecured_standard_loan_default_no_consequence", "[banking][tier5]") {
    auto state = make_test_world_state(10);
    NPC npc = make_test_npc(100, 0.0f);
    state.significant_npcs.push_back(npc);

    BankingModule module;

    BankingModule::BorrowerCredit bc{};
    bc.borrower_id = 100;
    bc.profile.credit_score = 0.60f;
    module.borrower_credits().push_back(bc);

    // Unsecured standard loan (collateral_id = 0).
    LoanRecord loan = make_test_loan(1, 100, 200, LoanPurpose::personal);
    module.active_loans().push_back(loan);

    DeltaBuffer delta{};

    // Run through grace period + 1.
    for (uint32_t i = 0; i <= BankingConfig{}.default_grace_ticks; ++i) {
        delta = DeltaBuffer{};
        module.execute(state, delta);
    }

    // Should be in default.
    REQUIRE(module.active_loans()[0].in_default == true);

    // No consequence deltas for unsecured standard loan (credit already penalized).
    REQUIRE(delta.consequence_deltas.empty());
}

// ===========================================================================
// Test 22: Compute repayment per tick
// ===========================================================================

TEST_CASE("test_compute_repayment_per_tick", "[banking][tier5]") {
    // principal = 10,000, rate = 0.00005/tick, duration = 365 ticks
    // total_cost = 10,000 * (1 + 0.00005 * 365) = 10,000 * 1.01825 = 10,182.5
    // repayment = 10,182.5 / 365 = ~27.89
    float repayment = BankingModule::compute_repayment_per_tick(10000.0f, 0.00005f, 365);
    REQUIRE_THAT(repayment, WithinAbs(27.89f, 0.1f));
}

TEST_CASE("test_compute_repayment_per_tick_zero_interest", "[banking][tier5]") {
    // No interest: repayment = principal / duration.
    float repayment = BankingModule::compute_repayment_per_tick(10000.0f, 0.0f, 100);
    REQUIRE_THAT(repayment, WithinAbs(100.0f, 0.01f));
}

TEST_CASE("test_compute_repayment_per_tick_zero_duration", "[banking][tier5]") {
    // Zero duration: immediate repayment of principal.
    float repayment = BankingModule::compute_repayment_per_tick(10000.0f, 0.00005f, 0);
    REQUIRE_THAT(repayment, WithinAbs(10000.0f, 0.01f));
}

// ===========================================================================
// Test 23: Defaulted loan not re-processed for repayment
// ===========================================================================

TEST_CASE("test_defaulted_loan_not_reprocessed", "[banking][tier5]") {
    auto state = make_test_world_state(10);
    NPC npc = make_test_npc(100, 50000.0f);  // plenty of cash
    state.significant_npcs.push_back(npc);

    BankingModule module;

    BankingModule::BorrowerCredit bc{};
    bc.borrower_id = 100;
    bc.profile.credit_score = 0.50f;
    module.borrower_credits().push_back(bc);

    LoanRecord loan = make_test_loan(1, 100, 200);
    loan.in_default = true;  // already in default
    module.active_loans().push_back(loan);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Balance should not have changed (loan skipped during repayment processing).
    REQUIRE_THAT(module.active_loans()[0].outstanding_balance, WithinAbs(10000.0f, 0.01f));

    // No NPC deltas should have been generated for this loan.
    REQUIRE(delta.npc_deltas.empty());
}

// ===========================================================================
// Test 24: Borrower credit auto-created on first loan processing
// ===========================================================================

TEST_CASE("test_borrower_credit_auto_created", "[banking][tier5]") {
    auto state = make_test_world_state(10);
    NPC npc = make_test_npc(100, 5000.0f);
    state.significant_npcs.push_back(npc);

    BankingModule module;

    // No pre-existing borrower credit record.
    REQUIRE(module.borrower_credits().empty());

    LoanRecord loan = make_test_loan(1, 100, 200);
    module.active_loans().push_back(loan);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // A borrower credit record should have been created.
    REQUIRE(module.borrower_credits().size() == 1);
    REQUIRE(module.borrower_credits()[0].borrower_id == 100);

    // Default starting score is 0.5, plus payment gain of 0.002.
    REQUIRE_THAT(module.borrower_credits()[0].profile.credit_score, WithinAbs(0.502f, 0.001f));
}

// ===========================================================================
// Test 25: Successful payment resets consecutive misses
// ===========================================================================

TEST_CASE("test_successful_payment_resets_consecutive_misses", "[banking][tier5]") {
    auto state = make_test_world_state(10);
    NPC npc = make_test_npc(100, 5000.0f);
    state.significant_npcs.push_back(npc);

    BankingModule module;

    BankingModule::BorrowerCredit bc{};
    bc.borrower_id = 100;
    bc.profile.credit_score = 0.50f;
    bc.consecutive_misses = 2;  // already had 2 misses
    module.borrower_credits().push_back(bc);

    LoanRecord loan = make_test_loan(1, 100, 200);
    module.active_loans().push_back(loan);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // After successful payment, consecutive misses should reset to 0.
    REQUIRE(module.borrower_credits()[0].consecutive_misses == 0);
}

// ===========================================================================
// Test 26: Constants verification
// ===========================================================================

TEST_CASE("test_banking_constants", "[banking][tier5]") {
    REQUIRE_THAT(BankingConfig{}.per_tick_base_interest_rate, WithinAbs(0.000027f, 0.000001f));
    REQUIRE_THAT(BankingConfig{}.credit_risk_spread, WithinAbs(0.000082f, 0.000001f));
    REQUIRE_THAT(BankingConfig{}.collateral_rate_discount, WithinAbs(0.000014f, 0.000001f));
    REQUIRE(BankingConfig{}.default_grace_ticks == 3);
    REQUIRE_THAT(BankingConfig{}.credit_score_payment_gain, WithinAbs(0.002f, 0.0001f));
    REQUIRE_THAT(BankingConfig{}.credit_score_miss_penalty, WithinAbs(0.01f, 0.0001f));
    REQUIRE_THAT(BankingConfig{}.per_tick_denial_dti_threshold, WithinAbs(0.40f, 0.001f));
    REQUIRE_THAT(BankingConfig{}.max_loan_multiple_of_income, WithinAbs(36.0f, 0.1f));
    REQUIRE_THAT(BankingConfig{}.criminal_conviction_penalty, WithinAbs(0.20f, 0.001f));
}

// ===========================================================================
// Test 27: Multiple borrowers processed independently
// ===========================================================================

TEST_CASE("test_multiple_borrowers_processed_independently", "[banking][tier5]") {
    auto state = make_test_world_state(10);

    NPC npc1 = make_test_npc(100, 5000.0f);
    NPC npc2 = make_test_npc(200, 0.0f);  // can't pay
    state.significant_npcs.push_back(npc1);
    state.significant_npcs.push_back(npc2);

    BankingModule module;

    BankingModule::BorrowerCredit bc1{};
    bc1.borrower_id = 100;
    bc1.profile.credit_score = 0.50f;
    module.borrower_credits().push_back(bc1);

    BankingModule::BorrowerCredit bc2{};
    bc2.borrower_id = 200;
    bc2.profile.credit_score = 0.50f;
    module.borrower_credits().push_back(bc2);

    LoanRecord loan1 = make_test_loan(1, 100, 300);
    LoanRecord loan2 = make_test_loan(2, 200, 300);
    module.active_loans().push_back(loan1);
    module.active_loans().push_back(loan2);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Borrower 100 made payment: credit improved.
    REQUIRE_THAT(module.borrower_credits()[0].profile.credit_score, WithinAbs(0.502f, 0.001f));
    REQUIRE(module.borrower_credits()[0].consecutive_misses == 0);

    // Borrower 200 missed payment: credit decreased.
    REQUIRE_THAT(module.borrower_credits()[1].profile.credit_score, WithinAbs(0.49f, 0.001f));
    REQUIRE(module.borrower_credits()[1].consecutive_misses == 1);
}

// ===========================================================================
// Test 28: Fully repaid loan (zero balance) is skipped for repayment
// ===========================================================================

TEST_CASE("test_zero_balance_loan_skipped", "[banking][tier5]") {
    auto state = make_test_world_state(10);
    NPC npc = make_test_npc(100, 5000.0f);
    state.significant_npcs.push_back(npc);

    BankingModule module;

    BankingModule::BorrowerCredit bc{};
    bc.borrower_id = 100;
    bc.profile.credit_score = 0.50f;
    module.borrower_credits().push_back(bc);

    LoanRecord loan = make_test_loan(1, 100, 200);
    loan.outstanding_balance = 0.0f;  // already fully paid
    module.active_loans().push_back(loan);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // No capital deduction should occur.
    REQUIRE(delta.npc_deltas.empty());

    // Credit score unchanged.
    REQUIRE_THAT(module.borrower_credits()[0].profile.credit_score, WithinAbs(0.50f, 0.001f));
}

// ===========================================================================
// Test 29: Interest rate computation with collateral at max credit
// ===========================================================================

TEST_CASE("test_interest_rate_with_collateral_at_max_credit", "[banking][tier5]") {
    // credit_score = 1.0, collateral:
    // rate = 0.000027 + (1.0 - 1.0) * 0.000082 - 0.000014 = 0.000013
    float rate = BankingModule::compute_interest_rate(
        1.0f, true, BankingConfig{}.per_tick_base_interest_rate, BankingConfig{}.credit_risk_spread,
        BankingConfig{}.collateral_rate_discount);
    REQUIRE_THAT(rate, WithinAbs(0.000013f, 0.000001f));
}

TEST_CASE("test_interest_rate_never_negative", "[banking][tier5]") {
    // Even with perfect credit and collateral, rate should not go below 0.
    float rate = BankingModule::compute_interest_rate(
        1.0f, true, BankingConfig{}.per_tick_base_interest_rate, BankingConfig{}.credit_risk_spread,
        BankingConfig{}.collateral_rate_discount);
    REQUIRE(rate >= 0.0f);

    // Also test without collateral.
    rate = BankingModule::compute_interest_rate(
        1.0f, false, BankingConfig{}.per_tick_base_interest_rate,
        BankingConfig{}.credit_risk_spread, BankingConfig{}.collateral_rate_discount);
    REQUIRE(rate >= 0.0f);

    // And at zero credit.
    rate = BankingModule::compute_interest_rate(
        0.0f, true, BankingConfig{}.per_tick_base_interest_rate, BankingConfig{}.credit_risk_spread,
        BankingConfig{}.collateral_rate_discount);
    REQUIRE(rate >= 0.0f);
}

// ===========================================================================
// Test 30: Empty loan list produces no deltas
// ===========================================================================

TEST_CASE("test_empty_loan_list_no_deltas", "[banking][tier5]") {
    auto state = make_test_world_state(10);

    BankingModule module;
    // No loans, no borrower credits.

    DeltaBuffer delta{};
    module.execute(state, delta);

    REQUIRE(delta.npc_deltas.empty());
    REQUIRE_FALSE(delta.player_delta.wealth_delta.has_value());
    REQUIRE(delta.consequence_deltas.empty());
    REQUIRE(delta.evidence_deltas.empty());
}

// ===========================================================================
// Test 31: Derived credit fields updated after execution
// ===========================================================================

TEST_CASE("test_derived_credit_fields_updated", "[banking][tier5]") {
    auto state = make_test_world_state(10);
    NPC npc = make_test_npc(100, 50000.0f);
    state.significant_npcs.push_back(npc);

    BankingModule module;

    BankingModule::BorrowerCredit bc{};
    bc.borrower_id = 100;
    bc.profile.credit_score = 0.50f;
    bc.profile.total_debt_outstanding = 0.0f;
    bc.profile.debt_service_per_tick = 0.0f;
    module.borrower_credits().push_back(bc);

    LoanRecord loan1 = make_test_loan(1, 100, 200);
    loan1.outstanding_balance = 5000.0f;
    loan1.repayment_per_tick = 20.0f;
    module.active_loans().push_back(loan1);

    LoanRecord loan2 = make_test_loan(2, 100, 200);
    loan2.outstanding_balance = 3000.0f;
    loan2.repayment_per_tick = 15.0f;
    module.active_loans().push_back(loan2);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // After execution, derived fields should reflect remaining loans.
    const auto& credit = module.borrower_credits()[0];

    // Both loans had payments processed, so balances decreased.
    // debt_service = 20 + 15 = 35
    REQUIRE_THAT(credit.profile.debt_service_per_tick, WithinAbs(35.0f, 0.1f));

    // total_debt should be sum of remaining outstanding balances.
    REQUIRE(credit.profile.total_debt_outstanding > 0.0f);
    REQUIRE(credit.profile.total_debt_outstanding < 8000.0f);
}

// ===========================================================================
// Test 32: Player loan with insufficient wealth triggers miss
// ===========================================================================

TEST_CASE("test_player_insufficient_wealth_triggers_miss", "[banking][tier5]") {
    auto state = make_test_world_state(10);
    PlayerCharacter player = make_test_player(1, 10.0f);  // low wealth
    state.player = std::make_unique<PlayerCharacter>(player);

    BankingModule module;

    BankingModule::BorrowerCredit bc{};
    bc.borrower_id = 1;
    bc.profile.credit_score = 0.60f;
    module.borrower_credits().push_back(bc);

    LoanRecord loan = make_test_loan(1, 1, 200);
    loan.repayment_per_tick = 50.0f;  // player can't afford
    module.active_loans().push_back(loan);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Should be a missed payment.
    REQUIRE(module.borrower_credits()[0].consecutive_misses == 1);
    REQUIRE_THAT(module.borrower_credits()[0].profile.credit_score, WithinAbs(0.59f, 0.001f));

    // No wealth delta should have been applied.
    REQUIRE_FALSE(delta.player_delta.wealth_delta.has_value());
}
