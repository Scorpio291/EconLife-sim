# population_aging — Developer Context

## What This Module Does
Processes demographic aging, mortality, birth rates, and population cohort transitions per province.

## Tier: 11 | Province-parallel.

## Key Dependencies
- runs_after: [healthcare]
- Reads: WorldState (see INTERFACE.md for specific fields)
- Writes: DeltaBuffer (see INTERFACE.md for specific deltas)

## Critical Rules
- Read INTERFACE.md before making changes
- All random draws through DeterministicRNG
- Floating-point accumulations in canonical sort order
- Process entities in deterministic order

## Interface Spec
- docs/interfaces/population_aging/INTERFACE.md
