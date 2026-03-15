# Module: seasonal_agriculture

## Purpose
Manages crop growth cycles (fallow -> planting -> growing -> harvest) for annual-cycle farms and applies seasonal yield multipliers for continuous-output agricultural facilities (perennial trees, livestock, timber). Determines yields based on soil quality, climate stress, fertilizer efficiency, and weather modifiers. Province-parallel.

## Inputs (from WorldState)
- `provinces[].facilities[]` — Facility records with farm_season_state (FarmSeasonState) for annual-cycle crops; seasonal_yield_multiplier for continuous categories
- `provinces[].facilities[].farm_season_state` — FarmSeasonState: crop_category, current_phase, phase_started_tick, growing_season_start, growing_season_length, pending_harvest, base_growth_rate, harvest_remaining_ticks, seed_planted, years_same_crop
- `provinces[].facilities[].soil_health` — per-facility float [0.0, 1.0]; modified by fallow recovery and monoculture penalty
- `provinces[].facilities[].input_buffer` — seed_good_id availability for planting phase consumption
- `provinces[].conditions` — RegionConditions: drought_modifier [0.0, 1.0], flood_modifier [0.0, 1.0]
- `provinces[].climate` — climate_stress_current, KoppenZone (determines growing_season_start and length)
- `provinces[].geography` — latitude (sign determines Northern/Southern hemisphere for season offset)
- `provinces[].regional_markets[]` — RegionalMarket: supply field for harvest output delivery
- `current_tick` — current simulation tick (tick_of_year = current_tick % TICKS_PER_YEAR)
- `config.seasonal` — planting_duration_ticks (7), harvest_duration_ticks (14), fallow_soil_recovery_rate (0.003), monoculture_penalty_threshold (3 years), monoculture_soil_penalty_rate (0.002), southern_hemisphere_offset (182), perennial_base (0.85), perennial_amplitude (0.25)
- `config.seasonal.growing_calendar[koppen_zone][crop_category]` — {start_tick, length_ticks} lookup table

## Outputs (to DeltaBuffer)
- `MarketDelta` per (good_id, province_id): additive supply delta during harvest phase -- release_per_tick = pending_harvest / harvest_remaining_ticks spread over harvest_duration_ticks (14 ticks)
- `MarketDelta` per (good_id, province_id): additive supply delta each tick for continuous-output categories (perennial_tree, livestock, timber) scaled by seasonal_yield_multiplier
- FarmSeasonState updates (replacement): current_phase transitions, phase_started_tick, pending_harvest accumulation/depletion, harvest_remaining_ticks countdown, seed_planted flag, years_same_crop increment
- Facility.soil_health updates (replacement): recovery during fallow (+fallow_soil_recovery_rate per tick, capped at 1.0), degradation during growing if monoculture penalty active (-monoculture_soil_penalty_rate per tick, floored at 0.5)
- Facility.input_buffer deduction: seed_good_id consumed during planting phase transition to growing
- Facility.seasonal_yield_multiplier (replacement): monthly recomputation for perennial_tree, livestock, timber categories using cosine curve formula
- FarmSeasonState.annual_yield_last (replacement): set to pending_harvest at the moment of growing->harvest transition

## Preconditions
- Production module has completed: base facility output values are finalized.
- All agriculture-sector facilities with annual-cycle crops have farm_season_state present and initialized.
- growing_season_start and growing_season_length are derived from province KoppenZone + CropCategory at facility creation (Southern Hemisphere offset applied if latitude < 0).
- base_growth_rate is precomputed: recipe.base_output_per_tick * TICKS_PER_YEAR / growing_season_length_ticks.
- Facilities in zones with growing_season_length == 0 for a given crop were rejected at creation.
- RegionConditions (drought_modifier, flood_modifier) are current for this tick.

## Postconditions
- All annual-cycle farms have correct SeasonPhase for the current tick_of_year:
  - Farms at planting_start tick_of_year with fallow phase transition to planting.
  - Farms in planting with seed available transition to growing with seed consumed from input_buffer.
  - Farms in growing accumulate daily_growth into pending_harvest each tick.
  - Farms in growing that reach growing_season_length transition to harvest.
  - Farms in harvest release pending_harvest / harvest_remaining_ticks to RegionalMarket.supply each tick.
  - Farms in harvest that reach harvest_remaining_ticks == 0 transition to fallow with years_same_crop incremented.
  - Farms in fallow recover soil_health each tick.
- All continuous-output farms (perennial_tree, livestock, timber) have output scaled by current seasonal_yield_multiplier and climate modifiers.
- pending_harvest >= 0.0 for all farms after processing.
- No single-tick harvest burst: harvest is always spread over harvest_duration_ticks (14).

## Invariants
- Annual-cycle crops: annual_grain, annual_oilseed, annual_fiber, sugarcane. These use FarmSeasonState.
- Continuous-with-modifier crops: perennial_tree, livestock, timber. These use seasonal_yield_multiplier; no FarmSeasonState.
- pending_harvest >= 0.0 at all times.
- base_growth_rate >= 0.0 at all times.
- growing_season_start in [0, TICKS_PER_YEAR).
- If current_phase == harvest: harvest_remaining_ticks > 0.
- soil_health in [0.5, 1.0] (floored at 0.5 by monoculture penalty, capped at 1.0 by fallow recovery).
- Southern Hemisphere farms have growing_season_start offset by 182 ticks from Northern baseline.
- Harvest is spread over harvest_duration_ticks (14) to prevent single-tick price crash artifacts.
- Same seed + same inputs = identical agricultural output (deterministic).
- Seasonal price variation is emergent from RegionalMarket equilibrium pricing reacting to supply bursts and inter-harvest depletion; no separate pricing formula is introduced.
- Crop rotation (set_crop_category action) resets years_same_crop to 0, eliminating monoculture penalty.

