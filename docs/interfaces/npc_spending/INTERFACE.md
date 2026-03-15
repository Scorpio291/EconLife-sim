# Module: npc_spending

## Purpose
Computes consumer demand from significant NPCs and background population cohorts each tick, writing demand contributions to `RegionalMarket.demand_buffer` for the next tick's price calculation. This module implements the Consumer Demand Model (TDD Section 5): per-NPC purchase decisions driven by disposable income, spot price, quality preference, and BuyerType modulation. Consumer demand is one of two components in the demand buffer (the other being derived demand from production in tick step 1).

The module iterates all significant NPCs and all `PopulationCohort` records per province, computing `demand_contribution(npc_or_cohort, good)` for every consumer good, then aggregates results into `demand_buffer_delta` per `(good_id, province_id)`. Background population cohorts use cohort-mean disposable income and the cohort's modal `BuyerType` for each good category, scaled by `cohort.population_size`.

## Inputs (from WorldState)
- `significant_npcs` -- iterated per province via `NPC.home_province_id`; reads `NPC.capital` (as disposable income proxy), `NPC.status` (only `active` NPCs participate)
- `named_background_npcs` -- same struct, LOD flag set; simplified demand model
- `provinces` -- province list for province-parallel dispatch; reads `Province.cohort_stats` containing `PopulationCohort` records per `DemographicGroup`
- `regional_markets` -- `spot_price` per `(good_id, province_id)` used in `price_factor` calculation; `batch_quality` and `market_quality_avg` used in `quality_factor`
- `current_tick` -- used for quarterly `BuyerType` update scheduling (every 90 ticks)
- Goods data file (loaded at startup) -- `base_consumer_demand_units`, `income_elasticity`, `price_elasticity`, `quality_weight` per good

## Outputs (to DeltaBuffer)
- `MarketDelta.demand_buffer_delta` -- additive consumer demand contribution per `(good_id, province_id)` for tick `t+1` price calculation
- `NPCDelta.spending_this_tick` -- total spending amount per significant NPC this tick (informational; feeds dashboard)

## Preconditions
- Production module (tick step 1) has completed and derived demand from industrial consumption is already written to `demand_buffer_delta`.
- Price engine (tick step 5) has completed; `spot_price` values in `regional_markets` are current for this tick.
- NPC behavior module (tick step 7) has completed; NPC disposable income and status are settled for this tick.
- Goods data file is loaded and immutable (loaded at startup from package content files).
- All `NPC.home_province_id` values reference valid provinces.

## Postconditions
- Every active significant NPC and every `PopulationCohort` in every province has had its consumer demand contribution computed exactly once for this tick.
- `RegionalMarket.demand_buffer` for each `(good_id, province_id)` reflects the sum of: (a) derived demand from production (already written by tick step 1) and (b) consumer demand written by this module.
- No demand contribution is negative; all values are clamped to `>= 0.0`.
- `BuyerType` per NPC is updated if `current_tick` falls on a quarterly boundary.

## Invariants
- Consumer demand formula: `demand_contribution(npc, g) = good.base_consumer_demand_units * income_factor(npc, g) * price_factor(npc, g) * quality_factor(npc, g)`.
- `income_factor` = `(npc.disposable_income / config.demand.reference_income) ^ good.income_elasticity`, clamped to `[0.0, config.demand.max_income_factor]` (default max: 5.0).
- `price_factor` = `(good.base_price / max(spot_price(g, p), 0.01)) ^ |good.price_elasticity|`, clamped to `[config.demand.min_price_factor, inf)` (default min: 0.05). BuyerType modulates elasticity: `necessity_buyer` applies `elasticity * 0.1`, `price_sensitive` applies `elasticity * 1.5`, `quality_seeker` applies `elasticity * 0.6`, `brand_loyal` applies `elasticity * 0.8`.
- `quality_factor` = `1.0 + good.quality_weight * (batch_quality - market_quality_avg)`. Quality weight varies by BuyerType: 0.0 for `price_sensitive`, 0.3 for `brand_loyal`, 0.6 for `quality_seeker`, 0.0 for `necessity_buyer`.
- Background population demand: `demand_contribution(cohort, g) * cohort.population_size`, using cohort-mean disposable income and modal `BuyerType`.
- Floating-point accumulations use canonical sort order (`good_id` ascending, then `province_id` ascending) for deterministic summation.
- Same seed + same inputs = identical demand output regardless of core count.
- All random draws (for BuyerType quarterly update) go through `DeterministicRNG`.

