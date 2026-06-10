# Emergence Baseline & Road Ahead — 2026-06-10

## Why this exists

The codebase is breadth-complete at the module level: ~50 V1 modules, 49
interface specs, ~1,600 fast tests. But the existing long-run integration tests
("runs 365 ticks", "prices stay positive") only assert *safety* invariants —
no NaN, values bounded — never that anything actually *happens*. So we had no
evidence about the question that matters for the 99%-experiment thesis: **does
the simulation produce emergent behavior, or do the modules just pass in
isolation while the orchestra fails to play?**

This pass built a behavioral harness to answer that empirically, then encoded
the answer as tests.

## What was built

- `simulation/tests/integration/emergence_harness.h` — boots a V1-scale
  generated world, runs it through the real base-game orchestrator for N years,
  captures an annual time series of behavioral aggregates from observable
  WorldState (module-private state like investigator cases is observed via its
  effects: NPC status, evidence pool, consequence queue, province conditions).
- `emergence_observe.cpp` (`[.emergence-observe]`, hidden) — dumps the 10-year
  series for a human to read. This is the diagnostic.
- `emergence_test.cpp` (`[emergence]`, opt-in label) — turns the baseline into
  assertions. Alive loops are locked in as regression guards; broken loops are
  Catch `[!shouldfail]` ratchets (suite stays green while they fail; the moment a
  loop is fixed the test passes, which Catch flags loudly → drop the tag).

Run: `ctest -L emergence` (behavioral suite) / `ctest -LE emergence` (fast gate).

## The baseline (seed 42, 500 NPCs, 6 provinces, 10 years)

```
yr | active wait imp dead | crim crImp | evid consq | crBiz sig | stab crime gini griev unemp | pop      | maxCap
 0 |    500    0   0    0 |   36     0  |    0     0 |    2    0 | 0.80 0.106 0.31 0.20 0.00 | 3,365,762 | 1.5e5
 1 |     10  490   0    0 |   36     0  | 1437   301 |    2    2 | 0.00 0.000 0.16 0.79 0.68 | 3,347,160 | 3.2e5
 2 |     12  ...   0    0 |   36     0  | 2549   351 |    2    2 | 0.00 0.000 0.08 0.99 0.68 | 3,273,487 | 3.4e5
 5 |     18        0    0 |   36     0  | 2050   534 |    2    2 | 0.00 0.000 0.01 1.00 1.00 | 3,066,783 | 5.8e5
10 |     19        0    0 |   36     0  | 2030   565 |    2    2 | 0.00 0.000 0.00 1.00 1.00 | 2,763,506 | 1.1e6
```

**Headline: the simulation flatlines after year 1.** Markets and capital
accumulation are alive (price spread moves; max capital grows 7× — wealth
concentrates). Almost everything else collapses to a boundary in the first year
and stays pinned for a decade.

## Findings (ranked)

### Alive loops (locked in as `[emergence]` regression guards)
- Markets move (cross-province price spread changes over time).
- Criminal activity generates evidence (pool → ~2000); the facility_signals→
  net_signal pipeline wired earlier this week reaches criminal businesses.
- Capital economy is active and bounded (no NaN, no runaway to ceiling).
- Determinism holds (identical seed → identical behavior).

### Broken / frozen loops (`[!shouldfail]` ratchets — the road ahead)

1. **Criminal justice loop never closes.** HIGH. Evidence accrues to ~2000 and
   consequences queue to ~565, but **zero imprisonments occur in 10 years**.
   `legal_process` has a working conviction→imprisonment path
   (`legal_process_module.cpp:315`), so the break is upstream: either cases are
   never seeded from the consequence/raid path, or seeded cases never progress
   to conviction. This is adjacent to (and the natural continuation of) the
   facility_signals→investigator_engine wiring just completed — the detection
   front end now works; the prosecution back end doesn't fire.

