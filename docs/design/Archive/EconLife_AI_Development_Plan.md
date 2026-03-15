# EconLife — AI-Coder Development Plan
*How to build a simulation of this complexity using AI coding tools*
*Companion document to GDD v0.9, Feature Tier List, and Technical Design Document*

---

## The Central Problem

EconLife is too large for any single AI coding session. A context window — even a large one — cannot hold the entire codebase, the full design intent, and the implementation task simultaneously. Naive use of AI coding tools on a project this size produces: inconsistent interfaces, duplicated logic, architectural drift, tests that don't test what matters, and code that works in isolation but breaks the simulation when integrated.

The solution is not to use less AI. It is to **structure the project so that AI tools can operate effectively within their constraints** — and to be deliberate about what humans own versus what AI generates.

This document describes that structure in full.

---

## Part 1 — The Foundational Principles

### 1.1 Context is the Asset

An AI coding tool is only as good as the context it has when it generates code. Context means: the precise task it's being asked to do, the interfaces it must conform to, the data structures it's working with, the invariants it must preserve, and the test that will verify its output.

The entire development methodology in this document is, at its core, a system for **manufacturing high-quality context** at the moment it's needed.

### 1.2 Interfaces First, Implementation Second

Every module in EconLife must have a fully specified interface — its inputs, outputs, preconditions, postconditions, and failure modes — before any implementation is written. An AI generating an implementation given a complete interface specification produces dramatically better code than an AI inferring both from a vague requirement.

**Rule:** No implementation task is given to an AI coder before the interface it must satisfy has been written, reviewed by a human, and committed to the repository.

### 1.3 The Simulation is the Source of Truth

The simulation must be deterministic, testable in isolation, and independent of the UI. This isn't just good architecture — it makes AI coding tractable. When the simulation is headless and deterministic, every AI-generated change can be validated by running the simulation against a known seed and comparing outputs. Regressions are immediately detectable.

**Rule:** The simulation core never depends on the UI layer. The UI layer depends on the simulation. This dependency direction is enforced at the build system level.

### 1.4 One Module, One Session

Each AI coding session works on one module. Not one system, not one feature — one module with a defined interface and a test suite that must pass. The session ends when the tests pass and the code is reviewed. The next session starts with a clean context built from the new state of the repository.

**Rule:** AI sessions are scoped to single modules. Multi-module sessions are prohibited.

### 1.5 Humans Own Architecture, AI Owns Implementation

The decisions about how systems connect, what the data structures look like, and what the interfaces between modules are — these are human decisions, made deliberately, documented explicitly, and not delegated to AI. AI generates the implementations that satisfy the architecture humans specify. This is not a limitation — it is the division of labor that makes the project viable.

---

## Part 2 — Repository Structure

The repository structure is not an afterthought. It is the physical manifestation of the architecture, and it must be designed to make AI sessions tractable.

