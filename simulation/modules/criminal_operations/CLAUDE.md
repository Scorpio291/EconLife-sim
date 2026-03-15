# criminal_operations — Developer Context

## What This Module Does
Manages criminal enterprise operations: drug production, money laundering fronts, protection racket enforcement, territory control.

## Tier: 7 | Sequential (global).

## Key Dependencies
- runs_after: [evidence]
- Reads: WorldState (see INTERFACE.md for specific fields)
- Writes: DeltaBuffer (see INTERFACE.md for specific deltas)

## Critical Rules
- Read INTERFACE.md before making changes
- All random draws through DeterministicRNG
- Floating-point accumulations in canonical sort order
- Process entities in deterministic order

## Interface Spec
- docs/interfaces/criminal_operations/INTERFACE.md
