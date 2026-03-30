# EconLife Codebase Audit Report

**Date:** 2026-03-30
**Scope:** Full simulation codebase (simulation/, packages/, CI, tests)
**Codebase size:** 124 .cpp files, 116 .h files, 40+ modules, ~462 total files

---

## Executive Summary

The codebase is well-structured for a bootstrap-phase project. Core architecture (tick orchestrator, DeltaBuffer, DeterministicRNG, cross-province propagation) is sound and matches the design specs. The module system is cleanly factored with consistent patterns. However, several issues were found ranging from determinism violations to memory safety concerns and architectural debt. This report categorizes findings by severity.

---

## CRITICAL Issues

### C1. `const_cast` violates const WorldState contract in NpcBehaviorModule
**File:** `simulation/modules/npc_behavior/npc_behavior_module.cpp:503`
```cpp
const_cast<WorldState&>(state).cross_province_delta_buffer.entries.push_back(cpd);
```
The ITickModule contract states modules receive `const WorldState&` and write only to `DeltaBuffer`. This `const_cast` mutates WorldState directly during province-parallel execution. If two provinces both have migrating NPCs, they will concurrently push to the same `entries` vector — a **data race** that causes undefined behavior and breaks determinism.

**Impact:** Undefined behavior under concurrency; determinism violation.
**Fix:** Province-parallel modules should write cross-province effects to a per-province buffer in the DeltaBuffer, then have the orchestrator merge them. Alternatively, add a `CrossProvinceDelta` vector to `DeltaBuffer` and merge in `apply_deltas`.

### C2. Raw `new` allocations without corresponding `delete` — memory leaks
**File:** `simulation/modules/persistence/persistence_module.cpp:1008,1652,1711`
```cpp
p.cohort_stats = new RegionCohortStats{};
out_state.player = new PlayerCharacter(read_player(r));
out_state.lod2_price_index = new GlobalCommodityPriceIndex{};
```
These use raw `new` with no `delete` path. `WorldState` has raw pointer members (`player`, `lod2_price_index`, `Province::cohort_stats`) that leak when WorldState is destroyed. The CLAUDE.md rule says "No raw pointers in module code — use smart pointers or pool allocation."

**Impact:** Memory leaks on every world load/unload cycle.
**Fix:** Change `PlayerCharacter*` to `std::unique_ptr<PlayerCharacter>`, `GlobalCommodityPriceIndex*` to `std::unique_ptr<GlobalCommodityPriceIndex>`, and `RegionCohortStats*` to `std::unique_ptr<RegionCohortStats>` (or use shared_ptr if needed for cross-references).

### C3. `drain_deferred_work` directly mutates WorldState, bypassing DeltaBuffer
**File:** `simulation/core/tick/drain_deferred_work.cpp:86-96,112-128,172-178`
Handlers like `handle_npc_relationship_decay` and `handle_evidence_decay` directly modify NPC/evidence fields on `WorldState&` instead of writing to the `DeltaBuffer`. This violates the core architectural invariant: "WorldState is never modified mid-tick" (except via `apply_deltas`).

```cpp
// Direct mutation:
rel.trust = std::max(0.0f, rel.trust - TRUST_DECAY_RATE);
// And:
token.actionability = std::max(0.0f, token.actionability - ...);
npc->travel_status = NPCTravelStatus::resident;
```

**Impact:** Breaks the DeltaBuffer invariant; could cause modules that run later in the tick to see inconsistent state; undermines determinism guarantees.
**Fix:** All handlers should write to the `DeltaBuffer& delta` parameter and let `apply_deltas` apply them.

---

## HIGH Severity Issues

### H1. NpcSpendingModule uses `home_province_id` instead of `current_province_id`
**File:** `simulation/modules/npc_spending/npc_spending_module.cpp:106`
```cpp
if (npc.home_province_id == province.id && npc.status == NPCStatus::active) {
```
NpcBehaviorModule correctly uses `current_province_id` (line 213), but NpcSpendingModule filters by `home_province_id`. An NPC who has migrated or is visiting will spend in their home province rather than where they physically are. This breaks the physics principle (§18.14).

