# player_actions — Developer Context

## What This Module Does
Drains the PlayerActionQueue each tick, validates player actions against
current WorldState, and translates them into DeltaBuffer writes and
DeferredWorkQueue items. This is the bridge between external input
(UI, CLI) and the deterministic simulation core.

## Tier: Core (runs before all domain modules)

## Key Dependencies
- runs_after: [] (first in topological order)
- runs_before: ["calendar", "scene_cards"]
- Reads: player_action_queue, pending_scene_cards, calendar, player state, NPC states
- Writes: SceneCardChoiceDelta, CalendarCommitDelta, PlayerDelta, NewBusinessDelta, CalendarEntry, DeferredWorkItems

## Critical Rules
- Actions are processed in sequence_number order (deterministic)
- Invalid actions are silently dropped (no crash)
- Physical presence is enforced for in-person actions
- Queue is cleared after processing each tick
- All RNG draws use DeterministicRNG for reproducibility
- The module uses const_cast for DeferredWorkQueue and queue clearing since
  it receives const WorldState& but needs to push deferred items and clear
  the action queue (both are logically write-side staging areas)

## Interface Spec
- docs/interfaces/player_actions/INTERFACE.md
