# Orchestrator Execution Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Execute the Orchestrator loop — bootstrap Pass 2 (Tier 6-12 scaffolding), then implement all 48 modules from Tier 0 through Tier 12.

**Architecture:** The Orchestrator is this Claude Code session. It dispatches subagents for each module implementation. State is tracked in `docs/build_dependency_graph.json`. Each module subagent reads its INTERFACE.md, implements `execute()`/`execute_province()`, writes tests, and returns. The Orchestrator validates (cmake --build + ctest), auto-commits on success, and moves to the next module.

**Tech Stack:** C++20, CMake 3.25+, Catch2 v3, MSVC 19.43 (Visual Studio 2022)

---

### Task 1: Set Up Orchestrator Infrastructure

**Files:**
- Create: `docs/session_logs/flagged_issues.md`

**Step 1: Create flagged issues tracker**

```markdown
# Flagged Issues

Issues discovered during autonomous implementation that require human review.

| Date | Module | Category | Description | Status |
|------|--------|----------|-------------|--------|
```

**Step 2: Verify build is clean**

Run: `cmake --build build --config Debug -j 2>&1 | tail -5`
Expected: All targets build successfully, zero errors

**Step 3: Commit**

```bash
git add docs/session_logs/flagged_issues.md
git commit -m "chore: add orchestrator session infrastructure"
```

---

### Task 2: Bootstrap Pass 2 — Tier 6-12 Scaffolding

**Goal:** Generate interface specs, headers, stubs, CLAUDE.md, and CMakeLists for all 27 Tier 6-12 modules. Same artifacts as Pass 1 produced for Tier 0-5.

**Modules (27 total):**
- Tier 6 (4): npc_spending, evidence, obligation_network, community_response
- Tier 7 (4): facility_signals, criminal_operations, media_system, antitrust
- Tier 8 (5): investigator_engine, money_laundering, drug_economy, weapons_trafficking, protection_rackets
- Tier 9 (4): legal_process, informant_system, alternative_identity, designer_drug
- Tier 10 (4): political_cycle, influence_network, trust_updates, addiction
- Tier 11 (4): regional_conditions, population_aging, currency_exchange, lod_system
- Tier 12 (1): persistence

**Step 1: Dispatch parallel agents per tier-group for interface specs + headers + stubs**

For each tier group, dispatch one subagent that:
1. Reads the relevant TDD sections from `docs/design/EconLife_Technical_Design_v29.md`
2. Reads the GDD from `docs/design/EconLife_GDD.md` for behavioral context
3. Reads `docs/design/EconLife_Feature_Tier_List.md` for V1/EX scope classification
4. For each module in the tier:
   - Writes `docs/interfaces/[module]/INTERFACE.md` following existing spec format
   - Writes `simulation/modules/[module]/[module]_types.h` with module-specific types
   - Writes `simulation/modules/[module]/[module]_module.cpp` as ITickModule stub
   - Writes `simulation/modules/[module]/CLAUDE.md` developer context
   - Writes `simulation/modules/[module]/CMakeLists.txt` with correct dependencies
5. Creates directory structure first: `mkdir -p simulation/modules/[module]` and `mkdir -p docs/interfaces/[module]`

Group agents: Tiers 6-7 (8 modules), Tiers 8-9 (9 modules), Tiers 10-12 (9 modules).

**Step 2: Update modules CMakeLists.txt**

Add `add_subdirectory()` calls for all 27 new modules in `simulation/modules/CMakeLists.txt` under the "Pass 2" section.

**Step 3: Add test stubs for Tier 6-12 modules**

Extend `simulation/tests/unit/module_exists_test.cpp` with TEST_CASE entries for each new module.
Add new scenario test files if the interface specs define behavioral scenarios.

**Step 4: Update dependency graph**

Set status from `"pending"` to `"stub"` for all 27 modules. Add missing fields (`cmake_target`, `province_parallel`, `interface_spec`).

