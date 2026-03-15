# Module: healthcare

## Purpose
Processes NPC health changes each tick: applies passive health recovery based on province healthcare quality, triggers treatment events for critically ill NPCs who can afford care, tracks hospital capacity utilization, degrades quality when overloaded, and computes labour force participation effects from sick leave. Province-parallel.

## Inputs (from WorldState)
- `provinces[].healthcare` — HealthcareProfile per province:
  - `access_level` (0.0-1.0) — 0 = no formal healthcare; 1 = universal coverage
  - `quality_level` (0.0-1.0) — affects recovery rate and treatment success
  - `cost_per_treatment` — game currency per NPC treatment event
  - `capacity_utilisation` (0.0-1.0) — above overload_threshold, quality degrades
- `provinces[].npcs[]` — NPC structs with:
  - `health` (0.0-1.0) — 0.0 = NPC death; 1.0 = full health (field added by TDD Section 26)
  - `capital` — cash available for treatment payments
  - `last_treatment_tick` — 0 = never treated
  - `status` — only active NPCs are processed
- `provinces[].demographics.labour_force` — total labour force count for sick leave fraction calculation
- `provinces[].conditions` — regional stability and addiction rate affecting health event frequency
- `current_tick` — for tracking treatment timing
- `config.healthcare.base_recovery_rate` — 0.001; health recovered per tick at full access and quality
- `config.healthcare.critical_health_threshold` — 0.30; below this, NPC seeks treatment if affordable
- `config.healthcare.treatment_health_boost` — 0.25; base health recovery per treatment event
- `config.healthcare.overload_threshold` — 0.85; above this capacity utilisation, quality degrades
- `config.healthcare.overload_quality_penalty` — 0.999; multiplicative quality decay per tick while overloaded
- `config.healthcare.labour_impairment_threshold` — 0.50; below this health, NPC counted as sick leave
- `config.healthcare.labour_supply_impact` — 0.80; multiplier on effective labour supply reduction from sick leave
- `config.healthcare.access_per_capacity_unit` — 0.05; access level added per hospital capacity unit built
- `config.healthcare.capacity_per_treatment` — 0.001; capacity utilisation increment per treatment event

## Outputs (to DeltaBuffer)
- `npc_deltas[]` — NPCDelta per affected NPC:
  - `capital_delta` (additive) — negative for treatment cost deduction (cost_per_treatment)
  - Health state changes written as NPC field updates:
    - Passive recovery: health += access_level * quality_level * base_recovery_rate (clamped to 1.0)
    - Treatment boost: health += treatment_health_boost * quality_level (when below critical threshold and can afford treatment)
  - `new_status` (replacement) — NPCStatus::dead when health reaches 0.0
  - `new_memory_entry` (append) — health_event memory when treatment occurs or health drops critically
- Province healthcare field updates:
  - `capacity_utilisation` incremented by capacity_per_treatment per NPC treated
  - `quality_level` multiplied by overload_quality_penalty when capacity_utilisation exceeds overload_threshold
- Labour force participation output (for cohort aggregation at tick step 17):
  - `effective_labour_supply` = labour_force * (1.0 - sick_leave_fraction * labour_supply_impact)
  - Where `sick_leave_fraction` = count(NPC where health < labour_impairment_threshold) / labour_force
- `region_deltas[]` — RegionDelta with stability_delta when widespread health crisis affects community stability
- `consequence_deltas[]` — health-related consequence events queued (health_event DeferredWorkItems for NPCs entering critical state)

## Preconditions
- financial_distribution has completed (NPC cash positions are current for treatment affordability checks).
- Province HealthcareProfile fields are initialized (access_level, quality_level, cost_per_treatment, capacity_utilisation all within valid ranges from world.json or facility construction).
- NPC health field exists and is in [0.0, 1.0] for all active NPCs at entry.
- capacity_utilisation has been reset or carried from previous tick (not accumulated unboundedly across ticks).