2. **Mass grievance produces no organized reaction; conditions pin with no
   resolution.** HIGH. This is the one to understand. Stability 0.80→0.00,
   grievance 0.20→1.00, unemployment →1.00, all pinned for a decade. The point
   isn't only "nothing pushes grievance down" — it's that a society under
   sustained mass unemployment should *break* (organize, strike, vote out the
   incumbent, form opposition), and the breaking is the emergent content. The
   machinery for this exists (GDD §14.2 seven-stage ladder in
   `community_response`), so the second observer pass measured the RESPONSE side:

   ```
   yr | unemp griev | response_stage(mean/max) cohesion inst_trust resource
    1 | 0.68 0.79  | 3.7 / 4   coh 0.27   trust 0.48   res 0.92
    2 | 0.68 0.99  | 4.0 / 5   coh 0.13   trust 0.45   res 0.76
    3 | 0.84 1.00  | 4.0 / 4   coh 0.00   trust 0.39   res 0.93
    4+| 1.00 1.00  | 4.0 / 4   coh 0.00   trust 0.36   res ~1.0   (frozen here for 7 yrs)
   ```

   Diagnosis: the population *does* start reacting — escalation climbs to stage
   4 (economic_resistance) and briefly stage 5 (direct_action/strikes) — but then
   **jams at stage 4 and never reaches stage 6 (sustained_opposition)**, so no
   OppositionOrganization ever forms despite grievance pinned at 1.0 and
   resource_access at ~1.0 for years. The block is **cohesion collapsing to
   0.00**: `direct_action` gates on cohesion ≥ 0.45 and `sustained_opposition`
   needs a qualifying leader, and cohesion is computed from NPC
   `social_capital × stability-motivation` averaged over the province. So the
   keystone chain is:

   > year-1 NPC-inactivity collapse (490/500 → `waiting`) → cohesion → 0 →
   > collective-action stages of the ladder are gated shut → population is
   > maximally aggrieved but cannot organize → escalation stuck at boycotts →
   > no opposition, no regime turnover, no relief → grievance/stability/
   > unemployment pinned at extremes forever.

   And even the stages it does reach have **no resolution mechanism**: economic
   resistance / brief strikes produce no concessions, no policy that addresses
   unemployment, no electoral turnover; opposition formation (if it occurred)
   only emits a `political_consequence` that *lowers* institutional trust,
   amplifying the downward spiral. The loop has no discharge — real uprisings end
   in suppression, concessions, or regime change; this one just floors.

   Two things to fix: (a) the upstream NPC-activity/cohesion collapse that gates
   the ladder shut, and (b) a resolution path so reaching the top of the ladder
   *changes the political/economic order* (electoral turnover, policy response,
   or regime change) instead of only amplifying decline. (Nation-level revolution
   / "war as failure mode" is EX scope per the Feature Tier List; the V1-
   appropriate discharge is organized opposition + electoral turnover via
   `political_cycle`.)

3. **Regional crime metric disconnected from reality.** MEDIUM. `crime_rate`
   →0.00 even though 36 criminals and 2 criminal businesses persist. The
   cohort-stats crime aggregation isn't tracking the criminal population it is
   meant to measure. Same disconnect smell as `gini`→0 while capital concentrates.

4. **Business & era dynamics inert.** MEDIUM/LOW. Business count frozen at 50
   (2 criminal) for a decade — `business_lifecycle` produces no failures/entries
   and no criminal-sector growth. Era frozen at 1 for 10 years (may be intended
   V1 pacing — verify against the R&D era-trigger design).

5. **Population monotonic decline** ~2%/yr (3.37M→2.76M). LOW. Possibly intended
   demographic drift; flag to confirm births/deaths balance is by design.

## Recommended road ahead

1. **Close the criminal justice loop (#1).** Highest leverage and continues the
   detection-pipeline thread. Trace consequence-fire → `LegalCaseSeedDelta` →
   `legal_process` case progression and find where it stalls. Flips the first
   ratchet green.
2. **Make the world react to mass unemployment (#2).** Two parts: (a) fix the
   year-1 NPC-activity/cohesion collapse that gates the escalation ladder shut,
   so a maximally-aggrieved population can actually organize (reach
   sustained_opposition); (b) give the top of the ladder a resolution path —
   electoral turnover / policy response via `political_cycle` — so the reaction
   changes the order instead of only amplifying decline. Flips the
   "organized opposition", "stability", and "grievance relaxes" ratchets together.
3. **Reconnect the crime metric (#3).** Smaller, isolated.
4. Then revisit business lifecycle / era pacing / population (verify intended).

Each fix is validated by a ratchet flipping from "failed as expected" to
"passed" — which is exactly the human-light validation loop the 99% experiment
is built around.
