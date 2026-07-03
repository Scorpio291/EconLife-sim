# grain_logistics — Developer Context

## What This Module Does
The "tyranny of the ox" (medieval band, design §3.5). A draft team eats the grain
it hauls, so surplus has a hard economic radius and water transport is far cheaper
than land. Computes each province's **net feedable surplus** — own haulable surplus
(`grain_surplus`, published by subsistence) plus what neighbours deliver across
`ProvinceLink`s before the oxen eat it — and publishes it to
`cohort_stats->net_feedable_surplus`. This is the catchment surplus a town/castle
can be fed from (consumed by feudal genesis in M3).

## Tier: early (runs_after subsistence; GLOBAL, not province-parallel)

## Critical Rules
- **Conserved:** grain consumed in transit is accounted as draft sustenance, never
  vanished — `delivered + eaten == exported`. Each source allocates its surplus once
  across {self + neighbours}.
- **Regime-gated:** active only in the commons regimes (GrainLogisticsConfig.
  active_regimes); inert in market eras (no commons surplus).
- **Deterministic:** sources in province order; destinations sorted by index; double
  accumulation in that fixed order.
- `delivered_fraction()` is pure/static (water << land via LinkType; mountains and
  heavy gravity shrink it toward 0; roads raise it). No physical distance yet —
  single-hop neighbours; multi-hop reach + centroid distance are the D6 refinement.
- REAL FLOWS (G4): stored grain also DIFFUSES down the scarcity gradient along links
  (grain_trade_rate_per_year of the granary-fullness gap closes per year), emitted as
  additive food_store_delta with the transit loss eaten by the teams (conserved sink).
  The catchment signals are views; the diffusion is the flow on the actual granaries.

## Key Types
- GrainLogisticsConfig (core/config/package_config.h)
- reads cohort_stats->grain_surplus + Province.links; writes
  RegionDelta.net_feedable_surplus_replacement -> cohort_stats

## Design
- docs/design/EconLife_Medieval_Band_Expansion_v01.md (§3.5, M2)
- docs/design/EconLife_Logistics_and_Political_Scale_v01.md (the general law)