```
econlife/
│
├── docs/
│   ├── design/
│   │   ├── GDD_v09.md                    # The game design document
│   │   ├── Feature_Tier_List.md          # What's in scope per tier
│   │   ├── Technical_Design.md           # Architecture specification
│   │   └── AI_Development_Plan.md        # This document
│   │
│   ├── interfaces/
│   │   ├── npc/                          # Interface specs for NPC module
│   │   ├── economy/                      # Interface specs for economy module
│   │   ├── evidence/                     # Interface specs for evidence module
│   │   └── [one folder per module]       # Written before implementation begins
│   │
│   └── session_logs/
│       └── [date]_[module]_[outcome].md  # Record of every AI session
│
├── simulation/                           # The headless simulation core
│   ├── core/
│   │   ├── tick/                         # Tick orchestration
│   │   ├── world_state/                  # World state data structures
│   │   └── rng/                          # Deterministic RNG
│   │
│   ├── modules/                          # One subdirectory per tick module
│   │   ├── production/
│   │   ├── supply_chain/
│   │   ├── npc_behavior/
│   │   ├── economy/
│   │   ├── evidence/
│   │   ├── consequence_queue/
│   │   ├── obligation_network/
│   │   ├── community_response/
│   │   ├── investigator_engine/
│   │   ├── legal_process/
│   │   ├── political_cycle/
│   │   ├── criminal_operations/
│   │   ├── influence_network/
│   │   └── [one directory per module]
│   │
│   └── tests/
│       ├── unit/                         # Per-module unit tests
│       ├── integration/                  # Cross-module integration tests
│       ├── determinism/                  # Same seed = same output
│       ├── scenarios/                    # Named simulation scenarios
│       └── benchmarks/                   # Performance regression tests
│
├── ui/                                   # UI layer — depends on simulation, never the reverse
│   ├── map/
│   ├── dashboards/
│   ├── scene_cards/
│   ├── calendar/
│   └── communications/
│
├── tools/
│   ├── sim_runner/                       # Headless simulation runner for testing
│   ├── world_inspector/                  # Debug tool: inspect world state at any tick
│   ├── scenario_builder/                 # Build and save named test scenarios
│   └── session_context_generator/       # Generates context packages for AI sessions (see Part 4)
│
└── CLAUDE.md                             # Root-level AI context file (see Part 3)
```

### The Interface Specification Files

Every module has an interface specification in `docs/interfaces/[module]/INTERFACE.md` before implementation begins. This file contains:

- **Purpose:** One paragraph. What this module does and why it exists.
- **Inputs:** Exactly what data the module reads from world state, with types.
- **Outputs:** Exactly what the module writes to the delta buffer, with types.
- **Preconditions:** What must be true about the world state when this module runs.
- **Postconditions:** What must be true about the delta buffer after this module runs.
- **Invariants:** Properties that must hold before and after execution.
- **Failure modes:** What the module does when inputs are malformed or preconditions aren't met.
- **Performance contract:** Maximum acceptable execution time at target NPC scale.
- **Test scenarios:** Named scenarios this module's tests must cover.

This file is written by a human engineer. It is the context package for the AI session that will implement the module.

---

## Part 3 — The CLAUDE.md System

AI coding tools read a `CLAUDE.md` file at the root of a repository for persistent project context. This file is the single most important non-code artifact in the project. Every engineer on the team — human and AI — starts every session by reading it.

### Root CLAUDE.md

```markdown
# EconLife — Developer Context

## What This Project Is
A deterministic agent-based economic simulation game. The simulation
runs headlessly at one tick per in-game day. The UI observes simulation
state. The player interacts with the UI.

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

## Current Development Status
See docs/design/Feature_Tier_List.md for what is V1 scope.
See docs/session_logs/ for recent AI session history.
Prototype phase: [ACTIVE / COMPLETE]. See docs/design/Technical_Design.md
Section 11 for prototype success criteria and current results.

## Performance Contracts
Tick must complete in < 500ms at 2,000 significant NPCs.
Benchmarks are in simulation/tests/benchmarks/.
Do not merge code that regresses benchmarks without explicit approval.

## Coding Standards
- Language: C++ (simulation), [web stack] (UI)
- Formatting: clang-format, config at root .clang-format
- All new simulation code requires unit tests in simulation/tests/unit/
- All new modules require an integration test scenario
- No raw pointers in module code — use smart pointers or pool allocation

## Who to Ask When Unsure
Design questions: Read GDD v0.9 in docs/design/
Architecture questions: Read Technical_Design.md in docs/design/
Scope questions: Read Feature_Tier_List.md in docs/design/
If still unclear: Do not guess. Flag for human review.
```

### Module-Level CLAUDE.md Files

Each module directory has its own `CLAUDE.md` that is read when working inside that module:

