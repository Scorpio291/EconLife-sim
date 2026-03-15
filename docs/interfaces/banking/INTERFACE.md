# Module: banking

## Purpose
Processes loan applications, collects scheduled loan repayments, adjusts credit scores based on payment history, handles loan defaults, and manages interest rate computation tied to economic conditions. All actors (player and NPCs) use the same credit model. Province-parallel is not enabled; the module runs sequentially because loans cross province boundaries (lender in one province, borrower in another).

## Inputs (from WorldState)
- `active_loans[]` — all outstanding LoanRecord structs, each with: id, borrower_id, lender_id, purpose (LoanPurpose), principal, outstanding_balance, interest_rate (per tick), repayment_per_tick, originated_tick, maturity_tick, in_default, collateral_id
- `player.wealth` — liquid cash for player loan repayments
- `player.credit_profile` — CreditProfile with credit_score (0.0-1.0), total_debt_outstanding, debt_service_per_tick, debt_to_income_ratio
- `player.loan_ids[]` — active LoanRecord ids held by the player
- `npc_businesses[].cash` — NPC business cash for repayments
- `npc_businesses[].credit_profile` — CreditProfile per business
- `npc_businesses[].loan_ids[]` — active LoanRecord ids per business
- `npc_businesses[].revenue_per_tick` — used for DTI ratio and max loan calculations
- `provinces[].conditions` — economic conditions influencing base interest rates
- `config.banking.base_interest_rate` — 0.000027 per tick (~1% annualized at 365 ticks/year)
- `config.banking.credit_risk_spread` — 0.000082 per tick per credit score point deficit (~3% annual spread)
- `config.banking.collateral_rate_discount` — 0.000014 per tick (~0.5% annual reduction for secured loans)
- `config.banking.default_grace_ticks` — number of missed payments before in_default triggers
- `config.banking.credit_score_payment_gain` — 0.002 per-tick credit score increase when current
- `config.banking.credit_score_miss_penalty` — 0.01 per missed payment credit score penalty
- `config.banking.denial_dti_threshold` — 0.40; DTI above this causes loan denial
- `config.banking.max_loan_multiple_of_income` — 36.0; max loan = 36 months of income
- `config.banking.min_credit_score_by_purpose` — minimum credit score per LoanPurpose (business_capital: 0.35, property_purchase: 0.45, personal: 0.25)
- `config.banking.maturity_ticks_by_purpose` — loan duration per purpose
- `current_tick` — for maturity checks and default timing

## Outputs (to DeltaBuffer)
- `npc_deltas[]` — NPCDelta with capital_delta for NPC business owners making or receiving loan payments
- `player_delta.wealth_delta` — additive; loan repayment deductions from player wealth
- Loan state mutations written back through DeltaBuffer:
  - LoanRecord.outstanding_balance decremented by principal portion of each payment
  - LoanRecord.in_default set to true when borrower misses payments beyond grace period
  - CreditProfile.credit_score adjusted: +0.002/tick when current, -0.01 per missed payment, -0.20 on criminal conviction
  - CreditProfile.total_debt_outstanding, debt_service_per_tick, debt_to_income_ratio recomputed as derived fields
- `consequence_deltas[]` — new ConsequenceEntry on default events (collateral seizure, criminal_informal violence escalation)
- `evidence_deltas[]` — financial EvidenceToken generated when criminal_informal loans create ObligationNode enforcement actions

## Preconditions
- financial_distribution has completed (borrower cash positions are current for this tick).
- All LoanRecord entries have principal > 0.0, repayment_per_tick > 0.0, and maturity_tick > originated_tick.
- CreditProfile.credit_score is in [0.0, 1.0] for all borrowers at entry.

## Postconditions
- Every active loan has been processed for this tick: payment collected or default flagged.
- CreditProfile.credit_score updated for all borrowers with active loans.
- CreditProfile derived fields (total_debt_outstanding, debt_service_per_tick, debt_to_income_ratio) are consistent with current loan state.
- Loans reaching maturity_tick with outstanding_balance <= 0 are retired from active_loans.
- Defaulted loans with collateral have collateral seizure consequences queued.
- Criminal_informal loan defaults have violence escalation consequences queued via ObligationNode.

## Invariants
- CreditProfile.credit_score always in [0.0, 1.0] after clamping.
- LoanRecord.outstanding_balance >= 0.0 (cannot overpay; clamped at zero).
- LoanRecord.interest_rate >= 0.0.
- LoanRecord.repayment_per_tick > 0.0 for all active loans.
- total_debt_outstanding >= 0.0; debt_service_per_tick >= 0.0; debt_to_income_ratio >= 0.0.
- Player and NPC use identical credit evaluation logic (player is not a special case).
- Same seed + same inputs = identical loan processing output (deterministic).

## Failure Modes
- Borrower entity not found (NPC business dissolved, player dead): mark loan as defaulted, log warning, skip repayment.
- Lender entity not found (bank NPC business dissolved): loan continues with payments going to system sink, log warning.
- Interest rate calculation produces negative value: clamp to 0.0, log error.
- CreditProfile with NaN fields: reset to default (credit_score = 0.5, zeroed derived fields), log error.
- Division by zero in DTI calculation (zero revenue): set DTI to infinity, deny loan.

## Performance Contract
- Sequential execution (not province-parallel); typical loan count at scale: ~200-500 active loans.
- Target: < 5ms for full loan processing pass.
- O(N) in active_loans count; no nested loops over NPC population.

## Dependencies
- runs_after: ["financial_distribution"]
- runs_before: [] (end of Pass 1 chain; no current-tick dependents)

## Test Scenarios
- `test_successful_repayment_reduces_balance_and_improves_credit`: Create a loan with known repayment_per_tick. Set borrower wealth above repayment amount. Run one tick. Verify outstanding_balance decreased by principal portion, credit_score increased by 0.002, and borrower wealth decreased by repayment_per_tick.
- `test_missed_payment_triggers_default_after_grace_period`: Create a loan where borrower has insufficient cash. Run for default_grace_ticks + 1 ticks. Verify in_default transitions from false to true and credit_score decreased by 0.01 per missed tick.
- `test_loan_denial_on_high_dti`: Submit a loan application for an applicant with debt_to_income_ratio above 0.40. Verify the loan is denied with reason: DTI exceeded.
- `test_loan_denial_on_low_credit_score`: Submit a business_capital loan for an applicant with credit_score 0.30 (below min 0.35). Verify denial with reason: credit_score insufficient.
- `test_interest_rate_reflects_credit_risk`: Evaluate loans for two applicants: one with credit_score 0.9 and one with credit_score 0.3. Verify the low-credit applicant receives a higher interest_rate by (1.0 - 0.3) * credit_risk_spread - (1.0 - 0.9) * credit_risk_spread = 0.6 * 0.000082 - 0.1 * 0.000082 difference.
- `test_collateral_reduces_interest_rate`: Evaluate two identical loan applications, one with collateral_id set and one unsecured. Verify the secured loan's interest_rate is lower by config.banking.collateral_rate_discount (0.000014).
- `test_criminal_informal_default_escalates_violence`: Create a criminal_informal loan. Set borrower to default. Verify a consequence is queued with ObligationNode enforcement (FavorType::financial_loan) and no credit_score impact (criminal_informal loans are not reflected in CreditProfile).
