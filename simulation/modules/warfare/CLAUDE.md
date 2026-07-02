# warfare — Developer Context

## What This Module Does
War between province-polities (M6c foundation). A polity attacks a REACHABLE neighbour
(adjacency = ox-cart reach) when the power balance favours it (EV decision). Power is
grounded in the economy (population × how well-fed). War is conserved — it kills (a
cohort-mortality spike both sides, defender worse), published as
`cohort_stats->war_mortality` and folded into mortality by population_aging (the
population war-dips).

## Tier: early (runs_after subsistence; GLOBAL, not province-parallel)

## Critical Rules
- Conserved: war kills (cohort war_mortality) AND plunders (proto-capital loser->victor,
  nets to zero). No minting. Territory transfer is a later layer.
- Rational EV: attack the weak (power gate) AND rich (prize weight), but warm RELATIONS
  deter it (allies don't fight). War sours relations; sustained peace warms them.
- Holds module STATE: relations_ (per-pair, [-1,1]), polity_of_/win_counts_ (emergent
  nesting polities: conquest absorbs whole polities, members pool power at internal
  peace, hold-failure secedes — design §5.4/§5.5), leader_until_/member_since_ (the
  conqueror multipliers: Alexander leadership surge, Genghis steppe 2-hop reach,
  Rome cohesion-by-tenure), + the war_state_dirty_ reset flag. Serialized (v3) —
  diplomacy and the political map survive save/load.
- ANNUAL cadence: one decision pass per year (current_tick % 365 == 0); all rates are
  per-year. On regime exit it publishes a one-time war_mortality=1.0 reset.
- Regime-gated to the pre-market arc; modern war is political_cycle's.
- Deterministic: provinces in index order; ordered pair sets; per-(year,attacker,
  defender) RNG. military_power()/pair_key() are pure/static (unit-tested).

## Design
- docs/design/EconLife_War_and_Diplomacy_v01.md (the full model: treaties, alliances,
  backstabbing, empires — Alexander/Genghis/Rome)
- docs/design/EconLife_Medieval_Band_Expansion_v01.md (§5.5, M6c)
