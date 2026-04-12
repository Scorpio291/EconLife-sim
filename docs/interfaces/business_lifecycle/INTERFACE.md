# Module: business_lifecycle

## Purpose
Handles era-driven business lifecycle events. Fires exactly once, on the tick immediately
after each era transition. Applies two effects:

1. **Stranded-asset penalties** — revenue and cost shocks to businesses in sectors made
   obsolete by the new era (e.g., fossil-fuel energy businesses in Era 3+, conventional
   manufacturing in Era 4+). Penalties are cumulative across eras: each successive era
   brings an additional fractional reduction to the same sector, compounding downward.
   A `stranded_revenue_floor` prevents any single-era shock from zero-ing a business's
   revenue outright.

2. **Era-entrant spawning** — new businesses created in sectors that emerge with the new
   era (e.g., technology fast-expanders in Era 2+, research quality-players in Era 3+).
   New entrants start lean (lower cash and revenue than incumbents) and use an era-appropriate
   tech tier as their baseline. This sustains business count over multi-era runs and
   introduces competitive pressure from new-generation firms.

NOT province-parallel because era-transition effects span all provinces simultaneously.

## Inputs (from WorldState)
- `technology.current_era` — the era currently active
- `technology.era_started_tick` — the tick on which the current era began (set by
  `apply_deltas` when a `TechnologyDelta.new_era` is applied)
- `current_tick` — compared to `era_started_tick + 1` to detect the tick after transition
- `npc_businesses[]` — read to apply penalties and to compute `max(id) + 1` for spawning
- `significant_npcs[]` — scanned for unowned NPCs to assign as owners of new businesses
- `provinces[]` — `demographics.income_high_fraction` and `income_middle_fraction` used
  to scale spawned business cash/revenue by province wealth

## Outputs (to DeltaBuffer)
- `business_deltas[]` — `BusinessDelta` per stranded business with:
  - `revenue_per_tick_update` = `biz.revenue_per_tick × (1.0 - revenue_penalty)`,
    clamped above `biz.revenue_per_tick × stranded_revenue_floor`
  - `cost_per_tick_update` = `biz.cost_per_tick × (1.0 + cost_increase)`
- `new_businesses[]` — `NewBusinessDelta` per spawned entrant, one per province per
  emerging-sector entry in config, count = `max(1, round(province_biz_count × spawn_fraction))`

## Preconditions
- `TechnologyModule` has completed for this tick and its delta has been applied
  (so `world.technology.era_started_tick` reflects the most recent transition).
- `apply_deltas()` has been called after `TechnologyModule` (guaranteed by topological sort).

## Postconditions
- On the tick immediately after an era transition:
  - All businesses in stranded sectors have received `revenue_per_tick_update` and
    `cost_per_tick_update` replacements.
  - New businesses have been appended to `world.npc_businesses` via `apply_new_businesses`.
- On all other ticks: no-op (returns immediately without emitting any deltas).

## Invariants
- Fires exactly once per era transition, on tick `era_started_tick + 1`. Idempotent.
- Stranded-asset penalties are applied as replacement fields (not additive) — the penalty
  compounds naturally because each successive era shock is applied to the already-reduced
  revenue from prior shocks.
- `stranded_revenue_floor` guarantees `new_revenue >= old_revenue × stranded_revenue_floor`
  per era transition, preventing instant bankruptcy from a single shock.
- New business IDs are computed as `max(existing_ids) + 1` each spawn event — unique by
  construction across all prior world-gen and runtime-spawned businesses.
- All random draws use `DeterministicRNG(world_seed).fork(new_era × 10000)` — same seed +
  same era = identical spawned businesses regardless of run ordering.
- `effective_tech_tier` on spawned businesses is set to `float(new_era)` — era-matched
  baseline rather than Era 1 equipment.

## Failure Modes
- Era with no stranded or emerging entries in config: no-op for that era, no businesses
  affected. Valid for EX eras not yet configured.
- Province with no unowned NPCs: spawned businesses get `owner_id = 0`. Financial
  distribution module logs a warning and skips compensation for such businesses.
- Province with zero businesses at transition tick: `spawn_fraction × 0` = 0;
  `max(1, round(0))` = 1, so at minimum 1 business spawns per configured emerging sector.

## Performance Contract
- Sequential (not province-parallel): < 5ms per era transition event.
- No-op on > 99.9% of ticks (fires only on era-transition ticks, ~5 per 5000-tick run).

## Dependencies
- runs_after: ["technology"]
- runs_before: ["npc_business", "production"]

## Configuration
All values in `packages/base_game/config/business_lifecycle.json` → `BusinessLifecycleConfig`.

| Field | Default | Meaning |
|-------|---------|---------|
| `stranded_revenue_floor` | 0.20 | Minimum revenue fraction after any single-era shock |
| `stranded_sectors[era][]` | (see JSON) | Per-era list of `{sector, revenue_penalty, cost_increase}` |
| `emerging_sectors[era][]` | (see JSON) | Per-era list of `{sector, spawn_fraction, profile}` |

## Test Scenarios
- `test_stranded_asset_penalty_on_era_transition`: Set up 3 energy businesses and trigger era
  1→2 transition. Verify that on the tick after transition, each energy business has
  `revenue_per_tick` reduced by 10% and `cost_per_tick` increased by 8%.
- `test_stranded_revenue_floor_respected`: Set an energy business with `revenue_per_tick = 100`
  and trigger multiple era transitions. Verify `revenue_per_tick` never drops below
  `100 × stranded_revenue_floor = 20`.
- `test_era_entrant_spawning`: Trigger era 2 transition. Verify 1+ new technology-sector
  fast-expander businesses appear in each province's `npc_businesses` list on the next tick.
- `test_no_op_on_non_transition_ticks`: Run 100 ticks with no era transition. Verify
  `business_deltas` and `new_businesses` in the delta are always empty from this module.
- `test_determinism`: Run to era 2 transition twice with the same seed. Verify identical
  spawned business IDs, cash, revenue, and owner assignments.
