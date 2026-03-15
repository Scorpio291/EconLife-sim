# weapons_trafficking — Developer Context

## What This Module Does
Manages weapons sourcing, trafficking routes, and distribution. Tracks weapon types, quantities, and territorial control.

## Tier: 8 | Sequential (global).

## Key Dependencies
- runs_after: [criminal_operations]
- Reads: WorldState (see INTERFACE.md for specific fields)
- Writes: DeltaBuffer (see INTERFACE.md for specific deltas)

## Critical Rules
- Read INTERFACE.md before making changes
- All random draws through DeterministicRNG
- Floating-point accumulations in canonical sort order
- Process entities in deterministic order

## Interface Spec
- docs/interfaces/weapons_trafficking/INTERFACE.md
