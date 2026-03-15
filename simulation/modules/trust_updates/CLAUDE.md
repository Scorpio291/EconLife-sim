# trust_updates — Developer Context

## What This Module Does
Processes trust score changes between NPCs based on observed actions, kept promises, and betrayals. Province-parallel.

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
- docs/interfaces/trust_updates/INTERFACE.md
