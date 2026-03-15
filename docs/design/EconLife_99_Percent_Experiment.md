# EconLife — The 99% Experiment
*A development methodology where AI does everything except play the game*
*Companion document to GDD v0.9, Feature Tier List, and Technical Design Document*

---

## What This Document Is

The original AI Development Plan described a sensible, conservative approach: humans own architecture, AI owns implementation. A small team of 8–15 people. AI as a force multiplier.

This document asks a different question: **what is the irreducible minimum that a human must do?**

The answer, if the GDD documents are taken seriously, is surprisingly small. The GDD is already a machine-readable specification. The Technical Design Document specifies the architecture. The Feature Tier List defines scope. Together they contain enough precision to derive — not just inform — the interface specifications, the data structures, the scenario tests, and the build order. A human did all of that work already. It's in the documents.

The experiment: **one person, plus AI, ships V1 EconLife.** The human's role is strictly:

1. The creative vision — already captured in the GDD documents. Done.
2. The final felt validation — playing the game and approving it.
3. Legal, financial, and publishing decisions.
4. The go/no-go after the simulation prototype.

Everything between "the documents exist" and "a human plays the finished game" is AI-driven.

This has not been done at this scale. This document is honest about what is proven, what is speculative, and where the experiment could fail.

---

## Part 1 — Why The Previous Plan Was Too Conservative

The original plan put humans in charge of:

- All architecture decisions and interface specifications
- All scenario tests (the tests that encode design intent)
- Design review decisions when AI flags ambiguity
- Playing the simulation to check emergent behavior
- CLAUDE.md maintenance
- Context package assembly
- Session management

Every one of these is justified by a legitimate concern. And every one of them can be challenged.

**Interface specifications written by humans:** The GDD already describes, in prose, exactly what every module must do. Section 3 specifies NPC architecture down to named fields. Section 21 specifies a 27-step tick with each step's purpose. The Technical Design Document specifies data structures with field names and types. A human writing an interface spec from these sources is transcribing — with the risk of introducing transcription errors. An AI reading the source documents and producing an interface spec is doing the same work with no transcription gap and no fatigue.

**Scenario tests written by design team:** The GDD describes specific emergent behaviors: "A journalist NPC opens an investigation when the player reaches significant net worth." "A whistleblower emerges from a workforce whose satisfaction has dropped below threshold." "A political obligation escalates from campaign contribution to criminal exposure over years." These are behavioral assertions. They are already written. They just aren't in test syntax yet. An AI reading them can write the tests.

**Human plays the simulation to check correctness:** This is real. The felt experience of play cannot be automated. But much of what human play sessions discover can be approximated: if the simulation runs 1,000 in-game years across 100 seeds and wealthy players attract investigation at the wrong rate, that's measurable without playing. If NPCs never form opposition movements despite design intent, that's measurable. The irreducible human role is not "watch the simulation run" — it is "does this feel like the game we wanted to make." That check happens once, at the end, not continuously.

**Human reviews AI output before merge:** CI does this. The CI pipeline — build, unit tests, scenario tests, determinism, benchmark regression, interface compliance, dependency direction — is a comprehensive automated review. The only thing human review adds is judgment about things the tests don't catch. The correct response to that is: write better tests, not add human review.

The original plan was optimized for safety. This experiment is optimized for scale and speed, with the simulation itself as the primary safety mechanism.

---

## Part 2 — The Reframing: The GDD Is the Source Code

Normal software projects start with requirements, which humans translate into architecture, which engineers translate into code.

EconLife has already done the first two steps. The GDD is not requirements — it is an architecture specification with a design rationale attached. The Technical Design Document is an implementation specification. Together they are close enough to compilable that the remaining gap — translating documented intent into running code — is exactly what AI coding tools do well.

The experiment reframes the project accordingly:

```
GDD + Technical Design + Feature Tier List
              ↓
         [AI reads]
              ↓
   Interface specs + Data structures
   + Scenario tests + CLAUDE.md files
   + Build dependency graph
              ↓
         [AI builds]
              ↓
        Running simulation
              ↓
    [Statistical validation]
              ↓
         [AI iterates]
              ↓
    Scenario tests passing
    Benchmarks met
    Determinism confirmed
              ↓
         [Human plays]
              ↓
   "Yes this is the game" / "No, here's what's wrong"
              ↓
         [Loop if needed]
              ↓
              SHIP
```

The human appears once in that chain in a substantive role. Everything else is AI-driven.

---

## Part 3 — The Orchestrator

