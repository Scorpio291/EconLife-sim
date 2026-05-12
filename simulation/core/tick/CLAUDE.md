# tick — Developer Context

## What This Module Does
Tick orchestration and scheduling infrastructure. Contains:
- `ITickModule` — interface all simulation modules implement
- `TickOrchestrator` — topological sort and dispatch of modules each tick
- `DeferredWorkQueue` — unified min-heap for scheduled work, drained once
  per tick before the module loop
- `PackageManager` — loads base_game, expansions, mods in order

## Key Files
- `tick_module.h` — ITickModule interface, ModuleScope enum
- `tick_orchestrator.h` — TickOrchestrator class
- `deferred_work.h` — WorkType enum, DeferredWorkItem, payload structs
- `package_manager.h` — PackageManager, MigrationRegistry, ScriptEngine

## Critical Rules
- Module list is immutable after `finalize_registration()`
- Province-parallel modules merge results in ascending province index order
- Same seed + same inputs = identical tick output regardless of core count
- Mod module exceptions are caught and the module is disabled; base game exceptions propagate
- Step-number references in design docs ("Step 2: drain queue", "27 steps")
  describe the V1 base-game module set as a guideline. The contract is the
  topological ordering (runs_after / runs_before), not the step number.
  Additional packages append to the effective tick.

## Dependency Direction
This is core infrastructure. Everything depends on this; this depends on nothing.

## Interface Specs
- docs/interfaces/tick_orchestrator/INTERFACE.md
- docs/interfaces/deferred_work_queue/INTERFACE.md
