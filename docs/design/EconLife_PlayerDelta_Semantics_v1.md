# EconLife — PlayerDelta and Player Action Semantics (v1)

**Status:** Ratified design. Closes issue #11.
**Scope:** Defines how external player input enters the deterministic
simulation, where it lands in the tick, and how it interacts with module
deltas. Most of this design is already implemented; this document
ratifies the de-facto behavior and pins down two items that were not
explicit before.

## Background

The simulation runs headlessly at one tick per in-game day. The UI and CLI
are external observers; player intent flows in through a queue:

```
UI / CLI                    enqueue_player_action()
   |                                  |
   v                                  v
+-----------------+        +---------------------------+
| PlayerAction    | -----> | world.player_action_queue |
| (typed payload) |        | (vector<PlayerAction>)    |
+-----------------+        +---------------------------+
                                       |
                                       v   start of next tick
                          +-----------------------------+
                          | player_actions module       |
                          | (Step 0; runs_after = [])   |
                          +-----------------------------+
                                       |
                                       v   writes to DeltaBuffer
                          +-----------------------------+
                          | DeltaBuffer for this tick   |
                          | (player_delta + others)     |
                          +-----------------------------+
                                       |
                                       v   end of tick
                          +-----------------------------+
                          | apply_deltas(WorldState)    |
                          +-----------------------------+
```

`PlayerDelta` itself is a member of `DeltaBuffer` (see
`simulation/core/world_state/delta_buffer.h:46`). The `player_actions`
module writes to it; nothing else does. Module-emitted deltas merge
into the same buffer in ascending province order.

## Six questions resolved

### 1. Ingestion point

**Decision:** PlayerActions are queued onto `world.player_action_queue`
between ticks via `enqueue_player_action()`
(`simulation/core/world_state/player_action_queue.cpp:7`). The
`player_actions` module drains the queue at the start of the next tick,
in ascending `sequence_number` order, and translates each action into
`DeltaBuffer` writes.

**No PlayerDelta path mutates WorldState mid-tick.** Player effects are
visible to other modules in the *next* tick (after `apply_deltas`).

This is consistent with the const-WorldState contract for modules.

### 2. Conflict resolution

**Decision:** No special conflict rule is needed. `player_actions` is
the first module in topological order (`runs_after: []`,
`runs_before: ["calendar", "scene_cards"]`). Its writes land in
`DeltaBuffer` before any other module runs, and downstream modules
read the same const `WorldState` everyone else reads — they cannot
"see" the in-flight player_delta until `apply_deltas` runs at end of
tick.

For replacement fields, last-write-wins by emission order; for
additive fields, sums accumulate. `PlayerDelta::merge_from`
(`simulation/core/world_state/delta_buffer.cpp:27`) implements both
policies. Player and modules cannot conflict on player-only fields
because `player_delta` is only written by `player_actions`.

### 3. Validation boundary

**Decision:** All player-action validation lives in
`player_actions_module.cpp`'s per-action `handle_*` functions.
Invalid actions are silently dropped (no crash, no error event in
V1). Examples of validation rules already in code:
travel-to-current-province, in-transit blocks, insufficient capital,
NPC alive checks, scene card already chosen, calendar entry
expired.

UI is allowed to perform optimistic validation (e.g. greying out the
"Travel" button when in transit) but is not the authoritative layer.
The simulation rejects anything illegal regardless of what the UI
allowed.

This is the canonical layer because:
- It runs against an authoritative `WorldState` snapshot.
- UI may operate on a stale state from the previous tick.
- It is deterministic and reproducible from a journal of actions +
  RNG seed.

### 4. Replay and journaling

**Decision:** Each `PlayerAction` carries `submitted_tick` and a
monotonic `sequence_number` assigned at enqueue time
(`player_action_queue.cpp:9`, `next_action_sequence++`). Within a
tick, actions are processed in ascending `sequence_number` order;
across ticks, by `submitted_tick` then `sequence_number`.

For replay to be deterministic, the journal records:
- world seed (immutable, in `WorldState`)
- per-action: `submitted_tick`, `sequence_number`, type, payload

