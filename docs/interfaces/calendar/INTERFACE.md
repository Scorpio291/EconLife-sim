# Module: calendar

## Purpose
Advances the in-game date, checks for deadline expirations on calendar entries, and emits consequence deltas for missed deadlines including relationship penalties, NPC unilateral actions, and NPC memory entries. NOT province-parallel; operates on global state.

## Inputs (from WorldState)
- `current_tick` — absolute tick counter; compared against CalendarEntry.start_tick and deadline ticks
- `calendar` — merged calendar of all entries (player + NPC commitments); filtered per-owner at read time
- `significant_npcs` — NPC relationship and memory state; needed for deadline consequence application
- `player` — player committed/attendance state for determining deadline misses

## Outputs (to DeltaBuffer)
- `NPCDelta.updated_relationship` — relationship_penalty applied immediately to `npc.relationships[player].trust` when a deadline is missed
- `NPCDelta.new_memory_entry` — deadline_missed memory entry with `emotional_weight = -(0.3 + relationship_penalty)` added to the NPC's memory log
- Queued `ConsequenceEntry` — scheduled at `deadline_tick + consequence_delay_ticks` with the specified `consequence_type` and `consequence_severity`
- Queued `npc_unilateral_action` — when `deadline_consequence.npc_initiative == true`, the NPC takes the action the player failed to take (file_complaint, contact_rival, report_to_regulator, publish_information, escalate_obligation)
- `CalendarDelta` — entries marked as expired/completed when `current_tick >= start_tick + duration_ticks`
- `PlayerDelta` — player_committed state updates; fast-forward suppression flag set during mandatory entries

## Preconditions
- `current_tick` is valid and monotonically increasing.
- All `CalendarEntry.npc_id` values reference valid NPCs in `significant_npcs` or `named_background_npcs`.
- All `CalendarEntry.scene_card_id` values reference valid scene card templates.
- `DeadlineConsequence` fields are populated for entries of type `deadline`.

## Postconditions
- All calendar entries whose deadline tick has passed (`current_tick > start_tick + duration_ticks` for deadline-type entries where `player_committed == false`) have had their 4-step missed-deadline procedure executed.
- Relationship penalties applied immediately (same tick) to affected NPC trust values.
- Memory entries appended to NPC memory logs for all missed deadlines.
- Consequence entries queued in the DeferredWorkQueue at correct future tick.
- NPC unilateral actions queued for deadline entries with `npc_initiative == true`.
- Completed non-deadline entries (meetings, events, operations, personal) removed from the active calendar.
- Fast-forward suppression is active during mandatory calendar entries.

## Invariants
- Missed deadline procedure always executes all 4 steps in order: (1) relationship penalty, (2) queue consequence, (3) NPC initiative action if flagged, (4) NPC memory entry.
- `emotional_weight` for missed deadline memory is always `-(0.3 + relationship_penalty)`, never just the penalty alone.
- Calendar entries with `mandatory == true` suppress fast-forward while active (`start_tick <= current_tick <= start_tick + duration_ticks`).
- NPC unilateral action types are limited to: file_complaint, contact_rival, report_to_regulator, publish_information, escalate_obligation.
- Scheduling friction: when an NPC is queried for availability, low-trust NPCs add buffer time to the next available slot; high-trust NPCs find time faster.
- Calendar is a merged view; per-owner filtering happens at read time, not storage time.

## Failure Modes
- Invalid `npc_id` on a calendar entry: log warning, skip deadline consequence for that entry, continue processing other entries.
- `consequence_delay_ticks` of 0: consequence fires immediately this tick (valid edge case, not an error).
- Calendar entry referencing a deleted or dead NPC: skip relationship and memory steps; still queue consequence if applicable.

## Performance Contract
- Sequential execution (not province-parallel).
- Target: < 5ms per tick for up to 200 active calendar entries.
- Deadline check is O(n) over active entries; acceptable given expected entry count.

## Dependencies
- runs_after: [] (early-tick module; no domain module dependencies)
- runs_before: ["scene_cards"]

## Test Scenarios
- `test_deadline_miss_applies_relationship_penalty`: Create a deadline entry with `relationship_penalty = 0.15` at tick 10. Player does not commit. At tick 11, verify `npc.relationships[player].trust` decreased by exactly 0.15.
- `test_deadline_miss_queues_consequence_with_delay`: Deadline at tick 10 with `consequence_delay_ticks = 5`. Verify a ConsequenceEntry is queued in the DeferredWorkQueue at tick 15 with the correct `consequence_type` and `consequence_severity`.
- `test_deadline_miss_creates_memory_entry`: Deadline missed by player. Verify NPC memory log contains entry with `type = deadline_missed`, `subject_id = player`, and `emotional_weight = -(0.3 + relationship_penalty)`.
- `test_npc_initiative_triggers_unilateral_action`: Deadline with `npc_initiative = true`. Player misses it. Verify an NPC unilateral action is queued (e.g., `report_to_regulator`).
- `test_npc_initiative_false_no_unilateral_action`: Deadline with `npc_initiative = false`. Player misses it. Verify no unilateral action is queued; only relationship penalty, consequence, and memory steps execute.
- `test_mandatory_entry_suppresses_fast_forward`: Calendar entry with `mandatory = true`, `start_tick = 5`, `duration_ticks = 3`. Verify fast-forward is suppressed during ticks 5-8 and re-enabled at tick 9.
- `test_committed_entry_not_flagged_as_missed`: Player commits to a deadline entry (`player_committed = true`). Verify no deadline miss procedure fires at expiration.
- `test_completed_entry_removed_from_active_calendar`: Meeting entry with `start_tick = 5`, `duration_ticks = 2`. At tick 8, verify the entry is no longer in the active calendar.
- `test_zero_delay_consequence_fires_immediately`: Deadline with `consequence_delay_ticks = 0` missed at tick 10. Verify consequence is queued at tick 10 (same tick execution).
- `test_dead_npc_skips_relationship_and_memory`: Calendar entry references NPC that died before the deadline. Verify relationship and memory steps are skipped but consequence is still queued.
