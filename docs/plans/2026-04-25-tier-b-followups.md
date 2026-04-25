# Tier B Follow-Ups — Architecture & Process Hygiene

**Date:** 2026-04-25
**Status:** Proposed
**Origin:** Audit cleanup pass on `claude/setup-econlife-sim-367ph`. Tier A
(supply_chain transit-delay bypass, antitrust evidence-id collision,
test_world_factory enum cast) shipped in the same branch. The items below
need design discussion before implementation.

---

## B1. `DeltaBuffer::merge_from` — eliminate brittle hand-merged province deltas

### Problem
`TickOrchestrator::execute_tick` (simulation/core/tick/tick_orchestrator.cpp,
~lines 200-271) hand-merges every province-parallel `DeltaBuffer` field into
the tick-level buffer:

```cpp
delta.npc_deltas.insert(...);
delta.market_deltas.insert(...);
delta.evidence_deltas.insert(...);
// ...one block per vector...
delta.cross_province_deltas.insert(...);
```

Plus per-field accumulator code for `player_delta` (additive for `health`,
`wealth`, `exhaustion`; replacement for `skill`, `province`, etc.).

This is two bugs waiting to happen:

1. Adding a new delta type to `delta_buffer.h` requires the developer to
   remember to add a merge clause here. Forget, and the deltas are silently
   dropped — same shape as the L4 bug fixed earlier (player_delta merge missed
   the province-parallel path until someone noticed).
2. The replacement-vs-additive policy lives in two places: `apply_deltas.cpp`
   and the orchestrator merge loop. Drift between them is not caught at
   compile time.

### Proposed approach
1. Add `void DeltaBuffer::merge_from(DeltaBuffer&& other);` declared next to
   the struct in `simulation/core/world_state/delta_buffer.h`. Implementation
   in `delta_buffer.cpp` owns the entire merge policy.
2. Replace the orchestrator's hand-rolled merge block with a single
   `delta.merge_from(std::move(province_deltas[p]));`.
3. For each `DeltaBuffer` member, document the policy with a short comment
   at the field declaration (`// merge: append`, `// merge: additive sum`,
   `// merge: last-write-wins`). This is the spec the merge implementation
   reads.
4. Add a static_assert or a unit test that constructs a populated
   `DeltaBuffer`, merges it into another, and verifies every field flows
   through. The test fails when a new field is added without merge support.

### Risks / open questions
- `player_delta`'s replacement fields (`new_province_id`, `skill_delta`)
  currently use last-write-wins by province index ordering. That is
  deterministic but arbitrary. Confirm it's the intended semantics, or
  define explicit precedence (e.g. ascending `province_id`).
- Performance: the existing merge is just `vector::insert`. `merge_from`
  on a moved-from buffer should be no slower if it `std::move`s
  vectors instead of copying.
- Scope: keep this PR mechanical — no behavioral changes. Replacement of
  the merge block + tests, nothing else.

### Sizing
~150 lines of code + tests. One PR.

---

## B2. Refresh `docs/CODEBASE_AUDIT_REPORT.md`

### Problem
The 2026-03-30 audit drove three weeks of fixes. Verification on
2026-04-25 shows most items are now addressed but the report still lists
them as open. New contributors reading the report will chase ghosts.

Quick re-verification result:

