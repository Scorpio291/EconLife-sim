# structural_demography — INTERFACE

**Package:** base_game · **Scope:** v1 · **Province-parallel:** NO (global — R5)
**Order:** `runs_after: [subsistence, warfare]`, `runs_before: [population_aging]`
**Cadence:** annual (secular cycles run centuries; this is a slow variable)

It is GLOBAL because a polity is one political unit whose stress is a property of the
whole of it, and because refugees move BETWEEN provinces — neither is expressible in a
province-parallel pass.

## What this module is for

THE ENDOGENOUS FALL. A society can come apart for reasons that have nothing to do with
the weather. As a population grows it depresses the real wage while inflating the incomes
of those at the top, so the number of people raised to expect a place above the plough
grows faster than the number of such places. Surplus claimants turn on one another, the
retinues they raise have to be fed by somebody, and the fiscal base erodes underneath.
Rome, the Han, late Ming, the Ottomans, France before 1789.

The model already had every ingredient and nothing joining them: a non-farming stratum
with generational inertia that persists through lean decades (R2C), a real wage (R2A), a
granary, and institutional trust. What was missing was that the GAP between the stratum a
society holds and the one its harvest supports is itself a destabilising force.

## Inputs (from WorldState, const)
- `cohort_stats.subsistence_surplus_ratio` — the real wage `w` (published by subsistence)
- `cohort_stats.working_age_fraction` — its complement is the youth/dependent share
- `cohort_stats.specialist_fraction` — the stratum the society HOLDS (has inertia)
- `cohort_stats.supported_specialist_fraction` — the stratum THIS harvest supports
- `cohort_stats.food_store` — the pre-modern treasury
- `Province.community.institutional_trust`

## Outputs (to DeltaBuffer)
- `RegionDelta.political_stress_replacement` → `cohort_stats.political_stress` (the PSI;
  observable, and deliberately uncapped)
- `RegionDelta.faction_death_fraction_replacement` → `cohort_stats.faction_death_fraction`.
  Real units (dead / population), consumed by population_aging in the SAME annual tick as
  an INDEPENDENT COMPETING RISK: `p = 1 - (1-p_env)(1-p_war)(1-p_faction)`. Kept separate
  from `war_death_fraction` on purpose — one is a polity fighting a neighbour, this is a
  polity coming apart inside, and a society can be doing both.
- `RegionDelta.food_store_delta` (additive, conserved) — retinue rations
- `RegionDelta.stability_delta` (additive) — structural stress erodes the ability to govern

## The laws

    mmp  = max(0, 1 - w/w_ref) * youth_share                 [who marches]
    emp  = max(0, held - supported) / max(sentinel, supported) [claimants per place]
    sfd  = (1 - granary_cover) * (1 - institutional_trust)    [a state that cannot pay]
    PSI  = mmp * emp * sfd
    p_faction = 1 - exp(-PSI * conflict_death_rate_at_unit_stress)

All five are pure/static and unit-tested.

### Stress contagion (R5, 2026-07-28)

Three changes, all about scope rather than new forces.

**Stress is the POLITY's, not the province's.** All three legs are computed over polity
aggregates — pooled treasury, combined stratum, population-weighted wage and youth share
— and every member province reads its state's index. Rome's third-century crisis was not
confined to one province. Measured per province in isolation the three legs almost never
rose together (the multiplicative form correctly demands that they do), so structural
collapse stayed a bleed of ~0.05%/yr. **Measured over the polity, peak PSI went from ~0.1
to 1.49.** It also supplies the nonlinearity: a rich province genuinely carries a poor one
because the treasury is pooled, until it cannot, and then the whole structure goes at once.

**Immiseration is measured against what people are USED TO** (`cohort_stats.wage_reference`,
a generation-scale average, persisted at schema v29), not against bare subsistence. This
turned out to decide whether a society can fall at all: a population whose numbers track
its food supply is never absolutely starving — the wage valve sees to that — so against
subsistence the mobilisation term was exactly zero for an entire 12,000-year climb, and
the multiplicative index was zero with it. Turchin's variable is the real wage against
trend and Davies' J-curve is the standard finding: revolutions follow reversals after
improvement, not steady poverty.

**Refugees carry the collapse across borders.** `flight_fraction` rises with the food
shortfall; people go only to reachable neighbours that are BETTER fed, weighted by how
much better, and the flow is conserved (`cohort_stats.refugee_flow`, applied to cohorts by
population_aging). A province whose neighbours are all worse off keeps its people and
starves in place — the Migration Period happened because there was somewhere to go.
Arrivals are more mouths on land that was only just feeding itself, so their surplus falls,
their stress rises, and they begin exporting in turn. Nothing models a cascade; that IS
one.

**Multiplicative on purpose.** This is the theory's sharpest and most falsifiable claim:
all three legs must be elevated at once. A miserable population under a united elite and
a solvent state does not bring the state down, and nor does a fractured elite over a
contented one. It is why most bad years are merely bad years, and why the ones that are
not are catastrophic.

## Invariants
- **Nothing is a mood.** Every effect is a located flow: people die through the normal
  cohort mortality path, grain leaves a named granary, stability falls.
- **Conserved.** The retinue draw counts only the EXTRA over the ordinary ration — those
  same people were already mouths in the harvest balance — and can never exceed what the
  granary actually holds.
- **No caps.** The PSI and the overproduction ratio are uncapped; the bound is physical,
  at `1 - exp(-rate)`. `min_positions_fraction` is a divide-by-zero sentinel.
- **Regime-gated**; `execute()` clears the published values exactly once on regime exit.
- **Deterministic**: no RNG.

## Deliberate gaps
- Elite FACTIONS are not individuated — there is no roster of rival houses, only the
  aggregate stress they generate. Naming them (and letting a faction win, and become the
  new state) is the next layer.
- The fiscal term uses the granary as the treasury. Once the government_budget module
  covers the pre-market arc, real debt and real tax capacity belong here instead.
- Stress is per province with no contagion between them; a real civil war spreads.

## Design
- docs/design/EconLife_Realism_Roadmap_v01.md (2D)
- Turchin & Korotayev, demographic-fiscal model (arXiv 1504.04688); Turchin,
  *Structural-Demographic Analysis* / PSI (PLOS ONE 2023); Goldstone, *Revolution and
  Rebellion in the Early Modern World*.