## Postconditions
- All active NPCs in each province have had passive health recovery applied.
- NPCs below critical_health_threshold who can afford treatment have received treatment_health_boost scaled by quality_level.
- NPC cash reduced by cost_per_treatment for each treatment event.
- Province capacity_utilisation reflects treatments performed this tick.
- Quality_level degraded by overload_quality_penalty if capacity_utilisation exceeded overload_threshold.
- NPCs whose health reached 0.0 have status set to dead.
- sick_leave_fraction and effective_labour_supply computed for downstream cohort aggregation.
- last_treatment_tick updated to current_tick for all NPCs who received treatment.

## Invariants
- NPC health clamped to [0.0, 1.0] after all adjustments.
- access_level clamped to [0.0, 1.0].
- quality_level clamped to [0.0, 1.0]; can only decrease from overload penalty within a tick (increases come from facility construction, not this module).
- capacity_utilisation in [0.0, 1.0].
- cost_per_treatment >= 0.0.
- NPC capital never goes negative from treatment (affordability checked before deduction: npc.cash >= cost_per_treatment).
- Treatment only occurs when access_level > 0 (no healthcare access = no treatment available regardless of cash).
- Province-parallel execution: Province A's healthcare does not depend on Province B.
- Same seed + same inputs = identical health output (deterministic).

## Failure Modes
- Province with access_level == 0: no passive recovery (recovery_rate = 0), no treatment available. NPCs in critical health deteriorate without intervention. This is a valid game state (no healthcare infrastructure).
- NPC with health already at 0.0 at entry: skip processing, ensure status is dead.
- capacity_utilisation exceeding 1.0 from many simultaneous treatments: clamp to 1.0, quality penalty already applied.
- HealthcareProfile with NaN fields: reset to defaults (access_level = 0.0, quality_level = 0.0, cost = 0.0, utilisation = 0.0), log error.
- Province with zero labour_force denominator: set sick_leave_fraction to 0.0 to avoid division by zero.

## Performance Contract
- Province-parallel: each province processed independently on a separate worker thread.
- Target: < 10ms total across all 6 provinces on 6 cores.
- O(N) per province in NPC count: one pass over all active NPCs for recovery and treatment, one counting pass for sick leave fraction.
- At 2,000 NPCs across 6 provinces (~333 per province): ~333 health checks per worker per tick.

## Dependencies
- runs_after: ["financial_distribution"]
- runs_before: [] (end of Pass 1 chain; no current-tick dependents. Labour participation output consumed by cohort aggregation in a later pass)

## Test Scenarios
- `test_passive_recovery_applies_correctly`: Set NPC health to 0.70 in a province with access_level 0.8 and quality_level 0.9. Run one tick. Verify health increased by 0.8 * 0.9 * 0.001 = 0.00072, resulting in health = 0.70072.
- `test_critical_npc_receives_treatment_when_affordable`: Set NPC health to 0.25 (below critical threshold 0.30) with cash = 1000 and province cost_per_treatment = 500. Run one tick. Verify health increased by treatment_health_boost * quality_level (0.25 * quality_level), cash decreased by 500, last_treatment_tick set to current_tick, and capacity_utilisation incremented by 0.001.
- `test_critical_npc_denied_treatment_when_broke`: Set NPC health to 0.20 with cash = 100 and province cost_per_treatment = 500. Run one tick. Verify no treatment boost applied, cash unchanged, only passive recovery applied.
- `test_no_recovery_without_healthcare_access`: Set province access_level to 0.0. Set NPC health to 0.50. Run one tick. Verify health unchanged (recovery_rate = 0.0 * quality * base_rate = 0.0) and no treatment available.
- `test_overload_degrades_quality`: Set province capacity_utilisation to 0.90 (above overload_threshold 0.85) and quality_level to 0.80. Run one tick. Verify quality_level reduced to 0.80 * 0.999 = 0.7992.
- `test_sick_leave_fraction_computed_correctly`: Set 10 NPCs in a province with labour_force = 100. Give 3 NPCs health below 0.50 (labour_impairment_threshold). Verify sick_leave_fraction = 3/100 = 0.03 and effective_labour_supply = 100 * (1.0 - 0.03 * 0.80) = 97.6.
- `test_npc_death_at_zero_health`: Set NPC health to 0.001 in a province with access_level 0.0 (no recovery, no treatment). Apply a health reduction that brings health to 0.0. Verify NPC status is set to dead.
