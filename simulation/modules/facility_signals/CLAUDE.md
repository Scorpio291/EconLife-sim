# facility_signals — Developer Context

## What This Module Does
Generates observable signals from business facilities that may indicate illegal activity. Signal strength varies by operation type and concealment.

## Tier: 7 | Province-parallel.

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
- docs/interfaces/facility_signals/INTERFACE.md
