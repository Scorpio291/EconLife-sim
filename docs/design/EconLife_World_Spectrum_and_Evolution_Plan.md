# World Spectrum & Society-Evolution Harness — Plan

*Status: proposal. Folds the improvement list (calibration, behavioral validation,
consequential loops, dev-experience) into one organizing goal: a tunable world
parameter space from **paradise → deathworld**, and a harness to run a society
forward from the dawn and observe **whether and how it evolves**. Companion to
`EconLife_Mechanical_History_Generation_Plan.md` (MHG) and
`EconLife_Origin_Economy_and_Early_Jobs_v01.md` (origin economy).*

## 0. The goal
Two capabilities, one feedback loop:
1. **Dial the world** — slide from a bountiful, gentle world to a barren, lethal one
   (and tune any axis independently), via a small data-driven knob set with presets.
2. **Watch the society** — run that world from the dawn (era 1) for decades→centuries
   and observe the trajectory: does the population survive, stagnate, develop
   (surplus → specialists → capital/inequality → firms → era advance), or overshoot
   and crash?

The same harness serves as **exploration tool** (see what a world does) and
**calibration gate** (assert worlds respond *plausibly* to their parameters).

### The design hypothesis worth testing
"How **or if** a society evolves" is the interesting part. The expectation:
- **Deathworld** → extinction or permanent bare subsistence (no surplus to spend).
- **Paradise** → survives easily but may **stagnate** — little pressure/necessity to
  specialize or innovate.
- **The temperate middle** → a "Goldilocks" band that develops fastest: enough
  surplus to free labour, enough pressure to make it worthwhile.

If that curve emerges from the mechanics rather than being scripted, the foundation
is working. The harness is how we'd see it.

### Harshness is relative — Earth *is* a deathworld
Crucial framing: a world is only a "deathworld" relative to who is adapted to it. By
galactic-fiction standards **Earth is a deathworld** — endemic disease, storms,
quakes, drought/flood, wide seasonal temperature swings, large predators, a heavy
1g pull — and that hostility is precisely what *forged* a capable, resilient species.
A true paradise would never have produced one.

So harshness must NOT be modeled as a flat penalty. The relationship is a curve with
a **survivable-adversity band**:
- **Below it (paradise)** → soft, stagnant; no necessity drives specialization or
  invention; and a population bred here is fragile if conditions ever worsen.
- **Inside it (Earthlike → hard-but-survivable)** → the **crucible**: sustained
  pressure that doesn't kill forges surplus discipline, specialization, and — over
  generations — an *adapted, hardened population* (see §5, adaptation loop).
- **Above it (deathworld)** → adversity outruns adaptation; extinction or bare
  subsistence.

This re-anchors the presets: **Earthlike is the mid-HIGH reference, not the gentle
baseline.** The same absolute world is a graveyard to an unadapted (paradise-bred)
society and home to a hardened one — which the harness can test directly via
*transplant* runs (drop a soft society onto a hard world, or vice-versa).

If the engine reproduces "adversity forges capability, up to a lethal threshold"
without it being scripted, that is the strongest possible signal the foundation
works.

### The headline success criterion (the Deathworlders test)
We adopt the *Deathworlders* (Jenkinsverse) **Deathworld Class** scale as the world's
headline rating (§2): Garden (1–3) → Typical (4–7) → Harsh (8–10) → Deathworld
(11–13) → Extreme (14+), with **Earth = Class 12**. The series' central premise — the
galaxy assumed advanced intelligence could only arise on gentle **Garden Worlds**,
and a technological species from a **Class-12 Deathworld** overturns that — *is* our
prime calibration target, stated as canon:

> A `garden` world should reliably survive but tend to **stagnate**; a `deathworld`
> should mostly **kill or stall** — yet a fertile Class-12 (`earthlike`) world should,
> at least sometimes, **forge an advanced, resilient society** that out-toughs any
> Garden-World society. That rare-but-real outcome, emerging unscripted, is the bar.

## 1. Why now / what it fixes
We just closed three dawn loops (food→surplus→population, →proto-capital,
→specialization) but they have **no long-horizon behavioral test** — the emergence
gate runs at era 5, not the dawn. Tests prove the loops *fire and stay bounded*, not
that they produce a *believable society*. As loops multiply, hand-tuning the
parameter space (`ceiling_per_capital_unit`, `proto_capital_rate`, birth/death
sensitivities, `max_specialist_fraction`, …) won't scale. This plan replaces "vibes"
calibration with an observable, gateable spectrum.

---

## 2. Part A — The world dial & the Deathworld Class
The headline knob/output is a **Deathworld Class**, adopting the scale from *The
Deathworlders* (Jenkinsverse): a single derived number that aggregates a world's
hazards, with named bands. You can dial the underlying factors and watch the class
fall out, or pick a target class/preset and let it distribute.

