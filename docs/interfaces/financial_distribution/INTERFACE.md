# Module: financial_distribution

## Purpose
Processes all business-to-actor money flows each tick: business revenue collection, salary payments (including deferred salary tracking), bonus and dividend distributions, owner's draw settlements, equity grant vesting, and tax withholding. Province-parallel.

## Inputs (from WorldState)
- `npc_businesses[]` — per-business: cash, revenue_per_tick, cost_per_tick, owner_id, province_id, deferred_salary_liability, criminal_sector
- `npc_businesses[].executive_compensation` — mechanism, salary_per_tick, bonus_rate, dividend_yield_target, equity_grants[]
- `npc_businesses[].board_composition` — independence_score, next_approval_tick, member_npc_ids (for board approval gating at medium/large scale)
- `npc_businesses[].scale` — BusinessScale (micro/small/medium/large) determines which CompensationMechanism values are valid
- `npcs[]` — NPC capital (for employee wage payments), employer_business_id
- `player_character` — wealth (for owner compensation deposits)
- `current_tick` — used for quarterly bonus/dividend cadence and equity cliff/vest evaluation
- `regional_markets[]` — spot prices for revenue verification (criminal_sector uses informal prices)
- `provinces[]` — provincial_business_tax_rate for withholding calculations
- `config.business` — deferred_salary_max_ticks, draw_reporting_threshold, board_rubber_stamp_threshold, cash_surplus_months, reasonable_salary_floor

## Outputs (to DeltaBuffer)
- `PlayerDelta.wealth_delta` — additive; salary, bonus, dividend, and owner's draw deposits into player wealth
- `NPCDelta.capital_delta` — additive; wage payments to NPC employees
- `NPCDelta.new_memory_entry` — witnessed_wage_theft memory when deferred_salary_liability exceeds deferred_salary_max_ticks
- `MarketDelta` — none directly (revenue is read, not modified)
- `EvidenceDelta.new_token` — suspicious_transaction evidence token when owner's draw exceeds draw_reporting_threshold per month; financial evidence tokens for compensation disclosures at listed companies (full_package mechanism)
- Business state updates via delta: cash reduction for wage/salary/bonus/dividend outflows, deferred_salary_liability accumulation or paydown, equity_grants[].shares_vested advancement

## Preconditions
- price_engine has completed for this tick (spot prices and equilibrium prices are current).
- All NPCBusiness records have valid owner_id references (0 for independent, valid npc_id or player_id otherwise).
- BusinessScale is recomputed from current revenue_per_tick every tick in init_for_tick().
- CompensationMechanism is valid for the business's current scale (e.g., owners_draw only on micro, full_package only on large).

## Postconditions
- Every business with cash >= salary_per_tick has paid its owner's salary this tick; shortfall businesses have incremented deferred_salary_liability.
- Deferred salary is paid first when cash recovers (FIFO: deferred before current salary).
- Quarterly bonus and dividend payments have been processed for businesses whose current_tick aligns with their quarterly cadence.
- Equity grants past their cliff_tick have advanced shares_vested by vesting_rate (capped at shares_granted).
- All NPC employee wages for this tick have been emitted as NPCDelta.capital_delta entries.
- Evidence tokens generated for owner's draw amounts exceeding the monthly reporting threshold.
- No business.cash has gone negative solely from compensation payments (payments capped at available cash; remainder deferred).

## Invariants
- Compensation mechanism availability by scale is enforced: owners_draw is micro-only; salary_dividend requires medium or large; full_package requires large.
- Board approval is required for bonus_rate > board_approval_bonus_threshold at medium/large scale; unapproved bonuses are not paid.
- Captured boards (independence_score < board_rubber_stamp_threshold) auto-approve all decisions.
- Dividend payout never reduces business cash below working_capital_floor (cost_per_tick * cash_surplus_months).
- Salary payments are deterministic: same business state + same tick = identical payment sequence.
- Criminal sector businesses use informal market revenue figures, not formal spot prices.
- `salary_per_tick` converges toward `current revenue_per_tick × scale_fraction` at 5%/tick (SALARY_CONVERGENCE_RATE). This prevents frozen salaries from draining businesses when revenue falls. Scale is re-evaluated every tick in `init_for_tick()` from the current revenue_per_tick thresholds: micro < 100, small < 500, medium < 2000, large >= 2000.

## Failure Modes
- Invalid owner_id reference: log warning, skip compensation for that business, continue processing other businesses.
- CompensationMechanism invalid for current scale (e.g., full_package on micro): log error, fall back to salary_only for that tick.
- Negative revenue_per_tick: treat as zero revenue; no bonus or dividend generated.

## Performance Contract
- Province-parallel execution: < 2ms per province at ~333 businesses/province (2,000 NPCs / 6 provinces).
- Total module budget: < 15ms at full V1 scale (2,000 NPCs, 6 provinces).

## Dependencies
- runs_after: ["price_engine"]
- runs_before: ["npc_behavior"]

## Test Scenarios
- `test_salary_paid_when_cash_sufficient`: Business with cash=10,000 and salary_per_tick=100. After one tick, player.wealth increased by 100, business.cash decreased by 100, deferred_salary_liability remains 0.
- `test_salary_deferred_when_cash_insufficient`: Business with cash=30 and salary_per_tick=100. After one tick, deferred_salary_liability=70, player.wealth increased by 30, business.cash=0.
- `test_deferred_salary_paid_first_on_recovery`: Business with deferred_salary_liability=200 and cash recovering to 500. Verify deferred liability paid before current tick salary. After tick: deferred_salary_liability=0, current salary also paid.
- `test_wage_theft_memory_on_sustained_deferral`: Business with deferred_salary_liability accumulating for > deferred_salary_max_ticks (default 30). Verify witnessed_wage_theft MemoryEntry generated on the owner NPC with emotional_valence = -0.6.
- `test_quarterly_dividend_respects_working_capital_floor`: Business with cash=50,000, cost_per_tick=1,000, dividend_yield_target=0.5, retained_earnings=100,000. Verify dividend payout does not reduce cash below cost_per_tick * cash_surplus_months (5,000). Payout capped at cash - working_capital_floor.
- `test_owners_draw_generates_evidence_above_threshold`: Micro business owner executes draw of 25,000 in a month (above draw_reporting_threshold=20,000). Verify suspicious_transaction EvidenceToken created at VisibilityScope::institutional.
- `test_board_rejects_excessive_compensation`: Medium-scale business with independent board (independence_score=0.8). Proposed salary = 2.0x COMPENSATION_BENCHMARK. Verify board rejects; deferred_salary_liability set to proposed - benchmark; board_compensation_dispute scene card scheduled.