```markdown
# Module: npc_behavior

## What This Module Does
Runs the NPC behavior decision loop. Reads world state for all
significant NPCs; for each, evaluates motivations against knowledge
and resources to determine if an action should be queued.

## Interface Spec Location
docs/interfaces/npc_behavior/INTERFACE.md

## Data Structures This Module Owns
simulation/modules/npc_behavior/npc.h — NPC struct (read/write)
simulation/modules/npc_behavior/memory.h — MemoryEntry struct (read/write)
simulation/modules/npc_behavior/behavior_engine.h — main module interface

## Data Structures This Module READS (does not own)
simulation/core/world_state/evidence_tokens.h — read-only this tick
simulation/modules/economy/market_state.h — read-only this tick

## Performance Contract
Full decision loop for 2,000 NPCs: < 100ms
Uses lazy evaluation: only NPCs with state changes since last tick
run the full loop. See INTERFACE.md for lazy eval specification.

## Known Complexity
NPC-to-NPC interactions within a tick are queued, not resolved
immediately. See INTERFACE.md section 4 for the queuing protocol.
This is intentional — resolving intra-tick interactions creates
dependency cycles.

## Tests
simulation/tests/unit/npc_behavior_test.cpp
simulation/tests/scenarios/scenario_journalist_investigates.cpp
simulation/tests/scenarios/scenario_whistleblower_emerges.cpp
```

---

## Part 4 — The Session Context Generator

This is a custom tool (`tools/session_context_generator/`) that is built early in the project and used for every AI coding session. It generates a **context package** — a single markdown file containing everything an AI session needs to work on a specific task.

### What It Generates

Running `generate_context --module npc_behavior --task implement_lazy_eval` produces a file containing:

1. **The root CLAUDE.md** — project-wide rules and status
2. **The module CLAUDE.md** — module-specific context
3. **The interface specification** — from `docs/interfaces/npc_behavior/INTERFACE.md`
4. **The relevant data structure headers** — the `.h` files the module owns and reads
5. **The existing implementation** — the current `.cpp` file being modified (if it exists)
6. **The failing tests** — the specific test cases that must pass after this session
7. **Related module interfaces** — the interfaces of modules this one depends on (headers only, not implementations)
8. **The task definition** — what specifically this session must accomplish
9. **The success criteria** — what must be true for the session to be considered complete

### Why This Tool Exists

Without it, every AI session starts with a human manually assembling context from across the repository — time-consuming, inconsistent, and prone to omission. The context package generator makes context assembly deterministic and repeatable. The human specifies the task; the tool assembles the context; the AI receives it pre-packaged.

### Session Workflow

```
1. Human writes task definition (2–3 sentences)
2. Human runs: generate_context --module [name] --task [task_id]
3. Tool outputs: session_context_[date]_[task_id].md
4. Human reviews context package for completeness
5. Human opens AI session with context package as first input
6. AI session produces: implementation + tests
7. Human reviews output
8. Run: make test && make benchmark
9. If passing: commit + log session in docs/session_logs/
10. If failing: iterate within session or discard and restart with refined context
```

---

## Part 5 — The Build Order

The build order is not determined by what's most interesting to build. It is determined by the dependency graph — what must exist before something else can be built. AI sessions on later modules require the earlier modules to already exist and be tested, because later sessions need to read those interfaces.

### Phase 0 — Foundation (Months 1–2, Human-Led)

This phase is **human-only**. No AI coding sessions until this is complete.

**Deliverables:**
- Repository structure created exactly as specified in Part 2
- Root CLAUDE.md written and committed
- All core data structure header files written (not implemented — just the `.h` files with struct definitions and documented invariants)
- The deterministic RNG module implemented and tested
- The world state container implemented (the object that holds all simulation state and accepts delta buffers)
- The tick orchestrator shell implemented (the loop that calls each module in sequence — no modules implemented yet, all steps are no-ops)
- The CI pipeline: build, test, determinism check, benchmark regression
- The session context generator tool built and working
- The sim runner tool built (can execute N ticks from a seed and dump world state)