## Failure Modes
- Missing goods data entry for a good referenced in `regional_markets`: log warning, skip that good for all NPCs in the affected province. No demand contribution for that good this tick.
- NPC with negative `capital` (debt): `income_factor` computes to 0.0 or near-zero via the power function; demand collapses gracefully. No special-case handling required.
- NaN or negative demand from floating-point edge case: clamp to 0.0, log diagnostic.
- Province with zero `PopulationCohort` entries (empty province): no consumer demand generated; province market relies on derived demand only.

## Performance Contract
- Province-parallel execution across up to 6 provinces (global aggregate first, then province-parallel recompute).
- Target: < 25ms total for ~2,000 significant NPCs + cohort aggregation across 6 provinces on 6 cores.
- Per-NPC demand computation: < 0.01ms average across all consumer goods.
- Must not exceed tick budget share that would push total tick above 200ms target.

## Dependencies
- runs_after: ["npc_behavior", "price_engine"]
- runs_before: []

## Test Scenarios
- `test_basic_consumer_demand_adds_to_demand_buffer`: A province with 5 significant NPCs and a food good. Verify `demand_buffer_delta` for food in that province equals the sum of individual `demand_contribution` values computed from the formula.
- `test_income_elasticity_scales_demand`: Two NPCs with identical BuyerType but different `capital` values (1x vs 3x reference income). Verify the higher-income NPC's demand contribution for a normal good (income_elasticity > 0) is proportionally larger per the power function.
- `test_price_sensitive_buyer_high_elasticity`: An NPC with `BuyerType::price_sensitive` faces a good at 2x `base_price`. Verify demand is reduced more aggressively (elasticity * 1.5 modulator) compared to a `brand_loyal` NPC (elasticity * 0.8).
- `test_necessity_buyer_inelastic`: An NPC with `BuyerType::necessity_buyer` faces a good at 5x `base_price`. Verify demand contribution barely changes (elasticity * 0.1 modulator), reflecting near-zero price elasticity.
- `test_quality_seeker_prefers_high_quality`: Two goods with identical prices but different `batch_quality` values. Verify `quality_seeker` NPC's demand shifts toward the higher-quality good via the quality_factor term (quality_weight = 0.6).
- `test_cohort_demand_scales_by_population_size`: A `PopulationCohort` with `size = 10000` and a single significant NPC. Verify cohort demand contribution equals per-capita demand * 10000.
- `test_income_factor_clamped_at_max`: An NPC with `capital` = 100x `reference_income`. Verify `income_factor` is clamped at `config.demand.max_income_factor` (default 5.0), not allowed to grow unbounded.
- `test_price_factor_floored_at_min`: A good with `spot_price` = 1000x `base_price`. Verify `price_factor` does not drop below `config.demand.min_price_factor` (default 0.05).
- `test_inactive_npc_excluded`: An NPC with `status == imprisoned` is skipped. Verify zero demand contribution from that NPC.
- `test_buyer_type_quarterly_update`: At a quarterly boundary tick, verify that NPC BuyerType values are re-evaluated based on current Background and Trait, and the updated BuyerType affects demand computation for subsequent ticks.
- `test_province_parallel_determinism`: Run 50 ticks of consumer demand with 6 provinces on 1 core and 6 cores. Verify bit-identical `demand_buffer_delta` values across all `(good_id, province_id)` pairs.
- `test_zero_population_province_no_crash`: A province with zero significant NPCs and zero cohort entries. Verify no demand is generated and no error occurs.
