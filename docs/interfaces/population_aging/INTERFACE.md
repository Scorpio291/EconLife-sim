# Module: population_aging

## Purpose
Manages demographic lifecycle processing: aging of significant NPCs, background population cohort transitions, birth rate modeling, mortality processing, education level drift, and cohort economic convergence. Updates PopulationCohort sizes from births, deaths, and inter-cohort aging. Converges cohort median_income toward regional wage market rates and cohort employment_rate toward provincial formal_employment_rate at monthly cadence. Recomputes province-level aggregates (total_population, mean_income, gini_coefficient) after any cohort change.

Province-parallel: each province's demographic processing is fully independent. Background population updates run at monthly cadence (most ticks are no-ops). Significant NPC age events (retirement, death from natural causes) run each tick for NPCs at age thresholds. Runs at tick step 18 per GDD Section 21.

## Inputs (from WorldState)
- `provinces[].significant_npc_ids[]` — NPC IDs assigned to each province for province-parallel dispatch
- `significant_npcs[]` — NPC structs for age-event processing:
  - `id` — unique NPC identifier
  - `health` (0.0-1.0) — health state affects mortality probability
  - `status` — NPCStatus; only `active` NPCs age-processed
  - `role` — NPCRole; role determines retirement behavior
  - `current_province_id` — province assignment
- `provinces[].cohort_stats` — RegionCohortStats:
  - `cohorts[]` — map of DemographicGroup to PopulationCohort:
    - `size` — headcount; updated annually from LOD 2 batch or demographic events
    - `median_income` — in-game currency per tick
    - `education_level` (0.0-1.0)
    - `employment_rate` (0.0-1.0)
    - `skill_supply` — map of SkillDomain to fraction
    - `political_lean` (-1.0 to 1.0)
    - `grievance_contribution` (0.0-1.0)
    - `addiction_prevalence` (0.0-1.0)
  - `total_population` — sum across all cohorts
  - `mean_income` — weighted mean of cohort incomes
  - `gini_coefficient` — income inequality measure
  - `aggregate_skill_supply`
- `provinces[].demographics` — RegionDemographics:
  - `total_population` — labour force denominator
  - `median_age` — affects birth and death rate baselines
  - `education_level` — province-level education for drift anchoring
- `provinces[].healthcare` — HealthcareProfile:
  - `access_level`, `quality_level` — affect death rates and birth survival rates
- `provinces[].conditions` — RegionConditions:
  - `stability_score` — instability increases mortality, decreases birth rates
  - `formal_employment_rate` — convergence target for cohort employment_rate
  - `addiction_rate` — affects cohort health and mortality
- `regional_wage_by_skill[]` — per-province wage market rates (from labor_market module, updated monthly) for income convergence target
- `current_tick` — for monthly and annual cadence checks
- Config constants from `simulation_config.json -> labor`:
  - `cohort_income_update_rate` = 0.05 per month
  - `cohort_employment_update_rate` = 0.02 per month

## Outputs (to DeltaBuffer)
- `ProvinceDelta[]` — per-province demographic updates:
  - `cohort_stats` — updated RegionCohortStats:
    - Cohort `size` changes from births (added to youth cohort), deaths (removed from relevant cohort), aging transitions between cohorts (annual)
    - `median_income` converged toward regional wage market rates: `income += cohort_income_update_rate * (target_wage - income)` per month
    - `employment_rate` converged toward `formal_employment_rate`: `rate += cohort_employment_update_rate * (target - rate)` per month
    - `education_level` slow drift toward province education level: capped at 0.01 per year
  - `total_population` — recomputed as `sum(cohort.size)` after any cohort change
  - `mean_income` — recomputed as weighted mean of cohort incomes by cohort size
  - `gini_coefficient` — recomputed from cohort income distribution using standard Gini formula
  - `aggregate_skill_supply` — recomputed as mean skill availability across all domains
- `NPCDelta[]` — for significant NPCs reaching age thresholds:
  - Age-related health degradation: `health_delta` proportional to age beyond baseline
  - Retirement transitions: `role` change for NPCs reaching retirement age
  - Death events: `status = NPCStatus::dead` for NPCs at end of natural lifespan (health-weighted probability)
- `DeferredWorkItem[]` — consequence entries for:
  - Named NPC promotion from background population when cohort events trigger (union organizer emerges, community leader steps forward)
  - Generational events (baby boom from high stability + high healthcare, population decline from low stability + low healthcare)

## Preconditions
- Healthcare module has completed (health outcomes affect death rate calculations).
- Province demographics and cohort_stats are initialized from world.json at game start.
- All PopulationCohort fields are within valid ranges (sizes non-negative, rates in [0.0, 1.0]).
- Monthly cadence: cohort field convergence updates occur at most once per 30 ticks.
- Annual cadence: population size changes from aging transitions occur once per 365 ticks.

