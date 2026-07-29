# energy_base — Developer Context

## What This Module Does
GHOST ACRES: coal substitutes a finite STOCK for the photosynthetic FLOW that bounds an
organic economy, so the carrying ceiling can rise instead of saturating. Publishes
`cohort_stats->ghost_land_fraction`, which subsistence adds to natural capital at the
same weight as arable land. Also answers WHY one province industrialises (R1C): adoption
is gated on the real wage against what the seam actually costs to work.

## Tier: early (runs_after technology, runs_before subsistence; province-parallel)

## Critical Rules
- Conserved and located: every tonne burned is a `DepositDelta` on a named seam. The
  escape is finite — when the coal runs out, the ceiling falls back.
- Regime-gated to the pre-market arc; `execute()` clears published values once on exit.
- No RNG. Seam choice is total-ordered (workability, ties by id).
- Constants are real units (Wrigley's acre-equivalents, English coal output series).
  `tonnes_per_deposit_unit` is a documented unit bridge, not a behaviour dial.

## Key Types
- EnergyBaseConfig (core/config/package_config.h)
- writes RegionDelta.{ghost_land_fraction,coal_burned}_replacement + DepositDelta

## Interface Spec
- docs/interfaces/energy_base/INTERFACE.md
