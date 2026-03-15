# drug_economy — Developer Context

## What This Module Does
Simulates drug production, distribution, and consumption markets. Province-parallel drug supply chains with quality and purity tracking.

## Tier: 8 | Province-parallel.

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
- docs/interfaces/drug_economy/INTERFACE.md
