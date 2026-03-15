# Module: trust_updates

## Purpose
Processes NPC-to-NPC trust score changes each tick based on observed actions, fulfilled or broken promises, evidence discoveries, and behavioral patterns. This is the primary mechanism through which the simulation's relationship graph evolves. Trust deltas accumulate from multiple sources within a tick (business outcomes, criminal activity exposure, community events, personal interactions) and are applied atomically per relationship. Applies the Floor Principle: catastrophic trust losses permanently set recovery ceilings that cap future trust rebuilding.

Province-parallel: each province's NPC trust updates are fully independent. NPCs are processed in deterministic order (npc_id ascending) within each province. Cross-province trust effects from this tick propagate via CrossProvinceDeltaBuffer and take effect next tick. Runs at tick step 21 per GDD Section 21.

## Inputs (from WorldState)
- `provinces[].significant_npc_ids[]` — NPC IDs assigned to each province for province-parallel dispatch
- `significant_npcs[]` — NPC structs (indexed by province assignment):
  - `id` — unique NPC identifier
  - `relationships[]` — Relationship records with:
    - `target_npc_id` — other party in the relationship
    - `trust` (-1.0 to 1.0) — current directional trust score
    - `fear` (0.0 to 1.0) — current fear level (NOT modified by this module)
    - `recovery_ceiling` (default 1.0) — max trust achievable after catastrophic loss
    - `last_interaction_tick` — recency tracking
    - `shared_secrets[]` — evidence token IDs both parties hold
  - `known_evidence` — KnowledgeMap of evidence tokens the NPC is aware of
  - `known_relationships` — KnowledgeMap of observed actor-to-actor relationships
  - `memory_log[]` — MemoryEntry records with tick_timestamp, type, subject_id, emotional_weight, decay
  - `status` — NPCStatus; only `active` NPCs are processed
  - `current_province_id` — province this NPC is physically in
- `provinces[].community` — CommunityState:
  - `institutional_trust` — for system-level trust aggregation updates
  - `grievance_level` — context for trust-damaging events
- `current_tick` — for memory recency weighting and timing-based calculations
- Config constants from `simulation_config.json`:
  - `CATASTROPHIC_TRUST_LOSS_THRESHOLD` = -0.55
  - `CATASTROPHIC_TRUST_FLOOR` = 0.1
  - `RECOVERY_CEILING_FACTOR` = 0.60
  - `RECOVERY_CEILING_MINIMUM` = 0.15

## Outputs (to DeltaBuffer)
- `NPCDelta[]` — per affected NPC, per affected relationship:
  - `trust_delta` (additive) — net trust change from all sources this tick for this relationship pair
  - `recovery_ceiling` (replacement) — set when catastrophic trust loss occurs:
    - Triggers when `trust_delta <= CATASTROPHIC_TRUST_LOSS_THRESHOLD` (-0.55) OR when resulting trust drops below `CATASTROPHIC_TRUST_FLOOR` (0.1) from a large negative delta
    - `recovery_ceiling = max(pre_loss_trust * RECOVERY_CEILING_FACTOR, RECOVERY_CEILING_MINIMUM)`
    - If `recovery_ceiling` already set from prior catastrophic loss, take the minimum of old and new
  - `new_memory_entry` (append) — trust_change memory entry when trust shift magnitude exceeds `config.trust.significant_change_threshold` (default 0.10)
- Trust-gain clamping applied during delta application: `new_trust = min(recovery_ceiling, old_trust + trust_gain_delta)`
- `ProvinceDelta[]` — aggregate institutional_trust updates derived from NPC trust patterns within the province

## Preconditions
- `community_response` has completed (community-driven trust effects such as grievance-to-trust mappings are finalized).
- NPC memory entries for this tick have been written by all prior modules (behavior engine, evidence, facility signals).
- All `Relationship` fields are within valid ranges at entry: trust in [-1.0, 1.0], fear in [0.0, 1.0], recovery_ceiling in [RECOVERY_CEILING_MINIMUM, 1.0].
- Evidence tokens referenced by trust calculations exist in KnowledgeMap with valid confidence values.

## Postconditions
- All NPC-to-NPC trust values updated based on this tick's accumulated evidence, interactions, and events.
- Trust gains capped by `recovery_ceiling` for relationships with prior catastrophic loss history.
- New catastrophic trust losses have set `recovery_ceiling` on affected relationships (permanent, never reset).
- Significant trust changes (|delta| > threshold) recorded as memory entries for future NPC decision-making.
- Province-level `institutional_trust` aggregated from NPC trust patterns (feed into CommunityState next tick).
- No NPC processed more than once per province per tick.

