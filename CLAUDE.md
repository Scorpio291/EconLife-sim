# EconLife — Developer Context

## What This Project Is
A deterministic agent-based economic simulation game. The simulation
runs headlessly at one tick per in-game day. The UI observes simulation
state. The player interacts with the UI. January 2000 start date with
dynamic era progression.

## The One Rule That Cannot Break
The simulation core (simulation/) must never import from ui/.
Dependency goes one direction: ui/ depends on simulation/.
Build CI enforces this. Do not route around it.

## Determinism is Required
All random draws go through `simulation/core/rng/DeterministicRNG`.
Never use std::rand, system time, or thread ID as entropy sources.
Same seed + same inputs = same outputs. CI runs determinism tests on
every commit.

## Module Interface Contract
Every module in simulation/modules/ has a corresponding interface spec
in docs/interfaces/[module]/INTERFACE.md. Read the interface spec before
reading the implementation. If implementation diverges from the spec,
the spec wins — update the spec through review, not by silently
diverging.

## Architecture Overview
- **Tick Orchestrator:** Runs registered modules per tick in topological order.
  Modules register via `ITickModule` interface with `runs_after()`/`runs_before()`.
  Step counts named in design docs ("27 steps", "Step 2: drain queue") are
  guidelines that reflect the V1 base-game module set; additional packages,
  expansions, and mods append to the effective tick. Module ordering — not
  step number — is the contract.
- **Province-parallel:** Modules that are independent per province dispatch
  to a thread pool (one thread per province, max 6). Results merge in
  ascending province index order for determinism.
- **DeltaBuffer:** Modules read `WorldState` (const) and write to `DeltaBuffer`.
  WorldState is never modified mid-tick.
- **DeferredWorkQueue:** Single min-heap for scheduled work (consequences,
  transit arrivals, decay batches, business decisions). The orchestrator
  drains it once per tick, before the module loop.
- **Cross-province effects:** One-tick propagation delay via `CrossProvinceDeltaBuffer`.
- **Package system:** Base game, expansions, and mods load in topological order.
  Same capability model; distinction is trust level.

## Current Development Status
Phase: Integration & behavioral validation. Breadth is largely in — ~50 V1
modules exist and pass their unit tests (~1,600 fast tests). The open work is
connective: closing cross-module feedback loops that were stubbed with proxies/
stand-ins, and getting the simulation to produce the emergent behavior the GDD
promises rather than just staying non-NaN and bounded.

A 10-year orchestrated baseline (simulation/tests/integration/emergence_observe)
shows several loops are broken/frozen — most notably the criminal
detection→prosecution→imprisonment loop never closes, and province conditions
(stability/grievance/unemployment) saturate to extremes with no restoring force.
These are tracked as Catch [!shouldfail] ratchets in the opt-in "emergence"
behavioral suite and written up in docs/session_logs/emergence_baseline_2026-06-10.md.

Test gates:
- Fast per-commit gate:  ctest -LE emergence   (excludes the slow behavioral runs)
- Behavioral suite:      ctest -L emergence    (multi-year orchestrated runs; ~tens of seconds)

See docs/design/EconLife_Feature_Tier_List.md for what is V1 scope.
See docs/session_logs/ for AI session history.

## Performance Contracts
Tick must complete in < 500ms at 2,000 significant NPCs.
Target: < 200ms on 6 cores.
Benchmarks are in simulation/tests/benchmarks/.
CI enforces the 200 ms target — not the 500 ms ceiling — to catch
regressions early. A failing benchmark gate means "we regressed below
target", not "we breached the ceiling".
Do not merge code that regresses benchmarks without explicit approval.

## Coding Standards
- Language: C++20 (simulation), TypeScript + React (UI)
- Formatting: clang-format, config at root .clang-format
- All new simulation code requires unit tests in simulation/tests/unit/
- All new modules require an integration test scenario
- No raw pointers in module code — use smart pointers or pool allocation
- Floating-point accumulations use canonical sort order (good_id asc,
  province_id asc) to prevent IEEE 754 non-associativity drift

## Data-Driven Content
Goods, recipes, and facility types are loaded from CSV files in
packages/base_game/. Keyed by string identifiers (not integer enums).
Modders edit CSVs without recompilation.

## Key Design Documents
- GDD v1.7: docs/design/EconLife_GDD.md
- Technical Design v29: docs/design/EconLife_Technical_Design_v29.md
- Feature Tier List: docs/design/EconLife_Feature_Tier_List.md
- Commodities & Factories: docs/design/EconLife_Commodities_and_Factories_v23.md
- R&D & Technology: docs/design/EconLife_RnD_and_Technology_v22.md
- AI Development Plan: docs/design/EconLife_AI_Development_Plan_updated.md

## Who to Ask When Unsure
Design questions: Read GDD v1.7
Architecture questions: Read Technical_Design_v29.md
Scope questions: Read Feature_Tier_List.md
If still unclear: Do not guess. Flag for human review.
