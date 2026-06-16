# business_lifecycle — Developer Context

## What This Module Does
Handles business lifecycle events. Two era-driven effects fire on the tick after
an era transition: stranded-asset penalties for businesses in sectors obsoleted
by the new era, and spawning of new-entrant businesses in sectors that emerge
with it. Separately, **continuous opportunity-driven firm genesis** runs on its
own monthly cadence (plus a founding-moment cohort at tick 1): firms are born
when a province is under its supportable firm target (`residents /
firms_per_resident_denominator`, the same ~1-per-10 density world gen seeds),
founded by a local resident who commits their own capital. This is what lets a
founding-seed world (zero firms) bootstrap an economy, and it self-limits at
saturation (10% deadband) so an already-seeded world stays quiet. Sequential
(not province-parallel) because the spawn paths assign unique ids and pick
founders across all provinces.

## Tier: 2 | Sequential (not province-parallel).

## Key Dependencies
- runs_after: [technology]
- runs_before: [npc_business, production]
- Reads: WorldState (technology.current_era, technology.era_started_tick,
         npc_businesses, significant_npcs, provinces)
- Writes: DeltaBuffer (business_deltas, new_businesses)

## Critical Rules
- Read INTERFACE.md before making changes
- Era effects (stranded penalties + era entrants) fire exactly once, on the tick
  immediately after an era transition (era_started_tick + 1 == current_tick).
- Firm genesis runs on its own cadence (tick 1, then every genesis_cadence_ticks),
  independent of era transitions. It is opportunity/saturation-gated, so it does
  NOT fire when a province is already at its firm target.
- Genesis firms are founder-funded: the founder's capital is debited (NPCDelta)
  and becomes the firm's seed cash — no money is minted. Earning power is grounded
  in the province endowment (genesis_sector_endowment), not a flat constant.
- Stranded-asset penalties are applied as revenue_per_tick_update and
  cost_per_tick_update replacements — cumulative across eras via compounding.
- New business IDs are computed as max(existing_ids) + 1 each spawn event.
- All random draws go through DeterministicRNG forked from world_seed + era.
- stranded_revenue_floor prevents any single-era shock from zero-ing revenue.

## Interface Spec
- docs/interfaces/business_lifecycle/INTERFACE.md

## Config
- packages/base_game/config/business_lifecycle.json
