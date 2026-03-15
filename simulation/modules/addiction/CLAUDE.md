# addiction — Developer Context

## What This Module Does
Simulates substance addiction progression, treatment, and relapse for NPCs. Tracks dependency levels and regional impact.

## Tier: 10 | Province-parallel.

## Key Dependencies
- runs_after: [community_response]
- Reads: WorldState (see INTERFACE.md for specific fields)
- Writes: DeltaBuffer (see INTERFACE.md for specific deltas)

## Critical Rules
- Read INTERFACE.md before making changes
- All random draws through DeterministicRNG
- Floating-point accumulations in canonical sort order
- Process entities in deterministic order

## Interface Spec
- docs/interfaces/addiction/INTERFACE.md
