# grain_logistics — Interface Spec

The "tyranny of the ox" (medieval band §3.5): a draft team eats the grain it hauls,
so surplus has a hard economic radius; water is an order of magnitude cheaper than
land. Computes each province's net feedable surplus and the urban (town) population
that catchment can sustain. Design: docs/design/EconLife_Medieval_Band_Expansion_v01.md
(§3.5, M2–M3); generalizes per docs/design/EconLife_Logistics_and_Political_Scale_v01.md.

## Cadence & gating
- Regime-gated to `GrainLogisticsConfig.active_regimes` (pre-market arc); inert in
  market eras. Global (cross-province); NOT province-parallel.
  `runs_after: [subsistence]`.

## Inputs (from WorldState)
- `technology.current_era` + `era_catalog` — regime gate
- `provinces[]`: `h3_index`, `links[]` (`neighbor_h3`, `type` land/river/maritime,
  `transit_terrain_cost`, `infrastructure_bonus`), `region_id`
- `provinces[].cohort_stats.grain_surplus` (published by subsistence)
- `provinces[].cohort_stats.total_population` (urban cap)
- `hazard_settings.gravity_g` (heavier world -> smaller haulage radius)

## Outputs (to DeltaBuffer)
- `RegionDelta.net_feedable_surplus_replacement` — own surplus + what neighbours
  deliver after the oxen eat their share (conserved: delivered + eaten == exported)
- `RegionDelta.urban_population_replacement` — net feedable surplus / per-capita
  food, capped at total_population (the aggregate town economy)

## Invariants
- Conserved allocation (each source distributes its surplus exactly once).
- Deterministic: sources in province order, destinations sorted by index.
- `delivered_fraction()` is pure: water >> land; mountains -> 0; roads raise;
  gravity lowers.
