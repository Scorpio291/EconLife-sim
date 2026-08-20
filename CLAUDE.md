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

## Grounding Doctrine — No Magic Rails (ratified 2026-07-03)
Mechanisms first, calibration second, rails never:
- **No arbitrary caps that shape behavior.** A limit must be physical: a
  probability arrives as `1 - exp(-rate)`; land is finite; a granary holds what
  was stored; assimilation saturates on a timescale. `std::clamp`/`std::min` are
  permitted only as crash sentinels for non-finite values — never as gameplay.
- **Every flow is conserved and located.** Every effect must trace to a mechanism
  you can point at in the world: who ate the grain, who paid the coin, who died,
  where the loss went. Signals derived from stocks must eventually be wired as
  real flows on those stocks, not remain parallel bookkeeping.
- **Constants must be defensible in real units** (an ox eats X per km, a soldier
  eats Y per campaign day, a levy is Z% of population). Pure pacing dials (era
  thresholds) are calibration and acceptable; mechanism-shaped constants that
  merely "make it work" are rails and are not.
- **Build the advanced mechanism first; scale back for performance explicitly**
  (LOD, batching, cadence, hop caps) and document each scaleback as such — never
  by silently replacing the mechanism with a rail.

**HOW TO CATCH ONE: docs/design/EconLife_No_Rails_Rule.md.** Read it before adding
any limit or constant. The principle above has been in force since 2026-07-03 and
three rails were still written and survived for months, because a rail looks exactly
like modelling. The short version:
- **Name the thing that stops it.** If the answer is "the constant", it is a rail.
  "It rises with the era/tier/level" is a schedule of outcomes with the mechanism
  left out — and it was the form of every rail found here.
- **Print the limit next to the value it limits.** A quantity sitting exactly on
  its bound is a rail doing the deciding. That is how all three were found.
- **Work the arithmetic at the extremes.** One "rare" threshold was arithmetically
  unreachable — never, not rare.
- **Ask what the constant is proportional to, and check the ratio.** A flat number
  applied at another magnitude decides the outcome by accident; and the WRONG
  proportionality can divide a real effect out entirely (fertility cancelled from
  the marginal product of labour because ceiling and half-saturation both carried it).
- **Ask who wrote the numerator and who wrote the denominator.** If one module writes
  both, the signal is reporting its own decision back to itself. The population-growth
  signal read 1.0 by construction for exactly this reason.
- **Expect removing a rail to expose a defect.** That is the point; the exposure is
  progress, not regression.
- **A per-tick rate is only correct if every tick runs.** Evolve stocks on a stated
  cadence or scale by elapsed time — never assume the caller's stride.

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
originally showed several loops broken/frozen. As of the 2026-06-11 milestone the
known broken-loop ratchets have been retired (e.g. the criminal
detection→prosecution→imprisonment loop now CLOSES — proven by a fast-gate test,
simulation/tests/integration/criminal_subsystem_integration_test.cpp; province
conditions gained restoring forces). Subsequent work grounded the economy in
conserved, located resources (extraction→production→energy→waste→fisheries/
agriculture) and is grounding the criminal economy likewise. See
docs/session_logs/emergence_baseline_2026-06-10.md for the running history; that log,
not this paragraph, is the source of truth for current loop status.

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
