# Module: influence_network

## Purpose
Tracks the player's influence network across four distinct influence types (trust-based, fear-based, obligation-based, movement-based) and computes the InfluenceNetworkHealth composite indicator for the player dashboard. Applies obligation trust erosion for overdue open obligations. Clears stale movement_ally flags for NPCs whose movement participation has lapsed. Manages the Floor Principle: catastrophic trust loss events set permanent recovery ceilings on affected relationships, capping future trust rebuilding.

This module is a derived layer on top of the existing NPC relationship graph, obligation network, and movement tracking data. It does not create new influence relationships; it classifies existing data into influence types and produces a summary health score. Fear decay is explicitly NOT handled here; it runs via DeferredWorkQueue batched every 30 ticks per NPC. Runs at tick step 22 per TDD Section 13.

## Inputs (from WorldState)
- `player.relationships[]` — all player Relationship records:
  - `target_npc_id` — NPC this relationship is directed toward
  - `trust` (-1.0 to 1.0) — directional trust score
  - `fear` (0.0 to 1.0) — fear level this NPC has of the player
  - `obligation_balance` — positive = they owe player, negative = player owes them
  - `last_interaction_tick` — recency of last interaction
  - `is_movement_ally` (bool) — true if NPC is active movement collaborator
  - `recovery_ceiling` (0.0 to 1.0; default 1.0) — max recoverable trust after catastrophic loss
- `obligation_network[]` — all ObligationNode records:
  - `creditor_npc_id`, `debtor_npc_id` — parties to the obligation
  - `status` — ObligationStatus (Open, Fulfilled, Defaulted, Expired)
  - `current_demand` — current value of the obligation
  - `original_value` — value at creation (for normalization)
  - `due_tick` — when obligation becomes overdue
- `player.movement_follower_count` — integer count of background population NPCs in player-led movements
- `network_health_dirty` (bool) — flag set by any delta that touches relationships, obligations, or movement counts this tick
- `current_tick` — for overdue obligation detection and staleness checks
- Config constants from `simulation_config.json`:
  - `TRUST_CLASSIFICATION_THRESHOLD` = 0.4
  - `FEAR_CLASSIFICATION_THRESHOLD` = 0.35
  - `CATASTROPHIC_TRUST_LOSS_THRESHOLD` = -0.55
  - `CATASTROPHIC_TRUST_FLOOR` = 0.1
  - `RECOVERY_CEILING_FACTOR` = 0.60
  - `RECOVERY_CEILING_MINIMUM` = 0.15
  - `HEALTH_TARGET_COUNT` = 10

## Outputs (to DeltaBuffer)
- `PlayerDelta` — updated InfluenceNetworkHealth snapshot (only written if `network_health_dirty == true`):
  - `trust_relationship_count` — count of relationships where `trust > TRUST_CLASSIFICATION_THRESHOLD`
  - `fear_relationship_count` — count where `fear > FEAR_CLASSIFICATION_THRESHOLD` AND `trust < 0.2`
  - `obligation_held_count` — open nodes where player is creditor
  - `obligation_owed_count` — open nodes where player is debtor
  - `movement_follower_count`, `movement_ally_count`
  - `avg_trust_strength`, `avg_fear_strength`, `avg_obligation_leverage`
  - `movement_coverage_fraction` — `movement_follower_count / sum(active_region_populations)`
  - `composite_health_score` — `0.35 * trust + 0.25 * obligation + 0.20 * fear + 0.20 * movement` where each component = `min(1.0, type_count / HEALTH_TARGET_COUNT)` plus `diversity_bonus` (+0.05 if all four types have at least one active relationship)
  - `computed_at_tick` — set to `current_tick`
- `NPCDelta[]` — obligation trust erosion: for each overdue open obligation, `trust_delta = -0.001` applied to the creditor-debtor relationship per tick
- `RelationshipDelta[]` — stale `is_movement_ally` flags cleared for NPCs whose movement participation has lapsed
- `network_health_dirty` flag cleared after recomputation

## Preconditions
- `community_response` has completed (relationship changes from community effects are finalized).
- All `Relationship` fields are within valid ranges: `trust` in [-1.0, 1.0], `fear` in [0.0, 1.0], `recovery_ceiling` in [RECOVERY_CEILING_MINIMUM, 1.0].
- All `ObligationNode` records have valid status values and reference existing NPC IDs.
- Fear decay is NOT applied here (handled by DeferredWorkQueue every 30 ticks per NPC at rate `config.npc.fear_decay_rate` = 0.002/tick).

## Postconditions
- If `network_health_dirty` was true: `InfluenceNetworkHealth` recomputed from current WorldState and written to `PlayerDelta`; dirty flag cleared.
- If `network_health_dirty` was false: no `InfluenceNetworkHealth` write (skip recomputation entirely).
- All overdue obligation nodes (where `due_tick < current_tick` AND `status == Open`) have had trust erosion of -0.001 applied to the relevant relationship.
- Stale `is_movement_ally` flags cleared for NPCs not participating in any active movement.
- Recovery ceilings enforced: any trust-increasing delta this tick capped at `recovery_ceiling` for relationships that previously experienced catastrophic trust loss.

