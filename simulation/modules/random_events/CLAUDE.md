# random_events — Developer Context

## What This Module Does
Rolls for random occurrences each tick based on probability tables and
world conditions. Events affect NPCs, businesses, provinces.
Province-parallel.

## Tier: 1

## Key Dependencies
- runs_after: ["calendar"]
- runs_before: []
- Reads: RandomEventTemplate pool, province conditions, RNG
- Writes: NPCDelta, RegionDelta, ConsequenceDelta for triggered events

## Critical Rules
- Uses DeterministicRNG — never external randomness
- Event probability modified by province conditions and season
- Events can be one-shot or create ongoing effects via DeferredWorkQueue
- Event templates are data-driven from package configuration

## Interface Spec
- docs/interfaces/random_events/INTERFACE.md
