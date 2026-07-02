# warfare — Interface Spec

War between province-polities in the pre-market arc (M6c). At the dawn each province
is a proto-polity (chiefdom / feudal lord). Rational EV war: attack a REACHABLE
(adjacent) neighbour when the power balance favours it, preferring rich targets;
warm diplomatic relations deter; a defender's allies add coalition power; betraying
an ally brands the betrayer a pariah. Full model:
docs/design/EconLife_War_and_Diplomacy_v01.md.

## Cadence & gating
- Runs one decision pass per YEAR (`current_tick % 365 == 0`); all rates are per-year.
- Regime-gated to `WarfareConfig.active_regimes` (pre-market arc). On regime exit it
  publishes a one-time `war_mortality = 1.0` reset for all provinces (no stale spike).
- Global (cross-province); NOT province-parallel. `runs_after: [subsistence]`.

## Inputs (from WorldState)
- `technology.current_era` + `era_catalog` — regime gate
- `provinces[]`: `h3_index`, `links[].neighbor_h3` (reach = adjacency), `region_id`
- `provinces[].cohort_stats`: `total_population`, `subsistence_surplus_ratio`
  (military power = population x how well-fed)
- `npc_indices_by_home_province` + `significant_npcs[].capital` (the plunder prize
  and its holders)
- `world_seed`, `current_tick` (deterministic per-(year,attacker,defender) RNG)

## Outputs (to DeltaBuffer)
- `RegionDelta.war_mortality_replacement` (>= 1.0; floored at apply) — per-province
  war-casualty multiplier, consumed by population_aging's annual mortality
- `NPCDelta.capital_delta` — conserved plunder transfers (loser residents debited
  proportional to wealth, victor residents credited equally; sums to zero; no
  transfer at all if the victor has no valid resident to receive it)

## Module state (serialized)
- `relations_` — per-province-pair diplomatic relation in [-1, 1]; war sours
  (`relation_war_hit`), a peaceful adjacent year heals (`relation_peace_heal`), warm
  relations deter attack and pool coalition defence (`ally_threshold`), betrayal
  sours the betrayer's relations with all neighbours
  (`backstab_reputation_penalty`).
- `polity_of_` — emergent nesting polities (design §5.4): sparse province -> polity
  map; an absent province is its own polity (ownership emerges from settlement).
  Repeated decisive wins (`absorb_after_wins`, per directed pair via `win_counts_`)
  absorb the loser's WHOLE polity into the victor's; members are at internal peace
  and pool power (defence and offence); a member whose own power outgrows the rest
  of its polity secedes (`secession_power_ratio` — the hold problem).
- `leader_until_` / `member_since_` — the conqueror multipliers (design §5.5):
  ALEXANDER — a rare great commander (`leadership_rate`) multiplies the polity's
  power (`leadership_power_mult`) for `leadership_tenure_years`, after which the
  hold problem fragments what institutions never caught up with. GENGHIS — a polity
  whose power is predominantly steppe-bred (`steppe_arable_max`/`steppe_forest_max`,
  `cavalry_polity_min_share`) strikes 2-hop targets (reach past the ox-cart).
  ROME — integration grows with tenure (`cohesion_per_year_held`, capped at
  `cohesion_max_mult`): old conquests need proportionally more power to secede.
- Round-tripped via serialize_state/deserialize_state (version 3; v1/v2 blobs load
  without the newer sections).

## Invariants
- Conserved: war kills (via the cohort mortality path) and moves wealth; it never
  mints or destroys either.
- Deterministic: provinces in index order; ordered pair/betrayer sets; year-seeded
  RNG per directed pair.
- Modern war is out of scope (political_cycle's domain).
