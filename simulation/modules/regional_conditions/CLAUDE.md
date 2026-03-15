# regional_conditions — Developer Context

## What This Module Does
Aggregates province-level conditions: stability index, inequality, crime rate, economic health. Feeds LOD transitions.

## Tier: 11 | Province-parallel.

## Key Dependencies
- runs_after: [political_cycle, influence_network]
- Reads: WorldState (see INTERFACE.md for specific fields)
- Writes: DeltaBuffer (see INTERFACE.md for specific deltas)

## Critical Rules
- Read INTERFACE.md before making changes
- All random draws through DeterministicRNG
- Floating-point accumulations in canonical sort order
- Process entities in deterministic order

## Interface Spec
- docs/interfaces/regional_conditions/INTERFACE.md
