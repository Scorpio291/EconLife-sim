# structural_demography — Developer Context

## What This Module Does
THE ENDOGENOUS FALL. Turchin's Political Stress Index: a society can come apart for
reasons that have nothing to do with the weather. Multiplicative in popular immiseration,
elite overproduction (the gap between the non-farming stratum a society HOLDS and the one
its harvest SUPPORTS) and fiscal exhaustion — all three at once, or nothing.

## Tier: mid (runs_after subsistence, runs_before population_aging; province-parallel)

## Critical Rules
- The stress is not a mood. It kills (published as an independent competing risk applied
  through the normal cohort mortality path), it eats (retinue rations drawn from the
  granary, conserved, counting only the EXTRA over the ordinary ration), and it erodes
  stability.
- Annual cadence — secular cycles run centuries.
- Regime-gated to the pre-market arc; `execute()` clears published values once on exit.
- No RNG. No caps: the PSI is uncapped and the death fraction arrives as 1 - exp(-rate).

## Key Types
- StructuralDemographyConfig (core/config/package_config.h)
- reads cohort_stats->{specialist_fraction, supported_specialist_fraction,
  subsistence_surplus_ratio, working_age_fraction, food_store} + community.institutional_trust
- writes RegionDelta.{political_stress,faction_death_fraction}_replacement,
  food_store_delta, stability_delta

## Interface Spec
- docs/interfaces/structural_demography/INTERFACE.md
