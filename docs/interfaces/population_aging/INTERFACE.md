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
  - `subsistence_surplus_ratio` — commons food signal (births/famine, Malthusian loop)
  - `food_store` — granary buffer; famine fires only once exhausted
  - `hardiness` — generational adaptation divisor on world-hazard mortality
  - `urban_population` — the town's actual headcount (sum of the urban cohorts,
    derived in `apply_cohort_stats_deltas` exactly as `total_population` is). Drives
    the disease-epidemic hazard (M6a) and the urban graveyard's crowding mortality.
  - `urban_capacity` — how many townsfolk the catchment could FEED (published by
    grain_logistics). With `specialist_fraction` — how many the harvest can SPARE —
    it sets the size migration steers the town toward: `min` of the two, because both
    are real and independent limits.
  - `specialist_fraction` — the non-farming stratum (published by subsistence)
  - `war_death_fraction` ([0,1]) — per-province EXTRA annual death fraction from war
    (real units: battle dead / people), published by the `warfare` module in the SAME
    annual tick (warfare declares `runs_before` population_aging, so publication and
    consumption never straddle a tick — the field needs no persistence). Consumed
    unconditionally in every era; 0 at peace; reset by the publisher on regime exit.
    Composed as an INDEPENDENT COMPETING RISK, not added to the environmental
    probability: `p_death = 1 - (1 - p_env) * (1 - p_war)`, which is exactly the
    addition of the two hazard RATES, so nothing is double-counted. With no
    environmental deaths a cohort loses precisely the published fraction, and a
    published 1.0 still annihilates it.

### The urban graveyard and land<->town migration (added 2026-07-28)

Before sanitation a town was a net consumer of people: crowding put the midden next to
the well, and London buried more than it baptised in almost every year of the 17th and
18th centuries while doubling in size, entirely on migrants walking in from the
countryside.

- Urban cohorts (`youth_urban`, `working_urban_*`, `retiree_urban`) carry an EXTRA
  annual death rate, `PopulationAgingModule::urban_crowding_rate(town, medicine, cfg)`.
  It is ADDITIVE, not a multiplier — crowding is its own cause of death, not an
  amplification of the rest — and saturates in town size, so it approaches the full
  rate without ever exceeding it. The era's tech `mortality_mult` releases it:
  sanitation and germ theory are what actually closed the grave.
- Births go to `youth_urban`/`youth_rural` in proportion to the WORKING-AGE population
  in each. (This was a flat 50/50 split, which drove any society toward a 50% urban
  composition regardless of what its land could feed.)
- `migrate_land_and_town` moves people between matched rural/urban cohorts, conserved
  head for head, closing `urban_migration_rate_per_year` of the gap to
  `min(urban_capacity, total_population * specialist_fraction)` each year — in either
  direction, so a society whose catchment fails de-urbanises. Retirees do not migrate.
  Commons regimes only: market-era urbanisation follows wages, which this does not
  model.

Together with subsistence counting townsfolk as non-farmers, urbanisation self-limits
at an emergent 3-5% across the world spectrum, with no clamp anywhere.

### Mortality composition (updated 2026-07-26 — rail retired)

Mortality is composed as an annual HAZARD RATE (expected deaths per person-year):
instability, addiction, famine, the world's hazard dials divided by generational
`hardiness`, and medicine each SCALE that rate. The annual death probability is then
the Poisson first-arrival `p = 1 - exp(-rate)`.

That conversion IS the bound: mortality approaches 100% and can never exceed it, so
nothing in the chain needs a cap. This replaced a `[hazard_mortality_min,
hazard_mortality_max]` = [0.15, 3.0] band on the hazard term, which was a
behaviour-shaping cap on finite values (the doctrine permits clamps only as
crash sentinels for non-finite values) and bound precisely during the multi-decade
maladaptation transient that is the deathworld-colonisation arc — a garden-adapted
people landing on a deathworld had its culling softened ~1.84x. `hardiness_floor`
remains, documented as a divide-by-zero sentinel only. Earth-normal mortality is
unchanged by construction (the maladaptation term is exactly 1.0 for an adapted
people); the rate→probability conversion alone moves Earth-normal deaths by −0.45%.
- `hazard_settings` — world hazard dials (disease/geology/radiation) for the M6a
  episodic and chronic mortality/fertility channels
- `technology.current_era` + `tech_effects_for_era().mortality_mult` / era regime —
  medicine release + the commons-era gate for pre-market demographics
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
- Does NOT write `grievance_level`. This module previously injected `+0.003 × income_low_fraction` into province grievance every tick — a constant, never-decaying pump off a static demographic, disconnected from current conditions. Economic deprivation is now grounded in `community_response`'s material grievance term (unemployment + inequality); grievance has a single owner. The per-cohort `grievance_contribution` field remains a demographic descriptor, not a province-grievance writer.

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
