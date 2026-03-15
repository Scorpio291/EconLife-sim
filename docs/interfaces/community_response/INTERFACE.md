# Module: community_response

## Purpose
Aggregates individual NPC state into four province-level community metrics (cohesion, grievance_level, institutional_trust, resource_access) using EMA smoothing, evaluates community response stage thresholds via DeferredWorkQueue scheduling, manages the 7-stage community response escalation state machine, and handles OppositionOrganization formation and lifecycle. This module is the primary feedback loop between player/NPC actions and collective community reaction -- all actors (player operations, NPC criminal orgs, NPC businesses) contribute to grievance at the same rate for the same action type.

The four community metrics are embedded in the `Region` struct as `CommunityState` fields and are updated in-place each tick via EMA smoothing (tick step 9). Stage evaluation is event-driven through `WorkType::community_stage_check` items in the DeferredWorkQueue, not polled every tick. The module also evaluates player intervention options (address_grievance, deflect, suppress, co_opt_leadership, ignore) and their consequences.

## Inputs (from WorldState)
- `significant_npcs` -- per-NPC: `social_capital`, `motivations[stability]` (for cohesion), memory log entries with negative `emotional_weight` (for grievance), `capital` (for resource_access), `risk_tolerance` (for opposition founding); iterated per province
- `provinces` -- `Region.community` fields (cohesion, grievance_level, institutional_trust, resource_access), `Region.base_institutional_trust`, `Region.corruption_index`, `Region.historical_trauma_index`; province demographic data
- `deferred_work_queue` -- `WorkType::community_stage_check` items fire per province (every 7 ticks normally, every 1 tick after stage change or grievance shock)
- `current_tick` -- used for EMA update scheduling and opposition formation timing
- `world_seed` -- used by `DeterministicRNG` for opposition member selection probability rolls

## Outputs (to DeltaBuffer)
- `RegionDelta.community.cohesion` -- EMA-smoothed update from NPC social_capital and stability motivation
- `RegionDelta.community.grievance_level` -- EMA-smoothed update from NPC negative memory attribution weights; direct jump on shock events
- `RegionDelta.community.institutional_trust` -- EMA-smoothed update from base trust, corruption, institutional successes/failures
- `RegionDelta.community.resource_access` -- EMA-smoothed update from NPC capital and social_capital
- `RegionDelta.community_response_stage` -- stage transitions (quiescent through sustained_opposition)
- New `OppositionOrganization` entries -- created when sustained_opposition stage is reached and no opposition org exists
- Consequence entries -- `ConsequenceType::opposition_org_formed` queued on opposition creation
- `BusinessDelta.revenue_modifier` -- `resistance_revenue_penalty` (default -0.15) applied to player businesses in provinces at stage 4 (economic_resistance) or higher
- DeferredWorkQueue entries -- reschedule `community_stage_check` at appropriate interval

## Preconditions
- NPC behavior module (tick step 7) has completed; NPC memory logs, social_capital, and motivations are settled for this tick.
- All province `Region.community` fields contain valid values from the previous tick (initialized at world load).
- `historical_trauma_index` is set at world load and does not change during simulation.
- All `WorkType::community_stage_check` items due this tick have been queued.

## Postconditions
- All four community metrics for every province have been updated via EMA smoothing exactly once this tick (tick step 9).
- Community response stage for provinces with due `community_stage_check` items has been evaluated. Stage is the highest stage whose threshold is fully satisfied. At most one stage transition per evaluation.
- Stage regression: at most one stage per 7 ticks minimum. `sustained_opposition` (stage 6) does not regress automatically once an `OppositionOrganization` is created.
- Grievance shock events (per-tick increase > `config.community.grievance_shock_threshold`) bypass EMA and apply directly; a `community_stage_check` is pushed at `current_tick + 1`.
- `resistance_revenue_penalty` is applied to all player businesses in affected provinces at stage 4+.

