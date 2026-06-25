# Mechanical History Generation — Architecture & Plan

**Status:** Proposed (planning). Authored 2026-06-16 per design decision.
**Decision:** The human-history layer of world creation shifts from *backward-narrated
flavor* (seed a full economy at t≈2000; Stage 10.2 invents a plausible past) to
*forward-simulated emergence*: generate the physical world + a minimal founding
population, then **run the simulation orchestrator for decades/centuries of pre-game
history** so settlements, firms, fortunes, nations, and crime actually develop, crash,
and evolve. The starting world is the *true outcome* of simulated history. The player
chooses where (and when) to enter. (Supersedes the "not real-time simulation" principle
in `EconLife_WorldGen_v016_updated.md` for the human layer; the physical pipeline is
unchanged.)

This is a large, multi-phase evolution. This doc is the source of truth; update as
phases land.

---

## 1. The key enabler: the play-sim *is* the history engine

There is no separate "history simulator." The tick orchestrator that runs gameplay runs
history. The headless multi-year driver **already exists** — `run_world_years(seed,
npc_count, years, …)` in `simulation/tests/integration/` runs the full orchestrator for
N in-game years, deterministically (the emergence diagnostic uses it for 5-year runs).
History generation = run that from a founding state for far longer, manage performance
via LOD, and snapshot for player entry.

## 2. Why this session's foundations are prerequisites (not incidental)

Deep time is unforgiving — a flaw invisible over a 10-year game is fatal over 200
simulated years. The recent work is exactly what makes forward history viable:
- **Conservation of matter/energy; no fake rails; removed arbitrary wealth caps; killed
  the drug revenue-feedback runaway.** A backward-narrated world never needed these; a
  forward-simulated one *cannot run centuries without them* — any exponential or
  arbitrary peg would make every civilization flatline or explode.
- **World-gen ↔ goods/production RNG decoupling.** History runs on a fixed physical
  substrate with the economy as a decoupled layer — precisely "generate world, then run
  history on it." Editing the catalog can't reshape the substrate.
- **Balancing loops with real bite** (crime↔enforcement↔governance; restoring forces on
  grievance/conditions). These are what let civilizations *rise and fall* rather than
  monotonically grow or collapse — the "crashing and evolving" of the vision.

## 2a. Scale: the whole planet (via LOD), not a 6-province slice

The world is **planet-scale**. This is not new architecture — it's the existing LOD
design applied globally:
- **H3 hexagonal grid (res 2–9)** already provides a planet-scale cell system;
  `target_province_count` is a parameter (V1 default 6). Planet scope raises it
  (provinces as H3-cell aggregations across the globe).
- **The LOD tiers (V1) are the planet-scale enabler:** LOD 0 (full, individual NPCs/
  firms), LOD 1 (simplified trade partners), **LOD 2 (statistical — cohorts/aggregate
  economy, "global signals without proportional compute").**

Therefore **mechanical history runs the entire planet at LOD 2** (statistical cohort/
aggregate dynamics everywhere), with LOD 0 detail materialized **only** for the player's
chosen region at entry. The founding seed is **planet-wide** (sparse founding population
across the globe), not a handful of provinces. This is the same insight V1's LOD system
was built on, extended from "rest of world during play" to "the whole world across deep
time." Full-detail-everywhere (LOD 0 globally) remains EX; statistical-planet is V1.

## 2b. Timeline span: earliest history → space age and beyond

The simulated timeline is the **full arc of human development**, and the player chooses
*where on it to enter* — from the earliest founding settlements through agrarian,
classical, industrial, and modern eras into the space age and beyond. The starting
"present" is not fixed at year 2000; **2000 becomes just one selectable entry point** on
a much longer line.