**Impact:** Incorrect economic simulation for migrated/traveling NPCs.
**Fix:** Change to `npc.current_province_id == province.id`.

### H2. ProductionModule `initialized_` flag is not thread-safe
**File:** `simulation/modules/production/production_module.cpp:86-88`
```cpp
if (!initialized_) {
    init_from_world_state(state);
    initialized_ = true;
}
```
This lazy initialization runs inside `execute_province()`, which is a province-parallel method. If the orchestrator dispatches provinces to a thread pool, multiple threads could race on `initialized_`, causing double-initialization or seeing partially initialized registries.

**Impact:** Data race on future thread pool activation; currently safe only because orchestrator runs provinces sequentially.
**Fix:** Move initialization to a dedicated `init()` method called before the first tick, or use `std::call_once`.

### H3. Evidence token ID generation uses `current_tick * 10000 + npc.id` — collision risk
**File:** `simulation/modules/npc_behavior/npc_behavior_module.cpp:468`
```cpp
token.id = state.current_tick * 10000 + npc.id;
```
With 2000 NPCs and tick values >1000, this can overflow `uint32_t` and produce collisions. Also, multiple evidence-generating actions in the same tick from NPCs with ids differing by multiples of 10000 would collide.

**Impact:** Evidence token ID collisions leading to incorrect evidence lookups.
**Fix:** Use the auto-assignment path in `apply_evidence_deltas` (set `token.id = 0` and let `apply_deltas` assign the next available ID).

### H4. ThreadPool is a stub — no actual parallelism
**File:** `simulation/core/tick/thread_pool.h`
The ThreadPool class is a no-op stub. The orchestrator's `execute_tick` method iterates provinces sequentially (line 164: `for (uint32_t p = 0; p < province_count; ++p)`). This means the province-parallel architecture is untested under actual concurrency.

**Impact:** Province-parallel code may have latent data races that won't surface until real threading is enabled. The const_cast in C1 is proof of this.
**Fix:** Implement ThreadPool with actual thread dispatch and run determinism tests under concurrency to flush out races. Even a 2-thread test would catch C1.

### H5. Linear scan lookups will exceed performance budget at scale
**Files:** Multiple modules use O(N) linear scan for NPC/business lookups:
- `apply_deltas.cpp:47`: `for (auto& n : world.significant_npcs)` per delta
- `drain_deferred_work.cpp:31`: `find_npc` linear scan
- `labor_market_module.cpp:565-572`: `find_employment` linear scan
- `npc_spending_module.cpp:86-90`: `get_buyer_type` linear scan

With 2000 NPCs and potentially thousands of deltas per tick, `apply_npc_deltas` alone is O(N*M) where N=NPCs and M=deltas. The 500ms tick budget at 2000 NPCs will be exceeded.

**Impact:** Performance regression at V1 scale.
**Fix:** Build hash maps (unordered_map<uint32_t, NPC*>) at tick start or maintain indexed lookups. Alternatively, ensure NPC vectors are sorted by ID and use binary search.

### H6. `apply_cross_province_deltas` silently drops future-tick entries
**File:** `simulation/core/world_state/apply_deltas.cpp:476-477`
```cpp
if (entry.due_tick > world.current_tick)
    continue;
```
After processing, `cpd.entries.clear()` (line 490) removes ALL entries, including those not yet due. Cross-province effects scheduled for future ticks (due_tick > current_tick) are lost.

**Impact:** Any cross-province effect scheduled more than 1 tick ahead is silently dropped.
**Fix:** Partition entries: apply those that are due, retain those that aren't.

---

## MEDIUM Severity Issues

### M1. Kahn's algorithm topological sort is non-deterministic for equal-priority modules
**File:** `simulation/core/tick/tick_orchestrator.cpp:87-107`
When multiple modules have in_degree=0 simultaneously, `std::queue` processes them in insertion order, which depends on registration order in `register_base_game_modules.cpp`. This is deterministic as long as registration order doesn't change, but is fragile — adding a new module could change the execution order of existing modules with no explicit dependency.

**Fix:** Use a priority queue sorted by module name as a tiebreaker, documented as the canonical tie-breaking rule.

