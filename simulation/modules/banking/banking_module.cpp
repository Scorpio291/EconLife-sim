// Banking Module — implementation.
// See banking_module.h for class declarations and
// docs/interfaces/banking/INTERFACE.md for the canonical specification.
//
// Per-tick processing (sequential — loans cross province boundaries):
//   1. Process loan repayments for all active loans (sorted by loan id ascending).
//   2. Handle defaults: secured collateral seizure, criminal violence escalation.
//   3. Retire matured loans with zero outstanding balance.
//   4. Update derived credit fields for all borrowers.

#include "modules/banking/banking_module.h"

#include <algorithm>
#include <cmath>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"  // PlayerCharacter complete type
#include "core/world_state/world_state.h"

namespace econlife {

// ===========================================================================
// Static utility functions
// ===========================================================================

float BankingModule::compute_interest_rate(float credit_score, bool has_collateral, float base_rate,
                                           float risk_spread, float collateral_discount) {
    float rate = base_rate + (1.0f - credit_score) * risk_spread -
                 (has_collateral ? collateral_discount : 0.0f);
    if (rate < 0.0f) {
        rate = 0.0f;
    }
    return rate;
}

float BankingModule::compute_max_loan_amount(float revenue_per_tick,
                                             float max_loan_multiple_of_income) {
    return revenue_per_tick * 365.0f * max_loan_multiple_of_income / 12.0f;
}

bool BankingModule::evaluate_loan_application(float credit_score, float dti_ratio,
                                              LoanPurpose purpose, float denial_dti_threshold) {
    if (credit_score < min_credit_score_for_purpose(purpose)) {
        return false;
    }
    if (dti_ratio > denial_dti_threshold) {
        return false;
    }
    return true;
}

float BankingModule::compute_repayment_per_tick(float principal, float interest_rate,
                                                uint32_t duration_ticks) {
    if (duration_ticks == 0) {
        return principal;  // immediate repayment
    }

    // Simple amortization: total cost / duration.
    // total_cost = principal * (1 + interest_rate * duration_ticks)
    float total_cost = principal * (1.0f + interest_rate * static_cast<float>(duration_ticks));
    return total_cost / static_cast<float>(duration_ticks);
}

float BankingModule::min_credit_score_for_purpose(LoanPurpose purpose) {
    switch (purpose) {
        case LoanPurpose::business_capital:
            return 0.35f;
        case LoanPurpose::property_purchase:
            return 0.45f;
        case LoanPurpose::personal:
            return 0.25f;
        case LoanPurpose::criminal_informal:
            return 0.0f;  // no credit check
        default:
            return 1.0f;  // deny unknown purposes
    }
}

// ===========================================================================
// Private helpers
// ===========================================================================

BankingModule::BorrowerCredit* BankingModule::find_borrower_credit(uint32_t borrower_id) {
    for (auto& bc : borrower_credits_) {
        if (bc.borrower_id == borrower_id) {
            return &bc;
        }
    }
    return nullptr;
}

void BankingModule::process_loan_repayment(LoanRecord& loan, const WorldState& state,
                                           DeltaBuffer& delta) {
    // Skip defaulted or fully repaid loans.
    if (loan.in_default || loan.outstanding_balance <= 0.0f) {
        return;
    }

    // Determine borrower's available cash.
    float borrower_cash = 0.0f;
    bool is_player = false;

    if (state.player != nullptr && loan.borrower_id == state.player->id) {
        borrower_cash = state.player->wealth;
        is_player = true;
    } else {
        // Look up NPC capital in significant_npcs.
        for (const auto& npc : state.significant_npcs) {
            if (npc.id == loan.borrower_id) {
                borrower_cash = npc.capital;
                break;
            }
        }
    }

    // Find or create borrower credit record.
    BorrowerCredit* credit = find_borrower_credit(loan.borrower_id);
    if (credit == nullptr) {
        BorrowerCredit new_credit{};
        new_credit.borrower_id = loan.borrower_id;
        new_credit.profile.credit_score = 0.5f;  // default starting score
        new_credit.profile.total_debt_outstanding = 0.0f;
        new_credit.profile.debt_service_per_tick = 0.0f;
        new_credit.profile.debt_to_income_ratio = 0.0f;
        new_credit.consecutive_misses = 0;
        borrower_credits_.push_back(new_credit);
        credit = &borrower_credits_.back();
    }

    if (borrower_cash >= loan.repayment_per_tick) {
        // --- Successful payment ---
        float interest = loan.outstanding_balance * loan.interest_rate;
        float principal_payment = loan.repayment_per_tick - interest;

        // Guard against negative principal payment (interest exceeds payment).
        if (principal_payment < 0.0f) {
            principal_payment = 0.0f;
        }

        // Reduce outstanding balance.
        loan.outstanding_balance -= principal_payment;
        if (loan.outstanding_balance < 0.0f) {
            loan.outstanding_balance = 0.0f;
        }

        // Apply wealth/capital deduction via delta.
        if (is_player) {
            if (delta.player_delta.wealth_delta.has_value()) {
                delta.player_delta.wealth_delta.value() -= loan.repayment_per_tick;
            } else {
                delta.player_delta.wealth_delta = -loan.repayment_per_tick;
            }
        } else {
            NPCDelta npc_delta{};
            npc_delta.npc_id = loan.borrower_id;
            npc_delta.capital_delta = -loan.repayment_per_tick;
            delta.npc_deltas.push_back(npc_delta);
        }

        // Improve credit score (not for criminal_informal loans).
        if (loan.purpose != LoanPurpose::criminal_informal) {
            credit->profile.credit_score += cfg_.credit_score_payment_gain;
            if (credit->profile.credit_score > 1.0f) {
                credit->profile.credit_score = 1.0f;
            }
        }

        // Reset consecutive misses on successful payment.
        credit->consecutive_misses = 0;

    } else {
        // --- Missed payment ---
        credit->consecutive_misses += 1;

        // Penalize credit score (not for criminal_informal loans).
        if (loan.purpose != LoanPurpose::criminal_informal) {
            credit->profile.credit_score -= cfg_.credit_score_miss_penalty;
            if (credit->profile.credit_score < 0.0f) {
                credit->profile.credit_score = 0.0f;
            }
        }

        // Check for default after grace period.
        if (credit->consecutive_misses > cfg_.default_grace_ticks) {
            loan.in_default = true;
            process_loan_default(loan, delta);
        }
    }
}

void BankingModule::process_loan_default(LoanRecord& loan, DeltaBuffer& delta) {
    if (loan.purpose == LoanPurpose::criminal_informal) {
        // Queue violence escalation consequence.
        ConsequenceDelta consequence{};
        consequence.new_entry_id = loan.id;
        delta.consequence_deltas.push_back(consequence);

        // Also queue evidence for criminal enforcement action.
        EvidenceDelta evidence{};
        EvidenceToken token{};
        token.id = loan.id + 10000;  // offset to avoid id collision
        token.type = EvidenceType::financial;
        token.source_npc_id = loan.lender_id;
        token.target_npc_id = loan.borrower_id;
        token.actionability = 0.5f;
        token.decay_rate = 0.001f;
        token.created_tick = 0;  // will be set by application layer
        token.province_id = 0;
        token.is_active = true;
        evidence.new_token = token;
        delta.evidence_deltas.push_back(evidence);

    } else if (loan.collateral_id != 0) {
        // Secured loan: queue collateral seizure consequence.
        ConsequenceDelta consequence{};
        consequence.new_entry_id = loan.id;
        delta.consequence_deltas.push_back(consequence);
    }
    // Standard unsecured loans: credit score already penalized; no further action.
}

void BankingModule::retire_matured_loans(uint32_t current_tick) {
    active_loans_.erase(std::remove_if(active_loans_.begin(), active_loans_.end(),
                                       [current_tick](const LoanRecord& loan) {
                                           return current_tick >= loan.maturity_tick &&
                                                  loan.outstanding_balance <= 0.0f;
                                       }),
                        active_loans_.end());
}

void BankingModule::update_derived_credit_fields(BorrowerCredit& credit, float revenue_per_tick) {
    float total_debt = 0.0f;
    float debt_service = 0.0f;

    for (const auto& loan : active_loans_) {
        if (loan.borrower_id == credit.borrower_id && !loan.in_default) {
            total_debt += loan.outstanding_balance;
            debt_service += loan.repayment_per_tick;
        }
    }

    credit.profile.total_debt_outstanding = total_debt;
    credit.profile.debt_service_per_tick = debt_service;

    // DTI = total_debt / annual_revenue (revenue_per_tick * 365)
    float annual_revenue = revenue_per_tick * 365.0f;
    credit.profile.debt_to_income_ratio =
        (total_debt > 0.0f && annual_revenue > 0.0f) ? total_debt / annual_revenue : 0.0f;
}

// ===========================================================================
// BankingModule — loan origination (quarterly)
// ===========================================================================

void BankingModule::process_loan_origination(const WorldState& state, DeltaBuffer& delta) {
    // Only run quarterly.
    if (state.current_tick % 90 != 0) {
        return;
    }

    // Iterate npc_businesses sorted by id ascending for deterministic processing.
    std::vector<const NPCBusiness*> sorted_businesses;
    sorted_businesses.reserve(state.npc_businesses.size());
    for (const auto& biz : state.npc_businesses) {
        sorted_businesses.push_back(&biz);
    }
    std::sort(sorted_businesses.begin(), sorted_businesses.end(),
              [](const NPCBusiness* a, const NPCBusiness* b) { return a->id < b->id; });

    for (const NPCBusiness* biz : sorted_businesses) {
        // Only consider businesses with revenue that need capital.
        if (biz->revenue_per_tick <= 0.0f) {
            continue;
        }
        if (biz->cash >= biz->revenue_per_tick * 30.0f) {
            continue;
        }

        // Find or create BorrowerCredit for the business owner.
        BorrowerCredit* credit = find_borrower_credit(biz->owner_id);
        if (credit == nullptr) {
            BorrowerCredit new_credit{};
            new_credit.borrower_id = biz->owner_id;
            new_credit.profile.credit_score = 0.5f;
            new_credit.profile.total_debt_outstanding = 0.0f;
            new_credit.profile.debt_service_per_tick = 0.0f;
            new_credit.profile.debt_to_income_ratio = 0.0f;
            new_credit.consecutive_misses = 0;
            borrower_credits_.push_back(new_credit);
            credit = &borrower_credits_.back();
        }

        // Compute DTI: debt_service_per_tick / max(1.0, revenue_per_tick)
        float dti = credit->profile.debt_service_per_tick / std::max(1.0f, biz->revenue_per_tick);

        if (!evaluate_loan_application(credit->profile.credit_score, dti,
                                       LoanPurpose::business_capital,
                                       cfg_.per_tick_denial_dti_threshold)) {
            continue;
        }

        // Compute loan terms.
        float loan_amount = std::min(
            compute_max_loan_amount(biz->revenue_per_tick, cfg_.max_loan_multiple_of_income),
            biz->revenue_per_tick * 180.0f);
        float interest_rate = compute_interest_rate(
            credit->profile.credit_score, false, cfg_.per_tick_base_interest_rate,
            cfg_.credit_risk_spread, cfg_.collateral_rate_discount);
        constexpr uint32_t duration_ticks = 365;
        float repayment = compute_repayment_per_tick(loan_amount, interest_rate, duration_ticks);

        // Create the loan record.
        LoanRecord loan{};
        loan.id = next_loan_id_++;
        loan.borrower_id = biz->owner_id;
        loan.lender_id = 0;  // institutional lender (no NPC bank entity in V1)
        loan.purpose = LoanPurpose::business_capital;
        loan.principal = loan_amount;
        loan.outstanding_balance = loan_amount;
        loan.interest_rate = interest_rate;
        loan.repayment_per_tick = repayment;
        loan.originated_tick = state.current_tick;
        loan.maturity_tick = state.current_tick + duration_ticks;
        loan.in_default = false;
        loan.collateral_id = 0;

        active_loans_.push_back(loan);

        // Credit the loan proceeds to the business owner's capital.
        NPCDelta npc_delta{};
        npc_delta.npc_id = biz->owner_id;
        npc_delta.capital_delta = loan_amount;
        delta.npc_deltas.push_back(npc_delta);

        // Also credit the business cash directly.
        BusinessDelta biz_delta{};
        biz_delta.business_id = biz->id;
        biz_delta.cash_delta = loan_amount;
        delta.business_deltas.push_back(biz_delta);
    }
}

// ===========================================================================
// BankingModule — tick execution (sequential)
// ===========================================================================

void BankingModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Step 0: Evaluate NPC businesses for new loan applications (quarterly).
    process_loan_origination(state, delta);

    // Ensure loans are sorted by id ascending for deterministic processing.
    std::sort(active_loans_.begin(), active_loans_.end(),
              [](const LoanRecord& a, const LoanRecord& b) { return a.id < b.id; });

    // Step 1: Process loan repayments for all active loans.
    for (auto& loan : active_loans_) {
        process_loan_repayment(loan, state, delta);
    }

    // Step 2: Retire matured loans.
    retire_matured_loans(state.current_tick);

    // Step 3: Update derived credit fields for all borrowers.
    // Build a lookup of revenue_per_tick per owner for DTI computation.
    for (auto& credit : borrower_credits_) {
        float revenue_per_tick = 0.0f;
        for (const auto& biz : state.npc_businesses) {
            if (biz.owner_id == credit.borrower_id) {
                revenue_per_tick += biz.revenue_per_tick;
            }
        }
        update_derived_credit_fields(credit, revenue_per_tick);
    }
}

}  // namespace econlife
