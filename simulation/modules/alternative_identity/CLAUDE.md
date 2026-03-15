# alternative_identity — Developer Context

## What This Module Does
Manages creation and maintenance of false identities for evading investigation. Tracks identity quality and discovery risk.

## Tier: 9 | Sequential (global).

## Key Dependencies
- runs_after: [investigator_engine]
- Reads: WorldState (see INTERFACE.md for specific fields)
- Writes: DeltaBuffer (see INTERFACE.md for specific deltas)

## Critical Rules
- Read INTERFACE.md before making changes
- All random draws through DeterministicRNG
- Floating-point accumulations in canonical sort order
- Process entities in deterministic order

## Interface Spec
- docs/interfaces/alternative_identity/INTERFACE.md
