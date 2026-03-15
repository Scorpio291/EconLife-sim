# Module: deferred_work_queue

## Purpose
Unified min-heap for all scheduled work: consequences, transit arrivals, decay batches, business decisions, market recomputes, and background tasks. Drained at tick Step 2; recurring work rescheduled after execution.

## Inputs (from WorldState)
- `deferred_work_queue` — the priority queue itself.
- `current_tick` — to determine which items are due.

## Outputs (to DeltaBuffer)
- Varies by WorkType: NPC deltas, market deltas, evidence deltas, consequence deltas, region deltas.
- New DeferredWorkItems pushed for recurring work (rescheduled).

## Preconditions
- Queue is a valid min-heap sorted on `due_tick`.
- All DeferredWorkItem payloads have valid subject_id references.

## Postconditions
- All items where `due_tick <= current_tick` have been popped and executed.
- Recurring work rescheduled: relationship decay → +30 ticks, evidence decay → +7 ticks.
- Transit arrivals partitioned by destination_province_id for parallel processing.
- Background work only runs if real_time_remaining > threshold after all due work completes.

## Invariants
- Items execute in due_tick order (min-heap property).
- ConsequenceEntry and TransitShipment arrivals are views over this queue — not separate structures.
- Cross-province effects go through CrossProvinceDeltaBuffer (one-tick delay).

## Failure Modes
- Invalid subject_id: log warning, skip item, continue processing.
- Cancelled consequence: check cancellation flag before execution; skip if cancelled.

## Performance Contract
- Drain all due items: < 20ms typical, < 50ms worst case at 2,000 NPCs.
- Single item pop + execute: < 0.1ms average.

## Dependencies
- Runs at Step 2 of each tick, before all domain modules.
- runs_after: [] (executes early in tick)
- runs_before: ["production"]

## Test Scenarios
- `items_fire_on_correct_tick`: Schedule item at tick 10. Run ticks 1-9, verify not fired. Run tick 10, verify fired.
- `cancelled_entry_respects_cancellation`: Schedule consequence, cancel it, verify not executed.
- `recurring_work_reschedules`: Execute relationship_decay. Verify new item at current_tick + 30.
- `queue_survives_serialization`: Serialize WorldState with queued items. Deserialize. Verify items intact.
- `transit_arrivals_partition_by_province`: Schedule 3 arrivals to different provinces. Verify correct partitioning.
- `background_work_deferred_when_busy`: Fill tick budget with due items. Verify background_work skipped.
