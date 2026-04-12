# business_lifecycle — Developer Context

## What This Module Does
Handles era-driven business lifecycle events: stranded-asset penalties for
businesses in sectors obsoleted by a new era, and spawning of new-entrant
businesses in sectors that emerge with each era transition. Sequential (not
province-parallel) because era effects span all provinces simultaneously.

## Tier: 2 | Sequential (not province-parallel).

## Key Dependencies
- runs_after: [technology]
- runs_before: [npc_business, production]
- Reads: WorldState (technology.current_era, technology.era_started_tick,
         npc_businesses, significant_npcs, provinces)
- Writes: DeltaBuffer (business_deltas, new_businesses)

## Critical Rules
- Read INTERFACE.md before making changes
- Fires exactly once, on the tick immediately after an era transition
  (era_started_tick + 1 == current_tick). No-op on all other ticks.
- Stranded-asset penalties are applied as revenue_per_tick_update and
  cost_per_tick_update replacements — cumulative across eras via compounding.
- New business IDs are computed as max(existing_ids) + 1 each spawn event.
- All random draws go through DeterministicRNG forked from world_seed + era.
- stranded_revenue_floor prevents any single-era shock from zero-ing revenue.

## Interface Spec
- docs/interfaces/business_lifecycle/INTERFACE.md

## Config
- packages/base_game/config/business_lifecycle.json
