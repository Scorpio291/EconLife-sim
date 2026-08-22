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
- **THE COMMONS EATS ITS ENVIRONMENT AND THE ENVIRONMENT ANSWERS.** Three located stocks,
  all of which can lower the carrying ceiling: `soil_health` (fertility, returns in a
  fallow generation), `forest_health` (standing forage and game, returns in a century) and
  `topsoil` (the ground itself — erodes when the land is over-worked AND the cover is
  gone, re-forms geologically, and is the CEILING soil_health recovers to). Geography
  fields are the PRISTINE endowment; the cohort stocks are the fraction retained. Anything
  consumed must be a stock with an inflow and an outflow — a static endowment that feeds
  people forever is a rail.
- **Sparable is not spared.** The stratum is `min(what the harvest can spare, what haulage
  can provision) x what can be CLAIMED`. The claim reach is reciprocity (village-scale,
  collapses with population) plus records (`codified_knowledge` per head). Without it the
  module asserts that anyone the fields do not need becomes a specialist, which no
  subsistence economy ever did.
- **The growth signal is measured at the labour the society COULD field**, never at what
  is left after the stratum is subtracted — `labor_needed` is solved against the same
  denominator, so the two divide to 1.0 by construction and the demography goes blind.

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