This makes the **era system the spine** of the whole design, and it is the largest
content/design lift:
- **The era arc must extend backward.** Today the era enum is anchored at 2000
  (`era_1_turn_of_millennium`) and runs forward to ~2250 (`era_10_divergence`, which
  already includes the space economy / post-scarcity / space colonialism — the "and
  beyond"). It has **no pre-2000 eras**. The full vision needs the arc re-based to span
  pre-history → agrarian → classical → industrial → modern → space → post-scarcity.
- **Per-era content across the whole span.** Goods, recipes, facilities, and tech nodes
  already carry `era_available` gating — the *mechanism* exists — but the *content* must
  be authored for every era (subsistence farming and handicraft → industrial mass
  production → information → biological/space manufacturing). This is Civilization-scale
  content breadth.
- **Per-era economic regimes.** The dynamics themselves shift by era: subsistence/barter
  and reciprocity → market formation → industrial capitalism → post-scarcity. The
  conservation/production *machinery* is era-agnostic (it works at any tech level — a
  major asset), but the behavioral models layered on top vary by era.

The founding seed (P0) is therefore literally "earliest history"; history-gen runs the
arc forward through eras; entry is "pick an era (and place)." Realistically this is a
generational, content-heavy effort — the engine and the era *mechanism* support it; the
mountain is the era arc + per-era content + per-era regimes. It should be staged era-band
by era-band rather than attempted whole.

## 3. Target architecture

```
PHYSICAL WORLD GEN (unchanged: Stages 1–8)         world gen → base resources
        │   geology, climate, hydrology, soils, biomes, deposits, fisheries
        ▼
FOUNDING SEED (Stage 9, reduced)                   minimal: small dispersed founding
        │   population + subsistence settlements; NO pre-built economy/firms/fortunes
        ▼
HISTORY-GEN RUN  ── orchestrator × decades/centuries, LOD-managed ──┐
        │   settlements found/grow/abandon; firms emerge from opportunity & die;       │
        │   wealth concentrates/disperses; nations form/war/collapse; tech advances;   │
        │   crime infests weak/corrupt regions and is suppressed in strong ones        │
        ▼                                                                              │
EMERGENT WORLD STATE (the "present")  ◄────────── recorded as Stage 10 history ────────┘
        │
        ▼
PLAYER ENTRY: choose location (+ time); refine that region to full LOD; optionally
              keep running history forward. Snapshot via existing persistence.
```

## 4. Reusable vs. gaps

**Reusable (already exists):**
- Headless multi-year run loop (`run_world_years`) — the history driver.
- Deterministic RNG + the world-gen↔economy decoupling (reproducible history from seed).
- Demographics as cohorts; `population_aging` (births/deaths); era/tech progression.
- Dynamic formation already present in places: `business_lifecycle` entrants/exits,
  opposition-org formation, nation formation, regime transitions.
- LOD scaffolding: `SimulationLOD` (full / lower), `lod_system` module, LOD2 abstracted
  regions, cohort-level demographics.
- Persistence (save/load) for the snapshot + player entry.

**Gaps to build:**
1. **Founding seed** — split today's world gen "create the full economy" (Stage 9
   `create_npcs`/`create_businesses`, full nations) into a *minimal founding state*
   (small population, subsistence settlements, no pre-built firms/fortunes). The rest
   must emerge.
2. **Entity genesis at the founding level** — settlements *found*, grow, and are
   *abandoned*; firms come into being from local opportunity (resource access, demand)
   rather than being seeded; nations coalesce/fragment from provinces. Today the engine
   mostly *evolves* a pre-seeded economy; history needs it to *grow one from near-zero*.
3. **Deep-time performance via LOD** — full detail (≈47 ms/tick at 2,000 NPCs) ×
   centuries is infeasible. History runs at *coarse* LOD (demographic cohorts + aggregate
   economy + LOD2 regions), with full-NPC detail materialized only for the player's
   chosen region at entry. This is THE feasibility gate (and where the diagnostic-
   slowness lesson — long-horizon accumulation — must be controlled).
4. **Player entry** — pick location (and time); promote that region to full LOD
   (instantiate individual NPCs/firms from the cohort aggregates); optionally continue.
5. **Stage 10 as record, not invention** — province/nation histories become a log of
   what was actually simulated (events emitted during history-gen), replacing the
   backward-narrative construction.

## 5. Phased plan (each phase independently buildable + validated)

- **P0 — Founding-seed mode.** A world-gen flag that emits the physical substrate +
  minimal founding population only (no full economy). Keep the current full-seed path as
  a fast "instant world" option / fallback. *Validates:* a foundable world loads and
  ticks without the pre-built economy.
- **P1 — History-gen driver (full LOD, short).** Productionize `run_world_years` into a
  headless history run from the founding seed; run a *short* horizon (e.g., 10–20 yrs) at
  full detail; confirm the economy bootstraps (firms appear, wealth forms) deterministically.
- **P2 — Entity genesis.** Settlement founding/growth/abandonment; firm genesis from
  opportunity; nation formation/fragmentation — enough that a world grows from the
  founding seed into a populated, varied one. *The hardest phase.*
- **P3 — Deep-time LOD at planet scale.** Run the *whole planet's* history at **LOD 2**
  (statistical cohort/aggregate — the existing tier) so centuries across thousands of
  provinces are tractable (target: minutes, not hours). Harden the LOD-2 economic model
  for long-horizon use, and define full-LOD-0 **materialization** (instantiating
  individual NPCs/firms from cohort aggregates) for the player's region at entry. This is
  the make-or-break feasibility gate; it reuses and extends V1's LOD 0/1/2 system.
- **P4 — Player entry.** Snapshot at chosen time; location/region selection; full-LOD
  promotion of the chosen region; optional continue-running.
- **P5 — Calibration & validation.** History must yield *reasonable* worlds — populated,
  regionally varied (rich/poor, crime-light/infested, stable/volatile), neither collapsed
  nor exploded. Reuse the emergence behavioral gates over long horizons as acceptance
  tests; tune founding density, growth/decline rates, and balancing strengths.

## 5a. Era-band staging — building the full timeline incrementally

