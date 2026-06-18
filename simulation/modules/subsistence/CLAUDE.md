# subsistence — Developer Context

## What This Module Does
The commons food economy of the dawn. In pre-market eras the whole province
population works the land directly (forage/farm/hunt/fish/herd) — no firms, no
facilities, no money. Produces food from the province's natural capital, capped
by a labour-saturating carrying ceiling, and records the surplus over need
(`cohort_stats->subsistence_surplus_ratio`) — the master variable that later
frees labour for specialists and trade.

## Tier: early (runs_after technology; province-parallel)

## Critical Rules
- Regime-gated: active only when the current era's economic_regime is in
  SubsistenceConfig.active_regimes (subsistence/barter). Inert in market eras —
  the modern economy is untouched.
- Pure-commons: does NOT use goods/recipes/facilities/markets. Food is an
  abstract per-province balance produced directly from natural capital + labour.
- Deterministic; province-parallel results merge in province order.

## Key Types
- SubsistenceConfig (core/config/package_config.h)
- writes RegionDelta.subsistence_surplus_replacement -> cohort_stats

## Design
- docs/design/EconLife_Origin_Economy_and_Early_Jobs_v01.md
- docs/design/EconLife_Mechanical_History_Generation_Plan.md (Band 1)