## Invariants
- EMA smoothing factor: `config.community.ema_alpha` (default 0.05). All four metrics use the same alpha. ~20 ticks to substantially shift.
- Cohesion sample per NPC: `clamp(npc.social_capital / config.community.social_capital_max, 0.0, 1.0) * clamp(npc.motivations[stability], 0.0, 1.0)`.
- Grievance contribution per NPC: sum of `abs(entry.emotional_weight) * action_type_weight(entry.type)` for negative attribution memory entries where `decay > config.community.memory_decay_floor`. Action type weights: direct harm types (witnessed_illegal_activity, witnessed_safety_violation, witnessed_wage_theft, physical_hazard, retaliation_experienced) = 1.0; economic harm types (employment_negative, facility_quality) = 0.5; all others = 0.0.
- Institutional trust target: `clamp(base_trust - corruption_index * corruption_trust_penalty + successes * trust_success_bonus - failures * trust_failure_penalty, 0.0, 1.0)`.
- Resource access sample per NPC: `clamp(npc.capital / config.community.capital_normalizer + npc.social_capital / config.community.social_normalizer, 0.0, 1.0)`.
- Stage thresholds (all from `simulation_config.json -> community.stage_thresholds`): quiescent (grievance < 0.15 OR cohesion < 0.10), informal_complaint (grievance >= 0.15 AND cohesion >= 0.10), organized_complaint (grievance >= 0.28 AND cohesion >= 0.25), political_mobilization (grievance >= 0.42 AND institutional_trust >= 0.20), economic_resistance (grievance >= 0.56 AND resource_access >= 0.25), direct_action (grievance >= 0.70 AND cohesion >= 0.45), sustained_opposition (grievance >= 0.85 AND leadership_npc exists AND resource_access >= 0.35).
- Opposition founding score: `(npc.motivations[ideology] + npc.motivations[power] + npc.motivations[revenge]) * npc.social_capital * npc.risk_tolerance` for NPCs with attribution memory.
- `historical_trauma_index` sets a grievance floor (`trauma_grievance_floor_scale * index`, default scale 0.25) and institutional trust ceiling (`1.0 - trauma_trust_ceiling_scale * index`, default scale 0.30) at world load.
- Floating-point accumulations use canonical sort order (`npc_id` ascending within each province) for deterministic EMA inputs.
- Same seed + same inputs = identical community state regardless of core count.
- All random draws go through `DeterministicRNG`.

## Failure Modes
- Province with zero significant NPCs: community metrics remain at previous values (EMA update with zero samples is skipped). Stage evaluation proceeds using existing metric values.
- No NPC qualifies as opposition founder (no attribution memory entries): `OppositionOrganization` is not formed even at sustained_opposition stage. Stage remains at sustained_opposition; re-evaluated next check.
- NaN in EMA calculation: clamp all four metrics to [0.0, 1.0], log diagnostic.
- `community_stage_check` references an invalid `province_id`: log warning, skip, do not reschedule.

## Performance Contract
- Province-parallel execution for EMA metric aggregation (tick step 9); each province's NPC population is aggregated independently.
- Stage evaluation is sequential (fired from DeferredWorkQueue, one province at a time).
- Target: < 20ms total for EMA aggregation across ~2,000 NPCs in 6 provinces on 6 cores.
- Per-province stage evaluation: < 0.5ms average.
- Must not exceed tick budget share that would push total tick above 200ms target.

## Dependencies
- runs_after: ["npc_behavior"]
- runs_before: ["trust_updates", "political_cycle"]

## Test Scenarios
- `test_cohesion_ema_smoothing`: Set NPC social_capital and stability motivation values. Run 20 ticks. Verify cohesion converges toward the sample mean at the expected EMA rate (alpha = 0.05).
- `test_grievance_from_negative_memory`: NPCs with `witnessed_illegal_activity` memory entries (weight = 1.0) and `employment_negative` entries (weight = 0.5). Verify grievance_level reflects the weighted sum normalized by `grievance_normalizer`.
- `test_grievance_shock_bypasses_ema`: A single tick produces grievance increase > 0.15 (shock threshold). Verify grievance jumps directly instead of EMA smoothing, and a `community_stage_check` is pushed at `current_tick + 1`.
- `test_stage_transition_informal_to_organized`: Grievance rises from 0.20 to 0.30 with cohesion = 0.30. Verify stage transitions from `informal_complaint` to `organized_complaint` and stage check is rescheduled at `current_tick + 1`.
- `test_stage_cannot_skip`: Grievance jumps from 0.10 to 0.80 in one evaluation. Verify stage advances by at most one level per evaluation, not directly to `direct_action`.
- `test_stage_regression_rate_limited`: Stage at `direct_action`, grievance drops below threshold. Verify stage regresses by at most one level per 7 ticks.
- `test_sustained_opposition_no_auto_regress`: OppositionOrganization formed. Grievance drops below 0.85. Verify stage remains at `sustained_opposition`.
- `test_opposition_org_formation`: Province reaches sustained_opposition with qualified founding NPC (high ideology + social_capital + risk_tolerance + attribution memory). Verify OppositionOrganization is created with correct founding_npc_id and member_ids.
- `test_economic_resistance_revenue_penalty`: Province at stage 4 (economic_resistance). Verify player businesses in that province receive `resistance_revenue_penalty` (-0.15) revenue modifier.
- `test_historical_trauma_sets_floors_and_ceilings`: Province with `historical_trauma_index = 0.60`. Verify grievance_level floor = 0.15 and institutional_trust ceiling = 0.82 at world load.
- `test_intervention_address_grievance`: Player uses `address_grievance` intervention. Verify grievance drops by `config.community.grievance_address_rate` per tick.
- `test_province_parallel_determinism`: Run 50 ticks of community response with 6 provinces on 1 core and 6 cores. Verify bit-identical community metric values and stage states.