The original plan had a human managing the development pipeline: writing task definitions, running the context generator, reviewing output, deciding what to build next. This is the role the experiment eliminates.

The **Orchestrator** is an AI process — a long-running agentic session, or a session relaunched on a schedule — that manages the development pipeline autonomously.

### What the Orchestrator Does

**Reads the dependency graph** and identifies modules that are ready to build: all their dependencies exist and their tests are passing. Maintains a priority queue ordered by the build order specified in the Technical Design Document.

**Generates context packages** for the next implementation session: reads the relevant GDD sections, the interface spec, the existing code, the failing tests, and the related module interfaces. Assembles them into a session context document.

**Launches implementation sessions** — hands the context package to an implementation AI, receives the output (code + tests), runs CI.

**Handles CI results:** If CI passes, commits the code, updates the session log, moves to the next item. If CI fails, analyzes the failure, decides whether this is a fixable implementation error (retry with a debugging session), a test error (flag for human review), or an architectural issue (flag for human review). Most failures are fixable implementation errors.

**Maintains the CLAUDE.md files**: After each session, updates the root and module CLAUDE.md with decisions made, issues discovered, and current status.

**Produces a weekly summary** for the human: what was built, what's blocked, what decisions were made, what needs human attention. The human reads this and responds if needed. Most weeks they don't need to.

### What the Orchestrator Cannot Do

Make architectural decisions that require design judgment — these are flagged, not decided. The flagged queue is what the human reviews weekly. Most architectural questions during implementation are resolvable by reading the GDD. A small number genuinely require human judgment. The experiment measures how small that number is.

### Orchestrator Implementation

The Orchestrator is itself an AI session, running against a context that includes: the project status file, the dependency graph, the build order, and the CI results from the last cycle. It produces a structured output (next action + generated context package) and that output is executed. It is not a magic autonomous agent — it is a well-contexted AI making one decision at a time, each decision bounded and verifiable.

---

## Part 4 — The Bootstrapper: From Documents to Repository

Before the Orchestrator can manage a development pipeline, the pipeline must exist. The **Bootstrapper** is a one-time process that reads the four companion documents and produces the initial repository state.

### The Bootstrapper Session

One AI session. Input: GDD v0.9, Feature Tier List, Technical Design Document, this document. Output: the complete initial repository state.

Specifically, the Bootstrapper produces:

**1. Repository structure** — the directory tree from Technical Design Part 2, created in full.

**2. All interface specification files** — one per V1 module, derived from:
- The GDD section describing what that module does
- The Technical Design section specifying its data structures
- The Feature Tier List confirming it's in V1 scope

For each module: purpose, inputs (from world state), outputs (to delta buffer), preconditions, postconditions, invariants, failure modes, performance contract, and named test scenarios.

**3. All core data structure headers** — the `.h` files for every data structure in the Technical Design, with documented invariants.

**4. All scenario test stubs** — derived from the GDD's described emergent behaviors. Every statement in the GDD of the form "when X, Y happens" becomes a scenario test. The GDD contains approximately 80–120 such statements in V1 scope. The Bootstrapper writes each one as an executable test with appropriate setup, run length, and assertion.

**5. CLAUDE.md files** — root and per-module, derived from the architecture and design documents.

**6. The build dependency graph** — a structured file listing every V1 module, its dependencies, and its current build status. The Orchestrator reads this file.

**7. The CI configuration** — build script, test runner configuration, determinism test harness, benchmark baseline initialization, dependency direction linter, interface compliance checker.

**8. Stub implementations** — every module gets a no-op stub that compiles and passes the "module exists and compiles" test. This gives CI something to run against from day one.

### Why the Bootstrapper Can Do This

The GDD already contains the information. The Bootstrapper's job is extraction and reformatting, not invention. Interface specs are already written in prose in the Technical Design — the Bootstrapper reformats them into the standard interface spec template. Scenario tests are already written in prose in the GDD — the Bootstrapper translates them into test syntax.

The risk is extraction errors — the Bootstrapper misreads a GDD section and produces a wrong interface spec. This is why the Bootstrapper's output is the one thing the human reviews in detail, before any implementation begins. One review of 20–30 interface specifications and 80–120 scenario test stubs, before the Orchestrator begins its work, is the investment that enables everything downstream to be AI-driven.

Estimated human time: one to two days of reading and approving. This is the primary human contribution to the development process.

---

## Part 5 — The Validation Architecture

The original plan relied on human play sessions to catch the class of error where "the simulation is doing something technically valid but designerly wrong." The experiment must replace this with automated validation, because human play sessions are the bottleneck that limits how autonomous the process can be.

