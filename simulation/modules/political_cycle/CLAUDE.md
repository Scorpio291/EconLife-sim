# political_cycle — Developer Context

## What This Module Does
Manages election cycles, political campaigns, policy platforms, voting, and government formation.

## Tier: 10 | Sequential (global).

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
- docs/interfaces/political_cycle/INTERFACE.md
