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
- Conserved: war only kills (no minting). Spoils/territory transfer are later layers.
- Regime-gated to the pre-market arc; modern war is political_cycle's.
- Deterministic: provinces in index order; per-(year,attacker,defender) RNG.
- military_power() is pure/static (unit-tested).

## Design
- docs/design/EconLife_War_and_Diplomacy_v01.md (the full model: treaties, alliances,
  backstabbing, empires — Alexander/Genghis/Rome)
- docs/design/EconLife_Medieval_Band_Expansion_v01.md (§5.5, M6c)
