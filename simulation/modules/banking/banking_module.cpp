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
#include <cstring>

#include "core/world_state/apply_deltas.h"  // lookup_npc_by_id
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
    } else if (const NPC* npc = lookup_npc_by_id(state, loan.borrower_id)) {
        borrower_cash = npc->capital;
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

        // Phase 5 — property_purchase loans additionally trigger
        // foreclosure: real_estate transfers ownership of the
        // collateral PropertyListing to the lender next tick. The
        // loan itself is wound down here (outstanding_balance = 0)
        // so retire_matured_loans removes it at end of next pass.
        if (loan.purpose == LoanPurpose::property_purchase) {
            PropertyForeclosureRequest fc{};
            fc.loan_id = loan.id;
            fc.property_id = loan.collateral_id;
            fc.borrower_id = loan.borrower_id;
            fc.lender_id = loan.lender_id;
            delta.new_property_foreclosures.push_back(fc);
            loan.outstanding_balance = 0.0f;
            loan.maturity_tick = loan.originated_tick;  // make retire pick it up
        }
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
    // Phase 4 — drain pending_loan_requests emitted at settlement by
    // real_estate (mortgage / mixed property buys). Each request becomes
    // a new LoanRecord; counters/credit are initialised by
    // process_loan_repayment on the next tick when the loan is first
    // visited. Queue cleared via const_cast (same carve-out as
    // pending_property_transactions in real_estate).
    if (!state.pending_loan_requests.empty()) {
        for (const auto& req : state.pending_loan_requests) {
            LoanRecord loan{};
            loan.id = next_loan_id_++;
            loan.borrower_id = req.borrower_id;
            loan.lender_id = req.lender_id;
            loan.purpose = static_cast<LoanPurpose>(req.purpose);
            loan.principal = req.principal;
            loan.outstanding_balance = req.principal;
            loan.interest_rate = req.interest_rate;
            loan.repayment_per_tick = req.repayment_per_tick;
            loan.originated_tick = state.current_tick;
            loan.maturity_tick = req.maturity_tick;
            loan.in_default = false;
            loan.collateral_id = req.collateral_id;
            active_loans_.push_back(loan);
        }
        auto& mutable_queue = const_cast<std::vector<NewLoanRequest>&>(state.pending_loan_requests);
        mutable_queue.clear();
    }

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

// ─── Persistence helpers (schema v7) ────────────────────────────────────────
//
// Format (little-endian):
//   u32 schema_tag (1)
//   u32 loan_count
//   for each LoanRecord:
//     u32 id, u32 borrower_id, u32 lender_id, u8 purpose
//     f32 principal, f32 outstanding_balance, f32 interest_rate, f32 repayment_per_tick
//     u32 originated_tick, u32 maturity_tick, u8 in_default, u32 collateral_id
//   u32 credit_count
//   for each BorrowerCredit:
//     u32 borrower_id, u32 consecutive_misses
//     f32 credit_score, f32 total_debt_outstanding, f32 debt_service_per_tick,
//     f32 debt_to_income_ratio

namespace {

void put_u32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void put_f32(std::vector<uint8_t>& out, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    put_u32(out, bits);
}

struct Reader {
    const uint8_t* data;
    size_t size;
    size_t pos = 0;
    bool error = false;
    bool need(size_t n) {
        if (pos + n > size) {
            error = true;
            return false;
        }
        return true;
    }
    uint32_t u32() {
        if (!need(4))
            return 0;
        uint32_t v = data[pos] | (uint32_t(data[pos + 1]) << 8) | (uint32_t(data[pos + 2]) << 16) |
                     (uint32_t(data[pos + 3]) << 24);
        pos += 4;
        return v;
    }
    uint8_t u8() {
        if (!need(1))
            return 0;
        return data[pos++];
    }
    float f32() {
        uint32_t bits = u32();
        float v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }
};

}  // namespace

void BankingModule::serialize_state(std::vector<uint8_t>& out) const {
    put_u32(out, 1u);
    put_u32(out, static_cast<uint32_t>(active_loans_.size()));
    for (const auto& l : active_loans_) {
        put_u32(out, l.id);
        put_u32(out, l.borrower_id);
        put_u32(out, l.lender_id);
        out.push_back(static_cast<uint8_t>(l.purpose));
        put_f32(out, l.principal);
        put_f32(out, l.outstanding_balance);
        put_f32(out, l.interest_rate);
        put_f32(out, l.repayment_per_tick);
        put_u32(out, l.originated_tick);
        put_u32(out, l.maturity_tick);
        out.push_back(l.in_default ? 1u : 0u);
        put_u32(out, l.collateral_id);
    }
    put_u32(out, static_cast<uint32_t>(borrower_credits_.size()));
    for (const auto& bc : borrower_credits_) {
        put_u32(out, bc.borrower_id);
        put_u32(out, bc.consecutive_misses);
        put_f32(out, bc.profile.credit_score);
        put_f32(out, bc.profile.total_debt_outstanding);
        put_f32(out, bc.profile.debt_service_per_tick);
        put_f32(out, bc.profile.debt_to_income_ratio);
    }
}

bool BankingModule::deserialize_state(const uint8_t* data, size_t size) {
    Reader r{data, size};
    if (r.u32() != 1u)
        return false;
    uint32_t loan_count = r.u32();
    active_loans_.clear();
    active_loans_.reserve(loan_count);
    for (uint32_t i = 0; i < loan_count; ++i) {
        LoanRecord l{};
        l.id = r.u32();
        l.borrower_id = r.u32();
        l.lender_id = r.u32();
        l.purpose = static_cast<LoanPurpose>(r.u8());
        l.principal = r.f32();
        l.outstanding_balance = r.f32();
        l.interest_rate = r.f32();
        l.repayment_per_tick = r.f32();
        l.originated_tick = r.u32();
        l.maturity_tick = r.u32();
        l.in_default = (r.u8() != 0);
        l.collateral_id = r.u32();
        if (r.error)
            return false;
        active_loans_.push_back(l);
    }
    uint32_t credit_count = r.u32();
    borrower_credits_.clear();
    borrower_credits_.reserve(credit_count);
    for (uint32_t i = 0; i < credit_count; ++i) {
        BorrowerCredit bc{};
        bc.borrower_id = r.u32();
        bc.consecutive_misses = r.u32();
        bc.profile.credit_score = r.f32();
        bc.profile.total_debt_outstanding = r.f32();
        bc.profile.debt_service_per_tick = r.f32();
        bc.profile.debt_to_income_ratio = r.f32();
        if (r.error)
            return false;
        borrower_credits_.push_back(bc);
    }
    return !r.error;
}

}  // namespace econlife
