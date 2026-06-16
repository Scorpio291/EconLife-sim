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
- **P3 — Deep-time LOD.** Run history at coarse LOD so centuries are tractable
  (target: a few minutes for ~150–300 yrs). Define the cohort/aggregate economic model
  used during history and the full-LOD materialization at entry.
- **P4 — Player entry.** Snapshot at chosen time; location/region selection; full-LOD
  promotion of the chosen region; optional continue-running.
- **P5 — Calibration & validation.** History must yield *reasonable* worlds — populated,
  regionally varied (rich/poor, crime-light/infested, stable/volatile), neither collapsed
  nor exploded. Reuse the emergence behavioral gates over long horizons as acceptance
  tests; tune founding density, growth/decline rates, and balancing strengths.

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