### Three Validation Levels

**Level 1 — Mechanical correctness (fully automated)**
Unit tests and CI. Does the code do what its interface says? Does it compile? Is it deterministic? Does it regress benchmarks? CI handles this entirely.

**Level 2 — Behavioral correctness (partially automated)**
Scenario tests. Does the simulation produce the behaviors the GDD describes? 80–120 named scenarios derived from GDD behavioral statements. These tests are the most important automated validation in the project. If they pass, the simulation is probably producing design-intent behavior.

**Level 3 — Felt correctness (human)**
Does playing the game produce the experience the GDD describes? Is it interesting? Does the emergence feel meaningful rather than mechanical? This cannot be automated. It is the one human role that cannot be delegated.

### Statistical Validation as Level 2.5

Between Level 2 and Level 3, an additional validation layer: **statistical output analysis**. Run the simulation across 100 seeds for 500 in-game years each. Analyze the distribution of outcomes against expected ranges derived from the GDD.

Examples of statistical assertions:
- In regions where the player operates a drug distribution network, law enforcement investigation rates should be elevated (not zero, not 100%).
- Player characters who neglect personal life (decline all personal scene cards) should die earlier on average than those who engage.
- Criminal operations that reach 15%+ regional addiction rates should produce measurable social stability decline.
- Opposition movements should form more frequently in regions where the player has high grievance and low community trust.
- Obligation networks should produce escalating demands in 80%+ of long-running high-obligation relationships.

These are not scenario tests — they're distribution checks. They can be wrong in individually valid simulation runs and still flag systematic design misalignment when aggregate distributions are wrong.

An AI session runs this statistical analysis on each major simulation milestone and compares results to GDD-derived expected ranges. Results outside expected ranges go into the flagged queue for the Orchestrator to investigate.

---

## Part 6 — Handling Ambiguity Without Humans

The original plan's design review process was: AI flags an ambiguity, human makes a decision. The experiment replaces human-in-the-loop with a **GDD arbitration session**.

When the Orchestrator encounters a flagged ambiguity — either from an implementation session or from a failing scenario test that could be interpreted as either a bug or a design gap — it launches a GDD arbitration session.

### GDD Arbitration Session

Input: the ambiguity description, the relevant GDD sections, the Technical Design sections, the scenario test that surfaced the issue, and the current implementation state.

The arbitration session:
1. Reads the GDD to find the design intent
2. Determines whether the issue is a bug (implementation doesn't match spec), a spec gap (the GDD doesn't address this case), or a genuine design ambiguity (the GDD could be read multiple ways)
3. For bugs: routes to a debugging session
4. For spec gaps: proposes a resolution consistent with the GDD's design principles, updates the interface spec, logs the decision
5. For genuine ambiguities: adds to the human review queue with a recommendation

The Orchestrator's weekly summary to the human contains all genuine ambiguities with the arbitration session's recommendations. The human approves, modifies, or overrides. In practice, the GDD is comprehensive enough that genuine ambiguities — cases where even careful reading produces two equally valid interpretations — should be rare. The experiment measures how rare.

---

## Part 7 — The Revised Build Order

The original build order had Phase 0 as human-only. The experiment collapses Phase 0 and the Bootstrapper.

### Phase 0 — Bootstrap (Week 1–2, Human + AI)

**Human does:**
- Reviews Bootstrapper output (interface specs, scenario tests, CLAUDE.md files)
- Approves or modifies — specifically looking for: specs that contradict the GDD, scenario tests that test the wrong thing, architectural decisions that won't scale
- Signs off on the repository state

**AI does:**
- Everything else in Phase 0 (Bootstrapper session produces full initial repository)

**Output:** A repository with all interface specs, all scenario test stubs, all data structure headers, all CLAUDE.md files, CI pipeline configured. Ready for the Orchestrator.

### Phase 1 — Simulation Prototype (Weeks 3–10, Orchestrator-Driven)

The Orchestrator builds the prototype modules in dependency order. Same scope as the original plan, but autonomous: NPC data structure, relationship graph, behavior engine stub, price model, supply chain stub, consequence queue, tick orchestrator integration, benchmark suite.

**Human checkpoint:** Reviews the prototype benchmark results (go/no-go decision on architecture). This is the second significant human intervention. If the benchmark results are in range, the Orchestrator proceeds. If not, the human makes the architectural simplification decision and the Bootstrapper is re-run for affected modules.

### Phase 2 — Core Simulation (Months 3–12, Orchestrator-Driven)

