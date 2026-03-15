# influence_network — Developer Context

## What This Module Does
Tracks and propagates influence relationships between NPCs, organizations, and political entities.

## Tier: 10 | Sequential (global).

## Key Dependencies
- runs_after: [community_response]
- Reads: WorldState (see INTERFACE.md for specific fields)
- Writes: DeltaBuffer (see INTERFACE.md for specific deltas)

## Critical Rules
- Read INTERFACE.md before making changes
- All random draws through DeterministicRNG
- Floating-point accumulations in canonical sort order
- Process entities in deterministic order

## Interface Spec
- docs/interfaces/influence_network/INTERFACE.md
