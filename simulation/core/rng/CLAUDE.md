# rng — Developer Context

## What This Module Does
Deterministic random number generator. All simulation randomness goes
through this. Supports forking for province-parallel work.

## Key Files
- `deterministic_rng.h` — DeterministicRNG class

## Critical Rules
- NEVER use std::rand, system time, thread ID, or any external entropy
- Same seed + same call sequence = identical output on any platform
- Fork with same context_id always produces same sub-stream
- Single call < 10ns; fork < 50ns

## Dependency Direction
Core utility. Available before all modules. No dependencies.

## Interface Spec
- docs/interfaces/rng/INTERFACE.md
