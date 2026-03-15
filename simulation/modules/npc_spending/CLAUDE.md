# npc_spending — Developer Context

## What This Module Does
Processes NPC consumer spending decisions based on needs, income, and market prices. Province-parallel.

## Tier: 6 | Province-parallel.

## Key Dependencies
- runs_after: [npc_behavior]
- Reads: WorldState (see INTERFACE.md for specific fields)
- Writes: DeltaBuffer (see INTERFACE.md for specific deltas)

## Critical Rules
- Read INTERFACE.md before making changes
- All random draws through DeterministicRNG
- Floating-point accumulations in canonical sort order
- Process entities in deterministic order

## Interface Spec
- docs/interfaces/npc_spending/INTERFACE.md