**Step 5: Build and test**

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Debug -DECONLIFE_BUILD_TESTS=ON -DECONLIFE_BUILD_BENCHMARKS=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5`
Run: `cmake --build build --config Debug -j`
Run: `ctest --test-dir build -C Debug -L module --output-on-failure`
Expected: All module-exists tests pass, zero build errors.

**Step 6: Commit**

```bash
git add -A
git commit -m "bootstrap: Pass 2 scaffolding for Tier 6-12 modules (27 modules)"
```

---

### Task 3: Implement Tier 0 — Core Infrastructure

**Modules (4):** rng, world_state (+ delta_buffer), deferred_work_queue, tick_orchestrator

**These are special:** Tier 0 modules are in `simulation/core/`, not `simulation/modules/`. They form the `econlife_core` library target. They must be implemented together since they are tightly coupled (TickOrchestrator uses DeferredWorkQueue, which uses WorldState, which uses DeltaBuffer, all using RNG).

**Step 1: Dispatch subagent for RNG implementation**

Context package:
- Read: `docs/interfaces/rng/INTERFACE.md`
- Read: `simulation/core/rng/deterministic_rng.h`
- Read: `simulation/core/rng/CLAUDE.md`

Subagent writes:
- `simulation/core/rng/deterministic_rng.cpp` — full implementation with SplitMix64 or xoshiro256** algorithm
- `simulation/tests/unit/rng_test.cpp` — tests from INTERFACE.md: same-seed reproducibility, fork independence, distribution uniformity, performance (< 10ns per call)
- Updates `simulation/core/CMakeLists.txt` to add new .cpp source
- Updates `simulation/tests/unit/CMakeLists.txt` to add new test file

Build + test. Auto-commit on success. Update dependency graph: rng → "done".

**Step 2: Dispatch subagent for WorldState + DeltaBuffer implementation**

Context package:
- Read: `docs/interfaces/world_state/INTERFACE.md`
- Read: `simulation/core/world_state/world_state.h`
- Read: `simulation/core/world_state/delta_buffer.h`
- Read: `simulation/core/world_state/npc.h`, `player.h`, `geography.h`
- Read: `simulation/core/world_state/CLAUDE.md`

Subagent writes:
- `simulation/core/world_state/world_state.cpp` — WorldState initialization, delta application logic
- `simulation/core/world_state/delta_buffer.cpp` — DeltaBuffer merge/apply implementation
- `simulation/tests/unit/world_state_test.cpp` — tests: delta application (additive vs replacement), clamping, canonical sort merge, empty delta no-op
- Updates CMakeLists

Build + test. Auto-commit on success. Update dependency graph: world_state → "done".

**Step 3: Dispatch subagent for DeferredWorkQueue implementation**

Context package:
- Read: `docs/interfaces/deferred_work_queue/INTERFACE.md`
- Read: `simulation/core/tick/deferred_work.h`

Subagent writes:
- `simulation/core/tick/deferred_work.cpp` — min-heap push/pop/drain, recurring rescheduling
- `simulation/tests/unit/deferred_work_test.cpp` — tests: ordering by due_tick, drain returns only due items, recurring reschedule, empty queue behavior

Build + test. Auto-commit. Update graph: deferred_work_queue → "done".

**Step 4: Dispatch subagent for TickOrchestrator implementation**

Context package:
- Read: `docs/interfaces/tick_orchestrator/INTERFACE.md`
- Read: `simulation/core/tick/tick_orchestrator.h`
- Read: `simulation/core/tick/tick_module.h`
- All previously implemented Tier 0 modules (RNG, WorldState, DeferredWorkQueue)

Subagent writes:
- `simulation/core/tick/tick_orchestrator.cpp` — Kahn's algorithm topological sort, province-parallel dispatch with thread pool, delta merge in ascending province order
- `simulation/tests/unit/tick_orchestrator_test.cpp` — tests: topological sort ordering, cycle detection, province-parallel dispatch + deterministic merge, module exception handling

Build + test. Auto-commit. Update graph: tick_orchestrator → "done".

**Step 5: Full build verification**

Run: `cmake --build build --config Debug -j`
Run: `ctest --test-dir build -C Debug --output-on-failure`
Expected: All Tier 0 tests pass, all existing module-exists tests still pass.

---

### Task 4: Implement Tier 1 — Independent Domain Modules

**Modules (4, parallel):** production, calendar, scene_cards, random_events

**Step 1: Dispatch 4 subagents in parallel**

Each subagent receives its module's context package:
- `docs/interfaces/[module]/INTERFACE.md`
- `simulation/modules/[module]/*_types.h`
- `simulation/modules/[module]/*_module.cpp` (stub to replace)
- `simulation/modules/[module]/CLAUDE.md`
- Core headers: `world_state.h`, `delta_buffer.h`, `tick_module.h`, `deterministic_rng.h`
- Relevant TDD sections from `docs/design/EconLife_Technical_Design_v29.md`

Each subagent:
1. Reads interface spec thoroughly
2. Replaces no-op `execute()` / `execute_province()` with real logic
3. Wires the module-exists test to actually instantiate the module
4. Writes module-specific unit tests based on INTERFACE.md test scenarios
5. Builds and runs tests (up to 3 debug attempts)

**Step 2: After all 4 return, run full build + test**

Run: `cmake --build build --config Debug -j`
Run: `ctest --test-dir build -C Debug --output-on-failure`

**Step 3: Auto-commit each passing module**

```bash
git commit -m "feat(production): implement production module per INTERFACE.md"
git commit -m "feat(calendar): implement calendar module per INTERFACE.md"
# etc.
```

Update dependency graph: each → "done".

---

### Task 5: Implement Tier 2 — Production Dependents

**Modules (3, parallel):** supply_chain, labor_market, seasonal_agriculture

Same pattern as Task 4. All depend on production (now "done"). Dispatch 3 parallel subagents. Each reads its INTERFACE.md + production module headers for context.

Additional context for these modules:
- `simulation/modules/economy/economy_types.h` (shared business/market types)
- `simulation/modules/trade_infrastructure/trade_types.h` (for supply_chain)
- `simulation/modules/labor_market/labor_types.h`
- `simulation/modules/seasonal_agriculture/agriculture_types.h`

Build, test, commit, update graph.

---

### Task 6: Implement Tier 3 — Price Discovery

**Modules (2, parallel):** price_engine, trade_infrastructure

Same pattern. Both depend on supply_chain (now "done"). Dispatch 2 parallel subagents.

**Critical for price_engine:** Must use canonical float sort order (good_id asc, province_id asc). Verify this in tests.

Build, test, commit, update graph.

---

### Task 7: Implement Tier 4 — Financial & Business

**Modules (4, parallel):** financial_distribution, npc_business, commodity_trading, real_estate

Same pattern. All depend on price_engine (now "done"). Dispatch 4 parallel subagents.

Build, test, commit, update graph.

---

### Task 8: Implement Tier 5 — NPC & Services

**Modules (4, parallel):** npc_behavior, banking, government_budget, healthcare

Same pattern. All depend on financial_distribution (now "done"). Dispatch 4 parallel subagents.

**npc_behavior is the heaviest module** — motivation evaluation, daily decision-making, memory formation/decay, relationship updates. Performance budget is strict. Subagent should profile.

Build, test, commit, update graph.

---

### Task 9: Implement Tier 6 — NPC Extensions

**Modules (4, parallel):** npc_spending, evidence, obligation_network, community_response

All depend on npc_behavior (now "done"). Same dispatch pattern.

Build, test, commit, update graph.

---

### Task 10: Implement Tier 7 — Investigation & Media

**Modules (4, parallel):** facility_signals, criminal_operations, media_system, antitrust

All depend on evidence (now "done"). Same dispatch pattern.

Build, test, commit, update graph.

---

### Task 11: Implement Tier 8 — Criminal Economy

**Modules (5, parallel):** investigator_engine, money_laundering, drug_economy, weapons_trafficking, protection_rackets

All depend on criminal_operations (now "done"). Same dispatch pattern.

Build, test, commit, update graph.

---

### Task 12: Implement Tier 9 — Legal & Identity

**Modules (4, parallel):** legal_process, informant_system, alternative_identity, designer_drug

All depend on investigator_engine (now "done"). Same dispatch pattern.

Build, test, commit, update graph.

---

### Task 13: Implement Tier 10 — Political & Social

**Modules (4, parallel):** political_cycle, influence_network, trust_updates, addiction

All depend on community_response (now "done"). Same dispatch pattern.

Build, test, commit, update graph.

---

### Task 14: Implement Tier 11 — Regional & Systems

**Modules (4, mixed dependencies):** regional_conditions, population_aging, currency_exchange, lod_system

Dependencies vary — dispatch as dependencies become available:
- population_aging depends on healthcare (done at Tier 5)
- currency_exchange depends on commodity_trading + government_budget (done at Tiers 4-5)
- regional_conditions depends on political_cycle + influence_network (done at Tier 10)
- lod_system depends on regional_conditions

So: population_aging + currency_exchange can run in parallel first, then regional_conditions, then lod_system.

Build, test, commit, update graph.

---

### Task 15: Implement Tier 12 — Persistence

**Modules (1):** persistence

Depends on world_state (done at Tier 0). Final module.

Subagent implements:
- WorldState serialization (Protocol Buffers or custom binary)
- LZ4 compression for snapshots
- Save/load round-trip determinism (must serialize-deserialize-serialize and get identical bytes)

Build, test, commit, update graph.

---

### Task 16: Full Integration Verification

**Step 1: Run complete test suite**

Run: `cmake --build build --config Release -j`
Run: `ctest --test-dir build -C Release --output-on-failure`
Expected: All unit tests pass, all determinism tests pass, scenario tests that depend on implemented modules pass.

**Step 2: Run 27-step tick integration test**

Create and run an integration test that:
1. Creates a WorldState with known seed
2. Registers all 48 modules via TickOrchestrator
3. Runs 100 ticks
4. Verifies determinism (run again, same result)
5. Verifies all modules executed in correct order

**Step 3: Review flagged issues**

Read `docs/session_logs/flagged_issues.md`. Report any unresolved items to human.

**Step 4: Final commit**

```bash
git commit -m "feat: all 48 modules implemented — full tick loop operational"
```

Update all dependency graph entries to "done".

---

## Orchestrator Loop Template (For Each Tier)

The Orchestrator repeats this for every tier:

```
1. READ dependency graph → find modules where all deps are "done" and status is "stub"
2. For each ready module, DISPATCH subagent with context package:
   - INTERFACE.md + headers + TDD sections + CLAUDE.md
   - Instruction: implement execute(), write tests, build & test, max 3 retries
3. AFTER subagent returns:
   - Run full build: cmake --build build --config Debug -j
   - Run full tests: ctest --test-dir build -C Debug --output-on-failure
   - If PASS → auto-commit, update graph status to "done"
   - If FAIL → log to flagged_issues.md, set status to "blocked", continue
4. LOOP to step 1 until no more ready modules
```

## Failure Recovery

If a module is "blocked":
- Log the failure details in `docs/session_logs/flagged_issues.md`
- Skip to next available module
- After tier-mates complete, retry blocked modules (fresh context)
- If still blocked after retry, flag for human and continue

## Commit Convention

```
feat(module_name): implement [module] per INTERFACE.md
bootstrap: Pass 2 scaffolding for Tier N modules
chore: update dependency graph after Tier N
fix(module_name): resolve [issue] in [module]
```
