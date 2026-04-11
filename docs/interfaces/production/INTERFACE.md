# Module: production

## Purpose
Processes all NPCBusiness entities each tick: consumes input goods from inventory according to recipes, produces output goods scaled by worker productivity and technology tier, and records derived demand for consumed inputs into the supply/demand pipeline.

## Inputs (from WorldState)
- `npc_businesses` — all NPCBusiness records; iterated per province to consume inputs and produce outputs
- `regional_markets` — spot prices and supply figures per (good_id x province_id); used to value input costs and determine input availability
- `provinces` — province list for province-parallel dispatch; each business is assigned to exactly one province via `NPCBusiness.province_id`
- `current_tick` — used for technology maturation lookups and seasonal modifiers
- `global_technology_state` — global tech context for maturation ceiling capping on technology-intensive recipes

## Outputs (to DeltaBuffer)
- `MarketDelta.supply_delta` — additive supply contribution per (good_id, province_id) from production output this tick
- `MarketDelta.demand_buffer_delta` — derived demand for input goods consumed (industrial demand component; feeds next tick's price calculation)
- `BusinessDelta.cash_delta` — operating cost deduction from business cash for input consumption and labor
- `BusinessDelta.revenue_delta` — revenue credited from output goods produced (at current spot_price or informal market price if `criminal_sector == true`)
- `BusinessDelta.output_quality` — quality grade of this tick's output batch, capped by technology tier ceiling and maturation level

## Preconditions
- `deferred_work_queue` has been drained (Step 2 complete); transit arrivals for this tick are already applied to supply.
- All `NPCBusiness.province_id` values reference valid provinces.
- Recipe registry is loaded and immutable (loaded at startup from package content files).
- Goods data file is loaded with `base_price`, `quality_premium_coeff`, and per-good config fields.

## Postconditions
- Every active (non-bankrupt) NPCBusiness has had its recipe executed exactly once for this tick.
- `RegionalMarket.supply` for each province reflects this tick's local production output (to be combined with transit arrivals already applied).
- Derived demand for consumed inputs is written to `demand_buffer_delta` for the next tick's price calculation (one-tick lag).
- Business `cash` reduced by actual operating costs; `revenue_per_tick` updated.
- No business inventory goes negative; production is clamped to available input supply.

## Invariants
- Recipes with `era_available > current_era` are skipped entirely. No output is produced and no operating cost is charged for that facility this tick. This gates new-era production methods until the simulation advances to the appropriate era.
- Businesses with `criminal_sector == true` read informal market prices, not formal `spot_price`. Market layer selection is always governed by the `criminal_sector` bool, never by `BusinessSector` enum alone.
- `BusinessSector::criminal` with `criminal_sector == false` is invalid and must never appear (loader rejects at startup).
- Output volume formula: `actual_output = recipe_output_per_tick * (1.0 + TECH_TIER_OUTPUT_BONUS_PER_TIER * max(0, facility.tech_tier - recipe.min_tech_tier))` where `TECH_TIER_OUTPUT_BONUS_PER_TIER = 0.08`.
- Operating cost formula: `actual_cost = recipe_base_cost_per_unit * (1.0 - TECH_TIER_COST_REDUCTION_PER_TIER * max(0, facility.tech_tier - recipe.min_tech_tier)) * bottleneck_ratio` where `TECH_TIER_COST_REDUCTION_PER_TIER = 0.05`. When `bottleneck_ratio == 0` (no input supply), `actual_cost == 0` — zero production incurs zero variable cost.
- Quality ceiling: `quality_ceiling = TECH_QUALITY_CEILING_BASE + TECH_QUALITY_CEILING_STEP * (facility.tech_tier - recipe.min_tech_tier)`, further capped by `actor_tech_state.maturation_of(recipe.key_technology_node)` for technology-intensive recipes. Commodity recipes (`key_technology_node == ""`) are unaffected by maturation cap.
- Floating-point accumulations use canonical sort order (good_id ascending, then province_id ascending) for deterministic summation.
- Same seed + same inputs = identical production output regardless of core count.

## Failure Modes
- Missing recipe for a business sector: log warning, skip business, continue. Business produces nothing this tick; no operating cost is charged.
- Input good unavailable (supply exhausted): production clamped to zero for that recipe; output reduced proportionally. Bottleneck recorded for supply chain propagation in Step 2 next tick.
- NaN or negative output from floating-point edge case: clamp to 0.0, log diagnostic.

## Performance Contract
- Province-parallel execution across up to 6 provinces.
- Target: < 30ms total for ~2,000 NPCBusiness entities across 6 provinces on 6 cores.
- Per-business recipe execution: < 0.015ms average.
- Must not exceed tick budget share that would push total tick above 200ms target.

## Dependencies
- runs_after: [] (first domain module; runs after deferred_work_queue drains at Step 2)
- runs_before: ["supply_chain"]

## Test Scenarios
- `test_basic_recipe_consumes_inputs_produces_output`: A business with a steel recipe (inputs: iron_ore + coking_coal) consumes the correct quantities from province supply and adds steel to province supply. Verify input supply decreases and output supply increases by recipe-specified amounts.
- `test_tech_tier_bonus_increases_output`: Two identical businesses run the same recipe; one has facility tech_tier 3 vs recipe min_tech_tier 2. Verify the higher-tier business produces exactly 8% more output (1.08x multiplier).
- `test_tech_tier_reduces_operating_cost`: Same setup as above. Verify the higher-tier business incurs 5% lower operating cost per unit (0.95x multiplier).
- `test_insufficient_input_clamps_output_to_zero`: A business with a recipe requiring 10 units of copper but only 3 available in province supply. Verify output is clamped proportionally and no negative inventory results.
- `test_criminal_sector_reads_informal_price`: A business with `criminal_sector = true` producing drugs. Verify revenue calculation uses informal market price, not formal `spot_price` from RegionalMarket.
- `test_derived_demand_written_to_demand_buffer`: After production, verify that `demand_buffer_delta` for consumed input goods reflects the quantities consumed, feeding the next tick's price calculation.
- `test_quality_ceiling_capped_by_maturation`: A technology-intensive recipe with `key_technology_node = "advanced_metallurgy"`. Business has tech_tier 4, recipe min_tier 2, but `maturation_of("advanced_metallurgy") = 0.5`. Verify output quality is capped at `min(tier_ceiling, 0.5)`, not the full tier-based ceiling.
- `test_commodity_recipe_ignores_maturation`: A commodity recipe with `key_technology_node = ""`. Verify quality ceiling uses only tier-based calculation with no maturation cap.
- `test_bankrupt_business_skipped`: A business in BANKRUPT status is not processed by the production module. Verify no supply contribution and no cost deduction.
- `test_province_parallel_determinism`: Run 50 ticks of production with 6 provinces on 1 core and 6 cores. Verify bit-identical supply, demand, cash, and revenue deltas.
- `test_seasonal_agriculture_modifier_applied`: A farm business during drought season. Verify agricultural output modifier from `FarmSeasonState` reduces output proportionally to the seasonal stress factor.
