# Orchestrator Design

**Date:** 2026-03-14
**Status:** Approved
**Context:** Bootstrap Pass 1 complete (Tiers 0-5 scaffolded). This design covers the Orchestrator that drives all subsequent development.

## Overview

The Orchestrator is a single Claude Code session that manages the entire development pipeline autonomously. It reads project state, picks the next module to implement, dispatches subagents for implementation, validates results, auto-commits on success, and loops.

### Two Phases

**Phase A — Bootstrap Pass 2:** Generate interface specs, headers, stubs, and test scaffolds for Tier 6-12 modules (~27 modules). Same process as Pass 1 but automated by the Orchestrator.

**Phase B — Implementation:** Implement every module from Tier 0 through Tier 12, replacing no-op stubs with real logic that passes all test scenarios.

## The Loop

```
1. Read state (dependency graph, test results)
2. Pick next module (dependency-ready, by tier)
3. Generate context (interface + headers + TDD sections)
4. Implement (dispatch subagent per module)
5. Build + Test (cmake --build, ctest)
6. If pass → auto-commit, update CLAUDE.md, update dependency graph
   If fail → debug (max 3 retries), then flag + skip
7. Loop back to step 1
```

## Module Selection

Algorithm for picking the next module:

1. Read `docs/build_dependency_graph.json`
2. Filter for modules where all dependencies have status `"done"` and module itself is `"stub"`
3. Sort by tier ascending (Tier 0 first)
4. Within a tier, use document order from the dependency graph

### Status Progression

```
Phase A:  pending → stub
Phase B:  stub → implementing → passing → done
```

- **pending**: Not yet bootstrapped (Tier 6-12 before Pass 2)
- **stub**: Bootstrapped with interface spec, headers, no-op implementation
- **implementing**: Orchestrator is actively working on it
- **passing**: Implementation compiles and module-specific tests pass
- **done**: Full build + all tests pass, committed

### Parallelism

Modules at the same tier with no cross-dependencies can be implemented by parallel subagents. Example: Tier 1 has 4 independent modules — 4 subagents work simultaneously.

## Implementation Session Per Module

Each module implementation is dispatched as a subagent with this context package:

1. The module's `INTERFACE.md`
2. The module's existing header(s)
3. Headers of dependencies (WorldState, DeltaBuffer, NPC, etc.)
4. The module's stub `.cpp`
5. Relevant TDD sections
6. The module's `CLAUDE.md`

### Subagent Responsibilities

1. Read the interface spec — understand inputs, outputs, invariants
2. Implement `execute()` or `execute_province()` — replace the no-op stub
3. Wire the module-exists test — instantiate module, verify `name()`
4. Implement named test scenarios from INTERFACE.md
5. Build and run tests
6. Fix failures (up to 3 debug iterations)

### Subagent Constraints

- Does NOT modify interface specs or shared headers (flags these)
- Does NOT change other modules' code
- Does NOT expand scope beyond the single module

### After Subagent Returns

1. Orchestrator runs full build (`cmake --build`)
2. Runs all tests (`ctest`) — not just the module's tests
3. If green → auto-commit: `"feat(module_name): implement [module] per INTERFACE.md"`
4. Update `build_dependency_graph.json` status to `"done"`
5. Update module's CLAUDE.md with decisions made

## Failure Handling

### Tier 1 — Build/Test Failure (auto-retry)

- Subagent gets 3 internal attempts
- If all fail → mark module as `"blocked"` in dependency graph
- Log failure in `docs/session_logs/`
- Move to next ready module
- Blocked modules retried after tier-mates complete

### Tier 2 — Interface/Header Conflict (flag + skip)

- Subagent discovers interface spec is wrong or shared header needs changes
- Returns a flag describing what needs to change
- Orchestrator logs to `docs/session_logs/flagged_issues.md`
- Skips module, continues with next

### Tier 3 — Genuine Ambiguity (flag + skip)

- GDD/TDD is contradictory or underspecified
- Same handling as Tier 2 — logged, flagged, skipped
- Orchestrator does NOT guess at design intent

### Recovery

When a flagged issue is resolved (human edits interface spec or header), the Orchestrator picks up the unblocked module on its next pass.

## Context Management

- Each module implementation is a separate subagent with fresh context
- The Orchestrator loop itself is lightweight — manages state, dispatches work
- No implementation details carried across modules
- State persisted in files: dependency graph JSON, session logs, CLAUDE.md files

## State Files

| File | Purpose |
|------|---------|
| `docs/build_dependency_graph.json` | Module status tracking |
| `docs/session_logs/flagged_issues.md` | Issues requiring human review |
| `docs/session_logs/YYYY-MM-DD-module.md` | Per-session implementation log |
| Module `CLAUDE.md` files | Accumulated developer context |
