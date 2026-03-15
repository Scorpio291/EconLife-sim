# evidence — Developer Context

## What This Module Does
Manages evidence token creation, accumulation, decay, and discovery. Tracks financial, testimonial, documentary, and physical evidence per NPC.

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
- docs/interfaces/evidence/INTERFACE.md
