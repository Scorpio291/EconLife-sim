# EconLife Codebase Audit Report

**Original audit:** 2026-03-30
**Last verified:** 2026-04-25
**Scope:** Full simulation codebase (simulation/, packages/, CI, tests)

---

## Executive Summary

The 2026-03-30 audit identified 3 critical, 6 high, 8 medium, and 8 low
severity issues. As of 2026-04-25, 16 of 25 are resolved and 1 is
partially resolved. The remaining 8 split into a perf concern (H5),
two doc/lint issues (L1, L6), one data-integrity check (L5), one CI
gap (L8), one architectural refactor (L3 — tracked separately as
B1 in `docs/plans/2026-04-25-tier-b-followups.md`), and a cluster of
stub `drain_deferred_work` handlers (M3) that wait on dependent modules.

The architecture itself has held up well: the const-WorldState contract,
DeltaBuffer pattern, and deterministic RNG have not needed to change.
The original risks list ("concurrency untested", "memory management
unsafe") is now obsolete — the threading is real, the lifetimes are
managed by smart pointers, and CI actually runs all 1300+ tests.

---

## Open Issues

### H5. Linear scan lookups will exceed performance budget at scale

**Status:** Open.
**Files:** Multiple modules use O(N) linear scan for NPC/business lookups:

- `simulation/core/world_state/apply_deltas.cpp:47` — per-NPCDelta scan over `world.significant_npcs`.
- `simulation/core/tick/drain_deferred_work.cpp:26-32` — `find_npc` linear scan, called from every relationship/travel/evidence handler.
- 22 occurrences of `for (const auto& npc : state.significant_npcs)` across modules (count via `grep -rc`).
- 16 occurrences of `for (... : state.regional_markets)` across modules.

With 2000 NPCs and thousands of deltas per tick, `apply_npc_deltas` is
O(N·M). The 500ms tick budget at V1 scale will be exceeded.

**Fix:** Build `std::unordered_map<uint32_t, size_t>` indices at tick
start (or maintain them as the NPC vector is mutated) and route lookups
through them. Alternative: sort NPCs by id and use `std::lower_bound`.
Decide once a benchmark profile shows which paths dominate.

**Priority:** This is the single largest remaining engineering risk. Do
before tightening B3 (CI perf gate), otherwise the gate locks in a bad
baseline.

### M3. Many `drain_deferred_work` handlers are stubs

**Status:** Partial. 4 of 12 handlers fully implemented (relationship
decay, evidence decay, NPC travel arrival, player travel arrival). The
remaining 8 are intentional `(void)` no-ops awaiting their consuming
modules:

- `handle_consequence` — needs `ConsequenceEntry` definition (Session 16).
- `handle_transit_arrival` — needs `TransitShipment` lookup table.
- `handle_npc_business_decision` — Session 4+.
- `handle_market_recompute` — design says this can stay a trigger no-op.
- `handle_investigator_meter_update` — Session 15+.
- `handle_climate_downstream` — pending real seasonal_agriculture.
- `handle_community_stage_check` — Session 13.
- `handle_maturation_advance`, `handle_commercialize` — pending R&D system.
- `handle_interception_check` — Session 15+.

**Fix:** No standalone fix; resolved as each consuming module lands.
Track via the per-handler `// Will be implemented in...` comments.

### L1. `good_id_from_string` uses a weak hash

**Status:** Open.
**File:** `simulation/modules/production/production_module.cpp:68-75`.

`hash * 31 + c` (Java string hash). Fine for the current ~252 goods, but
string collisions are not bounded. If two recipe-referenced good ids
collide, the bug surfaces as silently merged supply/demand on the wrong
market — hard to diagnose.

**Fix:** Replace with FNV-1a or wyhash; or, better, change `good_id` to
a stable integer assigned at goods-catalog load time and stored in a
single source of truth, removing the per-call hash entirely.

### L3. Brittle delta merge in tick_orchestrator

**Status:** Open. Tracked as **B1** in
`docs/plans/2026-04-25-tier-b-followups.md`.

### L5. Goods CSV not cross-validated against recipe inputs/outputs

**Status:** Open. No load-time check that every recipe `good_id` exists
in the goods catalog. A typo in a recipe CSV produces a silently-zero
production rate at runtime.

**Fix:** In `recipe_catalog.cpp` (or a follow-up `validate_packages()`
call after all catalogs load), iterate every recipe's inputs/outputs
and assert each `good_id` exists in `GoodsCatalog`. Fail fast at
package load.

### L6. Tier comments in `register_base_game_modules.cpp` drift

**Status:** Open. Cosmetic — topological sort handles the real order.
The "Tier N: Depends on X" comments are a registration aid; some no
longer match the dependency declarations the modules return from
`runs_after()`. Either update the comments or generate them.

### L8. No CI performance gate

**Status:** Open. Tracked as **B3** in
`docs/plans/2026-04-25-tier-b-followups.md`.

---

## Resolved Issues

| Item | Resolved by | Notes |
|------|-------------|-------|
| **C1** const_cast in npc_behavior | `b09a213` | Cross-province path now writes to `province_delta.cross_province_deltas`. |
| **C2** raw `new` in persistence | `b09a213` | `PlayerCharacter`, `GlobalCommodityPriceIndex`, `RegionCohortStats` all `std::unique_ptr` (world_state.h:63,96; geography.h:678). |
| **C3** drain mutates WorldState | `b09a213` (relationship/evidence/travel handlers); remaining stubs tracked under M3. |
| **H1** home_province_id | `b09a213` | npc_spending_module.cpp:106 uses `current_province_id`. |
| **H2** ProductionModule init race | `bdcdc0e` | `std::call_once` (production_module.cpp:86). |
| **H3** evidence id collision (npc_id) | `b09a213` | `EvidenceModule::next_token_id_` counter. Antitrust had a separate collision; fixed in `32ae755` (Tier A). |
| **H4** ThreadPool stub | `4a46185` | Real `submit`/`worker_loop`; orchestrator uses `parallel_for`. |
| **H6** cross-province future-tick drop | `b09a213` | `apply_cross_province_deltas` partitions by `due_tick` (apply_deltas.cpp:594-615). |
| **M1** topsort tiebreak | `a9b3660` | priority_queue ordered by module name. |
| **M2** capital_delta overwrite | `b09a213` | Single accumulator (npc_behavior_module.cpp:391-410). |
| **M4** supply decay missing | `4a46185` | `surplus_decay` applied each tick (tick_orchestrator.cpp:155-162). |
| **M5** motivation_delta single-slot | `a9b3660` | `motivation_replacement` (full-vector) added to NPCDelta. `motivation_delta` retained for additive single-slot updates and explicitly documented. |
| **M6** no integration determinism | `a9b3660` | determinism_test.cpp:282 and :311 register modules and verify bit-identical output. |
| **M7** CI doesn't run scenario tests | `d8cc050` | Tier A: labels added to all four `catch_discover_tests` calls; CI now runs 1306 tests instead of 0. |
| **M8** WorldState raw pointer leaks | `b09a213` | All three members migrated to `std::unique_ptr`. |
| **L2** CrossProvinceDeltaBuffer thread safety | `a9b3660` | Pushes go through per-province DeltaBuffer; orchestrator merges into `world.cross_province_delta_buffer` from the main thread (apply_deltas.cpp:564). |
| **L4** player_delta missed merge | (orchestrator refactor) | Merge added at tick_orchestrator.cpp:211-228. |
| **L7** NPCTravelStatus literal cast | `32ae755` | Tier A. |
| Supply chain transit-delay bypass | `32ae755` | Tier A. Was not in the original report but discovered during verification. |

---

## Recommended Priority Order (refreshed)

1. **B2 — refresh this report** (this PR).
2. **H5 — linear-scan lookups.** Largest perf risk; blocks meaningful CI perf gate.
3. **B1 — `DeltaBuffer::merge_from`.** Mechanical refactor; pays for itself the next time someone adds a delta type.
4. **L5 — recipe ↔ goods cross-validation.** Cheap, prevents silent typo bugs in modder content.
5. **L1 — better good-id hash (or integer ids).** Lower risk than L5 today; revisit when goods catalog grows past a few hundred entries.
6. **B3 — CI perf gate.** After H5; otherwise gate calibrates against known-slow code.
7. **L6 — register-tier comments.** Cosmetic; do alongside any module-registration touch.
8. **M3 stub handlers.** Resolve naturally as each consuming module lands.