**Why human-only:** The architecture decisions made in this phase determine whether every subsequent AI session produces coherent code or not. These decisions cannot be delegated. If the data structures are wrong, every module built on top of them is wrong. This phase is the foundation. It takes two weeks longer than engineers want to spend on it. Spend the two weeks.

**What a human writes in this phase:**
```
All .h files for core data structures
DeterministicRNG — implementation + tests
WorldState container — implementation + tests
TickOrchestrator shell — no-op modules
CI configuration
Tool skeletons (context generator, sim runner)
Root CLAUDE.md
```

### Phase 1 — Simulation Prototype (Months 2–4, AI-Assisted)

The prototype answers the fundamental technical question. AI sessions are small and bounded.

**Build order:**

1. **NPC data structure** — The struct definitions are already in the header from Phase 0. This session implements construction, serialization, and the memory log with decay. Tests: create NPC, add memory entries, verify decay over ticks, verify cap at 500 entries, serialize/deserialize round-trip.

2. **Relationship graph** — Sparse directed graph implementation. Tests: add relationship, query relationship, handle missing relationship (returns default), verify memory efficiency at 2,000 NPCs × 100 relationships.

3. **NPC behavior engine — stub** — A simplified decision loop that reads NPC state and produces queued actions. The stub uses simplified motivation weighting (no full motivation model yet). Tests: NPC with hostile memory queues a hostile action within 7 ticks, NPC with no actionable memories takes no action.

4. **Economy — price model** — Regional spot price with equilibrium and adjustment rate. 5 goods, 2 regions. Tests: price moves toward equilibrium, adjustment rate affects speed, import ceiling caps price, export floor floors price.

5. **Supply chain propagation — stub** — Simple two-layer chain (raw → processed). Tests: upstream shortage reduces downstream output next tick.

6. **Consequence queue** — Data structure and execution engine. Tests: entry fires on correct tick, cancellable entry respects cancellation, queue survives serialization, entries fire after character death.

7. **Tick orchestrator — integration** — Connect the above modules into a running 7-step tick (the rest are stubs). Tests: full tick executes in deterministic order, same seed produces identical output over 100 ticks, tick duration logged per run.

8. **Performance measurement** — The benchmark suite. This is the prototype's primary deliverable. Tests: tick duration at 100 / 500 / 1,000 / 2,000 / 5,000 NPCs. Output: performance report.

Each item above is one or two AI sessions, each with a context package generated by the tool. Total: 10–15 sessions.

**Prototype exits with:** A performance benchmark report and a go/no-go decision on the NPC architecture. If no-go, the architecture memo specifies what must change before Phase 2.

### Phase 2 — Core Simulation (Months 4–12, AI-Assisted)

Build the full 27-step tick with all V1 modules. Build order follows the dependency graph:

**Batch A — No dependencies on other simulation modules:**
- Complete NPC motivation model (depends only on NPC data structure)
- Complete NPC knowledge map (depends only on NPC data structure)
- Evidence token system (depends on NPC data structure)
- Population cohort simulation (independent)
- Deterministic random event system (depends only on RNG)

**Batch B — Depends on Batch A:**
- NPC behavior engine — full (depends on motivation model + knowledge map)
- Supply chain propagation — full (depends on economy price model)
- Community response model (depends on population cohorts + NPC behavior)
- Evidence propagation (depends on evidence tokens + NPC social graph)

**Batch C — Depends on Batch B:**
- Investigator engine (depends on evidence + NPC behavior)
- Obligation network (depends on NPC behavior + relationship graph)
- Consequence threshold detection (depends on all economic and NPC systems)
- Criminal detection risk (depends on evidence + OPSEC systems)

**Batch D — Depends on Batch C:**
- Legal process state machine (depends on investigator engine + evidence)
- Influence network (depends on obligation network + trust model)
- Community opposition formation (depends on community response + NPC behavior)
- Political cycle (depends on influence + community response + NPC behavior)

**Batch E — Full integration:**
- 27-step tick with all modules active
- Cross-module integration tests
- Scenario tests (see Part 6)
- Full performance regression suite

