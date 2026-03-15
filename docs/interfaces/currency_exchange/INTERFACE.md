# Module: currency_exchange

## Purpose
Manages exchange rates between all nations in the simulation. Updates CurrencyRecord usd_rate values weekly using a macro-anchored random walk model driven by trade balance, inflation differential, and sovereign risk. Handles pegged currency mechanics including peg defense via foreign reserve depletion and peg break events that trigger currency crisis random events. Provides the cross-currency price conversion function used by all cross-border transaction modules (trade infrastructure, commodity trading, LOD 1 trade offers).

NOT province-parallel: exchange rates are global-scope operations affecting all nations simultaneously. Weekly cadence: most ticks are no-ops. Specified in TDD Section 28.

## Inputs (from WorldState)
- `currencies` — `std::map<uint32_t, CurrencyRecord>` keyed by nation_id:
  - `nation_id` — owning nation
  - `iso_code` — ISO-style currency code (e.g., "USD", "EUR", "BRL")
  - `usd_rate` — units of this currency per 1 USD; updated weekly
  - `usd_rate_baseline` — rate at world load; used for percentage deviation tracking and floor/ceiling clamping
  - `volatility` — daily standard deviation for rate noise term (e.g., 0.01 = ~1% per tick)
  - `foreign_reserves` (0.0-1.0) — reserve level supporting peg; depleted during peg defense
  - `pegged` (bool) — whether currency is pegged to USD
  - `peg_rate` — exchange rate when pegged (only valid when `pegged == true`)
- `nations[]` — Nation economic indicators:
  - `trade_balance_fraction` — net exports as fraction of GDP proxy
  - `inflation_rate` — current inflation differential
  - `credit_rating` (0.0-1.0) — sovereign creditworthiness
- `lod1_trade_offers[]` — for trade balance calculation inputs (LOD 1 nation trade data)
- `current_tick` — for weekly cadence check (`current_tick % (TICKS_PER_MONTH / 4) == 0`)
- Config constants from `simulation_config.json -> currency`:
  - `trade_balance_weight` = 0.30
  - `inflation_weight` = 0.40
  - `sovereign_risk_weight` = 0.30
  - `peg_break_reserve_threshold` = 0.15
  - `floor_fraction` = 0.20 (rate cannot fall below 20% of baseline)
  - `ceiling_fraction` = 5.00 (rate cannot rise above 5x baseline)
  - `fx_transaction_cost` = 0.01 (1% friction on cross-currency transactions)

## Outputs (to DeltaBuffer)
- `CurrencyDelta[]` — per-nation CurrencyRecord updates (only on weekly ticks):
  - `usd_rate` — new exchange rate after macro model + noise application
  - `pegged` — changed to `false` if peg breaks (reserve depletion below threshold)
  - `foreign_reserves` — depleted during peg defense; unchanged for free-floating currencies
- `DeferredWorkItem[]` — consequence entries:
  - Currency crisis random event when peg breaks: `RandomEvent(type: economic, template_key: "currency_crisis", province_id: capital_province_id)`
  - Economic stability consequences propagated to affected provinces
- Cross-currency conversion utility (stateless function, not a delta):
  - `price_in_buyer_currency = price_in_seller_currency * (seller_currency.usd_rate / buyer_currency.usd_rate) * (1.0 + fx_transaction_cost)`

## Preconditions
- `commodity_trading` has completed (trade balance data for the current period is available).
- `government_budget` has completed (fiscal policy inputs affect inflation rate and credit rating).
- All CurrencyRecord fields are initialized: `usd_rate > 0`, `usd_rate_baseline > 0`, `volatility >= 0`.
- Nation economic indicators are current: `trade_balance_fraction`, `inflation_rate`, and `credit_rating` reflect this tick's state.
- DeterministicRNG is available with `(world_seed, current_tick, nation_id)` salt for noise generation.

## Postconditions
- If this is a weekly tick: all nation `CurrencyRecord.usd_rate` values updated.
- If this is NOT a weekly tick: zero deltas produced (module is a no-op).
- Pegged currencies with `foreign_reserves > peg_break_reserve_threshold`: rate unchanged at `peg_rate`.
- Pegged currencies with `foreign_reserves <= peg_break_reserve_threshold`: `pegged` set to `false`, currency crisis event queued, rate falls through to free-float update on next weekly tick.
- Free-floating currencies: rate adjusted by macro model: `macro_factor = 1.0 + (trade_balance * trade_balance_weight) - (inflation * inflation_weight) - ((1.0 - credit_rating) * sovereign_risk_weight)`, plus noise from `normal_distribution(mean=0.0, stddev=volatility)`.
- Rate clamped to `[floor_fraction * usd_rate_baseline, ceiling_fraction * usd_rate_baseline]`.