## Postconditions
- PopulationCohort sizes reflect births, deaths, and aging transitions for this period.
- `median_income` converged toward regional wage rates at `cohort_income_update_rate` (monthly).
- `employment_rate` converged toward `formal_employment_rate` at `cohort_employment_update_rate` (monthly).
- `education_level` drift applied if annual boundary crossed (capped at 0.01).
- Province-level aggregates (`total_population`, `mean_income`, `gini_coefficient`, `aggregate_skill_supply`) recomputed immediately after any cohort change.
- Labour force adjusted for demographic changes (births add to future workforce, deaths subtract).
- Significant NPCs at age thresholds processed for retirement or death.
- No cohort `size` is negative after processing.

## Invariants
- `total_population == sum(cohort.size)` for all cohorts — recomputed after any change, never derived incrementally.
- Population `size` changes only on major events or at monthly/annual cadence (not per-tick for background population).
- `education_level` drift capped at 0.01 per year (slow structural variable; not responsive to short-term events).
- `median_income` convergence rate = `cohort_income_update_rate` (0.05 per month) — EMA-style convergence.
- `employment_rate` convergence rate = `cohort_employment_update_rate` (0.02 per month) — slower than income.
- All demographic rates non-negative. Birth rate, death rate >= 0.
- Gini coefficient computed using standard sorted-income formula: `gini = sum((2i - n - 1) * income_sorted[i]) / (n * sum(income_sorted[i]))` with incomes sorted ascending.
- Province-parallel execution: Province A's demographics do not depend on Province B.
- Floating-point accumulations use canonical sort order (`DemographicGroup` enum value ascending for cohort iteration).
- Same seed + same inputs = identical demographic output regardless of core count.

## Failure Modes
- Province with zero population: skip processing, log warning. No births, deaths, or convergence.
- Cohort with `size = 0`: skip convergence calculations for that cohort (no income or employment to converge).
- Death rate exceeding birth rate for extended period: population declines (valid game state, not an error; demographic collapse is a legitimate simulation outcome).
- NaN in income or employment rate: reset to province mean, log error diagnostic.
- Missing DemographicGroup in cohort map: initialize with defaults from province demographics and group-specific offsets. All 12 groups must always be present.
- Gini computation with all-zero incomes: set `gini_coefficient = 0.0` (perfect equality by default).

## Performance Contract
- Province-parallel: each province processed independently on a separate worker thread.
- Target: < 5ms total across all 6 provinces on 6 cores.
- Monthly cadence for convergence updates: most ticks are no-ops for background cohort processing.
- Annual cadence for population size changes from aging: extremely rare processing.
- O(cohorts) per province per monthly update = O(12) per province (one per DemographicGroup).
- Significant NPC age processing: O(N) per province but only fires for NPCs at age thresholds (sparse).
- Must not exceed tick budget share that would push total tick above 200ms target.

## Dependencies
- runs_after: ["healthcare"]
- runs_before: []

## Test Scenarios
- `test_monthly_income_convergence`: Set cohort `median_income = 100`, regional wage target = 150. Run 30 ticks (1 month). Verify `median_income` moved toward 150 by `0.05 * (150 - 100) = 2.5`.
- `test_monthly_employment_convergence`: Set cohort `employment_rate = 0.60`, `formal_employment_rate = 0.80`. Run 30 ticks. Verify `employment_rate` moved by `0.02 * (0.80 - 0.60) = 0.004`.
- `test_education_drift_capped`: Set `education_level = 0.50`, province education = 0.80. Run 365 ticks (1 year). Verify education change <= 0.01.
- `test_total_population_recomputed`: Change cohort sizes (add 100 to youth, remove 50 from retiree). Verify `total_population = sum(all cohort sizes)` exactly.
- `test_gini_recomputed_after_income_change`: Change cohort incomes to create higher disparity. Verify `gini_coefficient` increased appropriately.
- `test_npc_death_at_natural_lifespan`: Set significant NPC at maximum natural lifespan with `health = 0.1`. Verify death probability fires and `status = NPCStatus::dead` when random check passes.
- `test_npc_retirement_at_age_threshold`: Set NPC at retirement age. Verify role transition fires (e.g., `worker` -> removed from active workforce).
- `test_zero_population_skipped`: Set province `total_population = 0`. Verify no processing occurs and no errors.
- `test_non_monthly_tick_is_noop`: Run on tick that is not a month boundary. Verify no cohort convergence deltas produced; only NPC age processing runs.
- `test_birth_rate_affected_by_healthcare`: Set two provinces: one with `healthcare.access_level = 0.9`, one with `0.2`. Compare birth survival rates at annual tick. Verify higher healthcare produces higher net births.
- `test_province_parallel_determinism`: Run 365 ticks (1 year) of population_aging across 6 provinces on 1 core and 6 cores. Verify bit-identical cohort sizes, incomes, employment rates, and gini values.