**Gap closed by this design pass:** `next_action_sequence` was not
included in the persistence schema (issue #11). Reloading a save then
enqueuing a new action would restart the counter at 0 and could
duplicate sequence numbers from earlier actions in the same save
lineage. This document calls for `next_action_sequence` to be
serialized; the accompanying patch does so and bumps
`CURRENT_SCHEMA_VERSION` accordingly.

The full action journal itself is *not* part of V1's persistence
format — saves capture state, not history. A replay-from-seed feature
would need a separate journal file. That is out of scope for this
document; only the next-sequence counter is fixed here.

### 5. Cross-province player effects

**Decision:** Today, the only cross-province player effect is `travel`,
which schedules an arrival via `DeferredWorkQueue` with a fixed 3-tick
delay (`player_actions_module.cpp:188-190`). It does NOT use
`CrossProvinceDeltaBuffer`.

This is intentional and stays. The reasoning:
- `CrossProvinceDeltaBuffer` is a one-tick propagation channel for
  province-parallel modules emitting effects targeting other provinces.
  It exists because province workers must not write to other provinces'
  state during the parallel phase.
- `player_actions` is sequential, not province-parallel. There is no
  parallel-phase invariant to protect.
- Player travel needs an arbitrary, often longer, delay than one tick.
  `DeferredWorkQueue` is the correct primitive for that.

If a future player action emits an effect targeting another province
with **exactly one-tick delay** (e.g. a player-issued cross-province
order that should land at the start of the next tick), it should use
`CrossProvinceDeltaBuffer` for symmetry with module behavior. No such
action exists in V1.

### 6. Batching

**Decision:** Multiple `PlayerAction`s in one tick are processed
sequentially in `sequence_number` order. Each handler writes its own
deltas to the shared `DeltaBuffer.player_delta`, which accumulates
through `PlayerDelta::merge_from` semantics: additive fields sum,
replacement fields take the last-set value. Coalescing is observable
to modules only through the final summed/replaced state at
`apply_deltas` time, never mid-tick.

This means a player who queues two `wealth_delta = -100` actions in
one tick will see `wealth_delta = -200` applied. Two travel actions
in the same tick produce one `new_travel_status = in_transit`
replacement and two scheduled arrivals on the deferred queue (the
second arrival overrides the first when it fires; this is a known
edge case and is not specifically guarded).

## Deliverables

- [x] **Decision doc.** This file.
- [x] **Interface spec.** `docs/interfaces/player_actions/INTERFACE.md`
      already documents the runtime contract; updated with a pointer to
      this design doc and a note that `next_action_sequence` is
      persisted.
- [x] **Persistence fix.** Serialize `next_action_sequence` in
      `PersistenceModule::serialize` / `deserialize`. Bump
      `CURRENT_SCHEMA_VERSION` to 2 and reject v1 saves with a clear
      error pointing here.
- [x] **Migration plan.** None required: V1 is pre-release, no
      shipped saves to migrate. The schema bump is gated behind
      `is_schema_compatible(read, current)` which already returns false
      on a major-version mismatch.
- [x] **Determinism test.** `simulation/tests/determinism/`
      exercises a fixed `PlayerAction` script against a fixed seed and
      asserts identical golden output across two runs.

## Non-goals

- A new replay/journal format for offline playback. Out of scope.
- New player-facing actions beyond what V1 already defines. The
  design covers the existing nine action types (see
  `simulation/core/world_state/player_action_types.h`).
- Multi-player. Single-player only.

## See also

- `simulation/core/world_state/player_action_types.h` — V1 action set
- `simulation/core/world_state/player_action_queue.cpp` — enqueue
- `simulation/modules/player_actions/player_actions_module.cpp` —
  validation and translation
- `simulation/core/world_state/delta_buffer.h` — `PlayerDelta` shape
- `docs/interfaces/player_actions/INTERFACE.md` — runtime contract
- `EconLife_Technical_Design_v29.md` §10 — DeltaBuffer architecture