Each batch is run in parallel where dependencies permit. AI sessions within a batch are independent and can run concurrently if multiple engineers are active.

### Phase 3 — Vertical Slice Content (Months 12–24, AI-Assisted)

With the simulation validated, build the V1 content: the economy data (goods, resources, supply chains for the starting nation), the NPC archetypes and their base motivation profiles, the starting world (one nation, 3–5 regions, generated parameters), the career path implementations on top of the simulation.

**Also in this phase:** the scene card system and calendar interface, built as a thin UI layer on top of the simulation.

### Phase 4 — UI and Closed Alpha (Months 24–36)

Full UI layer. Dashboards, map layer, communications layer, scene card art pipeline. Closed alpha at month 24 delivers the playable game to a small external group.

### Phase 5 — Beta and Ship (Months 36–48)

Content completion, balance, performance optimization, certification. Ship V1.

---

## Part 6 — Test Strategy for AI-Generated Code

Tests are not an afterthought. They are the primary mechanism for verifying that AI-generated implementations are correct. Without a strong test suite, AI coding at scale produces code that looks right and is wrong.

### Three Types of Tests, Each with a Different Purpose

**Unit tests** — Does each module do what its interface says? Written before or alongside implementation. AI-generated implementations must pass these before they're accepted. For every module, unit tests cover: normal operation, edge cases, known failure modes, and performance contract.

**Scenario tests** — Does the simulation produce the correct emergent behaviors? These are the most important tests for verifying that the simulation is doing what the design intends. Each scenario is a named seed + starting conditions + N ticks + assertion about world state. Examples:

```
scenario: journalist_investigates_wealthy_player
  setup: player reaches significant_net_worth threshold
  run: 90 ticks
  assert: at least one journalist NPC has opened an investigation
  assert: investigation meter for that journalist > 0.3
  assert: player has no direct evidence of investigation (unless they have law enforcement contacts)

scenario: whistleblower_emerges_after_wage_cut
  setup: player cuts wages 20% below market rate at owned factory
  run: 180 ticks
  assert: at least one worker NPC has satisfaction < 0.3 AND has witnessed financial irregularity
  assert: within 180 ticks, that worker has taken at least one action toward exposure (contacted journalist, approached regulator, or talked to coworker)

scenario: obligation_escalation
  setup: player accepts a favor from politician NPC
  run: 365 ticks (1 in-game year)
  assert: politician has made at least one escalated demand
  assert: demand at tick 365 > demand at tick 90 (escalation is real)
  assert: demand type reflects politician's motivation profile

scenario: drug_market_suppresses_under_law_enforcement_pressure
  setup: player establishes drug distribution network at medium scale
  run: 180 ticks
  assert: narcotics investigator NPC has player as priority target
  assert: player OPSEC score has degraded at least once from investigator actions
  assert: if player has no corrupt law enforcement relationships, arrest probability > 0
```

Scenario tests are written by the design team, not by AI. They encode design intent. Passing scenario tests means the simulation is producing the correct emergent behaviors, not just executing code without errors.

**Determinism tests** — Run the simulation twice with the same seed and verify bit-identical output. Run after every commit. Any non-determinism is a critical failure.

### The Test-First Rule for AI Sessions

Every AI coding session receives the failing tests it must make pass as part of its context package. The session ends when the tests pass. This makes AI sessions convergent — there is a clear, testable definition of done.

AI should not write its own tests from scratch. AI can add test coverage for edge cases it discovers during implementation, but the primary tests are human-written specifications.

### The Regression Safety Net

Before any merge to main:
- All unit tests pass
- All scenario tests pass
- Determinism test passes
- No benchmark regression > 10%

This is enforced by CI. It is not possible to merge code that breaks the safety net. This constraint makes AI coding safe at scale — regressions are caught automatically, not discovered weeks later during integration.

---

## Part 7 — AI Session Templates

