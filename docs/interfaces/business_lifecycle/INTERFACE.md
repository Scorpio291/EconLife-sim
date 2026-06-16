# Module: business_lifecycle

## Purpose
Handles business lifecycle events. Two **era-driven** effects fire exactly once, on the
tick immediately after each era transition, plus a **continuous firm-genesis** effect that
runs on its own cadence (see effect 3). Applies:

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

3. **Continuous opportunity-driven firm genesis** — firms are born from unmet local
   opportunity, independent of era transitions. Runs on its own cadence: a founding-moment
   cohort at `current_tick == 1`, then every `genesis_cadence_ticks` (default 30). Each
   province supports a target of `residents / firms_per_resident_denominator` legitimate
   firms (the same ~1-per-10 density world gen seeds). While a province is under target by
   more than `genesis_saturation_deadband`, new firms form, each founded by a local resident
   who **commits their own capital** (the founder's `capital` is debited and becomes the
   firm's seed `cash` — no money is minted). This lets a founding-seed world (zero firms)
   bootstrap an economy and self-limits at saturation, so an already-seeded world stays quiet.
   Genesis firm earning power is grounded in the province endowment (worked land for
   agriculture, local customer base for retail/services), not a flat constant — a solo
   owner-operator produces real value from labour + local resources.

NOT province-parallel: the era effects span all provinces simultaneously, and genesis
assigns globally-unique ids and picks founders across provinces in deterministic order.

## Inputs (from WorldState)
- `technology.current_era` — the era currently active
- `technology.era_started_tick` — the tick on which the current era began (set by
  `apply_deltas` when a `TechnologyDelta.new_era` is applied)
- `current_tick` — compared to `era_started_tick + 1` to detect the tick after transition
- `npc_businesses[]` — read to apply penalties, compute `max(id) + 1`, and count current
  legitimate firms per province (genesis saturation)
- `significant_npcs[]` — scanned for unowned NPCs (era entrants); for genesis, founders are
  drawn from residents with `capital ≥ founder_min_capital` who own no firm
- `npc_indices_by_home_province[]` — resident pool per province (sets the firm target and the
  founder candidate list, in deterministic order)
- `provinces[]` — `demographics.income_high_fraction` / `income_middle_fraction` (customer
  base) and `agricultural_productivity` (worked-land yield) ground genesis firm earning power;
  demographics also scale era-entrant cash/revenue

## Outputs (to DeltaBuffer)
- `business_deltas[]` — `BusinessDelta` per stranded business with:
  - `revenue_per_tick_update` = `biz.revenue_per_tick × (1.0 - revenue_penalty)`,
    clamped above `biz.revenue_per_tick × stranded_revenue_floor`
  - `cost_per_tick_update` = `biz.cost_per_tick × (1.0 + cost_increase)`
- `new_businesses[]` — `NewBusinessDelta` per spawned entrant:
  - era entrants: one per province per emerging-sector entry, count =
    `max(1, round(province_biz_count × spawn_fraction))`
  - genesis firms: per under-target province, count = `max(1, round(gap × fill))` where
    `gap = target − current_legit_firms` and `fill` is 1.0 at the founding moment (tick 1)
    else `genesis_gap_fill_fraction`; capped by the number of eligible founders (residents
    who own no firm and hold ≥ `founder_min_capital`)
- `npc_deltas[]` — for each genesis firm, a `capital_delta = −(founder_capital ×
  founder_investment_fraction)` on the founder (the capital becomes the firm's seed cash)

## Preconditions
- `TechnologyModule` has completed for this tick and its delta has been applied
  (so `world.technology.era_started_tick` reflects the most recent transition).
- `apply_deltas()` has been called after `TechnologyModule` (guaranteed by topological sort).

## Postconditions
- On the tick immediately after an era transition:
  - All businesses in stranded sectors have received `revenue_per_tick_update` and
    `cost_per_tick_update` replacements.
  - Era-entrant businesses have been appended to `world.npc_businesses`.
- On a genesis cadence tick (tick 1 or a multiple of `genesis_cadence_ticks`):
  - Under-target provinces with eligible founders have spawned genesis firms, and each
    founder's capital has been debited by their investment.
- On all other ticks: no-op (returns without emitting any deltas).

## Invariants
- Era effects fire exactly once per era transition, on tick `era_started_tick + 1`. Idempotent.
- Genesis is opportunity/saturation-gated: it emits nothing for a province already at/near
  its firm target (within `genesis_saturation_deadband`), so a fully-seeded world is
  effectively a no-op for genesis. Genesis firms are conserved (founder-funded), never minted.
- Stranded-asset penalties are applied as replacement fields (not additive) — the penalty
  compounds naturally because each successive era shock is applied to the already-reduced
  revenue from prior shocks.
- `stranded_revenue_floor` guarantees `new_revenue >= old_revenue × stranded_revenue_floor`
  per era transition, preventing instant bankruptcy from a single shock.
- New business IDs are computed as `max(existing_ids) + 1` each spawn event — unique by
  construction across all prior world-gen and runtime-spawned businesses.
- Era-effect random draws use `DeterministicRNG(world_seed).fork(new_era × 10000)`; genesis
  draws use `DeterministicRNG(world_seed).fork(0x6E5151 + current_tick)` — same seed = identical
  spawned businesses regardless of run ordering.
- `effective_tech_tier` on spawned businesses is set to `float(new_era)` — era-matched
  baseline rather than Era 1 equipment.

## Failure Modes
- Era with no stranded or emerging entries in config: no-op for that era, no businesses
  affected. Valid for EX eras not yet configured.
- Province with no unowned NPCs: spawned businesses get `owner_id = 0`. Financial
  distribution module logs a warning and skips compensation for such businesses.
- Province with zero businesses at transition tick: `spawn_fraction × 0` = 0;
  `max(1, round(0))` = 1, so at minimum 1 business spawns per configured emerging sector.
- Genesis with no eligible founder (no resident under-the-cap and unclaimed with sufficient
  capital): the province spawns fewer firms than its gap implies, or none — formation is
  bound to real local wealth, not forced. A founding world thus develops as capital allows.
- Province with zero residents (`npc_indices_by_home_province` empty): genesis skips it.

## Performance Contract
- Sequential (not province-parallel): < 5ms per era transition event.
- Genesis runs on ~1/30 of ticks; each evaluation is O(provinces × residents + businesses).
  No-op on the remaining ~96.7% of ticks (neither an era-transition nor a cadence tick).

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
| `genesis_enabled` | true | Master switch for opportunity-driven firm genesis |
| `genesis_cadence_ticks` | 30 | Genesis evaluation cadence (plus the tick-1 founding cohort) |
| `firms_per_resident_denominator` | 10.0 | Province firm target = residents / this |
| `genesis_saturation_deadband` | 0.10 | Spawn only while a province is >10% under target |
| `genesis_gap_fill_fraction` | 0.10 | Fraction of the unmet gap filled per cadence (1.0 at founding) |
| `founder_min_capital` | 6000.0 | A resident needs at least this capital to found a firm |
| `founder_investment_fraction` | 0.30 | Share of founder capital committed as the firm's seed cash |

## Test Scenarios
- `test_stranded_asset_penalty_on_era_transition`: Set up 3 energy businesses and trigger era
  1→2 transition. Verify that on the tick after transition, each energy business has
  `revenue_per_tick` reduced by 10% and `cost_per_tick` increased by 8%.
- `test_stranded_revenue_floor_respected`: Set an energy business with `revenue_per_tick = 100`
  and trigger multiple era transitions. Verify `revenue_per_tick` never drops below
  `100 × stranded_revenue_floor = 20`.
- `test_era_entrant_spawning`: Trigger era 2 transition. Verify 1+ new technology-sector
  fast-expander businesses appear in each province's `npc_businesses` list on the next tick.
- `test_no_op_on_non_transition_ticks`: On a tick that is neither an era-transition tick nor
  a genesis cadence tick (e.g. `current_tick = 100`, `era_started_tick = 50`), verify
  `business_deltas` and `new_businesses` from this module are empty.
- `test_genesis_bootstraps_founding_world` (integration, `history_gen_integration_test.cpp`):
  a founding-seed world run forward grows from zero firms to ≥ provinces firms, all located,
  legitimate, and revenue-positive, with aggregate population capital growing.
- `test_genesis_quiet_at_saturation`: a province already at its firm target emits no genesis
  `new_businesses` (covered behaviorally by the emergence suite staying 27/27).
- `test_determinism`: Run to era 2 transition twice with the same seed. Verify identical
  spawned business IDs, cash, revenue, and owner assignments.
