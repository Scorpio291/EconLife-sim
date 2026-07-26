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
- **Inputs are drawn at the throughput actually achieved (conservation).** The facility's effective throughput ratio — hard-input bottleneck x motive-power bottleneck x staffing x deposit-remainder fraction — is computed *before* any input is consumed, and hard inputs are debited at that ratio (yield-modifier inputs at that ratio x `modifier_avail`). Debiting inputs at the bare input-availability ratio while attenuating output by power/staffing/deposit destroyed the difference: the untransformed matter became neither product, nor waste (waste is derived from actual output), nor returned stock. Physically, you do not shovel ore into an unpowered, unstaffed furnace. Consequence: `inputs consumed / output produced` is invariant under a brownout, an unstaffed shift, or an exhausting deposit, and an unstaffed facility (`worker_count == 0`) consumes nothing. The worker term splits into a **staffing attenuator** (0 workers -> 0.0, otherwise 1.0), which throttles the input draw, and the diminishing-returns **throughput amplifier** `1 + 0.15 * (worker_count - 1)`, which applies to output only; their product is the single worker multiplier as before. Tech-tier and `output_rate_modifier` amplification likewise apply to output only.
- **Yield-modifier inputs (fertilizer/feed boost yield, do not gate it).** A recipe input flagged `yield_modifier` (the `yield_modifier_inputs` column in the recipes CSV, a `;`-separated list of input good keys) does NOT participate in the hard-input bottleneck. Instead, a facility produces a subsistence base even when the modifier input is absent, scaling up to full yield as it is applied: `yield_modifier_factor = yield_modifier_floor + (1 - yield_modifier_floor) * min(available/required across modifier inputs, clamped [0,1])`, defaulting to 1.0 when the recipe has no modifier input. Output is multiplied by this factor (alongside the hard-input bottleneck, motive-power bottleneck, worker, and tech-tier terms). The modifier input is consumed only in proportion to how much is applied (`required * hard_bottleneck * modifier_avail`), so unfertilized farming consumes no fertilizer. This realizes the GDD agriculture yield model (fertilizer/feed improve yield) and lets the food chain bootstrap from a primitive base before the fertilizer/feed industry exists. `yield_modifier_floor` is `ProductionConfig::yield_modifier_floor` (default 0.4). Base-game use: crop recipes mark `fertilizer_npk`; livestock recipes mark `corn`.
- **Extraction binding (deposit-bound recipes).** A recipe with `extracted_resource` set (the `extracted_resource` column in the recipes CSV, a `ResourceType` name) may only produce where the facility's province holds a matching `ResourceDeposit` that is (a) era-unlocked (`deposit.era_unlock <= current_era`), (b) accessible (`deposit.accessibility > 0`; permafrost/locked deposits seed to 0 until thaw + the required tech), and (c) not exhausted (`quantity_remaining > 0`). This is the existence precondition: a resource not located in the province cannot be extracted there. If no usable deposit exists, the facility produces nothing and is charged no operating cost. Among usable deposits the highest-grade is chosen (tie-broken by lowest `id`). Output is scaled by deposit grade (`0.5 + 0.5 * quality`) and the primary (non-byproduct) output is capped by `quantity_remaining`; the extracted amount is subtracted from the deposit via `DepositDelta` (applied in deterministic province/deposit order), making located resources finite and exhaustible. Recipes without `extracted_resource` (including raw goods whose `ResourceType` is not yet modelled) are not deposit-bound and behave as before.
  - **One deposit is one stock across the tick.** `WorldState` is const for the whole tick and deposit selection is deterministic, so every facility in a province extracting the same resource is handed the *same* deposit. The remainder each facility caps against is `quantity_remaining` **minus what facilities processed earlier in the same tick already drew** (a per-province, per-tick draw ledger keyed by province + deposit id; probed by key only, never iterated, so it cannot affect determinism). Therefore the sum of a tick's extraction from a deposit can never exceed `quantity_remaining` — without this each facility would cap against the full pre-tick remainder, `apply_deposit_deltas` would floor the deposit at 0, and the excess output would already have been booked as market supply (matter created on the exhaustion tick). A facility that finds the deposit drawn to zero by an earlier facility produces nothing that tick.
  - The deposit remainder is an **output-side** cap, so it is converted into an equivalent throughput fraction (`remainder / uncapped primary output`) and applied to the whole batch: inputs, primary output and byproducts scale together. Ore that is not there is not mined, so the inputs it would have consumed are not consumed and the byproducts riding along with it do not appear.
