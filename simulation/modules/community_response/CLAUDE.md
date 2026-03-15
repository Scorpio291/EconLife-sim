# community_response — Developer Context

## What This Module Does
Evaluates community-level reactions to events: protests, support movements, collective action thresholds.

## Tier: 6 | Sequential (global).

## Key Dependencies
- runs_after: [npc_behavior]
- Reads: WorldState (see INTERFACE.md for specific fields)
- Writes: DeltaBuffer (see INTERFACE.md for specific deltas)

## Critical Rules
- Read INTERFACE.md before making changes
- All random draws through DeterministicRNG
- Floating-point accumulations in canonical sort order
- Process entities in deterministic order

## Interface Spec
- docs/interfaces/community_response/INTERFACE.md