| Class | Band | Description |
|---|---|---|
| 1–3 | **Garden** | extremely safe; minimal predators/disease/hazards |
| 4–7 | **Typical** | most species evolve here |
| 8–10 | **Harsh** | stronger gravity, tougher climate, more aggressive ecosystems |
| 11–13 | **Deathworld** | survival demands constant adaptation |
| 14+ | **Extreme** | beyond most deathworld standards |

**Earth = Class 12** is the calibration anchor: the `earthlike` preset must compute
to ~12. (Per the source, the exact formula is unspecified and deliberately subjective
— rated from the perspective of fragile galactics, possibly an *under*-estimate — so
we define our own derived score, anchored to Earth≈12, not a canon formula.)

### Two orthogonal dimensions: Hazard (the Class) × Bounty
A key separation: the **Class measures hazard** (how hard it is to *survive*), while
**Bounty** measures resource abundance (how much there is to *develop with*). They are
independent — a world can be deadly *and* fertile, or safe *and* barren. This gives a
2-D space with Earth in the most interesting quadrant:

```
              high Bounty
                  │
   fertile garden │ FERTILE CRUCIBLE  ← Earth (Class 12, fertile):
   (soft, rich,   │ deadly enough to forge, rich enough to fund
    stagnant)     │ development
  ────────────────┼──────────────── high Hazard (Class) →
   barren garden  │ barren deathworld
   (poor, stuck)  │ (kills, or bare subsistence forever)
                  │
              low Bounty
```
The hypothesis (§0) in these terms: advanced society needs **hazard high enough to
forge capability** *and* **bounty high enough to fund it** — Earth hits both.

### Factors that compute the Class (covering the Jenkinsverse list)
Data-driven (`packages/base_game/world_archetypes/*.csv`); each scales fields that
mostly already exist. **Planetary baseline** (body constants; per MHG
`PlanetaryParameters` already abstracts the body) + **world axes** (regional/temporal).

| Factor | Layer | Jenkinsverse factor | Scales (engine) |
|---|---|---|---|
| **Gravity** | planetary | surface gravity | baseline cost of *all* effort — food/worker, construction, transit, exertion mortality |
| **Atmosphere** | planetary | atmospheric conditions | breathability/toxicity, metabolic load |
| **Radiation** | planetary | radiation levels | chronic mortality/illness, surface habitability |
| **Insolation / day length** | planetary | (climate base) | growing potential, activity rhythm |
| **Seasonality** | axis | climate extremes | temp amplitude, growing-season window, yield variance |
| **Cataclysm** | axis | geological activity + storms | quakes/volcanoes + floods/droughts/wildfires; disaster rate & severity |
| **Biohazard** | axis | predators, toxic flora/fauna, disease/parasites, evolutionary competition | sick_rate/disease pressure, predation, toxicity, natural-death pressure |
| **Bounty** *(not a hazard)* | axis | — | agricultural_productivity, arable_land, forage, fisheries, deposits, subsistence ceiling |
| **Isolation** *(not a hazard)* | axis | — | transit costs, trade access, connectivity |

`Class = weighted_aggregate(gravity, atmosphere, radiation, seasonality, cataclysm,
biohazard, …)`. Bounty and Isolation sit *outside* the class (they shape development,
not danger). Gravity is the standout hazard — a quiet, pervasive tax that, unlike
acute events, makes *everything* cost more all the time.

**Presets** (by class; Earthlike anchored at the mid-high reference, not the easy
baseline):
- `garden` (Class ~2) — safe and gentle; **stagnation risk**, and a fragile,
  unadapted population.
- `typical` (Class ~5) — most "baseline" worlds.
- `earthlike` (Class 12, fertile) — the **fertile crucible**: real
  gravity/disease/predators/disasters/seasons, but bountiful. The reference that
  *should* be able to forge an advanced society.
- `deathworld` (Class ~13) — survival demands constant adaptation; mostly lethal,
  rare hardened survivors.
- `extreme` (Class 14+) — almost always fatal to a founding population.
- mixed, e.g. `barren_deathworld` (high hazard, low bounty — kills or stalls) or
  `heavy_eden` (low biohazard but punishing gravity).

**Design notes:**
- The dial spans **two timing layers** — world-gen-time (geography/natural capital,
  resource deposits) and **runtime** behavioral scalars (mortality sensitivity,
  disaster frequency). One archetype must touch both coherently → a
  `WorldArchetypeConfig` resolved at world-gen that writes both the generated
  substrate and the relevant module configs (subsistence, population_aging,
  random_events).