All 27 V1 tick modules built in dependency batches. Orchestrator runs continuously, building and integrating. Human reviews the weekly summary, approves flagged decisions, plays the simulation observatory when statistical validation flags anomalies.

**Milestone at Month 6:** Human plays the simulation with the observatory tool for one session. Not looking for completeness — looking for whether emergent behavior is recognizably pointing in the right direction. A half-day commitment.

**Milestone at Month 12:** Human plays a playable build of the core loop for one to two days. This is the first real felt-correctness check. Issues found here feed back into the Orchestrator's queue as prioritized fixes.

### Phase 3 — Content and UI (Months 12–24, Orchestrator-Driven)

Scene card content, career path implementations, UI layer. The Orchestrator continues managing the pipeline. The UI is primarily the scene card system and the three interface layers (map, operational, communications) — all of which are specified in the GDD with enough precision for the Bootstrapper-derived specs to cover them.

**Scene card art:** This is genuinely outside the Orchestrator's scope. Scene card settings (boardroom, parking garage, etc.) require visual assets. These can be AI-generated from text prompts — "photorealistic two figures across a glass conference table, natural light, venetian blinds, formal atmosphere" is a tractable generation prompt. An AI visual generation pipeline produces a library of setting images. The human reviews and approves the library in a single session.

### Phase 4 — Alpha Testing (Months 24–30, Human Re-enters)

The human plays the game. Seriously, extensively. At this point the simulation is complete, the UI is functional, the content exists. The human is not reviewing code — they are playing a game and noting what feels wrong.

Every piece of feedback produces an Orchestrator task: "the journalist investigation system is activating too quickly for new players" becomes a scenario test parameter change and a statistical validation check. "The obligation escalation feels mechanical rather than organic" becomes a GDD arbitration session that may result in tuning the motivation model.

The Orchestrator handles the iteration. The human provides the play experience assessments.

### Phase 5 — Ship (Months 30–42)

Beta, certification, publishing. This phase involves legal, financial, and publishing decisions that are entirely human. The Orchestrator continues handling technical issues.

---

## Part 8 — What the Experiment Actually Requires

To run this experiment, the human must invest upfront in three things that enable everything downstream.

**1. The documents must be complete and precise.** The GDD, Technical Design, and Feature Tier List must be precise enough that an AI reading them can derive correct interface specifications. The current versions are close. They need one more review pass specifically asking: "Is this statement precise enough that an AI could generate an unambiguous interface specification from it?" Vague statements need to be tightened before the Bootstrapper runs.

**2. The Bootstrapper's output must be reviewed before implementation begins.** This is the highest-leverage human investment in the project. Spending two days reviewing Bootstrapper-generated interface specs and scenario tests prevents months of wrong implementations. This is not optional.

**3. The CI pipeline must be comprehensive and trusted.** The experiment's safety net is CI. If CI passes, code merges. The human doesn't review individual commits. This means CI must be comprehensive enough that passing it is genuinely sufficient. The interface compliance checker and dependency direction linter from the original plan become mandatory, not optional. Statistical validation must be automated and running on every milestone build.

---

## Part 9 — The Irreducible 1%

This is what the human actually does, expressed as a time estimate:

| Activity | Estimated time |
|---|---|
| Creative vision and GDD authorship | Already done |
| Document precision review (pre-Bootstrapper) | 3 days |
| Bootstrapper output review and approval | 2 days |
| Prototype go/no-go decision | 4 hours |
| Month 6 observatory session | 4 hours |
| Month 12 playable build review | 2 days |
| Weekly Orchestrator summary reviews | ~30 min/week × 100 weeks = ~50 hours |
| Alpha play testing and feedback | 2–3 weeks |
| Publishing, legal, financial decisions | Ongoing, low time |
| Final "yes, ship this" decision | 1 day |
| **Total estimated human time (active development)** | **~6–8 weeks spread over 3 years** |

The other 142+ weeks of development work is AI-driven.

---

## Part 10 — What Could Fail

This experiment has not been run. These are the honest failure modes.

### Failure Mode 1 — The GDD Isn't Precise Enough
**Risk:** The Bootstrapper produces interface specs with systematic gaps or errors because the GDD contains prose ambiguities that look clear to a human but resolve incorrectly when formalized.

**Detection:** The human review of Bootstrapper output catches this before implementation begins.

**Recovery:** Human tightens the relevant GDD sections, Bootstrapper re-runs on affected modules. This adds days, not months.

**Probability:** Medium. The GDD is unusually precise for a design document but was written as prose, not as a specification language. Some sections will need tightening.