These are the standard prompts used to open AI coding sessions. Consistency matters — different session openings produce different behaviors. These templates are stored in `tools/session_templates/` and used by the context generator.

### Template: New Module Implementation

```
You are implementing a new module for the EconLife simulation.

[CONTEXT PACKAGE IS ATTACHED — read it fully before writing any code]

The context package contains:
- Project-wide rules (CLAUDE.md)
- This module's interface specification
- Relevant data structure definitions
- The tests your implementation must pass
- The performance contract your implementation must meet

Your task: [TASK DEFINITION]

Rules:
1. Read the interface spec completely before writing code
2. Your implementation must pass all tests in the context package
3. Do not import from ui/ — only simulation/ modules
4. All random draws must use DeterministicRNG
5. Every new function needs a comment explaining its invariants
6. If something in the interface spec is ambiguous, implement the conservative
   interpretation and add a comment flagging the ambiguity for human review
7. Do not expand scope — implement exactly what the interface specifies

Output: implementation .cpp file + any additional test cases you added
```

### Template: Module Debugging Session

```
A module is failing its tests or producing incorrect simulation behavior.

[CONTEXT PACKAGE IS ATTACHED]

The context package contains:
- The module's interface spec
- The current implementation
- The failing tests with their output
- A world state dump at the tick where behavior diverges from expected

Your task: identify why the tests are failing and propose a fix.

Rules:
1. Do not change the interface specification
2. Do not change the tests unless they are demonstrably wrong (flag for human review)
3. Explain the root cause before proposing a fix
4. If the fix requires changing a data structure shared with other modules,
   flag this for human review instead of implementing it
```

### Template: Performance Optimization Session

```
A module is exceeding its performance contract.

[CONTEXT PACKAGE IS ATTACHED]

The context package contains:
- The module's interface spec and performance contract
- The current implementation
- Benchmark results showing the performance failure
- Profiler output identifying the hot path

Your task: optimize the implementation to meet the performance contract
without changing observable behavior.

Rules:
1. All tests must still pass after optimization
2. Determinism must be preserved
3. Document what was changed and why it improves performance
4. If the performance contract cannot be met without changing the interface,
   document this clearly and halt — do not change the interface unilaterally
```

### Template: Integration Testing Session

```
Two modules that have been implemented independently need to be
tested working together.

[CONTEXT PACKAGE IS ATTACHED]

The context package contains:
- Both modules' interface specs
- Both implementations
- The integration scenario that must pass (from docs/tests/scenarios/)
- The current failing test output

Your task: identify why the integration scenario is failing and propose a fix.
The fix may be in either module or in the scenario setup.

Rules:
1. Do not change either module's interface specification
2. If the failure indicates a design error in the interfaces, flag for human review
3. A scenario test failure may indicate a design assumption that doesn't hold —
   document this for design team review, do not paper over it
```

---

## Part 8 — Managing the Design–Implementation Gap

The most dangerous failure mode in AI-assisted development is **implementation drift** — the code does something slightly different from what the design says, nobody catches it, and it compounds over months until the simulation is producing the wrong emergent behaviors.

### The Interface Spec as Contract

The interface specification is the legal contract between design intent and code. When an AI implementation diverges from the spec, the spec wins. This means the spec must be:

- **Precise enough** that "diverges from" is a clear determination, not a judgment call
- **Maintained** — when design changes, the spec changes first, before implementation changes
- **Reviewed** — a human reads every spec change before the implementation follows it

### Design Review Triggers

Certain events must trigger a design review before the next AI session:

- Any scenario test produces behavior that passes technically but feels wrong in play
- An implementation requires changing a shared data structure
- A performance constraint cannot be met without simplifying the design
- An AI session discovers an ambiguity that could be interpreted multiple ways
- A module interaction produces emergent behavior the scenario tests didn't anticipate

Design reviews are brief — one human reads the flagged issue, makes a decision, updates the spec. The decision is documented in `docs/session_logs/`. What they are not is silent — every design decision made during implementation is recorded.