- Keep it **deterministic**: archetype only scales parameters, never adds entropy
  outside DeterministicRNG.
- Reuse, don't reinvent: most knobs already exist scattered across WorldGen stages
  and module configs; this consolidates them behind a coherent front.

## 3. Part B — The society-evolution harness
Extend the existing emergence harness (`simulation/tests/integration/emergence_harness.h`,
`emergence_observe`) rather than build anew.

- **Run from the dawn**: founding seed, `starting_era = 1`, archetype-parameterized.
- **Horizon**: decades → centuries (perf-gated; see Part E / MHG P3 LOD).
- **Annual society time-series** (observable WorldState only):
  population & growth rate; mean surplus & its spread; specialization
  (occupation layer-1 vs layer-2 fractions); proto-capital total **and inequality
  (gini)**; settlements; era/tech tier; (once it exists) firm count; and **event
  markers**: famine, population crash, extinction, era advance, first firm.
- **Two outputs**: a human-readable trajectory dump (exploration) and structured
  metrics (gating).
- **Trajectory classifier** — label each run: `Extinct | BareSubsistence |
  Developing | OvershootCrash | Thriving`. This is the unit the gate reasons about.

## 4. Part C — Calibration as response-plausibility
Not a single "acceptable" band — assert the **spectrum behaves sensibly across the
Deathworld Class**:
- **Monotonic response**: rising Class (hazard) ⇒ lower survival, lower steady-state
  population, slower/no development; rising Bounty ⇒ more surplus & faster development.
  (Sweep Class × Bounty; assert the ordering holds.)
- **Per-band envelopes** (Class bands, not exact values):
  `garden` survives but mostly `BareSubsistence`/stagnant; `typical` develops at a
  modest rate; `deathworld` mostly `Extinct`/`OvershootCrash`; `extreme` almost always
  `Extinct`.
- **The Deathworlders bar**: a fertile **Class-12 `earthlike`** world produces a
  `Thriving`/advanced trajectory at a meaningful-but-minority rate — more than a
  `deathworld`, and reaching *higher capability/hardiness* than a `garden` ever does.
- **Invariants always**: finite/bounded, deterministic, no runaway.
This is the CI gate that catches calibration regressions on everything added next.

## 5. Part D — Make the loops consequential (so evolution can happen)
The harness only shows "evolution" if the loops have teeth. Fold in the
"consequences are thin" debt:

### The knowledge engine — how a society actually moves *forward*
The harness already exposed the gap: dawn societies reach `Developing` but **plateau
at era 1** — surplus rises, specialists emerge, yet nothing advances. That is the
**Malthusian trap**: in a pure subsistence economy population tracks carrying
capacity, so surplus is always competed back to ~1. The *only* escape is raising
productivity faster than population — and that is what **knowledge** does.

So there is a third livelihood layer above food (Layer 1) and crafts/services
(Layer 2): **knowledge-producers** — scholars, scribes, mathematicians, astronomers,
priests-as-researchers. (Historically the earliest: Egyptian geometry for land and
flood, astronomy for the calendar, writing for administration ~5000-6000 BC.) The
loop:

> surplus frees scholars → scholars produce **knowledge** (feeds the existing
> `technology` module: domain_knowledge, research, era transitions) → knowledge
> **raises the carrying ceiling** (better farming/tools) and **advances eras** →
> a higher ceiling frees *more* surplus → more scholars. A virtuous cycle — the
> actual engine of "moving forward."

Design consequences:
- **Era progression must be knowledge-driven, not calendar-driven.** Re-base the
  hardcoded calendar-year era triggers so advancing out of subsistence requires
  *accumulated knowledge* (which scholars produce), gated by surplus/population.
- **Progress is contingent, not guaranteed** — exactly the "we have no idea how it
  would have moved forward" point. A society with surplus but no scholars (none
  freed, or surplus spent on other specialists / hoarded by elites) **stagnates** —
  rich but static. Whether and *which* scholars emerge is an emergent variable that
  shapes (or stalls) the whole trajectory. This is what turns `Developing` into a
  real `Thriving`/era-advancing arc — and what makes the Deathworlders test
  meaningful (a hard world that nonetheless funds knowledge is what forges an
  advanced species).
- Likely the single highest-value build for *watching a society move forward*.

### Other teeth
- **Occupations do work** — specialists actually contribute (craft goods, healing
  lowers sick_rate, traders enable exchange), so specialization has a payoff/cost.
- **Proto-capital concentrates** — current even split produces equality; weight
  accrual (e.g. toward the granary-keeper / by capital already held) so **inequality
  emerges** and can be observed/tuned.