## Invariants
- Trust clamped to [-1.0, 1.0] after all adjustments.
- `recovery_ceiling` in [RECOVERY_CEILING_MINIMUM (0.15), 1.0]; default 1.0.
- Recovery ceiling is permanent and monotonically non-increasing: can only be lowered by subsequent catastrophic losses, never raised.
- Fear is NOT modified by this module. Fear decay runs via DeferredWorkQueue (batched every 30 ticks per NPC).
- Province-parallel execution: Province A's trust updates do not read from or depend on Province B's trust state within the same tick.
- NPCs processed in `npc_id` ascending order within each province for deterministic accumulation.
- Floating-point accumulations use canonical sort order: `npc_id` ascending, then `target_npc_id` ascending within each NPC's relationship list.
- Cross-province trust effects (NPC in Province A affecting NPC in Province B) write to `CrossProvinceDeltaBuffer` and apply next tick.
- Same seed + same inputs = identical trust output regardless of core count.

## Failure Modes
- NPC with no relationships: skip processing for that NPC; zero deltas produced. Not an error.
- Relationship references invalid NPC ID (target deceased, fled, or non-existent): skip that relationship, log warning, continue processing remaining relationships.
- NPC with `status != active` (imprisoned, dead, fled, waiting): skip processing entirely for that NPC.
- Trust delta produces NaN from floating-point edge case: reset trust to 0.0, log error diagnostic.
- Recovery ceiling already set and new catastrophic loss occurs: take `min(existing_ceiling, new_ceiling)` — ceiling can only decrease.
- Memory log at capacity (`MAX_MEMORY_ENTRIES = 500`): new trust_change memory displaces oldest entry with lowest decay value.

## Performance Contract
- Province-parallel: each province processed independently on a separate worker thread.
- Target: < 8ms total across all 6 provinces on 6 cores.
- Per-province: < 2ms for ~330 NPCs with ~50 relationships each.
- O(N * R) per province where N = NPC count in province, R = mean relationships per NPC.
- At 2,000 NPCs with ~50 relationships each: ~10,000 NPCs across 6 provinces, ~166,000 relationship evaluations total.
- Must not exceed tick budget share that would push total tick above 200ms target.

## Dependencies
- runs_after: ["community_response"]
- runs_before: []

## Test Scenarios
- `test_positive_trust_delta_applied`: Set NPC relationship trust to 0.50. Apply `trust_delta = +0.10` from positive interaction. Verify trust = 0.60.
- `test_negative_trust_delta_applied`: Set NPC relationship trust to 0.50. Apply `trust_delta = -0.20` from betrayal event. Verify trust = 0.30.
- `test_trust_clamped_to_upper_bound`: Apply `trust_delta = +0.60` to trust = 0.80. Verify trust clamped to 1.0.
- `test_trust_clamped_to_lower_bound`: Apply `trust_delta = -0.80` to trust = -0.50. Verify trust clamped to -1.0.
- `test_catastrophic_loss_sets_ceiling`: Set trust to 0.80. Apply delta of -0.70 (exceeds -0.55 threshold, result = 0.10 at CATASTROPHIC_TRUST_FLOOR). Verify `recovery_ceiling = max(0.80 * 0.60, 0.15) = 0.48`.
- `test_trust_gain_capped_by_ceiling`: Set `recovery_ceiling = 0.48`, trust = 0.30. Apply `trust_delta = +0.30`. Verify trust = 0.48 (not 0.60).
- `test_default_ceiling_no_cap`: Set `recovery_ceiling = 1.0` (default). Apply trust gain pushing trust to 0.90. Verify trust = 0.90 with no capping.
- `test_second_catastrophic_loss_lowers_ceiling`: Set `recovery_ceiling = 0.48` from prior loss. Apply new catastrophic loss from trust = 0.40 with delta = -0.60. Verify `recovery_ceiling = min(0.48, max(0.40 * 0.60, 0.15)) = min(0.48, 0.24) = 0.24`.
- `test_inactive_npc_skipped`: Set NPC `status = NPCStatus::imprisoned`. Verify no trust deltas produced for that NPC.
- `test_significant_trust_change_creates_memory`: Apply `trust_delta = +0.15` (above significant_change_threshold of 0.10). Verify new MemoryEntry appended with `type = interaction` and appropriate `emotional_weight`.
- `test_province_parallel_determinism`: Run 50 ticks of trust updates across 6 provinces on 1 core and 6 cores. Verify bit-identical trust values, recovery ceilings, and memory entries.
- `test_cross_province_trust_via_delta_buffer`: NPC in Province 0 has relationship with NPC in Province 1. Verify trust delta writes to `CrossProvinceDeltaBuffer` (not applied same tick).