### The Simulation Observatory

Built during Phase 2, this is a debug tool that lets a human watch the simulation run and observe what NPCs are doing, why, and what evidence they hold. It is not the final UI — it is a developer tool for verifying that the simulation is producing correct emergent behaviors before the real UI is built.

Without this tool, verification of simulation correctness depends entirely on scenario tests, which can only test behaviors someone anticipated. The observatory allows a human to observe unexpected behaviors and decide whether they're bugs or interesting emergence. This distinction cannot be automated.

---

## Part 9 — Human Roles and AI Roles

This is the explicit division of labor. Blurring these lines is where projects using AI coding tools go wrong.

### Humans Own

- All architecture decisions (data structures, module interfaces, dependency graph)
- All interface specification files
- All scenario tests (these encode design intent)
- All design review decisions
- Performance contract negotiation (what is acceptable vs. what must be met)
- The determination of whether simulation behavior is correct or a bug
- Hiring and managing the small team of specialized engineers

### AI Generates

- Module implementations that satisfy human-specified interfaces
- Unit test coverage for edge cases discovered during implementation
- Performance optimization within human-specified constraints
- Debugging analysis and proposed fixes (human approves before merging)
- Boilerplate and structural code (serialization, data structure accessors, etc.)

### What AI Should Never Do

- Modify an interface specification
- Change a data structure that other modules depend on
- Decide that a failing scenario test is wrong without human review
- Expand scope beyond the task definition
- Make architectural decisions by inference when the spec is ambiguous
- Generate code for untested modules without corresponding tests in the context package

---

## Part 10 — The Simulation Prototype as Phase 0 Validation

The prototype (specified in Technical_Design.md Section 11) is not just a technical exercise. It is a test of the entire AI coding methodology before the full project is committed.

During the prototype phase, the team runs the full workflow — context packages, session templates, test-first development, scenario tests, CI pipeline — on a small but real system. By the end of the prototype, the team knows:

- Whether the AI coding workflow produces coherent code at module boundaries
- How long sessions typically take to produce passing implementations
- What the most common failure modes of AI sessions are (usually: insufficient context, ambiguous specs, or missing edge case tests)
- How the CI pipeline performs in practice
- Whether the session context generator produces useful packages or needs refinement

The prototype is not just a technical prototype. It is a methodology prototype. The outputs include not just performance benchmarks but a retrospective on what worked and what needs adjustment in the workflow before Phase 2 begins.

---

## Part 11 — Practical Tooling Decisions

### AI Coding Tool Selection

The primary AI coding tool should be Claude Code or a comparable agentic coding tool that can:
- Read large context packages as a first step
- Access and read multiple files in the repository
- Run tests and iterate on failures within a session
- Operate on a Unix development environment

Copilot-style autocomplete is useful for day-to-day human coding but is not the right tool for bounded-module sessions.

### Version Control Discipline

- Every module implementation lives on a branch named `module/[module_name]`
- Branches are short-lived — merge within one sprint or discard
- Commit messages follow a standard format: `[module_name]: [what changed] — [session_id]`
- Session IDs connect commits to session logs in `docs/session_logs/`
- No force-pushing to main. Ever.

### CI Pipeline

The CI pipeline runs on every commit to any branch and every merge to main:

```
1. Build (fail fast on compilation errors)
2. Unit tests (all modules)
3. Integration scenario tests (all scenarios)
4. Determinism test (100 tick run, compare two seeds)
5. Benchmark regression check (compare against baseline, fail if > 10% regression)
6. Interface compliance check (custom tool: verifies implementations match their spec types)
7. Dependency direction check (custom linter: no simulation/ → ui/ imports)
```

Steps 6 and 7 are custom tools built during Phase 0. They are worth building — they catch the classes of error that AI coding sessions are most likely to produce.

---

## Part 12 — Estimated Session Volume

To calibrate expectations: how many AI sessions does building this game require?

