# investigator_engine — Developer Context

## What This Module Does
Drives NPC investigators (police, regulatory, journalist) through evidence gathering, case building, and prosecution/publication.

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
- docs/interfaces/investigator_engine/INTERFACE.md
