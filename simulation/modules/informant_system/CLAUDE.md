# informant_system — Developer Context

## What This Module Does
Manages NPC informant recruitment, information flow, reliability scoring, and exposure risk.

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
- docs/interfaces/informant_system/INTERFACE.md
