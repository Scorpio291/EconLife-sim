# Module: addiction

## Purpose
Processes NPC substance addiction mechanics each tick: evaluates stage transitions through the seven-stage addiction state machine (none, casual, regular, dependent, active, recovery, terminal), applies tolerance buildup, craving increments, withdrawal health damage, work efficiency penalties, and recovery tracking. At the regional level, aggregates addiction rates that feed back into labour force participation, healthcare capacity utilization, and community grievance. Addiction is a consequence system — the drug supply chain creates addicts; the addiction system feeds systemic effects back into social and economic metrics.

Province-parallel: each province's NPC addiction processing is fully independent. NPCs are processed in `npc_id` ascending order within each province. Specified in TDD Section 27.

## Inputs (from WorldState)
- `provinces[].significant_npc_ids[]` — NPC IDs assigned to each province for province-parallel dispatch
- `significant_npcs[]` — NPC structs (indexed by province assignment):
  - `addiction_state` — AddictionState struct:
    - `stage` — AddictionStage enum (none, casual, regular, dependent, active, recovery, terminal)
    - `substance_key` — string key matching goods data (e.g., "cocaine", "methamphetamine", "opioid_pills")
    - `tolerance` (0.0-1.0) — rises with use; drives dose escalation
    - `craving` (0.0-1.0) — current craving level; drives stage transitions
    - `consecutive_use_ticks` — ticks of continuous substance use
    - `clean_ticks` — ticks without substance use (recovery tracking)
    - `supply_gap_ticks` — ticks without supply when dependent or higher
    - `relapse_probability` — computed: craving * history_weight; checked each tick during recovery
  - `health` (0.0-1.0) — for withdrawal damage and terminal stage detection
  - `status` — NPCStatus; only `active` NPCs processed
  - `capital` — cash available for substance purchases
  - `current_province_id` — province NPC is physically in
- `provinces[].conditions` — RegionConditions:
  - `addiction_rate` (0.0-1.0) — current fraction of province adult population at stages dependent through terminal
- `provinces[].demographics` — RegionDemographics:
  - `total_population` — for per-capita rate calculations
- `provinces[].healthcare` — HealthcareProfile:
  - `capacity_utilisation` (0.0-1.0) — for healthcare load contribution
  - `access_level`, `quality_level` — affect treatment success rates
- `provinces[].community` — CommunityState:
  - `grievance_level` — for grievance contribution from addiction burden
- `current_tick` — for timing calculations and cadence checks
- Config constants from `simulation_config.json -> addiction`:
  - `tolerance_per_use_casual` = 0.05
  - `regular_use_threshold_ticks` = 30
  - `dependency_threshold_ticks` = 90
  - `dependency_tolerance_floor` = 0.30
  - `active_craving_threshold` = 0.70
  - `active_duration_ticks` = 60
  - `withdrawal_health_hit` = 0.005 per tick
  - `dependent_work_efficiency` = 0.70
  - `recovery_attempt_threshold_ticks` = 14
  - `craving_decay_rate_recovery` = 0.003 per tick
  - `full_recovery_ticks` = 365
  - `recovery_success_threshold` = 0.05
  - `terminal_health_threshold` = 0.15
  - `terminal_persistence_ticks` = 90
  - `rate_delta_per_active_npc` = 0.001
  - `labour_impact_per_addict` = 0.80
  - `healthcare_load_per_addict` = 0.50
  - `grievance_per_addict_fraction` = 0.30

## Outputs (to DeltaBuffer)
- `NPCDelta[]` — per affected NPC:
  - `addiction_state` (replacement) — updated AddictionState with new stage, tolerance, craving, tick counters
  - `health_delta` (additive) — withdrawal damage: `-withdrawal_health_hit` per tick without supply when at dependent stage or higher
  - `work_efficiency_delta` (multiplicative) — `dependent_work_efficiency` (0.70) at dependent stage; further degraded at active and terminal
  - `new_memory_entry` (append) — `addiction_impairs_function` memory at active stage entry (one-time)
  - `status` (replacement) — `NPCStatus::dead` when terminal stage health failure confirmed
  - `capital_delta` (additive) — substance purchase cost deducted when NPC acquires supply
- `ProvinceDelta[]` — per province:
  - `conditions.addiction_rate_delta` (additive) — `+= rate_delta_per_active_npc` per NPC at active stage; net change from stage transitions
  - `healthcare.capacity_utilisation_delta` (additive) — `+= addiction_rate * healthcare_load_per_addict`
  - `community.grievance_level_delta` (additive) — `+= addiction_rate * grievance_per_addict_fraction`
- Labour force impact (written to province demographics):
  - `effective_labour_supply` reduced by `addiction_rate * labour_impact_per_addict`

## Preconditions
- `community_response` has completed (community state is current for grievance calculations).
- NPC `AddictionState` fields are initialized (stage, substance_key, tolerance, craving all within valid ranges).
- Substance goods exist in the goods registry (keyed by `substance_key` string identifier).
- NPC `health` field is current from healthcare module processing (tick step 8).
- Regional market data available for substance supply availability checks.