## Failure Modes
- Seed unavailable during planting phase: farm stays in planting, never transitions to growing. No harvest occurs this year. Generates a supply gap that produces a price spike at the expected harvest window.
- Severe drought (drought_modifier near 0.0): daily_growth approaches 0; pending_harvest at harvest time is minimal; supply shortfall causes price spike.
- Severe flood (flood_modifier near 0.0): same effect as severe drought on daily_growth.
- Facility with growing_season_length == 0 for its crop in its zone: rejected at facility creation; should never reach tick processing. If encountered, log error and skip.
- Monoculture soil degradation: soil_health reaches floor of 0.5 after sustained same-crop planting, reducing yields by up to 50% from baseline.

## Performance Contract
- Province-parallel: each province's agricultural facilities processed by one worker thread.
- Per-facility cost: O(1) per tick (phase check + single arithmetic update).
- Full step at 2,000 NPCs, 6 provinces, ~50 goods with ~200 agricultural facilities: < 5ms target.
- No DeferredWorkQueue interaction; all processing is synchronous within tick step 1 (production).

## Dependencies
- runs_after: ["production"]
- runs_before: ["price_engine"]

## Test Scenarios
- `test_full_annual_cycle`: Create a grain farm in a temperate province (Cfa zone, growing_season_length = 200). Start in fallow. Advance through one full year. Verify phase transitions: fallow -> planting -> growing -> harvest -> fallow in correct order at correct tick_of_year boundaries.
- `test_planting_consumes_seed`: Create a farm entering planting phase with seed available in input_buffer. Verify seed_good_id is deducted from input_buffer and seed_planted is set to true on transition to growing.
- `test_seed_shortage_blocks_growth`: Create a farm entering planting phase with input_buffer[seed_good_id] = 0. Verify farm stays in planting phase for the entire planting window and never transitions to growing. Verify no harvest output occurs.
- `test_harvest_spread_over_14_ticks`: Create a farm transitioning to harvest with pending_harvest = 1400. Verify RegionalMarket.supply receives 100 units per tick for exactly 14 ticks, and pending_harvest reaches 0 at the end.
- `test_no_single_tick_harvest_burst`: Create a farm transitioning to harvest. Verify the maximum supply added in any single tick is pending_harvest / harvest_duration_ticks, not the full pending_harvest.
- `test_drought_reduces_yield`: Create two identical farms in the same province. Set drought_modifier = 1.0 for one run and 0.5 for another. Verify the drought run produces approximately 50% of the non-drought pending_harvest at harvest time.
- `test_monoculture_penalty_after_threshold`: Create a farm with years_same_crop = 4 (above monoculture_penalty_threshold of 3). Run through a growing season. Verify soil_health decreases by monoculture_soil_penalty_rate per tick during growing and that yield is reduced accordingly.
- `test_crop_rotation_resets_monoculture`: Set years_same_crop = 5 on a farm. Execute set_crop_category action to change the crop. Verify years_same_crop resets to 0 and monoculture penalty stops applying.
- `test_fallow_soil_recovery`: Create a farm in fallow phase with soil_health = 0.6. Advance 100 ticks. Verify soil_health = min(1.0, 0.6 + 100 * 0.003) = 0.9.
- `test_fallow_soil_recovery_capped`: Create a farm in fallow phase with soil_health = 0.99. Advance 10 ticks. Verify soil_health is capped at 1.0, not 0.99 + 10 * 0.003 = 1.02.
- `test_southern_hemisphere_offset`: Create two identical grain farms, one in a Northern Hemisphere province (latitude > 0), one in a Southern Hemisphere province (latitude < 0). Verify growing_season_start differs by exactly southern_hemisphere_offset (182 ticks).
- `test_perennial_tree_seasonal_multiplier`: Create a coffee (perennial_tree) facility. Verify seasonal_yield_multiplier follows a cosine curve peaking at the zone's peak_tick and ranges between perennial_base - perennial_amplitude (0.60) and perennial_base + perennial_amplitude (1.10).
- `test_livestock_near_constant_output`: Create a livestock facility. Verify seasonal_yield_multiplier stays in the 0.85-1.05 range across a full year, with minimal seasonal variation compared to perennial_tree.
- `test_timber_flat_multiplier`: Create a timber facility. Verify seasonal_yield_multiplier is constant 1.0 across all ticks (multi-year cycle abstracted to flat rate in V1).
- `test_seasonal_price_cycle_emergence`: Set up a province with 3+ grain farms, no imports, no trade routes. Run for 2 * TICKS_PER_YEAR. Verify RegionalMarket.spot_price(annual_grain, province) is at least 20% higher in the pre-harvest quarter than in the harvest quarter in each cycle.
- `test_supply_gap_price_spike`: Set up a grain farm in planting phase with input_buffer[seed_good_id] = 0 and no seed arriving. Run for TICKS_PER_YEAR. Verify farm never transitions to growing and spot_price at expected harvest window >= 1.35 * baseline.
- `test_deterministic_across_core_counts`: Run 365 ticks of seasonal agriculture on 1 core and 6 cores with identical seeds. Verify bit-identical pending_harvest, soil_health, and RegionalMarket.supply for all agricultural facilities.
