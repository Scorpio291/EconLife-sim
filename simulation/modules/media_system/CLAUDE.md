# media_system — Developer Context

## What This Module Does
Simulates journalism, media coverage, and public information flow. Journalists investigate evidence, publish stories affecting public opinion.

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
- docs/interfaces/media_system/INTERFACE.md