## Invariants
- `usd_rate > 0` always — enforced by `floor_fraction * usd_rate_baseline` floor.
- `usd_rate <= ceiling_fraction * usd_rate_baseline` — prevents hyperinflation runaway.
- `usd_rate_baseline` is immutable after world load (reference point for deviation tracking).
- Pegged currency rate is exactly `peg_rate` while `pegged == true`; no drift or noise applied.
- `foreign_reserves` in [0.0, 1.0]; clamped on every update.
- `volatility >= 0.0` (zero volatility = deterministic macro-only rate movement).
- `fx_transaction_cost >= 0.0` (zero = no friction).
- Cross-currency conversions are symmetric modulo transaction costs: `A -> B -> A` loses exactly `fx_transaction_cost` twice.
- NOT province-parallel: exchange rates are global.
- All random draws (noise term) use `DeterministicRNG(world_seed, current_tick, nation_id)`.
- Same seed + same inputs = identical rate outputs regardless of execution environment.

## Failure Modes
- Nation with no CurrencyRecord: skip that nation, log warning. Other nations processed normally.
- `usd_rate_baseline == 0.0`: set to 1.0 (USD equivalent), log error. Prevents division by zero in floor/ceiling calculation.
- `foreign_reserves` goes negative from depletion calculation: clamp to 0.0, trigger peg break immediately.
- All nations pegged: valid state (no free-floating rate updates occur; only peg monitoring runs).
- NaN in rate calculation from extreme macro_factor or noise: reset to previous tick's rate, log error diagnostic.
- Division by zero in cross-currency conversion (`buyer_currency.usd_rate == 0`): reject conversion, log error, return 0.0.

## Performance Contract
- NOT province-parallel: runs on main thread.
- Target: < 1ms total for up to 50 nations on weekly ticks.
- Weekly cadence: 6 out of 7 ticks are no-ops (zero cost).
- O(N) in nation count per weekly update.
- Per-nation computation: one macro factor calculation, one RNG draw, one clamp — trivially fast.
- Must not exceed tick budget share that would push total tick above 200ms target.

## Dependencies
- runs_after: ["commodity_trading", "government_budget"]
- runs_before: []

## Test Scenarios
- `test_weekly_rate_update_macro_model`: Set `trade_balance_fraction = 0.10`, `inflation_rate = 0.05`, `credit_rating = 0.80`. Run on weekly tick. Verify `macro_factor = 1.0 + (0.10 * 0.30) - (0.05 * 0.40) - (0.20 * 0.30) = 1.0 + 0.03 - 0.02 - 0.06 = 0.95`. Verify `usd_rate` adjusted by `macro_factor + noise`.
- `test_non_weekly_tick_is_noop`: Run on non-weekly tick. Verify zero currency deltas produced. Verify module returns in < 0.01ms.
- `test_pegged_currency_stable`: Set `pegged = true`, `foreign_reserves = 0.50` (well above 0.15 threshold). Run weekly tick. Verify `usd_rate` remains exactly at `peg_rate`.
- `test_peg_break_on_low_reserves`: Set `pegged = true`, `foreign_reserves = 0.10` (below 0.15 threshold). Run weekly tick. Verify `pegged` set to `false` and `currency_crisis` random event queued.
- `test_rate_floor_clamp`: Set macro inputs and noise that would push rate below `0.20 * usd_rate_baseline`. Verify `usd_rate` clamped to floor.
- `test_rate_ceiling_clamp`: Set macro inputs and noise that would push rate above `5.0 * usd_rate_baseline`. Verify `usd_rate` clamped to ceiling.
- `test_cross_currency_conversion_accuracy`: Set seller `usd_rate = 5.0`, buyer `usd_rate = 1.0`, `fx_transaction_cost = 0.01`. Convert 100 local units. Verify `converted = 100 * (5.0 / 1.0) * 1.01 = 505.0`.
- `test_cross_currency_round_trip_loss`: Convert 100 units from Currency A to B, then B back to A. Verify loss equals approximately `2 * fx_transaction_cost` fraction of original amount.
- `test_deterministic_noise_across_runs`: Run two identical weekly updates with same `world_seed` and `current_tick`. Verify bit-identical `usd_rate` outputs.
- `test_zero_volatility_no_noise`: Set `volatility = 0.0`. Verify `usd_rate` moves by exactly macro_factor with no random component.
- `test_multiple_nations_independent`: Set two nations with different macro conditions. Verify each nation's rate update is computed independently with no cross-contamination.
