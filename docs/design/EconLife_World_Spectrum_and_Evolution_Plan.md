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

## 1. Why now / what it fixes
We just closed three dawn loops (food→surplus→population, →proto-capital,
→specialization) but they have **no long-horizon behavioral test** — the emergence
gate runs at era 5, not the dawn. Tests prove the loops *fire and stay bounded*, not
that they produce a *believable society*. As loops multiply, hand-tuning the
parameter space (`ceiling_per_capital_unit`, `proto_capital_rate`, birth/death
sensitivities, `max_specialist_fraction`, …) won't scale. This plan replaces "vibes"
calibration with an observable, gateable spectrum.

---

## 2. Part A — The world dial (paradise ↔ deathworld)
Two layers: **planetary baseline** (constants of the body) and **world axes**
(regional/temporal harshness). Composable presets; any knob overridable. Data-driven
(`packages/base_game/world_archetypes/*.csv`) so worlds are authored/tuned without
recompiling, and eventually exposed in the UI. Together they cover the popular-fiction
"Earth is a deathworld" factors: disease, disasters, storms, seasonal temperature
swing, predators, and gravity.

**Layer 1 — Planetary baseline** (global multipliers; per MHG, `PlanetaryParameters`
already abstracts the body):
- **Gravity** — the standout. Not "bounty" or "volatility"; it raises the **baseline
  cost of all effort**: less food output per worker (harder labour), costlier
  construction/transit, higher injury/exertion mortality. A heavy world taxes
  everything at once — a quiet, pervasive form of harshness distinct from acute
  hazards.
- **Insolation / base temperature**, **day length**, **atmospheric density** —
  global scalars on growing potential, metabolic load, and activity.

**Layer 2 — World axes (0..1, or a multiplier):**

| Axis | Covers (Earth-as-deathworld factors) | Scales (existing fields) | Paradise → Deathworld |
|---|---|---|---|
| **Bounty** | scarcity of food/resources | agricultural_productivity, arable_land_fraction, forest_coverage, fisheries carrying_capacity, deposit richness, subsistence `ceiling_per_capital_unit` | abundant → barren |
| **Seasonality** | large seasonal temperature variance, growing-season length | climate temp amplitude, growing-season window, year-to-year yield variance | mild → extreme swings |
| **Cataclysm** | natural disasters, storms, floods, droughts, quakes, outbreaks (acute) | drought/flood frequency & severity, random-disaster rate | calm → frequent/severe |
| **Hostility** | endemic disease load, large predators, (later) raiding | baseline sick_rate / disease pressure, natural-death pressure, predation hazard | benign → lethal |
| **Isolation** *(optional)* | connectivity, trade reach | transit costs, trade access, neighbour connectivity | connected → cut off |

**Presets** (compose the axes), anchored so **`earthlike` is mid-HIGH harshness —
the survivable-deathworld crucible — not the gentle baseline**:
- `paradise` — sub-Earth harshness across the board (stagnation risk).
- `earthlike` — the reference: real disease/disaster/seasonality/predators at 1g; the
  forge that produces capable societies.
- `harsh` — above Earth; survivable only with discipline and luck.
- `deathworld` — supra-Earth on multiple axes; mostly lethal, rare hardened survivors.
- mixed, e.g. `fertile_but_plagued` (high Bounty, high Hostility) or `heavy_eden`
  (abundant, calm, but punishing gravity).

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
Not a single "acceptable" band — assert the **spectrum behaves sensibly**:
- **Monotonic response**: harsher Bounty/Volatility/Hostility ⇒ lower steady-state
  population, less surplus, slower/no development; gentler ⇒ growth & progression.
  (Run a sweep of archetypes; assert the ordering holds.)
- **Per-archetype envelopes**: `deathworld` mostly `Extinct`/`BareSubsistence`;
  `paradise` survives (rarely `Extinct`); `temperate` produces `Developing` at a
  meaningful rate. Bands, not exact values.
- **Invariants always**: finite/bounded, deterministic, no runaway.
This is the CI gate that catches calibration regressions on everything added next.

## 5. Part D — Make the loops consequential (so evolution can happen)
The harness only shows "evolution" if the loops have teeth. Fold in the
"consequences are thin" debt:
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
1. **Axis set** — Bounty / Volatility / Hostility (+ Isolation)? Or a different
   decomposition?
2. **Single dial vs multi-axis** — one "harshness" slider (simple) or independent
   axes (richer; enables `fertile_but_plagued`)? (Recommend multi-axis with presets.)
3. **Where the dial lives** — pure config now, with a UI surface later? (Recommend
   yes — headless config first, UI consumes the same presets.)
4. **First slice** — build P1 (harness) or P2 (dial) first? (Recommend **P1**: the
   ability to *observe* is the prerequisite for tuning anything.)
5. **Scope/Tier** — is the spectrum a V1 feature or a post-V1 calibration tool?
   (Affects how much UI vs headless-only.)
