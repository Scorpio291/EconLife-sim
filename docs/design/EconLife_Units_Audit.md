# EconLife — The Units Audit

**2026-08-23.** Ratified standard: *we have our physical constants — gravity, the weak and
strong nuclear force, electromagnetism. Everything else must derive from those.*

Taken literally that is not computable; nobody derives the price of grain from QCD. Taken as
a standard for what a constant may BE, it is exact, and this is the map of how far the model
is from meeting it.

---

## The four kinds a constant may be

1. **Fundamental** — G, c, the coupling constants.
2. **A measured property of this world instance** — stellar luminosity, orbital radius,
   planet mass, axial tilt, crustal composition. An input, not a choice.
3. **Derived from 1 and 2 by a stated chain** — and the chain must be written down.
4. **A measured property of the things in it** — human basal metabolism, wheat's energy
   density, an ox's draught power, the service life of a thatched roof. Empirical, but
   checkable against the world.

A **labelled unit bridge** between model-internal scales is tolerated where nothing else is
available, provided it says so. Anything else — a number chosen because the output looks
right with it — is a rail.

## The root cause: almost nothing has a unit

Food is measured in "food". Land is measured in "natural capital units". Knowledge is
measured in "technique-equivalents". A constant with no unit **cannot be checked against a
measurement**, so the only test available when it needs a value is "does the output look
right", which is the definition of a rail. Every rail removed this session was of that kind,
and several were only found when a quantity was printed next to the bound it was sitting on.

## What is actually there

832 scalar constants across 62 config structs.

| | in the climb's causal path | modern game | total |
|---|---|---|---|
| constants | 235 | 597 | 832 |
| **no comment at all** | 53 (23%) | 471 (79%) | **524 (63%)** |

**63% of the model's constants carry no stated justification whatsoever.** That figure is
hard — it is simply the absence of a comment.

Among the 308 that ARE documented, quality varies from a sourced real-world figure to a
one-line restatement of the variable name. A hand sample of twelve found seven with a
genuine anchor ("a centimetre of topsoil every two to four centuries", "share of population
an agrarian polity can field", "two orders of magnitude, from the manuscript-to-print book
counts") and five that only describe what the number is for. So roughly half the documented
constants are genuinely justified — call it **~150 of 832, or 18%, that meet the standard**.

Automated classification undercounts this badly (it flagged `topsoil_formation_per_year` as
unjustified when its comment cites the real weathering rate), so the per-class figures below
are a lower bound on quality and an upper bound on the problem. The undocumented count is
the reliable one.

## Where the arbitrary layer concentrates

Of the climb's own modules, ungrounded share (no comment, or a comment that only restates
the name):

| config | ungrounded | share |
|---|---|---|
| RegionalConditionsConfig | 20/20 | 100% |
| CommunityResponseConfig | 14/14 | 100% |
| TradeInfrastructureConfig | 9/9 | 100% |
| GrainLogisticsConfig | 8/9 | 89% |
| WarfareConfig | 26/31 | 84% |
| SeasonalAgricultureConfig | 13/16 | 81% |
| SubsistenceConfig | 29/53 | 55% |
| PopulationAgingConfig | 16/33 | 49% |
| KnowledgeConfig | 8/26 | 31% |
| StructuralDemographyConfig | 2/7 | 29% |
| TechnologyAdoptionConfig | 2/10 | 20% |
| EnergyBaseConfig | 1/7 | 14% |

Two structural patterns stand out, and both are rail-shaped by construction:

- **Weighted-sum indices.** `RegionalConditionsConfig` builds province stability from seven
  weights (`stability_w_employment` 0.25, `_infrastructure` 0.15, `_trust` 0.10, `_crime`
  0.30, `_criminal_dominance` 0.20, `_inequality` 0.15, `_grievance` 0.20). Nothing in the
  world sets those. They exist to make an index come out.
- **Normalisers.** `CommunityResponseConfig` carries `capital_normalizer` 10,000,
  `social_normalizer` 50, `grievance_normalizer` 10 — three divisors with no units, turning
  real quantities into dimensionless scores so they can be summed.

## Cleared in this pass

`max_specialist_fraction` and the seven `specialist_ceiling_*` constants (0.15 subsistence
through 0.45 industrial) are deleted, along with the two accessors and the unit test that
asserted the schedule they encoded. This was the rail R12 replaced months ago; the constants
outlived the code that read them, and **a test was still guarding the shape of a rail nothing
used.** That is how a rail comes back.

## The chain that is actually available

The derivation runs further than it looks, and it runs through UNITS. For the food economy:

> stellar luminosity and orbital radius → insolation (1,361 W/m² at 1 AU) → with temperature
> and water, net primary productivity → pre-industrial wheat ~1 t/ha at 3,300 kcal/kg =
> 3.3M kcal/ha/yr → human need ~730,000 kcal/yr (2,000/day, itself derivable from body mass
> by Kleiber's law) → **one hectare feeds ~4.5 people gross, 2-3 after seed corn and
> spoilage**

That one chain fixes numbers currently guessed at:

| constant | today | becomes |
|---|---|---|
| `ceiling_per_capital_unit` = 4000 | pure unit bridge | net primary productivity per hectare |
| `per_capita_food_per_tick` = 1.0 | a unit *definition* | human basal metabolism, kcal/day |
| `labor_half_saturation_per_extent` = 8000 | derived from an internal ratio | hectares a ploughman works |
| `topsoil_formation_per_year` = 0.0002 | already anchored | weathering rate from rainfall and temperature |
| fisheries r, K | world-gen inputs | marine NPP |
| `forage_recovery_per_year` | anchored on regrowth time | forest NPP |

And it validates by accident: modern wheat at 8 t/ha against pre-industrial 1 t/ha is an
eightfold rise, and `food_gain_max` was set to 12 by guesswork. Real units would have said
so, instead of the model finding out when six provinces carried 239 million people.

Surface gravity already derives (GM/r²), so the planet layer is partly built.

## Where the chain genuinely stops

The difficulty of inventing writing is not a physical quantity. Neither is how fast a
society learns, nor what share of a harvest a state can claim, nor how much solidarity a
frontier forges. These are historical and contingent.

The honest move is not to pretend they derive. It is to **isolate them in one labelled
layer** so the arbitrary part is small, visible and countable — instead of dissolved through
832 constants where nobody can see it. On the evidence above, that layer is currently
everything, and the work is to shrink it.

## Order of work

1. **Units for the food economy** — kcal and hectares. ~40 constants, the core of the dawn,
   and where every rail found this session lived.
2. **The planet layer** — derive insolation, temperature, growing season and weathering from
   the star and the orbit, so NPP falls out rather than being an input.
3. **Retire the weighted-sum indices** — stability and grievance should be consequences of
   located flows, not scores built from weights.
4. **Document or delete the remaining 524.** A constant nobody can justify is either a
   measurement waiting to be written down or a rail waiting to be found.