- **Motive power (era-appropriate work: electricity, mechanical, process heat).** Before facilities run, a motive-power pre-pass per province computes per-form demand by summing each recipe's requirement over operational, era-available facilities: `energy_per_tick` (electricity), `mechanical_per_tick` (rotary/reciprocating work — mills, hammers, pumps), and `fuel_per_tick` (process heat — smelting, kilns). Each form is supplied from the province endowment, and a facility's output is scaled by the **bottleneck** `clamp(supplied/demanded, 0, 1)` across only the forms its recipe requires — a form with a zero requirement does not constrain it. So a water/biomass-powered recipe is unaffected by an electricity shortage and an electricity-only recipe is unaffected by a fuel shortage. Supply by form:
  - **Electricity:** renewables (`SolarPotential`/`WindPotential`/`Geothermal` deposit capacity + hydro from `river_flow_regime`) matter-free; the shortfall by **burning fossil** (`crude_oil`/`natural_gas`/`thermal_coal`, in that order). Emits the `electricity` good (net supply ≈ 0; `demand_buffer` carries consumption for pricing).
  - **Mechanical:** water (`river_flow_regime`) and wind (`WindPotential`) direct-drive — the same non-depleting renewable flows; the shortfall by **burning fuel** (steam).
  - **Process heat:** by **burning fuel** only (no matter-free heat).
  - **Fuel** for the mechanical/heat shortfalls is burnt biomass then fossil: `wood_chips`/`softwood_logs`/`hardwood_logs` (at `biomass_mwh_per_fuel_unit`) then `crude_oil`/`natural_gas`/`thermal_coal` (at `fossil_mwh_per_fuel_unit`).
  - **Conservation across forms:** all combustion draws the **one** province fuel stock, sequenced (electricity → mechanical → heat) through the shared supply scratch, so burnt matter is never double-counted and each burn emits `supply_delta < 0`. Renewable flows (sun/wind/water) are free and non-depleting. A province with no flows and no fuel is power-poor (a real comparative disadvantage), and a recipe needing no power (muscle only — all three requirements zero) is never throttled. **Emergent gating:** electricity exists only where generation capacity (renewables or fuel) does; there is no calendar/era cutoff for power — `era_available` gates recipe availability only.
- **Waste / byproducts (conservation).** Each output good emits waste proportional to its throughput, typed by the output good's catalog **category**: petroleum/chemicals/pharmaceutical and electronics → `hazardous_waste`; heavy_industry/metals/geological/vehicles/construction/structural and food/agricultural/textiles/timber/biological → `industrial_waste`; services/financial/energy/waste → none. Waste is emitted as province supply (it accumulates and disperses via surplus decay until handled). Requires the goods catalog; with no catalog (some unit tests) no waste is emitted. Rates are `ProductionConfig::waste_rate_*`. (Civilian/municipal waste from consumption is emitted by `npc_spending`.)
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
- `test_shared_deposit_cannot_be_over_extracted`: Two mines over one 15-unit iron deposit that would each produce 10. Verify the tick's combined `supply_delta` for the ore and the combined `DepositDelta.quantity_extracted` both equal 15 (never 20), and that the two agree.
- `test_exhausted_deposit_yields_nothing_to_later_facility`: The first of two mines takes the whole remainder; verify the second emits no supply and no `DepositDelta`.
- `test_power_shortfall_scales_input_debit`: The same smelter at full power and at 0.5 power. Verify output halves AND the input debit halves, so `inputs consumed / output produced` is identical in both runs (no matter destroyed).
- `test_unstaffed_facility_consumes_no_inputs`: A facility with `worker_count == 0` emits no output supply and no input draw.
- `test_seasonal_agriculture_modifier_applied`: A farm business during drought season. Verify agricultural output modifier from `FarmSeasonState` reduces output proportionally to the seasonal stress factor.
