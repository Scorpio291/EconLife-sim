#pragma once

// Banking Module — sequential tick module that processes loan repayments,
// adjusts credit scores, handles loan defaults, and manages interest rates.
//
// See docs/interfaces/banking/INTERFACE.md for the canonical specification.

#include <cstdint>
#include <string_view>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "modules/banking/banking_types.h"

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;

// ---------------------------------------------------------------------------
// BankingModule — ITickModule implementation for loan and credit processing
// ---------------------------------------------------------------------------
class BankingModule : public ITickModule {
   public:
    explicit BankingModule(const BankingConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "banking"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    bool is_province_parallel() const noexcept override { return false; }

    std::vector<std::string_view> runs_after() const override { return {"financial_distribution"}; }

    // End of Pass 1 chain — nothing runs after banking.
    std::vector<std::string_view> runs_before() const override { return {}; }

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Internal loan storage ---
    // WorldState doesn't carry active_loans in V1 bootstrap, so the module
    // owns its own loan vector. Sorted by id ascending for determinism.
    std::vector<LoanRecord>& active_loans() { return active_loans_; }
    const std::vector<LoanRecord>& active_loans() const { return active_loans_; }

    // --- Credit profile storage ---
    struct BorrowerCredit {
        uint32_t borrower_id;
        CreditProfile profile;
        uint32_t consecutive_misses = 0;
    };
    std::vector<BorrowerCredit>& borrower_credits() { return borrower_credits_; }
    const std::vector<BorrowerCredit>& borrower_credits() const { return borrower_credits_; }

    // --- Static utility functions (exposed for testing) ---

    // Compute per-tick interest rate given credit score and collateral status.
    // rate = base_interest_rate + (1.0 - credit_score) * credit_risk_spread
    //        - (has_collateral ? collateral_rate_discount : 0)
    // Result clamped to >= 0.0.
    static float compute_interest_rate(float credit_score, bool has_collateral, float base_rate,
                                       float risk_spread, float collateral_discount);

    // Compute maximum loan amount from per-tick revenue.
    // max_loan = revenue_per_tick * 365 * max_loan_multiple_of_income / 12
    static float compute_max_loan_amount(float revenue_per_tick, float max_loan_multiple_of_income);

    // Evaluate a loan application: returns true if approved.
    // Deny if credit_score < min_credit_score_for_purpose(purpose)
    // Deny if dti_ratio > denial_dti_threshold
    static bool evaluate_loan_application(float credit_score, float dti_ratio, LoanPurpose purpose,
                                          float denial_dti_threshold);

    // Compute fixed repayment per tick for an amortizing loan.
    // Simple amortization: (principal * (1 + interest_rate * duration_ticks)) / duration_ticks
    // Returns a positive per-tick payment amount.
    static float compute_repayment_per_tick(float principal, float interest_rate,
                                            uint32_t duration_ticks);

    // Return the minimum credit score required for a given loan purpose.
    static float min_credit_score_for_purpose(LoanPurpose purpose);

   private:
    std::vector<LoanRecord> active_loans_;
    std::vector<BorrowerCredit> borrower_credits_;
    uint32_t next_loan_id_ = 1;

    BankingConfig cfg_;

    // Find or create a BorrowerCredit entry for the given borrower.
    BorrowerCredit* find_borrower_credit(uint32_t borrower_id);

    // Evaluate NPC businesses for new loan applications (quarterly, every 90 ticks).
    void process_loan_origination(const WorldState& state, DeltaBuffer& delta);

    // Process a single loan repayment for one tick.
    void process_loan_repayment(LoanRecord& loan, const WorldState& state, DeltaBuffer& delta);

    // Handle a loan that has entered default status.
    void process_loan_default(LoanRecord& loan, DeltaBuffer& delta);

    // Remove loans that have reached maturity with zero balance.
    void retire_matured_loans(uint32_t current_tick);

    // Recompute derived credit fields (total_debt, debt_service, dti) for a borrower.
    void update_derived_credit_fields(BorrowerCredit& credit, float revenue_per_tick);
};

}  // namespace econlife
