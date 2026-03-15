# Module: regional_conditions

## Purpose
Aggregates all province-level indicators into the RegionConditions composite scores that drive downstream simulation systems. Computes stability_score, inequality_index, crime_rate, addiction_rate, criminal_dominance_index, formal_employment_rate, regulatory_compliance_index, and agricultural event scalars (drought_modifier, flood_modifier). These composite scores are consumed by NPC migration decisions, NPC business location decisions, random event probability weighting, the LOD system for province health assessment, and community response escalation.

Province-parallel: each province's condition aggregation is fully independent. Runs at tick step 20 per GDD Section 21. This module is the primary downstream consumer of political_cycle and influence_network outputs, combining their effects with community, healthcare, and demographic data into a unified province health snapshot.

## Inputs (from WorldState)
- `provinces[].conditions` — RegionConditions (current values to be updated):
  - `stability_score` (0.0-1.0) — previous tick's composite stability
  - `inequality_index` (0.0-1.0) — previous tick's income inequality measure
  - `crime_rate` (0.0-1.0) — previous tick's criminal activity rate
  - `addiction_rate` (0.0-1.0) — previous tick's addiction prevalence
  - `criminal_dominance_index` (0.0-1.0) — criminal revenue share of total economic activity
  - `formal_employment_rate` (0.0-1.0) — fraction of working-age population in declared employment
  - `regulatory_compliance_index` (0.0-1.0) — mean facility compliance
  - `drought_modifier` (0.0-1.0) — 1.0 = no drought; lower = active drought severity
  - `flood_modifier` (0.0-1.0) — 1.0 = no flood; lower = active flood severity
- `provinces[].community` — CommunityState:
  - `cohesion` (0.0-1.0)
  - `grievance_level` (0.0-1.0)
  - `institutional_trust` (0.0-1.0)
  - `resource_access` (0.0-1.0)
  - `response_stage` (0-6)
- `provinces[].political` — RegionalPoliticalState:
  - `approval_rating` (0.0-1.0)
  - `corruption_index` (0.0-1.0)
- `provinces[].cohort_stats` — RegionCohortStats:
  - `total_population` — sum across all cohorts
  - `mean_income` — weighted mean of cohort incomes
  - `gini_coefficient` (0.0-1.0) — income inequality measure
  - `aggregate_skill_supply` — mean skill availability
- `provinces[].healthcare` — HealthcareProfile:
  - `access_level`, `quality_level`, `capacity_utilisation`
- `provinces[].significant_npc_ids[]` — for per-province NPC iteration
- `significant_npcs[]` — NPC data for:
  - `criminal_sector` flag on associated NPCBusiness — for crime_rate calculation
  - Employment status — for formal_employment counting
  - AddictionState — for addiction_rate verification
- `npc_businesses[]` — per-province businesses for:
  - Revenue data (criminal vs. legitimate) — for criminal_dominance_index
  - Facility scrutiny meters — for regulatory_compliance_index
- `current_tick` — for recovery rate timing
- Config constants from `simulation_config.json -> regional_conditions`:
  - `STABILITY_RECOVERY_RATE` = 0.001 per tick
  - `EVENT_STABILITY_IMPACT` = -0.05 per instability event
  - `INFRASTRUCTURE_DECAY_RATE` = 0.0002 per tick
  - `INFRASTRUCTURE_COST_UNIT` = 100,000 per 0.01 infrastructure point
  - `drought_recovery_rate` — per-tick recovery toward 1.0
  - `flood_recovery_rate` — per-tick recovery toward 1.0

## Outputs (to DeltaBuffer)
- `ProvinceDelta[]` — updated RegionConditions per province:
  - `stability_score` — composite: `stability_score(t+1) = stability_score(t) + STABILITY_RECOVERY_RATE * (1 - stability_score(t)) - instability_events(t) * EVENT_STABILITY_IMPACT`
  - `inequality_index` — derived directly from `gini_coefficient` of current income distribution
  - `crime_rate` — from criminal NPC activity count, criminal business revenue, and criminal_dominance_index
  - `addiction_rate` — aggregated from NPC AddictionStates (stages dependent through terminal) normalized by population
  - `criminal_dominance_index` — `criminal_revenue(region, t) / total_economic_activity(region, t)` with quarterly EMA smoothing (alpha = 0.1)
  - `formal_employment_rate` — fraction of working-age population in declared, taxed employment
  - `regulatory_compliance_index` — `mean(1.0 - facility.scrutiny_meter.current_level)` across all non-criminal facilities
  - `drought_modifier` — recovery toward 1.0 at `drought_recovery_rate` when no active drought event
  - `flood_modifier` — recovery toward 1.0 at `flood_recovery_rate` when flood event clears

## Preconditions
- `political_cycle` has completed (political approval ratings and corruption indices are current).
- `influence_network` has completed (network health updates are finalized).
- `community_response` has completed (CommunityState fields are current).
- NPC addiction states are current from addiction module.
- Facility scrutiny meters are current from facility_signals module.
- RegionCohortStats are current (updated monthly via DeferredWorkQueue).