## Postconditions
- All active NPCs with `AddictionState.stage != none` have had their addiction state machine stepped exactly once.
- Stage transitions applied based on craving thresholds and consecutive_use/clean tick counts per TDD Section 27 transition rules.
- Withdrawal health damage applied for all NPCs at dependent stage or higher who have `supply_gap_ticks > 0`.
- Craving increments applied per stage (casual: +0.01, regular: +0.02, dependent: +0.03, active: +0.05, recovery: -0.003).
- Recovery craving decay applied for NPCs in recovery stage at `craving_decay_rate_recovery` per tick.
- Relapse checks performed each tick for NPCs in recovery: `random_float(0,1) < relapse_probability` triggers return to active.
- Province `addiction_rate` updated from aggregate NPC addiction states.
- Healthcare capacity and community grievance adjusted for addiction burden.
- Terminal NPCs with `health < terminal_health_threshold` for `terminal_persistence_ticks` consecutive ticks have `status` set to `dead`.

## Invariants
- AddictionStage transitions follow the state machine graph:
  - Forward: none -> casual -> regular -> dependent -> active -> recovery -> none (success)
  - Relapse: recovery -> active
  - Terminal: dependent/active -> terminal (when health conditions met)
  - No stage skipping: one transition per evaluation maximum
- `tolerance` clamped to [0.0, 1.0].
- `craving` clamped to [0.0, 1.0].
- `health` clamped to [0.0, 1.0] after withdrawal damage.
- `addiction_rate` clamped to [0.0, 1.0] per province.
- NPC `capital` never goes negative from substance purchases (affordability checked before deduction; insufficient funds = supply_gap).
- Craving transition thresholds: casual -> regular requires `craving >= 0.3`; regular -> dependent requires `craving >= 0.7`; dependent -> active requires `craving >= active_craving_threshold` (0.70) sustained for `active_duration_ticks` (60).
- Province-parallel execution: Province A's addiction processing does not depend on Province B.
- All random draws (relapse checks) use `DeterministicRNG(world_seed, current_tick, npc_id)`.
- Floating-point accumulations use canonical sort order (`npc_id` ascending within each province).
- Same seed + same inputs = identical addiction output regardless of core count.

## Failure Modes
- NPC with `AddictionState.stage == none`: skip processing for that NPC (not an error; most NPCs are not addicts).
- `substance_key` not found in goods registry: log warning, treat as `supply_gap` (no substance available this tick). NPC accumulates `supply_gap_ticks` and experiences withdrawal if dependent+.
- NPC health already at 0.0 at entry: skip processing, ensure status is `dead`. Log diagnostic if status was not already dead.
- Province with zero `total_population`: set all per-capita addiction metrics to 0.0 to avoid division by zero.
- `addiction_rate` exceeding 1.0 from many simultaneous NPC stage transitions: clamp to 1.0.
- NaN or negative in craving or tolerance from floating-point edge case: clamp to 0.0, log error diagnostic.

## Performance Contract
- Province-parallel: each province processed independently on a separate worker thread.
- Target: < 8ms total across all 6 provinces on 6 cores.
- Per-province: < 2ms for ~330 NPCs.
- O(N) per province in NPC count: single pass over all active NPCs for state machine stepping.
- At 2,000 NPCs across 6 provinces (~333 per province): ~333 addiction checks per worker per tick.
- Per-NPC state machine step: < 0.005ms average (branch-heavy but computationally trivial).
- Must not exceed tick budget share that would push total tick above 200ms target.

## Dependencies
- runs_after: ["community_response"]
- runs_before: []

## Test Scenarios
- `test_casual_to_regular_transition`: Set NPC at casual stage with `consecutive_use_ticks = 30` and `craving >= 0.3`. Verify transition to regular stage. Verify `consecutive_use_ticks` counter preserved.
- `test_regular_to_dependent_transition`: Set NPC at regular stage with `consecutive_use_ticks = 90`, `tolerance >= 0.30`, and `craving >= 0.7`. Verify transition to dependent stage.
- `test_withdrawal_health_damage`: Set NPC at dependent stage with `supply_gap_ticks = 5`. Verify health reduced by `0.005 * 5 = 0.025` over 5 ticks. Verify health clamped to [0.0, 1.0].
- `test_dependent_work_efficiency_penalty`: Set NPC at dependent stage. Verify `work_efficiency_delta = 0.70` written to NPCDelta.
- `test_active_stage_sustained_craving`: Set `craving = 0.75` (above 0.70 threshold) sustained for exactly 60 ticks. Verify transition to active stage at tick 60 and `addiction_impairs_function` memory entry created.
- `test_recovery_craving_decay`: Set NPC in recovery stage with `craving = 0.20`. Run 10 ticks. Verify craving decayed by `0.003 * 10 = 0.03` to approximately 0.17.
- `test_full_recovery_success`: Set NPC in recovery with `clean_ticks = 365` and `relapse_probability = 0.04` (below 0.05 threshold). Verify transition to none stage.
- `test_relapse_during_recovery`: Set NPC in recovery with `relapse_probability = 0.30`. Use deterministic RNG producing value = 0.20 (below 0.30). Verify transition back to active stage.
- `test_terminal_stage_entry`: Set NPC at active stage with health = 0.10 (below 0.15) sustained for 90 consecutive ticks. Verify transition to terminal stage.
- `test_terminal_death`: Set NPC at terminal stage. Continue running until health reaches 0.0. Verify `NPCStatus::dead` set.
- `test_province_addiction_rate_aggregation`: Place 3 NPCs at active stage in province with `total_population = 1000`. Verify `addiction_rate_delta += 3 * 0.001 = 0.003`.
- `test_tolerance_buildup_casual`: Set NPC at casual stage. Apply use event. Verify `tolerance += tolerance_per_use_casual` (0.05). Verify tolerance clamped to [0.0, 1.0].