- **Era progression driven by the dawn variables** — advance out of subsistence on
  surplus/population/specialization thresholds (re-base the hardcoded
  calendar-year era triggers, which are still modern-anchored), so a thriving
  society *advances eras* (= "evolves") and a stagnant one doesn't.
- **Crystallization rule** — a livelihood becomes an `NPCBusiness` when a specialist
  accumulates capital **and** employs surplus-freed labour to meet unmet demand
  (the "when is it a business" answer, made mechanical). Needs Band-2 content.
- **Population adaptation / hardiness loop** — the mechanism behind "Earth made us
  tough." Sustained *survivable* pressure (hostility/seasonality/cataclysm a society
  endures without collapsing) slowly raises a population's hardiness (mortality
  resistance, disease tolerance, labour under load); abundance/calm lets it drift
  back down. This makes harshness **relative to the adapted population**: a hardened
  people cope where a paradise-bred one would perish, and it is what lets the
  *transplant* tests in §0 mean something. Likely sits on the trait/demographics
  model (see `EconLife_Trait_System.md`); a later phase, but it is the loop that
  turns "adversity" into "capability" rather than just "death".
  - **Partially landed:** *Neolithic hardiness* (`PopulationAgingConfig.neolithic_hardiness`)
    — a people NATIVE to a harsh world are adapted to it, and at the subsistence
    level (survival = physical toughness) that adaptation offsets their own world's
    hazard mortality. So a harsh world's natives survive its dangers about as well
    as a gentle world's people (and, tuned > 1, *better*) — what then limits a
    society is food (bounty) and climate (seasonality), not the hazards it evolved
    under. Still TODO: the *generational* loop (hardiness rising/falling over time)
    and transplant scenarios (a soft people dropped onto a hard world).

## 6. Part E — Enabling work (threaded throughout)
- **Test-suite tiering** — the full V1 integration suite is ~20 min in debug; split a
  fast per-commit smoke subset from the slow multi-century runs (nightly/Release).
  Prereq for long-horizon gates to be affordable.
- **Perf / LOD for century scale** (MHG P3) — cohort/aggregate fidelity during
  history; the make-or-break for long runs.
- **Content for early eras** — eras 1–4 are mechanically alive but goods-empty
  (all content sits at era 5); author per-era goods/recipes alongside the mechanics
  so the engine stops outrunning the content.
- **Hygiene** — shared CSV util (parsing now duplicated across catalogs); structured
  persistence-schema bumps; a planned end-state for the `NPCRole` ⇄ `occupation`
  split.

---

## 7. Phasing (each independently useful)
1. **P1 — Harness from the dawn.** Extend the emergence harness to era-1 founding
   runs + the society time-series + trajectory classifier. *Immediately useful on
   the loops we already have (survival/population/surplus); grows as P3 lands.*
2. **P2 — The world dial.** `WorldArchetypeConfig` (axes + presets), wired into
   world-gen and the runtime module configs. *Now you can slide paradise↔deathworld.*
3. **P3 — Consequential loops** (Part D). *Now societies actually develop or fail.*
4. **P4 — Calibration gate** (Part C) + test-suite tiering. *Locks the spectrum in.*
5. **P5 — Enabling** (perf/LOD, early-era content, hygiene). *Ongoing; unblocks
   century-scale and richer worlds.*

**Why this order:** the harness comes first so we can *see* while we tune; the dial
makes the space explorable; teeth make evolution real; the gate makes it durable.
P1+P2 are small and high-signal — within a couple of focused sessions we could be
dialing worlds and watching the first trajectories, even before the loops have full
teeth.

## 8. Open decisions
1. **Factor set & Class weighting** — the hazard factors (gravity, atmosphere,
   radiation, seasonality, cataclysm, biohazard) and the weights that aggregate them
   into a Class, anchored so `earthlike` ≈ 12. What's the weighting, and is the Class
   a simple weighted sum or something non-linear (e.g. multiple high factors compound)?
2. **Hazard vs Bounty as the two headline dials** — confirm the orthogonal split
   (Class = survival difficulty; Bounty = development potential), with Earth in the
   "fertile crucible" quadrant?
3. **Where the dial lives** — config now (`WorldArchetypeConfig`), UI later consuming
   the same presets? (Recommend yes.)
4. **First slice** — build P1 (harness) or P2 (dial) first? (Recommend **P1**: the
   ability to *observe* is the prerequisite for tuning anything.)
5. **Scope/Tier** — V1 feature or post-V1 calibration tool? (Affects UI vs
   headless-only.)
6. **Adaptation depth** — is the population hardiness loop (§5) in scope soon, or a
   later phase? It's what makes "Class is relative to the adapted population" real and
   gives the transplant tests meaning.