## Invariants
- Trust clamped to [-1.0, 1.0] after all adjustments.
- Fear clamped to [0.0, 1.0]. Fear is never modified by this module.
- `recovery_ceiling` in [RECOVERY_CEILING_MINIMUM (0.15), 1.0]; default 1.0 (no ceiling).
- Recovery ceiling is permanent once set and monotonically non-increasing: it can only be lowered by subsequent catastrophic losses, never raised.
- Catastrophic trust loss detection: triggers when trust delta in a single tick exceeds `CATASTROPHIC_TRUST_LOSS_THRESHOLD` (-0.55) AND resulting trust falls below `CATASTROPHIC_TRUST_FLOOR` (0.1). On trigger: `recovery_ceiling = max(RECOVERY_CEILING_MINIMUM, trust_before_loss * RECOVERY_CEILING_FACTOR)`.
- Trust erosion from overdue obligations is -0.001 per tick (constant rate, not scaled by obligation value).
- Composite health score weights: trust 0.35, obligation 0.25, fear 0.20, movement 0.20. These are fixed and not config-backed.
- NOT province-parallel: influence network is global scope spanning all provinces and nations.
- Same seed + same inputs = identical output regardless of core count.

## Failure Modes
- Player has no relationships: `InfluenceNetworkHealth` returns zero counts across all types; `composite_health_score = 0.0`.
- ObligationNode references invalid NPC ID (NPC dead or fled): skip trust erosion for that node, log warning, continue processing.
- `network_health_dirty` flag stuck true across multiple ticks: recomputation runs every tick (performance safe at O(R + O) but wasteful; monitoring recommended).
- Player has no movement followers: movement component of health score = 0.0 (valid state for non-movement players).
- Obligation with `current_demand = 0.0` and `original_value = 0.0`: skip normalization (division by zero guard), set leverage to 0.0.

## Performance Contract
- NOT province-parallel: runs on main thread.
- Target: < 3ms total at 200 active player relationships and 50 obligation nodes.
- O(R + O) where R = player relationship count, O = obligation node count.
- `InfluenceNetworkHealth` recomputation is skipped when `network_health_dirty == false` (expected on most ticks); cost on clean ticks is O(O) for obligation erosion scan only.
- Must not exceed tick budget share that would push total tick above 200ms target.

## Dependencies
- runs_after: ["community_response"]
- runs_before: ["regional_conditions"]

## Test Scenarios
- `test_health_recomputed_when_dirty`: Set `network_health_dirty = true` with 5 trust-based and 3 fear-based relationships. Verify `InfluenceNetworkHealth` written to delta with `trust_relationship_count = 5`, `fear_relationship_count = 3`, and valid composite score.
- `test_health_skipped_when_clean`: Set `network_health_dirty = false`. Verify no `InfluenceNetworkHealth` delta produced and module returns in < 0.1ms.
- `test_obligation_trust_erosion_applied`: Create overdue open obligation (`due_tick < current_tick`). Verify `trust_delta = -0.001` applied to the creditor-debtor relationship.
- `test_non_overdue_obligation_no_erosion`: Create open obligation with `due_tick > current_tick`. Verify no trust erosion applied.
- `test_catastrophic_trust_loss_sets_ceiling`: Apply trust delta of -0.60 to relationship with prior trust = 0.80 (delta exceeds -0.55, result 0.20 > CATASTROPHIC_TRUST_FLOOR but delta magnitude qualifies). Verify `recovery_ceiling = max(0.80 * 0.60, 0.15) = 0.48`.
- `test_trust_gain_capped_by_recovery_ceiling`: Set `recovery_ceiling = 0.48` on relationship with current trust = 0.30. Apply trust gain of +0.30. Verify trust capped at 0.48 (not 0.60).
- `test_default_ceiling_allows_full_recovery`: Set `recovery_ceiling = 1.0` (default). Apply trust gain pushing trust to 0.90. Verify trust = 0.90 with no capping.
- `test_trust_classification_threshold`: Relationship with `trust = 0.5` (above 0.4): classified as trust-based. Relationship with `trust = 0.3` (below 0.4): not classified as trust-based.
- `test_fear_classification_requires_low_trust`: Relationship with `fear = 0.4` and `trust = 0.1`: classified as fear-based. Relationship with `fear = 0.4` and `trust = 0.3`: NOT classified as fear-based (trust takes precedence).
- `test_stale_movement_ally_flag_cleared`: Set `is_movement_ally = true` for NPC not participating in any active movement. Verify flag cleared to false after module execution.
- `test_composite_health_diversity_bonus`: Set one relationship of each type (trust, fear, obligation, movement). Verify composite score includes +0.05 diversity bonus.
- `test_composite_health_no_diversity_bonus`: Set relationships of only trust and obligation types. Verify composite score does NOT include diversity bonus.
