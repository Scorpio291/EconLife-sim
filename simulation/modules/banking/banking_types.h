#pragma once

#include <cstdint>
#include <vector>

namespace econlife {

// =============================================================================
// Banking and Loan Types — TDD §34
// =============================================================================

// --- §34.1 — CreditProfile ---

struct CreditProfile {
    float credit_score;             // 0.0-1.0; 1.0 = excellent credit; 0.0 = no credit history
                                    // Initialised at character/business creation from:
                                    //   player: Background (Born Poor -> 0.2, Working Class -> 0.4,
                                    //           Middle Class -> 0.6, Wealthy -> 0.8)
                                    //   NPCBusiness: age * 0.05 + revenue_stability * 0.3 (clamped 0-1)
                                    // Decays 0.01/tick on missed loan payment.
                                    // Increases 0.002/tick when all loans current.
                                    // Criminal conviction: -0.20 immediate.

    float total_debt_outstanding;   // sum of LoanRecord.outstanding_balance; derived, not authoritative
    float debt_service_per_tick;    // sum of current loan repayment obligations per tick; derived
    float debt_to_income_ratio;     // total_debt_outstanding / (revenue_per_tick * TICKS_PER_YEAR);
                                    // > config.banking.denial_dti_threshold -> loan denied

    // Invariants:
    //   credit_score in [0.0, 1.0]
    //   total_debt_outstanding >= 0.0
    //   debt_service_per_tick >= 0.0
    //   debt_to_income_ratio >= 0.0

    // FIELD ADDITION to PlayerCharacter:
    //   CreditProfile credit_profile;
    //   std::vector<uint32_t> loan_ids;    // active LoanRecord ids
    //
    // FIELD ADDITION to NPCBusiness:
    //   CreditProfile credit_profile;
    //   std::vector<uint32_t> loan_ids;
};

// --- §34.2 — LoanPurpose ---

enum class LoanPurpose : uint8_t {
    business_capital    = 0,  // Working capital or expansion; secured against NPCBusiness revenue
    property_purchase   = 1,  // Mortgage; secured against PropertyListing.market_value
    personal            = 2,  // Unsecured; higher rate; lower maximum
    criminal_informal   = 3,  // Loan from a criminal organization or loan shark; no formal record;
                               // not reflected in CreditProfile; enforced via ObligationNode
                               // (FavorType::financial_loan); violence escalation on default
};

// --- §34.2 — LoanRecord ---

struct LoanRecord {
    uint32_t  id;
    uint32_t  borrower_id;          // player_id or npc_id
    uint32_t  lender_id;            // NPCBusiness (bank) id; or npc_id for informal loans
    LoanPurpose purpose;
    float     principal;            // original loan amount
    float     outstanding_balance;  // remaining; decremented by repayments
    float     interest_rate;        // per tick; annualised = rate * TICKS_PER_YEAR
    float     repayment_per_tick;   // fixed; debited from borrower cash each tick
    uint32_t  originated_tick;
    uint32_t  maturity_tick;        // loan fully repaid by this tick under scheduled repayments
    bool      in_default;           // true if missed > config.banking.default_grace_ticks payments
    uint32_t  collateral_id;        // PropertyListing.id or NPCBusiness.id pledged; 0 if unsecured

    // Invariants:
    //   principal > 0.0
    //   outstanding_balance >= 0.0
    //   outstanding_balance <= principal (at origination; interest may cause overshoot)
    //   interest_rate >= 0.0
    //   repayment_per_tick > 0.0
    //   maturity_tick > originated_tick
    //
    // Repayment tick integration (§34.2):
    //   Each tick, for each active LoanRecord where borrower_id == player_id:
    //     If player.wealth >= repayment_per_tick: payment succeeds, credit_score improves
    //     Else: in_default = true, credit_score decays
    //
    // WorldState field: std::vector<LoanRecord> active_loans
    //   (all outstanding loans; partitioned by borrower_id at query time)
};

}  // namespace econlife
