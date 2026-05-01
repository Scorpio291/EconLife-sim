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

### L1. `good_id_from_string` weak hash — partially fixed

**Status:** Quick fix shipped; proper fix still open.
**File:** `simulation/core/good_id_hash.h` (new).

Quick fix (this branch): the three modules that hashed good_ids
(production, supply_chain, seasonal_agriculture) were duplicating the
same Java-style `hash * 31 + c` function with explicit "must match"
comments. They now route through a single `econlife::good_id_hash`
helper that uses FNV-1a (32-bit). Verified zero collisions on the
247-good base_game catalog with both old and new hashes.

Proper fix (open): change `good_id` to a stable integer assigned at
goods-catalog load time (`GoodDefinition::numeric_id` already exists)
and route every Recipe → MarketDelta path through the catalog instead
of hashing strings. This needs a `GoodsCatalog&` reference threaded
through ProductionModule / SupplyChainModule / SeasonalAgricultureModule
and a small migration in their tests. Defer until the goods catalog
grows past a few hundred entries or until a real collision surfaces.

### L3. Brittle delta merge in tick_orchestrator

**Status:** Open. Tracked as **B1** in
`docs/plans/2026-04-25-tier-b-followups.md`.

### L6. Tier comments in `register_base_game_modules.cpp` drift

**Status:** Resolved in this commit. The tier-numbered "Depends on X"
comments invited drift the moment a module added a new
`runs_after()` entry. They have been replaced with role-based
groupings (production/markets, finance, NPC behavior, evidence,
criminal economy, infrastructure) that describe what the section
*is* rather than where it sits in a dependency hierarchy. The
header comment at the top of the file points readers to
`runs_after()` / `runs_before()` as the source of truth and notes
that registration order only matters as a tiebreaker for the
topological sort. Verified all 1312 tests still pass after the
reordering.

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
| **L5** recipe ↔ goods cross-validation | `4ed4ebe` | `RecipeCatalog::validate_against_goods`; world_generator logs missing references at load. Verified clean on base_game (247 goods, 102 recipes). |
| **L1** weak good-id hash (quick) | (this commit) | Three duplicated `hash * 31 + c` implementations consolidated into `core/good_id_hash.h` (FNV-1a). Verified zero collisions on base_game. Long-form fix (catalog numeric ids) still open. |
| **L8** no CI perf gate | `3e53cd7` | `benchmark.yml` now runs the all-modules contract benchmark and gates on a 200 ms threshold; results uploaded as artifact. |
| **H5** linear-scan lookups (per-id) | `3a40d34` | drain_deferred_work, labor_market::find_employment, npc_spending::get_buyer_type now use hash-map indices. Filter-shape scans (22 NPC iterations) remain. |
| **L6** register-tier comment drift | (this commit) | Tier-numbered comments replaced with role-based section groupings; runs_after()/runs_before() declared as the source of truth. |
| Supply chain transit-delay bypass | `32ae755` | Tier A. Was not in the original report but discovered during verification. |

---

## Recommended Priority Order (refreshed)

1. **L1 catalog migration** — proper fix: thread `GoodsCatalog&` through Production / SupplyChain / SeasonalAgriculture and use `numeric_id` instead of hashing. Removes the hash entirely. Defer until justified by collision or scale.
2. **H5 filter-shape scans** — 22 `for npc : significant_npcs` iterations. Needs province-bucketed NPC lists or an iteration helper. Design pass first.
3. **M3 stub handlers.** Resolve naturally as each consuming module lands.