### M2. `NpcBehaviorModule` overwrites `capital_delta` for work/criminal actions
**File:** `simulation/modules/npc_behavior/npc_behavior_module.cpp:389-403`
```cpp
if (best_cost > 0.0f) {
    npc_delta.capital_delta = -best_cost;  // line 390
}
if (best_eval.action == DailyAction::work) {
    float wage = 50.0f * employment_rate;
    npc_delta.capital_delta = wage;  // overwrites the -best_cost!
}
```
If an NPC's best action is `work` and it has `best_cost > 0`, the cost deduction is silently overwritten by the wage. Similarly, `criminal_activity` income overwrites any prior cost.

**Fix:** Use additive logic: `npc_delta.capital_delta = (npc_delta.capital_delta.value_or(0.0f)) + wage;`

### M3. Many deferred work handlers are no-ops (stub implementations)
**File:** `simulation/core/tick/drain_deferred_work.cpp`
Handlers for: `consequence`, `transit_arrival`, `npc_business_decision`, `market_recompute`, `investigator_meter_update`, `climate_downstream`, `player_travel_arrival`, `community_stage_check`, `maturation_advance`, `commercialize`, `interception_check` are all no-ops.

**Impact:** Items pushed to the deferred work queue for these types are silently consumed and do nothing. This is expected during bootstrap but should be tracked.

### M4. `demand_buffer` reset happens but `supply` is never decayed
**File:** `simulation/core/tick/tick_orchestrator.cpp:141-143`
The orchestrator resets `demand_buffer` to 0 each tick, but there's no equivalent decay for `supply`. Supply only changes via module deltas. If production modules produce supply but no module consumes it, supply accumulates without bound (until hitting `MARKET_SUPPLY_CEILING = 1e8`).

**Fix:** Consider adding natural supply decay (perishable goods) or ensure `npc_spending` consumption is sufficient to prevent unbounded accumulation.

### M5. Motivation delta in `apply_deltas` only modifies `weights[0]`
**File:** `simulation/core/world_state/apply_deltas.cpp:119-130`
The `motivation_delta` application adds the delta only to `weights[0]` (financial_gain), then renormalizes. But `NpcBehaviorModule` emits `motivation_diff` (total absolute difference across all weights) as the delta value. This means the actual motivation shift computed by the behavior module is not properly transferred — only a scalar "something changed" signal gets applied to a single slot.

**Impact:** NPC motivation shifts don't actually take effect as designed.
**Fix:** Either pass the full MotivationVector as a replacement in NPCDelta, or add per-slot motivation deltas.

### M6. No integration tests with actual modules registered
**File:** `simulation/tests/determinism/determinism_test.cpp`
All determinism tests run with `finalize_registration()` called on empty orchestrators (no modules). The 365-tick scale test only verifies orchestrator + state management. There are no tests that register actual modules and verify deterministic behavior end-to-end.

**Fix:** Add a determinism test that registers at least the Tier 1-3 modules (production, supply_chain, price_engine, labor_market) and verifies bit-identical state after N ticks.

### M7. CI does not run scenario tests
**File:** `.github/workflows/ci.yml:33-35`
```yaml
# Scenario tests are expected to FAIL during bootstrap.
# Uncomment when module implementations are ready:
# - name: Run scenario tests
```
42 modules are implemented, but scenario tests are still disabled.

### M8. WorldState destructor does not free raw pointer members
**File:** `simulation/core/world_state/world_state.h`
`WorldState` is a struct with no destructor. Members `player`, `lod2_price_index`, and `Province::cohort_stats` are raw pointers allocated with `new` in persistence. Without a destructor or smart pointers, these leak.

---

## LOW Severity Issues

### L1. `good_id_from_string` uses a weak hash function
**File:** `simulation/modules/production/production_module.cpp:69-75`
```cpp
uint32_t hash = 0;
for (char c : good_id_str) {
    hash = hash * 31 + static_cast<uint32_t>(c);
}
```
The hash `hash * 31 + c` is a classic Java string hash but has known collision rates. With ~60 goods currently this is fine, but could become problematic as the goods catalog grows.