| Item | Status | Notes |
|------|--------|-------|
| C1 const_cast in npc_behavior | Fixed | No `const_cast<WorldState>` in npc_behavior_module.cpp; cross-province path uses `province_delta.cross_province_deltas`. |
| C2 raw `new` in persistence | Fixed | No `= new` allocations remain in persistence_module.cpp. |
| C3 drain mutates WorldState | Mostly fixed | `handle_npc_relationship_decay`, `handle_evidence_decay`, `handle_npc_travel_arrival`, `handle_player_travel_arrival` all write through DeltaBuffer. The remaining handlers (consequence, business_decision, climate_downstream, etc.) are stubbed pending dependent-module work. |
| H1 home_province_id | Fixed | npc_spending_module.cpp:106 uses `current_province_id`. |
| H2 ProductionModule init race | Fixed | Uses `std::call_once`. |
| H3 evidence id collision (npc_id) | Fixed | EvidenceModule has `next_token_id_` counter. (Antitrust had a separate collision; fixed in Tier A.) |
| H4 ThreadPool stub | Fixed | Real `submit`/`worker_loop` in thread_pool.cpp; orchestrator dispatches via `parallel_for`. |
| H5 linear-scan lookups | Open | 22 NPC linear scans across modules; 16 regional_market scans. Real concern at 2k NPCs. |
| H6 cross-province future-tick drop | Fixed | apply_deltas.cpp:594-615 partitions by `due_tick`. |
| M1 topsort tiebreak | Fixed | priority_queue ordered by module name. |
| M2 capital_delta overwrite | Fixed | Single accumulator in npc_behavior_module.cpp:391-410. |
| M3 stub drain handlers | Partial | See C3 above. |
| M4 supply decay missing | Fixed | tick_orchestrator.cpp:155-162 applies surplus_decay each tick. |
| M5 motivation_delta single-slot | Mitigated | `motivation_replacement` (full-vector override) added to NPCDelta. |
| M6 no integration determinism | Fixed | determinism_test.cpp:282 and :311 register modules. |
| M7 CI doesn't run scenario tests | Fixed | Tier A: labels added; CI now runs all 1306 tests. |
| M8 WorldState raw pointer leaks | Open | `player`, `lod2_price_index`, `Province::cohort_stats` still raw. |
| L3 brittle delta merge | Open | Tracked here as B1. |
| L4 player_delta missed merge | Fixed | tick_orchestrator.cpp:211-228. |
| L7 NPCTravelStatus literal cast | Fixed | Tier A. |
| L8 perf gate in CI | Open | benchmark.yml runs but doesn't gate. |

### Proposed approach
1. Move resolved entries to a "Resolved" section with the commit SHA that
   addressed each.
2. Re-state remaining open items (H5, M8 if still applicable, L1, L5, L6,
   L8) with current line numbers.
3. Re-evaluate priorities — the original "fix C1 first" ordering is no
   longer relevant. H5 (perf at scale) and M8 (lifetimes) become the lead
   items.

### Sizing
~30 minutes of edits. Doc-only PR.

---

## B3. CI performance gate

### Problem
The performance contract in `CLAUDE.md` is explicit: "Tick must complete
in < 500ms at 2,000 significant NPCs. Target: < 200ms on 6 cores."
Benchmarks exist (`simulation/tests/benchmarks/tick_benchmark.cpp`). A
GitHub Actions workflow exists (`.github/workflows/benchmark.yml`).
Neither gates merges — a 10x regression would pass CI silently, and we'd
discover it during release prep.

### Open questions (resolve before implementing)
1. **Gate type.** Two viable shapes:
   - *Absolute threshold.* Fail the build if `full tick performance at
     2000 NPCs` exceeds, say, 600ms wall time. Simple, but flaky on
     shared CI runners.
   - *Relative regression.* Compare against a recorded baseline; fail if
     >X% slower. Requires baseline storage (artifact, branch, S3).
2. **Hardware variance.** GitHub Actions runners vary considerably tick
   to tick. Either (a) run N iterations and use the median, (b) require
   a self-hosted runner, or (c) gate only on `ubuntu-latest` Release with
   a generous threshold.
3. **What to gate.** The full-tick benchmark is the headline contract,
   but `delta buffer merge`, `deferred work queue drain`, `RNG call`,
   `persistence round-trip`, and `province parallel scaling` all have
   targets too. Pick a small set or all.
4. **Failure UX.** Should a regression block merge or just post a PR
   comment? Blocking is right for the contract; commenting is right
   while we tune the threshold.

### Suggested first cut
- Run the benchmark suite on `ubuntu-latest` Release in CI.
- Parse the Catch2 benchmark JSON output.
- Fail only on the full-tick benchmark, only if median > 750ms (1.5x the
  contract, accounting for runner noise).
- Post a comment on every PR with the benchmark numbers regardless of
  pass/fail, so trend is visible.
- Tighten the threshold once we have ~20 PRs of baseline data.

### Sizing
1-2 days. Needs runner-noise calibration before the threshold is
meaningful. Defer until after H5 (linear-scan lookups) is addressed —
otherwise the baseline is being set against known-slow code.

---

## Suggested ordering
1. **B2 first** (~30 min). Cheap; unblocks correct prioritization of
   everything else and stops new contributors from working off stale
   info.
2. **B1 next** (one focused PR). Pays for itself the next time someone
   adds a delta type.
3. **B3 last** (with H5 either before or alongside). Gating perf before
   the known-slow lookup paths are addressed locks in a bad baseline.