### Failure Mode 2 — Orchestrator Loses Architectural Coherence
**Risk:** Over 300+ sessions spanning months, the Orchestrator makes local decisions that are individually reasonable but collectively inconsistent — accumulating architectural drift that no single session can see.

**Detection:** Scenario tests begin failing in unexpected combinations. The statistical validation distributions drift from expected ranges.

**Recovery:** An architectural coherence session — an AI session that reads the entire codebase and all interface specs and produces an inconsistency report. The human reviews and triages. This is expensive but recoverable.

**Mitigation:** The CLAUDE.md maintenance discipline. Every architectural decision is logged in CLAUDE.md within the session that made it. The Orchestrator's context always includes the full CLAUDE.md history. This makes architectural drift visible rather than invisible.

**Probability:** Medium-high at 300+ sessions. The mitigation makes it detectable; recovery makes it non-fatal.

### Failure Mode 3 — Scenario Tests Don't Capture Design Intent
**Risk:** The scenario tests pass, the statistical distributions look right, the human plays the game and it feels wrong in a way none of the automated validation caught.

**Detection:** Alpha play testing.

**Recovery:** Human articulates what's wrong, the Orchestrator writes new scenario tests that encode the missing intent, iterates until the new tests pass.

**This is actually the expected path, not a failure.** Alpha play testing is designed to find exactly this. The question is whether the gap is small (a few weeks of iteration) or large (months of redesign). The GDD's precision reduces the probability of large gaps.

**Probability of small gaps:** High. **Probability of large gaps:** Low, given the GDD quality.

### Failure Mode 4 — Performance Target Unreachable
**Risk:** The prototype benchmark results fail, and the architectural simplification required to meet performance targets requires rethinking module interfaces that the Bootstrapper already generated.

**Recovery:** Human makes the simplification decision. Orchestrator re-bootstraps affected modules. This is the prototype's purpose — finding this in month 2 rather than month 18.

**Probability:** Medium (this is flagged as a known risk in the Technical Design).

### Failure Mode 5 — The Orchestrator Gets Stuck
**Risk:** The Orchestrator encounters a failure it can't categorize — not a clean implementation error, not a clear spec gap — and halts waiting for human review while the human doesn't notice.

**Mitigation:** The weekly summary always includes the Orchestrator's current state and any items waiting for human attention. If the human doesn't respond within two weeks, the Orchestrator sends an escalation.

**Probability:** Low. The categorization logic is bounded (bug / spec gap / ambiguity), and the human check-in cadence is weekly.

---

## Part 11 — What This Experiment Proves (If It Works)

If EconLife ships under this methodology, it proves something genuinely new:

**A comprehensive design document is sufficient input for AI to build the system it describes**, given:
- A one-time human review of AI-derived specifications before implementation
- A CI pipeline comprehensive enough that passing it is meaningful
- A simulation whose deterministic outputs can serve as the validation mechanism
- A human who plays the finished game

This is not "AI helps developers." This is "developers produce documents; AI produces software from documents." The human role shifts from building to specifying and validating. The GDD is no longer a communication artifact between the designer and the engineering team — it is the engineering artifact that the engineering team (AI) reads directly.

If the experiment works at EconLife's scope, the methodology generalizes to any project whose design can be documented with similar precision. That is the larger implication.

---

## Part 12 — Before You Run This Experiment

Four things need to happen before the Bootstrapper runs.

**1. Tighten the GDD.** Read every section specifically looking for statements that could be interpreted two ways. Resolve them. The GDD should be precise enough that two different people reading the same section independently would produce the same interface specification.

**2. Annotate the GDD with behavioral assertions.** Every statement of the form "when X, Y happens" should be marked with a tag (e.g., `[SCENARIO]`) so the Bootstrapper knows to turn it into a scenario test. Do a pass of all four documents marking these. There are probably 80–120 in the GDD alone.

**3. Build the Bootstrapper prompt.** A precise, structured prompt that tells the Bootstrapper what to produce, in what format, for each output type. The prompt is itself a specification document. It should include the interface spec template, the CLAUDE.md templates, the scenario test format, and examples of each.

**4. Decide the tech stack definitively.** The Bootstrapper generates `.h` files and CI configuration. It needs to know the language (C++ for simulation, confirmed), the build system (CMake? Meson? decide now), the test framework (Catch2? GoogleTest? decide now), and the UI stack. These cannot be decided later without re-bootstrapping.

When these four things are done, the experiment is ready to run.

---

*EconLife — The 99% Experiment — Living Document*
*This document is speculative. It describes an approach that has not been validated at this scale. The experiment will produce data; this document is the hypothesis.*
