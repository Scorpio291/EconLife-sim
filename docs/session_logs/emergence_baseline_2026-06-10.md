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

2. **Province conditions saturate with no restoring force.** HIGH. Stability
   0.80→0.00 (pinned), grievance 0.20→1.00 (pinned), unemployment →1.00
   (pinned). The keystone looks like grievance: it rises monotonically to the
   ceiling, which holds community-response at maximum escalation, which generates
   perpetual instability events that overwhelm `compute_stability_recovery`'s
   `rate*(1-stability)` restoring term → stability pinned at 0. Likely chained
   from near-total NPC inactivity (490/500 fall to `waiting` by year 1) →
   unemployment→1 → grievance→1. Need to find what makes activity/employment
   collapse in year 1 and why grievance has no decay.

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
2. **Fix the province-conditions runaway (#2).** Find the year-1 activity/
   employment collapse and give grievance a restoring force. This likely
   un-pins stability and unemployment together — one root, several green
   ratchets.
3. **Reconnect the crime metric (#3).** Smaller, isolated.
4. Then revisit business lifecycle / era pacing / population (verify intended).

Each fix is validated by a ratchet flipping from "failed as expected" to
"passed" — which is exactly the human-light validation loop the 99% experiment
is built around.