### L2. `CrossProvinceDeltaBuffer::push` is not thread-safe
**File:** `simulation/core/world_state/cross_province_delta_buffer.cpp:10-12`
The comment in `delta_buffer.h:152` says "Thread-safe via partitioning: each province worker appends to its own partition." But the actual implementation is a single vector with no partitioning. Currently safe only because the thread pool is a stub.

### L3. Province delta merge in orchestrator is verbose and could miss new delta types
**File:** `simulation/core/tick/tick_orchestrator.cpp:168-199`
Each new DeltaBuffer field requires adding explicit merge code. If a developer adds a new delta vector to DeltaBuffer but forgets to update the merge loop, deltas are silently lost.

**Fix:** Consider a `DeltaBuffer::merge_from(DeltaBuffer&& other)` method.

### L4. `player_delta` not merged in province-parallel path
**File:** `simulation/core/tick/tick_orchestrator.cpp:168-199`
The province delta merge loop copies vectors but never merges `province_delta.player_delta` into `delta.player_delta`. If a province-parallel module writes player deltas, they are silently lost.

### L5. No validation of goods CSV against recipe inputs/outputs
The goods catalog (`goods_tier0.csv` through `goods_tier4.csv`) and recipes (`recipes_*.csv`) are loaded independently. There's no build-time or load-time validation that all recipe input/output `good_id` keys exist in the goods catalog.

### L6. Some modules register Tier comments don't match `runs_after` chains
In `register_base_game_modules.cpp`, comment "Tier 4: Depends on price engine" includes `FinancialDistributionModule`, but `FinancialDistributionModule`'s INTERFACE says it `runs_after: ["price_engine"]` — this is correct. However, it's registered before `NpcBusinessModule` which also says Tier 4 but has no explicit dependency between them. The topological sort handles this, but comment organization is misleading.

### L7. Test world factory `create_test_player` casts `NPCTravelStatus` from literal
**File:** `simulation/tests/test_world_factory.h:33`
```cpp
player.travel_status = static_cast<NPCTravelStatus>(0);  // resident
```
This relies on enum value mapping that could change. Should use the named enum value.

### L8. Benchmark test exists but no performance gate in CI
**File:** `.github/workflows/benchmark.yml` exists but benchmarks don't gate PR merges. The 500ms tick budget is a stated contract but is not enforced.

---

## Architecture Assessment

### Strengths
1. **Clean module interface** — `ITickModule` is well-designed with clear separation of province-parallel and sequential execution
2. **DeltaBuffer pattern** — enforces the "read WorldState const, write deltas" rule effectively
3. **Deterministic RNG** — SplitMix64 with fork() for province-parallel streams is correct and performant
4. **Topological ordering** — Kahn's algorithm with cycle detection is robust
5. **CI enforces key invariants** — dependency direction lint and forbidden randomness check
6. **Good test coverage** — 40+ unit test files, determinism tests, scenario tests
7. **Data-driven design** — CSV goods/recipes/facilities, JSON config — well separated from code
8. **Comprehensive type system** — rich enums, well-documented structs with invariants

### Risks
1. **Concurrency untested** — All parallelism is stub; latent races exist (C1, H2, L2)
2. **Performance at scale** — O(N) linear scans will fail at 2000 NPCs (H5)
3. **Many stub handlers** — 11 of 15 deferred work handlers are no-ops (M3)
4. **Memory management** — Raw pointers in WorldState need migration to smart pointers (C2, M8)

---

## Recommended Priority Order

1. **Fix C1** (const_cast data race) — architectural violation, blocks threading
2. **Fix C3** (drain_deferred_work bypasses DeltaBuffer) — architectural violation
3. **Fix H6** (cross-province deltas dropped) — silent data loss
4. **Fix C2/M8** (raw pointers → smart pointers) — memory leaks
5. **Fix H1** (home_province_id vs current_province_id) — simulation correctness
6. **Fix M5** (motivation delta) — NPC behavior not working as designed
7. **Fix M2** (capital_delta overwrite) — economic calculation error
8. **Fix H3** (evidence token ID collision) — data integrity
9. **Add integration determinism test** (M6) — validate end-to-end
10. **Performance audit** (H5) — before scaling to 2000 NPCs
