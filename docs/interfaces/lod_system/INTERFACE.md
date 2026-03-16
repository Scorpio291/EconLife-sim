# Module: lod_system

## Purpose
Manages Level of Detail simulation for nations and provinces across three fidelity tiers. LOD 0 (full simulation) runs the complete 27-step tick with full NPC models, detailed market clearing, scene card generation, and consequence processing for the player's home nation (6 provinces in V1). LOD 1 (simplified simulation) runs a monthly update with archetype-driven trade offers, aggregated production/consumption estimates, simplified climate stress, and technology tier advancement for trade-partner nations. LOD 2 (statistical simulation) runs an annual batch that computes the GlobalCommodityPriceIndex from aggregate production/consumption across all remaining nations. The LOD system enables global price signals without proportional compute cost.

In V1, LOD assignments are set at game start by the scenario file and do not change dynamically. The player's home nation is always LOD 0. Trade-partner nations specified in the scenario's `lod_1_defaults_by_start_province` are LOD 1. All other nations are LOD 2. Dynamic LOD promotion (LOD 1 to LOD 0) is expansion scope. Specified in TDD Sections 20 and 21.

## Inputs (from WorldState)
- `nations[]` — Nation structs:
  - `id` — nation identifier
  - `province_ids[]` — provinces belonging to this nation
  - `lod1_profile` — `std::optional<Lod1NationProfile>`: null = LOD 0 (player's home nation), populated = LOD 1
    - `export_margin` — markup over base production cost on exports
    - `import_premium` — premium nation pays above base cost for imports
    - `trade_openness` (0.0-1.0) — scales export quantity offered
    - `tech_tier_modifier` — multiplier on base production output
    - `population_modifier` — multiplier on base consumption
    - `research_investment` — accumulated R&D investment
    - `current_tier` (1-5) — current technology tier
    - `stability_delta_this_month` — change in stability this month
    - `climate_stress_aggregate` — accumulated national climate stress
    - `climate_vulnerability` (0.0-1.0) — scales climate stress delta
    - `geographic_centroid_lat`, `geographic_centroid_lon` — for LOD 1 transit time calculation
    - `lod1_transit_variability_multiplier` (1.0-1.3) — port congestion and customs delay variation
    - `archetype` — behavioral profile string: "aggressive_exporter", "protectionist", "resource_dependent", "industrial_hub"
- `provinces[].lod_level` — SimulationLOD enum (full = 0, simplified = 1, statistical = 2)
- `provinces[].conditions` — RegionConditions (for LOD 0 province assessment by downstream modules)
- `lod1_trade_offers[]` — NationalTradeOffer records from previous month (to be regenerated)
- `lod2_price_index` — GlobalCommodityPriceIndex pointer:
  - `lod2_price_modifier[]` — per-good price modifier
  - `last_updated_tick` — when LOD 2 batch last ran
- `lod1_national_stats` — `std::map<uint32_t, Lod1NationStats>` per LOD 1 nation
- `current_tick` — for monthly and annual cadence checks
- Goods registry — `base_production_cost[]`, `base_production[]`, `base_consumption[]` per good per nation
- `era_consumption_baseline_modifier[current_era]` — era-scaled consumption adjustment
- Config constants from `simulation_config.json`:
  - `TICKS_PER_MONTH` = 30
  - `TICKS_PER_YEAR` = 365
  - `lod1_stability_event_threshold` — stability delta triggering simplified political event
  - `lod2_min_modifier` = 0.50
  - `lod2_max_modifier` = 2.00
  - `lod2_smoothing_rate` = 0.30
  - `SUPPLY_FLOOR` — minimum production denominator to prevent division by zero
  - `tier_advance_cost[]` — research investment required per technology tier

## Outputs (to DeltaBuffer)
- **LOD 1 Monthly Update** (on monthly ticks, per LOD 1 nation):
  - `NationalTradeOffer[]` — regenerated trade offers:
    - Step 1: Estimate production per good: `production = base_production * tech_tier_modifier * (1.0 - climate_stress_penalty) * trade_openness`
    - Step 2: Estimate consumption per good: `consumption = base_consumption * population_modifier * era_consumption_baseline_modifier`
    - Step 3: Compute surplus/deficit per good: `surplus = production - consumption`
    - Step 4: Generate offers: export if `surplus > 0` at `price = base_cost * (1.0 + export_margin)`; import if `surplus < 0` at `price = base_cost * (1.0 + import_premium)`
  - `Lod1NationStats` — `production_by_good`, `consumption_by_good` written to `lod1_national_stats`
  - Climate stress delta from GlobalCO2Index applied to LOD 1 provinces
  - Technology tier advancement: `current_tier += 1` when `research_investment > tier_advance_cost[current_tier]`
  - Simplified political event queued when `abs(stability_delta_this_month) > lod1_stability_event_threshold`
- **LOD 2 Annual Batch** (on annual ticks, across all LOD 2 nations):
  - `GlobalCommodityPriceIndex` update per good:
    - Aggregate production and consumption across all LOD 2 nations
    - `ratio = aggregate_consumption / max(aggregate_production, SUPPLY_FLOOR)`
    - `raw_modifier = clamp(ratio, lod2_min_modifier, lod2_max_modifier)`
    - `smoothed_modifier = lerp(old_modifier, raw_modifier, lod2_smoothing_rate)`
  - Era transition contribution: if global aggregate conditions meet era N+1 thresholds, queue era transition event
  - `last_updated_tick` set to `current_tick`

## Preconditions
- `regional_conditions` has completed (RegionConditions are current for all LOD 0 provinces).
- LOD assignments are set from scenario file at game start; stored in `Province.lod_level` and `Nation.lod1_profile`.
- Base production and consumption data loaded from world.json (World Bank year 2000 baselines via GIS pipeline).
- GlobalCO2Index is available for climate stress delta calculations.
- Goods registry is loaded with `base_production_cost` data for trade offer pricing.
- `tier_advance_cost[]` array loaded from config for technology advancement checks.

## Postconditions
- On monthly ticks: all LOD 1 nations have completed their monthly update — trade offers regenerated, production/consumption estimated, climate stress updated, technology advancement checked, political stability evaluated.
- On annual ticks: LOD 2 batch has updated GlobalCommodityPriceIndex with smoothed price modifiers; era transition conditions evaluated.
- On non-cadence ticks: module produces zero deltas (immediate early-return).
- LOD level assignments are unchanged in V1 (dynamic transitions are expansion scope).
- `lod1_national_stats` for each LOD 1 nation contains current production and consumption estimates.

## Invariants
- LOD 0 provinces run the full 27-step tick. They are NOT processed by this module. LOD 0 province data is produced by all other tick modules.
- LOD 1 and LOD 2 provinces do NOT run individual NPC simulation, market clearing, political cycles, scene cards, or consequence generation.
- LOD 1 monthly update runs exactly once per `TICKS_PER_MONTH` ticks per LOD 1 nation.
- LOD 2 annual batch runs exactly once per `TICKS_PER_YEAR` ticks across all LOD 2 nations.
- `lod2_price_modifier[]` values clamped to [`lod2_min_modifier` (0.50), `lod2_max_modifier` (2.00)] per good.
- `lod2_smoothing_rate` (0.30) ensures 30% weight to new annual reading, 70% to old — prevents sharp year-on-year price jumps.
- Trade offer prices are always positive: `base_production_cost > 0` by goods registry invariant.
- LOD 1 archetype behavioral parameters (`export_margin`, `import_premium`, `trade_openness`) are set from the nation data file and immutable at runtime.
- Player's home nation is always LOD 0. Cannot be demoted. `lod1_profile` is always null for this nation.
- V1: LOD level assignments do not change at runtime. Dynamic LOD promotion is expansion scope.
- Nations processed in `nation_id` ascending order for deterministic output.
- NOT province-parallel: LOD updates span multiple nations and require global commodity aggregation.
- Same seed + same inputs = identical LOD outputs regardless of execution environment.

## Failure Modes
- LOD 1 nation with no base production data in goods registry: generate empty trade offers (zero exports), full deficit imports at `import_premium` pricing. Log warning.
- LOD 2 aggregate production for a good is zero: use `SUPPLY_FLOOR` as denominator. Price modifier = `lod2_max_modifier` (maximum scarcity signal). Log info (valid for goods not produced in LOD 2 world).
- LOD 1 nation with missing archetype string: default to "industrial_hub" profile (moderate parameters). Log warning.
- GlobalCO2Index unavailable (first tick before climate system initializes): skip climate stress delta for LOD 1/2 this period. Log warning.
- NaN in price modifier from extreme consumption/production ratio: clamp to `lod2_max_modifier`, log error diagnostic.
- Province with `lod_level` mismatch vs. nation profile (e.g., LOD 0 province in LOD 1 nation): log error, treat province according to nation's LOD level.
- `tier_advance_cost` array index out of bounds (nation at max tier): no advancement; log info.

## Performance Contract
- NOT province-parallel: LOD 1 and LOD 2 updates run on main thread.
- Monthly LOD 1 update target: < 5ms total for up to 20 LOD 1 nations.
- Annual LOD 2 batch target: < 10ms total for up to 100 LOD 2 nations.
- Non-cadence ticks: < 0.1ms (immediate early-return on cadence check failure).
- O(N_lod1 * G) per monthly update where G = goods count (~50 in V1).
- O(N_lod2 * G) per annual batch.
- Must not exceed tick budget share that would push total tick above 200ms target.

## Dependencies
- runs_after: ["regional_conditions"]
- runs_before: []

## Test Scenarios
- `test_lod1_monthly_trade_offer_export`: Set LOD 1 nation with production surplus of 100 units for steel. Run on monthly tick. Verify export offer generated with `quantity = 100 * trade_openness` and `price = base_cost * (1.0 + export_margin)`.
- `test_lod1_monthly_trade_offer_import`: Set LOD 1 nation with consumption deficit of 50 units for electronics. Run on monthly tick. Verify import offer generated with `quantity = 50` and `price = base_cost * (1.0 + import_premium)`.
- `test_lod1_non_monthly_tick_noop`: Run LOD 1 nation on non-monthly tick. Verify zero deltas produced and module returns in < 0.1ms.
- `test_lod1_tech_tier_advancement`: Set LOD 1 nation with `research_investment = 1000` exceeding `tier_advance_cost[2] = 800`. Verify `current_tier` incremented from 2 to 3.
- `test_lod1_archetype_aggressive_exporter`: Set nation archetype to "aggressive_exporter" (`export_margin = 0.05`, `trade_openness = 0.8`). Verify export offers have low markup (1.05x) and high volume (80% of surplus).
- `test_lod1_archetype_protectionist`: Set nation archetype to "protectionist" (`trade_openness = 0.3`). Verify export offer quantities scaled down to 30% of raw surplus.
- `test_lod2_annual_price_index_scarcity`: Set LOD 2 aggregate consumption = 1500, production = 1000 for a good. Verify `ratio = 1.5`, `raw_modifier` clamped within [0.50, 2.00], smoothed from previous value at `lod2_smoothing_rate`.
- `test_lod2_annual_price_index_surplus`: Set LOD 2 aggregate production = 2000, consumption = 1000. Verify `ratio = 0.5`, modifier reflects surplus (price decrease signal).
- `test_lod2_supply_floor_prevents_divide_by_zero`: Set LOD 2 aggregate production = 0 for a good with consumption = 100. Verify `SUPPLY_FLOOR` used as denominator and modifier clamped to `lod2_max_modifier`.
- `test_lod2_smoothing_prevents_sharp_jump`: Set previous `lod2_price_modifier = 1.0`, new raw modifier = 2.0. Verify smoothed = `lerp(1.0, 2.0, 0.30) = 1.30`.
- `test_lod2_non_annual_tick_noop`: Run LOD 2 batch on non-annual tick. Verify zero deltas produced.
- `test_lod0_provinces_not_processed`: Verify LOD 0 provinces produce zero deltas from this module (processed by full 27-step tick, not by LOD system).
