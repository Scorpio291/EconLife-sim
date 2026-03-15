# persistence — Developer Context

## What This Module Does
Serializes and deserializes WorldState for save/load. LZ4 compression, round-trip determinism verification.

## Tier: 12 | Sequential (global).

## Key Dependencies
- runs_after: [world_state]
- Reads: WorldState (see INTERFACE.md for specific fields)
- Writes: DeltaBuffer (see INTERFACE.md for specific deltas)

## Critical Rules
- Read INTERFACE.md before making changes
- All random draws through DeterministicRNG
- Floating-point accumulations in canonical sort order
- Process entities in deterministic order

## Interface Spec
- docs/interfaces/persistence/INTERFACE.md
