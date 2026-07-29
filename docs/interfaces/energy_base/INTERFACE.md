# energy_base — INTERFACE

**Package:** base_game · **Scope:** v1 · **Province-parallel:** yes
**Order:** `runs_after: [technology]`, `runs_before: [subsistence]`
**Cadence:** annual (coal is raised over a year, like the harvest)

## What this module is for

GHOST ACRES (R1B), and why one province industrialises and its neighbour does not (R1C).

An organic economy is bounded by photosynthesis on finite acres. Food, fodder for the
draft animals, firewood, charcoal for the forges, wool and timber all compete for the
same ground, so more of any one costs some of another. That bound is why every
pre-industrial society, however clever, eventually stopped rising — and it is a
structural problem for the model, not a cosmetic one: with a fixed land base the
carrying ceiling saturates, so Tainter's `B(C) = B_max(1 - exp(-gamma*C))` has a FIXED
`B_max` and every rise necessarily peaks at the same height.

Coal breaks the bound by substituting a STOCK for the flow. Burning a tonne does work
that would otherwise have taken woodland to grow, releasing acres that never existed.
England and Wales drew 4.3M acre-equivalents from coal in 1750, 11.2M in 1800 and 48.1M
by 1850 — more than the ~37M acres of their entire land surface.

The escape is temporary by construction. The coal is a finite located deposit, so a
society that industrialises is spending something it cannot replace; when the seam is
worked out the ghost acres go with it and the ceiling falls back to what the sun puts on
the fields.

## Inputs (from WorldState, const)
- `technology.current_era` — regime gate (the pre-market arc, same as subsistence)
- `technology.knowledge_level` — mining technique (saturating)
- `provinces[].deposits` — `ResourceType::Coal` entries: `quantity_remaining`, `quality`,
  `depth`, `accessibility`. The best seam in the province is worked (highest
  workability, ties by lower `id` for determinism).
- `provinces[].cohort_stats.total_population` — fuel demand
- `provinces[].cohort_stats.subsistence_surplus_ratio` — the REAL WAGE (consumption over
  subsistence), the same `w` the Malthusian valve runs on. This is the labour side of
  the induced-innovation ratio.

## Outputs (to DeltaBuffer)
- `RegionDelta.ghost_land_fraction_replacement` → `cohort_stats.ghost_land_fraction`.
  The extra land coal is standing in for, as a fraction of the ~4.1 acres a head a
  pre-industrial economy needed. Consumed by `SubsistenceModule::natural_capital_of`,
  which adds it at the same weight as arable land — because that is literally what it
  substitutes for.
- `RegionDelta.coal_burned_replacement` → `cohort_stats.coal_burned_per_year`
  (observability: the flow behind the ghost acres).
- `DepositDelta` — the CONSERVED drawdown on the named seam, in the deposit's own units.

## The laws

    workability   = quality * accessibility * (1 - depth)          [the fuel price]
    technique     = K / (K + mining_technique_halfsat)             [you must know how]
    adoption      = r / (r + halfsat),  r = wage / workability     [it must pay: R1C]
    wanted        = population * tonnes_per_head_per_year * adoption * technique
    burned        = min(wanted, what is in the ground)
    ghost         = burned * woodland_acres_per_tonne_coal
                        / (population * preindustrial_acres_per_head)

All four are pure/static and unit-tested against the historical series.

## Invariants
- **Conserved and located.** Every tonne burned comes out of one named seam in one named
  province, emitted as a `DepositDelta` in the deposit's own units. Nothing appears from
  nowhere, and an exhausted seam yields nothing.
- **No caps.** Every limit is physical: technique and adoption saturate (`x/(x+h)`), and
  the seam is finite. `std::max` appears only as divide-by-zero sentinels.
- **Regime-gated**, like subsistence and grain_logistics; `execute()` clears the
  published values exactly once on regime exit so no stale ghost acres survive into a
  market era.
- **Deterministic**: no RNG; the seam choice is total-ordered.

## Deliberate gaps
- **Productive capital is not a factor.** Pits, pumps and headgear obviously are capital,
  but `productive_capital` has no unit that converts to tonnes of coal, and any
  coefficient bridging them would be a number chosen to make the curve come out. Wiring
  it in properly needs the capital stock denominated in something real first.
- Only coal. Oil, gas and the renewables are the same mechanism with different stocks and
  different technique gates; the deposit types already exist.
- One seam per province per year (the best one), not a merit order across seams.

## Design
- docs/design/EconLife_Realism_Roadmap_v01.md (1B, 1C)
- Wrigley, *The Path to Sustained Growth*; Allen, *The British Industrial Revolution in
  Global Perspective*; Tainter, *The Collapse of Complex Societies*.