## Postconditions
- All RegionConditions fields updated to reflect current province state for this tick.
- `stability_score` is a composite reflecting overall province political and social health.
- `inequality_index` tracks income distribution via `gini_coefficient`.
- `crime_rate`, `addiction_rate`, `formal_employment_rate` reflect current NPC demographics.
- `criminal_dominance_index` reflects current criminal revenue share (per-tick value is authoritative; quarterly smooth provides trend).
- `regulatory_compliance_index` reflects current facility inspection status.
- Agricultural modifiers recovering monotonically toward 1.0 when no active weather event.
- All output values clamped to [0.0, 1.0].

## Invariants
- All RegionConditions fields clamped to [0.0, 1.0]. No field may be negative or exceed 1.0.
- `stability_score` recovers toward 1.0 at `STABILITY_RECOVERY_RATE` per tick when no instability events; degraded by `EVENT_STABILITY_IMPACT` per event (additive penalties).
- `criminal_dominance_index` and `formal_employment_rate` are inversely correlated over time (emergent from criminal territory expansion effects on NPCBusiness decisions, NOT mechanically enforced).
- `criminal_dominance_index` uses layered update cadence: per-tick from revenue ratio (authoritative), quarterly EMA smooth for trend data.
- `drought_modifier` and `flood_modifier` recover monotonically toward 1.0 when respective event is inactive; they never increase above 1.0.
- `inequality_index` equals `gini_coefficient` from cohort income distribution (standard Gini formula).
- `regulatory_compliance_index` excludes facilities with `criminal_sector == true` (criminal facilities are not expected to comply with regulations).
- Province-parallel execution: Province A's conditions do not depend on Province B.
- Floating-point accumulations use canonical sort order (`npc_id` ascending, then `business_id` ascending for revenue sums).
- Same seed + same inputs = identical condition output regardless of core count.

## Failure Modes
- Province with zero `total_population`: set all per-capita metrics to 0.0, `stability_score` to 0.5 (neutral default), log warning.
- Province with no facilities: `regulatory_compliance_index` = 1.0 (vacuously clean — no facilities to violate compliance).
- Province with no NPC businesses: `criminal_dominance_index` carries forward from previous tick (no new data).
- NaN in any input field: reset to safe default (0.5 for scores, 0.0 for rates), log error diagnostic.
- Division by zero in `criminal_dominance_index` denominator (`total_economic_activity == 0`): set to 0.0 (no economy = no criminal dominance).
- `gini_coefficient` returns NaN from degenerate income distribution (all zero): set `inequality_index` to 0.0.

## Performance Contract
- Province-parallel: each province processed independently on a separate worker thread.
- Target: < 5ms total across all 6 provinces on 6 cores.
- Per-province: < 1ms for ~330 NPCs and ~50 facilities.
- O(N + F) per province where N = NPC count, F = facility count.
- Two aggregation passes per province: one over NPCs (crime, employment, addiction), one over facilities (compliance, revenue).
- Must not exceed tick budget share that would push total tick above 200ms target.

## Dependencies
- runs_after: ["political_cycle", "influence_network"]
- runs_before: ["lod_system"]

## Test Scenarios
- `test_stability_recovery_toward_one`: Set `stability_score = 0.80` with no instability events. Run one tick. Verify `stability_score = 0.80 + 0.001 * (1.0 - 0.80) = 0.8002`.
- `test_stability_degraded_by_event`: Set `stability_score = 0.80` with one instability event. Verify `stability_score` decreased by `EVENT_STABILITY_IMPACT` (0.05) plus recovery.
- `test_inequality_from_gini`: Set `gini_coefficient = 0.45`. Verify `inequality_index = 0.45`.
- `test_crime_rate_from_criminal_npcs`: Place 10 NPCs associated with criminal businesses in a province of 100 total. Verify `crime_rate` reflects criminal activity fraction proportionally.
- `test_criminal_dominance_index_ratio`: Set criminal revenue = 200, total economic activity = 1000. Verify `criminal_dominance_index = 0.20`.
- `test_addiction_rate_from_npc_states`: Set 5 NPCs at dependent+ stage in province of 200. Verify `addiction_rate` reflects 5/200 = 0.025.
- `test_formal_employment_rate`: Set 80 formally employed NPCs out of 100 working-age. Verify `formal_employment_rate = 0.80`.
- `test_regulatory_compliance_excludes_criminal`: Set 3 non-criminal facilities with scrutiny at 0.0, 0.2, 0.4 and 1 criminal facility with scrutiny at 0.9. Verify `compliance = mean(1.0, 0.8, 0.6) = 0.80` (criminal excluded).
- `test_drought_recovery`: Set `drought_modifier = 0.50` with no active drought. Run one tick. Verify `drought_modifier` increased toward 1.0 by `drought_recovery_rate`.
- `test_flood_modifier_no_recovery_during_event`: Set `flood_modifier = 0.30` with active flood event. Verify `flood_modifier` does NOT recover toward 1.0 while event is active.
- `test_zero_population_safe_defaults`: Set `total_population = 0`. Verify all per-capita metrics = 0.0, `stability_score = 0.5`, no NaN or division by zero.
- `test_province_parallel_determinism`: Run 50 ticks of regional_conditions across 6 provinces on 1 core and 6 cores. Verify bit-identical RegionConditions output.
