# Module: tick_orchestrator

## Purpose
Manages the registration, topological sorting, and sequential/parallel execution of all tick modules. Runs the 27-step daily tick by dispatching modules in dependency order, merging delta buffers deterministically.

## Inputs (from WorldState)
- `current_tick` — tick counter for scheduling
- `provinces` — province list for parallel dispatch sizing
- `deferred_work_queue` — drained in the pre-step before modules run (see note below)
- `cross_province_delta_buffer` — flushed at tick start

## Outputs (to DeltaBuffer)
- None directly. Orchestrates module execution; each module writes its own deltas.
- Applies merged DeltaBuffer to WorldState after each step completes.

## Preconditions
- `finalize_registration()` has been called.
- All registered modules have valid, non-cyclic dependency declarations.
- Thread pool is initialized with `min(hardware_concurrency() - 1, 6)` workers.

## Postconditions
- All registered modules have executed in topological order.
- All DeltaBuffers merged in ascending province index order.
- `WorldState.current_tick` incremented by 1.
- `cross_province_delta_buffer` flushed and cleared.

## Invariants
- Module list is immutable after `finalize_registration()`.
- Same seed + same inputs = identical tick output regardless of core count.
- Province-parallel modules merge in province index order (0, 1, 2...).
- Floating-point accumulations use canonical sort (good_id asc, province_id asc).

## Failure Modes
- Cycle detected during `finalize_registration()`: panic with named cycle.
- Missing dependency name: panic at startup with descriptive error.
- Mod module exception: catch, log, disable module for session, notify player.
- Base game/expansion exception: propagate (crash).

## Performance Contract
- Full tick with all 27 steps: < 500ms at 2,000 NPCs (acceptable), < 200ms (target) on 6 cores.
- Thread pool: `min(hardware_concurrency() - 1, 6)` workers.

## Dependencies
- This IS the orchestrator — no runs_after/runs_before.

## Notes
- **Deferred work queue drain position:** Items scheduled in tick N fire at the start of tick N+1,
  before any module runs. The TDD §6 labels this "Step 2" (within the module loop), but the
  implementation drains the queue before the loop begins. The behavior is equivalent — consequences
  are visible to all modules in tick N+1. The GDD §21 step count discrepancy (27 vs 28 steps) is an
  open documentation ambiguity; do not change drain position without explicit design approval.

## Test Scenarios
- `tick_executes_in_topological_order`: Register 3 modules with A→B→C dependency. Verify execution order.
- `cycle_detection_panics`: Register A→B and B→A. Verify panic with named cycle.
- `province_parallel_merge_is_deterministic`: Run 100 ticks with province-parallel module on 1 core and 6 cores. Verify bit-identical output.
- `mod_exception_disables_module`: Register a mod module that throws. Verify it's disabled and tick completes.
- `finalize_locks_registration`: Call finalize, then register_module. Verify rejection.