The full arc (earliest history → space age and beyond) is **not built at once**. It is
staged **era-band by era-band**: each band is a coherent economic/tech epoch with its
own content (goods/recipes/facilities/tech) and economic regime, validated through
history-gen (run the band, confirm it produces a reasonable, varied, emergent world)
before the next band is added on top. Because history-gen runs *forward from the
founding*, the natural build order is forward — but the well-understood anchor is the
**modern band (today's content)**, so bands are added on either side of it as priorities
dictate.

**The bands** (a working decomposition; the era enum is re-based to span them):

| Band | Epoch | Economic regime | Content focus | Status |
|---|---|---|---|---|
| 0 | Founding | none (pre-economy) | physical substrate + founding population | **P0 done** |
| 1 | Subsistence | household / barter / reciprocity (no money or markets) | foraging, subsistence farming, handicraft; carrying-capacity population | new |
| 2 | Agrarian + market formation | surplus → trade → proto-markets → money/coinage; early specialization, towns, early states | settled agriculture, crafts, trade goods, money emergence | new |
| 3 | Pre-industrial / mercantile | guilds, long-distance trade, banking origins, mercantile states | workshops, shipping, early finance | new |
| 4 | Industrial | factories, wage labor, capital markets, fossil energy, urbanization | mass production, the existing extraction→production→energy resource economy | new (connects to current engine) |
| 5 | **Modern (≈2000)** | services, finance, globalization | **the current content anchor — eras 1+** | **exists** |
| 6 | Near-future | automation, fusion, information capital | eras 2–7 forward arc | exists |
| 7 | Space age & beyond | post-scarcity, space colonialism, bifurcation | eras 8–10 | exists (partial) |

**Per-band deliverable (the repeating unit of work):**
1. **Era definition** — add the era(s) to the (re-based) era enum + progression triggers
   (what advances out of the band: surplus/population/tech thresholds).
2. **Content** — goods, recipes, facilities, tech nodes tagged `era_available` for the
   band (authored data; no engine change thanks to the data-driven catalog + the
   world-gen↔goods RNG decoupling, so adding them is world-neutral).
3. **Economic regime** — the behavioral layer for the band (e.g., non-market household
   production in Band 1; money/market emergence in Band 2). The conserved
   extraction→production→consumption core is reused; the regime is a layer on top.
4. **Entity genesis for the band** — what forms and dies (settlements→towns→cities;
   households→workshops→firms; tribes→states→nations).
5. **History-gen validation** — run the band forward from its entry seed and assert a
   *reasonable, varied, emergent* world (populated, regionally differentiated, neither
   collapsed nor runaway) — reuse the emergence behavioral gates over long horizons.

**Recommended sequence.** Don't chase the whole timeline. First make the **engine**
era-spanning-ready (P1 history driver, P2 entity genesis, P3 deep-time LOD) using the
*existing* modern content (Band 5) as the test economy — this proves history-gen end to
end without authoring new eras. Then add bands outward **one at a time**, each its own
validated milestone: Band 4 (industrial — closest to the current resource economy) →
Band 1–3 backward toward the founding, and Band 6–7 forward (mostly content-completion).
Each band ships playable on its own (the player can start in any completed band).

> **Band 3 (Medieval entry) detailed design:** see
> `EconLife_Medieval_Band_Expansion_v01.md` — specifies how era 5 (`feudal`)
> becomes a playable start under a hard constraint: the Malthusian wall is
> inviolable, so the urban/specialist economy is *earned* from real agricultural
> surplus (freed specialists + proto-capital gate genesis), never granted.
> Societies that don't break the wall through technique stay subsistence-locked —
> by design.

## 6. Risks & open decisions

- **Performance is the make-or-break** (P3). Without effective LOD, century-scale history
  is a non-starter. The cohort/aggregate model during history, and clean full-LOD
  materialization at entry, are the core technical design problems.
- **Calibration / "reasonable worlds."** Emergent ≠ acceptable. Need acceptance criteria
  and tunables so most seeds produce playable, varied worlds (not dead/utopian/runaway).
- **Determinism & reproducibility** of multi-century runs (large state, RNG discipline,
  threading). The decoupling + DeterministicRNG help; must be held as an invariant.
- **Time vs. richness fidelity.** Coarse-LOD history loses individual-NPC granularity;
  decide what's preserved (aggregates, notable lineages/firms/events) vs. regenerated at
  entry.
- **Scope / Tier.** Confirm against the Feature Tier List whether this is V1 or a
  post-V1 evolution; the current static-seed path remains valid (and the fast default)
  meanwhile.
- **Open:** how far back does history start (founding density), and is entry time
  player-chosen or fixed (e.g., always "present")?

## 7. Out of scope (for now)
- Multi-body / solar-system content (EX; `PlanetaryParameters` already abstracts the body).
- Replacing the GIS-seeded real-world pipeline (it can supply the physical substrate for
  history-gen identically to the procedural pipeline).
