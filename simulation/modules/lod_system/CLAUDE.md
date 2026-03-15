# lod_system — Developer Context

## What This Module Does
Manages Level of Detail transitions for nations: LOD 0 (full sim), LOD 1 (simplified), LOD 2 (statistical).

## Tier: 11 | Sequential (global).

## Key Dependencies
- runs_after: [regional_conditions]
- Reads: WorldState (see INTERFACE.md for specific fields)
- Writes: DeltaBuffer (see INTERFACE.md for specific deltas)

## Critical Rules
- Read INTERFACE.md before making changes
- All random draws through DeterministicRNG
- Floating-point accumulations in canonical sort order
- Process entities in deterministic order

## Interface Spec
- docs/interfaces/lod_system/INTERFACE.md