### Phase 1 — Prototype
~15 focused sessions, each 1–2 hours. Primarily implementation sessions.

### Phase 2 — Core Simulation (V1 Modules)
~80–120 sessions across all 27 tick modules, integration work, scenario test debugging, and performance optimization. Average 2–4 sessions per module (initial implementation, edge case fixes, optimization).

### Phase 3 — Content and Career Paths
~60–80 sessions for the V1 content layer, career path implementations, and the scene card system.

### Phase 4 — UI Layer
~50–70 sessions for the full UI (dashboards, map, communications, scene cards).

### Phase 5 — Beta Polish
~30–50 sessions for bug fixes, balance tuning, performance, and certification issues.

**Total: approximately 250–350 AI coding sessions to ship V1.**

At one session per working day, that is roughly 14–18 months of session-level work. With parallel sessions across multiple engineers and multiple AI instances, this compresses significantly. The methodology supports parallelism because modules are independent — two engineers can run sessions on different modules simultaneously without conflict.

---

## Part 13 — What Can Go Wrong and How to Catch It Early

### Architectural Debt from Rushed Specs

**Symptom:** Integration tests fail in unexpected ways after individual module tests pass. Modules that work independently produce wrong behavior when combined.

**Cause:** Interface specifications written too quickly, with implicit assumptions that don't hold at module boundaries.

**Prevention:** Interface spec review by a second human before any implementation session begins. 30 minutes of review prevents weeks of debugging.

**Detection:** Integration scenario tests, run frequently. Don't wait until Phase 2 is complete to run them — run them as each module joins the tick.

### Performance Surprises at Scale

**Symptom:** Prototype benchmarks look fine at 500 NPCs; they collapse at 1,500.

**Cause:** O(N²) operations hidden in the implementation that the NPC count makes visible.

**Prevention:** Performance contracts in the interface spec with explicit complexity targets. Profile early.

**Detection:** Benchmark suite with multiple NPC count levels. Run at each commit.

### Simulation Correctness Drift

**Symptom:** Scenario tests pass but the simulation feels wrong in play — NPCs do technically valid things that aren't what the design intended.

**Cause:** Spec ambiguity resolved by AI in the technically correct but designerly wrong direction.

**Prevention:** Scenario tests that test design intent, not just mechanical correctness. The observatory tool for human observation of simulation behavior.

**Detection:** Regular human play sessions with the observatory open. This cannot be automated — a human must watch the simulation run and ask "is this what we wanted?"

### Context Degradation Over Long Projects

**Symptom:** Later AI sessions produce code that contradicts early architectural decisions, as if the later sessions didn't know about those decisions.

**Cause:** The context packages for later sessions are missing information from early decisions. The session context generator only includes what it knows to include.

**Prevention:** Root CLAUDE.md is a living document — updated every sprint with the most important recent decisions. Critical architectural decisions are added to CLAUDE.md within 48 hours of being made.

**Detection:** Session logs. Review every 10 sessions for recurring ambiguities that suggest CLAUDE.md is missing something.

---

## Summary: The Path to a Shipped Game

The game is buildable with AI coding tools if three things are true:

**1. The architecture is designed by humans first, in full, before any AI writes a line of implementation.** Phase 0 exists for this reason and cannot be shortcut.

**2. Every AI session operates on a complete, human-reviewed context package.** The session context generator exists for this reason and is built before any implementation sessions begin.

**3. The test suite encodes design intent through scenario tests, not just mechanical correctness through unit tests.** Scenario tests are written by the design team and are the primary signal that the simulation is producing the right emergent behaviors.

If all three are in place, EconLife is a 4-year project for a small team (8–15 people at peak) producing a game that would take a traditional team of 80–120 people 7–10 years. The AI coding methodology doesn't just help — it makes the project viable at a scale and timeline that would otherwise be unreachable.

---

*EconLife AI-Coder Development Plan — Companion to GDD v0.9 — Living Document*
