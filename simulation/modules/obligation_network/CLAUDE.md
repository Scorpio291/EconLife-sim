# obligation_network — Developer Context

## What This Module Does
Tracks debts, favors, and obligation relationships between NPCs. Manages obligation creation, satisfaction, and expiry.

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
- docs/interfaces/obligation_network/INTERFACE.md
