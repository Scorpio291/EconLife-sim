# EconLife — Technical Design Document v29 — Pass 5 Session 6 Complete
*Pass 5 Session 2 (Production Readiness Audit — TDD BLOCKER fixes: B1–B5)*
*Pass 5 Session 6 (Precision, Inconsistency, and Scope fixes: TDD-P1 through TDD-SC2) — 9 targeted fixes complete*
*Pass 5 Session 9 (Scenario Tests) applied: 3 TDD scenario stubs added (TDD-S1 Transit Disruption Supply Gap, TDD-S2 Board Rejection and Compensation Deferral, TDD-S3 Expansion Team Interception Below Minimum)*
*Companion documents: GDD v1.7, Feature Tier List, Commodities & Factories v2.3, R&D & Technology v2.2, World Map & Geography v1.1*
*Living document — updated in sync with GDD and Feature Tier List*
*Pass 2 complete; Pass 3 Sessions 1–4 complete; Pass 3 Session 6 (Moddability) complete; Pass 3 Session 7 (Architecture: Packages, Parallelism, Transit, Persistence) complete; Pass 3 Session 8 (Physics Consistency Pass) complete; Pass 3 Session 9 (Structural Fixes) complete; Pass 3 Session 10 (Missing Struct Definitions) complete; Pass 3 Session 11 (Precision Gap Completions) complete; Pass 3 Session 12 (Mechanical Fixes) complete; Pass 3 Session 13 (Universal Facility Signals and Scrutiny System) complete; Pass 3 Session 14 (Grey Zone Compliance and Universal Community Grievance) complete; Pass 3 Session 15 (Player-Special Fixes) complete; Pass 3 Session 16 (Constants Homing) complete; Pass 3 Session 17 (Technology Lifecycle Integration) complete; Pass 3 Session 18 (Cross-Doc Audit Fixes: commercialize_technology action, farm runtime fields, permafrost dual-gate) complete; Pass 3 Session 19 complete; Pass 3 Session 20 (Economy Consolidation and Universal OPSEC) complete; Pass 3 Session 21 (Missing Struct Definitions and Blocker Fixes) complete; Pass 3 Session 22 (WorldGen V1 Scope: H3Index, ProvinceLink, WorldGenParameters) complete; Pass 3 Session 23 (V1 Completeness Pass — 16-issue audit resolution: Legal Process, Financial Markets, Random Events structs, Healthcare, Addiction, Currency, Media, Movement) complete — Bootstrapper ready; Pass 4 Session 1 (Gap Audit Fixes: S-2 Player Business Ownership, P-3 Profit Transfer, S-3 PopulationCohort/RegionCohortStats, BLOCKER-D Labor Market and Hiring System) complete; Pass 4 Session 2 (Executive Compensation realistic model, BLOCKER-G Multi-Level Government Budget System) complete; Pass 4 Session 3 (BLOCKER-A Money Laundering System, EvidenceType enum formal declaration) complete; Pass 4 Session 4 (BLOCKER-B Real Estate Market, BLOCKER-C Banking/Loan System, BLOCKER-E Alternative Identity, BLOCKER-F Informant System, BLOCKER-H Protection Rackets, BLOCKER-I Weapons Trafficking) complete — all audit BLOCKERs resolved; Pass 4 Session 5 (P-1 Drug Economy Type System, P-5 Antitrust Trigger Model, P-6 Criminal R&D / Designer Drug Scheduling Race, P-7 Commodities Trading Player Action, S-1 NPC-to-NPC Obligation Scope, Ab-1 through Ab-6 explicit design decisions) complete — all audit items resolved; Pass 4 Session 6 (§44 Seasonal Agricultural Production — Ab-4 reinstated as V1; CropCategory enum, SeasonPhase enum, FarmSeasonState struct, growing-season calendar by KoppenZone × CropCategory, per-tick harvest-cycle integration, cosine seasonal modifier for perennial crops, two scenario tests added) complete*

---

## Purpose

This document specifies the technical architecture required to build what the GDD describes. It is written for engineers, not designers. Where the GDD says what the simulation does, this document says how. Sections marked **[PROTOTYPE]** identify components that must be validated before full production begins. Sections marked **[RISK]** identify known technical unknowns. Sections marked **[V1]** are in scope for the simulation prototype and V1 build. Sections marked **[EX]** are included for architectural continuity but are not implemented until the relevant expansion. Sections marked **[CORE]** are architectural foundations shared across all tiers. The Bootstrapper generates interface specs only for **[V1]** and **[CORE]** sections.

---

## 1. The Fundamental Technical Question

Before anything else, the following question must be answered by the simulation prototype:

> **Can we run 2,000–3,000 significant NPCs per region with full memory, motivation, and relationship models on a one-minute real-time tick without performance degradation?**

The entire design rests on the NPC architecture. CK3 runs ~5,000 characters at significantly lower model complexity. Victoria 3 runs population groups rather than individuals. EconLife proposes individual agents with memory logs, motivation weighting, and relationship graphs. This has not been shipped at this scope.

The prototype answers this question. Everything in this document is contingent on that answer. If the answer requires simplification, the simplification gets designed into the architecture before content production begins.

---

## 2. Architecture Overview and Tech Stack Decision

EconLife is a **deterministic agent-based simulation** with a **React-style UI layer** sitting on top of it. The simulation runs headless and would produce the same outputs given the same seed and inputs regardless of whether the UI is attached. The UI observes simulation state and presents it. The player interacts with the UI; the UI translates player actions into simulation inputs.

```
┌─────────────────────────────────────────┐
│              PLAYER INPUT               │
│   (scene card choices, calendar, etc.)  │
└───────────────────┬─────────────────────┘
                    │
┌───────────────────▼─────────────────────┐
│              UI / PRESENTATION          │
│  Map layer | Dashboards | Scene cards   │
│  Calendar | Communications | Facility   │
└───────────────────┬─────────────────────┘
                    │  reads state / writes inputs
┌───────────────────▼─────────────────────┐
│           SIMULATION CORE               │
│   Daily tick (province-parallel)        │
│   NPC engine | Economy engine           │
│   Evidence engine | DeferredWorkQueue   │
└───────────────────┬─────────────────────┘
                    │
┌───────────────────▼─────────────────────┐
│         PACKAGE SYSTEM [§2a]            │
│  PackageManager | TickOrchestrator      │
│  ScriptEngine (Lua) | MigrationRegistry │
│  Loaded once at startup — immutable     │
└───────────────────┬─────────────────────┘
                    │
┌───────────────────▼─────────────────────┐
│        PERSISTENCE LAYER [§22]          │
│  Continuous autosave | WAL segments     │
│  Monthly snapshots | Schema versioning  │
│  Ironman / Standard mode               │
└─────────────────────────────────────────┘
```

### Tech Stack — Decided

These decisions are locked. The Bootstrapper generates build configuration based on these. Changing them requires a full re-bootstrap.

**Simulation core language:** C++20. Required for performance at NPC scale.

**Build system:** CMake 3.25+. Standard for C++ cross-platform projects; well-supported tooling.

**Test framework:** Catch2 v3. Header-only where needed, good BDD-style syntax for scenario tests, integrates cleanly with CMake's CTest.

**UI layer:** Electron with TypeScript + React. Allows rapid UI iteration, ships on Windows/Mac/Linux without separate builds, can access native filesystem for autosave. The simulation core exposes a C API; the Electron main process communicates with it via IPC.

**Serialization:** Protocol Buffers (protobuf) for world state snapshots. Binary format, fast, schema-versioned (required for save compatibility across versions). WAL entries use the same schema.

**Compression:** LZ4 for snapshot compression. Fast decompression is more important than compression ratio for load times.

**CI system:** GitHub Actions. YAML-based, integrates with CMake and Catch2 CTest, supports matrix builds.

**Dependency direction enforcement:** A custom CMake rule that fails the build if any `simulation/` target lists `ui/` as a dependency. This is enforced at the CMake level, not as a linter. Violation = compile error.

### Persistence Architecture Constraint: Consequence-Aware Restoration Only

The persistence layer exposes two game modes (see §22). In **Ironman** mode, no restoration API is reachable from the UI. In **Standard** mode, restoration is available only through `PersistenceModule::restore_with_consequences()` — a path that validates mode, applies tiered disruption consequences, and loads the snapshot. Bare snapshot restoration (`restore_raw()`) is a private method and never callable from outside the persistence module.

The CI pipeline includes a test that scans the codebase for any call to `restore_raw()` from outside `PersistenceModule`. Any such call fails the build. The consequence-aware path is the only legal external restoration call. This preserves the causality-over-content design principle: restoration is not undone — it is a world event with consequences.

---

## 2a. Package System [CORE]

All content and behavior extensions — whether first-party expansions or community mods — use a single unified system. The distinction between an expansion and a mod is **trust level and support contract**, not capability. Both can ship goods CSVs, recipe CSVs, compiled tick modules, Lua scripts, migration functions, and config overrides. Expansions are studio-authored and officially tested. Mods are community-authored and may cause instability the studio has not anticipated. The player is warned when mods are enabled.

### PackageType

```cpp
enum class PackageType : uint8_t {
    base_game  = 0,  // Engine core. Always loaded first. Ships with the binary.
                     // Never appears as a package folder — it is the engine itself.
    expansion  = 1,  // Studio-authored. Officially tested. Compatibility guaranteed
                     // across game versions. Crash = studio bug to fix.
    mod        = 2,  // Community-authored. May include anything an expansion can.
                     // Not tested by the studio. May cause instability.
                     // Player accepts risk at enable time. Crash = mod author's bug.
                     // Crash reports label mod-sourced modules for attribution.
};
```

### Package Manifest — `package.json`

All packages (expansions and mods) use the same manifest format:

```json
{
  "package_id":          "trafficking_expansion",
  "display_name":        "EconLife: Shadows",
  "version":             "1.0.0",
  "type":                "expansion",
  "author":              "Studio",
  "load_after":          ["base_game"],
  "load_before":         [],
  "schema_version_bump": 2,
  "scripts": [
    { "file": "scripts/trafficking_prices.lua", "type": "behavior_hook", "budget": "normal" }
  ],
  "description":         "Adds trafficking supply chain, new NPC roles, new consequence types."
}
```

```json
{
  "package_id":          "advanced_automotive",
  "display_name":        "Advanced Automotive Mod",
  "version":             "1.0.0",
  "type":                "mod",
  "author":              "modder_name",
  "load_after":          ["base_game"],
  "load_before":         [],
  "schema_version_bump": 0,
  "scripts": [
    { "file": "scripts/custom_ev_pricing.lua", "type": "behavior_hook", "budget": "normal" }
  ],
  "description":         "Adds luxury and electric vehicle supply chains."
}
```

**`load_before` symmetry:** Complements `load_after`. Used when an expansion must precede an optional bridge mod that integrates its content into another package. Both are expressed as arrays of `package_id` strings.

**`schema_version_bump`:** For expansions that add WorldState fields requiring a migration. 0 = no migration needed. Non-zero = must register a migration function during `load_all()`.

**`scripts` budget values:** `"normal"` = 0.1ms per hook invocation. `"extended"` = 0.5ms for hooks doing heavier computation — must be declared explicitly so the engine reserves budget.

### Lua Behavior Hook Interface

Hook type: `"behavior_hook"`

Entry point: A Lua function named `on_tick` at the top level of the script file.

**Invocation signature:**
```lua
function on_tick(ctx)
-- ctx fields (read-only):
-- ctx.province_id  : integer   -- province being processed this tick
-- ctx.tick         : integer   -- current simulation tick number
-- ctx.good_prices  : table     -- { [good_key_string] = spot_price_float }
--                              -- read-only snapshot of formal market prices this tick
-- ctx.era          : integer   -- current SimulationEra value (0–4)
```

**Return value:**
- Return `nil` (or nothing) to apply no effect.
- Return a table of delta descriptors to apply effects:

```lua
{ type = "price_modifier", good_key = "steel", modifier = 1.05 }
{ type = "npc_memory", npc_id = 1234, weight = -0.1, label = "custom_event" }
```

Unrecognized delta types are ignored with a logged warning.

**Budget enforcement:**
- `"normal"` hooks: 0.1ms budget per invocation. Exceeded = hook disabled for session.
- `"extended"` hooks: 0.5ms budget per invocation. Must declare in `package.json`.
- Budget is measured per province per tick (`on_tick` is called once per LOD 0 province per tick).

**[V1 constraint]** The delta type list is intentionally minimal. Additional delta types will be added as the public API expands. V1 hooks are suitable for price modifiers and NPC memory injection only. Compiled tick modules (`ITickModule`) have full `DeltaBuffer` access for more complex effects.

### Package Directory Structure

```
/packages/                         ← studio expansions (preinstalled)
  base_game/
    package.json                   ← type: "base_game"; load_after: []
    goods/
      goods_tier1.csv
      goods_tier2.csv
    recipes/
    facilities/
      facility_types.csv
    config/
      simulation_config.json
      climate_config.json
      economy.json
      transport.json
      investigation.json
      resources.json

  trafficking_expansion/
    package.json                   ← type: "expansion"
    goods/
      trafficking_goods.csv
    recipes/
    facilities/
    modules/
      trafficking_network.so       ← compiled tick module (C++ against public headers)
      trafficking_npc.so
    scripts/
      trafficking_prices.lua       ← Lua behavior hook
    migrations/
      v1_to_v2.json                ← migration descriptor
    config/
      simulation_config.json       ← only overridden keys; merged, not replaced

/mods/                             ← user-installed community packages
  advanced_automotive/
    package.json                   ← type: "mod"
    goods/
    recipes/
    scripts/
      custom_ev_pricing.lua        ← same capability as expansion scripts
    overrides/
      recipes/
        steel_mill.json            ← base game recipe override; same key = replacement
```

**Config merge rule:** A package's `config/*.json` files are merged on top of the active config at load time. Only keys present in the package file are overridden. Keys absent from the package config retain their previously loaded value. Last-loaded package wins on conflict — same rule as key collision resolution.

**Public API headers:** The engine ships a stable set of public C++ headers (`econlife_public_api/`) exposing `ITickModule`, read-only WorldState accessors, and DeltaBuffer write methods. Both compiled expansion modules and community mod modules compile against these headers. Internal engine headers are never exposed. What cannot be done within the public API needs a new hook point in the API — not a sandbox bypass.

### PackageManager

```cpp
class PackageManager {
public:
    // === Initialization (called once at startup, in order) ===

    // Scans /packages/ and /mods/ directories. Reads all package.json manifests.
    // Does not load any content yet.
    void discover_packages();

    // Topological sort on load_after/load_before constraints across all packages.
    // Detects cycles and missing dependencies. Panics at startup with named cycle on failure.
    // Returns sorted load order: e.g. ["base_game", "trafficking_expansion", "advanced_auto"]
    std::vector<std::string> resolve_load_order();

    // Loads all packages in resolved order. For each package:
    //   1. Loads content files (goods, recipes, facilities) → content registries
    //   2. Loads compiled modules (.so/.dll) → orchestrator.register_module() per module
    //   3. Loads Lua scripts → script_engine.register_hook() per script entry
    //   4. Registers migration functions (if schema_version_bump > 0) → migrations
    //   5. Merges config files onto active config
    //   6. Validates key uniqueness; logs conflicts per conflict resolution rules
    // After all packages processed: calls orchestrator.finalize_registration()
    void load_all(TickOrchestrator& orchestrator,
                  ScriptEngine&     script_engine,
                  MigrationRegistry& migrations);

    // === Queries (available after load_all) ===
    bool        is_loaded(std::string_view package_id) const;
    PackageType package_type(std::string_view package_id) const;
    std::vector<std::string> loaded_package_ids() const;   // in load order

    // Content registries — populated during load_all:
    const GoodRegistry&          goods()      const;
    const RecipeRegistry&        recipes()    const;
    const FacilityTypeRegistry&  facilities() const;
};
```

### ITickModule Interface

```cpp
enum class ModuleScope : uint8_t {
    core = 0,   // Always runs. Architectural foundation.
    v1   = 1,   // Base game. Registered by base_game package at startup.
    ex   = 2,   // Expansion or mod. Registered by its package during load_all().
};

class ITickModule {
public:
    virtual ~ITickModule() = default;

    virtual std::string_view name()       const noexcept = 0;
    virtual std::string_view package_id() const noexcept = 0;   // must match package.json
    virtual ModuleScope      scope()      const noexcept { return ModuleScope::v1; }

    // Fine-grained ordering within the resolved package graph.
    // Reference module names from any already-loaded package.
    // PackageManager validates all named dependencies exist after load_all().
    virtual std::vector<std::string_view> runs_after()  const { return {}; }
    virtual std::vector<std::string_view> runs_before() const { return {}; }

    virtual void execute(const WorldState& state, DeltaBuffer& delta) = 0;
};
```

### TickOrchestrator

```cpp
class TickOrchestrator {
public:
    // Called internally by PackageManager::load_all() for each compiled module.
    // Not part of public API — only PackageManager calls this.
    void register_module(std::unique_ptr<ITickModule> module);

    // Called by PackageManager::load_all() after all packages are processed.
    // Runs topological sort (Kahn's algorithm) on modules using runs_after/runs_before.
    // Validates all named dependencies exist. Panics with named cycle if cycle detected.
    // Locks module list — subsequent register_module() calls are rejected.
    void finalize_registration();

    // Main tick entry point. Asserts finalize_registration() was called.
    // See §3 for full parallel execution model.
    void execute_tick(WorldState& state, ThreadPool& thread_pool);

private:
    std::vector<std::unique_ptr<ITickModule>> modules_;
    bool finalized_ = false;

    void resolve_and_sort();  // Kahn's algorithm + cycle detection; called by finalize_registration()
};
```

**Cycle detection:** If Module A declares `runs_after: ["module_b"]` and Module B declares `runs_after: ["module_a"]`, `resolve_and_sort()` panics at startup with a descriptive error naming the cycle. This is a developer error — it must never reach production.

**Schema downgrade guard:** On save load, if `loaded_schema_version > CURRENT_SCHEMA_VERSION`, `PersistenceModule` returns `LoadResult::schema_too_new` and refuses to load. The UI displays: *"This save requires a newer version of the game."*

### Scripting Layer (Lua 5.4)

The engine embeds Lua 5.4. All packages (expansions and mods) can register Lua scripts alongside or instead of compiled modules. The studio uses the scripting layer internally for rapid prototyping and lighter behavior extensions — the same API modders use.

**Hook types available to scripts:**

```lua
-- PRE/POST hooks around named tick steps:
hooks.on_before("economy.price_update",    function(state) ... end)
hooks.on_after("npc.behavior_generation",  function(state, delta) ... end)

-- Consequence hooks — fire when a specific consequence type is queued:
hooks.on_consequence(ConsequenceType.investigation_opens, function(entry, state) ... end)

-- Market hooks — price resolution for specific goods:
hooks.on_price_resolve("trafficking_expansion:heroin", function(market, state) ... end)

-- NPC decision hooks — behavior generation for specific NPC roles:
hooks.on_npc_decision(NPCRole.criminal_operator, function(npc, state, delta) ... end)

-- Facility tick hooks — each tick for specific facility types:
hooks.on_facility_tick("trafficking_expansion:processing_lab",
                        function(facility, state, delta) ... end)
```

**Script constraints:**
- Scripts receive `WorldState` as read-only. Writes go through a sandboxed subset of `DeltaBuffer`.
- No cross-script calls within a tick — avoids ordering dependency between scripts.
- No filesystem access, no network calls, no arbitrary C function calls.
- Per-hook CPU budget enforced: `normal` = 0.1ms, `extended` = 0.5ms wall time.
- Three consecutive budget overruns: hook disabled for session. Mod-sourced: player notification. Studio-sourced: dev error log.

**Sandbox applies equally to studio and mod scripts.** If a desired behavior cannot be expressed within the scripting sandbox, that is a signal to add a new hook point to the public API — not to widen the sandbox.

### Mod Error Boundary

Compiled modules from community mods run inside a structured exception boundary:

```cpp
// In TickOrchestrator::execute_tick(), for each module:
try {
    module->execute(state, delta);
} catch (const std::exception& e) {
    if (package_manager.package_type(module->package_id()) == PackageType::mod) {
        // Log: mod name, module name, exception, current tick
        // Disable module for remainder of session
        // Notify player: "Mod [name] encountered an error and was disabled this session"
        // Continue tick with remaining modules
    } else {
        // base_game or expansion failure — unrecoverable; propagate and crash
        throw;
    }
}
```

Crash reports include the full load order and all active package IDs, so players can report to mod authors, not the studio.

### Initialization Sequence

```
Startup:
  1. PackageManager::discover_packages()
       → scan /packages/ and /mods/
       → read all package.json manifests
       → no content loaded yet

  2. PackageManager::resolve_load_order()
       → topological sort on load_after/load_before graph
       → validate: no cycles, no missing load_after targets
       → panic on violation with named cycle / missing dependency
       → return sorted order: ["base_game", "trafficking_expansion", ...]

  3. PackageManager::load_all(orchestrator, script_engine, migrations)
       → for each package in resolved order:
           a. load content files → content registries; validate key uniqueness
           b. load compiled modules → orchestrator.register_module() per module
           c. load Lua scripts → script_engine.register_hook() per entry
           d. if schema_version_bump > 0 → migrations.register_migration()
           e. merge config files onto active config
       → orchestrator.finalize_registration()
            → resolve_and_sort() on modules (Kahn's + cycle detection)
            → lock module list

  4. MigrationRegistry::migrate(world_state, loaded_schema_version)
       → apply migrations in version order to loaded save

  5. First tick executes.
```

Everything loads before tick 1. No dynamic package loading mid-run. The module and script lists are immutable once the game starts.

---

## 3. The Simulation Tick [CORE]

The world runs on a **daily tick**. One tick = one in-game day = one real-time minute at normal speed (30x fast-forward = one tick per two real seconds).

The tick executes the 27 steps defined in GDD Section 21. Steps are ordered to respect causality: production before prices, prices before financial distribution, NPC behavior after financial state is updated, consequence thresholds checked after NPC behavior. Most steps operate only on items that have changed state since the previous tick or whose deferred work is due this tick — not all items unconditionally.

### Tick Budget

**Hard requirement:** At 30x fast-forward, a tick must complete in under 2,000ms. A tick exceeding 2,000ms causes fast-forward to fall behind real time.

**Target (parallel):** Tick completes in under **200ms at 2,000 significant NPCs on 6 cores.** With province-parallel execution (see §3 Province-Parallel Execution) and event-driven processing, the dominant NPC behavior step drops from ~300ms single-threaded to ~50ms. The combined architecture brings the realistic tick duration well below the prior 500ms single-threaded estimate.

**Prototype benchmark matrix:** NPC count × core count (1, 2, 4, 6, 8 cores). Target: 200ms at 2,000 NPCs on 6 cores. Acceptable: 500ms. Failing: 1,000ms. The prototype must validate parallel execution — not single-threaded only.

**There is no separate normal-speed budget.** At 1x speed the tick is throttled to one per real-time minute. All performance engineering targets 30x fast-forward.

### Design Principle: Event-Driven Processing

**If work does not need to happen this tick, it goes into the `DeferredWorkQueue` with a `due_tick`.** This single rule replaces per-tick polling across most systems:

- NPC relationship decay: batch every 30 ticks per NPC; 30 ticks of decay in one operation. Cost: 1/30th of daily polling.
- Evidence actionability decay: batch every 7 ticks per token.
- NPC business decisions: scheduled in the queue at decision time; zero per-tick cost between decisions.
- Market recomputation: triggered only when supply or demand changes (production step, shipment arrival). Not run for static markets.
- Investigator meter updates: triggered when facility signal composite changes (any facility, criminal or legitimate); not unconditionally each tick.
- Climate downstream effects (agricultural modifiers, community stress): batch every 7 ticks.
- Transit arrivals: scheduled at dispatch; arrive at `due_tick` with zero in-transit cost.
- Interception checks: one check per tick per criminal shipment in transit, spread across the transit duration rather than concentrated at arrival.

The consequence queue already embodies this principle. All other deferred work uses the same underlying `DeferredWorkQueue` mechanism (see §3.3).

### Province-Parallel Execution

The simulation has 6 provinces at LOD 0. Many steps are independent per province — Province A's NPC behavior does not depend on Province B's results within the same tick. The tick processes provinces simultaneously via a persistent thread pool.

**Thread pool sizing:**
```
pool_size = min(hardware_concurrency() - 1, 6)
```
Reserve 1 core for main thread + UI rendering. Cap at 6 (one per province; additional workers idle). On 4 cores: 3 workers, 2 batches of 3 provinces. On 8 cores: 6 workers, all 6 provinces simultaneously. On 2 cores: 1 worker, graceful degradation to near-sequential.

The pool is created once at startup and lives for the entire session.

**Step classification:**

| Steps | Strategy |
|---|---|
| 1 (production), 3 (R&D), 4 (labor), 7 (NPC behavior), 9 (community cohesion), 12 (facility signals), 13 (investigators), 15 (obligations), 18 (population aging), 21 (trust updates) | Province-parallel — fully independent |
| 2 (supply chain/transit), 5 (prices), 6 (financials), 17 (NPC spending) | Global aggregate first; then province-parallel recompute |
| 8, 10, 11, 14, 16, 19, 20, 22–27 | Sequential — fast; infrequent or trivially cheap |

**Deterministic merge protocol:** Each province worker writes to an isolated per-worker `DeltaBuffer`. After all workers complete, the main thread merges delta buffers in ascending province index order (Province 0 first, then 1, 2... regardless of thread completion order). Same seed + same inputs = identical results on any number of cores.

**Floating-point canonical summation:** All accumulations (supply totals, aggregate demand) use a fixed ascending sort order (`good_id` ascending, then `province_id` ascending) before summing. IEEE 754 does not guarantee `(a + b) + c == a + (b + c)`; summation order must be deterministic to prevent price drift across runs.

**Cross-province interactions:** When a province worker generates an effect targeting another province (NPC action, criminal shipment dispatch, media story propagation), it writes to a `CrossProvinceDeltaBuffer` rather than the standard per-worker `DeltaBuffer`. After all province workers complete, the main thread processes cross-province deltas sequentially. Effects take hold at the start of the following tick — one tick of propagation delay, consistent with the existing one-tick lag principle for price signals.

### 3.3 — DeferredWorkQueue [CORE]

A single min-heap sorted on `due_tick` holds all future-scheduled work across all systems. `ConsequenceEntry` and `TransitShipment` arrivals are views over this queue — not separate data structures.

```cpp
enum class WorkType : uint8_t {
    consequence,                // ConsequenceEntry execution
    transit_arrival,            // TransitShipment arriving at destination
    interception_check,         // per-tick criminal shipment exposure check
    npc_relationship_decay,     // batch decay for one NPC's relationships
    evidence_decay_batch,       // batch decay for one evidence token
    npc_business_decision,      // quarterly decision for one NPCBusiness
    market_recompute,           // price recompute for one RegionalMarket
    investigator_meter_update,  // InvestigatorMeter recalc for one LE NPC
    climate_downstream_batch,   // agricultural + community stress update
    background_work,            // non-urgent: route table rebuild, log compaction, etc.
    npc_travel_arrival,         // NPC physically arrives at destination province
    player_travel_arrival,      // Player character physically arrives at destination province
    community_stage_check,      // community response stage threshold evaluation for one province
    maturation_project_advance, // advance one MaturationProject (see R&D doc §Part 3.5)
                                // scheduled every tick per active project; runs in tick step 26
                                // alongside evidence notifications and GlobalTechnologyState update.
                                // Payload: { business_id, node_key }
                                // On completion: actor_tech_state.holdings[node_key].maturation_level
                                //   incremented and re-clamped to maturation_ceiling.
    commercialize_technology,   // player command: bring a researched technology to market.
                                // Payload: { business_id, node_key, decision: CommercializationDecision }
                                //   (CommercializationDecision enum defined in R&D doc §Part 3.5)
                                // Execution: immediate — no dispatch offset. Fires in the same tick it
                                //   is queued, after step 26 R&D processing completes.
                                // Precondition: actor_tech_state.has_researched(node_key) == true.
                                //   Attempting to commercialize an unresearched node is a no-op with
                                //   UI error feedback; simulation state is unchanged.
                                // Effects on completion:
                                //   actor_tech_state.holdings[node_key].stage = commercialized
                                //   actor_tech_state.holdings[node_key].commercialized_tick = current_tick
                                //   if GlobalTechnologyState.first_commercialized_by.count(node_key) == 0:
                                //       GlobalTechnologyState.first_commercialized_by[node_key] = business_id
                                //   Product appears in regional market price feeds next tick.
                                // NPC path: CommercializationDecision is evaluated inside
                                //   WorkType::npc_business_decision (quarterly). NPC actors do not use
                                //   this WorkType — it is the player-specific dispatch for immediate
                                //   command execution. Both paths write to the same actor_tech_state fields.
};

struct DeferredWorkItem {
    uint32_t    due_tick;        // min-heap sort key
    WorkType    type;
    uint32_t    subject_id;      // NPC id, shipment id, market id, etc.
    WorkPayload payload;         // type-specific; std::variant
};

// In WorldState:
std::priority_queue<DeferredWorkItem,
                    std::vector<DeferredWorkItem>,
                    DeferredWorkComparator> deferred_work_queue;
```

Each tick, step 2 (supply chain/transit/consequences):
1. Pop all items where `due_tick <= current_tick`; execute by type
2. Reschedule recurring work (after NPC relationship decay batch, push next at `current_tick + 30`)
3. Partition transit arrivals by `destination_province_id` — processed by that province's worker in parallel

**Background work at idle time:**
```
each tick:
  1. execute all due deferred work         ← must complete; time-critical
  2. execute all tick modules              ← must complete; time-critical
  3. IF real_time_remaining > threshold:
       process N items from background_work subqueue   ← uses idle time only
```
At 30x fast-forward, idle time is near-zero. At 1x speed, background work completes comfortably. No manual throttling needed.

### NPC Business Dispatch Spreading

Each `NPCBusiness` has a `dispatch_day_offset` (0–29, set at world generation as `hash(business_id) % 30`). Dispatch evaluation runs on tick `dispatch_day_offset`, `dispatch_day_offset + 30`, `dispatch_day_offset + 60`... This spreads NPC dispatch load evenly across each 30-tick month with no monthly processing spike. Player-owned businesses dispatch on player command and have no offset.

### Tick Architecture: ITickModule

Each step is a module registered by a package conforming to `ITickModule` (see §2a). Modules that support province-parallel execution declare `is_province_parallel() = true` and implement `execute_province()`. Sequential modules implement `execute()` only. The `TickOrchestrator` dispatches province-parallel modules to the thread pool and sequential modules on the main thread.

```cpp
// Province-parallel capable modules additionally declare:
virtual bool is_province_parallel() const noexcept { return false; }
virtual void execute_province(uint32_t province_idx,
                              const WorldState& state,
                              DeltaBuffer& province_delta) {}
```

---

## 4. NPC Architecture [V1]

### Complete Enum: NPCRole

```cpp
enum class NPCRole : uint8_t {
    // --- Political and Institutional ---
    politician          = 0,   // Holds or seeks elected office. Behavior engine: campaign
                                //   mechanics, coalition management, legislative voting.
    candidate           = 1,   // In active election cycle; not yet an officeholder.
                                //   Enables: campaign scene cards, endorsement requests.
    regulator           = 2,   // Government agency employee with enforcement authority.
                                //   Enables: formal inquiry, permit denial, fine issuance.
    law_enforcement     = 3,   // Police, investigative unit. Drives InvestigatorMeter.
                                //   Enables: arrest consequence, surveillance, raid.
    prosecutor          = 4,   // Legal authority to charge and try. Enables: charges filed,
                                //   plea negotiation, case advancement.
    judge               = 5,   // Adjudicates legal proceedings. Enables: ruling consequences,
                                //   sentencing, bail decisions. Corruption node when applicable.
    appointed_official  = 6,   // Central bank governor, agency head. [EX in V1 — record
                                //   exists but active mechanics are expansion scope.]

    // --- Business and Corporate ---
    corporate_executive = 7,   // C-suite NPC at a significant NPC business. Makes
                                //   strategic decisions for NPCBusiness. Acquirable as contact.
    middle_manager      = 8,   // Below executive; carries operational knowledge.
                                //   Whistleblower risk source. Visible in workforce scene cards.
    worker              = 9,   // Employee at facility (player or NPC). Drives satisfaction
                                //   model, strike risk, whistleblower emergence.
    accountant          = 10,  // Access to financial records. Enables: audit findings,
                                //   money laundering facilitation, financial evidence creation.
    banker              = 11,  // Enables: suspicious transaction flagging, FIU reporting,
                                //   money laundering facilitation, credit denial.
    lawyer              = 12,  // Legal defense, advice, contract execution. Criminal defense
                                //   lawyer is a distinct service type (see FTL: criminal
                                //   defense firm V1). General civil lawyer is this role.
    media_editor        = 13,  // Senior editorial gatekeeper at media outlet. Controls
                                //   whether journalist's story is published.

    // --- Media and Civil Society ---
    journalist          = 14,  // Investigates, publishes. Primary mechanism for evidence
                                //   tokens reaching public. Motivation drives story pursuit.
    ngo_investigator    = 15,  // Non-governmental investigator (human rights, environmental,
                                //   financial crime). Cannot be neutralized by domestic
                                //   political corruption. High activation in EX expansion
                                //   (trafficking). Thin role in V1.
    community_leader    = 16,  // Respected figure in a region's demographic community.
                                //   High social_capital; potential opposition org founder.
                                //   Can be co-opted (Section 14: co_opt_leadership).
    union_organizer     = 17,  // [Thin in V1 per Feature Tier List.] Organizes workers
                                //   in player-owned or NPC businesses. Enables: collective
                                //   action events, strike scene cards. Thin behavior engine
                                //   in V1; full model is expansion depth.

    // --- Grey-Area and Criminal ---
    criminal_operator   = 18,  // Runs criminal enterprise operations (drug production,
                                //   distribution coordination). Member of CriminalOrganization.
                                //   Potential informant under arrest pressure.
    criminal_enforcer   = 19,  // Coercive capacity for criminal org. Enables: physical
                                //   threat consequences, territorial violence, protection
                                //   collection. Elevated law enforcement response if active.
    fixer               = 20,  // Grey-area connector. Facilitates introductions, procures
                                //   services, maintains plausible deniability. Access expands
                                //   as player's street reputation grows.
    bodyguard           = 21,  // Personal protection NPC. Has loyalty and skill stats.
                                //   [EX — placeholder role in V1; personal security system
                                //   is expansion scope per Feature Tier List.]

    // --- Personal Life ---
    family_member       = 22,  // Partner, child, parent. Emotional weight modifier on
                                //   memory entries. Target for leverage against player.
                                //   Generates personal scene cards.
};
```

### KnowledgeMap [V1]

`KnowledgeMap` is the type of `NPC.known_evidence` and `NPC.known_relationships`. It stores what an NPC knows about the world — evidence tokens they're aware of and relationships between other actors they've observed or been told about. It is an information store, not a physical container. Knowledge propagates via NPC communication actions (`share_evidence`, `share_intelligence`) and direct observation events, not via transit.

```cpp
enum class KnowledgeType : uint8_t {
    evidence_token  = 0,  // NPC is aware of an EvidenceToken (token_id stored as subject_id).
                          //   Does not mean they possess it — only that they know it exists.
    relationship    = 1,  // NPC knows about a relationship between two actors.
                          //   subject_id = first actor; secondary_subject_id = second actor.
    activity        = 2,  // NPC knows about an ongoing activity (business op, criminal op,
                          //   investigation). subject_id = activity owner's npc_id.
    identity_link   = 3,  // NPC knows a burned identity is connected to a real identity.
                          //   subject_id = cover_identity_id; secondary_subject_id = real_actor_id.
};

struct KnowledgeEntry {
    uint32_t       subject_id;            // primary entity this knowledge is about
    uint32_t       secondary_subject_id;  // second entity, for relational knowledge types
                                          //   (0 if unused; relationship and identity_link types)
    KnowledgeType  type;
    float          confidence;            // 0.0–1.0; certainty. Direct observation = 1.0;
                                          //   hearsay from trusted source = 0.6–0.8;
                                          //   rumour = 0.2–0.4. Decays at
                                          //   config.knowledge.confidence_decay_rate per tick
                                          //   when not reinforced by new corroborating entries.
    uint32_t       acquired_at_tick;      // when this NPC first obtained this knowledge
    uint32_t       source_npc_id;         // 0 = direct observation; else npc_id of informant
    VisibilityScope original_scope;       // scope of the underlying activity/token when observed;
                                          //   governs how many layers of investigation were needed
};

// KnowledgeMap is the container. Used as NPC.known_evidence and NPC.known_relationships.
// Two separate maps per NPC because evidence and relationship knowledge have different
// access patterns in the behavior engine (evidence lookup by token_id; relationship
// lookup by actor pair).
using KnowledgeMap = std::vector<KnowledgeEntry>;
```

**Propagation rule:** When an NPC shares knowledge (`share_evidence` action), the receiving NPC's `KnowledgeMap` gains a new `KnowledgeEntry` with `source_npc_id` = sharer, `confidence` scaled by the sharer's own confidence and the trust between the two NPCs. Sharing is instantaneous — no transit delay. This is consistent with §18.14 (information vs. physical transit distinction).

**Evidence confidence scaling on share (TDD-I3):**
```
received_confidence = original_confidence × sharer_confidence × trust_factor(relationship_score)
where trust_factor(r) = clamp(r / 100.0, 0.1, 1.0)
```
The sharer's own confidence in the evidence is the first multiplier. The trust factor normalizes relationship score (0–100) to [0.1, 1.0] so that unknown or hostile relationships (r < 10) still permit some evidence propagation (10% multiplier) but trusted relationships (r ≥ 100) preserve full fidelity.

**Named constants (`simulation_config.json` → `knowledge`):**

| Constant | Default | Effect |
|---|---|---|
| `confidence_decay_rate` | 0.0005 | Per-tick confidence decay on unconfirmed entries (~5.5 years to halve at 1x) |
| `min_confidence_threshold` | 0.10 | Entries below this are pruned from KnowledgeMap on next consolidation pass |
| `direct_observation_confidence` | 1.00 | Starting confidence for directly witnessed knowledge |
| `trusted_source_confidence_scale` | 0.80 | Multiplier applied when source has trust > 0.6 |
| `rumour_confidence_scale` | 0.35 | Multiplier applied when source has trust < 0.3 |

### [PROTOTYPE] Core Data Structure

```cpp
struct NPC {
    uint32_t id;
    NPCRole role;                         // see complete enum above

    // Motivation model
    MotivationVector motivations;         // weighted map of: money, career, ideology,
                                          //   revenge, stability, power, survival
    float risk_tolerance;                 // 0.0–1.0; how likely to act on knowledge

    // Memory
    std::vector<MemoryEntry> memory_log;  // timestamped entries, capped at MAX_MEMORY_ENTRIES
                                          // MAX_MEMORY_ENTRIES = 500 (exact; declared in shared header)

    // Knowledge
    KnowledgeMap known_evidence;          // evidence tokens this NPC is aware of
    KnowledgeMap known_relationships;     // what they know about who knows whom

    // Relationships
    RelationshipGraph relationships;      // directed: NPC's view of each other NPC
                                          // not stored as full matrix — sparse representation

    // Resources
    float capital;                        // money available
    float social_capital;                 // platform / access / authority
    std::vector<uint32_t> contact_ids;    // who they can actually reach

    // Movement leadership (role-conditional; see below)
    uint32_t movement_follower_count;  // count of background population NPCs
                                        // in a movement this NPC leads or
                                        // co-leads. 0 for NPCs not in movement-
                                        // capable roles.
                                        // Meaningful for: community_leader,
                                        // politician, union_organizer,
                                        // ngo_investigator.
                                        // Updated by tick step 22 alongside
                                        // PlayerCharacter.movement_follower_count.
                                        // Uses identical update logic — the player
                                        // is not special in the movement system.

    // Location — physical presence
    uint32_t home_province_id;            // province where NPC is based and returns to
    uint32_t current_province_id;         // province NPC is physically in this tick
                                          // == home_province_id when not travelling
                                          // updated to destination when DeferredWorkItem(npc_travel_arrival) fires
    NPCTravelStatus travel_status;        // resident, in_transit, visiting (see §18.15)

    // State
    NPCStatus status;                     // active, imprisoned, dead, fled
    // NOTE: NPC delayed actions are DeferredWorkItem entries with type == WorkType::consequence
    // and subject_id == npc.id. There is no separate ActionQueue. See §3.3.
};
```

**movement_follower_count note:** Non-zero only for NPCs in movement-capable roles who have accumulated followers through community engagement. The field exists on all NPCs for struct uniformity; roles without movement capacity will always hold 0. Tick step 22 updates all movement leaders (player and NPC) in the same pass.

### Memory Log

Memory entries are the most expensive component per NPC. Each entry is:

```cpp
struct MemoryEntry {
    uint32_t tick_timestamp;
    MemoryType type;                      // interaction, observation, hearsay, event
    uint32_t subject_id;                  // who/what this memory is about
    float emotional_weight;              // how significant; affects decay rate
    float decay;                         // 0.0–1.0; decays over time toward floor
    bool is_actionable;                  // does this memory motivate action?
};
```

**Memory cap:** Significant NPCs hold a maximum of MAX_MEMORY_ENTRIES (= 500) active memory entries. Entries below a decay threshold are archived (retained for forensic purposes like investigation mechanics, but no longer driving behavior). Background population NPCs hold no individual memories — their behavior is driven by cohort-level statistics only.

**[RISK]** At 2,000 significant NPCs × 500 entries = 1,000,000 memory entries in active memory. Serialization and deserialization at autosave must be fast. Structure-of-arrays layout preferred over array-of-structures for cache performance.

**V1 planning assumption:** 750 significant NPCs per region. Prototype validates viability. This value is adjustable based on prototype results.

### 4.5 — Worker Satisfaction [V1]

Worker satisfaction is not a stored field on the NPC struct — it is computed fresh from the NPC's memory log when needed. This is intentional: satisfaction is always consistent with memory state, and there is no stale cached value to diverge.

#### worker_satisfaction() — Full Specification

```cpp
float worker_satisfaction(const NPC& npc, uint32_t current_tick):

    float numerator   = 0.0f;
    float denominator = 0.0f;

    for entry in npc.memory_log:
        if entry.type not in SATISFACTION_RELEVANT_TYPES:
            continue

        recency_weight = exp(
            -config.satisfaction_decay_rate × (current_tick - entry.tick_timestamp)
        )

        numerator   += entry.emotional_weight × recency_weight
        denominator += abs(entry.emotional_weight) × recency_weight

    if denominator < EPSILON:
        return 0.5f   // no relevant memories; neutral satisfaction

    // Result in range (-1.0, 1.0); normalize to (0.0, 1.0)
    raw = numerator / denominator
    return (raw + 1.0f) / 2.0f
```

**Config constant (`simulation.json`):** `satisfaction_decay_rate = 0.002` — approximately 500-tick half-life; recent events matter more than old ones.

#### SATISFACTION_RELEVANT_TYPES

Memory entry types that contribute to satisfaction computation. `emotional_weight` sign convention for employment contexts:

| Memory type | Emotional weight range | Direction |
|---|---|---|
| `employment_positive` | +0.2 to +0.5 | Positive: fair treatment, raise, recognition, promotion |
| `employment_negative` | −0.2 to −0.7 | Negative: pay cut, overwork, unfair treatment, demotion |
| `witnessed_illegal_activity` | −0.3 to −0.8 | Negative: scale by severity of witnessed activity |
| `witnessed_safety_violation` | −0.4 to −0.9 | Negative: unsafe equipment, chemical exposure, documented hazard |
| `witnessed_wage_theft` | −0.3 to −0.7 | Negative: payroll fraud, unpaid overtime, illegal deductions |
| `physical_hazard` | −0.5 to −0.9 | Negative: workplace injury, unsafe conditions |
| `facility_quality` | −0.1 to +0.2 | Sign depends on whether quality improved or degraded |
| `retaliation_experienced` | −0.6 to −1.0 | Negative: fired for organizing, threatened |

#### Whistleblower eligibility condition

All three conditions must be simultaneously true. Missing any one: no whistleblower action:

```
worker_satisfaction(npc, current_tick) < 0.35
AND npc.memory_log has qualifying entry where:
    entry.type in {
        witnessed_illegal_activity,  // criminal activity at facility
        witnessed_safety_violation,  // dangerous conditions; legal or illegal
        witnessed_wage_theft         // wage suppression, unpaid hours,
                                     // illegal deductions
    }
    AND entry.emotional_weight < -0.6
AND npc.risk_tolerance > 0.4
```

This is the canonical definition of whistleblower eligibility. It is the implementation target for GDD Section 6.8 scenario tag `scenario_whistleblower_emerges_on_eligible_conditions`.

**Design note:** Whistleblower eligibility is path-agnostic. A legitimate business with severe safety violations and low worker satisfaction faces the same whistleblower emergence risk as a criminal facility. The memory type determines which authority the whistleblower contacts: witnessed_illegal_activity → LE NPC; witnessed_safety_violation → regulator NPC; witnessed_wage_theft → regulator NPC or journalist NPC depending on severity. Contact target is resolved in the NPC behavior engine (tick step 10) via expected_value evaluation, not hardcoded.

#### Performance note

`worker_satisfaction()` is not called every tick for every NPC. It is called:
- When an NPC's whistleblower eligibility needs checking (tick step 10, behavior evaluation)
- When a facility calculates its `worker_satisfaction_score` (tick step 5, production)
- When the UI layer requests it for a specific NPC (not during simulation tick)

The computation iterates over the NPC's memory log once — O(MAX_MEMORY_ENTRIES). This is acceptable given the call frequency.

### Relationship Graph

Not stored as a full N×N matrix. Stored as a **sparse directed graph**: each NPC holds a list of relationships they have opinions about, not an opinion about every other NPC.

```cpp
struct Relationship {
    uint32_t target_npc_id;
    float trust;                          // -1.0 to 1.0
    float fear;                           // 0.0 to 1.0
    float obligation_balance;            // positive = they owe player, negative = player owes them
    std::vector<uint32_t> shared_secrets; // evidence tokens both parties hold
    uint32_t last_interaction_tick;
    bool is_movement_ally;               // true if NPC is active collaborator in a player-led movement
    float recovery_ceiling;              // 1.0 default (no ceiling); set below 1.0 on catastrophic
                                          // trust loss. Trust can never rebuild above this value.
                                          // See Section 13 for floor principle specification.
};
```

An NPC only has entries for NPCs they've actually interacted with or know about. The average significant NPC has 20–100 meaningful relationships. This makes the graph O(N × avg_relationships) rather than O(N²).

### Behavior Generation

Each tick, the NPC behavior engine runs each significant NPC through a decision loop (tick step 10). The core of this loop is the `expected_value` function — the most important algorithm in the simulation. Every NPC decision passes through it.

#### expected_value() — Full Specification

```cpp
// ActionOutcome: the atomic unit of what an action might produce
struct ActionOutcome {
    OutcomeType type;
    float       probability;    // 0.0–1.0; estimated likelihood this outcome occurs
    float       magnitude;      // 0.0–1.0; how impactful if it occurs
};

enum class OutcomeType : uint8_t {
    financial_gain,         // money outcomes
    security_gain,          // reduces threat or uncertainty
    career_advance,         // status, promotion, influence increase
    revenge,                // punishes a specific party
    ideological,            // advances a belief or cause
    relationship_repair,    // improves a valued relationship
    self_preservation,      // survival outcomes
    loyalty_obligation,     // fulfills a felt duty
};

float expected_value(action, npc):
    base_ev = sum over all action.outcomes:
        outcome_type_weight(npc, outcome.type)
        × outcome.probability
        × outcome.magnitude

    risk_adjusted_ev = base_ev × risk_discount(npc, action)

    relationship_ev = risk_adjusted_ev
        × (1.0 + relationship_modifier(npc, action.target))

    return relationship_ev
```

#### Sub-function: outcome_type_weight()

```cpp
float outcome_type_weight(const NPC& npc, OutcomeType type) {
    return npc.motivation.weights[type];
    // Motivation weights sum to 1.0 across all OutcomeType values
    // Initialized at NPC creation; can shift slowly from memory entries
}
```

#### Sub-function: risk_discount()

```cpp
float risk_discount(npc, action):
    risk_gap = action.exposure_risk - npc.risk_tolerance
    if risk_gap <= 0:
        return 1.0   // risk within tolerance; no discount
    else:
        return max(MIN_RISK_DISCOUNT, 1.0 - risk_gap × RISK_SENSITIVITY_COEFF)
        // MIN_RISK_DISCOUNT: loaded from simulation.json; proposed default: 0.05
        //   Prevents expected value from going negative — action remains conceivable
        // RISK_SENSITIVITY_COEFF: loaded from simulation.json; proposed default: 2.0
```

#### Sub-function: relationship_modifier()

```cpp
float relationship_modifier(npc, target_id):
    if target_id == null:
        return 0.0   // no target; no relationship effect
    rel = npc.relationships[target_id]
    if rel == null:
        return 0.0   // no relationship; neutral
    if action.type is cooperative:
        return rel.trust × config.trust_ev_bonus     // positive trust boosts cooperative EV
    if action.type is adversarial:
        return -(rel.trust × config.trust_ev_bonus)  // positive trust penalizes adversarial EV
    return 0.0
    // config.trust_ev_bonus: loaded from simulation.json
```

#### Sub-function: inaction_threshold()

```cpp
float inaction_threshold(npc):
    return npc.inaction_threshold_personal
    // Set at NPC creation
    // Default = config.inaction_threshold_default (loaded from simulation.json; proposed: 0.10)
    // High risk_tolerance NPCs get lower thresholds (more likely to act)
    // Conservative NPCs (institutional_integrity ideology) get higher thresholds
```

#### Action selection pseudocode (tick step 10)

```
// Runs for each significant NPC in tick step 10: NPC behavior evaluation

candidate_actions = generate_available_actions(npc, world_state)
// Filtered by: NPC has required resources, NPC has required relationships,
// action preconditions are met (e.g., has_evidence before can_publish)

for action in candidate_actions:
    action.ev = expected_value(action, npc)

best_action = candidate_actions.max_by(ev)

if best_action.ev > inaction_threshold(npc):
    queue_action(best_action, npc)
    // action dispatched as DeferredWorkItem in WorldState.deferred_work_queue
    // (WorkType::consequence, subject_id == npc.id; see §3.3)
    // pending_actions was removed; all NPC delayed actions use the unified deferred queue
else:
    npc.status = waiting
    // NPC re-evaluates next tick; conditions may change
```

#### Action outcome authoring — example: publish_story

Each action type in the system specifies its `outcomes` array as authored content in the actions data file. Example for `publish_story` (journalist NPC):

```cpp
// publish_story outcomes:
outcomes = [
    { OutcomeType::career_advance, probability: 0.6, magnitude: 0.7 },  // career upside if true
    { OutcomeType::security_gain,  probability: 0.4, magnitude: 0.3 },  // safety if story exposes threat
    { OutcomeType::financial_gain, probability: 0.3, magnitude: 0.2 },  // freelance/bonus income
]
// exposure_risk = 0.3 (legal risk of publishing)
// target_id = player (adversarial action against player)
```

The full action type catalog with outcome arrays is authored content — specified in the NPC actions data file, not hardcoded in engine logic. This allows behavioral tuning without engine changes.

**All constants reference `simulation.json`:** `MIN_RISK_DISCOUNT`, `RISK_SENSITIVITY_COEFF`, `trust_ev_bonus`, `inaction_threshold_default`.

**[RISK]** This loop running on 2,000+ NPCs per tick is the most expensive single operation in the simulation. Optimization strategies:

- **Lazy evaluation:** Only run full decision loop on NPCs whose state changed in the last tick. NPCs in stable situations skip the loop.
- **Priority queuing:** NPCs with high emotional_weight recent memories get higher evaluation priority. NPCs with no actionable memories get evaluated at reduced frequency (every 7 ticks instead of every tick).
- **LOD system:** NPCs far from the player's attention zone (no recent player interaction, not in regions player is active in) run at reduced fidelity.

### NPC Population Scale Targets

| Category | Count per region | Simulation model |
|---|---|---|
| Significant NPCs | 500–1,000 (V1 planning: 750) | Full memory, motivation, relationship model |
| Named background NPCs | 2,000–5,000 | Simplified: motivation + 1 relationship to player, no full memory log |
| Background population | Millions | Cohort statistics only; individual promotion to Named or Significant on threshold |

**[PROTOTYPE]** These targets must be validated. If full-model NPCs at 1,000 per region exceeds tick budget, the design options are: (a) reduce to 300–500 full-model NPCs and expand Named NPC tier, or (b) implement more aggressive lazy evaluation.

---

## 5. Economy Engine [V1]

### Complete Enum: BusinessSector

```cpp
enum class BusinessSector : uint8_t {
    manufacturing       = 0,   // Heavy and consumer goods manufacturing. High capital,
                                //   significant labor force, supply chain dependencies.
    food_beverage       = 1,   // Food production, processing, distribution, retail F&B.
                                //   High consumer demand visibility; addiction drug overlap
                                //   at precursor level.
    retail              = 2,   // Consumer goods retail. High cash flow, good laundering
                                //   vehicle, demand-signal sensitive.
    services            = 3,   // Professional services, consulting, cleaning, security
                                //   (legitimate). High labor dependence, variable margin.
    real_estate         = 4,   // Property development, management, sales. Key money
                                //   laundering sector. Price responds to criminal dominance.
    agriculture         = 5,   // Farming, livestock, aquaculture. Resource-dependent.
                                //   Climate conditions affect output.
    energy              = 6,   // Extraction of energy resources (oil, gas, coal) and
                                //   regional energy utility operations. [V1: extraction
                                //   only; power generation abstracted per Feature Tier List.]
    technology          = 7,   // Software, electronics, data services. High-margin,
                                //   cybercrime adjacent, R&D intensive.
    finance             = 8,   // Banking, insurance, investment. Laundering infrastructure.
                                //   FIU reporting obligation. Highly regulated.
    transport_logistics = 9,   // Trucking, rail, shipping, warehousing. Supply chain
                                //   backbone. Drug and contraband distribution vehicle.
    media               = 10,  // Newspapers, broadcast, digital platforms. Exposure
                                //   activation mechanism. Ownable for influence.
    security            = 11,  // Private security firms. Facility protection, enforcement
                                //   capacity. Corruption and criminal adjacency risk.
    research            = 12,  // Corporate and pharmaceutical R&D labs. Technology
                                //   advancement, criminal R&D overlap (designer drugs,
                                //   precursor synthesis).
    criminal            = 13,  // Explicitly criminal operations: drug production,
                                //   distribution, protection rackets, laundering fronts.
                                //   Uses informal market price signals, not formal market.
};
```

### BusinessSector vs criminal_sector — canonical rules

BusinessSector describes what an NPCBusiness does.
NPCBusiness.criminal_sector (bool) controls which market layer it uses.
The two fields are independent. All four combinations:

| sector       | criminal_sector | Meaning |
|---|---|---|
| criminal     | true  | Pure criminal operation. Uses informal market. Expected case. |
| retail/other | true  | Front business. Registered legitimate sector; actual revenue from criminal supply chain. Uses informal market pricing for actual revenue; formal market for cover. |
| criminal     | false | INVALID in V1. Loader rejects and logs error. |
| retail/other | false | Normal legitimate business. Uses formal market. Expected case. |

The authoritative signal for market layer selection is criminal_sector (bool).
BusinessSector::criminal is a semantic tag for categorisation and UI purposes.
When criminal_sector = true, the business participates in informal market
supply/demand regardless of its registered sector.

### VisibilityScope — Information Visibility for All Actors [V1]

OPSEC is not a criminal-exclusive concern. Every actor — legitimate corporation, criminal operation, political office — manages how much of their activity is observable by other actors. `VisibilityScope` is the enum that governs this universally. It applies to activities, documents, relationships, and business operations. Criminal activity defaults to `concealed`; legitimate trade secrets sit at `internal`; a patent filing transitions from `internal` to `public_info` on filing date.

```cpp
enum class VisibilityScope : uint8_t {
    public_info     = 0,  // Observable by any actor. Published financials,
                          //   news, public contracts, listed prices, patent filings.
    industry        = 1,  // Observable by sector peers, trade press, analysts.
                          //   Revenue estimates, hiring signals, supplier hints,
                          //   industry conference disclosures.
    institutional   = 2,  // Observable by regulators, tax authority, licensed
                          //   inspectors. Audit-accessible records, tax filings,
                          //   mandatory compliance documentation.
    internal        = 3,  // Observable only by the owning actor and direct employees.
                          //   R&D pipelines, trade secrets, internal pricing,
                          //   unreported financials, strategic plans.
    concealed       = 4,  // Requires active investigation to surface.
                          //   Default for criminal activity. Also applies to
                          //   covert political funding, industrial espionage,
                          //   and any activity the actor is actively hiding.
                          //   FacilitySignals.scrutiny_mitigation governs how
                          //   much physical signal leaks despite concealed intent.
};
```

**What reads VisibilityScope:**
- Law enforcement: reads activities at `concealed` with `criminal_sector = true`. Requires evidence tokens to advance past `InvestigatorMeter` threshold.
- Regulators: read activities at `institutional` and above for their jurisdiction. `RegulatorScrutinyMeter` builds from physical signals (§16) regardless of `VisibilityScope`.
- Journalists: read `public_info` and `industry` directly; reach `internal` via whistleblower or leaked documents (evidence token of type `documentary`).
- Rival actors (corporate espionage): attempt to surface `internal` activities through bribery, infiltration, or hired investigation.
- Player: observes own activities at all scopes; observes NPC activities up to their `KnowledgeMap` access level.

**Default values by context:**

| Context | Default VisibilityScope |
|---|---|
| Normal business operation | `institutional` (regulators can audit) |
| R&D pipeline, trade secrets | `internal` |
| Criminal operation | `concealed` |
| Public filing, press release | `public_info` |
| Covert political donation | `concealed` |
| Patent on filing date | `public_info` |

`VisibilityScope` is set per-activity at the time an action is taken. It is stored on the relevant event or relationship record, not on the actor. The actor's `default_activity_scope` (see `NPCBusiness`) provides the fallback when no activity-level override is set.

### Price Model

Each tradeable good in each region has:

```cpp
struct RegionalMarket {
    uint32_t good_id;
    uint32_t province_id;               // province this market belongs to (not region_id; Region is thin grouping only)
    float spot_price;
    float equilibrium_price;              // recalculated each tick
    float adjustment_rate;               // good-type dependent: financial fast, housing slow
    float supply;                         // local production this tick + transit arrivals this tick (§18.9)
                                          // goods in transit to this province are NOT included until arrival_tick
    float demand_buffer;                  // from previous tick (one-tick lag)
    float import_price_ceiling;  // set on LOD 1 trade offer acceptance (see §18.16):
                                  // = offer.offer_price for the accepted import offer.
                                  // Applied immediately on acceptance — price cannot
                                  // exceed what LOD 1 importers will pay even while
                                  // goods are in transit.
                                  // 0.0 = no active LOD 1 import offer for this good.
                                  // When non-zero, overrides config.import_ceiling_coeff
                                  // as the effective upper clamp in the equilibrium
                                  // price formula (Step 1).
    float export_price_floor;    // set when a LOD 1 nation publishes an import bid
                                  // for this good (they want to buy it at offer_price).
                                  // = bid offer_price; province can always sell to LOD 1
                                  // at this price, so domestic price won't fall below it.
                                  // 0.0 = no active LOD 1 export bid for this good.
                                  // When non-zero, overrides config.export_floor_coeff
                                  // as the effective lower clamp in Step 1.
};
```

Price update per tick — full 3-step specification:

```
Per tick, for each good in each LOD 0 RegionalMarket:

Step 1: Compute equilibrium price
    equilibrium_price = clamp(
        good.base_price × (demand_buffer / max(supply, SUPPLY_FLOOR)),
        (market.export_price_floor > 0.0f
            ? market.export_price_floor
            : good.base_price × config.export_floor_coeff),
        (market.import_price_ceiling > 0.0f
            ? market.import_price_ceiling
            : good.base_price × config.import_ceiling_coeff)
    )
    // config.export_floor_coeff and config.import_ceiling_coeff remain
    // as global fallbacks when no active LOD 1 offer exists for a good.
    // LOD 1 offer prices take precedence when set because they represent
    // actual available trade at a known price — a harder constraint than
    // the global structural coefficient.

Step 2: Adjust spot price toward equilibrium
    price_delta = (equilibrium_price - spot_price) × config.price_adjustment_rate
    spot_price += price_delta

Step 3: Apply LOD 2 global price modifier
    spot_price *= world_state.lod2_price_index.lod2_price_modifier[good_id]
    // modifier is 1.0 for goods with no LOD 2 contribution
```

**Variable definitions:**

| Variable | Source | Description |
|---|---|---|
| `good.base_price` | goods data file (CSV) | Fixed reference price for this good. The anchor — not recomputed each tick. Prevents runaway drift. At equilibrium demand/supply (ratio = 1.0), spot_price equals base_price. |
| `demand_buffer` | tick step 17 output | Accumulated demand units for this good this tick |
| `supply` | tick steps 4–5 output | Local production this tick + quantities from TransitShipments with `arrival_tick == current_tick`. Goods in transit to this province are not counted until they physically arrive. LOD 1 imports are also in transit (see §18.16) and appear in supply only on their arrival tick. |
| `SUPPLY_FLOOR` | engine constant = 0.01 | Division-by-zero guard. Not in config — must not be 0 |
| `config.export_floor_coeff` | `economy.json` | Lower clamp on equilibrium; proposed default: 0.40 |
| `config.import_ceiling_coeff` | `economy.json` | Upper clamp on equilibrium; proposed default: 3.0 |
| `config.price_adjustment_rate` | `economy.json` | Fraction of gap closed each tick; proposed default: 0.10 (10% per tick) |
| `lod2_price_modifier[good_id]` | `WorldState.lod2_price_index` | Annual global modifier from LOD 2 batch (see Section 21); 1.0 for unmodified goods |

**`base_price` is the drift anchor.** Without it, a good with no demand would ratchet down to zero and never recover. `base_price` provides gravity — prices orbit it rather than wandering. At demand/supply ratio = 1.0, `spot_price = base_price` is the steady state.

**LOD 1 price contribution:** LOD 1 nations publish trade offers at their own `offer_price` (see Section 20). When a LOD 0 province accepts a LOD 1 trade offer, the accepted quantity becomes an import into the province's supply. This adds to the `supply` variable in Step 1 on subsequent ticks, indirectly dampening equilibrium price.

**Quality premium overlay** (applied at point of transaction, not stored in spot_price):

```
effective_price = spot_price × (1.0 + good.quality_premium_coeff × (batch_quality - market_quality_avg))
```

`good.quality_premium_coeff` is defined per-good in the goods data file. This overlay is transactional only — it does not alter the `spot_price` stored in `RegionalMarket`.

### Consumer Demand Model [V1]

Consumer demand populates `RegionalMarket.demand_buffer` each tick (tick step 17). Demand has two components: **derived demand** (industrial consumption of inputs by facilities — computed in tick step 1 as a by-product of production) and **consumer demand** (NPC household purchase decisions). This section specifies consumer demand only.

```cpp
enum class BuyerType : uint8_t {
    price_sensitive  = 0,  // Maximises value/price ratio. Demand strongly elastic to price.
    quality_seeker   = 1,  // Prioritises quality score regardless of price.
                           //   Effective in high-income NPC cohorts.
    brand_loyal      = 2,  // Sticky to established brand; higher price tolerance within
                           //   trusted brand. Switching cost modelled as inertia on demand.
    necessity_buyer  = 3,  // Buys minimum required quantity regardless of price
                           //   (food, medicine, fuel). Near-zero price elasticity.
};
```

**Consumer demand formula (per good g, per province p, tick t):**

```
demand_contribution(npc, g) =
    good.base_consumer_demand_units
    × income_factor(npc, g)
    × price_factor(npc, g)
    × quality_factor(npc, g)

Where:
    income_factor(npc, g) =
        (npc.disposable_income / config.demand.reference_income)
        ^ good.income_elasticity
        // income_elasticity > 0 = normal good; < 0 = inferior good

    price_factor(npc, g) =
        (good.base_price / max(spot_price(g, p), 0.01))
        ^ |good.price_elasticity|
        // price_elasticity < 0 (elastic); near 0 (inelastic, e.g. necessity_buyer)
        // BuyerType modulates: necessity_buyer applies elasticity × 0.1
        //                      price_sensitive applies elasticity × 1.5
        //                      quality_seeker applies elasticity × 0.6
        //                      brand_loyal applies elasticity × 0.8

    quality_factor(npc, g) =
        1.0 + good.quality_weight × (batch_quality - market_quality_avg)
        // quality_weight = 0.0 for price_sensitive; 0.3 for brand_loyal;
        //                  0.6 for quality_seeker; 0.0 for necessity_buyer

demand_buffer_contribution(p, g, t) =
    Σ_significant_npc  [ demand_contribution(npc, g) × 1.0 ]
  + Σ_cohort           [ demand_contribution(cohort, g) × cohort.population_size ]
  // Background population cohorts use cohort-mean disposable_income
  // and the cohort's modal buyer_type for that good category.
  // Result is added to RegionalMarket(g, p).demand_buffer for tick t+1 price calc.
```

Each good in the goods data file carries: `base_consumer_demand_units`, `income_elasticity`, `price_elasticity`, `quality_weight`. `BuyerType` per NPC is set at world generation from their Background and Trait and updated quarterly.

**Named constants (`simulation_config.json` → `demand`):**

| Constant | Default | Effect |
|---|---|---|
| `reference_income` | 3,000.0 | Reference monthly income for income_factor normalisation |
| `max_income_factor` | 5.0 | Clamp on income_factor; prevents extreme-wealth NPCs from dominating demand |
| `min_price_factor` | 0.05 | Floor on price_factor; demand never collapses to zero even at very high prices |

### Supply Chain Propagation

Supply chain effects propagate in tick step 2. When a processing facility's input good is short, its output drops. When output drops, downstream facilities face input shortages next tick. This creates propagation waves that travel through the chain over multiple ticks — which is the correct simulation of how supply chain shocks behave in reality.

**[RISK]** Circular dependencies in the supply chain (e.g., energy needed to produce energy equipment) require careful initialization order and may need iterative solving rather than single-pass propagation.

### NPC Business Simulation

NPC businesses run simplified economics — no tile-level facility design, but real production, costs, and revenue:

```cpp
struct NPCBusiness {
    uint32_t id;
    BusinessSector sector;
    BusinessProfile profile;              // cost_cutter, quality_player, fast_expander, defensive_incumbent
    float cash;
    float revenue_per_tick;
    float cost_per_tick;
    float market_share;                   // in their regional sector
    uint32_t strategic_decision_tick;    // when they next make a quarterly decision; see dispatch_day_offset
    uint8_t  dispatch_day_offset;        // 0–29; set at world gen as hash(id) % 30
                                          // first decision at world_start_tick + offset; then every 30 ticks
                                          // spreads NPC business dispatch load evenly across each month
                                          // player-owned businesses dispatch on command; no offset
    ActorTechnologyState actor_tech_state; // per-actor technology portfolio (see R&D doc §Part 3.5)
                                           // replaces flat technology_tier float.
                                           // Effective tech tier is derived at recipe execution time:
                                           //   derived_tech_tier = max over active facilities of
                                           //   facility.tech_tier weighted by actor_tech_state.maturation_of(recipe.key_technology_node)
                                           // Quality ceiling computation uses both facility tier
                                           //   and actor maturation_level (see Commodities & Factories §Part 7)
    bool criminal_sector;                // true for businesses in criminal supply chain;
                                          // uses informal market price rather than formal spot price
    uint32_t province_id;                // province this business operates in; used for market lookup
    float regulatory_violation_severity;  // 0.0–1.0; legal-but-noncompliant ops
                                           // 0.0 = fully compliant (default)
                                           // 0.0–0.5 = noncompliant: generates
                                           //   evidence tokens, feeds
                                           //   RegulatorScrutinyMeter fill additive
                                           // 0.5–1.0 = severe: triggers
                                           //   enforcement_action consequence when
                                           //   scrutiny_meter crosses threshold
                                           // Does NOT change criminal_sector or
                                           //   market layer.
                                           // Populated by: labor disputes, safety
                                           //   incident consequences, audit findings,
                                           //   player/NPC decisions.
                                           // Decays at
                                           //   config.scrutiny.violation_decay_rate
                                           //   per tick when no new violations.
                                           // Field applies universally: NPC businesses
                                           //   and player-operated facilities carry
                                           //   identical semantics and decay rules.
    VisibilityScope default_activity_scope; // fallback scope applied to activities
                                            //   when no activity-level override is set.
                                            // Legitimate businesses: institutional
                                            //   (regulators can audit by default).
                                            // Criminal businesses (criminal_sector=true):
                                            //   concealed (all activity hidden by default;
                                            //   FacilitySignals govern physical leakage).
                                            // Individual activities may override:
                                            //   a legitimate business can conduct a
                                            //   concealed covert political donation;
                                            //   a criminal business can make a
                                            //   public_info charitable donation as cover.
};
```

Strategic decisions quarterly; behavioral profile determines intra-quarter behavior. This gives NPC businesses predictable but not scripted patterns — the player can learn a profile and exploit it.

### 5.1 — Player-Owned Business Architecture [V1]

**Canonical rule:** A player-owned business is an `NPCBusiness` record with `owner_id = player_id`. There is no separate `PlayerBusiness` struct. This is a direct consequence of the player-is-not-special principle: the player's restaurant, factory, or criminal distribution network is modeled identically to any NPC business. The field `owner_id` (added to `NPCBusiness` below) identifies the owning actor; `0` means no owner (independently operated NPC business), any other value is the NPC or player ID.

```cpp
// FIELD ADDITION to NPCBusiness:
uint32_t owner_id;  // 0 = independent NPC business (no player or parent-org owner)
                    // player_id = player-owned business
                    // npc_id    = NPC-owned subsidiary (e.g., criminal org front company)
                    // Ownership does not alter tick processing — same production,
                    // market, and scrutiny logic runs regardless of owner_id.
                    // Ownership determines: profit transfer (see below), acquisition
                    // eligibility, IPO listing authorization, and board composition.
```

**Dispatch rule:** NPC-owned businesses (`owner_id` is an NPC or 0) dispatch quarterly on their `dispatch_day_offset`. Player-owned businesses dispatch on player command — there is no scheduled decision; the player drives strategy directly. The `dispatch_day_offset` field on player-owned businesses is set to `0` and never consulted.

### 5.1a — Executive Compensation Model [V1]

**Design principle:** How a player (or NPC owner-operator) extracts value from a business they control is determined by the business's scale. Real-world mechanisms differ substantially between a sole-proprietor restaurant and a listed conglomerate — the extraction method, its visibility to investigators and tax authorities, the role of a board, and the timing of cash flows all change as scale increases. This system models those differences. No simplification to a single "profit sweep" — the player makes real strategic decisions about compensation structure, just as owners do in reality.

#### BusinessScale [V1]

Scale is derived from `monthly_revenue = npc_business.revenue_per_tick × TICKS_PER_MONTH` and recomputed monthly. Scale determines which compensation mechanisms are available.

**Scale classification stickiness (TDD-P3):** Business scale classification is STICKY: re-evaluated once per 30 ticks (monthly). Scale can only change by one level per evaluation. Hysteresis: downgrade threshold = 80% of upgrade threshold (prevents oscillation at boundaries).

```cpp
enum class BusinessScale : uint8_t {
    micro   = 0,  // monthly_revenue < config.business.scale_micro_ceiling
                   // Sole proprietor / small partnership equivalent.
                   // Single owner-operator. No formal separation of business
                   // and personal finances required.

    small   = 1,  // monthly_revenue < config.business.scale_small_ceiling
                   // Small LLC / S-corp equivalent.
                   // Formal salary recommended; distributions available.
                   // No mandatory board.

    medium  = 2,  // monthly_revenue < config.business.scale_medium_ceiling
                   // Private mid-market company.
                   // Salary + bonus + dividends. Board composition
                   // matters for compensation approval.

    large   = 3,  // monthly_revenue >= config.business.scale_medium_ceiling
                   // Large private or public company.
                   // Full executive compensation package. Board compensation
                   // committee approval required. Public disclosure if listed.
};
```

**Scale thresholds (`simulation_config.json → business`):**

| Constant | Default | Real-world equivalent |
|---|---|---|
| `scale_micro_ceiling` | 50,000 | ~$50k/month revenue |
| `scale_small_ceiling` | 500,000 | ~$500k/month revenue |
| `scale_medium_ceiling` | 5,000,000 | ~$5M/month revenue |

#### ExecutiveCompensation [V1]

Each `NPCBusiness` with an owner carries an `ExecutiveCompensation` record describing how the controlling actor extracts value. This is a **strategic decision** the player sets explicitly; NPC owner-operators set it via their motivation model at quarterly decision time.

```cpp
enum class CompensationMechanism : uint8_t {
    owners_draw    = 0,  // Informal draw: owner pulls cash directly from business account
                          // on demand. No formal salary recorded. All business profit
                          // is personal income. Available: micro only.
                          // Visibility: high — large irregular cash movements are
                          // a classic AML signal. VisibilityScope: internal → institutional
                          // once transaction volume exceeds reporting threshold.

    salary_only    = 1,  // Fixed per-tick salary as operating cost. Owner is formally
                          // an employee of their own company. Salary independent of profit.
                          // Unpaid if cash insufficient (deferred liability).
                          // Available: small, medium, large.
                          // Visibility: low — salary is normal payroll; unremarkable.

    salary_bonus   = 2,  // Salary + periodic performance bonus. Bonus computed quarterly
                          // or annually as a % of net profit above threshold.
                          // Available: small, medium, large.

    salary_dividend = 3, // Salary + dividends on ownership stake. Dividends paid
                          // from retained earnings, not operating cash flow.
                          // Requires: share structure on the business (public_float_pct > 0
                          // or explicit dividend_eligible_shares field).
                          // Board approval required at medium and large scale.
                          // Available: medium, large.

    full_package   = 4,  // Salary + bonus + equity compensation (options/RSUs vesting
                          // over time) + perks. Standard for large/public companies.
                          // Equity compensation creates a deferred wealth transfer
                          // (EquityGrant vesting schedule; see below).
                          // Board compensation committee approval required.
                          // Public disclosure required if business is listed (StockListing exists).
                          // Available: large only.
};

struct EquityGrant {
    uint32_t business_id;
    float    shares_granted;          // total shares in this grant
    float    shares_vested;           // accumulated; increases each tick by vesting_rate
    float    vesting_rate;            // shares_granted / vesting_period_ticks
    uint32_t grant_tick;
    uint32_t cliff_tick;              // no vesting before this tick (typical: 1 year = 365 ticks)
    uint32_t full_vest_tick;          // all shares vested by this tick (typical: 4 years)
    float    strike_price;            // for options: price at grant; 0.0 for RSUs
                                       // Vested RSUs transfer at current stock price on exercise.
                                       // Vested options: player commands exercise; profit =
                                       //   (current_price - strike_price) × shares_exercised.
};

struct ExecutiveCompensation {
    CompensationMechanism mechanism;

    // --- Salary component (salary_only, salary_bonus, salary_dividend, full_package) ---
    float salary_per_tick;            // recorded as cost_per_tick addition; paid each tick
                                       // if business.cash >= salary_per_tick; else deferred.
                                       // Player sets this. NPC owner sets at quarterly decision.
                                       // Reasonable salary constraint (small+): IRS-equivalent
                                       // rule — must be >= config.business.reasonable_salary_floor
                                       // × regional_wage_by_skill[Management] to avoid
                                       // regulatory scrutiny for distribution-only strategies.

    // --- Bonus component (salary_bonus, full_package) ---
    float bonus_rate;                 // fraction of quarterly net profit paid as bonus; 0.0–1.0
                                       // Net profit = revenue_per_tick × TICKS_PER_QUARTER
                                       //            - cost_per_tick × TICKS_PER_QUARTER
                                       //            - salary_per_tick × TICKS_PER_QUARTER
                                       // Bonus only paid if net profit > 0.
                                       // Medium/large: board approval required if bonus_rate
                                       // exceeds config.business.board_approval_bonus_threshold.

    // --- Distribution / dividend component (salary_dividend, full_package) ---
    float dividend_yield_target;      // fraction of retained earnings distributed per quarter
                                       // Retained earnings = accumulated net profit held in business.
                                       // Actual payout: min(retained_earnings × dividend_yield_target,
                                       //                    business.cash - working_capital_floor)
                                       // where working_capital_floor = cost_per_tick
                                       //                               × config.business.cash_surplus_months
                                       // Medium/large: board vote required (see Board Approval below).

    // --- Equity component (full_package only) ---
    std::vector<EquityGrant> equity_grants;
                                       // Grants accumulate over time. Player commands exercise
                                       // of vested options/RSUs via player action; not automatic.

    // --- Owners draw (micro only) ---
    // No struct field — player commands a draw as an on-demand action.
    // Each draw: player specifies amount; deducted from business.cash immediately;
    // added to player.wealth; generates a DeltaBuffer entry for both.
    // Draws above config.business.draw_reporting_threshold per month generate
    // a suspicious_transaction evidence token at VisibilityScope::institutional.
};
```

#### Compensation Execution — Tick Integration [V1]

| Event | Cadence | Mechanism |
|---|---|---|
| Salary payment | Every tick | `business.cash -= salary_per_tick; player.wealth += salary_per_tick` (if cash available; else deferred to `deferred_salary_liability`) |
| Bonus payment | Quarterly (tick step 28, post-income) | Computed from quarterly net profit × `bonus_rate`; transferred to `player.wealth` |
| Dividend payment | Quarterly (same tick step) | From retained earnings subject to board approval at medium/large; transferred to `player.wealth` |
| Equity vesting | Per tick | `equity_grant.shares_vested += vesting_rate` after cliff; exercised on player command |
| Owner's draw | On player command | Immediate transfer; evidence token generated above threshold |

**Deferred salary liability:** If `business.cash < salary_per_tick`, the salary is not paid that tick. Shortfall accumulates in `deferred_salary_liability: float` on the NPCBusiness. Deferred salary is paid first when cash recovers. Sustained deferred salary (> `config.business.deferred_salary_max_ticks` ticks) generates a `witnessed_wage_theft` memory entry on the player's own compensation record — same mechanic as unpaid worker wages. This is intentional: owner-managers who fail to pay their own declared salary have the same legal exposure as those who fail to pay workers.

#### Board Approval at Medium and Large Scale [V1]

At `medium` and `large` scale, some compensation decisions require board approval. The board is represented as a set of NPC relationships with defined composition:

```cpp
struct BoardComposition {
    std::vector<uint32_t> member_npc_ids;  // NPCs serving on the board
    float independence_score;              // 0.0–1.0; fraction of members not
                                            // personally obligated to the player.
                                            // 0.0 = fully captured board (rubber stamp).
                                            // 1.0 = fully independent board.
    uint32_t next_approval_tick;           // next quarterly board meeting
};
```

**Board approval wiring (TDD-P2):** Board approval evaluation is queued in tick step 27 (Regulatory Compliance) when `current_tick == BoardComposition.next_approval_tick`. The DeferredWorkQueue entry: `{ type: BOARD_REVIEW, entity: business_id, execute_tick: next_approval_tick }`.

**Approval logic (runs at quarterly board meeting tick):**

```
FOR each pending_compensation_decision:
    IF board.independence_score < config.business.board_rubber_stamp_threshold:
        // Captured board: auto-approves all decisions
        approve()
    ELSE:
        // Independent board: evaluates against market benchmarks
        benchmark_salary = regional_wage_by_skill[Management]
                          × config.business.ceo_salary_market_multiple[scale]
        excess_ratio = decision.salary_per_tick / benchmark_salary

        IF excess_ratio > config.business.board_challenge_threshold:
            // Board challenges: scene card fires (board_compensation_dispute)
            // Player must negotiate, accept reduction, or replace board members
            schedule_scene_card(type: board_compensation_dispute,
                                participants: [board_npc, player])
        ELSE:
            approve()
```

**CEO salary market multiples by scale (`simulation_config.json → business`):**

| Scale | `ceo_salary_market_multiple` | Rationale |
|---|---|---|
| small | 1.5× | Owner-manager earns ~50% above management wage |
| medium | 3.0× | Private company CEO at market rate |
| large | 6.0× | Large company CEO; board benchmarks against comp surveys |

**Board capture mechanics:** Player can increase board `independence_score` (by appointing independent directors — good governance signal, reduces scrutiny) or decrease it (by appointing loyal allies — cheaper, faster approvals, but higher regulatory and investor scrutiny at large scale). Board composition changes through player commands and NPC resignation events.

> Test: When an independent board evaluates executive compensation that exceeds COMPENSATION_BENCHMARK × 1.5, the board rejects the salary. Rejected salary creates deferred_salary_liability on the business. The affected NPC generates a wage_theft MemoryEntry with emotional_valence = -0.6.
>
> Seed setup: Business with BoardComposition containing 2 independent directors (out of 3 total). CEO NPC with proposed salary = 2.0 × COMPENSATION_BENCHMARK. Assert: board rejects at next quarterly review. deferred_salary_liability = proposed_salary - COMPENSATION_BENCHMARK. CEO NPC memory contains wage_theft entry.

#### Visibility and Evidence Generation [V1]

Compensation mechanism affects `VisibilityScope` of wealth transfers:

| Mechanism | Visibility | Evidence risk |
|---|---|---|
| `owners_draw` (above threshold) | `institutional` | Suspicious transaction token if > `draw_reporting_threshold` per month |
| `salary_only` | `public_info` (payroll records) | None under normal conditions |
| `salary_bonus` | `institutional` | Bonus disclosures required at medium+ scale |
| `salary_dividend` | `institutional` → `public_info` if listed | Dividend payments disclosed in filings |
| `full_package` (listed company) | `public_info` | Full compensation disclosure; journalists can read it |

**Implication:** A player extracting large sums via owner's draw from a nominally small business that is actually generating large criminal revenue creates a pattern detectable by the FIU. A player using a salary structure at a legitimate business generates minimal investigative signal. The compensation structure is itself an OPSEC decision.

#### Business Acquisition [V1 — thin model]

When an `NPCBusiness` enters `BANKRUPT` status (cash < 0 sustained for `config.business.bankruptcy_grace_ticks` ticks), it becomes acquirable. Acquisition price:

```
acquisition_price = max(
    npc_business.revenue_per_tick × TICKS_PER_MONTH × config.business.acquisition_revenue_multiple,
    0.0f  // bankrupt businesses may have negative book value; floor at 0
)
```

Player acquisition: player command → scene card negotiation overlay (may adjust price ±30%) → `owner_id` set to `player_id`, cash transferred from `player.wealth`. NPC-to-NPC acquisition: resolved in strategic decision matrix (§5) when `fast_expander` profile identifies bankrupt neighbour.

**Named constants (`simulation_config.json → business`):**

| Constant | Default | Effect |
|---|---|---|
| `scale_micro_ceiling` | 50,000 | Monthly revenue ceiling for micro scale |
| `scale_small_ceiling` | 500,000 | Monthly revenue ceiling for small scale |
| `scale_medium_ceiling` | 5,000,000 | Monthly revenue ceiling for medium scale |
| `reasonable_salary_floor` | 0.8 | Min salary as fraction of regional Management wage |
| `board_approval_bonus_threshold` | 0.30 | Bonus rate requiring board approval at medium/large |
| `board_rubber_stamp_threshold` | 0.25 | Board independence below this = auto-approval |
| `board_challenge_threshold` | 1.5 | Salary ratio above market that triggers dispute |
| `deferred_salary_max_ticks` | 30 | Ticks of unpaid salary before wage_theft memory |
| `draw_reporting_threshold` | 20,000 | Monthly owner's draw above this → evidence token |
| `acquisition_revenue_multiple` | 6.0 | Acquisition price = 6 months of revenue |
| `bankruptcy_grace_ticks` | 10 | Ticks of negative cash before BANKRUPT status |

### Technology Tier Efficiency Differential [V1]

Technology tier above a recipe's minimum produces measurable output and cost advantages. This differential drives the NPC business strategic decision to upgrade and is the player's upgrade ROI model.

```
Technology tier effect on production:
    actual_output = recipe_output_per_tick
        × (1.0 + TECH_TIER_OUTPUT_BONUS_PER_TIER × max(0, facility.tech_tier - recipe.min_tech_tier))

    actual_cost_per_unit = recipe_base_cost_per_unit
        × (1.0 - TECH_TIER_COST_REDUCTION_PER_TIER × max(0, facility.tech_tier - recipe.min_tech_tier))
```

**Named constants (both to `economy.json`):**

| Constant | Proposed default | Effect |
|---|---|---|
| `TECH_TIER_OUTPUT_BONUS_PER_TIER` | 0.08 | 8% more output per tier above recipe minimum |
| `TECH_TIER_COST_REDUCTION_PER_TIER` | 0.05 | 5% lower operating cost per tier above recipe minimum |

**Numeric example — Tier 3 facility running a `min_tier=2` recipe:**
```
output = recipe_output × (1.0 + 0.08 × max(0, 3 − 2)) = recipe_output × 1.08   // 8% more than Tier 2
cost   = recipe_cost  × (1.0 − 0.05 × max(0, 3 − 2)) = recipe_cost  × 0.95    // 5% cheaper than Tier 2
```

**Quality ceiling interaction:** The quality ceiling is computed separately from output volume:
```
quality_ceiling = TECH_QUALITY_CEILING_BASE + TECH_QUALITY_CEILING_STEP × (facility.tech_tier - recipe.min_tech_tier)
```
`TECH_QUALITY_CEILING_BASE` and `TECH_QUALITY_CEILING_STEP` are in `economy.json`. They are independent of the output/cost constants above — output bonus is about volume; quality ceiling is about the maximum achievable output quality grade.

**Maturation coupling (R&D lifecycle model):** For technology-intensive recipes, the quality ceiling computed above is further capped by `actor_tech_state.maturation_of(recipe.key_technology_node)`. The effective ceiling is `min(tier_ceiling, maturation_level)`. Commodity recipes with `key_technology_node = ""` are unaffected. See Commodities & Factories §Part 7 (`ComputeOutputQuality`) for the full three-step formula.

### NPC Business Strategic Decision Matrix [V1]

NPC businesses evaluate their strategic position quarterly (every `TICKS_PER_QUARTER` ticks). Behavioral profile determines which decision branch executes. This gives NPC businesses predictable but not scripted patterns — the player can learn a profile and anticipate it.

```
NPC Business Strategic Decision Logic (runs quarterly = every TICKS_PER_QUARTER ticks)

// cost_cutter profile:
IF business.cash < config.business.cash_critical_months × monthly_operating_costs:
    action = reduce_headcount(rate: 0.10)   // lay off 10% of workforce
ELSE IF competitor_avg_quality > business.output_quality + 0.2:
    action = reduce_input_quality()   // switch to cheaper inputs to maintain margin
ELSE IF business.market_share < config.exit_market_threshold (0.05):
    if random_float() < 0.3: action = exit_market()   // 30% chance per quarter
ELSE IF input_costs_rising AND cost_reduction_possible:
    action = renegotiate_supplier_contracts()
ELSE:
    action = maintain_current_operations()

// quality_player profile:
IF business.brand_equity.brand_rating < 0.6 AND business.cash > config.business.cash_comfortable_months × monthly_operating_costs:
    action = increase_quality_investment(spend_rate: 0.10)   // 10% more on R&D/inputs
ELSE IF competitor_price < business.price × 0.8:
    action = differentiate()   // increase marketing, not match price
ELSE IF province.conditions.economic_stress > 0.5:
    action = reduce_premium_inventory()   // shift to mid-tier offering in recession
ELSE IF business.market_share_premium_segment < 0.20:
    action = increase_marketing_spend()
ELSE:
    action = maintain_quality_investment()

// fast_expander profile:
IF business.cash > config.business.cash_comfortable_months × monthly_operating_costs:
    action = identify_and_enter_adjacent_market()
ELSE IF business.market_share < 0.30 AND expansion_capital_available:
    action = increase_capacity_investment()
ELSE IF credit_available AND expansion_return > config.expansion_return_threshold (0.15):
    action = take_leverage_for_expansion()   // creates ObligationNode if debt-financed
ELSE IF competitor_just_exited_market:
    action = acquire_their_supplier_relationships()
ELSE:
    action = continue_expansion_at_current_rate()

// defensive_incumbent profile:
IF new_entrant_detected_in_market:
    action = lobby_for_regulatory_barriers()   // creates political ObligationNode
ELSE IF business.market_share_erosion_rate > 0.05 per year:
    action = reduce_price_to_floor()
ELSE IF technology_gap > 1 tier AND new_competitor_using_advanced_tier:
    action = lobby_for_standards_that_disadvantage_new_tech()
ELSE IF business.cash > config.business.cash_surplus_months × monthly_operating_costs:
    action = pay_political_obligations()   // political retention over capability investment
ELSE:
    action = defend_existing_position()
```

**Named constants (`simulation_config.json` → `business`):**

| Constant | Default | Effect |
|---|---|---|
| `cash_critical_months` | 2.0 | Cash below this many months of operating costs triggers survival mode (headcount reduction, cost cutting). Used by cost_cutter profile and criminal org decision matrix. |
| `cash_comfortable_months` | 3.0 | Cash above this many months of operating costs indicates healthy reserves. Used by quality_player and fast_expander profiles as the threshold for reinvestment and expansion decisions. |
| `cash_surplus_months` | 5.0 | Cash above this many months of operating costs indicates surplus. Used by defensive_incumbent profile for political retention spending. |

**R&D investment rates by profile (default quarterly allocation):**

| Profile | R&D spend rate | Condition |
|---|---|---|
| `cost_cutter` | 0 | Never; capital preserved over capability |
| `quality_player` | 5–10% of cash | When `brand_equity.brand_rating < 0.8` |
| `fast_expander` | 3–5% of cash | When expansion is blocked by market or capital constraint |
| `defensive_incumbent` | 0 | Lobbying preferred over innovation |

**Note:** Full R&D investment decision matrix (per-domain spend allocation, patent strategy, NPC research project selection) is deferred to Pass 4, pending further R&D system definition.

---

## 6. Evidence and Consequence Systems [V1]

### Complete Enum: ConsequenceType

```cpp
enum class ConsequenceType : uint8_t {
    // --- NPC Actions ---
    npc_takes_action          = 0,   // Generic: an NPC executes a decision (files complaint,
                                      //   contacts a contact, changes employer). Payload contains
                                      //   action specification.
    npc_leaves_network        = 1,   // NPC removes themselves from player's contact list or
                                      //   organization. Triggered by low trust, high fear,
                                      //   or better opportunity.
    npc_becomes_hostile       = 2,   // NPC's relationship score crosses hostile threshold.
                                      //   Changes behavior toward player in all future
                                      //   interactions.

    // --- Evidence and Legal ---
    evidence_token_created    = 3,   // New EvidenceToken enters the world. Payload: token
                                      //   type, initial holder, subject.
    investigation_opens       = 4,   // Formal investigation created. InvestigatorMeter status
                                      //   transitions to formal_inquiry.
    legal_process_advances    = 5,   // A step in the legal process chain fires:
                                      //   charges_filed, arraignment, trial_begins,
                                      //   verdict, sentencing, appeal_outcome.
    evidence_suppressed       = 6,   // Evidence token actionability reduced to 0.0 by
                                      //   corrupted official. Token persists; is recoverable.
    warrant_issued            = 7,   // Law enforcement NPC gains legal access to financial
                                      //   records or facility search.

    // --- Obligation and Relationship ---
    obligation_called_in      = 8,   // Creditor NPC activates an ObligationNode demand.
                                      //   Payload: obligation_id, demand specification.
    obligation_escalated      = 9,   // Unpaid obligation escalates to next demand tier.
    relationship_changes      = 10,  // A relationship score shifts beyond a threshold:
                                      //   trust, fear, or obligation_balance change.
    favor_requested           = 11,  // NPC requests a favor (offer, not obligation yet).
                                      //   Player can accept (creates obligation) or decline.

    // --- Media and Public Exposure ---
    media_story_breaks        = 12,  // A journalist publishes a story. Evidence token
                                      //   actionability reduced; player's reputation
                                      //   moves in affected domain. Payload: story type,
                                      //   journalist_npc_id, publication reach.
    whistleblower_contacts_authority = 13, // Worker or NPC contacts regulator or journalist
                                            //   directly. Creates new evidence token.
    public_scandal_threshold  = 14,  // Cumulative exposure in a domain crosses the threshold
                                      //   where it becomes publicly toxic without media action.

    // --- Community and Political ---
    community_escalates       = 15,  // Community response stage advances. Payload:
                                      //   new_stage, region_id.
    opposition_org_formed     = 16,  // OppositionOrganization created in a region.
    political_approval_shift  = 17,  // A demographic's approval of the player moves
                                      //   significantly. Campaign or governing consequence.
    election_outcome          = 18,  // Election resolves. Payload: office_id, won/lost,
                                      //   vote_share.
    legislation_enacted       = 19,  // Enacted law triggers the policy consequence engine.
    political_crisis          = 20,  // Simulation-generated crisis enters the calendar.

    // --- Criminal and Physical ---
    criminal_retaliates       = 21,  // An NPC criminal org or criminal NPC takes retaliatory
                                      //   action against player. Payload: action type (economic,
                                      //   property, personnel) and subject.
    territory_conflict_stage  = 22,  // TerritorialConflictStage transitions. Payload:
                                      //   org_id, new_stage.
    raid_executed             = 23,  // Law enforcement raid on a player facility. Outcome:
                                      //   arrests, seizures, facility damage.
    arrest_warrant_issued     = 24,  // Personal arrest warrant for player character or
                                      //   key NPC. Scene card required for response.

    // --- Health, Aging, and Systemic ---
    health_event              = 25,  // Player character health event: illness, injury,
                                      //   stress threshold. May modify calendar capacity.
    scheduled_death           = 26,  // Terminal phase completion: player character death.
                                      //   Triggers succession system.
    npc_death                 = 27,  // An NPC dies (natural, violence, accident). Memory
                                      //   entries updating all NPCs who knew them.
    facility_incident         = 28,  // Random event in a facility (fire, explosion,
                                      //   accident). Payload: facility_id, incident_type,
                                      //   severity.

    // --- Random Events ---
    random_event_fires        = 29,  // Natural, accident, economic, or human random event.
                                      //   Payload: event_type, affected_region, magnitude.

    // --- System ---
    consequence_cancelled     = 30,  // A cancellable consequence was successfully cancelled.
                                      //   Retained for audit log purposes.
    consequence_chain         = 31,  // This entry triggers child entries: queues one or more
                                      //   additional ConsequenceEntry objects at a future tick.
};
```

### Evidence Token

```cpp
struct EvidenceToken {
    uint32_t id;
    EvidenceType type;                    // financial, testimonial, documentary, physical
    uint32_t subject_npc_id;             // who this evidence is against; player_id,
                                          // npc_id, or npc_business_id. The simulation
                                          // generates evidence against any actor whose
                                          // actions produce observable signals.
    uint32_t holder_npc_id;             // who currently holds / knows this
    uint32_t location_id;                // if physical
    float actionability;                 // how usable this evidence is (degrades if holder is discredited)
    bool player_aware;                   // is this on the player's evidence map?
    uint32_t created_tick;
    std::vector<uint32_t> secondary_holders; // NPCs who also know this evidence exists
};
```

Evidence tokens propagate through the NPC social graph when their holder shares them with contacts. The player sees tokens they created directly; secondary tokens (created by NPCs observing or documenting player actions) are invisible until discovered through intelligence investment.

**Evidence propagation is information, not physical.** When an NPC's behavior engine decides to share an evidence token (via `share_evidence` action), the target NPC's `known_evidence` map is updated in the same tick's DeltaBuffer. No transit delay. This is consistent with the physics principle (§18.14): evidence sharing happens by phone, message, or conversation — all information channels. The *physical* evidence artifact (a document, a sample, a recording) would require transit if it needed to move provinces, but the knowledge of what that evidence contains propagates instantly via communication.

### Evidence Actionability Decay [V1]

Evidence actionability degrades over time. A discredited holder causes faster decay, representing loss of investigative value when the source can no longer be trusted.

Decay runs as a **DeferredWorkQueue batch every 7 ticks per token** (`WorkType::evidence_decay_batch`). Each token has one scheduled batch item at all times; after execution it reschedules itself at `current_tick + 7`. This cuts evidence decay processing cost to 1/7th of per-tick polling with no meaningful accuracy loss — `base_decay_rate` is calibrated against ticks, so multiply by 7 when applying the batch.

**Per-batch decay amount (TDD-P4):** `actionability -= EVIDENCE_BATCH_DECAY_AMOUNT` (default: 0.014). Over 7 ticks this equals ~0.002/tick effective rate, consistent with GDD §10.2 NATURAL_DECAY_RATE.

```
DeferredWorkQueue fires WorkType::evidence_decay_batch for token T:

    if token.actionability <= config.actionability_floor:
        reschedule at current_tick + 7
        return   // already at floor

    holder = get_npc(token.primary_holder_id)
    is_credible = evaluate_credibility(holder)

    if is_credible:
        decay_this_batch = config.base_decay_rate × 7
    else:
        decay_this_batch = config.base_decay_rate × config.discredit_decay_multiplier × 7

    token.actionability = max(config.actionability_floor, token.actionability - decay_this_batch)

    reschedule at current_tick + 7
```

**Named constants (all to `investigation.json`):**

| Constant | Proposed default | Effect |
|---|---|---|
| `base_decay_rate` | 0.001 per tick | ~700 ticks (~2 in-game years) to halve from 1.0 |
| `discredit_decay_multiplier` | 5.0 | Discredited holder: half-life ~140 ticks (~5 months) |
| `actionability_floor` | 0.10 | Evidence never disappears; minimum floor; engine constant |
| `EVIDENCE_SHARE_TRUST_THRESHOLD` | 0.45 | Minimum trust level for an NPC contact to share an evidence token with the player. |

**NPC credibility evaluation — `is_credible = false` when ANY of:**
1. `holder.public_credibility < 0.3`
   // covers discredited sources regardless of cause: criminal history,
   // professional disgrace, prior false testimony, public scandal
2. NPC has memory entry with `type == credibility_damaged` AND `emotional_weight < -0.5`
3. NPC has memory entry with `type == accepted_player_payment` (implies compromised source)

`is_credible = true` otherwise.

### ConsequenceEntry

`ConsequenceEntry` is a payload type stored inside a `DeferredWorkItem`. It is not a separate queue — the unified `DeferredWorkQueue` (§3.3) handles all scheduling and firing. When a system queues a consequence, it pushes a `DeferredWorkItem { type: WorkType::consequence, due_tick: target_tick, payload: consequence_entry }`.

```cpp
struct ConsequenceEntry {
    ConsequenceType type;
    uint32_t source_npc_id;             // who triggered it
    ConsequencePayload payload;          // varies by ConsequenceType
    bool is_cancellable;                 // can player action prevent this?
    uint32_t cancellation_window_ticks; // ticks from now the player has to act
};
// ConsequenceEntry is the payload of DeferredWorkItem when WorkType == consequence.
// due_tick lives on DeferredWorkItem, not here. Do not add a target_tick field.
```

Each tick, `DeferredWorkQueue` fires all items with `due_tick <= current_tick`. For `WorkType::consequence` items, the consequence engine extracts the `ConsequenceEntry` payload and executes it. Cancellable entries check whether the cancellation condition has been met before executing.

**Consequences survive player character death.** Consequences queued during the player's lifetime continue firing after death. The heir inherits the `DeferredWorkQueue` intact — not a clean slate.

---

## 7. The Obligation Network [V1]

### Complete Enum: FavorType

```cpp
enum class FavorType : uint8_t {
    // --- Financial ---
    financial_loan            = 0,  // Loan extended when formal credit wasn't available.
                                     //   Creditor expects repayment plus future compliance.
    financial_contribution    = 1,  // Gift or contribution (campaign donation, personal
                                     //   gift, organizational funding). No repayment
                                     //   expectation; expectation of future alignment.
    business_opportunity      = 2,  // Introductions to contracts, clients, or deals
                                     //   that benefited the debtor's economic position.

    // --- Legal and Protective ---
    legal_defense             = 3,  // Arranged or funded legal defense. High-value favor.
                                     //   Creditor holds significant leverage.
    evidence_suppressed       = 4,  // Evidence token neutralized through corrupt official.
                                     //   Extremely high leverage — ongoing mutual incrimination.
    regulatory_intervention   = 5,  // Regulator directed to stand down, delay, or reduce
                                     //   scrutiny. Creates ongoing obligation to maintain.
    criminal_protection       = 6,  // Law enforcement tipped off to rivals, heat redirected.
                                     //   Player either provides this or is the recipient.
    physical_protection       = 7,  // Bodyguard service or threat neutralization.
                                     //   [EX — personal security system; V1 records obligation
                                     //   but behavior engine thin in V1.]

    // --- Career and Political ---
    career_advancement        = 8,  // Arranged promotion, reference, introduction that
                                     //   advanced debtor's career or professional standing.
    political_endorsement     = 9,  // Public or coalition endorsement during campaign.
                                     //   Tracked as coalition commitment obligation.
    appointment_facilitated   = 10, // Arranged government appointment for debtor.
                                     //   [EX — appointed roles are expansion; V1 placeholder.]
    vote_cast                 = 11, // NPC legislator voted as instructed. Single-use favor;
                                     //   each vote is a distinct obligation event.

    // --- Social and Information ---
    introduction_made         = 12, // Connected debtor to a contact they needed.
                                     //   Lower-value favor; repeated introductions accumulate.
    information_provided      = 13, // Shared sensitive, useful, or dangerous information.
                                     //   Value depends on exclusivity and actionability of
                                     //   the information.
    media_story_buried        = 14, // Editor or journalist killed or delayed a damaging story.
                                     //   High-value favor; evidence of capture if discovered.
    whistleblower_silenced    = 15, // Worker or source persuaded or pressured not to report.
                                     //   Creates ongoing mutual incrimination.
};
```

### ObligationNode

```cpp
struct ObligationNode {
    uint32_t creditor_npc_id;
    uint32_t favor_tick;                  // when the favor was received
    FavorType favor_type;
    float original_value;
    float current_demand;                 // escalates over time if unpaid
    ObligationStatus status;             // open, called_in, resolved, escalated
    std::vector<EscalationStep> history; // what they've asked for so far
};
```

**V1 scope note:** WorldState.obligation_network tracks only ObligationNodes
where the player is creditor or debtor. NPC-to-NPC obligations (a politician
who owes a criminal org, a banker leveraged by a corporate) exist in the
simulation world but are modeled implicitly through NPC motivation weights
and relationship trust values — they do not generate ObligationNode entries
in V1. The full NPC-to-NPC obligation graph is expansion scope. This is a
deliberate V1 constraint, not an architectural limitation.

Obligation escalation is driven by the creditor NPC's motivation model and the passage of time. A creditor with a high money motivation escalates toward financial demands. A creditor with a power motivation escalates toward political influence demands. The escalation path is not scripted — it emerges from motivation × time × the player's current resources (what looks achievable to ask for).

**Obligation Escalation Algorithm — Full Specification**

The escalation algorithm runs per tick for every open or escalated ObligationNode. It grows `current_demand` as a function of creditor motivation, time overdue, and player visible wealth.

```
Per tick, for each ObligationNode in player.obligation_network
where node.status in {open, escalated}:

    time_overdue = max(0, current_tick - node.deadline_tick)

    if time_overdue == 0:
        continue  // not yet overdue; no escalation this tick

    // Demand growth rate
    creditor = get_npc(node.creditor_id)
    dominant_motivation = creditor.motivation.highest_weight_type()
    creditor_urgency = creditor.motivation.weights[dominant_motivation]
                       // 0.0–1.0; high-motivation creditors escalate faster

    player_wealth_factor = min(
        player.visible_net_worth / config.wealth_reference_scale,
        config.max_wealth_factor  // wealthy players face steeper demands
    )

    demand_growth_per_tick = creditor_urgency
        × config.escalation_rate_base
        × (1.0 + player_wealth_factor)

    node.current_demand += demand_growth_per_tick

    // Status transition: open → escalated
    if node.current_demand > node.original_value × config.escalation_threshold:
        if node.status != escalated:
            node.status = escalated
            node.history.push({ tick: current_tick, type: first_escalation })
            schedule_scene_card(creditor, scene_type: obligation_escalation_demand)

    // Status transition: escalated → critical
    if node.current_demand > node.original_value × config.critical_threshold:
        if node.status != critical:
            node.status = critical
            node.history.push({ tick: current_tick, type: critical_escalation })
            schedule_consequence(creditor, ConsequenceType::obligation_creditor_unilateral_action)

    // Status transition: critical → hostile (risk-tolerance gated)
    if node.status == critical AND creditor.risk_tolerance > config.hostile_action_threshold:
        node.status = hostile
        // Creditor NPC selects hostile action via expected_value engine.
        // Context gates filter the candidate action set before expected_value
        // evaluation. A player with no street reputation cannot be subject to rival
        // criminal org contact. A player with no active businesses cannot be subject to
        // competitor contact. The creditor selects via expected_value from the remaining
        // valid candidates.
        // Available hostile action types:
        //   report_to_law_enforcement,   // always available
        //   expose_player,               // always available
        //   public_accusation,           // always available
        //   contact_rival_criminal_org,  // only if player.reputation.street > 0.0
        //                                // (player has criminal network exposure)
        //   contact_rival_competitor,    // only if player has active NPCBusiness
        //                                // operations; legitimate-path equivalent:
        //                                // tip a business competitor with damaging info
        trigger_creditor_hostile_action(creditor, player)
```

**Named constants (all in `simulation.json`):**

| Constant | Proposed default | Effect |
|---|---|---|
| `escalation_rate_base` | 0.002 per tick | Doubles demand in ~500 ticks (≈1.4 in-game years) |
| `escalation_threshold` | 1.5 | Demand reaches 1.5× original value → escalated status |
| `critical_threshold` | 3.0 | Demand reaches 3× original value → critical status |
| `hostile_action_threshold` | 0.7 | Creditor risk_tolerance above which hostile action triggers |
| `wealth_reference_scale` | 1,000,000 | $1M reference point for wealth factor calculation |
| `max_wealth_factor` | 2.0 | Cap on wealth scaling multiplier |

**Counter-escalation mechanic:** The player can reduce `current_demand` by making partial payments, fulfilling favors, or directly negotiating a new term via scene card interaction with the creditor. Partial payment reduces `current_demand` by the payment fraction of `original_value`. Successful renegotiation resets `current_demand` to the new agreed value and resets `deadline_tick`, returning status to `open`.

---

## 8. The Calendar and Scheduling System [V1]

### Calendar Entry

```cpp
struct CalendarEntry {
    uint32_t id;
    uint32_t start_tick;
    uint32_t duration_ticks;
    CalendarEntryType type;               // meeting, event, operation, deadline, personal
    uint32_t npc_id;                      // for meetings
    bool player_committed;               // false = invited but not yet accepted
    bool mandatory;                       // legal summons, etc.
    DeadlineConsequence deadline_consequence; // what happens if the player misses it
    uint32_t scene_card_id;              // which scene card to render on engagement
};
```

**DeadlineConsequence** — complete specification:

```cpp
struct DeadlineConsequence {
    float           relationship_penalty;       // applied immediately on miss to npc.relationships[player].trust; 0.0 if none
    bool            npc_initiative;             // if true, NPC acts unilaterally on deadline
    ConsequenceType consequence_type;           // consequence that fires
    float           consequence_severity;       // 0.0–1.0; input to consequence engine
    uint32_t        consequence_delay_ticks;    // delay after deadline before consequence fires
    std::string     default_outcome_description; // for UI / player information
};
```

**When deadline is missed — 4-step procedure:**
```
1. Apply deadline_consequence.relationship_penalty to npc.relationships[player].trust immediately

2. Queue ConsequenceEntry at (deadline_tick + consequence_delay_ticks):
   type     = deadline_consequence.consequence_type
   severity = deadline_consequence.consequence_severity

3. If deadline_consequence.npc_initiative == true:
       // NPC takes the action they were waiting for the player to take,
       // without player involvement; evaluated via NPC behavior engine.
       // NPC unilateral action types: file_complaint, contact_rival,
       //   report_to_regulator, publish_information, escalate_obligation
       queue_npc_unilateral_action(npc, action_type: action_npc_was_enabling)

4. Add to NPC memory log:
   type             = deadline_missed
   subject_id       = player
   emotional_weight = -(0.3 + deadline_consequence.relationship_penalty)
   // missed deadlines are remembered; relationship effect is durable across future interactions
```

When the player is in a committed calendar entry (start_tick to start_tick + duration_ticks), the simulation continues but the player's interaction is limited to that scene card's choices. Fast-forward is suppressed during mandatory entries.

### Scheduling Friction

When a player initiates contact with an NPC, the scheduling system queries the NPC's calendar for availability and returns the earliest available slot, respecting the NPC's own commitments and their relationship with the player (a low-trust NPC adds buffer time; a high-trust one finds time faster).

---

## 9. Scene Card System [V1]

Scene cards are the primary interface through which the player experiences NPC interactions. They are not 3D — they are **2D illustrated or rendered scenes** with atmospheric framing, NPC portrait, dialogue, and player choice options.

### Complete Enum: SceneSetting

```cpp
enum class SceneSetting : uint8_t {
    // --- Business Interiors ---
    boardroom               = 0,   // Corporate meeting room. Formal, observed, professional.
    private_office          = 1,   // One-on-one in a private office. More candid than boardroom.
    open_plan_office        = 2,   // Visible to coworkers. Limits conversation sensitivity.
    factory_floor           = 3,   // Industrial facility operations area. Loud, physical.
    warehouse               = 4,   // Storage facility, distribution point. Liminal, private.
    construction_site       = 5,   // Active construction. Casual, physically demanding.
    laboratory              = 6,   // Research or production lab. Clinical, specialized.
                                    //   Includes criminal lab settings.

    // --- Food and Hospitality ---
    restaurant              = 7,   // Semi-public dining. Moderate privacy. Good for
                                    //   relationship-building meetings.
    cafe                    = 8,   // Casual, public. Low sensitivity. Good for initial
                                    //   contacts and low-stakes exchanges.
    hotel_lobby             = 9,   // Public but transitional. Deniable meeting location.
    nightclub               = 10,  // Loud, crowded. Privacy through noise. Criminal
                                    //   contact venue. High visual atmosphere.

    // --- Government and Legal ---
    government_office       = 11,  // Official setting. Formal, on-record. Regulator,
                                    //   politician, or civil servant interaction.
    courthouse              = 12,  // Legal proceedings location. High stakes. Mandatory
                                    //   attendance possible.
    courtroom               = 13,  // Active trial or hearing. Structured, adversarial.
    police_station          = 14,  // Voluntary or compelled visit. Investigator interaction.
    prison_visiting         = 15,  // Contact with incarcerated NPC or player. Limited
                                    //   communication, observed.
    prison_cell             = 16,  // Player character incarcerated. Constrained operational
                                    //   state; scene cards limited to prison-available actions.

    // --- Public and Outdoor ---
    street_corner           = 17,  // Informal, urban. Criminal network access. Visible.
    public_park             = 18,  // Semi-private outdoor meeting. Surveillance-difficult
                                    //   in low-tech jurisdictions.
    parking_garage          = 19,  // Classic discreet meeting location. Poor lighting,
                                    //   private, physically exposed.
    political_rally         = 20,  // Public political event. Campaign setting.
                                    //   Crowd interaction implied.

    // --- Residential and Personal ---
    home_dining             = 21,  // Dinner at player or NPC home. High trust intimacy.
                                    //   Personal relationship signal.
    home_office             = 22,  // Player's personal working space. Personal,
                                    //   unobserved.
    hospital                = 23,  // Health event or visiting ill NPC. Emotionally weighted.

    // --- Remote Communication ---
    phone_call              = 24,  // Voice call. No visual. Deniable. Routine contact.
    video_call              = 25,  // Video link. Remote formal or semi-formal. Business
                                    //   and political use.

    // --- Transit and Transitional ---
    moving_vehicle          = 26,  // Car, train, plane. Private but transitional.
                                    //   Classic secure conversation setting.
};
```

### Scene Card Data Structure

```cpp
struct SceneCard {
    uint32_t id;
    SceneType type;                       // meeting, call, personal_event, news_notification
    SceneSetting setting;                 // see complete enum above
    uint32_t npc_id;                      // primary NPC
    std::vector<DialogueLine> dialogue;   // NPC says, driven by their state
    std::vector<PlayerChoice> choices;    // player options, each with consequence payload
    float npc_presentation_state;        // 0.0 (hostile/closed) to 1.0 (open/cooperative)
                                          // drives visual state of NPC portrait
};
```

`npc_presentation_state` is derived from the NPC's relationship score with the player and their current risk tolerance — a hostile NPC shows differently from a cooperative one. This is the visual feedback for relationship quality without any explicit relationship meter.

### Scene Card Generation vs. Authored Cards

**Some scene cards are procedurally generated** from NPC state — a meeting card for any NPC is generated from their current knowledge, motivation, and relationship with the player. The dialogue is templated and filled from simulation state.

**Some scene cards are authored** — specific high-significance interactions (first meeting with a category of contact, major plot-adjacent moments like an NPC approaching to threaten exposure) benefit from authored dialogue quality. These should be prioritized for authoring where the interaction type is common and high-stakes.

**[RISK]** Fully procedural dialogue generation at quality is a hard problem. Recommend: templated dialogue with authored variations keyed to NPC state, rather than pure generation. Reserve authored content for high-frequency, high-stakes scene types.

---

## 10. WorldState and DeltaBuffer [CORE]

WorldState is the top-level container passed to every tick module. Every module reads from WorldState; no module writes directly to it. Proposed changes are staged in DeltaBuffer and applied to WorldState between tick steps (see Section 3).

### WorldState

```cpp
struct WorldState {
    uint32_t current_tick;                          // absolute tick counter; monotonically increasing
    uint64_t world_seed;                            // determinism anchor; used by all RNG calls

    // --- Geography ---
    std::vector<Nation> nations;                    // V1: exactly 1 nation
    std::vector<Province> provinces;               // replaces regions; see Section 12
    std::vector<Region> region_groups;             // thin grouping layer; see Section 12

    // --- NPC Population ---
    std::vector<NPC> significant_npcs;              // full model; see Section 4
    std::vector<NPC> named_background_npcs;         // simplified model; same struct, LOD flag set
    // Background population is not stored per-individual; it is aggregated in Region.cohort_stats

    // --- Player ---
    PlayerCharacter player;                         // see Section 11

    // --- Economy ---
    std::vector<RegionalMarket> regional_markets;   // one entry per (good_id × province_id); see Section 5
    std::vector<NPCBusiness> npc_businesses;        // see Section 5

    // --- Technology State ---
    GlobalTechnologyState global_technology_state;  // see R&D doc §Part 9
                                                    // tracks first_researched_by, first_commercialized_by,
                                                    // domain_knowledge_levels, active_patents,
                                                    // current_era, global_co2_index, climate_state.
                                                    // Per-actor holdings live in NPCBusiness.actor_tech_state.
                                                    // Updated by tick step 26 alongside evidence notifications.

    // --- Evidence ---
    std::vector<EvidenceToken> evidence_pool;       // all active tokens in the world; see Section 6

    // --- Deferred Work Queue (unified) ---
    // Holds: consequences, transit arrivals, interception checks, relationship decay batches,
    //        evidence decay batches, NPC business decisions, market recomputes,
    //        investigator meter updates, climate downstream batches, background work.
    // Min-heap sorted on due_tick. All systems write to this; Step 2 drains it each tick.
    std::priority_queue<DeferredWorkItem,
                        std::vector<DeferredWorkItem>,
                        DeferredWorkComparator> deferred_work_queue;  // see Section 3.3

    // --- Obligation Network ---
    std::vector<ObligationNode> obligation_network; // all open obligation nodes; see Section 7

    // --- Scheduling ---
    std::vector<CalendarEntry> calendar;            // merged calendar: player + NPC commitments
                                                    // filtered per-owner at read time

    // --- Scene Cards ---
    std::vector<SceneCard> pending_scene_cards;     // generated this tick, awaiting UI delivery

    // --- Global Tick Metadata ---
    uint32_t ticks_this_session;                    // monotonic counter reset on load; for WAL
    GameMode game_mode;                             // ironman or standard; set at game creation; immutable

    // --- Trade and Transport Infrastructure ---
    std::vector<TariffSchedule>         tariff_schedules;        // one per nation; see Section 18
    std::vector<NationalTradeOffer>     lod1_trade_offers;       // regenerated monthly; see Section 18
    GlobalCommodityPriceIndex           lod2_price_index;        // updated annually; see Section 18
    std::map<uint32_t, Lod1NationStats> lod1_national_stats;     // nation_id → stats; LOD 1 monthly → LOD 2 annual
    std::map<std::pair<uint32_t,uint32_t>,
             std::array<RouteProfile, 5>> province_route_table;  // precomputed at load; see Section 18.8
    uint32_t current_schema_version;                             // for migration validation on load; see Section 22
    bool network_health_dirty;                                   // set by any delta touching relationships/obligations/movement; cleared by §13 module after recompute

    // --- Cross-Province Delta Buffer ---
    CrossProvinceDeltaBuffer cross_province_delta_buffer;        // scratch; cleared each tick; not persisted
                                                                 // always empty at save time (flushed at end of each tick)
};
```

**Read access pattern:** Modules receive a const reference to WorldState and a mutable reference to DeltaBuffer. WorldState is never modified mid-tick. This enforces causality: Step 7 (NPC behavior) reads the same market prices that Step 5 wrote to DeltaBuffer and that DeltaBuffer applied before Step 7 ran.

**[RISK]** At full V1 scale (~2,000 significant NPCs, 4 regions, ~50 goods per region), WorldState occupies an estimated 10–15MB in memory. Passing by reference is required; no tick module receives a copy.

### DeltaBuffer

DeltaBuffer stages every proposed change from tick modules. Between each tick step, the engine applies pending DeltaBuffer entries to WorldState in the order they were written, then clears those entries before the next step runs.

```cpp
// --- NPC deltas ---
struct NPCDelta {
    uint32_t npc_id;
    std::optional<float> capital_delta;             // additive
    std::optional<NPCStatus> new_status;            // replacement
    std::optional<MemoryEntry> new_memory_entry;    // appended to memory_log
    std::optional<Relationship> updated_relationship; // upsert by target_npc_id
    std::optional<float> motivation_delta;          // additive to specific motivation slot
};

// --- Player deltas ---
struct PlayerDelta {
    std::optional<float> health_delta;              // additive
    std::optional<float> wealth_delta;              // additive; liquid cash only
    std::optional<SkillDelta> skill_delta;          // skill_id + additive value
    std::optional<uint32_t> new_evidence_awareness; // evidence token id added to awareness map
    std::optional<float> exhaustion_delta;          // additive
    std::optional<RelationshipDelta> relationship_delta; // player ↔ NPC relationship update
};

// --- Economy deltas ---
struct MarketDelta {
    uint32_t good_id;
    uint32_t region_id;
    std::optional<float> supply_delta;              // additive
    std::optional<float> demand_buffer_delta;       // additive; written by Step 17
    std::optional<float> spot_price_override;       // replacement; set by Step 5
    std::optional<float> equilibrium_price_override; // replacement; set by Step 5
};

// --- Evidence and consequence deltas ---
struct EvidenceDelta {
    std::optional<EvidenceToken> new_token;         // appended to evidence_pool
    std::optional<uint32_t> retired_token_id;       // removed from active pool
    std::optional<float> actionability_delta;       // additive to existing token
};

struct ConsequenceDelta {
    std::optional<ConsequenceEntry> new_entry;      // appended to consequence_queue
    std::optional<uint32_t> cancelled_entry_id;     // removed from queue
};

// --- Region deltas ---
struct RegionDelta {
    uint32_t region_id;
    std::optional<float> stability_delta;           // additive
    std::optional<float> inequality_delta;          // additive
    std::optional<float> crime_rate_delta;          // additive
    std::optional<float> addiction_rate_delta;      // additive
    std::optional<float> criminal_dominance_delta;  // additive
    std::optional<float> cohesion_delta;            // additive
    std::optional<float> grievance_delta;           // additive
    std::optional<float> institutional_trust_delta; // additive
};

// --- Top-level DeltaBuffer ---
struct DeltaBuffer {
    std::vector<NPCDelta> npc_deltas;
    PlayerDelta player_delta;
    std::vector<MarketDelta> market_deltas;
    std::vector<EvidenceDelta> evidence_deltas;
    std::vector<ConsequenceDelta> consequence_deltas;
    std::vector<RegionDelta> region_deltas;
    std::vector<CalendarEntry> new_calendar_entries;
    std::vector<SceneCard> new_scene_cards;
    std::vector<ObligationNode> new_obligation_nodes;
};
```

**Application rule:** Additive deltas are summed and clamped to their domain range before application (e.g., health is clamped to [0.0, 1.0]; trust is clamped to [-1.0, 1.0]). Replacement fields overwrite in write order — if two modules in the same tick step write an override to the same field, the last write wins. No two tick modules in the same step should write replacement overrides to the same field. Violations are a logic error.

**[RISK]** DeltaBuffer accumulation per tick step at 2,000 NPCs (worst case: Step 7, NPC behavior) is ~2,000 NPCDelta entries. Memory allocation for DeltaBuffer should be pre-reserved at WorldState initialization using the known NPC count.

### CrossProvinceDeltaBuffer [CORE]

```cpp
// Entries written by province workers when an action targets a different province.
// Processed sequentially by the main thread after all province workers complete.
// All effects land at the START of the following tick — one-tick propagation delay.

struct CrossProvinceDelta {
    uint32_t source_province_id;     // province that generated this delta
    uint32_t target_province_id;     // province that receives the effect
    uint32_t due_tick;               // current_tick + 1; applied at tick start

    // Delta payload — exactly one of these is populated per entry:
    std::optional<NPCDelta>              npc_delta;           // NPC in target province affected
    std::optional<DeferredWorkItem>      deferred_work_item;  // criminal shipment dispatch, travel arrival
    std::optional<EvidenceToken>         evidence_token;      // media story propagation, evidence sharing
    std::optional<RegionalMarketDelta>   market_delta;        // price signal or supply delta
};

struct CrossProvinceDeltaBuffer {
    std::vector<CrossProvinceDelta> entries;

    // Written by province workers during parallel execution.
    // Thread-safe: each province worker appends to its own partition;
    // main thread merges after join. No concurrent writes to the same partition.
    void push(CrossProvinceDelta delta);

    // Called by main thread after all workers complete.
    // Converts each entry into a DeferredWorkItem with due_tick = current_tick + 1
    // and pushes to WorldState.deferred_work_queue.
    void flush_to_deferred_queue(WorldState& world_state);
};
```

---

## 11. PlayerCharacter [V1]

The player character is a simulation agent with a richer data model than a significant NPC. The character's stats, health state, skills, and relationship data are full simulation objects that the tick advances each day, independent of player input.

```cpp
// --- Skill domains (GDD Section 4) ---
enum class SkillDomain : uint8_t {
    Business             = 0,
    Finance              = 1,
    Engineering          = 2,
    Politics             = 3,
    Management           = 4,
    Trade                = 5,
    Intelligence         = 6,
    Persuasion           = 7,
    CriminalOperations   = 8,
    UndercoverInfiltration = 9,   // [EX] — see Feature Tier List
    SpecialtyCulinary    = 10,
    SpecialtyChemistry   = 11,
    SpecialtyCoding      = 12,
    SpecialtyAgriculture = 13,
    SpecialtyConstruction = 14
};

enum class Background : uint8_t {
    BornPoor      = 0,
    WorkingClass  = 1,
    MiddleClass   = 2,
    Wealthy       = 3
};

enum class Trait : uint8_t {
    Charismatic       = 0,
    Analytical        = 1,
    Ruthless          = 2,
    Cautious          = 3,
    Creative          = 4,
    PhysicallyRobust  = 5,
    PoliticallyAstute = 6,
    StreetSmart       = 7
};

struct PlayerSkill {
    SkillDomain domain;
    float level;                  // 0.0–1.0; leveled through use
    float decay_rate;             // per-tick reduction when domain not exercised
                                  // default: 0.0002 per tick (~7% per in-game year of neglect)
    uint32_t last_exercise_tick;  // used to compute accumulated decay between exercises
};

struct ReputationState {
    float public_business;        // -1.0 to 1.0
    float public_political;       // -1.0 to 1.0
    float public_social;          // -1.0 to 1.0
    float street;                 // -1.0 to 1.0; see criminal entry conditions (GDD §12.1)
                                  //   0.0 at character creation; built through criminal network activity
};

struct HealthState {
    float current_health;         // 0.0–1.0; 0.0 = death
    float lifespan_projection;    // projected in-game years remaining; recalculated each tick
    float base_lifespan;          // set at character creation; 70.0–80.0 in-game years + trait modifier
    float exhaustion_accumulator; // 0.0–1.0; above 0.7: performance penalty on high-stakes engagements
    float degradation_rate;       // composite: base age rate + violence damage + substance use modifier
};

// Exposure is NOT a stat. It is the set of evidence tokens the player is currently aware of.
struct EvidenceAwarenessEntry {
    uint32_t token_id;            // references EvidenceToken in WorldState.evidence_pool
    uint32_t discovery_tick;
    uint32_t source_npc_id;       // 0 = discovered directly
};

struct PlayerCharacter {
    uint32_t id;

    // Character creation outputs
    Background background;
    std::array<Trait, 3> traits;
    uint32_t starting_province_id;

    // Stats
    HealthState health;
    float age;                    // in-game years; increments each tick by (1.0 / 365.0)
    ReputationState reputation;
    float wealth;                 // liquid cash; can be negative (debt)
    float net_assets;             // derived; not authoritative for transactions

    // Skills
    std::vector<PlayerSkill> skills; // one entry per SkillDomain

    // Evidence awareness (Exposure)
    std::vector<EvidenceAwarenessEntry> evidence_awareness_map;

    // Obligation network
    std::vector<uint32_t> obligation_node_ids;

    // Scheduling
    std::vector<uint32_t> calendar_entry_ids;

    // Personal life
    uint32_t residence_id;
    uint32_t partner_npc_id;      // 0 = no current partner
    std::vector<uint32_t> children_npc_ids;
    uint32_t designated_heir_npc_id; // 0 = no heir established

    // Relationships
    std::vector<Relationship> relationships; // player's directed view of all NPCs

    // Influence network summary (computed by tick step 22)
    InfluenceNetworkHealth network_health;
    uint32_t movement_follower_count;

    // Milestone tracking (see "Organic Milestone Tracking" subsection below)
    std::vector<MilestoneRecord> milestone_log;      // chronological; populated by tick step 27
    std::set<MilestoneType>      achieved_milestones; // O(1) lookup; prevents duplicate entries

    // Physical location — obeys physics principle (§18.14)
    uint32_t        home_province_id;             // player's base province
    uint32_t        current_province_id;          // where player physically is this tick
    NPCTravelStatus travel_status;                // resident, in_transit, visiting (see §18.15)
    // Player can only take actions requiring physical presence in current_province_id.
    // Calendar engagements in other provinces are only reachable if travel completes before due_tick.

    RestorationHistory              restoration_history;      // see Section 22a
    std::vector<TimeBoundedModifier> calendar_capacity_modifiers; // time-bounded overlays on calendar
    bool                            ironman_eligible;         // true if game_mode == ironman AND restoration_count == 0
};
```

### Evidence Awareness Update

The player's `evidence_awareness_map` is updated by tick step 26 (device notifications). When an NPC contact with trust > `EVIDENCE_SHARE_TRUST_THRESHOLD` (= 0.45) learns of a new evidence token during tick step 11, a ConsequenceDelta schedules a device notification at `current_tick + contact_delay_ticks`. Contact delay is derived from the contact NPC's trust — higher trust = shorter delay. Tokens the player is unaware of remain only in the evidence_pool. This is the operational definition of "the player doesn't know about it."

### Skill Decay

Each tick, for every PlayerSkill where `current_tick - last_exercise_tick > SKILL_DECAY_GRACE_PERIOD` (= 30 ticks, ~one in-game month), level decays by `decay_rate`. Minimum floor: `SKILL_DOMAIN_FLOOR` (= 0.05). Using a skill sets `last_exercise_tick` to current_tick and applies a level gain proportional to the difficulty of the engagement.

**Named constants (all to `simulation.json`):**

| Constant | Default | Effect |
|---|---|---|
| `SKILL_DECAY_GRACE_PERIOD` | 30 | Ticks of inactivity before skill decay begins (~1 in-game month). |
| `SKILL_DOMAIN_FLOOR` | 0.05 | Minimum skill level; decay never reduces below this value. |

### Organic Milestone Tracking [V1]

Milestones are never announced during play. They are silently recorded as the player reaches them and appear retrospectively in the character legacy view and heir inheritance screen. The player discovers them; the game never points at them.

```cpp
enum class MilestoneType : uint8_t {
    first_profitable_business,
    first_arrest,
    first_acquittal,
    first_bribe_given,
    first_cover_up,
    first_whistleblower_silenced,
    first_election_won,
    first_election_lost,
    first_law_passed,
    first_cartel_established,
    first_union_formed,
    first_major_rival_eliminated,
    first_bankruptcy,
    first_political_office,
    first_major_asset_acquired,
    first_heir_created,
    first_generational_handoff,
    net_worth_1m,
    net_worth_10m,
    net_worth_100m,
    // Expansion milestones omitted; [EX] scope
};

struct MilestoneRecord {
    MilestoneType type;
    uint32_t      achieved_tick;
    std::string   context_summary;    // generated at achievement; e.g., "Valdoria Iron Works, Year 2003"
};

// Timeline restoration modifier — time-bounded calendar capacity penalty
struct TimeBoundedModifier {
    float         delta;          // e.g., -1 calendar slot/day; negative
    uint32_t      expires_tick;
    ModifierSource source;        // disruption, health_event, imprisonment, etc.
};

// Restoration record for one timeline restoration event
struct TimelineRestorationRecord {
    uint32_t restoration_index;   // 1-based
    uint32_t restored_to_tick;    // snapshot tick that was loaded
    uint32_t restoration_real_tick; // tick the player was at when they restored
    uint32_t ticks_erased;        // restored_real_tick - restored_to_tick
    uint8_t  tier_applied;        // 1, 2, or 3
};

struct RestorationHistory {
    uint32_t restoration_count;                         // 0 in ironman mode; increments on each restoration
    std::vector<TimelineRestorationRecord> records;
};
```

**Milestone evaluation — tick step 27:**
```
// In tick step 27 (world state update), after all other state changes are applied:
for each MilestoneType not in player.achieved_milestones:
    if milestone_condition_met(type, player, world_state):
        record = MilestoneRecord{ type, current_tick, generate_context(type, player) }
        player.milestone_log.push(record)
        player.achieved_milestones.insert(type)
        // No announcement. Milestone is silently recorded for legacy view.
```

**Design principle:** No notification fires. No achievement popup. No journal entry. The simulation records what happened; the player finds it when they look back. This preserves the emergent-narrative feel — the player's own actions acquire meaning retrospectively, not because the game validated them in the moment.

---

## 12. Province and Nation [V1]

### WorldLoadParameters

```cpp
struct WorldLoadParameters {
    std::string world_file;          // path to base_world.json (required)
    std::string scenario_file;       // path to .scenario file (required)
    uint64_t    random_seed;         // deterministic seed for stochastic elements
    bool        debug_all_lod0;      // force all provinces to LOD 0; debug mode only
};
```

### H3Index [V1]

```cpp
// Uber H3 hexagonal spatial index. 64-bit cell identifier encoding resolution
// and position on the globe. Resolution 4 = province scale (~1,770 km²);
// resolution 9 = facility placement (~0.1 km²).
// Parent/child/neighbor relationships are O(1) from the index — no extra storage.
// Named constants (simulation_config.json → world):
//   h3_province_resolution  = 4   // Province-scale cells; ~1,770 km² avg area
//   h3_facility_resolution  = 9   // Facility placement precision; ~0.1 km² avg area
using H3Index = uint64_t;
```

### LinkType [V1]

```cpp
enum class LinkType : uint8_t {
    Land      = 0,   // shared land border
    Maritime  = 1,   // sea or ocean crossing; no shared land border
    River     = 2    // navigable river corridor; distinct transit cost profile
};
```

### ProvinceLink [V1]

```cpp
// Rich adjacency descriptor. Replaces bare adjacent_province_ids on the Province struct.
// Encodes how two provinces connect and the cost of transiting between them.
struct ProvinceLink {
    H3Index  neighbor_h3;           // H3 res-4 cell index of the neighboring province
    LinkType type;
    float    shared_border_km;      // 0.0 for Maritime links (no physical border)
    float    transit_terrain_cost;  // 0.0–1.0; 0.0 = highway flat land; 1.0 = mountain or swamp
    float    infrastructure_bonus;  // 0.0–1.0; road/rail quality; reduces effective transit cost
};
```

### WorldGenParameters [V1]

```cpp
// Input parameters for the procedural world generation pipeline (WorldGen v0.17 §Pipeline).
// Passed to WorldGenPipeline::run() at new-game creation; not present at runtime.
// Written by the scenario editor; consumed once before WorldLoadParameters takes over.
// Named constants (simulation_config.json → worldgen):
//   worldgen.v1_target_province_count   = 6
//   worldgen.resource_richness_min      = 0.5
//   worldgen.resource_richness_max      = 2.0
//   worldgen.climate_volatility_min     = 0.5
//   worldgen.climate_volatility_max     = 2.0
struct WorldGenParameters {
    uint64_t     seed;                        // deterministic RNG seed for all 11 pipeline stages
    uint8_t      target_province_count;       // V1 target: 6
    float        resource_richness;           // 0.5–2.0; global multiplier on deposit quantity seeding
    float        climate_volatility;          // 0.5–2.0; precipitation and temperature variance multiplier
    float        corruption_baseline;         // 0.0–1.0; Province.political.corruption floor at world load
    float        criminal_activity_baseline;  // 0.0–1.0; informal market share floor at world load
    std::string  output_world_file;           // path for generated world.json; consumed by WorldLoadParameters.world_file
};
```

### ResourceDeposit

```cpp
enum class ResourceType : uint8_t {
    IronOre = 0, Copper, Bauxite, Lithium, Coal, CrudeOil, NaturalGas, LimestoneSilica,
    Wheat, Corn, Soybeans, Cotton, Timber, Fish,
    SolarPotential, WindPotential
    // [EX] full deposit list from GDD Section 8.3 expanded in later passes
};

struct ResourceDeposit {
    uint32_t id;
    ResourceType type;
    float quantity;
    float quality;                // 0.0–1.0; affects processing conversion efficiency
    float depth;                  // 0.0–1.0; affects extraction cost
    float accessibility;          // 0.0–1.0; infrastructure requirement before extraction is viable.
                                  // For permafrost-locked deposits (ResourceType::CrudeOil or
                                  // NaturalGas in provinces with KoppenZone ET or EF, or
                                  // GeographyProfile.latitude > config.resources.arctic_latitude_threshold):
                                  //   accessibility is overridden to 0.0 until BOTH conditions hold:
                                  //     (1) province.climate.climate_stress_current
                                  //             > config.resources.permafrost_thaw_threshold
                                  //     (2) actor_tech_state.has_researched("arctic_drilling") == true
                                  //   When both conditions are met, accessibility is restored to its
                                  //   seeded base value. Evaluated each tick in tick step 1 (production)
                                  //   before extraction output is computed.
                                  //   Constants in resources.json:
                                  //     permafrost_thaw_threshold = 0.40  // climate_stress_current 0.0–1.0 scale
                                  //     arctic_latitude_threshold  = 66.5  // degrees; Arctic Circle
    float depletion_rate;         // fraction depleted per tick at full production
    float quantity_remaining;
};
```

### Climate and Geography Enums and Structs

```cpp
enum class KoppenZone : uint8_t {
    Af = 0, Am = 1, Aw = 2,           // Tropical
    BWh = 3, BWk = 4, BSh = 5, BSk = 6, // Arid
    Cfa = 7, Cfb = 8, Cfc = 9,        // Temperate/oceanic
    Csa = 10, Csb = 11, Cwa = 12,     // Mediterranean/subtropical
    Dfa = 13, Dfb = 14, Dfc = 15, Dfd = 16, // Continental
    ET = 17, EF = 18,                  // Polar
};

enum class SimulationLOD : uint8_t {
    full        = 0,   // 27-step tick; full NPC model; detailed market clearing
    simplified  = 1,   // Monthly LOD 1 update; archetype NPCs; aggregated markets
    statistical = 2,   // Annual batch update only; contributes to price index
};

struct GeographyProfile {
    float      latitude;
    float      longitude;
    float      elevation_avg_m;
    float      terrain_roughness;        // 0.0 (flat) to 1.0 (mountainous)
    float      forest_coverage;          // 0.0–1.0; from ESA WorldCover
    float      arable_land_fraction;     // 0.0–1.0; from FAO GAEZ
    float      coastal_length_km;
    bool       is_landlocked;
    float      port_capacity;            // 0.0 (none) to 1.0 (major port)
    float      river_access;             // 0.0–1.0; navigable river density
    float      area_km2;
};

struct ClimateProfile {
    KoppenZone koppen_zone;
    float      temperature_avg_c;
    float      temperature_min_c;
    float      temperature_max_c;
    float      precipitation_mm;
    float      precipitation_seasonality;
    float      drought_vulnerability;    // 0.0–1.0
    float      flood_vulnerability;      // 0.0–1.0
    float      wildfire_vulnerability;   // 0.0–1.0
    float      climate_stress_current;   // runtime; accumulation updated each tick (tiny delta per tick)
                                          // downstream effects (agricultural_productivity, community_state)
                                          // batched every 7 ticks via DeferredWorkQueue (WorkType::climate_downstream_batch)
};
```

### Province

```cpp
struct Province {
    // Identity
    H3Index     h3_index;                // canonical spatial identifier (H3 res 4); external-stable across
                                          // saves and used by WorldGen pipeline; O(1) neighbor/parent queries.
                                          // Distinct from id: id is the runtime array index in
                                          // WorldState.provinces for O(1) in-simulation lookups.
    uint32_t    id;
    std::string fictional_name;
    std::string real_world_reference;    // pipeline internal; in world.json; not shown in UI

    // Geography
    GeographyProfile  geography;
    ClimateProfile    climate;

    // Resources
    std::vector<ResourceDeposit> deposits;  // seeded from USGS data at world load

    // Economy
    RegionDemographics demographics;
    float infrastructure_rating;         // 0.0–1.0; from OSM road/rail density
    float agricultural_productivity;     // 0.0–1.0; runtime: modified by climate_stress
    float energy_cost_baseline;
    float trade_openness;                // 0.0–1.0; affects LOD 1 trade offer generation

    // Simulation
    SimulationLOD     lod_level;         // set by scenario file at load
    CommunityState    community;
    RegionalPoliticalState political;
    RegionConditions  conditions;

    // NPCs (LOD 0 only; empty at LOD 1/2)
    std::vector<uint32_t> significant_npc_ids;
    RegionCohortStats     cohort_stats;   // aggregated; all LOD levels

    // Terrain modifiers (set by GIS pipeline from world.json at load; static at runtime)
    bool     has_karst;                  // true = province contains karst terrain (cave systems,
                                          // sinkholes, underground drainage). Load-bearing for V1
                                          // criminal systems (WorldGen v0.17 §Stage 7).
                                          // Effects:
                                          //   route concealment: RouteProfile.concealment_bonus += config.routes.karst_concealment_bonus
                                          //     for routes traversing this province.
                                          //   facility signals: FacilitySignals.scrutiny_mitigation += config.opsec.karst_mitigation_bonus
                                          //     for underground or partially-concealed facilities in this province.
                                          // Populated by the GIS pipeline from WorldGen karst probability layer.
                                          // Provinces with KoppenZone Csa, Csb, Cfa, BWh frequently have
                                          // karst (limestone karst belt); ET/EF provinces never do.
    float    historical_trauma_index;    // 0.0–1.0; static province-level measure of accumulated
                                          // historical grievance from past conflicts, colonialism,
                                          // displacement, or institutional failure. Set at world gen;
                                          // does not change at runtime.
                                          // Effect on community response (§14):
                                          //   CommunityState.grievance_level base floor at province load:
                                          //     grievance_level = max(grievance_level, historical_trauma_index × config.community.trauma_grievance_floor_scale)
                                          //   CommunityState.institutional_trust ceiling at province load:
                                          //     institutional_trust = min(institutional_trust, 1.0 - historical_trauma_index × config.community.trauma_trust_ceiling_scale)
                                          // These are applied once at world load to initial CommunityState.
                                          // Subsequent runtime dynamics can push grievance higher or trust
                                          // lower; the trauma index does not act as a per-tick floor —
                                          // only as a starting condition. Recovery is possible through
                                          // player and NPC institutional investment.
                                          // Named constants (simulation_config.json → community):
                                          //   trauma_grievance_floor_scale = 0.25  // max trauma (1.0) → 0.25 base grievance
                                          //   trauma_trust_ceiling_scale   = 0.30  // max trauma (1.0) → trust ≤ 0.70 at start
                                          // Populated by the GIS pipeline from WorldGen historical data layer.

    // Relationships
    uint32_t region_id;
    uint32_t nation_id;
    std::vector<ProvinceLink> links;     // rich adjacency descriptors; replaces bare adjacent_province_ids;
                                          // encodes LinkType, shared border, terrain cost, infrastructure bonus
    std::vector<uint32_t> market_ids;    // LOD 0 only
};

// Thin grouping layer; no simulation state
struct Region {
    uint32_t id;
    std::string fictional_name;
    uint32_t nation_id;
    std::vector<uint32_t> province_ids;
};
```

### Supporting Structs (unchanged from Pass 2)

```cpp
struct RegionDemographics {
    uint32_t total_population;
    float    median_age;
    float    education_level;     // 0.0–1.0
    float    income_low_fraction;
    float    income_middle_fraction;
    float    income_high_fraction;
    float    political_lean;      // -1.0 (hard left) to 1.0 (hard right)
};

struct CommunityState {
    float cohesion;               // 0.0–1.0
    float grievance_level;        // 0.0–1.0; primary driver of escalation stage
    float institutional_trust;    // 0.0–1.0
    float resource_access;        // 0.0–1.0; gate on upper escalation stages
    uint8_t response_stage;       // 0–6; see Section 14
};

struct RegionalPoliticalState {
    uint32_t governing_office_id;
    float    approval_rating;     // 0.0–1.0
    uint32_t election_due_tick;
    float    corruption_index;    // 0.0–1.0
};

struct RegionConditions {
    float stability_score;
    float inequality_index;
    float crime_rate;
    float addiction_rate;
    float criminal_dominance_index;
    float formal_employment_rate;       // 0.0–1.0; fraction of working-age
                                         // population in formal (declared, taxed)
                                         // employment. Updated monthly via
                                         // DeferredWorkQueue. High
                                         // criminal_dominance_index suppresses
                                         // this over time as legitimate businesses
                                         // exit or are extorted.
    float regulatory_compliance_index;  // 0.0–1.0; mean(1.0 -
                                         // facility.scrutiny_meter.current_level)
                                         // across all facilities in province where
                                         // criminal_sector == false.
                                         // Recomputed each tick step 13.
                                         // 1.0 = all formal facilities clean;
                                         // 0.0 = pervasive enforcement actions.

    // --- Agricultural event scalars (read by ComputeOutputQuality, farm recipes) ---
    // These are runtime event multipliers, not static exposure factors.
    // Static exposure factors (drought_vulnerability, flood_vulnerability) live on ClimateProfile.
    // These scalars are updated by the climate downstream batch (WorkType::climate_downstream_batch).
    float drought_modifier;             // 0.0–1.0; 1.0 = no active drought; 0.3 = severe drought.
                                         // Set by drought event onset; recovers toward 1.0 each tick
                                         // at config.climate.drought_recovery_rate when event is inactive.
    float flood_modifier;               // 0.0–1.0; 1.0 = no active flood; 0.0 = crops inundated.
                                         // Set by flood event onset; recovers toward 1.0 when event
                                         // clears. Flood events are shorter-duration than droughts.
    // NOTE: soil_health is per-facility (farm), not per-province. It lives on the Facility struct
    // for farm facilities, updated by the agricultural management system (tick step 1, production).
    // See farm recipe handling in Commodities & Factories §ComputeOutputQuality.
};
```

**Note:** `criminal_dominance_index` and `formal_employment_rate` are inversely
correlated over time. This emerges from criminal territory expansion effects
on NPCBusiness strategic decisions (§5) and community response stage (§14) —
it is not enforced mechanically.

**Cadence resolution (TDD-I2):** `criminal_dominance_index` uses LAYERED update cadence: (1) Seeded from world.json at game start. (2) Updated per-tick at step 12 from criminal_revenue / total_economic_activity. (3) Quarterly DeferredWorkQueue smooths the running average (exponential moving average, alpha=0.1). The per-tick value is the authoritative real-time value; the quarterly smooth provides trend data for NPC decision-making.

**Initialization rule:** At world-load time, `criminal_dominance_index` is seeded from
`WorldGenParameters.criminal_activity_baseline` (the per-world-gen value stored in
`world.json`). Specifically: `province.conditions.criminal_dominance_index = world_gen_params.criminal_activity_baseline`
for each province at engine startup. After that, the field is updated exclusively by the
criminal territory expansion mechanics in tick step 12 and CriminalOrganization enforcement
logic in §13. The Bootstrapper must not initialize this field to 0.0 — it must read from
`world.json → province[].criminal_activity_baseline`.

### Nation

```cpp
enum class GovernmentType : uint8_t {
    Democracy  = 0,
    Autocracy  = 1,
    Federation = 2,
    FailedState = 3
};

struct NationPoliticalCycleState {
    uint32_t current_administration_tick;
    float    national_approval;
    bool     election_campaign_active;
    uint32_t next_election_tick;
};

struct Nation {
    uint32_t id;
    std::string name;
    std::string currency_code;
    GovernmentType government_type;
    NationPoliticalCycleState political_cycle;
    std::vector<uint32_t> province_ids;
    float corporate_tax_rate;
    float income_tax_rate_top_bracket;
    std::map<uint32_t, float> diplomatic_relations; // [EX] nation_id → -1.0 to 1.0; empty in V1
    TariffSchedule tariff_schedule;                 // see Section 18
    std::optional<Lod1NationProfile> lod1_profile;  // null → LOD 0 (player's home nation, full simulation)
                                                    // populated → LOD 1 (simplified monthly update); see §20
};
```

> **V1 note:** V1 ships with exactly 1 LOD 0 nation (the player's home nation; `lod1_profile` is null). All other nations have `lod1_profile` populated. Expansion enables additional LOD 0 nations.

### Lod1NationProfile

Embedded in `Nation` as `std::optional<Lod1NationProfile> lod1_profile`. If null, the nation is LOD 0 (player-controlled, full simulation). If populated, the nation runs the LOD 1 monthly update (see §20.3). Fields below are archetype-driven and set from the nation data file; some are overridable by the scenario file.

```cpp
struct Lod1NationProfile {
    // Trade behavior — archetype-driven; set from nation data file
    float export_margin;              // markup over base production cost on exports
    float import_premium;            // premium the nation will pay above base cost for imports
    float trade_openness;            // 0.0–1.0; scales export quantity offered

    // Production capacity
    float tech_tier_modifier;        // multiplier on base production output (1.0 = current era baseline)
    float population_modifier;       // multiplier on base consumption (scales with population)

    // R&D / technology advancement (simplified — no full R&D tree)
    float research_investment;       // accumulated investment; compared to tier_advance_cost[] to advance
    uint8_t current_tier;            // current technology tier; 1–5

    // Political stability tracking
    float stability_delta_this_month; // change in stability this month; absolute value checked against threshold

    // Climate
    float climate_stress_aggregate;  // accumulated climate stress for this nation
    float climate_vulnerability;     // 0.0–1.0; scales climate_stress delta from GlobalCO2Index

    // Geography (for LOD 1 import transit time calculation)
    float geographic_centroid_lat;   // real-world centroid latitude; pipeline-seeded from world.json
    float geographic_centroid_lon;   // real-world centroid longitude
    float lod1_transit_variability_multiplier; // 1.0–1.3; seeded at world load; models port congestion
                                               // and customs delay variation; stored in world.json

    // LOD 1 archetype — set in nation data file; overridable by scenario file
    // Determines behavioral profile in LOD 1 monthly update (see §20.3)
    // "aggressive_exporter" | "protectionist" | "resource_dependent" | "industrial_hub"
    std::string archetype;
};
```

---

## 13. Influence Network [V1]

The Influence Network tracks the player's relationship network by influence type and computes a summary indicator for the UI. It is a derived layer on top of the existing relationship graph.

### The Four Influence Types

**Trust-based:** Falls directly out of `Relationship.trust`. Classified as trust-based when `trust > TRUST_CLASSIFICATION_THRESHOLD` (= 0.4).

**Fear-based:** Falls directly out of `Relationship.fear`. Classified as fear-based when `fear > FEAR_CLASSIFICATION_THRESHOLD` (= 0.35) AND `trust < 0.2`. Trust takes precedence when both conditions apply.

**Obligation-based:** Derived from `WorldState.obligation_network`. Any ObligationNode where `creditor_npc_id == player.id` and `status == ObligationStatus::Open` represents an obligation-based influence relationship. Strength = `ObligationNode.current_demand` normalized to [0.0, 1.0] relative to `original_value`.

**Movement-based:** Tracked via `PlayerCharacter.movement_follower_count` (integer count of background population NPCs in player-led movements) and the `Relationship.is_movement_ally` flag (Significant NPCs who are co-leaders or organizers). `movement_follower_count` is updated by tick step 22.

### Network Health Indicator

```cpp
struct InfluenceNetworkHealth {
    uint32_t trust_relationship_count;
    uint32_t fear_relationship_count;
    uint32_t obligation_held_count;          // open nodes where player is creditor
    uint32_t obligation_owed_count;          // open nodes where player is debtor
    uint32_t movement_follower_count;
    uint32_t movement_ally_count;

    float avg_trust_strength;
    float avg_fear_strength;
    float avg_obligation_leverage;
    float movement_coverage_fraction;        // movement_follower_count / sum of active region populations

    // Composite: 0.35 × trust + 0.25 × obligation + 0.20 × fear + 0.20 × movement
    // where each component = min(1.0, type_count / HEALTH_TARGET_COUNT)
    // HEALTH_TARGET_COUNT = 10
    // diversity_bonus: +0.05 if all four types have at least one active relationship
    float composite_health_score;

    uint32_t computed_at_tick;
};
```

### The Floor Principle

A trust drop qualifies as catastrophic when the trust delta in a single tick exceeds `CATASTROPHIC_TRUST_LOSS_THRESHOLD` (= −0.55) AND the resulting trust falls below `CATASTROPHIC_TRUST_FLOOR` (= 0.1).

On catastrophic trust loss:
```
recovery_ceiling = max(RECOVERY_CEILING_MINIMUM, trust_before_loss × RECOVERY_CEILING_FACTOR)
RECOVERY_CEILING_FACTOR   = 0.60
RECOVERY_CEILING_MINIMUM  = 0.15
```

In tick step 21, when applying a trust-increasing delta:
```
new_trust = min(relationship.recovery_ceiling, old_trust + trust_gain_delta)
```

When `recovery_ceiling == 1.0` (default), this clamp has no effect. The ceiling is permanent and never reset.

**Named constants (all to `simulation.json`):**

| Constant | Default | Effect |
|---|---|---|
| `TRUST_CLASSIFICATION_THRESHOLD` | 0.4 | Minimum trust for trust-based influence classification. |
| `FEAR_CLASSIFICATION_THRESHOLD` | 0.35 | Minimum fear for fear-based classification (also requires trust < 0.2). |
| `CATASTROPHIC_TRUST_LOSS_THRESHOLD` | −0.55 | Trust delta in a single tick that qualifies as catastrophic. |
| `CATASTROPHIC_TRUST_FLOOR` | 0.1 | Resulting trust below which a loss is catastrophic. |
| `RECOVERY_CEILING_FACTOR` | 0.60 | Multiplier on pre-loss trust to compute recovery ceiling. |
| `RECOVERY_CEILING_MINIMUM` | 0.15 | Floor on recovery ceiling regardless of factor. |
| `HEALTH_TARGET_COUNT` | 10 | Target relationship count per influence type for composite health score normalisation. |

### Tick Step 22 Module Specification

```
Module name: "InfluenceNetworkWeightUpdate"
Tick position: 22

Read from WorldState:
  - player.relationships (all)
  - obligation_network (all open nodes)
  - player.movement_follower_count

Write to DeltaBuffer:
  - player_delta: updated InfluenceNetworkHealth snapshot (only if dirty; see below)
  - npc_deltas: obligation trust erosion — overdue open obligations: trust_delta = -0.001/tick

Execution:
  1. IF network_health_dirty flag is set (written by any step that changed a relationship,
     obligation, or movement count this tick):
       Recompute InfluenceNetworkHealth from current WorldState
       Write updated InfluenceNetworkHealth to player_delta
       Clear network_health_dirty flag
     ELSE:
       Skip recomputation — no relationships changed
  2. Apply obligation trust erosion for all overdue obligation nodes
  3. Clear stale movement_ally flags for NPCs whose movement participation lapsed

Fear decay is NOT applied here. Fear is a Relationship field.
Fear decay runs via DeferredWorkQueue (WorkType::npc_relationship_decay), batched every 30 ticks
per NPC. Rate: config.npc.fear_decay_rate per tick (default 0.002) × 30 applied as one operation.
Batch rescheduled at current_tick + 30 after execution.
Fear decay is suspended for any tick in which a fear-reinforcing event fires for that NPC
(e.g., threat_delivered consequence). Reinforcing events reset the batch schedule.
```

**`network_health_dirty` flag:** Added to `WorldState` as `bool network_health_dirty`. Set to `true` by any DeltaBuffer write that touches: `Relationship.trust`, `Relationship.fear`, `ObligationNode.status`, `PlayerCharacter.movement_follower_count`, or any `is_movement_ally` flag. This flag-based approach replaces the [RISK] dirty-flag note — it is the implementation.

---

## 14. Community Response System [V1]

### Community State

`CommunityState` fields are embedded in the `Region` struct (Section 12). They are four scalar fields updated in-place each tick. See the Region struct for field definitions.

### Aggregation from NPC State to Community Metrics (Tick Step 9)

EMA smoothing factor: `config.community.ema_alpha` (default: 0.05; loaded from `simulation_config.json`). At this value, a sustained shift takes approximately 20 ticks to substantially shift community metrics.

**Cohesion:**
```
cohesion_sample_i = clamp(npc.social_capital / config.community.social_capital_max, 0.0, 1.0)
                  × clamp(npc.motivations[stability], 0.0, 1.0)
npc_cohesion_mean = mean(cohesion_sample_i for all significant NPCs in region)
region.community.cohesion += config.community.ema_alpha × (npc_cohesion_mean - region.community.cohesion)
```

**Grievance Level:**
Driven by memory entries with negative emotional weight, weighted by harm type. All actors — player operations, NPC criminal orgs, NPC businesses — contribute at the same rate for the same action type.
```
negative_attribution_weight_i =
    sum(abs(entry.emotional_weight) × action_type_weight(entry.type))
    for memory entries where:
        type in {interaction, observation}
        AND emotional_weight < 0
        AND decay > config.community.memory_decay_floor

float action_type_weight(MemoryType type):
    if type in {witnessed_illegal_activity, witnessed_safety_violation,
                witnessed_wage_theft, physical_hazard,
                retaliation_experienced}:
        return 1.0   // direct harm; full weight regardless of actor
    if type in {employment_negative, facility_quality}:
        return 0.5   // economic harm; moderate weight
    return 0.0       // other types do not contribute to grievance_level

npc_grievance_mean = mean(clamp(negative_attribution_weight_i / config.community.grievance_normalizer, 0.0, 1.0))
region.community.grievance_level += config.community.ema_alpha × (npc_grievance_mean - region.community.grievance_level)
```

**Design note:** The simulation does not privilege any actor as a grievance source. Player operations, NPC criminal orgs, and NPC businesses all contribute at the same rate for the same action type. Player actions are likely to dominate in practice because players operate at larger scale — that dominance is emergent, not baked into the formula. Political grievance (institutional failure, corruption) flows through institutional_trust rather than grievance_level — a structural distinction based on action type, not actor identity.
`config.community.grievance_normalizer` = 5.0 (default). **Fast-path for shock events:** If a single tick produces > `config.community.grievance_shock_threshold` (default: 0.15) of raw grievance increase, the EMA is bypassed and grievance jumps directly.

**Institutional Trust:**
```
trust_target = clamp(
    region.base_institutional_trust
    - region.corruption_index × config.community.corruption_trust_penalty
    + recent_institutional_successes × config.community.trust_success_bonus
    - recent_institutional_failures × config.community.trust_failure_penalty,
    0.0, 1.0
)
region.community.institutional_trust += config.community.ema_alpha × (trust_target - region.community.institutional_trust)
```

**Resource Access:**
```
npc_resource_mean = mean(clamp(npc.capital / config.community.capital_normalizer + npc.social_capital / config.community.social_normalizer, 0.0, 1.0))
region.community.resource_access += config.community.ema_alpha × (npc_resource_mean - region.community.resource_access)
```

### Community Response Escalation Stages

```cpp
enum class CommunityResponseStage : uint8_t {
    quiescent             = 0,  // Threshold: grievance < config.stage_thresholds.quiescent_grievance_max
                                //   OR cohesion < config.stage_thresholds.quiescent_cohesion_min
                                //   (default: grievance < 0.15 OR cohesion < 0.10)
    informal_complaint    = 1,  // Threshold: grievance >= 0.15 AND cohesion >= 0.10
    organized_complaint   = 2,  // Threshold: grievance >= 0.28 AND cohesion >= 0.25
                                //   Generates regulatory complaint consequence entries.
    political_mobilization = 3, // Threshold: grievance >= 0.42 AND institutional_trust >= 0.20
    economic_resistance   = 4,  // Threshold: grievance >= 0.56 AND resource_access >= 0.25
                                //   Revenue penalty: config.stage_thresholds.resistance_revenue_penalty (default: -0.15)
    direct_action         = 5,  // Threshold: grievance >= 0.70 AND cohesion >= 0.45
    sustained_opposition  = 6,  // Threshold: grievance >= 0.85 AND leadership_npc exists
                                //             AND resource_access >= 0.35
};
```

**Community response stage thresholds are loaded from `simulation_config.json` under `stage_thresholds`.** All threshold values (grievance, cohesion, institutional_trust, resource_access per stage) and the `resistance_revenue_penalty` are named constants in that file. The enum integer values are engine constants (stage ordering cannot be modded); the numeric thresholds that gate each stage transition are fully moddable. Proposed defaults are shown in the comments above.

**Named constants (all to `simulation_config.json` → `community.stage_thresholds`):**

| Constant | Proposed default | Effect |
|---|---|---|
| `informal_grievance_min` | 0.15 | Grievance floor for stage 1 |
| `informal_cohesion_min` | 0.10 | Cohesion floor for stage 1 |
| `organized_grievance_min` | 0.28 | Grievance floor for stage 2 |
| `organized_cohesion_min` | 0.25 | Cohesion floor for stage 2 |
| `mobilization_grievance_min` | 0.42 | Grievance floor for stage 3 |
| `mobilization_trust_min` | 0.20 | Institutional trust floor for stage 3 |
| `resistance_grievance_min` | 0.56 | Grievance floor for stage 4 |
| `resistance_resource_min` | 0.25 | Resource access floor for stage 4 |
| `resistance_revenue_penalty` | -0.15 | Revenue modifier on player in stage 4+ |
| `direct_action_grievance_min` | 0.70 | Grievance floor for stage 5 |
| `direct_action_cohesion_min` | 0.45 | Cohesion floor for stage 5 |
| `opposition_grievance_min` | 0.85 | Grievance floor for stage 6 |
| `opposition_resource_min` | 0.35 | Resource access floor for stage 6 |

**Stage transition logic:** Stage evaluation is scheduled via `DeferredWorkQueue` using `WorkType::community_stage_check` (one item per province):
- At world load: each province gets a `community_stage_check` at `current_tick + 7`
- After evaluation with **no stage change**: reschedule at `current_tick + 7`
- After evaluation with **stage change**: reschedule at `current_tick + 1` (immediate re-check for fast cascades)
- After a **grievance shock event** (EMA bypass): push `community_stage_check` at `current_tick + 1`

`subject_id` on the `DeferredWorkItem` is the `province_id` being evaluated.

When evaluation runs: stage = highest stage whose threshold is fully satisfied. Stages cannot skip — one stage per evaluation maximum. **Regression:** one stage per 7 ticks minimum. `sustained_opposition` (stage 6) does not regress automatically once an `OppositionOrganization` is created.

### Opposition Organization

```cpp
struct OppositionOrganization {
    uint32_t id;
    uint32_t founding_npc_id;
    uint32_t province_id;
    OppositionTarget target;
    CommunityResponseStage stage;
    float resource_level;              // 0.0–1.0; grows if grievance remains high
    std::vector<uint32_t> member_ids;
    uint32_t formation_tick;
    std::vector<uint32_t> consequence_ids;
};

struct OppositionTarget {
    enum class TargetType : uint8_t {
        player_general, specific_operation, specific_policy
    };
    TargetType type;
    uint32_t subject_id;               // facility_id, business_id, or law_id. 0 if general.
};
```

**Formation trigger algorithm (tick step 10):**
```
IF region.community.stage == sustained_opposition AND province.opposition_organization == nullptr:
    founding_npc = argmax over significant NPCs in region of:
        (npc.motivations[ideology] + npc.motivations[power] + npc.motivations[revenge])
        × npc.social_capital × npc.risk_tolerance
        WHERE npc has attribution memory entry
    IF founding_npc exists:
        Create OppositionOrganization { founding_npc_id, resource_level = region.community.resource_access,
            member_ids = all significant NPCs with grievance_attribution > 0.5 }
        Queue ConsequenceEntry { type = opposition_org_formed, payload = { opposition_org_id } }
```

### Intervention Types

```cpp
enum class CommunityIntervention : uint8_t {
    address_grievance   = 0,  // Fix underlying cause. Grievance drops by config.community.grievance_address_rate/tick.
                               //   OppositionOrganization may dissolve if grievance < 0.15.
    deflect             = 1,  // Pauses grievance accumulation for config.community.deflect_pause_ticks (default: 30).
                               //   Resumes at same rate. Overuse reduces institutional_trust.
    suppress            = 2,  // Reduces OppositionOrganization.resource_level immediately.
                               //   Negative memory entries in all members. Evidence token generated.
    co_opt_leadership   = 3,  // Offer founding_npc employment or favor. If accepted: NPC exits,
                               //   resource_level drops, new leader promoted from member_ids.
                               //   Risk: detectable; may spike grievance (betrayal memory).
    ignore              = 4,  // Grievance accumulates. resource_level grows by
                               //   config.community.opposition_growth_rate × region.community.resource_access/tick.
};
```

**Named constants (all to `simulation_config.json` → `community`):**

| Constant | Proposed default | Effect |
|---|---|---|
| `ema_alpha` | 0.05 | EMA smoothing factor; ~20 ticks to substantially shift metrics |
| `social_capital_max` | 100.0 | Normalizer for NPC social_capital in cohesion sample |
| `memory_decay_floor` | 0.01 | Minimum memory decay below which entries don't contribute grievance |
| `grievance_normalizer` | 5.0 | Normalizer for per-NPC grievance contribution |
| `grievance_shock_threshold` | 0.15 | Per-tick grievance increase that bypasses EMA |
| `corruption_trust_penalty` | 0.20 | Trust reduction per unit of corruption_index |
| `trust_success_bonus` | 0.05 | Trust gain per recent institutional success |
| `trust_failure_penalty` | 0.08 | Trust loss per recent institutional failure |
| `capital_normalizer` | 1000.0 | Normalizer for NPC capital in resource access sample |
| `social_normalizer` | 50.0 | Normalizer for NPC social_capital in resource access sample |
| `grievance_address_rate` | 0.002 | Per-tick grievance reduction from address_grievance intervention |
| `deflect_pause_ticks` | 30 | Ticks grievance accumulation is paused by deflect intervention |
| `opposition_growth_rate` | 0.001 | Per-tick opposition resource growth rate when ignored |
| `trauma_grievance_floor_scale` | 0.25 | `historical_trauma_index × scale` = grievance_level floor applied at world load |
| `trauma_trust_ceiling_scale` | 0.30 | `1.0 - historical_trauma_index × scale` = institutional_trust ceiling applied at world load |

---

### Political Office

```cpp
enum class PoliticalOfficeType : uint8_t {
    city_council        = 0,
    mayor               = 1,
    regional_governor   = 2,
    national_legislator = 3,
    cabinet_minister    = 4,
    head_of_state       = 5,
    appointed_role      = 6,  // [EX] placeholder record only in V1
};

struct PoliticalOffice {
    uint32_t id;
    PoliticalOfficeType office_type;
    uint32_t current_holder_id;
    uint32_t region_id;                    // 0 for national offices
    uint32_t election_due_tick;
    uint32_t term_length_ticks;
    std::map<DemographicGroup, float> approval_by_demographic;
    float win_threshold;                   // default 0.5
    std::vector<uint32_t> pending_legislation_ids;
};
```

### Demographic Groups

```cpp
enum class DemographicGroup : uint8_t {
    working_class           = 0,
    professional            = 1,
    small_business          = 2,
    corporate               = 3,
    criminal_adjacent       = 4,
    community_organized     = 5,
    agricultural            = 6,
    youth                   = 7,
    retiree                 = 8,
    religious_conservative  = 9,
    progressive_professional = 10,
    media_public            = 11,
};
```

### PopulationCohort and RegionCohortStats [V1]

`RegionCohortStats` is the population model for a province. It is a collection of `PopulationCohort` records — one per `DemographicGroup` — plus province-level aggregates derived from them. This struct is referenced by `Province.cohort_stats` and is read by the Consumer Demand Model (§5), the hiring system (§30), and the Community Response System (§14).

```cpp
struct PopulationCohort {
    DemographicGroup group;

    uint32_t size;                      // headcount in this province; updated annually
                                        // from LOD 2 batch or by demographic event consequences

    float median_income;                // in-game currency per tick; used to compute
                                        // household_budget in the Consumer Demand Model (§5)
                                        // Updated monthly: tracks formal wage market rates
                                        // (see §30 regional_wage_by_skill) weighted by
                                        // employment composition of this group.

    float education_level;              // 0.0–1.0; affects skill supply in hiring system (§30)
                                        // and long-run productivity. Set at world gen from
                                        // province education_level (RegionDemographics) with
                                        // group-specific offsets:
                                        //   professional, corporate: +0.15
                                        //   working_class, criminal_adjacent: -0.10
                                        //   agricultural: -0.05
                                        // Clamped 0.0–1.0. Updates at ≤ 0.01 per year
                                        // (slow-moving structural variable).

    float employment_rate;              // 0.0–1.0; fraction of this cohort in formal employment
                                        // Initialized from Province.conditions.formal_employment_rate
                                        // with group-specific offsets. Updated monthly.

    std::map<SkillDomain, float> skill_supply;
                                        // SkillDomain → fraction of this cohort with meaningful
                                        // skill in that domain (0.0–1.0). Used by hiring system
                                        // (§30) to size applicant pools per domain.
                                        // Group-skill priors set at world gen (see table below).
                                        // Example: working_class has high Trade, Management;
                                        // professional has high Finance, Politics, Engineering.

    float political_lean;               // -1.0 (hard left) to 1.0 (hard right)
                                        // Group-level political lean; feeds voter coalition
                                        // model (§14 election resolution).

    float grievance_contribution;       // 0.0–1.0; this cohort's weight in province
                                        // grievance_level aggregation (§14 tick step 9).
                                        // Higher means this group's negative memories
                                        // are weighted more heavily in community response.
                                        // Defaults: working_class = 0.20; agricultural = 0.15;
                                        // others proportional to cohort size.

    float addiction_prevalence;         // Fraction of cohort with AddictionState != none; range [0.0, 1.0];
                                        // aggregated from NPC AddictionStates each tick (TDD-I1)
};

struct RegionCohortStats {
    std::map<DemographicGroup, PopulationCohort> cohorts;
                                        // one entry per DemographicGroup;
                                        // all 12 groups always present after world load

    // --- Province-level aggregates (derived from cohorts; recomputed monthly) ---
    uint32_t total_population;          // sum of cohort.size across all groups
    float    mean_income;               // weighted mean of cohort.median_income by cohort.size
    float    gini_coefficient;          // income inequality measure; 0.0 (equal) to 1.0 (extreme)
                                        // computed from cohort income distribution each month
                                        // feeds RegionConditions.inequality_index
    float    aggregate_skill_supply;    // mean skill availability across all domains; used as
                                        // a province-level labor market summary for LOD 1/2
};
```

**Group-skill prior table (world gen initialization):**

| DemographicGroup | High-skill domains | Low-skill domains |
|---|---|---|
| `working_class` | Trade, Management | Finance, Politics |
| `professional` | Finance, Engineering, Management | CriminalOperations |
| `small_business` | Trade, Management, Persuasion | Engineering |
| `corporate` | Finance, Management, Politics | CriminalOperations |
| `criminal_adjacent` | CriminalOperations, Trade, Intelligence | Politics, Engineering |
| `agricultural` | SpecialtyAgriculture, Trade | Finance, Politics |
| `youth` | SpecialtyCoding | Management, Politics |
| `professional`, `progressive_professional` | Persuasion, Politics | SpecialtyAgriculture |

Skill supply within each group is initialized with modest variance (±0.10 around the prior) using `world_seed`. Provincial education level scales all priors proportionally.

**Update cadence:** `PopulationCohort` fields update at most once per simulated month via `DeferredWorkQueue` (`WorkType::cohort_monthly_update`). `RegionCohortStats` aggregates are recomputed immediately after any cohort update. Population size changes only on major events (large migration, disaster, generational growth from LOD 2 batch).

**Named constants (all to `simulation_config.json → labor`):**

| Constant | Default | Effect |
|---|---|---|
| `cohort_income_update_rate` | 0.05 | Monthly convergence rate of cohort income toward regional wage market |
| `cohort_employment_update_rate` | 0.02 | Monthly convergence rate toward formal_employment_rate |

### Legislative Proposal

```cpp
enum class LegislativeStatus : uint8_t {
    drafted = 0, in_committee = 1, floor_debate = 2, voted = 3, enacted = 4, failed = 5
};

enum class LegislatorPosition : uint8_t {
    support = 0, oppose = 1, undecided = 2
};

struct LegislativeProposal {
    uint32_t id;
    std::string proposal_text;
    uint32_t sponsor_id;
    LegislativeStatus status;
    uint32_t submitted_tick;
    uint32_t vote_tick;
    std::map<uint32_t, LegislatorPosition> npc_legislator_positions;
    std::vector<uint32_t> obligation_node_ids;
    struct VoteResult {
        int32_t votes_for;
        int32_t votes_against;
        int32_t abstentions;
        bool passed;
    } vote_result;
    uint32_t policy_effect_id;
};
```

**Vote resolution algorithm:**
```
For each NPC legislator n where position == undecided:
    base_support = dot(n.motivations, policy_motivation_alignment_vector)
    obligation_bonus = sum(obligation_node.current_demand for nodes targeting n created by sponsor)
    constituency_pressure = coalition_support[n.constituency_demographic]
    IF (base_support + obligation_bonus + constituency_pressure) > SUPPORT_THRESHOLD: support
    ELSE IF ... < OPPOSE_THRESHOLD: oppose
    ELSE: abstain

passed = (votes_for / (votes_for + votes_against)) > config.politics.MAJORITY_THRESHOLD
// MAJORITY_THRESHOLD = 0.5 for standard legislation; 0.67 for constitutional — see named constants table below
```

### Campaign

```cpp
struct Campaign {
    uint32_t id;
    uint32_t active_candidate_id;
    uint32_t office_id;
    uint32_t campaign_start_tick;
    uint32_t election_tick;
    struct CoalitionCommitment {
        DemographicGroup demographic;
        std::string promise_text;
        uint32_t obligation_node_id;
        bool delivered;
    };
    std::vector<CoalitionCommitment> coalition_commitments;
    struct Endorsement {
        uint32_t endorser_npc_id;
        DemographicGroup primary_demographic;
        float approval_bonus;
    };
    std::vector<Endorsement> endorsements;
    float resource_deployment;
    std::map<DemographicGroup, float> current_approval_by_demographic;
    std::vector<float> event_modifiers;
};
```

### Election Resolution Algorithm

All inputs evaluated at `election_tick`. Output: `player_vote_share` (float 0.0–1.0). Win condition: `player_vote_share > office.win_threshold`.

```
// Step 1: Coalition support per demographic
for each DemographicGroup d:
    coalition_support[d] = current_approval_by_demographic[d]
    for each endorsement e where e.primary_demographic == d:
        coalition_support[d] = clamp(coalition_support[d] + e.approval_bonus, 0.0, 1.0)

// Step 2: Weighted vote share
numerator   = Σ_d (coalition_support[d] × turnout_weight[d] × population_fraction[d])
denominator = Σ_d (turnout_weight[d] × population_fraction[d])
raw_share   = numerator / denominator

// Step 3: Resource deployment modifier (diminishing returns)
// See named constants table below: RESOURCE_MAX_EFFECT, RESOURCE_SCALE
resource_modifier = clamp(tanh(resource_deployment × config.politics.RESOURCE_SCALE) × config.politics.RESOURCE_MAX_EFFECT,
                          -config.politics.RESOURCE_MAX_EFFECT, config.politics.RESOURCE_MAX_EFFECT)

// Step 4: Event modifiers (capped at ±0.20)
event_total = clamp(sum(event_modifiers), -0.20, 0.20)

// Step 5: Final
player_vote_share = clamp(raw_share + resource_modifier + event_total, 0.0, 1.0)
won = (player_vote_share > office.win_threshold)
```

**Named constants (all to `simulation_config.json` → `politics`):**

| Constant | Default | Effect |
|---|---|---|
| `SUPPORT_THRESHOLD` | 0.55 | Base support score above which an undecided NPC legislator votes in favour. Represents simple majority sentiment. |
| `OPPOSE_THRESHOLD` | 0.35 | Base support score below which an undecided NPC legislator votes against. |
| `MAJORITY_THRESHOLD` | 0.50 | Vote share required for standard legislation to pass. Constitutional threshold is 0.67 [EX in V1]. |
| `RESOURCE_SCALE` | 2.0 | Scaling factor in tanh() for campaign resource deployment modifier. Controls diminishing returns curve. |
| `RESOURCE_MAX_EFFECT` | 0.15 | Maximum vote share swing from resource deployment alone (±15 points). |

---

## 15. Legal Process State Machine [V1]

Specified per GDD §10.6. Legal cases are multi-stage processes with player agency at each transition. Processing runs at **tick step 14** (GDD §21 step 14: "Update legal process state").

### LegalCaseStage [V1]

```cpp
enum class LegalCaseStage : uint8_t {
    investigation = 0,  // case opened; evidence gathering; player may not know
    arrested      = 1,  // minor charges; bail available; public exposure generated
    charged       = 2,  // formal felony charges filed; discovery begins
    trial         = 3,  // evidence vs. defence; probabilistic resolution
    convicted     = 4,  // sentence assigned; player enters prison state if custodial
    imprisoned    = 5,  // physically confined; restricted operations
    acquitted     = 6,  // terminal: not guilty or case dismissed
    appealing     = 7,  // post-conviction appeal in progress; imprisoned ops continue
    paroled       = 8,  // early release with conditions; conviction record remains
    pardoned      = 9,  // conviction cleared; requires political Influence expenditure
};
```

### LegalCase [V1]

```cpp
struct LegalCase {
    uint32_t                    id;
    uint32_t                    defendant_id;       // PlayerCharacter id or NPC id
    LegalCaseStage              stage;
    uint32_t                    opened_tick;
    uint32_t                    stage_entered_tick; // when current stage was entered
    float                       evidence_weight;    // 0.0–1.0; accumulated admissible evidence
    std::vector<uint32_t>       evidence_token_ids; // tokens held by prosecution
    uint32_t                    lead_investigator_id;
    uint32_t                    prosecutor_npc_id;
    uint32_t                    judge_npc_id;       // assigned at charged stage
    uint8_t                     charge_severity;    // 1 = minor; 5 = maximum severity
    uint32_t                    sentence_ticks;     // set at conviction; 0 if non-custodial
    uint32_t                    sentence_remaining;
    bool                        bail_posted;
    bool                        appeal_pending;
    float                       defence_quality;    // 0.0–1.0; set by player's legal contacts and resources
};
```

### Stage Transition Rules [V1]

```
investigation → arrested:
    trigger:  evidence_weight >= config.legal.arrest_evidence_threshold
              AND lead_investigator_id's InvestigatorMeter.status == critical
    effect:   generate EvidenceToken(type: public_arrest_record)
              set PlayerCharacter.exposure += config.legal.arrest_exposure_hit
              bail_posted defaults to true if player has liquid assets > config.legal.bail_amount

arrested → charged:
    trigger:  evidence_weight >= config.legal.charge_evidence_threshold
              AND tick_count_in_stage(arrested) >= config.legal.investigation_to_charge_ticks
    effect:   discovery: player learns evidence_token_ids (KnowledgeMap entry per token)
              judge_npc_id assigned from province NPC pool (role = judge)

charged → acquitted:
    trigger:  evidence_weight < config.legal.dismissal_evidence_threshold
              OR procedural_challenge_success (defence_quality roll)
    effect:   case closed; evidence tokens remain in pool but are inadmissible for re-filing
              for config.legal.double_jeopardy_cooldown_ticks

charged → trial:
    trigger:  tick_count_in_stage(charged) >= config.legal.charge_to_trial_ticks
    effect:   trial_outcome_roll()

trial → convicted | acquitted:
    resolved_by: trial_outcome_roll()

trial_outcome_roll():
    base_conviction_probability = evidence_weight
        × (1.0 − defence_quality × config.legal.defence_evidence_discount)
        × judge_bias_modifier(judge_npc_id, player)  // Influence + corruption
        × witness_reliability_modifier(evidence_token_ids)
    clamp to [0.0, 1.0]
    roll random_float(0.0, 1.0):
        if < base_conviction_probability → convicted
            assign sentence_ticks based on charge_severity × config.legal.base_sentence_ticks_per_severity
        else → acquitted

convicted → imprisoned  (if charge_severity >= config.legal.custodial_sentence_severity_floor):
    effect:   PlayerCharacter.state = imprisoned
              delegation assignments persist at efficiency × config.legal.prison_delegation_efficiency
              no new hires, delegations, or obligation actions during imprisonment

convicted → fined       (if charge_severity < config.legal.custodial_sentence_severity_floor):
    effect:   PlayerCharacter.cash -= fine_amount; case closed

imprisoned → paroled:
    trigger:  sentence_remaining <= sentence_ticks × config.legal.parole_eligibility_fraction
              AND parole_board_roll(player) passes
    effect:   PlayerCharacter.state = normal; conditions apply

imprisoned → appealing:
    trigger:  player initiates appeal via legal contact action
    effect:   appeal_pending = true; legal case remains open; imprisonment continues

appealing → acquitted | sentence_reduced:
    resolved_by: appeal_outcome_roll() — lower base probability than trial; requires new defence argument
```

### Named Constants [V1]

All in `simulation_config.json → legal`:

| Constant | Default | Notes |
|---|---|---|
| `legal.arrest_evidence_threshold` | 0.35 | Evidence weight at which investigator can make arrest |
| `legal.charge_evidence_threshold` | 0.55 | Required to file formal charges |
| `legal.dismissal_evidence_threshold` | 0.25 | Below this, case dismissed at charged stage |
| `legal.charge_to_trial_ticks` | 180 | ~6 months game time from charges to trial |
| `legal.investigation_to_charge_ticks` | 60 | ~2 months from arrest to charges |
| `legal.defence_evidence_discount` | 0.40 | Max evidence discount a perfect defence can achieve |
| `legal.base_sentence_ticks_per_severity` | 365 | Severity 1 = 1yr; severity 5 = 5yr |
| `legal.custodial_sentence_severity_floor` | 3 | Below this severity, fine only (no prison) |
| `legal.bail_amount` | 50000.0 | Game currency; auto-posted if player has liquidity |
| `legal.prison_delegation_efficiency` | 0.60 | Delegate performance multiplier during imprisonment |
| `legal.parole_eligibility_fraction` | 0.50 | Eligible for parole after serving 50% of sentence |
| `legal.double_jeopardy_cooldown_ticks` | 1825 | ~5 years before same charge can be re-filed |
| `legal.arrest_exposure_hit` | 0.15 | Immediate public exposure increase on arrest |

---

## 16. Facility Signals and Scrutiny System [V1]

### Facility Signals Data Structure

```cpp
// Applied to all facility types. Signals are physical facts — they do not
// encode legality. Which NPC types read which signals is defined by the
// scrutiny integration rules below. Weights are per-facility-type in
// facility_types.csv.
struct FacilitySignals {
    float power_consumption_anomaly;  // 0.0–1.0; deviation from regional baseline
                                      // Criminal lab: high (unusual for cover business)
                                      // Steel mill: low (expected; not anomalous)
    float chemical_waste_signature;   // 0.0–1.0; observable waste streams
                                      // Drug lab: high; Clean office: 0.0
    float foot_traffic_visibility;    // 0.0–1.0; observable traffic vs. cover
                                      // Front business: high (traffic/revenue mismatch)
    float olfactory_signature;        // 0.0–1.0; detectable smell at/around facility
    float base_signal_composite;      // weighted sum of above four; computed each tick
    float scrutiny_mitigation;        // 0.0–1.0; reduces net signal for all observers
                                      // Criminal facility: clamp(sum(corrupted_le_npc
                                      //   .authority_weight), 0.0, 1.0)
                                      // Legitimate facility: compliance_investment_score
                                      //   (legal team, clean audits, proactive engagement)
    float net_signal;                 // = max(0.0, base_signal_composite
                                      //        - scrutiny_mitigation)
                                      // Read by all scrutiny observers
};
```

### Signal Composite Formula

```cpp
// Signal weights are loaded per facility type from facility_types.csv.
// Each facility type row carries four columns:
//   signal_w_power_consumption, signal_w_chemical_waste, signal_w_foot_traffic, signal_w_olfactory
// The four weights must sum to 1.0; the loader validates and rejects invalid rows.

struct FacilityTypeSignalWeights {   // populated by facility_types.csv loader
    float w_power_consumption;       // default 0.30
    float w_chemical_waste;          // default 0.25
    float w_foot_traffic;            // default 0.20
    float w_olfactory;               // default 0.25
};

// Computed each tick in tick step 12:
const FacilityTypeSignalWeights& w = facility.type_def.signal_weights;
facility.signals.base_signal_composite = clamp(
    w.w_power_consumption * facility.signals.power_consumption_anomaly
  + w.w_chemical_waste    * facility.signals.chemical_waste_signature
  + w.w_foot_traffic      * facility.signals.foot_traffic_visibility
  + w.w_olfactory         * facility.signals.olfactory_signature,
    0.0f, 1.0f
);

facility.signals.net_signal = max(0.0f,
    facility.signals.base_signal_composite - facility.signals.scrutiny_mitigation);
```

**Proposed defaults by facility type (all values in `facility_types.csv`):**

| Facility type    | power | chemical | foot_traffic | olfactory | Notes |
|---|---|---|---|---|---|
| drug_lab         | 0.30  | 0.25     | 0.20         | 0.25      | Original defaults |
| pharma_plant     | 0.20  | 0.35     | 0.15         | 0.30      | Chemical and smell dominant |
| steel_mill       | 0.35  | 0.30     | 0.20         | 0.15      | Power and waste dominant |
| office           | 0.10  | 0.00     | 0.50         | 0.00      | Foot traffic only anomaly |
| warehouse        | 0.10  | 0.05     | 0.55         | 0.05      | Traffic pattern dominant |
| skunkworks_rd    | 0.25  | 0.25     | 0.30         | 0.10      | Mixed; unusual for location |
| rendering_plant  | 0.15  | 0.20     | 0.20         | 0.45      | Olfactory dominant |

**Moddability:** Signal weights are per-facility-type data. All facility types — criminal and legitimate — define their own four-weight profile in `facility_types.csv` without touching engine code.

### Investigator Meter Integration (Tick Step 13)

```
// LE cannot distinguish player-owned from NPC-org-owned criminal facilities
// from signal alone. target_id is resolved by the behavior engine after
// regional_signal aggregation. If multiple criminal actors are present,
// target_id = argmax(known_actor, estimated_signal_contribution).
// Actors below LE awareness threshold are not targetable until an
// EvidenceToken creates awareness of their presence.
For each law enforcement NPC in region r:
    // LE reads net_signal (universal field on FacilitySignals) but
    // filters to criminal_sector == true facilities. Regulators read all facilities.
    // The signal is the same field on the same struct — the reader determines the
    // filter, not the struct.
    regional_signal = sum(facility.signals.net_signal
                          for all facilities in r
                          where facility.criminal_sector == true)
                    / config.opsec.facility_count_normalizer
    le_npc.investigator_meter.fill_rate =
        clamp(regional_signal × config.opsec.detection_to_fill_rate_scale, 0.0f, config.opsec.fill_rate_max)
    // config.opsec.detection_to_fill_rate_scale default = 0.005:
    //   net_signal 0.50 on single facility → fill_rate 0.0025/tick
    //   → reaches 0.30 threshold in ~120 ticks (~4 game months)

    le_npc.investigator_meter.current_level = clamp(
        le_npc.investigator_meter.current_level + le_npc.investigator_meter.fill_rate,
        0.0f, 1.0f)

    // Corruption also reduces fill_rate:
    le_npc.investigator_meter.fill_rate ×= (1.0f - le_npc.corruption_susceptibility
                                                   × regional_corruption_coverage)
```

### Investigator Meter

```cpp
enum class InvestigatorStatus : uint8_t {
    inactive       = 0,  // level < config.investigator.surveillance_threshold (default: 0.30). No formal action.
    surveillance   = 1,  // level >= surveillance_threshold. Observation begins. Physical evidence token created.
    formal_inquiry = 2,  // level >= config.investigator.formal_inquiry_threshold (default: 0.60).
                          //   Formal investigation opens. ConsequenceEntry: investigation_opens.
                          //   Warrant possible if institutional_trust >= config.investigator.warrant_trust_min (default: 0.30).
    raid_imminent  = 3,  // level >= config.investigator.raid_threshold (default: 0.80). Raid planned.
                          //   Consequence queued; delay = 7–30 ticks (seed-deterministic).
};

struct InvestigatorMeter {
    uint32_t investigator_npc_id;
    uint32_t target_id;   // npc_id of the primary investigation subject.
                          // Resolved by the LE NPC's behavior engine from
                          // available regional signal — whichever known criminal
                          // actor has the highest estimated signal contribution
                          // in the province. Player and NPC criminal org members
                          // are equally valid targets.
                          // 0 = inactive sentinel (no current investigation subject).
                          // [EX] Simultaneous meters per LE NPC is expansion scope.
                          // V1: one InvestigatorMeter per LE NPC.
    float current_level;           // 0.0–1.0
    float fill_rate;               // per-tick increment
    float decay_rate;              // per-tick reduction when detection risk drops; default: 0.001 (simulation_config.json → investigator.decay_rate)
    InvestigatorStatus status;     // derived each tick from named thresholds above
    uint32_t opened_tick;          // tick when status first became formal_inquiry
    std::vector<uint32_t> evidence_token_ids;
};
```

**Status derivation:** `>= config.investigator.raid_threshold` → raid_imminent; `>= config.investigator.formal_inquiry_threshold` → formal_inquiry; `>= config.investigator.surveillance_threshold` → surveillance; else inactive.

**Decay:** When `net_signal` drops to 0.0, meter decays at `decay_rate`. Formally opened investigations do not close on signal drop alone — require active intervention.

**Named constants (all to `simulation_config.json` → `investigator`):**

| Constant | Proposed default | Effect |
|---|---|---|
| `surveillance_threshold` | 0.30 | Level at which surveillance begins |
| `formal_inquiry_threshold` | 0.60 | Level at which formal investigation opens |
| `raid_threshold` | 0.80 | Level at which raid is planned |
| `warrant_trust_min` | 0.30 | Minimum regional institutional_trust for warrant to be issued |
| `decay_rate` | 0.001 | Per-tick meter reduction when detection risk is zero |

**Named constants (all to `simulation_config.json` → `opsec`):**

| Constant | Proposed default | Effect |
|---|---|---|
| `facility_count_normalizer` | 5.0 | Normalizes facility count in scrutiny formula; 5 criminal facilities = 1.0 contribution; divisor for aggregating regional signal |
| `detection_to_fill_rate_scale` | 0.005 | Converts net_signal to per-tick meter fill |
| `fill_rate_max` | 0.01 | Cap on per-tick meter fill regardless of signal |
| `personnel_violence_multiplier` | 3.0 | InvestigatorMeter fill_rate multiplier during personnel violence stage |
| `karst_mitigation_bonus` | 0.10 | Added to `FacilitySignals.scrutiny_mitigation` for facilities in provinces with `has_karst == true`. Represents physical concealment from cave or underground terrain. |

**Moddability note (facility_types.csv):** Signal weights (`w_power_consumption`, `w_chemical_waste`, `w_foot_traffic`, `w_olfactory`) are per-facility-type columns in `facility_types.csv`. The simulation_config.json constants above are global; per-type weights are local to each facility type definition.

---

### Regulator Scrutiny Meter [V1]

```cpp
enum class RegulatorStatus : uint8_t {
    inactive           = 0,  // net_signal below notice threshold. No active scrutiny.
    notice_filed       = 1,  // >= config.scrutiny.notice_threshold (default: 0.25).
                              //   Compliance notice issued. ConsequenceEntry:
                              //   npc_takes_action (compliance_notice).
    formal_audit       = 2,  // >= config.scrutiny.audit_threshold (default: 0.50).
                              //   Formal audit opened. Regulator gains access to
                              //   facility operational records.
                              //   Evidence token created: documentary (facility records).
    enforcement_action = 3,  // >= config.scrutiny.enforcement_threshold (default: 0.75).
                              //   Fine, permit suspension, or facility order.
                              //   ConsequenceEntry: legal_process_advances.
};

struct RegulatorScrutinyMeter {
    uint32_t regulator_npc_id;
    uint32_t target_facility_id;  // one meter per facility per regulator
    float    current_level;       // 0.0–1.0
    float    fill_rate;           // per-tick increment from regulator_signal
    float    decay_rate;          // per-tick reduction when signal drops;
                                  // default: config.scrutiny.decay_rate (0.001)
    RegulatorStatus status;       // derived each tick from named thresholds
    uint32_t opened_tick;         // tick when status first became formal_audit
    std::vector<uint32_t> evidence_token_ids;
};
```

**Regulator scrutiny integration (Tick Step 13):**

```
// Regulator scrutiny — runs in tick step 13 for all facilities with
// net_signal > 0.0 in each province:

For each regulator NPC in province p:
    For each facility f in p where f.signals.net_signal > 0.0:
        // Regulators read chemical_waste and foot_traffic only
        regulator_signal =
            f.signals.chemical_waste_signature
                × f.type_def.signal_weights.w_chemical_waste
          + f.signals.foot_traffic_visibility
                × f.type_def.signal_weights.w_foot_traffic
        regulator_signal = clamp(
            regulator_signal - f.signals.scrutiny_mitigation, 0.0, 1.0)

        f.scrutiny_meter.fill_rate = clamp(
            regulator_signal × config.scrutiny.detection_to_fill_rate_scale
            + f.regulatory_violation_severity
              × config.scrutiny.violation_to_fill_rate_scale,
            0.0f, config.scrutiny.fill_rate_max)

        f.scrutiny_meter.current_level = clamp(
            f.scrutiny_meter.current_level + f.scrutiny_meter.fill_rate,
            0.0f, 1.0f)
```

**Named constants (all to `simulation_config.json` → `scrutiny`):**

| Constant | Default | Effect |
|---|---|---|
| `notice_threshold` | 0.25 | Level at which compliance notice is filed |
| `audit_threshold` | 0.50 | Level at which formal audit opens |
| `enforcement_threshold` | 0.75 | Level at which enforcement action fires |
| `decay_rate` | 0.001 | Per-tick meter reduction when signal drops |
| `detection_to_fill_rate_scale` | 0.005 | Converts regulator_signal to fill_rate |
| `fill_rate_max` | 0.01 | Cap on fill_rate per tick |
| `violation_to_fill_rate_scale` | 0.008 | Converts violation_severity to fill_rate additive; slightly higher than physical signal scale — documented violations are more actionable than ambient physical signals. |
| `violation_decay_rate` | 0.0005 | Per-tick decay of regulatory_violation_severity when no new violations; ~2,000 ticks (~5.5 years) to halve. |

---

### Criminal Organization Data Structure

```cpp
enum class CriminalOrganizationType : uint8_t {
    street_gang            = 0,
    syndicate              = 1,
    cartel                 = 2,
    transnational_network  = 3,  // [EX — no active AI logic in V1]
};

struct CriminalOrganization {
    uint32_t id;
    CriminalOrganizationType organization_type;
    std::vector<uint32_t> territory_province_ids;  // provinces where org has established presence
    std::vector<uint32_t> income_source_ids;       // NPCBusiness ids with criminal_sector = true
    std::vector<uint32_t> member_npc_ids;
    uint32_t leadership_npc_id;
    uint32_t strategic_decision_tick;              // next quarterly decision; see decision_day_offset
    uint8_t  decision_day_offset;                  // 0–89; set at world gen as hash(id) % 90
                                                   // first decision at world_start_tick + offset; then every 90 ticks
                                                   // mirrors NPCBusiness dispatch_day_offset; spreads quarterly load
    float cash;
    TerritorialConflictState conflict_state;
    std::unordered_map<ProvinceId, float> dominance_by_province;  // Criminal market share per province; range [0.0, 1.0]
};
```

### Territorial Conflict State

```cpp
enum class TerritorialConflictStage : uint8_t {
    none                    = 0,
    economic                = 1,
    intelligence_harassment = 2,
    property_violence       = 3,  // Law enforcement attention rises regardless of corruption coverage
    personnel_violence      = 4,  // InvestigatorMeter fill_rate × config.opsec.personnel_violence_multiplier (default: 3.0)
    open_warfare            = 5,  // Maximum law enforcement response; previously dormant capacity activates
    resolution              = 6,  // Conflict resolved; TerritorialConflictState resets to none
};

struct TerritorialConflictState {
    TerritorialConflictStage conflict_stage;
    uint32_t opposing_org_id;        // 0 if none
    uint32_t escalation_tick;
    uint32_t last_action_tick;
    std::vector<uint32_t> consequence_ids;
};
```

### Territorial Conflict Stage Machine [V1]

**State machine transitions and conditions:**

```
IDLE (none) → PLANNING (economic) → ASSEMBLING (intelligence_harassment) → IN_TRANSIT (property_violence)
  → CONFRONTATION (personnel_violence) → RESOLUTION (open_warfare) → IDLE (none)
```

| From | To | Condition | Tick Step | NPC Stage During Transit |
|---|---|---|---|---|
| none | economic | CriminalOrganization.conflict_state becomes active (rival org detected in shared territory) | 5 | Operatives in territory remain operational |
| economic | intelligence_harassment | `si.territory_pressure >= 0.60` AND decision matrix triggers `initiate_conflict: true` | 5 | Operatives begin harassment campaign; some deployed as intel gathering |
| intelligence_harassment | property_violence | Intelligence operations accumulate evidence against rival; `escalation_tick += 1`; threshold: duration >= 30 ticks | 5 | Operatives shift to property destruction phase; teams dispatched to rival facilities |
| property_violence | personnel_violence | Property damage escalates to direct confrontation; combatants engaged; InvestigatorMeter monitored | 5 | Teams deployed for confrontation; active combat operatives in transit between target sites |
| personnel_violence | open_warfare | Personnel casualties sustained; conflict becomes regional news; institutional response triggered; `escalation_tick >= 90` | 5 | Full assault teams deployed; NPC roles suspended for duration of conflict |
| open_warfare | resolution | Player or mediating NPC initiates peace negotiation; consequence fires; cash settlement; dominance realignment | 5 | Operatives stand down; personnel return to normal operational posts |
| resolution | none | Consequence completes; conflict state reset; both orgs' dominance values updated | 5 | Normal operations resume; no special NPC stage |

**Escalation driver:** Strategic decision tick (quarterly, every 90 ticks). Decision matrix outputs `conflict_escalation = one stage up`. Escalation can also be triggered by scene card player action.

**De-escalation:** Player can negotiate via scene card system (if regional_authority >= 0.60 and player has positive relationships with both org leadership). Skips to `resolution` stage; consequences applied.

**Escalation trigger:** Stage advances when the offending organization's strategic decision produces an action crossing a stage boundary. One stage per decision cycle by default. Player can escalate or de-escalate via scene card system.

### NPC Criminal Organizations and the Economy

NPC criminal organizations use the same `RegionalMarket` and supply chain infrastructure as legitimate NPC businesses. An NPC criminal organization is modeled as a `CriminalOrganization` record plus one or more `NPCBusiness` records with `criminal_sector = true`, referencing goods in the informal/black market layer.

Tick step 1 (production outputs) includes criminal facility production into `RegionalMarket` for that good via the informal market overlay. Tick step 5 (price update) computes the informal market spot_price and equilibrium_price using the same equilibrium formula as the formal market, with a separate price per layer.

Architectural consequence: player competition with NPC criminal orgs is genuine economic competition in the same simulation step. Money laundering moves value from informal market revenue to formal market positions — this transition is a ledger operation between market layers.

**[V1 constraint]:** V1 NPC criminal organizations operate in the drug supply chain and protection racket sectors only, matching the V1 Feature Tier List scope.

### NPC Criminal Organization Strategic Decision Logic

Strategic decisions are made quarterly. Uses the same architecture as legitimate NPCBusiness quarterly decisions (Section 5) with criminal-specific inputs.

```cpp
struct CriminalStrategicInputs {
    float territory_pressure;    // sum of competing orgs' dominance_by_province in shared provinces
    float cash_level;            // CriminalOrganization.cash
                                  // / (monthly_operating_cost_estimate
                                  //    × config.business.cash_comfortable_months)
                                  // monthly_operating_cost_estimate: computed from active
                                  // facility costs + member payroll each tick.
                                  // Uses same config.business constants as NPCBusiness —
                                  // the threshold for "comfortable cash reserves" is not
                                  // specific to criminal operations.
                                  // cash_level >= 1.0 → org has comfortable reserves
                                  // cash_level < 0.5 → org in cost-cutting territory
    float law_enforcement_heat;  // max(InvestigatorMeter.current_level) across all le NPCs in regions
    float rival_activity;        // sum of rival territorial actions against this org in last 30 ticks
};
```

**Decision matrix:**
```
IF si.law_enforcement_heat >= 0.60:
    { reduce_facility_activity: true, accelerate_laundering: true, initiate_conflict: false }

ELSE IF si.territory_pressure >= 0.60 AND si.cash_level >= 1.0:
    { initiate_conflict: true, target = argmax(rival dominance in shared regions),
      conflict_escalation = one stage up }

ELSE IF si.cash_level < 0.50:
    { reduce_headcount: true, increase_price_pressure: true, expand_territory: false }

ELSE IF si.territory_pressure < 0.30 AND si.law_enforcement_heat < 0.30:
    { expand_territory: true, target_region = adjacent_region_with_lowest_competition }

ELSE:
    { maintain: true }

Set strategic_decision_tick = current_tick + 90.
```

**Leadership NPC influence:** `leadership_npc_id` NPC motivation vector modifies decision thresholds as additive offsets. High `money` motivation weights toward cash maximization; high `power` toward territorial dominance; high `survival` toward reducing law enforcement heat.

**[RISK]** The simplified decision matrix is the V1 approximation. Replace with full utility-function evaluation using the NPC behavior engine's expected_value framework in later production passes.

---

## 17. Financial Markets [V1]

Covers stock market (B-17) and government bond market (M-06). Both systems feed the investor career path and connect to the macro-economy via business performance and fiscal policy. Processing runs at **tick step 19** (after income/dividend settlement).

### 17.1 — Stock Market [V1]

#### StockListing

```cpp
struct StockListing {
    uint32_t  id;
    uint32_t  npc_business_id;          // or player_business_id for player-owned listed entity
    float     shares_outstanding;        // total shares in existence
    float     public_float_pct;          // 0.0–1.0; fraction of shares tradeable on open market
    float     current_price;             // game currency per share; updated each tick step 19
    float     ipo_price;                 // reference; set at listing time
    uint32_t  listed_tick;
    float     trailing_eps;             // earnings per share; rolling 30-tick average
    float     dividend_per_share;       // paid each month tick (TICKS_PER_MONTH); 0.0 if no dividend policy
    float     book_value_per_share;     // assets − liabilities / shares_outstanding
};
```

#### StockPortfolio

```cpp
struct StockPortfolio {
    uint32_t                      owner_id;   // NPC id or player id
    std::map<uint32_t, float>     holdings;   // listing_id → shares held
    float                         unrealized_gain; // for display; recomputed at tick step 19
};
```

#### Stock Price Model

Price updates run at tick step 19 for each listed stock:

```
price_new = price_old
    × (1.0 + earnings_momentum × config.stock.earnings_weight)
    × (1.0 + sentiment_factor  × config.stock.sentiment_weight)
    × (1.0 + random_noise)

earnings_momentum = (trailing_eps − prev_trailing_eps) / max(prev_trailing_eps, 0.01)

sentiment_factor  = (province_conditions.investor_confidence − 0.5) × 2.0
                  // maps [0,1] confidence to [-1,+1]

random_noise      = normal_distribution(mean=0.0, stddev=config.stock.daily_volatility)

clamp price_new to [ipo_price × config.stock.price_floor_fraction, ∞)
```

**IPO mechanics:** An NPCBusiness or player-owned business with revenue > `config.stock.ipo_revenue_threshold` for at least `config.stock.ipo_qualifying_ticks` consecutive ticks may list. IPO sets `current_price = book_value_per_share × config.stock.ipo_price_to_book`. `public_float_pct` defaults to 0.30 at IPO.

**Dividend:** If `trailing_eps > 0` and business profile is `defensive_incumbent` or player elects dividend policy, `dividend_per_share = trailing_eps × config.stock.dividend_payout_ratio` is distributed to all shareholders each month tick.

#### Named Constants — Stock Market

All in `simulation_config.json → stock`:

| Constant | Default | Notes |
|---|---|---|
| `stock.earnings_weight` | 0.60 | Fraction of daily price movement explained by EPS momentum |
| `stock.sentiment_weight` | 0.30 | Fraction explained by regional investor confidence |
| `stock.daily_volatility` | 0.015 | Std dev of daily random noise (~1.5% per tick) |
| `stock.price_floor_fraction` | 0.10 | Stock cannot fall below 10% of IPO price (floor = delisting) |
| `stock.ipo_revenue_threshold` | 500000.0 | Monthly revenue required to qualify for IPO |
| `stock.ipo_qualifying_ticks` | 360 | ~12 months sustained above threshold |
| `stock.ipo_price_to_book` | 2.5 | IPO price set at 2.5× book value |
| `stock.dividend_payout_ratio` | 0.40 | 40% of EPS paid as dividend when policy is active |

---

### 17.2 — Government Bond Market [V1]

#### GovernmentBond

```cpp
struct GovernmentBond {
    uint32_t    id;
    uint32_t    nation_id;
    float       face_value;             // game currency; principal repaid at maturity
    float       coupon_rate;            // annual interest rate; fixed at issuance
    float       current_yield;          // market yield; rises as price falls (inversely related)
    float       current_price;          // market price; updated at tick step 19
    float       credit_rating;          // 0.0–1.0; 1.0 = AAA; computed from fiscal health
    uint32_t    maturity_ticks;         // ticks from issuance until principal repayment
    uint32_t    issued_tick;
    uint32_t    maturity_absolute_tick; // issued_tick + maturity_ticks
};
```

#### BondHolding

```cpp
struct BondHolding {
    uint32_t    bond_id;
    uint32_t    holder_id;              // NPC id or player id
    float       quantity;               // number of bond units held
    float       purchase_price;         // for unrealized gain/loss calculation
};
```

#### Bond Price and Yield Model

Updated at tick step 19:

```
credit_rating = fiscal_health_score(nation)
    = clamp(1.0 − debt_to_gdp × config.bond.debt_rating_sensitivity
                − deficit_fraction × config.bond.deficit_rating_sensitivity, 0.0, 1.0)

risk_premium    = (1.0 − credit_rating) × config.bond.max_risk_premium
current_yield   = config.bond.base_risk_free_rate + risk_premium
current_price   = face_value × coupon_rate / current_yield   // simplified bond pricing
                  // Note: ignores duration; acceptable for V1 approximation

// Fiscal cascade (tick step 19):
// If nation.deficit_fraction > config.bond.deficit_pressure_threshold:
//     bond yield rises → existing bond prices fall → investor wealth effect
//     nation borrowing cost increases next month tick
//     if credit_rating < config.bond.credit_warning_threshold:
//         generate ConsequenceQueue entry: fiscal_pressure_warning
```

**Auction mechanics:** Nations issue new bonds at the start of each simulated quarter (every `TICKS_PER_MONTH × 3` ticks) when `fiscal_deficit > 0`. Auction price = face value. NPC investors and player can subscribe. Oversubscription raises effective price; undersubscription forces yield increase at next auction.

#### Named Constants — Bond Market

All in `simulation_config.json → bond`:

| Constant | Default | Notes |
|---|---|---|
| `bond.base_risk_free_rate` | 0.04 | 4% annual baseline yield for AAA-rated sovereign |
| `bond.max_risk_premium` | 0.18 | Maximum additional yield for near-default (credit = 0.0) |
| `bond.debt_rating_sensitivity` | 0.60 | How much debt/GDP ratio degrades credit rating |
| `bond.deficit_rating_sensitivity` | 0.40 | How much annual deficit fraction degrades credit |
| `bond.deficit_pressure_threshold` | 0.05 | 5% deficit/GDP triggers fiscal cascade |
| `bond.credit_warning_threshold` | 0.40 | Below this, fiscal_pressure_warning fires |
| `bond.default_maturity_ticks` | 3650 | Standard 10-year bond at world load |

---

## 18. Trade Infrastructure [V1]

### 18.1 — National Trade Offer

```cpp
struct GoodOffer {
    uint32_t good_id;
    float    quantity_available;      // units per simulated month
    float    offer_price;             // per unit; production cost + margin
};

struct NationalTradeOffer {
    uint32_t nation_id;
    uint32_t tick_generated;
    std::vector<GoodOffer> exports;   // what this nation will sell
    std::vector<GoodOffer> imports;   // what this nation wants to buy (bids)
    // Regenerated by LOD 1 monthly update
};
```

### 18.2 — Global Commodity Price Index

```cpp
struct GlobalCommodityPriceIndex {
    uint32_t last_updated_tick;
    std::map<uint32_t, float> lod2_price_modifier;
    // good_id → multiplier on base spot price
    // Values > 1.0: LOD 2 world is net consuming (scarcity signal)
    // Values < 1.0: LOD 2 world is net producing (surplus signal)
    // Updated annually during LOD 2 batch job
    // Applied to LOD 0 and LOD 1 equilibrium price calculations
};
```

### 18.3 — Tariff Schedule and Trade Agreements

```cpp
struct TradeAgreement {
    uint32_t partner_nation_id;
    float    tariff_reduction;        // 0.0 = no effect; 1.0 = full free trade
    uint32_t signed_tick;
    uint32_t expires_tick;            // 0 = permanent unless dissolved
    bool     is_active;
};

struct TariffSchedule {
    uint32_t nation_id;
    std::map<uint32_t, float> good_tariff_rates;  // good_id → ad valorem rate
    float    default_tariff_rate;
    std::vector<TradeAgreement> trade_agreements;
};
```

`TariffSchedule` is embedded in the `Nation` struct (see Section 12) as `TariffSchedule tariff_schedule`. `std::vector<TariffSchedule> tariff_schedules` is also added to `WorldState` as the top-level index (one per nation).

### 18.4 — Physical Transport: Information vs. Physics Asymmetry

**Information travels at communication speed.** Price signals, intelligence reports, wire transfers, and device notifications propagate within the same tick — they are not delayed by distance. A player learns about a price spike in Province B the tick it happens.

**Physical goods travel at physics speed.** Steel, food, cocaine, and machinery obey distance and infrastructure. A player can *know* about an opportunity in Province B instantly but their goods take multiple ticks to arrive. By the time they arrive, the opportunity may be gone. This lag is not a bug — it is the primary tension the player navigates.

### 18.5 — Transport Mode Speeds

| Mode | Speed (km/tick) | Requires | Notes |
|---|---|---|---|
| Road (truck) | 800 | `infrastructure_rating > 0` | Always available; degrades with terrain |
| Rail (freight) | 700 | `rail_connectivity > 0` at both ends | Faster on dedicated freight; limited routes |
| Sea (cargo) | 900 | `port_capacity > 0` at both ends | Cheapest per tonne; slow to load/unload |
| River barge | 450 | `river_access > 0` along route | Very cheap; limited to river corridors |
| Air freight | 10,000 (always 1 tick) | Always available | ~50× road cost; viable only for high-value urgent goods. [V1] Air freight applies to goods only. NPCs do not use air transport in V1 (no air travel in the province route table for NPC travel dispatch). NPC air travel is a future research tree milestone (see §18.15). Any transit dispatch for NPC movement must select from: road, rail, sea, river. |

All mode speed constants in `transport.json`.

### 18.6 — Transit Time Formula

```
route_distance_km    = province_route_table[origin_id][dest_id][mode].distance_km

base_transit_ticks   = ceil(route_distance_km / mode_speed_km_per_tick)

terrain_delay        = 1.0 + (route_terrain_roughness × config.terrain_delay_coeff)

infra_delay          = 1.0 + ((1.0 - min_infrastructure_along_route) × config.infra_delay_coeff)

transit_ticks        = max(1, round(base_transit_ticks × terrain_delay × infra_delay))
```

Same-province shipments: `transit_ticks = 0`. Intra-province distribution is abstracted.

Named constants in `transport.json`:

| Constant | Default | Effect |
|---|---|---|
| `terrain_delay_coeff` | 0.4 | Mountain crossing adds up to 40% transit time |
| `infra_delay_coeff` | 0.6 | Poor roads add up to 60% transit time |
| `transit.max_concealment_modifier` | 0.40 | Upper bound on route_concealment_modifier for any concealed shipment. Concealed route reduces interception risk by at most 40%. Caps benefit regardless of route profile. route_concealment_modifier is set at dispatch from the selected RouteProfile and stored on TransitShipment (see §18.9). |
| `routes.karst_concealment_bonus` | 0.08 | Concealment bonus added to RouteProfile.concealment_bonus for each karst province (`has_karst == true`) along the route path. Stacks across multiple karst provinces but is still capped by `transit.max_concealment_modifier` at dispatch. |

### 18.7 — Transport Cost Formula

```
transport_cost_per_unit =
    (distance_km × config.base_transport_rate)
    × terrain_cost_modifier
    × (1.0 + (1.0 - min_infrastructure_along_route) × config.infra_cost_coeff)
    × route_mode_modifier
    × good.physical_size_factor
    × (1.0 + good.perishable_decay_rate × transit_ticks)   // 0.0 for non-perishables

Where:
    terrain_cost_modifier    — 1.0 (flat) to 2.5 (mountain crossing)
    route_mode_modifier      — road: 1.0; rail: 0.45; sea: 0.30; river: 0.60; air: 50.0
                               // Rail modifier is lower than road (cheaper per unit-km on established
                               // freight lines) but higher than sea (capital cost of rail infrastructure
                               // vs. bulk cargo economics). Value in transport.json.
    good.physical_size_factor — 0.0 (financial instruments) to 2.0 (bulk ore)
    transit_ticks            — actual computed transit duration from §18.6 formula
```

**All transport constants live in `transport.json`.**

**Calibration anchor:** Maritime bulk shipping in year 2000 was approximately $8–12/tonne for a 10,000km route. `base_transport_rate` should produce ~2–3% of commodity `base_price` per tonne for a sea route of that distance. Calibrated during Phase 3 pipeline validation; anchor documented in `transport.json` alongside the value.

### 18.8 — Route Precomputation

A `province_route_table` is computed once at world load via Dijkstra on the province adjacency graph. For each province pair and each transport mode:

```cpp
struct RouteProfile {
    float    distance_km;
    float    route_terrain_roughness;      // max roughness along path
    float    min_infrastructure;           // Minimum infrastructure level along route; range [0.0, 1.0]; affects transit time
    uint8_t  hop_count;                    // province boundaries crossed
    bool     requires_sea_leg;
    bool     requires_rail;
    float    concealment_bonus;            // additive bonus to route_concealment_modifier on any
                                           // TransitShipment dispatched along this route.
                                           // Computed at route table build time as:
                                           //   sum(config.routes.karst_concealment_bonus
                                           //       for each province in province_path where has_karst == true)
                                           // 0.0 for routes with no karst provinces.
                                           // Capped by transit.max_concealment_modifier at dispatch.
    std::vector<uint32_t> province_path;   // intermediate provinces
};

// Stored at world load. 6×6×5 entries for V1 = 180 RouteProfiles. Trivial memory cost.
std::map<std::pair<uint32_t,uint32_t>, std::array<RouteProfile, 5>> province_route_table;
//        origin_id, dest_id                                           one per TransportMode
```

Route lookup at dispatch: O(1). Route table rebuilt when a province's infrastructure changes — at most once per tick, negligible cost.

### 18.9 — TransitShipment

```cpp
enum class TransportMode : uint8_t {
    road = 0, rail = 1, sea = 2, river = 3, air = 4,
};

enum class ShipmentStatus : uint8_t {
    in_transit   = 0,
    arrived      = 1,   // processed this tick; goods deposited into destination market
    intercepted  = 2,   // criminal shipment seized; evidence token generated
    lost         = 3,   // weather event / accident; goods destroyed
};

struct TransitShipment {
    uint32_t      id;
    uint32_t      good_id;
    float         quantity_dispatched;
    float         quantity_remaining;       // degrades each tick for perishables
    float         quality_at_departure;
    float         quality_current;          // degrades each tick for perishables
    uint32_t      origin_province_id;
    uint32_t      destination_province_id;
    uint32_t      owner_id;                 // player_id or npc_business_id
    uint32_t      dispatch_tick;
    uint32_t      arrival_tick;             // precomputed at dispatch; immutable
    TransportMode mode;
    float         cost_paid;               // deducted at dispatch
    bool          is_criminal;             // informal route; interception checks active
    float         interception_risk_per_tick; // computed at dispatch; 0.0 for legitimate
    bool          is_concealed;             // true if criminal operator selected alternate low-visibility route
                                            // longer transit time; lower interception_risk_per_tick
    float         route_concealment_modifier; // 0.0 (direct route) to config.transit.max_concealment_modifier
                                            // computed at dispatch from RouteProfile selection; stored here for
                                            // use in per-tick interception checks (§18.10)
    ShipmentStatus status;
};
```

`TransitShipment` entries live in the `DeferredWorkQueue` with `type = transit_arrival` and `due_tick = arrival_tick`. Criminal shipments additionally have one `interception_check` item per in-transit tick queued at dispatch time. All transit processing partitions by `destination_province_id` — each province worker handles arrivals into that province in parallel.

### 18.10 — Interception Model

Criminal shipments in transit are detectable each tick they are in motion. **Work is spread across transit duration** — not concentrated at arrival — consistent with the DeferredWorkQueue event-driven principle.

```
interception_risk_this_tick =
    base_interception_risk
    × (1.0 - corruption_coverage_along_route)
    × (1.0 + law_enforcement_heat_along_route)
    × good.physical_size_factor
    × mode_visibility_modifier              // road: 1.0; sea: 0.6; air: 1.8 (customs)
    × (1.0 - route_concealment_modifier)    // alternate routes reduce visibility
```

If `DeterministicRNG(world_seed, current_tick, shipment_id) < interception_risk_this_tick`:
- `shipment.status` → `intercepted`; remaining interception checks cancelled
- `EvidenceToken { type: physical_seizure }` pushed to origin + intercepting province pools
- `ConsequenceEntry { type: raid_executed }` queued for destination province
- `ConsequenceEntry { type: investigation_opens }` queued for the intercepting LE NPC

**Route concealment:** Criminal operators can select alternate routes — longer, slower, lower infrastructure, lower LE visibility. The route profile for a concealed route is longer in `distance_km` and lower in `min_infrastructure` (which increases `infra_delay` and transit time) but carries a `route_concealment_modifier > 0`. The route selection decision is a genuine trade-off: fast high-exposure route vs. slow low-exposure route. `is_concealed` and `route_concealment_modifier` are set on `TransitShipment` at dispatch time (read from the selected `RouteProfile`; stored per-shipment for use in per-tick interception checks above).

**Strategic implication:** Product is most vulnerable during transit. An operation shipping from a production province to a distribution province is exposed for the entire transit window. At scale, more shipments in transit simultaneously = more interception surface. Growth creates risk — which is correct.

### 18.11 — Supply Chain Buffering

Factories depending on inputs from another province need warehouse buffer stock to cover transit time. A 2-tick transit means the factory needs at least 2 ticks of buffer stock or it idles. Buffer stock is capital tied up in inventory. Supply chain disruptions (weather, infrastructure damage, police action) extend transit times — players without buffer stock absorb disruptions immediately; players with buffer stock absorb them smoothly. This is existing mechanics (input_availability in the production formula) now operating against real physical delay rather than instantaneous arrival.

### 18.12 — Information vs. Physics: Infrastructure Ownership

Owning logistics infrastructure between provinces gives:
- Lower transport cost per unit (transport formula, already modelled)
- Shorter effective transit time (owner's throughput bonus, stored in `RouteProfile` per owner)
- Visibility into competitor shipments using the same infrastructure (intelligence value)
- Toll-booth pricing power on third-party shipments

The third point is new — owning the port or rail line means the player's NPC network is informed of significant shipments passing through, generating intelligence evidence tokens in their contact network. This is optional and not visible to the shipper.

### 18.13 — Informal Economy Price Formula

```
black_market_price = formal_spot_price
    × (1.0 + tariff_rate + enforcement_risk_premium)
    × quality_premium_modifier

Where:
    tariff_rate               — the tariff on the legal equivalent good (or 0 if none)
    enforcement_risk_premium  = base_risk_premium × (1.0 - corruption_coverage)
                                  × (1.0 + law_enforcement_heat)
                                base_risk_premium is set in criminal goods data file
    quality_premium_modifier  — standard quality system formula

For goods with no formal equivalent:
    black_market_price = cost_of_production × markup_coeff
    markup_coeff is set per-good in criminal goods data file; default: 3.0
```

**Smuggling profitability scales with tariffs:** A 40% tariff creates a 40% margin premium for criminal importers before any other markup. High-tariff environments are also high-criminal-logistics-demand environments. This is intentional.

### 18.14 — Physics Principle: All Mass Obeys Transit

**If it has mass, it obeys the transit system.** This is the canonical rule. It applies to:

- Goods (§18.6–18.12)
- People — NPCs and player character — have physical bodies; travel between provinces uses the same route table and transit time formula
- Imports from LOD 1 nations — goods cross physical distance (§18.16)
- Criminal org territory expansion — personnel physically move to establish presence (§18.17)

Information (phone calls, wire transfers, news propagation, evidence sharing, price signals) has no mass and travels within the same tick. The information vs. physics asymmetry (§18.4) applies everywhere. If there is ever ambiguity about whether a new system should have transit time, this rule resolves it: does it have mass? Yes → transit. No → instant.

The research tree may introduce future technologies (hyperloop logistics, drone delivery, telemedicine) that modify transit speeds. No such technologies exist in the 2000 start era. Any reduction in transit time must be modelled as a named constant improvement via R&D progression, not by bypassing the transit formula.

---

### 18.15 — NPC Physical Movement [V1]

NPCs have physical bodies. When an NPC's decision requires them to be in a different province — meeting a contact, establishing territory, attending a political event, fleeing law enforcement — they travel physically. Travel consumes time. During travel, they cannot act in their origin or destination province.

#### NPCTravelStatus

```cpp
enum class NPCTravelStatus : uint8_t {
    resident   = 0,  // In home_province_id. Available for local actions. Default state.
    in_transit = 1,  // Physically moving between provinces. Cannot act at origin or destination.
                     // Criminal NPCs in transit carry interception risk (see interception model, §18.10).
    visiting   = 2,  // In a non-home province temporarily (arrived, not yet returned).
                     // Can act in current_province_id. Returns to home_province_id when
                     // visit reason concludes (meeting complete, campaign over, etc.).
};
```

#### NPC Travel Dispatch

When the behavior engine generates an action requiring physical presence in province P:

```
IF npc.current_province_id == P:
    action executes this tick (NPC is already there)
ELSE:
    transit_ticks = compute_transit_ticks(npc.current_province_id, P, road)
    // NPCs always travel by road unless criminal with sea access; mode selection below
    npc.travel_status → in_transit
    push DeferredWorkItem {
        type:       npc_travel_arrival,
        subject_id: npc.id,
        due_tick:   current_tick + transit_ticks,
        payload:    { destination_province_id: P, reason: action_type }
    }
    // The action that required travel is queued behind the travel item
    // and fires as a consequence at due_tick + 0 (same tick as arrival)
```

On `npc_travel_arrival` fires:
```
npc.current_province_id → payload.destination_province_id
npc.travel_status → visiting (if destination != home) or resident (if returning home)
// Action that triggered travel now executes
```

#### NPC Travel Mode Selection

| NPC type | Default mode | Notes |
|---|---|---|
| All non-criminal NPCs | Road | Always available; uses road_speed_km_per_tick |
| Criminal NPC (known to LE) | Road, low-visibility route | Applies route_concealment_modifier |
| Criminal NPC (low heat) | Road, direct route | Standard road transit |
| NPC with `sea_access` flag | Sea if faster | Port availability at both ends required |

Air freight rates apply to goods; NPCs do not fly in V1. (Research tree: air travel reduces NPC transit time to 1 tick for inter-province movement — future tech milestone, not V1 scope.)

#### NPC Interception During Travel

Criminal NPCs in transit check interception using the same model as criminal shipments (§18.10), treating the NPC as a `TransitShipment` with `good.physical_size_factor = 1.0` and mode-appropriate `mode_visibility_modifier`. If intercepted:

```
npc.status → imprisoned
DeferredWorkItem { type: consequence, consequence_type: arrest_warrant_issued } queued immediately
travel cancelled; npc.travel_status → resident (detained in intercepting province)
```

#### Actions Requiring Physical Presence

The following action types require NPC physical presence in the target province. If the NPC is not there, travel is dispatched before the action executes:

- `meet_contact` — requires both parties in same province
- `establish_territory` — requires NPC in target province (§18.17)
- `attend_political_event` — candidate must be in constituency province
- `conduct_surveillance` — investigator must be in target facility's province
- `deliver_threat` — enforcer must be in target NPC's province
- `testify` — witness must be in court's province

Actions executable remotely (phone, wire, message) do not trigger travel:
- `share_evidence`, `phone_call`, `wire_transfer`, `place_order`, `send_message`, `publish_story` (digital)

---

### 18.16 — LOD 1 Import Transit [V1]

Goods imported from LOD 1 nations have physical mass. They travel from the exporting nation's geographic centroid to the importing LOD 0 province's port. This transit time is real — it affects when goods enter the province's `supply` and therefore when the price signal responds.

#### LOD 1 Import TransitShipment

When a LOD 0 province accepts a LOD 1 trade offer, a `TransitShipment` is created:

```cpp
// On LOD 1 trade offer acceptance:
TransitShipment lod1_import {
    .id                      = next_shipment_id(),
    .good_id                 = offer.good_id,
    .quantity_dispatched     = accepted_quantity,
    .quantity_remaining      = accepted_quantity,
    .quality_at_departure    = lod1_default_quality,   // per-good in goods data file
    .quality_current         = lod1_default_quality,
    .origin_province_id      = LOD1_SENTINEL_PROVINCE_ID,  // sentinel; not a real province
    .destination_province_id = accepting_province_id,
    .owner_id                = LOD1_NATION_OWNER_ID,    // sentinel; not a player or NPC business
    .dispatch_tick           = current_tick,
    .arrival_tick            = current_tick + lod1_transit_ticks(nation, province, good),
    .mode                    = TransportMode::sea,       // LOD 1 nations trade by sea by default
    .cost_paid               = offer.offer_price × accepted_quantity,
    .is_criminal             = false,
    .interception_risk_per_tick = 0.0f,               // LOD 1 imports not subject to interception model
    .status                  = ShipmentStatus::in_transit,
};
```

#### LOD 1 Transit Time Formula

```
lod1_transit_ticks(nation, dest_province, good) =
    max(1, ceil(nation.geographic_centroid_distance_to(dest_province.geography.port_location)
                / transport_config.sea_speed_km_per_tick))
    × lod1_transit_variability_multiplier

Where:
    sea_speed_km_per_tick       = 900 (from transport.json; same constant as §18.5)
    lod1_transit_variability_multiplier = 1.0–1.3 (per-nation; stored in
                                          `nation.lod1_profile->lod1_transit_variability_multiplier`
                                          (see §12 Lod1NationProfile); seeded from world.json at load;
                                          represents port congestion, customs delay, routing)
    nation.geographic_centroid  = stored in nation data file (lat/lon); pipeline-seeded from
                                  real-world country centroids at world load
```

**LOD 1 transit does not use the full `RouteProfile` table** — LOD 1 nations are not in the province adjacency graph. Distance is computed directly from geographic centroid to destination port. No terrain or infrastructure modifiers. No route concealment. No criminal interception. LOD 1 trade is abstracted; only the physics of distance is preserved.

**Price ceiling effect:** LOD 1 trade offer `offer_price` sets the `import_price_ceiling` in `RegionalMarket`. This ceiling applies as soon as the offer is accepted — even while the goods are in transit. The price cannot exceed what importers will eventually pay. The supply effect (goods arriving in the `supply` field) is delayed by transit time.

**Price ceiling timing clarification (TDD-P1):** Price ceiling is evaluated at the tick the offer is ACCEPTED (not created). The ceiling uses the price_index from the PREVIOUS tick's market clearing (tick t-1). This prevents circular dependencies between current-tick pricing and trade acceptance.

---

### 18.17 — Criminal Territory Expansion Transit [V1]

When a criminal organization's decision matrix issues `expand_territory: true`, the expansion is not instantaneous. Personnel must physically travel to the target province, establish relationships with local contacts, and begin operations. This takes time — and those personnel are vulnerable during transit.

#### Expansion Procedure

```
CriminalOrg strategic decision: expand_territory to province P

    Step 1: Select expansion team
        team_npc_ids = select N members from org.member_npc_ids where:
            npc.current_province_id == org.leadership_province_id
            AND npc.role in {criminal_operator, criminal_enforcer}
        N = max(config.criminal.min_expansion_team_size, ceil(org.cash / CASH_PER_EXPANSION_SLOT))
        // CASH_PER_EXPANSION_SLOT: simulation_config.json → criminal.cash_per_expansion_slot; default: 5000.0
        // min_expansion_team_size: simulation_config.json → criminal.min_expansion_team_size; default: 2

    Step 2: Dispatch team — each member travels physically
        For each npc in team_npc_ids:
            dispatch npc travel to province P (§18.15 travel model)
            // Transit time: road distance org.leadership_province → P
            // Personnel in transit are exposed to interception

    Step 3: Queue establishment deferred work
        push DeferredWorkItem {
            type:       consequence,
            due_tick:   current_tick + max(travel_ticks for all team members),
            payload:    ConsequenceEntry {
                type:   territory_established,
                payload: { org_id, province_id: P, initial_dominance: 0.05 }
            }
        }
        // dominance_by_province[P] remains 0.0 until this item fires

    Step 4: While team is in transit
        org cannot conduct operations in province P
        org.cash reduced by expansion_cost (personnel payments, logistics)
        Each team member uses NPC interception model for criminal transit (§18.10)
        If any team member is intercepted: that member is removed from team;
            if team drops below 2 members: expansion fails, refund remaining cash
```

**Dominance initialisation on arrival:** When the `territory_established` consequence fires, `org.dominance_by_province[P] = 0.05`. This is the seed from which dominance grows through subsequent operations. It is not 0.0 (not present) until that moment.

**Strategic implication:** A criminal org sending its best operators to a new province leaves its existing territory undermanned during their transit window. A rival org (or the player) that detects the expansion can exploit the coverage gap. Detection path: surveillance of the departing personnel creates evidence tokens; their absence from normal operational locations is observable via investigator meter signals dropping.

**Named constants (`simulation_config.json` → `criminal`):**

| Constant | Default | Effect |
|---|---|---|
| `cash_per_expansion_slot` | 5000.0 | Cash cost per personnel slot in the expansion team. Determines team size via N = max(min_expansion_team_size, ceil(org.cash / cash_per_expansion_slot)). |
| `min_expansion_team_size` | 2 | Minimum number of personnel required to attempt territorial expansion. Expansion fails if interception reduces team below this count. |

> Test: When a criminal expansion team is intercepted during transit and surviving team_size falls below MIN_EXPANSION_TEAM_SIZE, the expansion attempt fails. The initiating CriminalOrganization receives a cash refund of EXPANSION_REFUND_FRACTION × initial_investment. Surviving NPCs return to origin province.
>
> Seed setup: Criminal org sends expansion team of 5 (MIN_EXPANSION_TEAM_SIZE = 4). Law enforcement intercepts and arrests 2 members at tick 10. Assert: team_size = 3 < MIN_EXPANSION_TEAM_SIZE → expansion fails. Cash refund = 0.5 × initial_investment returned. 3 surviving NPCs return to origin.

---

### Lod1NationStats

```cpp
struct Lod1NationStats {
    std::map<uint32_t, float> production_by_good;    // good_id → estimated units/month
    std::map<uint32_t, float> consumption_by_good;   // good_id → estimated units/month
};
```

Written by the LOD 1 monthly update; read by the LOD 2 annual batch. Stored in `WorldState.lod1_national_stats` as `std::map<uint32_t, Lod1NationStats>` (nation_id → stats).

---

## 19. Random Events [V1]

Random events fire via a Poisson process. This models "genuinely random in timing" correctly — Poisson events have no memory (previous events don't affect when the next one fires) and their rate is adjustable by regional conditions. Random events are processed at **tick step 25** for each LOD 0 province.

### 19.1 — Probability Model

```
Random Event Tick Processing (tick step 25):

For each Province at LOD 0:

    adjusted_rate = config.random_event_base_rate
        × (1.0 + province.climate.climate_stress_current × config.climate_event_amplifier)
        × (1.0 + province.conditions.instability × config.instability_event_amplifier)
        × economic_volatility_modifier(province)

    p_event_this_tick = 1.0 - exp(-adjusted_rate / config.ticks_per_month)

    if random_float(0.0, 1.0) < p_event_this_tick:
        fire_random_event(province, current_tick)
```

**Config constants (all in `simulation.json`):**

| Constant | Proposed default | Effect |
|---|---|---|
| `random_event_base_rate` | 0.15 | Events per province per simulated month at baseline (≈1.8/year) |
| `climate_event_amplifier` | 1.5 | High-climate-stress provinces receive more natural events |
| `instability_event_amplifier` | 1.0 | Unstable provinces receive more human events |
| `ticks_per_month` | Architecture constant (not in config) | Used as denominator in Poisson formula |
| `events.evidence_severity_threshold` | 0.3 | Minimum accident severity at which an evidence token is generated. Below this: local memory entries only. |

### 19.2 — Event Type Selection

When an event fires, event type is selected by weighted random draw. Weights are conditioned on regional state:

```
weights = {
    natural_event:   0.25 × (1.0 + province.climate.climate_stress_current),
    accident_event:  0.20 × (1.0 + (1.0 - province.infrastructure_rating)),
    economic_event:  0.30 × economic_volatility_modifier(province),
    human_event:     0.25 × (1.0 + province.conditions.instability),
}
// Normalize weights to sum to 1.0 before random draw
```

### 19.3 — Event Effect Model

Each event type writes to the DeltaBuffer. Delta types by event category:

```
natural_event effects:
    → RegionConditions.agricultural_output_modifier: −0.05 to −0.40 (drought/flood scale)
    → Province.deposits[affected_type].accessibility: +0.0 to −0.20 (landslide, flood)
    → RegionConditions.infrastructure_damage: +0.01 to +0.15
    → WorldState.evidence_pool: push EvidenceToken(type: environmental_incident) [optional]

accident_event effects:
    → specific Facility.output_rate: −0.10 to −1.0 (partial to full disruption)
    → RegionConditions.infrastructure_damage: +0.01 to +0.05
    → NPC memory entries for workers at facility (witnessed_physical_hazard)
    → generates EvidenceToken { type: industrial_accident, subject_id: facility.owner_id }
      unconditionally when accident severity >= config.events.evidence_severity_threshold
      (default: 0.3). subject_id is the facility owner — player or NPC business id.
      The token is created regardless of who owns the facility.

economic_event effects:
    → RegionalMarket.spot_price[affected_good]: ±0.10 to ±0.40 (spike or crash)
    → NPCBusiness.cash: ±modifier (credit tightening or investment opportunity)
    → NPC memory entries for affected business owners

human_event effects:
    → NPC memory entries for witnesses
    → CommunityState: grievance or cohesion modifier
    → may generate new NPC (promotion from background cohort)
    → may generate scene card for player if in affected province
```

### 19.4 — Effect Templates

Each event type has 5–8 named templates (e.g., `drought_mild`, `drought_severe`, `industrial_fire`, `credit_crunch`, `labor_dispute`). Templates specify the delta magnitudes and which fields are affected. Template selection is weighted by regional conditions: drought templates are more likely in low-precipitation provinces; `credit_crunch` is more likely during economic instability.

**Templates are data-driven.** Template definitions live in `/data/events/event_templates.json`. The engine reads them at startup. Modders can add new event templates without engine changes.

### 19.5 — Data Structures [V1]

```cpp
enum class RandomEventType : uint8_t {
    natural   = 0,   // earthquakes, floods, droughts, pandemics
    accident  = 1,   // industrial incidents, transport failures, lab explosions
    human     = 2,   // key NPC deaths, political crises, competitor breakthroughs, strikes
    economic  = 3,   // market shocks, commodity spikes, currency crises, drug market collapse
};

// Schema for /data/events/event_templates.json — one entry per named template.
// Engine reads into std::vector<RandomEventTemplate> at startup.
struct RandomEventTemplate {
    std::string       template_key;         // e.g. "drought_mild", "industrial_fire"
    RandomEventType   type;
    float             base_weight;          // base selection weight within its type category
    float             severity_min;         // 0.0–1.0 range for this template
    float             severity_max;
    uint32_t          duration_ticks_min;   // event persists for this many ticks
    uint32_t          duration_ticks_max;
    // condition modifiers: engine multiplies base_weight by these factors at selection time
    float             climate_stress_weight_scale;  // > 1.0 = more likely under climate stress
    float             instability_weight_scale;     // > 1.0 = more likely in unstable provinces
    float             infrastructure_weight_scale;  // < 1.0 = less likely in high-infra provinces
    bool              generates_evidence_token;     // if true, EvidenceToken fired at severity >= threshold
};

// Runtime tracking of an event currently in progress.
struct ActiveRandomEvent {
    uint32_t          id;
    std::string       template_key;
    uint32_t          province_id;
    uint32_t          started_tick;
    uint32_t          end_tick;             // started_tick + duration; 0 = resolved/expired
    float             severity;             // 0.0–1.0; set at event fire time
    bool              evidence_generated;   // true once EvidenceToken has been pushed
};
// WorldState holds: std::vector<ActiveRandomEvent> active_random_events;
// Events expire when current_tick >= end_tick; effects applied per-tick while active.
```

---

## 20. LOD 1 Monthly Update [V1]

LOD 1 nations run a simplified monthly update pass — not a subset of the 27-step tick. It executes once per simulated month (every `TICKS_PER_MONTH` ticks) for each LOD 1 nation.

### 20.1 — Decision Resolutions

**E1 — Trade offer frequency:** Monthly for all goods. All LOD 1 nations publish trade offers once per simulated month. Weekly-vs.-monthly differentiation by category is a noted expansion tuning option; V1 uses uniform monthly cadence.

**E2 — LOD 1 nation set:** Scenario-file-specified with per-starting-province defaults. The engine reads the LOD 1 set from the scenario file — it does not compute it automatically.

Sample `lod_1_defaults_by_start_province` block in the base game scenario file:

```json
"lod_1_defaults_by_start_province": {
    "valdoria_ruhr_analog":    ["corrath", "merevant", "solara", "kavandri", "zanthar"],
    "corrath_london_analog":   ["valdoria", "merevant", "kavandri", "solara", "zanthar"],
    "kavandri_delhi_analog":   ["zanthar", "korelu", "solara", "valdoria", "merevant"]
}
```

### 20.2 — LOD 1 Monthly Update Procedure

```
LOD 1 Monthly Update (runs every TICKS_PER_MONTH ticks):

For each LOD 1 Nation:

    Step 1: Update aggregate production
        For each good produced by this nation:
            production_estimate[good_id] = base_production[good_id]
                × nation.tech_tier_modifier
                × (1.0 - climate_stress_production_penalty)
                × nation.trade_openness

    Step 2: Update aggregate consumption
        For each good consumed by this nation:
            consumption_estimate[good_id] = base_consumption[good_id]
                × nation.population_modifier
                × era_consumption_baseline_modifier[current_era]

    Step 3: Compute surplus and deficit
        For each good:
            surplus[good_id] = production_estimate[good_id] - consumption_estimate[good_id]
            // positive = available for export; negative = demand for import

    Step 4: Generate NationalTradeOffer
        For each good where surplus[good_id] > 0:
            exports.push({
                good_id: good_id,
                quantity_available: surplus[good_id] × trade_openness,
                offer_price: base_production_cost[good_id] × (1.0 + nation.export_margin)
            })
        For each good where surplus[good_id] < 0:
            imports.push({
                good_id: good_id,
                quantity_available: abs(surplus[good_id]),
                offer_price: base_production_cost[good_id] × (1.0 + nation.import_premium)
            })
        world_state.lod1_trade_offers[nation_id] = new_offer

    Step 5: Apply climate stress delta
        province_climate_stress_mean = mean of all province.climate.climate_stress_current
        nation.climate_stress_aggregate += GlobalCO2Index contribution this month
        for each province in nation:
            province.climate.climate_stress_current += climate_delta × province_vulnerability

    Step 6: Technology tier advancement check
        if nation.research_investment > tier_advance_cost[nation.current_tier]:
            nation.current_tier += 1
            update base_production_cost for affected goods
        // LOD 1 nations do not run the R&D system in detail; they advance on investment level alone

    Step 7: Political stability tracking
        if abs(nation.stability_delta_this_month) > config.lod1_stability_event_threshold:
            schedule simplified political event (no full political cycle)

    Step 8: Write national statistics for LOD 2 price index contribution
        world_state.lod1_national_stats[nation_id] = {
            production_by_good: production_estimate,
            consumption_by_good: consumption_estimate
        }
```

### 20.3 — LOD 1 Nation Archetype Profiles

Each LOD 1 nation has an archetype, set in the nation data file and overridable by the scenario file. Archetypes provide behavioral variation without running individual NPC simulation:

| Archetype | `export_margin` | `import_premium` | `trade_openness` | Character |
|---|---|---|---|---|
| `aggressive_exporter` | 0.05 | — | 0.8 | Competes on price; high export volume |
| `protectionist` | — | 0.4 | 0.3 | High tariffs; low import acceptance |
| `resource_dependent` | low (raw resources) | high (manufactured goods) | moderate | Commodity-heavy export mix |
| `industrial_hub` | moderate | moderate | moderate | Tier 2–3 goods production focus |

'—' cells indicate the field is 0.0 for that archetype. The LOD 1 monthly update procedure guards export offer generation with `if surplus[good_id] > 0` and import offer generation with `if surplus[good_id] < 0`. A nation with `export_margin = 0.0` produces export offers at base production cost with no markup — it will still export if it has surplus, but at minimal margin. A nation with `trade_openness = 0.3` (protectionist) scales its surplus down before offer generation, producing low offer quantities regardless of actual surplus.

---

## 21. LOD 2 Annual Batch [V1]

The LOD 2 annual batch runs once per simulated year (every `TICKS_PER_YEAR` ticks) for all LOD 2 nations. Its sole output is an updated `GlobalCommodityPriceIndex`. LOD 2 nations do not run NPC simulation, market clearing, political cycles, scene cards, or consequence generation.

### 21.1 — LOD 2 Annual Batch Procedure

```
LOD 2 Annual Batch (runs every TICKS_PER_YEAR ticks):

    Initialize aggregate accumulators:
        lod2_aggregate_production[good_id] = 0 for all goods
        lod2_aggregate_consumption[good_id] = 0 for all goods

    For each LOD 2 Nation:

        Step 1: Update production estimate
            For each good produced by this nation:
                production = base_production[good_id]
                    × nation.tech_tier_modifier
                    × (1.0 - climate_stress_production_penalty)
                lod2_aggregate_production[good_id] += production

        Step 2: Update consumption estimate
            For each good consumed by this nation:
                consumption = base_consumption[good_id]
                    × nation.population_modifier
                    × era_consumption_baseline_modifier[current_era]
                lod2_aggregate_consumption[good_id] += consumption

        Step 3: Apply climate stress delta
            // LOD 2 nations receive simplified climate stress from GlobalCO2Index
            nation.climate_stress += global_co2_index × nation.climate_vulnerability

    // After all LOD 2 nations processed:

    Step 4: Compute and normalize GlobalCommodityPriceIndex
        For each good:
            ratio = lod2_aggregate_consumption[good_id]
                    / max(lod2_aggregate_production[good_id], SUPPLY_FLOOR)
            // ratio > 1.0: LOD 2 world net consuming (scarcity) → price up
            // ratio < 1.0: LOD 2 world net producing (surplus) → price down

            raw_modifier = clamp(ratio, LOD2_MIN_MODIFIER, LOD2_MAX_MODIFIER)
            smoothed_modifier = lerp(
                world_state.lod2_price_index.lod2_price_modifier[good_id],
                raw_modifier,
                LOD2_SMOOTHING_RATE
            )
            world_state.lod2_price_index.lod2_price_modifier[good_id] = smoothed_modifier

    Step 5: Era transition contribution check
        // LOD 2 batch contributes to era transition conditions
        // If global aggregate economic conditions meet era_N+1 trigger thresholds:
        //     queue era transition event (processed in main tick)

    world_state.lod2_price_index.last_updated_tick = current_tick
```

### 21.2 — Named Constants

All three constants in `economy.json`:

| Constant | Proposed default | Effect |
|---|---|---|
| `lod2_min_modifier` | 0.50 | LOD 2 surplus can reduce global prices by at most 50% |
| `lod2_max_modifier` | 2.00 | LOD 2 scarcity can raise global prices by at most 100% |
| `lod2_smoothing_rate` | 0.30 | 30% weight to new annual reading; prevents sharp year-on-year price jumps |

### 21.3 — What LOD 2 Does NOT Do

- Does not run individual NPC simulation
- Does not run detailed market clearing
- Does not run political cycles
- Does not generate scene cards or consequence queue entries
- Does not promote nations to LOD 0 (LOD 1 is the promotion layer)

### 21.4 — Data Sourcing

LOD 2 base production/consumption data is seeded from World Bank national accounts (year 2000 baselines) via the GIS pipeline and stored per nation in `world.json`. The annual batch applies modifiers to these baselines — it does not recompute production from first principles each year.

---

## 22. Persistence Architecture [CORE]

### 22.1 — Game Mode

```cpp
enum class GameMode : uint8_t {
    ironman  = 0,  // Timeline restoration locked. Achievement-eligible.
                   // Snapshot browser exists but is read-only (review only).
    standard = 1,  // Timeline restoration available with TimelineDisruptionConsequence.
                   // restoration_count tracked in PlayerCharacter.restoration_history.
};
```

Set at new game creation. Stored in `WorldState.game_mode`. Cannot be changed mid-run. The word "reload" never appears in the UI — restoration in Standard mode is presented as a diegetic disruption event, not a game mechanic.

**Ironman game creation warning (two-button modal — no default selection):**
> *"In Ironman mode, the Timeline is locked. You can review your history but cannot return to it. Ironman runs are achievement-eligible."*
> *"This choice is permanent for this run."*
> `[Begin — Ironman]`  `[Play Standard Mode Instead]`

### 22.2 — Autosave Architecture

The world state is written to disk continuously:

- **Write-ahead log (WAL):** Every delta to world state is appended to the current WAL segment before being applied. If the game crashes mid-tick, the WAL allows reconstruction from the most recent snapshot.
- **Periodic snapshots:** Full world state snapshot every in-game month (every 30 ticks). Protobuf binary, LZ4-compressed. Snapshots allow fast load without replaying the full WAL.
- **WAL segments:** WAL is not a single file but a series of segments. Each segment covers the 30 ticks between two snapshots. On timeline restoration, the current segment is marked `SUPERSEDED`, a new segment opens from the restored snapshot, and the superseded segment is retained on disk for debugging but never replayed. WAL replay uses only the most recent non-superseded segment.
- **Background snapshot writes:** Snapshots run on a background thread with copy-on-write WorldState. The tick does not block on snapshot I/O (see Risk 5, §24).

### 22.3 — SnapshotSummary

The Timeline browser loads summary data without reading full 10MB snapshots. Each snapshot writes a companion `.summary` file alongside the full state:

```cpp
struct SnapshotSummary {
    uint32_t snapshot_tick;
    uint16_t in_game_year;            // 2000–2040+; derived from tick at write time
    uint8_t  in_game_month;           // 1–12
    uint8_t  in_game_day;             // 1–30
    float    net_worth;               // player net worth at snapshot time
    float    player_exposure_level;   // max InvestigatorMeter.current_level
                                      // across all LE NPCs where
                                      // target_id == player.id.
                                      // 0.0 if player is not currently a target.
                                      // UI: player's personal legal risk indicator.
    float    regional_le_activity;    // max InvestigatorMeter.current_level
                                      // across all LE NPCs in all provinces,
                                      // regardless of target.
                                      // Reflects how active law enforcement is
                                      // in the environment this month.
                                      // UI: ambient heat indicator.
    float    cash_on_hand;
    uint8_t  consequence_queue_count; // active deferred consequence items
    uint8_t  notable_events_count;    // high-impact consequences fired this month
    std::array<std::string, 5> event_labels;  // up to 5: "Investigation opened", etc.
    uint32_t schema_version;          // for migration validation
};
```

~204 bytes per summary (one additional float vs. original ~200 byte estimate). A 25-year playthrough = 300 months = ~61KB to load the full timeline list. Instant menu open.

### 22.4 — PersistenceModule API

```cpp
enum class RestoreResult : uint8_t {
    success,
    locked_ironman_mode,
    snapshot_not_found,
    migration_failed,
    schema_too_new,          // loaded_version > CURRENT_SCHEMA_VERSION; refuse load
    already_restoring,       // re-entrant call guard
};

class PersistenceModule {
public:
    // Available to all callers (both modes):
    std::vector<SnapshotSummary> list_snapshots() const;

    // Standard mode only. Validates mode before executing.
    // Returns locked_ironman_mode if game_mode == ironman.
    // Increments restoration_count, applies disruption consequences, loads snapshot.
    RestoreResult restore_with_consequences(uint32_t snapshot_tick, WorldState& world_state);

    // Called on every load — applies any needed schema migrations before first tick.
    RestoreResult load_snapshot(uint32_t snapshot_tick, WorldState& world_state);

private:
    // Internal only. CI-enforced: any call to restore_raw() from outside
    // PersistenceModule fails the build.
    RestoreResult restore_raw(uint32_t snapshot_tick, WorldState& world_state);

    // Translates a TimelineDisruptionEvent into concrete DeferredWorkItems and
    // MemoryEntries, injected into world_state before the first tick after restoration.
    void inject_disruption_consequences(WorldState& world_state,
                                        const TimelineDisruptionEvent& event);
};
```

`restore_with_consequences()` uses `DeterministicRNG(world_state.world_seed, current_tick, 0xDEADFACE)` for all random draws within disruption event generation. Same run + same restoration point = same disruption pattern. Determinism is maintained.

### 22.5 — Schema Versioning and Migration

```cpp
constexpr uint32_t CURRENT_SCHEMA_VERSION = 1;  // bump on every migration-requiring change

using MigrationFn = std::function<void(WorldState&)>;

class MigrationRegistry {
public:
    // Called during load_all() for each expansion with schema_version_bump > 0.
    void register_migration(uint32_t from_version, MigrationFn fn);

    // Called by PersistenceModule::load_snapshot() when loaded_version < CURRENT_SCHEMA_VERSION.
    // Applies migrations in version order.
    void migrate(WorldState& ws, uint32_t loaded_version) const;

private:
    std::map<uint32_t, MigrationFn> migrations_;  // from_version → fn
};
```

**Migration contract:** Migrations are additive only. Add new fields with defaults; never delete data. Example first-expansion migration:

```cpp
// registered as migrate v1 → v2 by trafficking_expansion package
void migrate_trafficking_init(WorldState& ws) {
    ws.trafficking_network = TraffickingNetwork{};    // empty
    ws.player.trafficking_exposure = 0.0f;
}
```

**Schema downgrade guard:** If `loaded_schema_version > CURRENT_SCHEMA_VERSION`, `load_snapshot()` returns `schema_too_new`. The UI shows: *"This save requires a newer version of the game."* Never attempted to load forward-incompatible saves.

### World State Size Estimate

| Component | Estimated size |
|---|---|
| 3,000 significant NPCs × 500 memory entries | ~6MB |
| Regional market state (50 goods × 20 regions) | ~100KB |
| DeferredWorkQueue (all deferred items) | ~500KB |
| NPC relationship graph (sparse) | ~2MB |
| Facility and business state | ~1MB |
| Route table (180 RouteProfiles) | ~50KB |
| **Total estimated active state** | **~10MB** |

Snapshot compression (LZ4) reduces disk footprint. Background thread writes prevent tick hitching.

---

## 22a. Timeline Restoration System [CORE]

### 22a.1 — TimelineDisruptionEvent

```cpp
struct TimelineDisruptionEvent {
    uint8_t  tier;                           // 1, 2, or 3; determined by restoration_count BEFORE increment
    uint8_t  restoration_count_after;        // restoration_count after this restoration
    std::vector<uint32_t> affected_npc_ids;  // selected by highest relationship score
    float    memory_emotional_weight;        // computed by tier; see table below
    float    trust_delta_per_close_contact;  // 0.0 for Tier 1
    uint8_t  close_contact_exit_count;       // 0 for Tier 1 and 2; 1–2 for Tier 3
    float    exhaustion_spike;               // added to HealthState.exhaustion_accumulator
    uint32_t disorientation_expires_tick;    // when calendar_capacity_modifier expires
};
```

`inject_disruption_consequences()` translates `TimelineDisruptionEvent` into concrete items:
- One `MemoryEntry` per `affected_npc_id` (type: `observation`, context_label: `behavioral_inconsistency`)
- `relationship_changes` DeltaBuffer entries for trust damage (Tier 2+)
- `npc_leaves_network` DeferredWorkItem for exits (Tier 3+) — selected by lowest `resilience` score on the relationship, not lowest absolute trust
- `health_event` DeferredWorkItem for exhaustion spike
- `TimeBoundedModifier` written directly to `PlayerCharacter.calendar_capacity_modifiers`

### 22a.2 — Penalty Tiers

| Field | Tier 1 (first) | Tier 2 (second) | Tier 3 (third+) |
|---|---|---|---|
| `affected_npc_count` | 3–8 | 8–20 | 20–40 |
| `memory_emotional_weight` | −0.20 to −0.35 | −0.35 to −0.55 | −0.50 to −0.75 |
| `trust_delta_per_close_contact` | 0.0 | −0.15 to −0.25 | −0.30 to −0.50 |
| `close_contact_exit_count` | 0 | 0 | 1–2 |
| `exhaustion_spike` | +0.15 | +0.30 | +0.55 |
| `disorientation_expires_tick` | current + 30 | current + 60 | current + 90 |
| `calendar_capacity_delta` | −1 slot/day | −1 slot/day | −2 slots/day |

**`restoration_count` increment timing:** Increments atomically inside `restore_with_consequences()` at snapshot load. Warning modal displays current tier (based on count *before* increment). The player sees Tier 1 warning on first restoration; count becomes 1 after they confirm.

### 22a.3 — Tier 2 Queued Consequence

Tier 2 additionally queues one `DeferredWorkItem { type: consequence }`:
- `delay_ticks`: 7–21 (deterministic RNG)
- Source: NPC with highest trust score in player's contact network
- Effect: private conversation scene card expressing concern
- NPC-role-conditional outcomes: journalist may pitch erratic behavior story; criminal org may send a warning; politician contact raises concern in party; default: scene card only

### 22a.4 — Tier 3 Additional Consequences

Tier 3 additionally queues:
```
{ type: npc_leaves_network, count: 1–2, source: lowest-resilience affected NPCs }
{ type: health_event, due_tick: current + 1–7 }
{ type: health_event (calendar_block), due_tick: current + health_event_due + 14–30 }
{ type: consequence, count: 1–3 (scene cards from affected contacts) }
```

### 22a.5 — Warning Modals

**Tier 1 warning:**
> *"Returning to this point will cause a disorientation event. Your closest contacts may notice inconsistencies in your behaviour over the following weeks."*
> `[Return to Timeline]`  `[Stay]`

**Tier 2 warning:**
> *"A second disruption will have measurable effects on your relationships and mental health. Some contacts may begin to distance themselves."*
> `[Return to Timeline]`  `[Stay]`

**Tier 3+ warning:**
> *"Further disruption at this level will have severe and lasting consequences. This will not go unnoticed."*
> `[Return to Timeline]`  `[Stay]`

No default selection. Both buttons explicit. The player cannot accidentally confirm by pressing Enter.

### 22a.6 — Achievement Tracking

`PlayerCharacter.ironman_eligible` is `true` if `game_mode == ironman` and `restoration_count == 0`. Since restoration is impossible in ironman, this is effectively a constant set at game creation — it is retained as an explicit field for audit and legacy screen display. The heir succession screen and character legacy view display an "Ironman" marker if `ironman_eligible == true` at character death or endgame succession.

---

## 23. The Simulation Prototype — Specification [PROTOTYPE]

### Purpose

Answer the fundamental technical question before production scales. Secondary outputs: baseline performance numbers for all simulation systems, identification of required architectural changes, and a reference implementation that the full production team builds from.

### Team

4–6 engineers with backgrounds in:
- Agent-based simulation (required)
- High-performance C++ (required)
- Economic simulation or game simulation (preferred)
- Database / persistence systems (one person minimum)

### Duration

12–16 weeks.

### Prototype Scope

The prototype builds only the systems needed to answer the performance question:

**Include:**
- NPC data structure (memory, motivation, relationships) at target scale
- Behavior generation engine (full decision loop on all NPCs per tick)
- A simplified economy (5 goods, 2 regions, price model with supply/demand)
- Consequence queue
- 27-step tick sequence (stubbed steps for systems not yet implemented)
- Performance measurement tooling

**Exclude:**
- All UI (headless simulation)
- Scene cards
- Facility design
- Criminal economy detail
- Political system detail

### Success Criteria

| Metric | Target | Acceptable | Failing |
|---|---|---|---|
| Tick duration at 2,000 significant NPCs | < 200ms | < 500ms | > 1,000ms |
| Tick duration at 5,000 significant NPCs | < 500ms | < 1,500ms | > 3,000ms |
| Memory footprint at 2,000 NPCs | < 500MB | < 1GB | > 2GB |
| Autosave snapshot time | < 2s | < 5s | > 10s |
| Simulation determinism (same seed = same output) | Required | — | Any non-determinism |

If the prototype hits Acceptable rather than Target, the architecture is viable with optimization. If it hits Failing, the NPC model requires architectural simplification before proceeding.

### Outputs

1. **Performance benchmark report** with tick duration under load at multiple NPC count levels
2. **Architecture memo** documenting what was built, what was cut, and what the production architecture should be
3. **Reference implementation** to be handed to the full engineering team as the foundation

---

## 24. Known Technical Risks [CORE]

### Risk 1 — NPC Scale Performance
**Probability:** High. **Impact:** Critical.
Full individual agent simulation at 2,000+ NPCs per tick has not been shipped. The prototype is specifically designed to validate or disprove viability.
**Mitigation:** Lazy evaluation, LOD system, Named NPC tier as intermediate fidelity. If necessary: reduce full-model NPC count and expand simplified-model tier.

### Risk 2 — Supply Chain Circular Dependencies
**Probability:** Medium. **Impact:** Moderate.
Some goods feed into the production of their own prerequisites (energy to produce energy equipment). Single-pass propagation will produce wrong results.
**Mitigation:** Identify circular dependencies at world generation time and resolve with iterative convergence (max 3–5 iterations per tick for affected goods). Performance cost is bounded and predictable.

### Risk 3 — Consequence Queue Interaction Complexity
**Probability:** Medium. **Impact:** Moderate.
Multiple queued consequences interacting with each other in unexpected ways.
**Mitigation:** Consequence entries should be self-contained and independent where possible. Design rule: if an entry's validity depends on world state at execution time, it checks state when it fires, not when it's queued.

### Risk 4 — Procedural Dialogue Quality
**Probability:** High. **Impact:** Moderate.
Templated procedural dialogue will produce text that feels flat or repetitive at scale.
**Mitigation:** Invest in authored variation libraries for high-frequency dialogue contexts. Use LLM-assisted dialogue generation as a production tool for authoring variation sets, not at runtime.

### Risk 5 — Autosave Latency Spikes
**Probability:** Low. **Impact:** Low.
Snapshot serialization may cause frame hitches if run synchronously.
**Mitigation:** Snapshots run on a background thread with copy-on-write world state.

### Risk 6 — Simulation Determinism
**Probability:** Medium. **Impact:** High.
Non-determinism makes debugging emergent behavior nearly impossible.
**Mitigation:** Integer-based random seed system. All random draws go through a deterministic RNG keyed to the seed and tick number. All parallelism uses fork-join with deterministic merge. Determinism test suite runs in CI from prototype onward.

---

## 25. Development Milestones [CORE]

| Milestone | Timing | Deliverable |
|---|---|---|
| Simulation prototype complete | Month 3–4 | Performance benchmark, architecture memo, reference implementation |
| V1 vertical slice (core economy + 1 career path) | Month 12 | Playable build: one region, business path, basic NPC simulation |
| Closed alpha (3 paths, full economy) | Month 24 | Business + criminal + political paths, full consequence system, 3 regions |
| Beta | Month 36 | Full V1 feature set, one nation, content complete for V1 |
| V1 ship | Month 42–48 | Gold |

Milestones compress or extend based on prototype results. If the prototype fails, the Month 12 milestone date shifts right while the architecture is redesigned.

---

## 26. Healthcare System [V1]

Specified per GDD §7.9 (hospital facility type) and GDD §4 (healthcare access as health management tool). Healthcare is a regional service that affects NPC health, labour force participation, and community stability. It is not a detailed medical simulation — it is an economic and social output system.

### HealthcareProfile [V1]

```cpp
// Per-province snapshot of healthcare system quality.
// Set at world load from world.json; updated at runtime by facility investment.
struct HealthcareProfile {
    float access_level;          // 0.0–1.0; 0 = no formal healthcare; 1 = universal coverage
    float quality_level;         // 0.0–1.0; affects NPC health recovery rate and treatment success
    float cost_per_treatment;    // game currency per NPC treatment event
    float capacity_utilisation;  // 0.0–1.0; above overload_threshold, quality degrades
};
// Added to Province struct: HealthcareProfile healthcare;
```

### NPC Health Model [V1]

```cpp
// Added to NPC struct (§4):
float    health;               // 0.0–1.0; 0.0 = NPC death; 1.0 = full health
uint32_t last_treatment_tick;  // 0 = never treated
```

**Health tick update — tick step 8 (NPC motivation update):**

```
if npc.health < 1.0:
    recovery_rate = province.healthcare.access_level
                  × province.healthcare.quality_level
                  × config.healthcare.base_recovery_rate
    npc.health = min(1.0, npc.health + recovery_rate)

if npc.health < config.healthcare.critical_health_threshold:
    if province.healthcare.access_level > 0 AND npc.cash >= province.healthcare.cost_per_treatment:
        npc.cash   -= province.healthcare.cost_per_treatment
        npc.health += config.healthcare.treatment_health_boost × province.healthcare.quality_level
        npc.last_treatment_tick = current_tick
        province.healthcare.capacity_utilisation += config.healthcare.capacity_per_treatment

if province.healthcare.capacity_utilisation > config.healthcare.overload_threshold:
    province.healthcare.quality_level *= config.healthcare.overload_quality_penalty  // applied once per tick
```

**Labour force participation effect — tick step 17 (cohort aggregation):**

```
sick_leave_fraction = count(NPC where health < config.healthcare.labour_impairment_threshold)
                    / province.demographics.labour_force
effective_labour_supply = province.demographics.labour_force
                        × (1.0 − sick_leave_fraction × config.healthcare.labour_supply_impact)
```

### Healthcare Facility [V1]

`FacilityType::hospital` added to the `FacilityType` enum. On build completion:

```
province.healthcare.access_level  = min(1.0,
    province.healthcare.access_level + facility.capacity × config.healthcare.access_per_capacity_unit)
province.healthcare.quality_level = weighted_average(existing, facility.quality_rating, facility.capacity)
```

### Named Constants — Healthcare

All in `simulation_config.json → healthcare`:

| Constant | Default | Notes |
|---|---|---|
| `healthcare.base_recovery_rate` | 0.001 | Health recovered per tick at full access and quality |
| `healthcare.critical_health_threshold` | 0.30 | Below this, NPC seeks treatment if affordable |
| `healthcare.treatment_health_boost` | 0.25 | Base health recovery per treatment event |
| `healthcare.overload_threshold` | 0.85 | Above this capacity utilisation, quality degrades |
| `healthcare.overload_quality_penalty` | 0.999 | Multiplicative quality decay per tick while overloaded |
| `healthcare.labour_impairment_threshold` | 0.50 | Below this NPC health, counted as sick leave |
| `healthcare.labour_supply_impact` | 0.80 | Sick-leave multiplier on effective labour supply reduction |
| `healthcare.access_per_capacity_unit` | 0.05 | Access level added per hospital capacity unit built |
| `healthcare.capacity_per_treatment` | 0.001 | Capacity utilisation increment per NPC treatment event |

---

## 27. Addiction State Machine [V1]

Specified per GDD §12.11. Addiction is modeled per-NPC as a five-stage progression. At the regional level, aggregate addiction rates feed back into labour force participation, healthcare capacity, and community stability. Addiction is a consequence system — the drug supply chain creates addicts; the addiction system feeds back into social metrics.

### AddictionStage [V1]

```cpp
enum class AddictionStage : uint8_t {
    none       = 0,  // no dependency; NPC may still use recreationally without progressing
    casual     = 1,  // irregular use; no physiological dependency yet; tolerance begins
    regular    = 2,  // habitual use; tolerance builds; work efficiency slightly reduced
    dependent  = 3,  // physiological dependency; withdrawal if supply interrupted
    active     = 4,  // full addiction; NPC prioritises supply above other decisions
    recovery   = 5,  // cessation attempt; relapse probability active
    terminal   = 6,  // addiction-induced health failure (health below threshold for N ticks)
};
```

### AddictionState [V1]

```cpp
// Added to NPC struct (§4):
struct AddictionState {
    AddictionStage stage;
    std::string    substance_key;         // which drug; matches goods data key e.g. "cocaine"
    float          tolerance;             // 0.0–1.0; rises with use; drives dose escalation
    float          craving;              // 0.0–1.0; drives urgency; decays when clean
    uint32_t       consecutive_use_ticks;
    uint32_t       clean_ticks;           // ticks since last use; drives recovery progress
    float          relapse_probability;   // computed: craving × history_weight; checked each tick during recovery
};
```

### Stage Transition Rules [V1]

```
none → casual:
    trigger:  NPC consumes drug (any source) for first time
    effect:   tolerance += config.addiction.tolerance_per_use_casual

casual → regular:
    trigger:  consecutive_use_ticks >= config.addiction.regular_use_threshold_ticks
    effect:   craving accumulation begins; memory entry: recreational_drug_use

regular → dependent:
    trigger:  consecutive_use_ticks >= config.addiction.dependency_threshold_ticks
              AND tolerance >= config.addiction.dependency_tolerance_floor
    effect:   withdrawal: if supply_gap_ticks > 0, health -= config.addiction.withdrawal_health_hit/tick
              work efficiency × config.addiction.dependent_work_efficiency

dependent → active:
    trigger:  craving >= config.addiction.active_craving_threshold
              for config.addiction.active_duration_ticks consecutive ticks
    effect:   supply acquisition promoted to highest NPC decision priority
              memory entry: addiction_impairs_function
              province.conditions.addiction_rate += config.addiction.rate_delta_per_active_npc

active → recovery:
    trigger:  clean_ticks >= config.addiction.recovery_attempt_threshold_ticks
    (NPC achieves clean ticks via: incarceration, treatment facility, high-motivation roll)
    effect:   craving decays at config.addiction.craving_decay_rate_recovery per tick

recovery → none (success):
    trigger:  clean_ticks >= config.addiction.full_recovery_ticks
              AND relapse_probability < config.addiction.recovery_success_threshold

recovery → active (relapse):
    trigger:  random_float(0,1) < relapse_probability (checked each tick during recovery)

any stage → terminal:
    trigger:  npc.health < config.addiction.terminal_health_threshold
              for config.addiction.terminal_persistence_ticks consecutive ticks
    effect:   NPC health continues declining; NPCRole effectively inactive
```

### Regional Aggregate [V1]

```cpp
// Added to RegionConditions (§4):
float  addiction_rate;  // fraction of province adult population at stages dependent → terminal
                        // updated at tick step 17 (cohort aggregation)
                        // downstream effects:
                        //   labour force participation -= addiction_rate × config.addiction.labour_impact_per_addict
                        //   province.healthcare.capacity_utilisation += addiction_rate × config.addiction.healthcare_load_per_addict
                        //   CommunityState.grievance_level += addiction_rate × config.addiction.grievance_per_addict_fraction
```

### Craving Accumulation Per Stage [V1]

Craving increments are applied each tick while NPC is at each stage. The NPC's `craving` field is incremented based on the current `AddictionStage` and the frequency of substance access.

| Stage | craving_increment_per_use_tick | Transition Threshold | Notes |
|---|---|---|---|
| casual | 0.005 | craving > 0.3 → regular | Slow accumulation during experimental/recreational phase |
| regular | 0.012 | craving > 0.7 → dependent | Accelerating accumulation; habit solidifying |
| dependent | 0.025 | craving > 0.9 → active | Rapid spiral; physiological dependency deepens |
| active | 0.000 | (maintained via sustained use) | Craving remains high; managed by repeated supply access |
| recovery | −0.003 | craving < 0.05 → none (success) | Negative increment; craving decays per `config.addiction.craving_decay_rate_recovery` |

**Craving mechanics:** Each use event increments `craving += stage_increment`. Craving naturally decays at `config.addiction.craving_decay_rate_recovery` per tick during recovery. During all other stages, craving decays passively at a slower rate (not specified; assumed 0.0 to maintain high craving pressure) unless actively pursuing supply.

**Stage transitions triggered by craving thresholds:**
- `craving >= 0.3` AND `consecutive_use_ticks >= config.addiction.regular_use_threshold_ticks` → regular stage
- `craving >= 0.7` AND `consecutive_use_ticks >= config.addiction.dependency_threshold_ticks` AND `tolerance >= 0.30` → dependent stage
- `craving >= 0.9` (or `config.addiction.active_craving_threshold` = 0.70) for 60 consecutive ticks → active stage

### Named Constants — Addiction

All in `simulation_config.json → addiction`:

| Constant | Default | Notes |
|---|---|---|
| `addiction.tolerance_per_use_casual` | 0.05 | Tolerance increment per casual use event |
| `addiction.regular_use_threshold_ticks` | 30 | ~1 month continuous use → regular stage |
| `addiction.dependency_threshold_ticks` | 90 | ~3 months regular use → dependent |
| `addiction.dependency_tolerance_floor` | 0.30 | Minimum tolerance for dependency transition |
| `addiction.active_craving_threshold` | 0.70 | Craving level classifying active addiction |
| `addiction.active_duration_ticks` | 60 | Ticks sustained at high craving before active stage |
| `addiction.withdrawal_health_hit` | 0.005 | Health lost per tick without supply when dependent+ |
| `addiction.dependent_work_efficiency` | 0.70 | NPC work output multiplier at dependent stage |
| `addiction.recovery_attempt_threshold_ticks` | 14 | Minimum clean ticks to enter recovery stage |
| `addiction.craving_decay_rate_recovery` | 0.003 | Craving decrement per tick during recovery |
| `addiction.full_recovery_ticks` | 365 | Clean ticks required for successful recovery |
| `addiction.recovery_success_threshold` | 0.05 | Relapse probability below which recovery is complete |
| `addiction.terminal_health_threshold` | 0.15 | Health floor for terminal stage entry |
| `addiction.terminal_persistence_ticks` | 90 | Consecutive ticks below threshold before terminal |
| `addiction.rate_delta_per_active_npc` | 0.001 | Province addiction_rate increment per active addict |
| `addiction.labour_impact_per_addict` | 0.80 | Labour participation reduction per addict fraction |
| `addiction.healthcare_load_per_addict` | 0.50 | Healthcare capacity load per addict fraction |
| `addiction.grievance_per_addict_fraction` | 0.30 | Grievance increase per addict fraction |

---

## 28. Currency and Exchange Rates [V1]

Specified per GDD §2 (nations have independent currencies), GDD §17 (currency crises as random events), and GDD §12.1 (dollar-denominated entry conditions requiring a defined canonical unit).

### CurrencyRecord [V1]

```cpp
// One record per nation. Stored in WorldState.currencies (std::map<uint32_t, CurrencyRecord>).
struct CurrencyRecord {
    uint32_t    nation_id;
    std::string iso_code;            // e.g. "USD", "EUR", "RUB"; set from world.json or WorldGen
    float       usd_rate;            // units of this currency per 1 USD; updated each simulated week
    float       usd_rate_baseline;   // rate at world load; used for % deviation tracking
    float       volatility;          // daily σ for rate random walk; 0.01 = ~1% per tick
    float       foreign_reserves;    // 0.0–1.0; low reserves increase crisis probability
    bool        pegged;              // true = fixed to USD; peg breaks if reserves fall below threshold
    float       peg_rate;            // exchange rate when pegged; only valid if pegged = true
};
```

### Exchange Rate Update Model [V1]

Rates update once per simulated week (every `TICKS_PER_MONTH / 4` ticks):

```
if currency.pegged:
    if currency.foreign_reserves > config.currency.peg_break_reserve_threshold:
        usd_rate = peg_rate  // peg holds
    else:
        currency.pegged = false
        fire RandomEvent(type: economic, template_key: "currency_crisis", province_id: capital_province_id)
        // falls through to free-float update on next weekly tick

// Free-float update (macro-anchored random walk):
macro_factor = 1.0
    + (nation.trade_balance_fraction × config.currency.trade_balance_weight)
    − (nation.inflation_rate          × config.currency.inflation_weight)
    − ((1.0 − nation.credit_rating)   × config.currency.sovereign_risk_weight)

noise     = normal_distribution(mean=0.0, stddev=currency.volatility)
usd_rate  = clamp(usd_rate × (macro_factor + noise),
                  usd_rate_baseline × config.currency.floor_fraction,
                  usd_rate_baseline × config.currency.ceiling_fraction)
```

### Cross-Currency Price Conversion [V1]

All internal simulation prices are denominated in the canonical currency of the province's nation. Cross-province transactions convert at weekly usd_rate:

```
price_in_buyer_gcu = price_in_seller_gcu
    × (seller_nation_currency.usd_rate / buyer_nation_currency.usd_rate)
    × (1.0 + config.currency.fx_transaction_cost)
```

**GDD §12.1 dollar thresholds:** All dollar-denominated entry conditions (e.g., "revenue > $50,000/month") are denominated in the USD-equivalent at current exchange rates. The engine converts: `threshold_in_local = threshold_usd × nation_currency.usd_rate`.

### Named Constants — Currency

All in `simulation_config.json → currency`:

| Constant | Default | Notes |
|---|---|---|
| `currency.trade_balance_weight` | 0.30 | Trade surplus strength on exchange rate |
| `currency.inflation_weight` | 0.40 | Inflation differential effect on rate |
| `currency.sovereign_risk_weight` | 0.30 | Credit rating effect on rate |
| `currency.peg_break_reserve_threshold` | 0.15 | Reserve floor at which a peg breaks |
| `currency.floor_fraction` | 0.20 | Rate floor: cannot fall below 20% of baseline |
| `currency.ceiling_fraction` | 5.00 | Rate ceiling: cannot rise above 5× baseline |
| `currency.fx_transaction_cost` | 0.01 | 1% friction on all cross-currency transactions |

---

## 29. Media System and Movement-Based Influence [V1]

Resolves S-02 (media V1/EX boundary) and S-04 (movement-based influence V1 scope). All media and movement mechanics are V1 per updated Feature Tier List.

---

### 29.1 — Media System [V1]

Covers: independent media outlets, story propagation, social media dynamics, outlet ownership. Specified per GDD §16.

#### MediaOutlet [V1]

```cpp
enum class OutletType : uint8_t {
    newspaper       = 0,
    television      = 1,
    digital_outlet  = 2,   // online news sites, blogs with editorial structure
    social_platform = 3,   // social media networks; no single owner; emergent propagation
};

struct MediaOutlet {
    uint32_t    id;
    OutletType  type;
    uint32_t    province_id;           // where the outlet is based / most influential
    uint32_t    owner_npc_id;          // 0 = independent; player_id or NPC id if owned
    float       credibility;           // 0.0–1.0; affects story amplification multiplier
    float       reach;                 // 0.0–1.0; fraction of province population it touches
    float       editorial_independence;// 0.0–1.0; 1.0 = fully independent; reduced by owner influence
    uint32_t    primary_journalist_id; // lead NPC (role = journalist) attached to outlet
    std::vector<uint32_t> journalist_ids; // all NPC journalists at this outlet
};
```

#### Story [V1]

```cpp
enum class StoryTone : uint8_t {
    neutral    = 0,
    positive   = 1,   // boosts public reputation
    damaging   = 2,   // converts Exposure tokens into public crisis
    speculative= 3,   // low credibility; generates noise; may or may not resolve
};

struct Story {
    uint32_t    id;
    uint32_t    subject_id;           // player or NPC id the story is about
    uint32_t    journalist_id;        // NPC who filed the story
    uint32_t    outlet_id;            // outlet that published it
    StoryTone   tone;
    float       evidence_weight;      // 0.0–1.0; derived from evidence tokens attached
    float       amplification;        // current spread score; grows via propagation model
    uint32_t    published_tick;
    std::vector<uint32_t> evidence_token_ids;  // tokens this story is based on
};
```

#### Story Propagation Model [V1]

Stories propagate at **tick step 16** (Facility Signals and Scrutiny also at step 16; media is a separate sub-pass):

```
For each active Story published within config.media.propagation_window_ticks:

    // Outlet-to-NPC reach (direct readership)
    direct_readers = outlet.reach × province.demographics.adult_population

    // Cross-outlet amplification: other outlets pick up a high-evidence story
    for each other_outlet in province.media_outlets where other_outlet.id != story.outlet_id:
        pickup_probability = story.evidence_weight
            × other_outlet.credibility
            × config.media.cross_outlet_pickup_rate
        if random_float(0,1) < pickup_probability:
            story.amplification += other_outlet.reach × config.media.cross_outlet_amplification_factor

    // Social media amplification (if social_platform outlets exist in province)
    for each social_outlet in province.media_outlets where type == social_platform:
        social_amplification = story.amplification
            × social_outlet.reach
            × config.media.social_amplification_multiplier
            × (1.0 + story.evidence_weight)  // damaging evidence spreads faster
        story.amplification += social_amplification

    // Exposure conversion: damaging story converts evidence to public crisis
    if story.tone == damaging AND story.evidence_weight >= config.media.crisis_evidence_threshold:
        for each evidence_token_id in story.evidence_token_ids:
            subject.exposure += story.amplification × config.media.exposure_per_amplification_unit
```

**Outlet ownership editorial filter:** If `outlet.owner_npc_id == player_id` and story is damaging to player:

```
publish_decision = journalist.editorial_independence
    × (1.0 − config.media.owner_suppression_base_rate)
    // If journalist suppressed, memory entry: editorial_suppression_witnessed
    // If journalist then fires or leaves outlet, editorial_independence on new outlet += 0.30
    // (GDD §16: "journalists at the outlet still have their own motivation models")
```

#### Named Constants — Media

All in `simulation_config.json → media`:

| Constant | Default | Notes |
|---|---|---|
| `media.propagation_window_ticks` | 90 | Stories remain active for ~3 months |
| `media.cross_outlet_pickup_rate` | 0.15 | Base probability another outlet picks up a story per tick |
| `media.cross_outlet_amplification_factor` | 0.50 | Reach fraction added per pickup |
| `media.social_amplification_multiplier` | 2.50 | Social media amplifies 2.5× faster than traditional |
| `media.crisis_evidence_threshold` | 0.40 | Minimum story evidence_weight to convert exposure |
| `media.exposure_per_amplification_unit` | 0.02 | Exposure increment per unit of story amplification |
| `media.owner_suppression_base_rate` | 0.50 | Base probability owner suppresses damaging story |

---

### 29.2 — Movement-Based Influence [V1 — Thin System]

Resolves S-04. The thin V1 implementation: `movement_follower_count` (already on NPC and PlayerCharacter) and `is_movement_ally` (already on Relationship) are the data structures. This section specifies the update logic and downstream effects.

**V1 scope boundary:** V1 ships follower accumulation, community pressure mechanics, and policy influence. Strike mechanics and formal collective bargaining are Expansion.

#### Follower Accumulation [V1]

Updated at **tick step 22**:

```
For each movement leader (player and NPCs with role ∈ {community_leader, union_organizer, politician}):

    // Background population converts to followers based on community resonance
    resonance = province.community.grievance_level
              × leader.social_reputation
              × config.movement.resonance_scale

    new_followers = floor(
        province.demographics.adult_population
        × resonance
        × config.movement.follower_conversion_rate
    )
    follower_loss = floor(
        movement_leader.movement_follower_count
        × config.movement.follower_attrition_rate
    )
    movement_leader.movement_follower_count =
        clamp(movement_leader.movement_follower_count + new_followers − follower_loss,
              0, province.demographics.adult_population)
```

#### Movement Pressure Effects [V1]

Applied at **tick step 23** (consequence cascade):

```
pressure_index = movement_leader.movement_follower_count
               / province.demographics.adult_population  // 0.0–1.0

// Political pressure: modifies election probability if movement leader is active in politics
if province.political.has_active_election:
    province.political.movement_candidate_modifier +=
        pressure_index × config.movement.election_pressure_per_follower_fraction

// Employer pressure: if player is largest employer and union organizer has high followers
if movement_leader.role == union_organizer
   AND player.is_largest_employer_in(province_id):
    player.workforce_disruption_risk +=
        pressure_index × config.movement.union_disruption_risk_per_follower_fraction

// Community trust amplification: large movements shift institutional_trust
province.community.institutional_trust +=
    pressure_index × movement_leader.social_reputation
                   × config.movement.trust_shift_per_follower_fraction
```

#### Named Constants — Movement

All in `simulation_config.json → movement`:

| Constant | Default | Notes |
|---|---|---|
| `movement.resonance_scale` | 1.50 | Scales grievance × reputation to resonance score |
| `movement.follower_conversion_rate` | 0.002 | Fraction of adult population converting per tick at resonance=1 |
| `movement.follower_attrition_rate` | 0.005 | Fraction of followers lost per tick without active engagement |
| `movement.election_pressure_per_follower_fraction` | 0.20 | Vote share modifier per follower fraction during elections |
| `movement.union_disruption_risk_per_follower_fraction` | 0.30 | Workforce disruption risk per follower fraction |
| `movement.trust_shift_per_follower_fraction` | 0.10 | Institutional trust change per follower fraction |

---

## 30. Labor Market and Hiring System [V1]

The hiring system connects `PopulationCohort` skill supply (§14) to facility labor demand. It applies identically to the player and NPC businesses — no separate hiring path exists for the player. Hiring is a real action with scheduling cost, channel friction, and information asymmetry between employer and applicant.

### 30.1 — Enums and Core Structs

```cpp
enum class HiringChannel : uint8_t {
    public_board        = 0,  // Maximum applicant pool; zero employer control over who applies.
                               // Generates the broadest skill distribution. Employer reputation
                               // visible to all applicants — bad reputation suppresses quality.
    professional_network = 1, // Narrower pool (professional + corporate cohorts only).
                               // Higher average skill level. Requires employer.relationship_network
                               // to contain at least one NPC in professional or corporate group.
                               // Unavailable to criminal businesses (criminal_sector == true).
    personal_referral   = 2,  // Smallest pool: drawn from the NPC social graph of
                               // the hiring actor's existing relationships (trust > 0.4).
                               // Highest average trust, lowest average skill variance.
                               // No reputation visibility to applicants — applicant trusts referrer.
};

struct JobPosting {
    uint32_t id;
    uint32_t owner_id;                  // player_id or npc_id posting the job
    uint32_t business_id;               // NPCBusiness this role is for
    uint32_t province_id;               // province where role is located; constrains applicant pool
    SkillDomain required_domain;        // primary skill domain sought
    float    min_skill_level;           // 0.0–1.0; applicants below this are filtered
    float    offered_wage;              // per-tick; compared against regional_wage_by_skill
    HiringChannel channel;
    uint32_t posted_tick;
    uint32_t expires_tick;              // posting auto-closes at this tick if unfilled
    std::vector<uint32_t> applicant_ids; // NPC ids generated at post time (see §30.2)
    bool filled;
};

struct WorkerApplication {
    uint32_t applicant_npc_id;
    float    skill_level;               // NPC's actual level in required_domain; not shown to employer
                                        // until interview scene card fires
    float    salary_expectation;        // per-tick; NPC will not accept offers below this
                                        // computed: regional_wage × (1.0 + npc.motivation.money × 0.3)
    float    loyalty_prior;             // 0.0–1.0; estimated trust employer has with this applicant
                                        // before interview. personal_referral: referrer.trust × 0.8.
                                        // public_board/professional_network: 0.0 (unknown).
    bool     background_visible;        // true after interview scene card completes
};
```

### 30.2 — Regional Wage Market [V1]

The regional wage market is a per-province, per-skill-domain equilibrium price for labor. It is the reference rate workers use to set `salary_expectation` and employers use to assess whether their offered wage is competitive.

```cpp
// FIELD ADDITION to Province:
std::map<SkillDomain, float> regional_wage_by_skill;
    // SkillDomain → market wage rate (per-tick currency units)
    // Initialized at world gen from province.demographics.median_income
    //   with skill-domain multipliers (see table below).
    // Updated monthly: adjusts toward equilibrium based on
    //   labor supply/demand imbalance per domain.
```

**Initialization multipliers (applied to province median income per-tick equivalent):**

| SkillDomain | Multiplier |
|---|---|
| Finance, Engineering | 1.40 |
| Politics, Management | 1.25 |
| Persuasion, Intelligence | 1.15 |
| Trade, SpecialtyChemistry, SpecialtyCoding | 1.05 |
| Business, SpecialtyAgriculture, SpecialtyConstruction | 1.00 |
| CriminalOperations | 0.85 (informal market; not tax-reported) |
| SpecialtyCulinary, UndercoverInfiltration | 0.90 |

**Monthly update formula:**

```
FOR each skill_domain in province.regional_wage_by_skill:
    supply  = sum(cohort.skill_supply[domain] × cohort.size × cohort.employment_rate
                  for cohort in province.cohort_stats.cohorts)
    demand  = sum(facility.labor_requirement × facility_domain_weight(domain)
                  for facility in facilities where province_id == province.id)
    // demand: labor_requirement from FacilityRecipe; domain_weight is 1.0 for
    // the recipe's primary SkillDomain, 0.0 for others (simplified V1)

    ratio = demand / max(supply, 1.0)  // demand/supply; > 1.0 = labor shortage → wage rises
    wage_delta = regional_wage_by_skill[domain]
                 × config.labor.wage_adjustment_rate
                 × (ratio - 1.0)
    regional_wage_by_skill[domain] = clamp(
        regional_wage_by_skill[domain] + wage_delta,
        config.labor.wage_floor,
        config.labor.wage_ceiling_multiplier × province.demographics.median_income
    )
```

**Named constants (`simulation_config.json → labor`):**

| Constant | Default | Effect |
|---|---|---|
| `wage_adjustment_rate` | 0.03 | Monthly convergence rate toward equilibrium |
| `wage_floor` | 0.01 | Minimum wage (in-game currency per tick) |
| `wage_ceiling_multiplier` | 5.0 | Wages cannot exceed 5× province median income |

### 30.3 — Applicant Pool Generation [V1]

When a `JobPosting` is created, an applicant pool is generated immediately and attached to `job.applicant_ids`. Pool generation runs once at posting time; it does not regenerate each tick.

```
FUNCTION generate_applicant_pool(job: JobPosting, world_state: WorldState)
    → std::vector<WorkerApplication>:

    province = world_state.provinces[job.province_id]
    pool_size_target = base_pool_size(job.channel)
        // public_board:         config.labor.pool_size_public
        // professional_network: config.labor.pool_size_professional
        // personal_referral:    config.labor.pool_size_referral

    // Employer reputation penalty: if employer_reputation(job.owner_id) < 0.3,
    // reduce pool_size_target by (0.3 - reputation) × config.labor.reputation_pool_penalty_scale
    rep = employer_reputation(job.owner_id, province.id)  // see §30.4
    IF rep < config.labor.reputation_threshold:
        pool_size_target = max(1,
            pool_size_target - int((config.labor.reputation_threshold - rep)
                                   × config.labor.reputation_pool_penalty_scale))

    // Channel-specific NPC sourcing:
    candidates = []
    IF job.channel == personal_referral:
        // Draw from hiring actor's NPC relationship graph: trust > 0.4, same province
        candidates = [npc for npc in actor_relationships(job.owner_id)
                      if npc.province_id == job.province_id
                      AND relationship_trust(job.owner_id, npc.id) > 0.4
                      AND npc.id not in employed_npcs(job.business_id)]
    ELSE IF job.channel == professional_network:
        // Named NPCs from professional/corporate cohorts in province
        candidates = [npc for npc in world_state.significant_npcs
                      if npc.province_id == job.province_id
                      AND npc.demographic_group in {professional, corporate, small_business}
                      AND npc.skill(job.required_domain) >= job.min_skill_level × 0.7]
    ELSE:  // public_board
        // Mix of named NPCs (up to 20%) and background NPC stubs (remainder)
        named = [npc for npc in world_state.significant_npcs
                 if npc.province_id == job.province_id
                 AND npc.skill(job.required_domain) >= job.min_skill_level × 0.5]
        background_count = max(0, pool_size_target - min(len(named), pool_size_target / 5))
        candidates = named[:pool_size_target / 5] + generate_background_stubs(
            count = background_count,
            province = province,
            domain = job.required_domain,
            seed = world_state.world_seed ^ job.id)

    // Build WorkerApplication for each candidate (up to pool_size_target):
    applications = []
    FOR npc in candidates[:pool_size_target]:
        app = WorkerApplication{
            applicant_npc_id:  npc.id,
            skill_level:       npc.skill(job.required_domain),  // hidden until interview
            salary_expectation: regional_wage_by_skill[job.required_domain]
                                × (1.0 + npc.motivation.weights[money] × 0.3),
            loyalty_prior:     IF job.channel == personal_referral:
                                   relationship_trust(job.owner_id, npc.id) × 0.8
                               ELSE: 0.0,
            background_visible: false
        }
        applications.append(app)

    RETURN applications
```

**Background NPC stubs:** For `public_board` hiring when named NPC supply is insufficient, background population cohort data generates lightweight applicant stubs. Stubs have a `skill_level` drawn from a normal distribution centered on `cohort.skill_supply[domain]` (std dev 0.15), and a `salary_expectation` at exactly `regional_wage_by_skill[domain]`. Stubs have no NPC id for the memory log — they are hired as anonymous background workers who can still become significant NPCs if promoted (see `significant_npc_promotion` in GDD §3).

**Named constants (`simulation_config.json → labor`):**

| Constant | Default | Effect |
|---|---|---|
| `pool_size_public` | 12 | Applicant pool size for public board |
| `pool_size_professional` | 5 | Applicant pool size for professional network |
| `pool_size_referral` | 3 | Applicant pool size for personal referral |
| `reputation_threshold` | 0.3 | Below this, pool size is penalized |
| `reputation_pool_penalty_scale` | 8.0 | Pool reduction per reputation point below threshold |

### 30.4 — Employer Reputation [V1]

Employer reputation is a per-province float (0.0–1.0) computed for any actor that has employed workers in that province. It is not a stored field — it is derived from the collective memory logs of current and former employees.

```
FUNCTION employer_reputation(owner_id: uint32_t, province_id: uint32_t) → float:
    relevant_npcs = [npc for npc in world_state.significant_npcs
                     if npc.employer_history contains owner_id
                     AND npc.province_id == province_id]

    IF len(relevant_npcs) == 0:
        RETURN config.labor.reputation_default  // 0.5 — unknown employer

    scores = []
    FOR npc in relevant_npcs:
        npc_score = worker_satisfaction(npc, current_tick)  // §4.5
        scores.append(npc_score)

    reputation = mean(scores)
    // High reputation → more applicants, higher quality, lower salary expectations
    // Low reputation  → fewer applicants, lower average quality, salary premium demanded
    RETURN reputation
```

**Salary premium for low-reputation employers:** Applicants aware of the employer's reputation demand a wage premium. Applied inside `generate_applicant_pool` when `reputation < config.labor.reputation_threshold`:

```
salary_expectation ×= 1.0 + (config.labor.reputation_threshold - reputation)
                             × config.labor.salary_premium_per_reputation_point
```

**Named constant:** `salary_premium_per_reputation_point = 0.5` (`simulation_config.json → labor`) — 10 reputation points below threshold → 5% salary premium.

### 30.5 — Interview and Hire [V1]

**Interview mechanic:** An interview is a scene card interaction (§9 Scene Card System). The employer (player or NPC) selects an applicant from `job.applicant_ids`. A scene card fires with `scene_type: job_interview`. On completion:

- `background_visible` is set to `true` on the `WorkerApplication`
- `skill_level` is revealed to the employer
- The applicant NPC gains a memory entry: `employment_positive` (employer interest) or `employment_negative` (rejected)

**Hire action:** Employer selects applicant with `background_visible == true`. Hire succeeds if `offered_wage >= applicant.salary_expectation`. On hire:

- NPC's `employer_id` set to `job.business_id`; `employer_history` gains entry
- NPC gains `employment_positive` memory: emotional_weight = `(offered_wage / salary_expectation - 1.0) × 0.5`, clamped `+0.1 to +0.5`. Higher overpay → higher initial satisfaction.
- `JobPosting.filled = true`; remaining applicants receive a rejection memory entry (`employment_negative`, weight −0.1)

**NPC hiring (simplified):** NPC businesses do not run full interview scene cards. They hire by selecting the applicant with highest `skill_level / salary_expectation` ratio from the generated pool, subject to `skill_level >= job.min_skill_level`. This runs as a DeferredWorkQueue batch (WorkType::npc_hire_decision), not per tick.

### 30.6 — Firing and Workforce Departure [V1]

**Firing:** Player command or NPC strategic decision. On fire:
- NPC's `employer_id` cleared; `employer_history` gains exit entry with reason
- NPC gains `employment_negative` memory: weight scaled by firing reason (performance −0.2, layoff −0.4, witnessed crime then fired −0.7)
- NPC retains all prior memory entries including `witnessed_illegal_activity` — firing does not clear knowledge
- Consequence queue entry scheduled for `current_tick + delay` where `delay` is drawn from an exponential distribution with mean = `npc.risk_tolerance × 180 ticks`

**Voluntary departure:** If `worker_satisfaction(npc) < config.labor.voluntary_departure_threshold`, the NPC evaluates departure each month. Departure probability:

```
departure_probability = config.labor.departure_base_rate
    × (1.0 - worker_satisfaction(npc, current_tick))
    × npc.motivation.weights[career]  // career-motivated NPCs leave bad jobs faster
```

**Named constants (`simulation_config.json → labor`):**

| Constant | Default | Effect |
|---|---|---|
| `voluntary_departure_threshold` | 0.35 | Below this satisfaction, departure is possible |
| `departure_base_rate` | 0.08 | Base monthly probability at satisfaction = 0.0 |

### 30.7 — Tick Integration [V1]

Labor market updates run on a monthly cadence via `DeferredWorkQueue`:

| WorkType | Cadence | Action |
|---|---|---|
| `wage_market_update` | Monthly | Recompute `regional_wage_by_skill` for each province (§30.2) |
| `cohort_monthly_update` | Monthly | Update `PopulationCohort` employment_rate, median_income (§14 / S-3) |
| `npc_hire_decision` | On posting | NPC business selects from applicant pool; no tick-step cost |
| `voluntary_departure_check` | Monthly | Per-worker departure probability roll for all NPC employees |

Player-initiated hiring (post job, interview, hire) is player-action driven and does not consume a tick step — it occupies calendar time (scene cards have scheduling friction per §8).

---

## Change Log — Pass 2

### Sections Added

| Section | Content | Source |
|---|---|---|
| 10 | WorldState and DeltaBuffer | Pass 2A — Resolves B1 |
| 11 | PlayerCharacter | Pass 2A — Resolves B2 |
| 12 | Region and Nation | Pass 2A — Resolves B3 |
| 13 | Influence Network | Pass 2A — Resolves B4 |
| 14 | Community Response System | Pass 2B — Resolves B5 |
| 15 | Political Cycle System | Pass 2B — Resolves B6, B17 |
| 16 | Facility Signals and Scrutiny System | Pass 2B — Resolves B7, P7 |
| 17 | Criminal Organization | Pass 2B — Resolves B8 |

### Enums Completed (replacing "etc." stubs)

| Enum | Location | Source |
|---|---|---|
| NPCRole | Section 4 | Pass 2B ENUM 1 — Resolves B9 |
| ConsequenceType | Section 6 | Pass 2B ENUM 2 — Resolves B10 |
| FavorType | Section 7 | Pass 2B ENUM 3 — Resolves B11 |
| SceneSetting | Section 9 | Pass 2B ENUM 4 — Resolves B12 |
| BusinessSector | Section 5 | Pass 2B ENUM 5 — Resolves P10 |

### Sections Renumbered (Pass 2)

| Former section | New section |
|---|---|
| 10 (Persistence Architecture) | 18 |
| 11 (Simulation Prototype) | 19 |
| 12 (Known Technical Risks) | 20 |
| 13 (Development Milestones) | 21 |

### Additional Precision Additions

- `DeadlineConsequence` defined inline in Section 8 (partially resolves P12)
- `MAX_MEMORY_ENTRIES = 500` (exact constant, resolves P1)
- V1 planning NPC count: 750 per region (resolves P2)
- `recovery_ceiling` on Relationship struct with exact formula (resolves P6)
- `InfluenceNetworkHealth` struct specified (resolves P7 partially via Section 13)
- `WorldGenParameters` with named constants (resolves P8)
- Skill decay model with exact rates (resolves P9)
- `is_movement_ally` flag on Relationship struct
- Scope markers ([V1], [EX], [CORE]) added to all sections per S1 requirement

---

## Change Log — Pass 3, Session 2

### Sections Restructured

| Section | Change | Source |
|---|---|---|
| 12 | Renamed "Region and Nation" → "Province and Nation"; WorldGenParameters replaced with WorldLoadParameters; Region struct replaced with Province struct; new thin Region grouping struct added; KoppenZone, SimulationLOD, GeographyProfile, ClimateProfile enums/structs added; Nation struct updated with TariffSchedule field | Pass 3 Session 2 — Resolves A10, A11, A12, A13 |

### Sections Added (Pass 3 Session 2)

| Section | Content | Source |
|---|---|---|
| 18 | Trade Infrastructure — GoodOffer, NationalTradeOffer, GlobalCommodityPriceIndex, TradeAgreement, TariffSchedule, transport cost formula, informal economy price formula, Lod1NationStats | Pass 3 Session 2 — Resolves B1, B8 |

### Sections Renumbered (Pass 3 Session 2)

| Former section | New section | Reason |
|---|---|---|
| 18 (Persistence Architecture) | 22 | Section 18 assigned to Trade Infrastructure; Sections 19–21 reserved for Session 3 (Random Events, LOD 1 Monthly Update, LOD 2 Annual Batch) |
| 19 (Simulation Prototype) | 23 | Cascade from Persistence renumber |
| 20 (Known Technical Risks) | 24 | Cascade |
| 21 (Development Milestones) | 25 | Cascade |

### WorldState Fields Updated (Pass 3 Session 2)

- `std::vector<Region> regions` replaced with `std::vector<Province> provinces`
- Added `std::vector<Region> region_groups`
- Added `std::vector<TariffSchedule> tariff_schedules`
- Added `std::vector<NationalTradeOffer> lod1_trade_offers`
- Added `GlobalCommodityPriceIndex lod2_price_index`
- Added `std::map<uint32_t, Lod1NationStats> lod1_national_stats`

### Internal Cross-References Updated (Pass 3 Session 2)

- Section 3 Tick Budget: "see Section 19" → "see Section 23"

### Header Updated

- Companion document line updated to list all five companion documents
- Pass notation updated to reflect Pass 3 in progress

---

## Change Log — Pass 3, Session 3

### Blockers Resolved

| Blocker | Location | Resolution |
|---|---|---|
| B13 | Section 5 Price Model | Equilibrium price formula: full 3-step specification with clamp, spot-price adjustment, and LOD 2 modifier application. All constants reference `economy.json`. |
| B14 | Section 4 Behavior Generation | NPC expected_value function: full specification including OutcomeType enum, outcome_type_weight(), risk_discount(), relationship_modifier(), inaction_threshold(), and action selection pseudocode. Constants reference `simulation.json`. |
| B15 | Section 7 ObligationNode | Obligation escalation algorithm: per-tick demand growth formula, four status transitions (open → escalated → critical → hostile), counter-escalation mechanic. All 6 constants in `simulation.json`. |
| B16 | Section 4.5 (new) | Worker satisfaction model: recency-weighted memory aggregation formula, SATISFACTION_RELEVANT_TYPES table with 6 memory types, three-gate whistleblower eligibility condition. Config constant in `simulation.json`. |
| B18 | Section 19 (new) | Random event probability model: Poisson process with adjusted_rate formula, event type selection by conditioned weighted draw, effect model specifying all DeltaBuffer delta types for all 4 event categories, data-driven template system. |

### Sections Added (Pass 3 Session 3)

| Section | Content | Resolves |
|---|---|---|
| 4.5 | Worker Satisfaction — computed-from-memory function, SATISFACTION_RELEVANT_TYPES, whistleblower eligibility condition | B16 |
| 19 | Random Events — Poisson probability model, event type selection, effect model by category, template system | B18 |
| 20 | LOD 1 Monthly Update — E1/E2 decision resolutions, 8-step monthly procedure, 4 archetype profiles | E1, E2 |
| 21 | LOD 2 Annual Batch — accumulation loop, GlobalCommodityPriceIndex computation (ratio/clamp/lerp), named constants, explicit scope boundary | — |

### Sections Modified (Pass 3 Session 3)

| Section | Change |
|---|---|
| 4 — Behavior Generation | Placeholder decision loop replaced with full expected_value() specification including OutcomeType enum, ActionOutcome struct, all sub-functions, and action selection pseudocode |
| 5 — Price Model | Placeholder `f()` and [BLOCKER — B13 UNRESOLVED] replaced with full 3-step equilibrium price specification and variable definitions table |
| 7 — ObligationNode | [BLOCKER — B15 UNRESOLVED] replaced with full per-tick escalation algorithm, status transition logic, named constants table, and counter-escalation mechanic |

### Header Updated

- Title updated to "Pass 3 Session 3 Complete"
- Pass notation updated

---

---

## Change Log — Pass 3, Session 4

### Precision Gaps Resolved

| Gap | Location | Resolution |
|---|---|---|
| P11 (partial) / B2 | Section 5 — Economy | Technology Tier Efficiency Differential: `actual_output` and `actual_cost_per_unit` formulas with 2 named constants in `economy.json`, numeric example (Tier 3 / min_tier=2), quality ceiling interaction note, R&D doc cross-reference |
| B3 | Section 6 — Evidence Pool | Evidence Actionability Decay: per-tick loop, credibility branching, 3 named constants in `investigation.json`, 3-condition NPC credibility evaluation |
| B4 / P11 | Section 5 — Economy | Legitimate NPC Business Strategic Decision Matrix: all 4 profiles (cost_cutter, quality_player, fast_expander, defensive_incumbent) with full conditional logic and threshold values; R&D investment rates by profile table |
| B5 | Section 8 — Calendar | DeadlineConsequence: complete 6-field struct; 4-step missed-deadline procedure; NPC unilateral action types enumerated |
| B6 | Section 11 — PlayerCharacter | Organic Milestone Tracking: `MilestoneType` enum (19 values + EX note), `MilestoneRecord` struct, `milestone_log` and `achieved_milestones` added to `PlayerCharacter`, tick step 27 evaluation pseudocode, no-announcement design principle |

### Structs Modified

| Struct | Change |
|---|---|
| `ReputationState` | Added inline comment on `street` field linking to GDD §12.1 criminal entry conditions |
| `PlayerCharacter` | Added `milestone_log` and `achieved_milestones` fields |
| `DeadlineConsequence` | Replaced 2-field inline stub with complete 6-field struct and 4-step missed-deadline procedure |

### Header Updated

- Title updated to "Pass 3 Session 4 Complete"
- Version number updated to v5

---

## Change Log — Pass 3, Session 6 (Moddability Pass)

### Moddability Violations Resolved

| ID | Location | Resolution |
|---|---|---|
| C1 | Section 16 — Detection Risk Formula | `constexpr` OPSEC signal weights (`W_POWER_CONSUMPTION`, `W_CHEMICAL_WASTE`, `W_FOOT_TRAFFIC`, `W_OLFACTORY`) replaced with `FacilityTypeOPSECWeights` struct loaded per-facility-type from `facility_types.csv`. Moddability note added. |
| C2 | Section 14 — Community Response Escalation Stages | All 13 stage threshold values moved to `simulation_config.json → community.stage_thresholds`. Enum comments updated to reference config keys. `RESISTANCE_REVENUE_PENALTY` renamed `resistance_revenue_penalty` in config. Named constants table added. |
| C3 | Section 16 — InvestigatorMeter | Status thresholds (0.30/0.60/0.80) and `warrant_trust_min` moved to `simulation_config.json → investigator`. Enum and struct comments updated to reference config keys. Named constants table added. |
| C5a | Section 13 — Influence Network | `FEAR_DECAY_RATE` replaced with `config.npc.fear_decay_rate` reference (default: 0.002/tick). |
| C5b | Section 14 — Community Response | `COMMUNITY_EMA_ALPHA`, `SOCIAL_CAPITAL_MAX`, `MEMORY_DECAY_FLOOR`, `GRIEVANCE_NORMALIZER`, `GRIEVANCE_SHOCK_THRESHOLD`, `CORRUPTION_TRUST_PENALTY`, `TRUST_SUCCESS_BONUS`, `TRUST_FAILURE_PENALTY`, `CAPITAL_NORMALIZER`, `SOCIAL_NORMALIZER`, `GRIEVANCE_ADDRESS_RATE`, `DEFLECT_PAUSE_TICKS`, `OPPOSITION_GROWTH_RATE` all replaced with `config.community.*` references. Named constants table added. |
| C5c | Section 16 — OPSEC fill rate | `FACILITY_COUNT_NORMALIZER`, `DETECTION_TO_FILL_RATE_SCALE`, `FILL_RATE_MAX`, `PERSONNEL_VIOLENCE_MULTIPLIER` replaced with `config.opsec.*` references. Named constants table added. |

### Companion Document Updated

| Document | Change |
|---|---|
| R&D & Technology v1.1 | C6: `CO2_PERSISTENCE`, `FEEDBACK_SENSITIVITY`, `CLIMATE_SENSITIVITY_FACTOR`, `FOREST_SEQUESTRATION_RATE`, `CAPTURE_EFFICIENCY`, `INDUSTRIAL_CO2_COEFFICIENT`, `REGIONAL_SENSITIVITY_MULTIPLIER`, `FARM_STRESS_SENSITIVITY`, `DROUGHT_YIELD_PENALTY` all moved to `climate_config.json`. Formulas updated to `config.climate.*` references. Named constants table expanded from 5 to 9 entries. Moddability rationale added. |

### Header Updated

- Title updated to "Pass 3 Session 6 Complete — Moddability Pass"
- Version number updated to v6
- Companion document references updated to GDD v1.2, Commodities v2.1, R&D v1.1

---

## Change Log — Pass 3, Session 7

### Architecture Additions

| Component | Location | Description |
|---|---|---|
| Package System | §2a (new) | Unified mod/expansion architecture. `PackageType` as trust marker (not capability gate). `PackageManager`, `ITickModule`, `TickOrchestrator`, `ScriptEngine` (Lua 5.4), `MigrationRegistry`. Unified `package.json` manifest with `load_after`/`load_before`. Mod error boundary. Initialization sequence. |
| Province-Parallel Tick | §3 | Thread pool (min(cores−1, 6)). Province partitioning for 10 of 27 steps. Deterministic merge protocol (partition index order). Floating-point canonical summation rule. `CrossProvinceDeltaBuffer` for cross-province interactions (one-tick delay). |
| DeferredWorkQueue | §3.3 | Unified event-driven deferred work queue replacing separate consequence queue and transit pool. `WorkType` enum (10 values). Event-driven processing: relationship decay, evidence decay, business decisions, market recompute, investigator updates, climate batches, transit arrivals. Background work at idle time. |
| NPC Business Dispatch Spreading | §3 | `dispatch_day_offset` on `NPCBusiness` spreads monthly dispatch load evenly across 30 ticks. |
| Transit System | §18.4–18.12 | Information vs. physics asymmetry as design principle. `TransportMode` enum (5 modes). Transit time formula with terrain/infra delays. Transport cost formula updated with real `transit_ticks`. `RouteProfile`, `province_route_table` (precomputed at load). `TransitShipment` struct and `ShipmentStatus` enum. Interception model with per-tick checks spread across transit duration. Route concealment mechanic. Supply chain buffering implications. Infrastructure ownership intelligence value. |
| Persistence Rewrite | §22 | `GameMode` enum. Ironman two-button creation modal. WAL segment model (superseded segments on restoration). `SnapshotSummary` struct for fast timeline browser. `PersistenceModule` API with `RestoreResult` enum. `MigrationRegistry` with `MigrationFn`. Schema downgrade guard. |
| Timeline Restoration | §22a (new) | `TimelineDisruptionEvent` struct. Three penalty tiers (counts, trust delta, exit count, exhaustion, disorientation duration). `restoration_count` increment timing. Tier 2 queued consequence. Tier 3 additional consequences. Warning modal text (no default selection). `ironman_eligible` on `PlayerCharacter`. |

### Architecture Diagram Updated

§2 architecture diagram updated to show Package System and Persistence layers explicitly.

### §2 Persistence Constraint Updated

Replaced "No Save Slots / CI scan for RestoreToSnapshot" with mode-aware consequence-aware restoration language. CI test now scans for `restore_raw()` calls outside `PersistenceModule`.

### WorldState Fields Added

- `deferred_work_queue` (replaces `consequence_queue` + separate transit pool)
- `game_mode: GameMode`
- `province_route_table`
- `current_schema_version`

### PlayerCharacter Fields Added

- `restoration_history: RestorationHistory`
- `calendar_capacity_modifiers: std::vector<TimeBoundedModifier>`
- `ironman_eligible: bool`

### Header Updated

- Title updated to v7, Pass 3 Session 7 Complete

---

## Change Log — Pass 3, Session 8 (Physics Consistency Pass)

### Design Principle Established

| Principle | Location | Statement |
|---|---|---|
| Physics principle | §18.14 (new) | "If it has mass, it obeys the transit system." Goods, people (NPC and player), LOD 1 imports, criminal expansion personnel all travel at physics speed. Information (phone, wire, news, evidence knowledge) is instant. Research tree may introduce future tech that reduces transit times — no such tech exists at 2000 start. |

### Blockers Resolved

| ID | Location | Resolution |
|---|---|---|
| B1 | §5 NPCBusiness struct | Added `dispatch_day_offset: uint8_t` and `province_id: uint32_t` to struct |
| B2 | §4 NPC struct | Removed `ActionQueue pending_actions`. NPC delayed actions are `DeferredWorkItem` entries. Added `home_province_id`, `current_province_id`, `travel_status` for physics compliance |
| B3 | §6 ConsequenceEntry | ConsequenceEntry is now documented as a payload type inside `DeferredWorkItem`, not a separate queue. `target_tick` field removed from ConsequenceEntry (lives on `DeferredWorkItem.due_tick`). Queue description replaced with DeferredWorkQueue reference |
| B4 | §5 RegionalMarket | `region_id` renamed to `province_id`. WorldState comment updated. `supply` field comment updated to exclude in-transit goods |

### Inconsistencies Resolved

| ID | Location | Resolution |
|---|---|---|
| I1 | §6 Evidence Decay | "Per tick" spec replaced with DeferredWorkQueue 7-tick batch. Decay formula multiplied by 7 for batch application. Reschedule pattern documented |
| I2 | §13 Module spec | Fear decay removed from tick step 22 module. Moved to DeferredWorkQueue `npc_relationship_decay` batch (every 30 ticks per NPC). InfluenceNetworkHealth recomputation made dirty-flag driven via `network_health_dirty` bool in WorldState |
| I3 | §5 Price Model | `supply` variable definition updated: explicitly excludes in-transit goods; includes only local production + arriving shipments this tick. LOD 1 import transit reference added |

### Ambiguities Resolved

| ID | Decision | Location |
|---|---|---|
| A1 | LOD 1 imports travel at physics speed via sea route (Option A) | §18.16 (new) — LOD 1 Import Transit. `TransitShipment` created on offer acceptance. Transit time = nation centroid → province port at sea speed. LOD 1 transit variability multiplier (1.0–1.3) per nation. No interception, no terrain modifier. Price ceiling applies immediately on acceptance; supply effect delayed |
| A2 | NPC physical movement obeys transit (Option A) | §18.15 (new) — NPC Physical Movement. `NPCTravelStatus` enum. Travel dispatch via DeferredWorkQueue. Criminal NPCs in transit use interception model. List of actions requiring physical presence vs. remote-executable actions. No air travel in V1 (research tree milestone) |

### Precision Gaps Resolved

| ID | Location | Resolution |
|---|---|---|
| P1 | §6 Evidence tokens | Evidence propagation is information (instant). Physical evidence artifacts would require transit if moving provinces, but knowledge of evidence propagates via communication channel same tick |
| P2 | §12 ClimateProfile | `climate_stress_current` comment updated to distinguish per-tick accumulation from 7-tick downstream batch |
| P3 | §17/§18.17 | Criminal territory expansion now creates travel DeferredWorkItems for expansion personnel. `dominance_by_province[P]` stays 0.0 until `territory_established` consequence fires. Transit window creates exploitable coverage gap |

### Sections Added

| Section | Content |
|---|---|
| §18.14 | Physics Principle — canonical "if it has mass" rule; research tree note; future tech path |
| §18.15 | NPC Physical Movement — `NPCTravelStatus` enum, travel dispatch, mode selection, interception during travel, action list for presence-required vs remote-executable |
| §18.16 | LOD 1 Import Transit — `TransitShipment` creation on acceptance, transit time formula, LOD 1 sentinel IDs, price ceiling vs supply timing |
| §18.17 | Criminal Territory Expansion Transit — expansion team dispatch, establishment deferred work, dominance seed on arrival, coverage gap mechanic |

### Structs Modified

| Struct | Changes |
|---|---|
| `NPCBusiness` | Added `dispatch_day_offset: uint8_t`, `province_id: uint32_t` |
| `NPC` | Removed `ActionQueue pending_actions`. Added `home_province_id`, `current_province_id`, `travel_status: NPCTravelStatus` |
| `PlayerCharacter` | Added `home_province_id`, `current_province_id`, `travel_status: NPCTravelStatus` |
| `RegionalMarket` | `region_id` → `province_id`. `supply` comment updated |
| `ConsequenceEntry` | `target_tick` field removed (lives on `DeferredWorkItem.due_tick`). Struct is now a payload type only |
| `CriminalOrganization` | Added `decision_day_offset: uint8_t`. `territory_region_ids` → `territory_province_ids`. `dominance_by_region` → `dominance_by_province` |

### WorldState Fields Added

- `network_health_dirty: bool` — dirty flag for InfluenceNetworkHealth recomputation

### Header Updated

- Title updated to v8, Pass 3 Session 8 Complete

---

## Change Log — Pass 3, Session 9 (Structural Fixes)

| Fix | Location | Change |
|---|---|---|
| 9A | §18 | Deleted duplicate `### 18.5 — Informal Economy Price Formula` section. The formula exists correctly at §18.13. |
| 9B | §11 | Merged split `PlayerCharacter` struct into single definition. Removed "PlayerCharacter additions" prose wrapper and duplicate `milestone_log`/`achieved_milestones` fields. Added `home_province_id`, `current_province_id`, `travel_status`, `restoration_history`, `calendar_capacity_modifiers`, `ironman_eligible` to the canonical struct. Supporting structs (`TimeBoundedModifier`, `TimelineRestorationRecord`, `RestorationHistory`) placed immediately before `Milestone evaluation` block. |
| 9C | §11, §12, §14 | Renamed stale field names: `PlayerCharacter.starting_region_id` → `starting_province_id`; `Nation.region_ids` → `province_ids`; `OppositionOrganization.region_id` → `province_id`. Updated §14 formation trigger algorithm reference `region.opposition_organization` → `province.opposition_organization`. `Province.region_id` (foreign key to Region grouping layer) unchanged. |
| 9D | §17 | `CriminalStrategicInputs.territory_pressure` comment updated: `dominance_by_region in shared regions` → `dominance_by_province in shared provinces`. Reflects rename applied in Session 8. |
| 9E | GDD v1.2, R&D v1.1, Commodities v2.1 | Companion document version citations updated from "Technical Design v5" / "Technical Design Document v5" to "Technical Design Document v8" in all three companion document headers. |

### Header Updated

- Title updated to v9, Pass 3 Session 9 (Structural Fixes) complete

---

## Change Log — Pass 3, Session 10 (Missing Struct Definitions)

Pass 3 Session 10 (Missing Struct Definitions) complete.

| Addition | Location | Change |
|---|---|---|
| 10A | §12 (Province and Nation) | Added `Lod1NationProfile` struct. Fields cover trade behavior, production capacity, R&D / tech advancement, political stability tracking, climate, geography, and LOD 1 archetype. Embedded in `Nation` as `std::optional<Lod1NationProfile> lod1_profile` (added after `tariff_schedule`). V1 note added: 1 LOD 0 nation (player's home; `lod1_profile` null); all other nations have `lod1_profile` populated. Expansion enables additional LOD 0 nations. |
| 10B | §10 (WorldState and DeltaBuffer) | Added `CrossProvinceDelta` and `CrossProvinceDeltaBuffer` structs immediately after the DeltaBuffer definition block. `CrossProvinceDelta` holds source/target province IDs, `due_tick` (current_tick + 1), and a single optional payload (`NPCDelta`, `DeferredWorkItem`, `EvidenceToken`, or `RegionalMarketDelta`). `CrossProvinceDeltaBuffer` provides `push()` (called by province workers, partition-safe) and `flush_to_deferred_queue()` (called by main thread after worker join). Added `cross_province_delta_buffer: CrossProvinceDeltaBuffer` to `WorldState` (scratch field; cleared each tick; not part of persistence snapshot). |

### WorldState Fields Added

- `cross_province_delta_buffer: CrossProvinceDeltaBuffer` — scratch buffer for cross-province effects generated during province-parallel execution; always empty at save time; not persisted.

### Nation Fields Added

- `lod1_profile: std::optional<Lod1NationProfile>` — null for the player's LOD 0 home nation; populated for all other (LOD 1) nations.

### Header Updated

- Title updated to v10, Pass 3 Session 10 (Missing Struct Definitions) complete

---

## Change Log — Pass 3, Session 11 (Precision Gap Completions)

Pass 3 Session 11 (Precision Gap Completions) complete.

| Fix | Location | Change |
|---|---|---|
| 11A | §18.7 | Added `rail = 0.45` to `route_mode_modifier` table. Note added explaining rail is cheaper than road (established freight lines) but more expensive than sea (infrastructure capital cost). Value in `transport.json`. |
| 11B | §18.5 | Added [V1] restriction note to air freight row: air applies to goods only; NPCs do not use air transport in V1; NPC air travel is a future research tree milestone (§18.15); NPC transit dispatch must select from road, rail, sea, or river. |
| 11C | §3.3, §14 | Added `community_stage_check` to `WorkType` enum in §3.3 (one item per province; community response stage threshold evaluation). Replaced ambiguous `WorkType::market_recompute — or a dedicated community_stage_check item` language in §14 with explicit `WorkType::community_stage_check` scheduling spec. `subject_id` on `DeferredWorkItem` is `province_id` being evaluated. Changed "At world gen" to "At world load" for consistency. |
| 11D | §18.9, §18.10 | Moved `is_concealed` and `route_concealment_modifier` from `RouteProfile` (route-general, precomputed) to `TransitShipment` (per-shipment, set at dispatch). Both fields added to `TransitShipment` struct with behavioral comments. §18.10 interception model note updated: concealment fields are read from `TransitShipment`, not `RouteProfile`. |
| 11E | §18.16 | `lod1_transit_variability_multiplier` description updated: field is stored in `nation.lod1_profile->lod1_transit_variability_multiplier` (§12 `Lod1NationProfile`); seeded from `world.json` at load. Struct home was previously unspecified. |
| 11F | §20.3 | Added explanatory note below archetype table clarifying that '—' cells equal 0.0. Documents the LOD 1 monthly update guard conditions (`if surplus > 0` for exports, `if surplus < 0` for imports) and behavioral implications of `export_margin = 0.0` and `trade_openness = 0.3`. |
| 11G | §2a | Added `### Lua Behavior Hook Interface` subsection after manifest format examples. Specifies: hook type `"behavior_hook"`, entry point `on_tick(ctx)`, read-only `ctx` fields (`province_id`, `tick`, `good_prices`, `era`), return value semantics (nil = no effect; table of delta descriptors), supported delta types (`price_modifier`, `npc_memory`), budget enforcement (0.1ms normal / 0.5ms extended, per province per tick), and V1 constraint note. |

### Header Updated

- Title updated to v11, Pass 3 Session 11 (Precision Gap Completions) complete

---

---

## Change Log — Pass 3, Session 12 (Mechanical Fixes)

Pass 3 Session 12 (Mechanical Fixes) — R-1, R-2, C-1, C-2, C-3, C-4.

| Fix | Location | Change |
|---|---|---|
| R-1 | §4 NPC behavior pseudocode (tick step 10) | Replaced stale comment `// action is queued in npc.pending_actions; executes at action.execution_tick` with accurate three-line comment documenting that action is dispatched as `DeferredWorkItem` in `WorldState.deferred_work_queue` (WorkType::consequence) and that `pending_actions` was removed. |
| R-2 | §18.7 transport.json constants table | Added `transit.max_concealment_modifier` = 0.40. Documents upper bound on `route_concealment_modifier` for concealed shipments; caps interception risk reduction at 40% regardless of route profile; notes field is set at dispatch and stored on `TransitShipment` (see §18.9). |
| C-1 | §6 NPC credibility evaluation | Removed `holder.criminal_record == true` from condition 1 — field does not exist on NPC struct. Replaced with `holder.public_credibility < 0.3` standalone check, with comment that it covers all disqualifying causes (criminal history, professional disgrace, scandal, false testimony). No other changes to condition 2 or 3. `criminal_record` not added to NPC struct. |
| C-2 | §16 InvestigatorMeter struct | Added full comment block to `target_id` field: resolution logic (argmax signal contribution among known actors), player/NPC equality as targets, 0 = inactive sentinel, V1 one-meter-per-LE-NPC constraint, [EX] note for simultaneous meters. Added four-line comment block at top of tick step 13 LE integration formula: LE cannot distinguish player-owned from NPC-org facilities from signal alone; target_id resolved after aggregation; actors below awareness threshold not targetable without EvidenceToken. |
| C-3 | §7 hostile creditor escalation | Replaced flat hostile action type list with context-gated annotated list. `contact_rival_criminal_org` gated on `player.reputation.street > 0.0`. Added `contact_rival_competitor` gated on player having active NPCBusiness operations. Added explanatory note that context gates filter candidates before expected_value evaluation. |
| C-4 | §5 after BusinessSector enum | Added `### BusinessSector vs criminal_sector — canonical rules` section. Defines all four field-combination cases with validity rules (criminal/false = INVALID in V1, loader rejects). Establishes `criminal_sector` as authoritative for market layer selection; `BusinessSector::criminal` as semantic/UI tag only. |

### Header Updated

- Title updated to v12, Pass 3 Session 12 (Mechanical Fixes) complete

---

## Change Log — Pass 3, Session 13 (Universal Facility Signals and Scrutiny System)

Pass 3 Session 13 (Universal Facility Signals and Scrutiny System) — 13A–13F.

| Change | Location | Description |
|---|---|---|
| 13A | §16 header + section index | Renamed section from "Criminal OPSEC Scoring" to "Facility Signals and Scrutiny System". Updated section index cross-reference table. |
| 13B | §16 FacilityOPSEC struct | Renamed `FacilityOPSEC` → `FacilitySignals` everywhere. Replaced struct definition with universal version: `base_signal_composite` replaces `base_detection_risk`; `scrutiny_mitigation` replaces `corruption_coverage` (covers both criminal corruption and legitimate compliance investment); `net_signal` replaces `net_detection_risk`. Added struct-level comment explaining signals are physical facts with no legality encoding. Renamed facility field `opsec` → `signals` throughout. |
| 13C | §16 Detection Risk Formula | Renamed section to "Signal Composite Formula". Renamed `FacilityTypeOPSECWeights` → `FacilityTypeSignalWeights` everywhere. Updated all formula references from `facility.opsec.*` to `facility.signals.*` and from `type_def.opsec_weights` to `type_def.signal_weights`. Updated `facility_types.csv` column name prefix from `opsec_w_*` to `signal_w_*`. Added proposed defaults table for 7 facility types. Replaced criminal-specific moddability note with universal one. |
| 13D | §16 after InvestigatorMeter block | Added `### Regulator Scrutiny Meter [V1]` subsection. Adds `RegulatorStatus` enum (4 states: inactive/notice_filed/formal_audit/enforcement_action with default thresholds 0.25/0.50/0.75). Adds `RegulatorScrutinyMeter` struct (one per facility per regulator NPC). Adds tick step 13 regulator integration loop: reads `chemical_waste` and `foot_traffic` only, applies `scrutiny_mitigation`, scales to fill_rate. Adds 6 named constants in `simulation_config.json → scrutiny`. |
| 13E | §16 Tick Step 13 LE formula | Updated signal aggregation from `facility.opsec.net_detection_risk for all criminal facilities` to `facility.signals.net_signal for all facilities where facility.criminal_sector == true`. Added reader/filter note: LE filters to criminal_sector; regulators read all; same struct field, different filter. Updated example comment from `net_detection_risk 0.50` to `net_signal 0.50`. Updated `decay_rate` prose note to reference `net_signal`. Updated opsec constants table description to reference `net_signal`. |
| 13F | §12 RegionConditions struct | Added `formal_employment_rate` (0.0–1.0; working-age population in formal employment; updated monthly; suppressed by high criminal dominance) and `regulatory_compliance_index` (0.0–1.0; mean clean-facility scrutiny level; recomputed tick step 13). Added note that `criminal_dominance_index` and `formal_employment_rate` correlation is emergent, not mechanically enforced. |

### New Structs Added

| Struct | Section | Notes |
|---|---|---|
| `FacilitySignals` | §16 | Replaces `FacilityOPSEC`; universal; all facility types |
| `FacilityTypeSignalWeights` | §16 | Replaces `FacilityTypeOPSECWeights`; loaded from `facility_types.csv` |
| `RegulatorScrutinyMeter` | §16 | New; one per facility per regulator NPC |
| `RegulatorStatus` | §16 | New enum; 4 states |

### RegionConditions Fields Added

- `formal_employment_rate: float` — formal sector employment fraction; updated monthly
- `regulatory_compliance_index: float` — mean formal-facility scrutiny cleanliness; recomputed tick step 13

### Header Updated

- Title updated to v13, Pass 3 Session 13 (Universal Facility Signals and Scrutiny System) complete

---

---

## Change Log — Pass 3, Session 14 (Grey Zone Compliance and Universal Community Grievance)

Pass 3 Session 14 (Grey Zone Compliance and Universal Community Grievance) — 14A–14C.

| Change | Location | Description |
|---|---|---|
| 14A | §5 NPCBusiness struct; §16 scrutiny tick step 13; §16 scrutiny constants table | Added `regulatory_violation_severity: float` (0.0–1.0) to NPCBusiness struct. Field models legal-but-noncompliant operations: 0.0–0.5 generates evidence tokens and feeds RegulatorScrutinyMeter additive; 0.5–1.0 triggers enforcement_action consequence. Does not change criminal_sector or market layer. Decays at `config.scrutiny.violation_decay_rate` per tick when no new violations. Comment notes field applies universally — NPC businesses and player-operated facilities carry identical semantics. Updated regulator scrutiny fill_rate formula to add `violation_additive = f.regulatory_violation_severity × config.scrutiny.violation_to_fill_rate_scale`. Added two new scrutiny constants: `violation_to_fill_rate_scale` = 0.008 (slightly higher than physical signal scale; documented violations more actionable); `violation_decay_rate` = 0.0005 (~5.5 years to halve). |
| 14B | §14 Grievance Level aggregation | Removed `player_actor_ids` filter. Replaced flat sum with `action_type_weight(MemoryType)` weighted sum. Direct harm types (witnessed_illegal_activity, witnessed_safety_violation, witnessed_wage_theft, physical_hazard, retaliation_experienced) → weight 1.0; economic harm types (employment_negative, facility_quality) → weight 0.5; all other types → 0.0. Updated intro prose: all actors contribute at same rate for same action type. Added design note explaining player dominance is emergent from scale, not formula privilege; political grievance flows through institutional_trust by action type, not actor identity. |
| 14C | §4 SATISFACTION_RELEVANT_TYPES table; §4 Whistleblower eligibility condition | Added `witnessed_safety_violation` (−0.4 to −0.9; unsafe equipment, chemical exposure, documented hazard) and `witnessed_wage_theft` (−0.3 to −0.7; payroll fraud, unpaid overtime, illegal deductions) to SATISFACTION_RELEVANT_TYPES table. Replaced `type == witnessed_illegal_activity` single-type match in whistleblower eligibility with three-type set {witnessed_illegal_activity, witnessed_safety_violation, witnessed_wage_theft}. Added design note: eligibility is path-agnostic; memory type determines contact authority (LE NPC vs. regulator vs. journalist); contact target resolved by behavior engine expected_value, not hardcoded. |

### NPCBusiness Fields Added

- `regulatory_violation_severity: float` — legal-but-noncompliant severity; universally applicable to all facility types

### Scrutiny Constants Added

| Constant | Config | Value |
|---|---|---|
| `violation_to_fill_rate_scale` | simulation_config.json → scrutiny | 0.008 |
| `violation_decay_rate` | simulation_config.json → scrutiny | 0.0005 |

### Memory Types Added to SATISFACTION_RELEVANT_TYPES

- `witnessed_safety_violation` — weight range −0.4 to −0.9
- `witnessed_wage_theft` — weight range −0.3 to −0.7

### Header Updated

- Title updated to v14, Pass 3 Session 14 (Grey Zone Compliance and Universal Community Grievance) complete

---

---

## Change Log — Pass 3, Session 15 (Player-Special Fixes)

Pass 3 Session 15 (Player-Special Fixes) — P-1 through P-6.

| Fix | Location | Description |
|---|---|---|
| P-1 | §22.3 SnapshotSummary struct | Replaced single `exposure_level` field with two fields: `player_exposure_level` (max InvestigatorMeter where target_id == player.id; 0.0 if player is not a target; UI: personal legal risk indicator) and `regional_le_activity` (max InvestigatorMeter across all LE NPCs regardless of target; UI: ambient heat indicator). Updated size estimate comment from ~200 to ~204 bytes per summary. |
| P-2 | §19 accident_event effects block; §19 constants table | Removed `if player owns facility` gate from accident evidence token generation. Replaced with unconditional EvidenceToken creation when accident severity >= `config.events.evidence_severity_threshold` (default: 0.3); subject_id = facility.owner_id (player or NPC business). Added `events.evidence_severity_threshold = 0.3` to §19 simulation.json constants table with note that below-threshold accidents generate only local memory entries. |
| P-3 | §5 RegionalMarket struct; §5 Step 1 price formula | Added comments to previously undocumented `import_price_ceiling` and `export_price_floor` fields: ceiling set on LOD 1 import offer acceptance (applied immediately, before goods arrive); floor set by LOD 1 export bid; 0.0 = no active offer. Updated Step 1 equilibrium price formula clamp bounds to use LOD 1 offer fields when non-zero, falling back to config coefficients otherwise. Added inline note: LOD 1 prices are a harder constraint than global structural coefficients. |
| P-4 | §6 EvidenceToken struct | Replaced `// who this is evidence against (usually player)` comment on `subject_npc_id` with actor-neutral comment: `player_id, npc_id, or npc_business_id — generated against any actor whose actions produce observable signals`. |
| P-5 | §7 after ObligationNode struct | Added V1 scope note: WorldState.obligation_network tracks only player-as-creditor or player-as-debtor nodes. NPC-to-NPC obligations are modeled implicitly through motivation weights and relationship trust values in V1. Full NPC-to-NPC graph is expansion scope. Labeled as deliberate V1 constraint, not architectural limitation. |
| P-6 | §4 NPC struct Resources section | Added `movement_follower_count: uint32_t` under Resources. Meaningful for community_leader, politician, union_organizer, ngo_investigator roles; 0 for all others. Updated by tick step 22 alongside PlayerCharacter.movement_follower_count using identical logic — player is not special in the movement system. Added post-struct note explaining field exists on all NPCs for struct uniformity. |

### SnapshotSummary Fields Changed

- `exposure_level: float` → removed
- `player_exposure_level: float` → added (personal LE targeting indicator)
- `regional_le_activity: float` → added (ambient environment heat indicator)

### NPC Fields Added

- `movement_follower_count: uint32_t` — movement leadership count; 0 for non-movement-capable roles

### Constants Added

| Constant | Config file | Default |
|---|---|---|
| `events.evidence_severity_threshold` | simulation.json | 0.3 |

### Header Updated

- Title updated to v15, Pass 3 Session 15 (Player-Special Fixes) complete

---

---

## Change Log — Pass 3, Session 16 (Constants Homing)

Pass 3 Session 16 (Constants Homing) — Groups A–E.

| Group | Location | Description |
|---|---|---|
| A — §6 | §6 investigation.json constants table | Added `EVIDENCE_SHARE_TRUST_THRESHOLD = 0.45` to investigation.json constants table. Was previously referenced inline only (= 0.45 in §11 prose). Now has declared config file home. |
| A — §11 | §11 Skill Decay section | Added named constants table (`simulation.json`) after skill decay prose. `SKILL_DECAY_GRACE_PERIOD = 30` (ticks before decay begins; ~1 in-game month). `SKILL_DOMAIN_FLOOR = 0.05` (minimum level; decay floor). Both were previously inline parenthetical values only. |
| A — §13 | §13 Influence Network, after Floor Principle recovery ceiling block | Added named constants table (`simulation.json`) with 7 entries: `TRUST_CLASSIFICATION_THRESHOLD = 0.4`, `FEAR_CLASSIFICATION_THRESHOLD = 0.35`, `CATASTROPHIC_TRUST_LOSS_THRESHOLD = −0.55`, `CATASTROPHIC_TRUST_FLOOR = 0.1`, `RECOVERY_CEILING_FACTOR = 0.60`, `RECOVERY_CEILING_MINIMUM = 0.15`, `HEALTH_TARGET_COUNT = 10`. All were previously inline literal values in code blocks and prose. |
| B | §15 vote resolution formula; §15 election resolution formula; end of §15 | Removed inline `MAJORITY_THRESHOLD = 0.5 for standard legislation; 0.67 for constitutional` from vote resolution formula block. Replaced with `config.politics.MAJORITY_THRESHOLD` reference and note pointing to constants table below. Removed inline `RESOURCE_MAX_EFFECT = 0.15` comment and bare `RESOURCE_SCALE`/`RESOURCE_MAX_EFFECT` identifiers from election formula. Replaced with `config.politics.*` qualified references. Added named constants table (`simulation_config.json → politics`) with 5 entries: `SUPPORT_THRESHOLD = 0.55`, `OPPOSE_THRESHOLD = 0.35`, `MAJORITY_THRESHOLD = 0.50` (noting 0.67 constitutional is [EX]), `RESOURCE_SCALE = 2.0`, `RESOURCE_MAX_EFFECT = 0.15`. |
| C | §5 NPC Business Strategic Decision Matrix | Replaced three inline cash multipliers with `config.business.*` references: `2 × monthly_operating_costs` → `config.business.cash_critical_months × monthly_operating_costs`; both `3 × monthly_costs` instances → `config.business.cash_comfortable_months × monthly_operating_costs`; `5 × monthly_costs` → `config.business.cash_surplus_months × monthly_operating_costs`. Added named constants table (`simulation_config.json → business`): `cash_critical_months = 2.0`, `cash_comfortable_months = 3.0`, `cash_surplus_months = 5.0`. |
| D | §17 CriminalStrategicInputs struct | Replaced `CriminalOrganization.cash / CASH_COMFORTABLE_THRESHOLD` comment on `cash_level` with full computed ratio using `config.business.cash_comfortable_months` — the same constant as legitimate NPCBusiness. Added `monthly_operating_cost_estimate` derivation (facility costs + member payroll). Added ratio interpretation thresholds (>= 1.0 comfortable; < 0.5 cost-cutting). `CASH_COMFORTABLE_THRESHOLD` retired — no standalone constants table entry existed to remove. |
| E | §18.17 expansion team selection; §18.17 criminal constants table | Replaced `max(2, ceil(...))` with `max(config.criminal.min_expansion_team_size, ceil(...))`. Updated inline comment to reference both constants. Added named constants table (`simulation_config.json → criminal`) with two entries: `cash_per_expansion_slot = 5000.0` (formalising existing inline comment) and `min_expansion_team_size = 2` (new; replaces hardcoded literal). |

### Constants Homed This Session

| Constant | Config file → key | Default |
|---|---|---|
| `EVIDENCE_SHARE_TRUST_THRESHOLD` | investigation.json | 0.45 |
| `SKILL_DECAY_GRACE_PERIOD` | simulation.json | 30 |
| `SKILL_DOMAIN_FLOOR` | simulation.json | 0.05 |
| `TRUST_CLASSIFICATION_THRESHOLD` | simulation.json | 0.4 |
| `FEAR_CLASSIFICATION_THRESHOLD` | simulation.json | 0.35 |
| `CATASTROPHIC_TRUST_LOSS_THRESHOLD` | simulation.json | −0.55 |
| `CATASTROPHIC_TRUST_FLOOR` | simulation.json | 0.1 |
| `RECOVERY_CEILING_FACTOR` | simulation.json | 0.60 |
| `RECOVERY_CEILING_MINIMUM` | simulation.json | 0.15 |
| `HEALTH_TARGET_COUNT` | simulation.json | 10 |
| `SUPPORT_THRESHOLD` | simulation_config.json → politics | 0.55 |
| `OPPOSE_THRESHOLD` | simulation_config.json → politics | 0.35 |
| `MAJORITY_THRESHOLD` | simulation_config.json → politics | 0.50 |
| `RESOURCE_SCALE` | simulation_config.json → politics | 2.0 |
| `RESOURCE_MAX_EFFECT` | simulation_config.json → politics | 0.15 |
| `cash_critical_months` | simulation_config.json → business | 2.0 |
| `cash_comfortable_months` | simulation_config.json → business | 3.0 |
| `cash_surplus_months` | simulation_config.json → business | 5.0 |
| `cash_per_expansion_slot` | simulation_config.json → criminal | 5000.0 |
| `min_expansion_team_size` | simulation_config.json → criminal | 2 |

### Constants Retired

- `CASH_COMFORTABLE_THRESHOLD` — subsumed by `config.business.cash_comfortable_months`

### Header Updated

- Title updated to v16, Pass 3 Session 16 (Constants Homing) complete

---

## Change Log — Pass 3, Session 17 (Technology Lifecycle Integration)

### Summary
Integrated the technology lifecycle model (R&D doc §Part 3.5) into TDD structs. Three structural changes: NPCBusiness, WorldState, WorkType.

### Changes

**§5 NPCBusiness struct**
- Replaced `float technology_tier` with `ActorTechnologyState actor_tech_state`.
- `technology_tier` was a flat float with no per-technology granularity. `ActorTechnologyState` holds a map of `TechHolding` entries (node_key → stage, maturation_level, maturation_ceiling) for all technology nodes the actor has researched or licensed.
- Added derivation comment: effective tech tier at recipe execution is derived from `actor_tech_state` + facility tier at call time; it is no longer a persisted scalar.

**§5 Technology Tier Efficiency Differential prose**
- Updated quality ceiling interaction note. The tier-based ceiling is now further capped by `actor_tech_state.maturation_of(recipe.key_technology_node)` for technology-intensive recipes. Commodity recipes (`key_technology_node = ""`) are unaffected.
- Removed `FacilityRecipe.tech_tier_output_bonus` R&D cross-reference (that field is unchanged; the cross-reference was misleading in this context).

**§10 WorldState struct**
- Added `GlobalTechnologyState global_technology_state` in new `// --- Technology State ---` block, after the Economy block.
- `GlobalTechnologyState` tracks world-level knowledge signals: `first_researched_by`, `first_commercialized_by`, `domain_knowledge_levels`, `active_patents`, `current_era`, `global_co2_index`, `climate_state`. Per-actor holdings remain in `NPCBusiness.actor_tech_state`.
- Updated by tick step 26 alongside evidence notifications.

**§3.3 WorkType enum**
- Added `maturation_project_advance` work item. Scheduled every tick per active `MaturationProject`. Runs in tick step 26. Payload: `{ business_id, node_key }`. On completion: `actor_tech_state.holdings[node_key].maturation_level` incremented and re-clamped to `maturation_ceiling`.
- Added note: commercialization decision (`CommercializationDecision` enum, R&D §3.5) resolves via `npc_business_decision` (NPC quarterly) or immediate player command.

### Fields Added

| Struct | Field | Type |
|---|---|---|
| `NPCBusiness` | `actor_tech_state` | `ActorTechnologyState` |
| `WorldState` | `global_technology_state` | `GlobalTechnologyState` |

### Fields Removed

| Struct | Field | Reason |
|---|---|---|
| `NPCBusiness` | `technology_tier` | Replaced by `actor_tech_state`; effective tier is now derived per-recipe |

### Enums Modified

| Enum | Addition |
|---|---|
| `WorkType` | `maturation_project_advance` |

### Header Updated

- Title updated to v17, Pass 3 Session 17 (Technology Lifecycle Integration) complete.

---

## Change Log — Pass 3, Session 18 (Cross-Doc Audit Fixes)

Resolves findings B-01, A-01/I-09, and P-04 from the Pass 3 post-Session 17 cross-document consistency audit.

### Changes

**§3.3 WorkType enum — `commercialize_technology` added [B-01]**
- Added `commercialize_technology` WorkType entry for player-commanded technology commercialization.
- Payload: `{ business_id, node_key, decision: CommercializationDecision }` (enum in R&D §Part 3.5).
- Execution: immediate — fires in the same tick it is queued, after step 26 R&D processing completes.
- Precondition: `actor_tech_state.has_researched(node_key) == true`. Attempting to commercialize an unresearched node is a no-op with UI error feedback.
- Effects: sets `holdings[node_key].stage = commercialized`, sets `commercialized_tick`, updates `GlobalTechnologyState.first_commercialized_by` if actor is first for that node.
- NPC path: commercialization decision is evaluated inside `WorkType::npc_business_decision` (quarterly). NPC actors do not use `commercialize_technology` WorkType. Both paths write identical fields.
- Removed the prior inline comment on `maturation_project_advance` that described the player path ambiguously; that comment is superseded by the new WorkType entry.

**§12 RegionConditions struct — `drought_modifier` and `flood_modifier` added [A-01 / I-09]**
- Added `drought_modifier: float` (0.0–1.0) as a runtime active-event scalar. 1.0 = no active drought; 0.3 = severe drought active. Recovers toward 1.0 at `config.climate.drought_recovery_rate` when no drought event is active.
- Added `flood_modifier: float` (0.0–1.0) as a runtime active-event scalar. 1.0 = no active flood; 0.0 = inundated. Shorter-duration than droughts.
- Both updated by `WorkType::climate_downstream_batch`. Read by farm recipe production calculation (`ComputeOutputQuality`, Commodities & Factories §Part 7).
- Added clarifying note: `soil_health` (also referenced by the farm formula in Commodities) is a per-facility float on farm Facility structs, not a province-level field. Updated by tick step 1 (production) agricultural management logic.
- Design note: `drought_modifier` and `flood_modifier` are distinct from the static exposure factors `ClimateProfile.drought_vulnerability` / `flood_vulnerability`, which are GIS-sourced structural risks. The modifiers are runtime state; the vulnerability fields are static province properties.

**§12 ResourceDeposit struct — permafrost dual-gate formula [P-04]**
- Expanded `accessibility` field comment to document the permafrost thaw dual-gate.
- Two conditions must both hold for permafrost-locked deposits to become accessible: (1) `province.climate.climate_stress_current > config.resources.permafrost_thaw_threshold` AND (2) `actor_tech_state.has_researched("arctic_drilling") == true`. When both hold, `accessibility` is restored to seeded base value.
- Scope: applies to CrudeOil and NaturalGas deposits in provinces with KoppenZone ET/EF or latitude above `config.resources.arctic_latitude_threshold`.
- Evaluated in tick step 1 (production) before extraction output is computed.
- Two new named constants added to `resources.json`: `permafrost_thaw_threshold = 0.40`, `arctic_latitude_threshold = 66.5`.

**Config file registry updated**
- Added `resources.json` to the base package config file list (§2 Package System). Previously referenced inline in ResourceDeposit but not registered.

### Fields Added

| Struct | Field | Type |
|---|---|---|
| `RegionConditions` | `drought_modifier` | `float` |
| `RegionConditions` | `flood_modifier` | `float` |

### WorkType Additions

| Enum | Addition |
|---|---|
| `WorkType` | `commercialize_technology` |

### Constants Added

| Constant | Config file | Value |
|---|---|---|
| `permafrost_thaw_threshold` | `resources.json` | 0.40 |
| `arctic_latitude_threshold` | `resources.json` | 66.5 |

### Header Updated

- Title updated to v18, Pass 3 Session 18 (Cross-Doc Audit Fixes) complete.
- Companion documents header: Commodities & Factories updated from v2.1 → v2.2 (correcting stale reference; Commodities was updated in Session 17 but TDD header was not updated at that time).

---

## Change Log — Pass 3, Session 19 (Province Terrain Modifiers and Community Trauma)

Resolves findings P-03 and P-06 from the Pass 3 post-Session 17 cross-document consistency audit.

### Changes

**§12 Province struct — `has_karst` and `historical_trauma_index` added [P-03, P-06]**

`has_karst: bool` — set by GIS pipeline from WorldGen karst probability layer at world load; static at runtime. WorldGen v0.17 §Stage 7 flagged this as a load-bearing V1 field for criminal smuggling routes and hidden facility concealment. Two downstream effects:
- `RouteProfile.concealment_bonus` is incremented by `config.routes.karst_concealment_bonus` (= 0.08) for each karst province on the route path, at route table build time. Capped by `transit.max_concealment_modifier` at dispatch.
- `FacilitySignals.scrutiny_mitigation` receives `config.opsec.karst_mitigation_bonus` (= 0.10) for facilities physically located in a province with `has_karst == true`. Applied during tick step 13 scrutiny computation.

`historical_trauma_index: float` — 0.0–1.0 static province measure. Set by GIS pipeline from WorldGen historical data layer at world load; not updated at runtime. Effect on community response (§14): applied once at world load as a starting-condition floor/ceiling on `CommunityState.grievance_level` and `CommunityState.institutional_trust`. Recovery from these starting conditions is possible through player and NPC institutional investment — the trauma index does not act as a per-tick floor.

**§12 RouteProfile struct — `concealment_bonus` field added [P-03]**
- Added `float concealment_bonus` to `RouteProfile`. Computed at route table build time from karst province traversal. Zero for routes through no karst provinces.

**§14 Community constants table — two new constants [P-06]**
- `trauma_grievance_floor_scale` = 0.25: `historical_trauma_index × scale` = `grievance_level` floor at world load.
- `trauma_trust_ceiling_scale` = 0.30: `1.0 - historical_trauma_index × scale` = `institutional_trust` ceiling at world load.

**§18.6 transport.json constants — `routes.karst_concealment_bonus` added [P-03]**
- `routes.karst_concealment_bonus` = 0.08: per-province karst concealment bonus, stacking across multiple karst provinces on a route path.

**§16 opsec constants — `karst_mitigation_bonus` added [P-03]**
- `karst_mitigation_bonus` = 0.10: added to `FacilitySignals.scrutiny_mitigation` for facilities in karst provinces.

### Fields Added

| Struct | Field | Type |
|---|---|---|
| `Province` | `has_karst` | `bool` |
| `Province` | `historical_trauma_index` | `float` |
| `RouteProfile` | `concealment_bonus` | `float` |

### Constants Added

| Constant | Config file | Value |
|---|---|---|
| `routes.karst_concealment_bonus` | `transport.json` | 0.08 |
| `karst_mitigation_bonus` | `simulation_config.json → opsec` | 0.10 |
| `trauma_grievance_floor_scale` | `simulation_config.json → community` | 0.25 |
| `trauma_trust_ceiling_scale` | `simulation_config.json → community` | 0.30 |

### Header Updated

- Title updated to v19, Pass 3 Session 19 complete.
- Companion documents: R&D updated to v2.2; Commodities updated to v2.3 (correcting stale v2.2 reference in v18 header).

---

## Change Log — Pass 3, Session 20 (Economy Consolidation and Universal OPSEC)

Pass 3 Session 20 (Economy Consolidation and Universal OPSEC) — 20A–20E.

| Change | Location | Description |
|---|---|---|
| 20A | §5 after `BusinessSector vs criminal_sector` | Added `### VisibilityScope — Information Visibility for All Actors [V1]` section. Defines `VisibilityScope` enum (5 values: `public_info`, `industry`, `institutional`, `internal`, `concealed`). Establishes OPSEC as universal — all actors manage information exposure; criminal path applies `concealed` scope, not a separate system. Documents which observer types read which scope levels (LE, regulators, journalists, rival actors, player). Provides default-value table by context. Notes that scope is set per-activity and falls back to `NPCBusiness.default_activity_scope`. |
| 20B | §5 `NPCBusiness` struct | Added `default_activity_scope: VisibilityScope` field. Legitimate businesses default `institutional`; criminal businesses (`criminal_sector = true`) default `concealed`. Comment notes individual activities may override (e.g. a criminal business making a `public_info` charitable donation as cover). |
| 20C | §3 Province-Parallel step classification table | Renamed `12 (OPSEC)` → `12 (facility signals)` to match Session 13 rename of `FacilityOPSEC` → `FacilitySignals`. |
| 20D | §3 Design Principle: Event-Driven Processing | Updated "Investigator meter updates: triggered when OPSEC score changes" → "triggered when facility signal composite changes (any facility, criminal or legitimate)". Reflects universal facility signals — legitimate facility regulatory scrutiny updates use same trigger path. |
| 20E | Header + companion docs | Title updated to v20. Companion GDD updated to v1.4. Session 20 appended to pass status line. |

### New Structs / Enums Added

| Enum | Section | Notes |
|---|---|---|
| `VisibilityScope` | §5 | New; 5 values; universal to all actor types |

### NPCBusiness Fields Added

- `default_activity_scope: VisibilityScope` — fallback scope for activities without an activity-level override; `institutional` for legitimate, `concealed` for criminal

### Header Updated

- Title updated to v20, Pass 3 Session 20 (Economy Consolidation and Universal OPSEC) complete.
- Companion GDD updated to v1.4.

---

## Change Log — Pass 3, Session 21 (Missing Struct Definitions and Blocker Fixes)

Pass 3 Session 21 (Missing Struct Definitions and Blocker Fixes) — 21A–21D.

| Change | Location | Description |
|---|---|---|
| 21A | §4 before NPC Core Data Structure | Added `### KnowledgeMap [V1]` section. Defines `KnowledgeType` enum (4 values: `evidence_token`, `relationship`, `activity`, `identity_link`). Defines `KnowledgeEntry` struct (7 fields: `subject_id`, `secondary_subject_id`, `type`, `confidence`, `acquired_at_tick`, `source_npc_id`, `original_scope`). Defines `KnowledgeMap` as `std::vector<KnowledgeEntry>`. Documents propagation rule (instantaneous via communication; not transit-delayed). Adds 5 named constants to `simulation_config.json → knowledge`. Closes MISSING SPEC: `KnowledgeMap` was referenced in `NPC.known_evidence` and `NPC.known_relationships` without definition. |
| 21B | §5 before Supply Chain Propagation | Added `### Consumer Demand Model [V1]` section. Defines `BuyerType` enum (4 values: `price_sensitive`, `quality_seeker`, `brand_loyal`, `necessity_buyer`). Specifies `demand_contribution(npc, g)` formula with `income_factor`, `price_factor`, `quality_factor` components. Documents NPC cohort contribution to `demand_buffer`. Maps `BuyerType` to elasticity modifiers and quality weights. Adds 3 named constants to `simulation_config.json → demand`. Per-good parameters (`base_consumer_demand_units`, `income_elasticity`, `price_elasticity`, `quality_weight`) added to goods data file. Closes MISSING SPEC: consumer demand model feeding `RegionalMarket.demand_buffer`. |
| 21C | Header | Title updated to v21. Companion GDD updated to v1.5. Session 21 appended to pass status line. |
| 21D | — | Note: `InfluenceNetwork` is intentionally NOT a struct. §13 already states it is a derived layer over existing data (Relationship.trust, Relationship.fear, ObligationNode, movement_follower_count). `InfluenceNetworkHealth` (§13) is the only stored summary. No action required. Note: NPC business decision matrix (§5) is fully specified for all 4 profiles. Both audit flags closed as false positives. |

### New Structs / Enums Added

| Item | Section | Notes |
|---|---|---|
| `KnowledgeType` | §4 | New enum; 4 values |
| `KnowledgeEntry` | §4 | New struct; 7 fields |
| `KnowledgeMap` | §4 | `std::vector<KnowledgeEntry>` type alias |
| `BuyerType` | §5 | New enum; 4 values |

### Named Constants Added

| Constant | Config file | Default |
|---|---|---|
| `knowledge.confidence_decay_rate` | `simulation_config.json` | 0.0005 |
| `knowledge.min_confidence_threshold` | `simulation_config.json` | 0.10 |
| `knowledge.direct_observation_confidence` | `simulation_config.json` | 1.00 |
| `knowledge.trusted_source_confidence_scale` | `simulation_config.json` | 0.80 |
| `knowledge.rumour_confidence_scale` | `simulation_config.json` | 0.35 |
| `demand.reference_income` | `simulation_config.json` | 3000.0 |
| `demand.max_income_factor` | `simulation_config.json` | 5.0 |
| `demand.min_price_factor` | `simulation_config.json` | 0.05 |

### Per-Good Data File Fields Added

- `base_consumer_demand_units: float` — units demanded per tick at reference price and income
- `income_elasticity: float` — positive = normal good; negative = inferior good
- `price_elasticity: float` — negative; magnitude governs demand sensitivity to price change
- `quality_weight: float` — 0.0–1.0; how much quality premium affects this good's demand

### Header Updated

- Title updated to v21, Pass 3 Session 21 (Missing Struct Definitions and Blocker Fixes) complete.
- Companion GDD updated to v1.5.

---

## Change Log — Pass 3, Session 22 (WorldGen V1 Scope: H3Index, ProvinceLink, WorldGenParameters)

Pass 3 Session 22 (WorldGen V1 Scope Promotion) — 22A–22E.

| Change | Location | Description |
|---|---|---|
| 22A | §12 before ResourceDeposit | Added `H3Index` type alias (`uint64_t`). Canonical spatial identifier for H3 res-4 province cells and res-9 facility placement. O(1) parent/child/neighbor computation from index; no additional storage. Named constants `h3_province_resolution = 4` and `h3_facility_resolution = 9` added to `simulation_config.json → world`. |
| 22B | §12 before ResourceDeposit | Added `LinkType` enum (3 values: `Land`, `Maritime`, `River`). Classifies the physical nature of a province-to-province connection. |
| 22C | §12 before ResourceDeposit | Added `ProvinceLink` struct (5 fields: `neighbor_h3`, `type`, `shared_border_km`, `transit_terrain_cost`, `infrastructure_bonus`). Replaces the bare `uint32_t` list with rich adjacency descriptors that encode connection cost. |
| 22D | §12 before ResourceDeposit | Added `WorldGenParameters` struct (7 fields: `seed`, `target_province_count`, `resource_richness`, `climate_volatility`, `corruption_baseline`, `criminal_activity_baseline`, `output_world_file`). Parameters for the 11-stage procedural pipeline (WorldGen v0.17). Not present at runtime — consumed once at new-game world generation. Named constants `worldgen.v1_target_province_count = 6`, `worldgen.resource_richness_min/max`, `worldgen.climate_volatility_min/max` added to `simulation_config.json → worldgen`. |
| 22E | §12 Province struct | Added `H3Index h3_index` before `uint32_t id`. Replaced `std::vector<uint32_t> adjacent_province_ids` with `std::vector<ProvinceLink> links`. Updated karst comment version ref: WorldGen v0.16 → v0.17. Updated header to v22; companion GDD updated to v1.6. |

### New Structs / Enums Added

| Item | Section | Notes |
|---|---|---|
| `H3Index` | §12 | `uint64_t` type alias; H3 spatial index |
| `LinkType` | §12 | New enum; 3 values |
| `ProvinceLink` | §12 | New struct; 5 fields |
| `WorldGenParameters` | §12 | New struct; 7 fields |

### Province Struct Fields Changed

| Field | Change |
|---|---|
| `H3Index h3_index` | Added before `uint32_t id`; canonical external identifier |
| `adjacent_province_ids` | Replaced with `std::vector<ProvinceLink> links` |

### Named Constants Added

| Constant | Config file | Default |
|---|---|---|
| `world.h3_province_resolution` | `simulation_config.json` | 4 |
| `world.h3_facility_resolution` | `simulation_config.json` | 9 |
| `worldgen.v1_target_province_count` | `simulation_config.json` | 6 |
| `worldgen.resource_richness_min` | `simulation_config.json` | 0.5 |
| `worldgen.resource_richness_max` | `simulation_config.json` | 2.0 |
| `worldgen.climate_volatility_min` | `simulation_config.json` | 0.5 |
| `worldgen.climate_volatility_max` | `simulation_config.json` | 2.0 |

### Header Updated

- Title updated to v22, Pass 3 Session 22 (WorldGen V1 Scope) complete.
- Companion GDD updated to v1.6.

---

## Change Log — Pass 3, Session 23 (V1 Completeness Pass — 16-Issue Audit Resolution)

Pass 3 Session 23 — resolves all 16 open audit issues. FTL/GDD consistency fixes (I-01, I-03, I-04), precision clarifications (P-01, P-02, P-05), 7 new TDD spec sections.

| Change | Location | Description |
|---|---|---|
| 23A | §15 (new) | Legal Process State Machine: `LegalCaseStage` (10 values), `LegalCase` (14 fields), transition rules, trial probability formula, 13 constants. Closes B-09. |
| 23B | §17 (new) | Financial Markets: §17.1 Stock Market — `StockListing`, `StockPortfolio`, price model (earnings + sentiment + noise), IPO, dividends, 8 constants. §17.2 Bond Market — `GovernmentBond`, `BondHolding`, credit/yield/price model, fiscal cascade, auction, 7 constants. Closes B-17, M-06. |
| 23C | §19.5 (new) | `RandomEventType` enum (4 values), `RandomEventTemplate` (11 fields), `ActiveRandomEvent` (7 fields). Closes B-15 struct gap. |
| 23D | §26 (new) | Healthcare System: `HealthcareProfile` (4 fields → Province), NPC health float + treatment algorithm, labour participation formula, `FacilityType::hospital`, 9 constants. Closes M-02. |
| 23E | §27 (new) | Addiction State Machine: `AddictionStage` (7 values), `AddictionState` (7 fields → NPC), full transition rules, `addiction_rate` → RegionConditions, 16 constants. Closes M-04. |
| 23F | §28 (new) | Currency and Exchange Rates: `CurrencyRecord` (8 fields → WorldState.currencies), macro-anchored rate model, peg-break → currency_crisis event, cross-currency conversion, USD threshold conversion. 7 constants. Closes M-05. |
| 23G | §29 (new) | Media + Movement: `OutletType` (4), `MediaOutlet` (8), `StoryTone` (4), `Story` (9), propagation at tick step 16, editorial filter. Movement follower accumulation (tick 22) + pressure effects (tick 23). 13 constants. Closes S-02, S-04. |
| 23H | Header | Title → v23; companion GDD → v1.7; Session 23 appended to pass status line. |

### New Structs / Enums Added

`LegalCaseStage`, `LegalCase`, `StockListing`, `StockPortfolio`, `GovernmentBond`, `BondHolding`, `RandomEventType`, `RandomEventTemplate`, `ActiveRandomEvent`, `HealthcareProfile`, `AddictionStage`, `AddictionState`, `CurrencyRecord`, `OutletType`, `MediaOutlet`, `StoryTone`, `Story` — 17 new items across 7 sections.

### Header Updated

- Title updated to v23, Pass 3 Session 23 complete.
- Companion GDD updated to v1.7.

---

## 31. Multi-Level Government Budget System [V1]

Government in EconLife operates at three tiers, each with its own budget, revenue sources, and expenditure authorities. The player's budget access is determined strictly by which office they currently hold — holding no office means no budget authority. This mirrors real government structure: a mayor controls the city budget, a provincial governor controls the provincial budget, the head of state controls the national budget. These budgets interact through intergovernmental transfers (grants flowing down from higher levels) and shared tax bases (where multiple levels collect from the same economic activity).

### 31.1 — Budget Hierarchy [V1]

```
Nation
  └── Province(s)            (1–N per nation; V1: 6 provinces)
        └── City/District(s) (1–N per province; modeled as sub-province units)
```

Each level has an independent `GovernmentBudget`. Budget data lives on the entities that already exist in WorldState:

```cpp
// FIELD ADDITION to Nation:
GovernmentBudget national_budget;

// FIELD ADDITION to Province:
GovernmentBudget provincial_budget;
std::vector<CityBudget> city_budgets;   // one per city/district within the province
                                         // V1: 1–3 cities per province
```

### 31.2 — GovernmentBudget [V1]

```cpp
enum class GovernmentLevel : uint8_t {
    city     = 0,  // Mayor, City Council
    province = 1,  // Regional Governor, Provincial Legislature
    national = 2,  // Head of State, National Legislature / Cabinet
};

enum class SpendingCategory : uint8_t {
    public_services      = 0,  // health, education, sanitation, emergency services
    infrastructure       = 1,  // roads, bridges, utilities, transit
    law_enforcement      = 2,  // police, courts, corrections
    social_programs      = 3,  // welfare, housing assistance, unemployment
    public_sector_wages  = 4,  // salaries for all government employees this level
    debt_servicing       = 5,  // interest on outstanding bonds/debt
    intergovernmental    = 6,  // transfers to lower levels (nation → province → city)
    discretionary        = 7,  // unallocated; player-directed special projects,
                                // grants, subsidies; highest corruption surface
};

struct GovernmentBudget {
    GovernmentLevel level;
    uint32_t        jurisdiction_id;  // province_id for provincial; 0 for national;
                                       // city_id for city-level

    // --- Revenue (per simulated quarter = 90 ticks) ---
    float revenue_own_taxes;          // collected from tax base this level controls
                                       // (see tax collection rules §31.3)
    float revenue_transfers_in;       // received from higher level this quarter
    float revenue_other;              // fees, fines, asset sales, bond proceeds

    float total_revenue;              // sum of above; recomputed at quarter end

    // --- Expenditure (per quarter) ---
    std::map<SpendingCategory, float> spending_allocations;
                                       // player/NPC sets target allocations;
                                       // actual spending may be less if revenue insufficient
    std::map<SpendingCategory, float> spending_actual;
                                       // what was actually paid; may differ from
                                       // allocation if cash constrained

    float total_expenditure;          // sum of spending_actual; recomputed at quarter end

    // --- Balance ---
    float surplus_deficit;            // total_revenue - total_expenditure; positive = surplus
    float accumulated_debt;           // cumulative deficit carried forward; serviced via
                                       // debt_servicing spending category

    // --- Cash position ---
    float cash;                        // liquid balance; revenue credited here;
                                       // expenditures debited here

    // --- Fiscal health indicators (derived; updated quarterly) ---
    float debt_to_revenue_ratio;      // accumulated_debt / total_revenue;
                                       // > config.fiscal.debt_warning_ratio → fiscal stress event
    float deficit_to_revenue_ratio;   // surplus_deficit / total_revenue (negative = deficit);
                                       // feeds bond market credit_rating at national level (§17.2)
};

struct CityBudget {
    uint32_t city_id;
    std::string city_name;
    GovernmentBudget budget;
    uint32_t governing_office_id;     // references PoliticalOffice (mayor or city_council)
};
```

### 31.3 — Tax Collection by Level [V1]

Each level of government collects from a distinct but overlapping tax base. Collection runs quarterly (every 90 ticks) via `DeferredWorkQueue` (`WorkType::tax_collection_quarterly`).

**National level tax collection:**

```
FOR each province in nation.provinces:
    FOR each npc_business in province WHERE criminal_sector == false:
        corporate_tax = npc_business.revenue_per_tick
                        × TICKS_PER_QUARTER
                        × nation.corporate_tax_rate
                        // V1 simplification: tax on gross revenue, not net profit.
                        // Net profit taxable base is expansion scope (requires full P&L model).
        national_budget.cash += corporate_tax
        national_budget.revenue_own_taxes += corporate_tax

    // Income tax: collected from PopulationCohort wage income
    FOR each cohort in province.cohort_stats.cohorts:
        wage_base = cohort.median_income × TICKS_PER_QUARTER × cohort.size × cohort.employment_rate
        income_tax = wage_base × nation.income_tax_rate_top_bracket
                     × cohort_tax_rate_modifier(cohort.group)
                     // cohort_tax_rate_modifier: working_class = 0.4 (lower effective rate);
                     //   professional = 0.85; corporate = 1.0; criminal_adjacent = 0.1
                     //   (most income unreported). Stored as per-group constants in
                     //   simulation_config.json → fiscal.
        national_budget.cash += income_tax
        national_budget.revenue_own_taxes += income_tax
```

**Provincial level tax collection:**

```
FOR each npc_business in province WHERE criminal_sector == false:
    // Provincial business tax (rate set by provincial policy; default from scenario file)
    provincial_tax = npc_business.revenue_per_tick
                     × TICKS_PER_QUARTER
                     × province.provincial_business_tax_rate

    provincial_budget.cash += provincial_tax
    provincial_budget.revenue_own_taxes += provincial_tax

// Property tax: applied to PropertyListing records in province (§BLOCKER-B, when specced)
// Placeholder: property_tax_revenue seeded from province base value at world load until
// real estate market is specced.
provincial_budget.revenue_own_taxes += province.property_tax_revenue_estimate
provincial_budget.cash               += province.property_tax_revenue_estimate
```

**City level tax collection:**

```
// Local sales tax / business license fees
// V1 simplified: city_tax_revenue = provincial_budget.revenue_own_taxes
//               × config.fiscal.city_revenue_fraction_of_province
// (Approximate until city-level property and sales tax are fully modelled)
city_budget.revenue_own_taxes = province.provincial_budget.revenue_own_taxes
                                 × config.fiscal.city_revenue_fraction_of_province
city_budget.cash += city_budget.revenue_own_taxes
```

**FIELD ADDITIONS to Province:**
```cpp
float provincial_business_tax_rate;  // set by scenario file; modifiable by provincial legislation
float property_tax_revenue_estimate; // seeded at world gen; placeholder until real estate market specced
```

### 31.4 — Intergovernmental Transfers [V1]

Higher-level governments fund lower levels through quarterly transfers. Transfer amounts are set by the higher-level government's spending allocation under `SpendingCategory::intergovernmental`.

```
// National → Provincial:
FOR each province:
    transfer = national_budget.spending_allocations[intergovernmental] / num_provinces
               × config.fiscal.national_to_province_distribution_modifier(province)
               // distribution_modifier: equal split by default; adjustable by head of state
               // as a political tool (favoring allied provinces is classic pork barrel)
    national_budget.cash     -= transfer
    provincial_budget.cash   += transfer
    provincial_budget.revenue_transfers_in += transfer

// Provincial → City:
FOR each city in province:
    transfer = provincial_budget.spending_allocations[intergovernmental] / num_cities
    provincial_budget.cash -= transfer
    city_budget.cash       += transfer
    city_budget.revenue_transfers_in += transfer
```

**Political consequence:** Unequal intergovernmental distribution generates `employment_negative` or `facility_quality` memory entries in disadvantaged provinces/cities, feeding `CommunityState.grievance_level`. NPC politicians in disadvantaged jurisdictions gain motivation to oppose the player at the next election.

### 31.5 — Spending Consequences [V1]

Budget spending in each category drives province and city condition fields. Underspending relative to the prior year creates negative consequences; overspending creates positive ones. Consequences are emergent (via NPC memory entries), not scripted.

| SpendingCategory | Province/City field driven | Effect of underspend |
|---|---|---|
| `public_services` | `conditions.stability_score`, `RegionDemographics` health proxy | Stability declines; health events more likely |
| `infrastructure` | `infrastructure_rating` | Rating decays 0.02/quarter if no investment |
| `law_enforcement` | `conditions.crime_rate`, InvestigatorMeter capacity | Crime rate rises; LE response capacity shrinks |
| `social_programs` | `conditions.inequality_index`, cohort `employment_rate` | Inequality worsens; working_class grievance rises |
| `public_sector_wages` | NPC satisfaction for government employee NPCs | Government worker NPCs gain `employment_negative` memories; service quality degrades |
| `debt_servicing` | `accumulated_debt`, credit rating | Missed servicing → debt compounds; national credit rating falls |
| `discretionary` | Variable; player-directed | Corruption surface: unallocated budget creates evidence token risk if diverted |

**Infrastructure decay formula:**
```
province.infrastructure_rating -= config.fiscal.infrastructure_decay_per_quarter
IF spending_actual[infrastructure] > 0:
    province.infrastructure_rating += spending_actual[infrastructure]
                                       / config.fiscal.infrastructure_investment_scale
province.infrastructure_rating = clamp(province.infrastructure_rating, 0.0f, 1.0f)
```

### 31.6 — Player Budget Authority by Office [V1]

The player interacts with budgets only through a held `PoliticalOffice`. Budget access is strict — the player cannot view or modify budgets for jurisdictions outside their office.

| PoliticalOfficeType | Budget access | Revenue levers | Expenditure authority |
|---|---|---|---|
| `city_council` | Read `CityBudget` only | None (rates set at province/nation) | Can propose spending reallocation within existing allocation; requires council vote |
| `mayor` | Full `CityBudget` | Set local fee schedules (minor) | Full discretionary authority within city budget; no vote required for reallocation |
| `regional_governor` | Full `provincial_budget` + read `CityBudget`s | Set `provincial_business_tax_rate`; set intergovernmental transfer amounts to cities | Full provincial spending authority; can adjust city allocations via transfer amounts |
| `national_legislator` | Read `national_budget` | Propose tax legislation (goes through legislative cycle §14) | Vote on budget legislation; cannot unilaterally direct spending |
| `cabinet_minister` | Full `national_budget` for their ministry's categories | Set ministry-level spending within allocated envelope | Full authority within ministry categories; cross-category changes require cabinet |
| `head_of_state` | Full `national_budget` | Set `corporate_tax_rate`, `income_tax_rate_top_bracket`; propose tax legislation | Full expenditure authority across all national categories; intergovernmental transfers to provinces |

**Budget manipulation as corruption surface:** A `regional_governor` who diverts provincial `discretionary` funds to a privately-owned contractor generates a `financial` evidence token when the spending record is auditable. Evidence token `actionability` = `spending_amount / config.fiscal.corruption_evidence_threshold` — small diversions are low actionability; large ones are immediately actionable. Auditable by regulators and journalists with `institutional` scope access.

### 31.7 — Fiscal Stress and Crisis Events [V1]

Fiscal thresholds trigger consequence queue entries that feed into the random events and community response systems:

```
QUARTERLY FISCAL CHECK (runs after tax_collection_quarterly):

FOR each GovernmentBudget in [national, all provincial, all city]:
    // Debt stress
    IF budget.debt_to_revenue_ratio > config.fiscal.debt_warning_ratio:
        schedule_consequence(type: fiscal_pressure_warning,
                             jurisdiction_id: budget.jurisdiction_id,
                             severity: budget.debt_to_revenue_ratio
                                       / config.fiscal.debt_warning_ratio)

    IF budget.debt_to_revenue_ratio > config.fiscal.debt_crisis_ratio:
        schedule_consequence(type: fiscal_crisis,
                             effects: [infrastructure_decay_accelerated,
                                       social_program_cuts_forced,
                                       grievance_spike])

    // Cash insolvency
    IF budget.cash < 0:
        // Cannot service debt or pay public sector wages this quarter
        // Forced spending cuts to debt_servicing and public_sector_wages
        // NPC government employees gain employment_negative memories
        schedule_consequence(type: government_insolvency,
                             jurisdiction_id: budget.jurisdiction_id)
```

**Named constants (`simulation_config.json → fiscal`):**

| Constant | Default | Effect |
|---|---|---|
| `debt_warning_ratio` | 2.0 | Debt = 2× annual revenue → fiscal_pressure_warning |
| `debt_crisis_ratio` | 4.0 | Debt = 4× annual revenue → fiscal_crisis consequence |
| `infrastructure_decay_per_quarter` | 0.01 | Infrastructure rating falls 0.01/quarter without investment |
| `infrastructure_investment_scale` | 1,000,000 | Currency units needed per 0.01 rating improvement |
| `city_revenue_fraction_of_province` | 0.25 | City collects ~25% of what province collects |
| `national_to_province_distribution_modifier` | 1.0 (equal) | Default equal split; adjustable by head of state |
| `corruption_evidence_threshold` | 500,000 | Discretionary diversion above this → high-actionability evidence |
| `cohort_tax_rate_modifier.working_class` | 0.40 | Effective income tax rate relative to top bracket |
| `cohort_tax_rate_modifier.professional` | 0.85 | |
| `cohort_tax_rate_modifier.corporate` | 1.00 | |
| `cohort_tax_rate_modifier.criminal_adjacent` | 0.10 | Most income unreported |

---

## 32. Money Laundering System [V1]

Criminal revenue is cash held in the informal market layer — it has no plausible legitimate origin. Laundering converts it into formal market cash with a defensible paper trail. Every method produces the conversion at a different speed, capacity, and evidence generation rate. The method choice is itself an OPSEC decision: faster laundering generates more signal; slower laundering generates less but creates opportunity cost.

**Architectural framing:** Laundering is a ledger transfer between market layers. The simulation treats the informal market and formal market as separate cash pools. A `LaunderingOperation` schedules a deferred transfer: `criminal_org.cash -= amount; player.wealth += amount_clean` (or `npc_business.cash += amount_clean` for the front business). The intermediate steps — the shell company chain, the bank deposits, the real estate transaction — are modelled as the evidence generation profile of the method, not as a full transaction simulation. The evidence is what matters; the accounting is the consequence.

### 32.1 — EvidenceType Enum [V1]

`EvidenceType` is referenced by `EvidenceToken` but has not been formally declared. Declaring it here; it is used across all evidence systems.

```cpp
enum class EvidenceType : uint8_t {
    financial     = 0,  // Wire transfers, SAR filings, unusual cash flows,
                         // shell company records, transaction pattern anomalies.
                         // Primary output of money laundering operations.

    testimonial   = 1,  // Witness accounts: what an NPC saw, heard, or was told.
                         // Generated by NPC memory entries reaching actionability.
                         // Degrades fastest when witness is discredited.

    documentary   = 2,  // Contracts, emails, invoices that should not exist.
                         // Generated by criminal business operations, obligation
                         // records, and paper trails from non-cash transactions.

    physical      = 3,  // Meetings observed, locations visited, goods intercepted.
                         // Generated by facility signals and surveillance events.
                         // Does not degrade with holder credibility — it is the
                         // physical record, not a person's account of it.
};
```

### 32.2 — Laundering Method Enum [V1]

```cpp
enum class LaunderingMethod : uint8_t {
    structuring         = 0,  // Smurfing: cash deposited in sub-threshold amounts
                               // across multiple accounts. Zero prerequisites beyond
                               // access to a bank. Slowest capacity. FIU pattern
                               // analysis is specifically trained for this signature.

    shell_company_chain = 1,  // Cash moved through a series of NPCBusiness records
                               // with criminal_sector = false. Each node in the chain
                               // is an NPC who can be flipped. Fastest capacity at
                               // scale; evidence compounds with chain length.

    real_estate         = 2,  // Cash purchase through intermediary; sale to
                               // player-controlled entity at inflated price.
                               // Requires: at least one PropertyListing in target
                               // province (§BLOCKER-B placeholder: requires real
                               // estate market; operation queued but cannot complete
                               // until PropertyListing struct is specced).
                               // Price inflation effect: elevates regional property
                               // prices by launder_amount / province property stock value.

    trade_invoice       = 3,  // Over/under-invoicing on legitimate trade transactions.
                               // Requires: player owns or controls an NPCBusiness in
                               // import/export sector AND at least one trade counterpart
                               // NPC with trust > 0.5.
                               // Generates documentary evidence (invoice discrepancy).

    crypto_mixing       = 4,  // Pseudonymous blockchain route through mixing services.
                               // Requires: access to a MixerNode (see §32.6).
                               // Partial anonymization — sufficiently resourced FIU
                               // investigators can still trace. Evidence generation
                               // rate scales with investigator_skill, not flat.

    cash_commingling    = 5,  // Blending criminal proceeds with legitimate high-cash
                               // revenue in a player-controlled NPCBusiness.
                               // Requires: owner_id == player_id on the front business;
                               // BusinessSector in {retail, hospitality, entertainment,
                               // construction} (high natural cash volume).
                               // Employees and suppliers are potential witnesses to
                               // volume/traffic discrepancy.
};
```

### 32.3 — LaunderingOperation [V1]

```cpp
struct LaunderingOperation {
    uint32_t id;
    uint32_t actor_id;              // player_id or criminal_org_id initiating the operation
    LaunderingMethod method;

    float dirty_amount;             // total informal-market cash to be laundered
    float laundered_so_far;         // accumulated clean cash transferred; updated each tick
    float launder_rate_per_tick;    // currency units per tick (derived from method + infrastructure)
    float conversion_loss_rate;     // fraction of dirty_amount lost to fees and friction
                                    // (see §32.4 per-method profiles)

    uint32_t started_tick;
    uint32_t estimated_complete_tick; // started_tick + ceil(dirty_amount / launder_rate_per_tick)

    // --- Destination ---
    uint32_t destination_business_id; // NPCBusiness receiving clean cash (front company);
                                       // 0 if destination is directly player.wealth
                                       // (structuring and crypto methods may credit directly)

    // --- Infrastructure ---
    std::vector<uint32_t> shell_chain_business_ids;
                                    // Ordered list of NPCBusiness ids forming the chain;
                                    // empty for methods that don't use chains.
                                    // Each id must have: criminal_sector == false,
                                    //   owner_id == player_id or npc_id in org.
                                    // Chain length: 1–config.laundering.max_chain_depth.

    uint32_t mixer_node_id;         // MixerNode id for crypto_mixing method; 0 otherwise

    // --- Evidence tracking ---
    float evidence_generated_total; // running sum of evidence tokens generated this operation
    std::vector<uint32_t> evidence_token_ids; // tokens generated by this operation

    // --- State ---
    bool paused;                    // operation suspended by player command or LE event
    bool completed;
};
```

**Tick integration:** `LaunderingOperation` processing runs at tick step 6 (Evidence and Consequence Systems). Each active operation that is not paused and not completed:

```
FOR each active LaunderingOperation op:
    transfer_this_tick = min(op.launder_rate_per_tick,
                             op.dirty_amount - op.laundered_so_far)
    clean_amount = transfer_this_tick × (1.0 - op.conversion_loss_rate)

    // Debit informal cash
    IF op.actor_id == player_id:
        // player.informal_cash -= transfer_this_tick (see §32.7 PlayerCharacter field addition)
    ELSE:
        criminal_org[op.actor_id].cash -= transfer_this_tick

    // Credit clean cash to destination
    IF op.destination_business_id != 0:
        npc_businesses[op.destination_business_id].cash += clean_amount
    ELSE:
        player.wealth += clean_amount

    op.laundered_so_far += transfer_this_tick

    // Evidence generation (see §32.4 per-method rate)
    generate_laundering_evidence(op, transfer_this_tick)

    IF op.laundered_so_far >= op.dirty_amount:
        op.completed = true
```

### 32.4 — Per-Method Evidence and Capacity Profiles [V1]

Each method has a fixed profile for laundering rate, conversion loss, and evidence generation. Evidence generation is expressed as a probability per unit laundered of producing a `financial` EvidenceToken directed at the operating actor.

| Method | `launder_rate_per_tick` | `conversion_loss_rate` | Evidence tokens/tick | FIU pattern flag | Prerequisites |
|---|---|---|---|---|---|
| `structuring` | `config.laundering.rate_structuring` (default 500) | 0.02 | 1 per `config.laundering.structuring_token_interval` ticks | High — pattern match on sub-threshold deposit cadence | Bank access only |
| `shell_company_chain` | `config.laundering.rate_chain_per_node × chain_length` | 0.05 | 1 per chain_node per 30 ticks | Medium — corporate record discrepancy | ≥1 shell company NPC business |
| `real_estate` | Single transaction (not per-tick) | 0.08 | 1 on transaction (high actionability) | Medium-high | PropertyListing; BLOCKER-B gating applies |
| `trade_invoice` | `config.laundering.rate_trade` (default 2,000) | 0.06 | 1 per 20 ticks (documentary type) | Medium — invoice audit | Import/export business + counterpart NPC |
| `crypto_mixing` | `config.laundering.rate_crypto` (default 3,000) | 0.12 | Investigator-skill-scaled (see §32.6) | Low base, scales with FIU crypto capability | MixerNode access |
| `cash_commingling` | `min(front_business.revenue_per_tick × config.laundering.commingle_capacity_fraction, config.laundering.rate_commingle_max)` | 0.03 | 1 per 15 ticks (testimonial risk) | Low — blended with legitimate revenue | High-cash-flow business |

**Shell chain rate note:** Each chain node multiplies throughput but also multiplies evidence generation linearly. A 3-node chain launders 3× faster but generates 3× the evidence tokens. Optimal chain depth is not infinite — there is a marginal evidence cost to adding nodes.

**Cash commingling capacity note:** Commingling capacity is bounded by the front business's legitimate revenue. Injecting more criminal cash than the business's natural cash flow can plausibly explain is detectable — the discrepancy between reported revenue and observable traffic (customer counts, supplier invoice volume) is a physical evidence signal that feeds `RegulatorScrutinyMeter` fill rate for that business.

### 32.5 — Shell Company Chain [V1]

Shell companies are `NPCBusiness` records with `criminal_sector = false` and `owner_id` pointing to the criminal actor (player or org). They appear legitimate in formal market records. The laundering system represents the chain as an ordered list of `NPCBusiness` ids; no separate struct is needed — the chain's structure is in `LaunderingOperation.shell_chain_business_ids`.

**Chain constraints:**

```
// At operation start, validate:
FOR each business_id in op.shell_chain_business_ids:
    assert npc_businesses[business_id].criminal_sector == false
    assert npc_businesses[business_id].owner_id == op.actor_id
                 OR owner is npc in criminal_org[op.actor_id].member_npc_ids

assert len(op.shell_chain_business_ids) <= config.laundering.max_chain_depth
// Cross-province chains require at least one ProvinceLink between consecutive chain members' provinces.
// Cross-nation chains: [EX] — V1 chains are intra-nation only.
```

**Chain NPCs as informant risk:** Each shell company has a named NPC who fronts it (the company's `owner_id` NPC, or a hired nominee — an NPC with `role = accountant` or `role = lawyer` employed by the company). Under arrest pressure (§15 Legal Process), each shell company NPC evaluates flip probability (BLOCKER-F informant system). A flipped shell company NPC can expose: the company's role in the chain, the connected criminal actor, and the transaction records — generating a high-actionability `documentary` EvidenceToken against the criminal actor.

**Creating a shell company:** Player command → acquires or creates an `NPCBusiness` with sector = `finance` or `consulting` (low natural scrutiny sectors) → sets `owner_id` = player_id → adds to a `LaunderingOperation`'s chain. Creation cost: `config.laundering.shell_company_formation_cost`. Formation generates one `documentary` evidence token at `VisibilityScope::institutional` (corporate registry filing).

### 32.6 — Crypto Mixing [V1]

```cpp
struct MixerNode {
    uint32_t id;
    uint32_t province_id;           // physical jurisdiction of the mixing service
    float    capacity_per_tick;     // maximum currency units processed per tick
    float    fee_rate;              // fraction of amount taken as fee (additional to
                                    // conversion_loss_rate from §32.4)
    float    traceability;          // 0.0 (fully opaque) to 1.0 (trivially traceable)
                                    // Starts at config.laundering.mixer_base_traceability.
                                    // Increases if: FIU has opened investigation into
                                    // this mixer node (traceability += 0.30 on formal
                                    // inquiry); decreases if node is upgraded (player
                                    // action: invest in mixer infrastructure).
    uint32_t operator_npc_id;       // NPC who operates the mixer; flip risk under pressure
    bool     fiu_flagged;           // true if FIU has formally linked this node to
                                    // suspicious activity; use generates evidence immediately
};
```

**Evidence generation for crypto:** Unlike flat-rate methods, crypto evidence generation scales with investigator skill:

```
evidence_probability_per_tick = op.launder_rate_per_tick
    × mixer.traceability
    × max(le_npc.skill(Intelligence) for le_npc with InvestigatorMeter targeting op.actor_id)
    / config.laundering.crypto_evidence_skill_divisor

IF random(0.0, 1.0) < evidence_probability_per_tick:
    generate EvidenceToken(type: financial, actionability: mixer.traceability × 0.8)
```

**Implication:** Against an unskilled LE NPC, crypto is nearly undetectable. Against a specialized financial crimes unit (high `Intelligence` skill), it generates evidence at rates comparable to structuring. The player's OPSEC value from crypto degrades as investigators level up — a realistic dynamic.

**MixerNode storage:** `std::vector<MixerNode> mixer_nodes` added to `WorldState` (field addition).

### 32.7 — FIU Detection System [V1]

The Financial Intelligence Unit runs pattern analysis each month, separate from per-operation evidence generation. The FIU is not a separate NPC — it is a system that feeds evidence tokens to existing LE NPCs with `role == banker` or `role == law_enforcement` who have financial crimes jurisdiction.

```cpp
struct FIUPatternResult {
    uint32_t target_actor_id;
    float    suspicion_score;       // 0.0–1.0; derived from pattern match weight
    uint32_t evidence_token_id;     // generated if suspicion_score > threshold
    LaunderingMethod inferred_method; // FIU's best guess at the method
};
```

**Monthly FIU pattern analysis** (`WorkType::fiu_monthly_analysis`):

```
FOR each province:
    // Collect all financial transactions this month:
    //   - NPCBusiness cash changes above threshold
    //   - player.wealth changes without linked business revenue
    //   - deposits flagged by banking NPCs (role == banker)

    // Pattern 1: Structuring detection
    IF count_of_sub_threshold_deposits(actor, province, last_30_ticks)
       > config.laundering.structuring_deposit_count_threshold:
        score = count / config.laundering.structuring_deposit_count_threshold
        generate FIUPatternResult(suspicion_score = min(score × 0.4, 1.0),
                                   method = structuring)

    // Pattern 2: Shell company round-trip
    FOR each NPCBusiness chain reachable from same owner_id:
        IF cash_in ≈ cash_out across all nodes within last_90_ticks:
            // Money entered one end and exited the other — round-trip signal
            generate FIUPatternResult(suspicion_score = 0.6, method = shell_company_chain)

    // Pattern 3: Revenue/traffic discrepancy (commingling)
    FOR each NPCBusiness with high cash flow AND criminal_sector == false:
        IF reported_revenue > config.laundering.traffic_revenue_ratio_threshold
                              × observable_customer_proxy:
            // observable_customer_proxy: derived from FacilitySignals.foot_traffic_signal
            generate FIUPatternResult(suspicion_score = 0.5, method = cash_commingling)

    // Convert results above threshold to EvidenceTokens:
    FOR each result WHERE result.suspicion_score > config.laundering.fiu_token_threshold:
        token = EvidenceToken{
            type: financial,
            subject_npc_id: result.target_actor_id,
            actionability: result.suspicion_score,
            holder_npc_id: fiu_responsible_le_npc_id(province)
        }
        le_npc.investigator_meter.fill_rate += token.actionability
                                               × config.laundering.fiu_meter_fill_scale
```

**FIU countermeasures available to the player:**
- Corrupt the responsible banking NPC (`role == banker`): suppresses SAR filing; generates `obligation` and `testimonial` evidence against the player
- Use nominee directors for shell companies: raises the FIU's inferred chain length before reaching the player; buys time but not immunity
- Rotate mixer nodes: resets `fiu_flagged` flag on prior node; new node starts at base traceability

### 32.8 — CriminalOrganization Field Addition [V1]

```cpp
// FIELD ADDITION to CriminalOrganization:
std::vector<uint32_t> active_laundering_operation_ids;
    // References to LaunderingOperation records in WorldState.
    // The strategic decision matrix sets accelerate_laundering: true →
    // new LaunderingOperation created with method = shell_company_chain
    // (default NPC method; NPCs do not use crypto or real estate in V1).
    // Capacity limit: total launder_rate_per_tick across all active
    // operations cannot exceed config.laundering.org_capacity_multiplier
    // × org.cash / TICKS_PER_QUARTER.
    // NPCs do not select method strategically — they use the fastest
    // available method within their infrastructure. Player selects method
    // explicitly.
```

**WorldState field addition:**
```cpp
// FIELD ADDITION to WorldState:
std::vector<LaunderingOperation> laundering_operations;
std::vector<MixerNode>           mixer_nodes;
```

**PlayerCharacter field addition:**
```cpp
// FIELD ADDITION to PlayerCharacter:
float informal_cash;    // criminal revenue pool; denominated in local currency;
                        // not displayable as wealth until laundered.
                        // Sourced from: criminal facility production revenue,
                        // protection racket payments, drug sale proceeds.
                        // Not included in player.net_assets computation.
                        // Seized in full on arrest + asset forfeiture event.
```

### 32.9 — Named Constants [V1]

All to `simulation_config.json → laundering`:

| Constant | Default | Effect |
|---|---|---|
| `rate_structuring` | 500 | Currency units laundered per tick via structuring |
| `rate_chain_per_node` | 1,500 | Per-chain-node throughput for shell company method |
| `rate_trade` | 2,000 | Currency units per tick via trade invoice |
| `rate_crypto` | 3,000 | Currency units per tick via crypto mixing |
| `rate_commingle_max` | 5,000 | Cap on commingling rate regardless of business revenue |
| `commingle_capacity_fraction` | 0.40 | Max fraction of front business revenue usable for commingling |
| `max_chain_depth` | 5 | Maximum shell company chain length |
| `shell_company_formation_cost` | 10,000 | One-time cost to create a shell company |
| `mixer_base_traceability` | 0.15 | Starting traceability for a new MixerNode |
| `crypto_evidence_skill_divisor` | 10.0 | Scales investigator skill effect on crypto evidence rate |
| `structuring_deposit_count_threshold` | 8 | Deposits per month triggering FIU structuring flag |
| `traffic_revenue_ratio_threshold` | 3.0 | Revenue/traffic ratio above which commingling suspected |
| `fiu_token_threshold` | 0.35 | FIU suspicion score above which evidence token is generated |
| `fiu_meter_fill_scale` | 0.05 | FIU token actionability × this scale → InvestigatorMeter fill_rate additive |
| `org_capacity_multiplier` | 0.25 | NPC org laundering capacity fraction of quarterly cash |
| `structuring_token_interval` | 7 | Ticks between structuring evidence tokens |

---

## 33. Real Estate Market [V1]

Real estate is the third major asset class alongside businesses and financial instruments. It generates rental income, appreciates with provincial prosperity, serves as a laundering vehicle (§32), and is a prerequisite for the investor career path. `PropertyType` resolves audit ambiguity S-4: residential and commercial properties use the same `PropertyListing` struct distinguished by enum.

### 33.1 — PropertyType and PropertyListing [V1]

```cpp
enum class PropertyType : uint8_t {
    residential = 0,  // Apartment, house, multi-family building.
                       // Primary use: player residence (PlayerCharacter.residence_id),
                       // rental income, laundering vehicle.
                       // Price drivers: province population density, income level,
                       // criminal_dominance_index (suppresses price).

    commercial  = 1,  // Office, retail unit, warehouse.
                       // Primary use: business premises for NPCBusiness (reduces
                       // cost_per_tick by config.realestate.commercial_cost_reduction_rate
                       // when owner_id matches NPCBusiness.owner_id).
                       // Price drivers: infrastructure_rating, formal_employment_rate.

    industrial  = 2,  // Factory floor, port facility, storage yard.
                       // Primary use: facility housing for Facility records.
                       // Price drivers: energy_cost_baseline, infrastructure_rating.
};

struct PropertyListing {
    uint32_t    id;
    PropertyType type;
    uint32_t    province_id;
    uint32_t    owner_id;           // player_id, npc_id, or 0 (province-owned / market stock)
    float       asking_price;       // current list price; converges toward market_value over time
    float       market_value;       // equilibrium price derived from province conditions (see §33.2)
    float       rental_yield_rate;  // fraction of market_value paid as rent per tick
                                    // residential default: config.realestate.residential_yield_rate
                                    // commercial:          config.realestate.commercial_yield_rate
    float       rental_income_per_tick; // derived: market_value × rental_yield_rate
    bool        rented;             // true if a tenant NPC or business is currently occupying
    uint32_t    tenant_id;          // NPC or NPCBusiness id; 0 if not rented
    bool        launder_eligible;   // true if owned under a nominee or shell structure;
                                    // enables real_estate LaunderingMethod (§32.4)
    uint32_t    purchased_tick;     // tick of last ownership transfer
    float       purchase_price;     // price paid at last transfer; used for capital gain calc
};
```

### 33.2 — Market Value Formula [V1]

`market_value` is recomputed monthly per property. It is the equilibrium price the `asking_price` converges toward.

```
FUNCTION compute_market_value(listing: PropertyListing, province: Province) → float:
    base = config.realestate.base_value_by_type[listing.type]
           × province.demographics.total_population
           / config.realestate.population_value_divisor

    prosperity_multiplier = 1.0
        + province.demographics.income_high_fraction × config.realestate.high_income_premium
        + province.infrastructure_rating × config.realestate.infrastructure_premium
        - province.conditions.criminal_dominance_index × config.realestate.criminal_dominance_discount
        - province.conditions.inequality_index × config.realestate.inequality_discount

    // Laundering pressure: active real_estate LaunderingOperations inflate prices
    launder_pressure = sum(op.launder_rate_per_tick for op in laundering_operations
                           WHERE op.method == real_estate
                           AND op.destination_business_id links to province)
                       / config.realestate.launder_pressure_divisor

    RETURN base × clamp(prosperity_multiplier + launder_pressure, 0.1, 5.0)
```

**Monthly asking price convergence:**
```
listing.asking_price += (listing.market_value - listing.asking_price)
                        × config.realestate.price_convergence_rate
```

**Named constants (`simulation_config.json → realestate`):**

| Constant | Default | Effect |
|---|---|---|
| `base_value_by_type.residential` | 100,000 | Base residential price per population unit |
| `base_value_by_type.commercial` | 250,000 | Base commercial price |
| `base_value_by_type.industrial` | 400,000 | Base industrial price |
| `population_value_divisor` | 10,000 | Scales base by population density |
| `high_income_premium` | 0.40 | High-income fraction × this → price premium |
| `infrastructure_premium` | 0.30 | Infrastructure rating × this → price premium |
| `criminal_dominance_discount` | 0.50 | CDI × this → price suppression |
| `inequality_discount` | 0.15 | Inequality × this → mild price suppression |
| `launder_pressure_divisor` | 100,000 | Scales laundering pressure to price impact |
| `price_convergence_rate` | 0.05 | Monthly fraction of gap between asking and market value |
| `residential_yield_rate` | 0.0003 | Rental yield per tick (~4% annualised) |
| `commercial_yield_rate` | 0.0005 | Commercial rental yield per tick (~6.5% annualised) |
| `commercial_cost_reduction_rate` | 0.10 | Cost reduction for NPCBusiness occupying owned premises |

### 33.3 — Transactions [V1]

**Player buy:** Player command → scene card negotiation overlay (±15% of `asking_price`) → `owner_id` set to `player_id`; `purchase_price` recorded; `player.wealth` debited. Transaction generates one `financial` EvidenceToken at `VisibilityScope::institutional` (property registry).

**Player sell:** Player command at any time. Sale price = `asking_price` at sale moment. Capital gain = `sale_price - purchase_price`; credited to `player.wealth`; taxed as income in the provincial tax collection step. Transaction generates one `financial` EvidenceToken.

**Rental income:** Each tick where `listing.rented == true`: `player.wealth += listing.rental_income_per_tick`. Tenant assignment is automatic for `commercial` properties when an NPCBusiness in the same province has no owned premises and `cost_per_tick` would benefit from the reduction.

**WorldState field addition:**
```cpp
std::vector<PropertyListing> property_listings;  // all properties in the world; province-partitioned at load
```

**Province field addition:**
```cpp
float avg_property_value;  // mean market_value across all PropertyListing in province;
                            // recomputed monthly; referenced by criminal_dominance scenario test (GDD §12.12)
```

---

## 34. Banking and Loan System [V1]

The banking system provides credit to businesses and players, and gives player-owned banks coercive power over competitors. All actors use the same credit model — the player is not a special case.

### 34.1 — CreditProfile [V1]

```cpp
struct CreditProfile {
    float credit_score;             // 0.0–1.0; 1.0 = excellent credit; 0.0 = no credit history
                                    // Initialised at character/business creation from:
                                    //   player: Background (Born Poor → 0.2, Working Class → 0.4,
                                    //           Middle Class → 0.6, Wealthy → 0.8)
                                    //   NPCBusiness: age × 0.05 + revenue_stability × 0.3 (clamped 0–1)
                                    // Decays 0.01/tick on missed loan payment.
                                    // Increases 0.002/tick when all loans current.
                                    // Criminal conviction: −0.20 immediate.

    float total_debt_outstanding;   // sum of LoanRecord.outstanding_balance; derived, not authoritative
    float debt_service_per_tick;    // sum of current loan repayment obligations per tick; derived
    float debt_to_income_ratio;     // total_debt_outstanding / (revenue_per_tick × TICKS_PER_YEAR);
                                    // > config.banking.denial_dti_threshold → loan denied
};

// FIELD ADDITION to PlayerCharacter:
CreditProfile credit_profile;
std::vector<uint32_t> loan_ids;    // active LoanRecord ids

// FIELD ADDITION to NPCBusiness:
CreditProfile credit_profile;
std::vector<uint32_t> loan_ids;
```

### 34.2 — LoanRecord [V1]

```cpp
enum class LoanPurpose : uint8_t {
    business_capital    = 0,  // Working capital or expansion; secured against NPCBusiness revenue
    property_purchase   = 1,  // Mortgage; secured against PropertyListing.market_value
    personal            = 2,  // Unsecured; higher rate; lower maximum
    criminal_informal   = 3,  // Loan from a criminal organization or loan shark; no formal record;
                               // not reflected in CreditProfile; enforced via ObligationNode
                               // (FavorType::financial_loan); violence escalation on default
};

struct LoanRecord {
    uint32_t  id;
    uint32_t  borrower_id;          // player_id or npc_id
    uint32_t  lender_id;            // NPCBusiness (bank) id; or npc_id for informal loans
    LoanPurpose purpose;
    float     principal;            // original loan amount
    float     outstanding_balance;  // remaining; decremented by repayments
    float     interest_rate;        // per tick; annualised = rate × TICKS_PER_YEAR
    float     repayment_per_tick;   // fixed; debited from borrower cash each tick
    uint32_t  originated_tick;
    uint32_t  maturity_tick;        // loan fully repaid by this tick under scheduled repayments
    bool      in_default;           // true if missed > config.banking.default_grace_ticks payments
    uint32_t  collateral_id;        // PropertyListing.id or NPCBusiness.id pledged; 0 if unsecured
};
```

**Repayment tick integration:** Each tick, for each active `LoanRecord` where `borrower_id == player_id`:
```
IF player.wealth >= loan.repayment_per_tick:
    player.wealth            -= loan.repayment_per_tick
    loan.outstanding_balance -= loan.repayment_per_tick × (1.0 - loan.interest_rate × TICKS_PER_YEAR)
    player.credit_profile.credit_score = min(1.0,
        player.credit_profile.credit_score + config.banking.credit_score_payment_gain)
ELSE:
    loan.in_default = true
    player.credit_profile.credit_score = max(0.0,
        player.credit_profile.credit_score - config.banking.credit_score_miss_penalty)
    // Default consequence: consequence_chain → collateral seizure at maturity_tick
    //   if outstanding_balance > 0 AND collateral_id != 0
```

### 34.3 — Loan Origination [V1]

```
FUNCTION evaluate_loan(applicant_id, lender_npc_business_id, purpose, amount, collateral_id):
    profile = credit_profile(applicant_id)

    // Denial conditions (any one → deny):
    IF profile.credit_score < config.banking.min_credit_score_by_purpose[purpose]:
        RETURN denied(reason: credit_score)
    IF profile.debt_to_income_ratio > config.banking.denial_dti_threshold:
        RETURN denied(reason: debt_to_income)
    IF amount > config.banking.max_loan_multiple_of_income × monthly_income(applicant_id):
        RETURN denied(reason: amount_exceeds_capacity)

    // Interest rate: base rate + credit risk spread
    rate = config.banking.base_interest_rate
           + (1.0 - profile.credit_score) × config.banking.credit_risk_spread
    IF collateral_id != 0:
        rate -= config.banking.collateral_rate_discount   // secured loans cheaper

    maturity_ticks = config.banking.maturity_ticks_by_purpose[purpose]
    repayment = (amount × (1.0 + rate × maturity_ticks)) / maturity_ticks

    RETURN approved(LoanRecord{...})
```

**Credit denial player action:** When denied, player can: (a) accept denial; (b) offer additional collateral (re-evaluate); (c) leverage an `ObligationNode` against a banker NPC to override the denial — generates `documentary` EvidenceToken against both parties; (d) seek informal loan (creates `LoanRecord` with `purpose = criminal_informal` and an `ObligationNode` with `FavorType::financial_loan`).

**Player-owned bank coercion:** A player who owns an NPCBusiness with `sector == finance` and `owner_id == player_id` can deny loans to specific NPCBusiness targets (player action: `deny_credit`). Effect: target cannot expand or refinance. Target's `strategic_decision` next cycle evaluates contraction. Generates no public evidence — loan denial is a normal business decision.

### 34.4 — WorldState Field Addition

```cpp
std::vector<LoanRecord> active_loans;  // all outstanding loans; partitioned by borrower_id at query time
```

**Named constants (`simulation_config.json → banking`):**

| Constant | Default | Effect |
|---|---|---|
| `min_credit_score_by_purpose.business_capital` | 0.35 | Minimum credit score for business loan |
| `min_credit_score_by_purpose.property_purchase` | 0.45 | Mortgage threshold |
| `min_credit_score_by_purpose.personal` | 0.25 | Unsecured personal loan threshold |
| `denial_dti_threshold` | 0.40 | DTI above this → loan denied |
| `max_loan_multiple_of_income` | 36.0 | Max loan = 36 months of income |
| `base_interest_rate` | 0.000027 | Per tick (~1% annualised at 365 ticks/year) |
| `credit_risk_spread` | 0.000082 | Per tick per credit score point deficit (~3% annual spread) |
| `collateral_rate_discount` | 0.000014 | Rate reduction for secured loans (~0.5% annual) |
| `default_grace_ticks` | 5 | Missed payments before in_default |
| `credit_score_payment_gain` | 0.002 | Per-tick credit score gain when current |
| `credit_score_miss_penalty` | 0.01 | Per-missed-payment credit score penalty |

---

## 35. Alternative Identity System [V1]

An alternative identity is a parallel identity record with its own documentation, evidence attribution, and exposure history. Actions taken under an alt identity generate evidence tokens attributed to that identity, not the real actor. This applies equally to the player using an alt to evade prosecution and to an undercover NPC infiltrating a criminal org — the architecture is the same.

### 35.1 — AlternativeIdentity [V1]

```cpp
enum class IdentityStatus : uint8_t {
    active  = 0,  // Currently in use; evidence tokens generated under this identity
                   // are attributed to alt_identity_id, not real_actor_id.
    burned  = 1,  // Identity has been abandoned. All associated evidence tokens
                   // have identity_linked = false set against real_actor_id.
                   // Tokens retain full actionability against the burned identity.
                   // Become actionable against real actor only on identity_link discovery.
    compromised = 2, // Investigator has opened identity_link discovery action;
                      // identity_link KnowledgeEntry exists with confidence > 0.5
                      // on at least one LE NPC's KnowledgeMap. Burn immediately
                      // or evidence collapses onto real actor.
};

struct AlternativeIdentity {
    uint32_t    id;                 // alt_identity_id; used as subject_npc_id on evidence tokens
    uint32_t    real_actor_id;      // player_id or npc_id who controls this identity
    std::string alias_name;
    uint32_t    province_id;        // province where documentation was created;
                                    // identity is strongest in this province (less scrutiny)
    IdentityStatus status;
    uint32_t    created_tick;
    uint32_t    burned_tick;        // 0 if not yet burned

    // --- Documentation depth ---
    float documentation_quality;   // 0.0–1.0; how convincing the identity is under investigation.
                                    // 0.0 = street alias only (no paper trail; detected immediately
                                    //   by formal investigation).
                                    // 0.5 = forged documents (passes routine checks; fails forensic).
                                    // 1.0 = legend-quality (built over months, backstopped by
                                    //   bribed officials; survives most investigative scrutiny).
                                    // Raised by investing time and resources (player action:
                                    //   build_identity_depth). Decays 0.001/tick if not maintained.

    // --- Evidence attribution ---
    std::vector<uint32_t> attributed_evidence_token_ids;
                                    // All EvidenceToken ids with subject_npc_id == this identity's id.
                                    // On burn: EvidenceToken.identity_linked set to false for all entries.

    // --- Surviving links (latent liability) ---
    std::vector<uint32_t> witness_npc_ids;
                                    // NPCs who have interacted with this identity and could
                                    // connect it to the real actor under investigation pressure.
                                    // Populated every time the alt identity interacts with a
                                    // named NPC (direct observation → witness_npc_ids += npc.id).
                                    // Every burned identity's witness list is a permanent latent
                                    // liability — even after burn.
};
```

### 35.2 — Evidence Attribution Rule [V1]

When an evidence token is generated by an action performed under an active `AlternativeIdentity`:

```
IF actor has AlternativeIdentity WHERE status == active:
    token.subject_npc_id = active_alt_identity.id   // attributed to alias, not real actor
    active_alt_identity.attributed_evidence_token_ids += token.id
ELSE:
    token.subject_npc_id = real_actor_id            // standard attribution
```

**Switching identity:** Player command. Active identity can only be one at a time. Actions in the world are performed as whichever identity is currently active. Switching requires physical context (cannot switch identity mid-scene-card). NPC witnesses to the switch itself know both identities.

### 35.3 — Burn Mechanic [V1]

Player command: `burn_identity(alt_identity_id)`. Fires immediately.

```
FUNCTION burn_identity(alt_id: uint32_t):
    identity = alt_identities[alt_id]
    identity.status = burned
    identity.burned_tick = current_tick

    FOR each token_id in identity.attributed_evidence_token_ids:
        token = evidence_pool[token_id]
        token.identity_linked = false   // not actionable against real actor yet
        // token.actionability unchanged — still fully actionable against the burned identity

    // Surviving witnesses are now the primary liability:
    // Each witness_npc.known_evidence may contain KnowledgeEntry of type identity_link
    // with confidence = their direct observation confidence (set at interaction time).
    // These are not cleared by the burn — they are the latent liability.
```

**Post-burn:** The burned identity becomes a separate investigation subject. LE NPCs targeting the burned identity cannot automatically connect it to the real actor. Connection requires `identity_link_discovery` — a distinct investigative action triggered when:
- A `witness_npc_id` NPC is arrested or pressured and reveals identity information
- Forensic document analysis (consequence of `formal_inquiry` status on the alt identity)
- A rival actor (NPC criminal org or political enemy) provides the link deliberately

On `identity_link_discovery`, a `KnowledgeEntry { type: identity_link, subject_id: alt_id, secondary_subject_id: real_actor_id, confidence: 0.7 }` is added to the discovering LE NPC's `KnowledgeMap`. All `attributed_evidence_token_ids` become actionable against `real_actor_id` immediately.

### 35.4 — PlayerCharacter Field Addition

```cpp
// FIELD ADDITION to PlayerCharacter:
std::vector<AlternativeIdentity> alt_identities;    // all identities, active and burned
uint32_t active_alt_identity_id;                    // 0 = acting as real identity
```

**Named constants (`simulation_config.json → identity`):**

| Constant | Default | Effect |
|---|---|---|
| `documentation_decay_rate` | 0.001 | Per-tick decay of documentation_quality without maintenance |
| `build_identity_depth_rate` | 0.005 | Per-action quality gain from build_identity_depth player action |
| `link_discovery_witness_confidence` | 0.70 | Confidence of identity_link KnowledgeEntry from witness |
| `link_discovery_forensic_confidence` | 0.55 | Confidence from forensic document analysis |

---

## 36. Informant System [V1]

Every person in the criminal network is a potential informant under arrest pressure. The GDD is explicit: there is no separate loyalty field — defection probability emerges from existing NPC fields (`risk_tolerance`, `trust`, memory entries). The informant system formalises how that emergence works and what the player can do about it.

### 36.1 — InformantRecord [V1]

`InformantRecord` is created when an NPC transitions from `potential informant` to `active cooperation`. Before that, informant risk is assessed dynamically from NPC state without a record.

```cpp
enum class InformantStatus : uint8_t {
    potential  = 0,  // Under arrest pressure; not yet cooperating.
                      // Flip probability evaluated each tick while pressure active.
    cooperating = 1, // Has agreed to cooperate with LE. Evidence flowing.
    protected   = 2, // Player has intervened (payment, threat, relocation).
                      // Flip probability set to 0.0 while protected. Protection
                      // can be maintained or withdrawn.
    eliminated  = 3, // NPC is dead or permanently removed.
                      // All of their knowledge dies with them. Evidence tokens they
                      // held decay at discredited rate immediately.
                      // Violence consequence: InvestigatorMeter fill_rate ×
                      //   config.opsec.personnel_violence_multiplier (§16).
};

struct InformantRecord {
    uint32_t npc_id;
    uint32_t arresting_le_npc_id;   // LE NPC applying pressure
    InformantStatus status;
    float flip_probability;         // 0.0–1.0; evaluated per tick while potential
    uint32_t pressure_started_tick;
    uint32_t cooperation_started_tick; // 0 if not yet cooperating
    std::vector<uint32_t> disclosed_knowledge_ids;
                                    // KnowledgeEntry ids revealed to LE on cooperation;
                                    // each becomes a high-actionability EvidenceToken
};
```

### 36.2 — Flip Probability Formula [V1]

Evaluated each tick for every NPC with `InformantStatus::potential`. Cooperation fires if `random(0.0, 1.0) < flip_probability`.

```
FUNCTION compute_flip_probability(npc: NPC, record: InformantRecord) → float:
    base = config.informant.base_flip_rate   // 0.005 per tick under arrest pressure

    // Risk tolerance: low risk tolerance → higher flip rate
    risk_factor = (1.0 - npc.risk_tolerance) × config.informant.risk_tolerance_scale

    // Trust with primary obligation creditor (player or criminal org leader):
    trust_with_player = relationship_trust(npc.id, record.arresting_le_npc_id's
                                           criminal_target_id)
    trust_factor = (1.0 - trust_with_player) × config.informant.trust_scale
    // High trust → lower flip; zero trust → maximum trust_factor contribution

    // Mutual incrimination: obligation nodes suppress cooperation
    mutual_incrimination_count = count(obligation_nodes WHERE
        creditor_id == npc.id
        AND debtor_id == player_id
        AND favor_type IN {evidence_suppressed, whistleblower_silenced})
    incrimination_suppression = mutual_incrimination_count
                                × config.informant.mutual_incrimination_suppression
    // Each mutual incrimination obligation reduces flip probability — the NPC is
    // equally exposed if they talk. Documented in GDD §12.2.

    // Compartmentalization benefit: if NPC has no KnowledgeEntry naming other operatives,
    // cooperation value to LE is low → LE applies less pressure → lower flip probability
    operative_knowledge_count = count(npc.knowledge_map WHERE
        type == activity AND confidence > 0.5)
    compartmentalization_bonus = 0.0
    IF operative_knowledge_count == 0:
        compartmentalization_bonus = config.informant.compartmentalization_bonus

    raw = base + risk_factor + trust_factor - incrimination_suppression
          - compartmentalization_bonus
    RETURN clamp(raw, 0.0, config.informant.max_flip_probability)
```

**Quantified formula (TDD-SC1):**
```
flip_probability =
    BASE_FLIP_RATE                    // 0.02
    + risk_tolerance_factor × 0.15   // risk_tolerance [0,1]
    + trust_deficit × 0.20           // (1 - trust/100) [0,1]
    - intimidation × 0.25            // clamp [0, 0.25]
    - loyalty × 0.10                 // clamp [0, 0.10]
// Result clamped to [0.0, 0.5]
```
This represents BASE_FLIP_RATE as a 2% per-tick baseline under arrest pressure. Risk tolerance adds up to 15% to flip probability for extremely risk-averse NPCs. Trust deficit contributes up to 20% for completely untrusted contacts. Intimidation suppression (from threats or protective custody arrangements) can reduce by up to 25%. Loyalty obligations (family, long-term partnership) suppress by up to 10%. The final result is bounded at [0.0, 0.5] to ensure even maximally vulnerable NPCs have a 50% monthly flip probability ceiling under sustained pressure, matching GDD §12.2 design intent.

### 36.3 — Cooperation Consequences [V1]

When `flip_probability` roll succeeds:

```
record.status = cooperating
record.cooperation_started_tick = current_tick

FOR each KnowledgeEntry in npc.knowledge_map WHERE confidence > 0.4:
    // Convert to high-actionability EvidenceToken held by arresting LE NPC
    token = EvidenceToken {
        type: IF entry.type == identity_link: financial
              ELIF entry.type == activity:    testimonial
              ELIF entry.type == evidence_token: documentary
              ELSE: testimonial,
        subject_npc_id: entry.subject_id,
        actionability: entry.confidence × config.informant.cooperation_actionability_scale,
        holder_npc_id: record.arresting_le_npc_id
    }
    evidence_pool += token
    record.disclosed_knowledge_ids += entry.id
    record.arresting_le_npc_id's InvestigatorMeter.current_level
        += token.actionability × config.informant.meter_fill_per_disclosure
```

### 36.4 — Player Countermeasures [V1]

| Action | Mechanism | Evidence risk |
|---|---|---|
| `pay_silence` | Transfers `config.informant.silence_payment` to NPC; sets `status = protected`; creates `ObligationNode(FavorType::whistleblower_silenced)` | `financial` + `testimonial` EvidenceToken (payment trail + NPC knows the payment happened) |
| `threaten_silence` | Sets `status = protected`; adds `employment_negative` memory to NPC (weight −0.7); increases `risk_tolerance` 0.10 | Immediate: NPC generates `testimonial` evidence if they have any relationship with LE NPC |
| `relocate_witness` | Transit action: NPC physically moved to different province; LE NPC loses direct pressure access; flip probability drops to `base × 0.2` | `physical` EvidenceToken on departure |
| `eliminate` | NPC death; `status = eliminated`; all knowledge removed; InvestigatorMeter fill_rate × 3.0 | High-actionability `physical` evidence; violence escalation |

**WorldState field addition:**
```cpp
std::vector<InformantRecord> informant_records;
```

**Named constants (`simulation_config.json → informant`):**

| Constant | Default | Effect |
|---|---|---|
| `base_flip_rate` | 0.005 | Per-tick cooperation probability base under arrest pressure |
| `risk_tolerance_scale` | 0.30 | Risk tolerance contribution to flip probability |
| `trust_scale` | 0.25 | Low trust contribution |
| `mutual_incrimination_suppression` | 0.08 | Per obligation node reduction in flip probability |
| `compartmentalization_bonus` | 0.10 | Reduction for operatives with no operative knowledge |
| `max_flip_probability` | 0.20 | Per-tick cap (even worst-case: ~5 months under constant pressure) |
| `cooperation_actionability_scale` | 0.85 | Knowledge confidence → token actionability |
| `meter_fill_per_disclosure` | 0.08 | InvestigatorMeter fill per disclosed knowledge entry |
| `silence_payment` | 50,000 | Default silence payment amount |

---

## 37. Protection Rackets [V1]

Protection rackets are systematic extortion of legitimate businesses. Revenue flows directly to the criminal org's `cash` pool. They are mechanically simpler than the drug supply chain — no production facility, no goods, no supply chain. The core loop is: demand → payment or refusal → escalation.

### 37.1 — ProtectionRacket [V1]

```cpp
enum class RacketStatus : uint8_t {
    active   = 0,  // Business is paying; revenue flowing each tick
    refused  = 1,  // Business refused; escalation timer running
    lapsed   = 2,  // Player/org stopped collecting; racket inactive
    expelled = 3,  // Criminal org expelled from province; all rackets lapsed
};

enum class RacketEscalationStage : uint8_t {
    demand_issued   = 0,  // Initial demand sent; no action taken yet
    warning         = 1,  // Payment 5+ ticks overdue; intimidation action performed
    property_damage = 2,  // 15+ ticks overdue; facility incident consequence queued
    violence        = 3,  // 30+ ticks overdue; personnel violence consequence
                           // InvestigatorMeter fill_rate × config.opsec.personnel_violence_multiplier
    abandonment     = 4,  // Business exits province or enters bankruptcy under racket pressure
};

struct ProtectionRacket {
    uint32_t id;
    uint32_t criminal_org_id;       // or player_id if player-operated
    uint32_t target_business_id;    // NPCBusiness being extorted
    float demand_per_tick;          // ongoing payment; set at creation from:
                                    //   target_business.revenue_per_tick
                                    //   × config.racket.demand_rate
    RacketStatus status;
    RacketEscalationStage escalation_stage;
    uint32_t last_payment_tick;
    uint32_t demand_issued_tick;
    float community_grievance_contribution;
                                    // amount added to province CommunityState.grievance_level
                                    // each tick the racket is active:
                                    //   config.racket.grievance_per_demand_unit × demand_per_tick
};
```

### 37.2 — Payment and Escalation Logic [V1]

**Tick integration (runs at tick step 7, NPC behavior):**

```
FOR each ProtectionRacket racket WHERE status == active OR status == refused:
    IF status == active:
        target_biz = npc_businesses[racket.target_business_id]
        IF target_biz.cash >= racket.demand_per_tick:
            target_biz.cash -= racket.demand_per_tick
            criminal_org[racket.criminal_org_id].cash += racket.demand_per_tick
            racket.last_payment_tick = current_tick
            // protection_cost tracked in NPCBusiness (see field addition below)
            target_biz.protection_cost_per_tick = racket.demand_per_tick
        ELSE:
            // Business cannot pay — status remains active; no escalation yet
            // Sustained inability to pay → strategic_decision: exit province

    IF status == refused:
        ticks_overdue = current_tick - racket.demand_issued_tick
        new_stage = compute_escalation_stage(ticks_overdue)
        IF new_stage > racket.escalation_stage:
            racket.escalation_stage = new_stage
            schedule_racket_consequence(racket, new_stage)

    // Community grievance accumulation
    IF status == active OR escalation_stage >= warning:
        province.community.grievance_level = min(1.0,
            province.community.grievance_level
            + racket.community_grievance_contribution)
```

**Refusal response:** When an NPCBusiness refuses a racket demand, the decision is made in the target's `strategic_decision` step. Refusal probability:

```
refuse_probability = target.profile == defensive_incumbent ? 0.40 : 0.20
refuse_probability += (1.0 - province.conditions.criminal_dominance_index) × 0.30
// High criminal dominance → less likely to refuse (realistic coercion effect)
refuse_probability -= target.regulatory_violation_severity × 0.10
// Businesses with their own violations can be threatened reciprocally
```

### 37.3 — Racket Consequences [V1]

| EscalationStage | Ticks overdue | Consequence |
|---|---|---|
| `warning` | 5 | Intimidation scene card fires for target business owner NPC; `employment_negative` memory entry (emotional_weight −0.5) |
| `property_damage` | 15 | `facility_incident` consequence queued (severity 0.4); `physical` EvidenceToken generated in province |
| `violence` | 30 | Personnel violence consequence; InvestigatorMeter fill_rate × 3.0; `testimonial` evidence from witnesses |
| `abandonment` | 60 | Business enters bankruptcy or exits province; CDI in province increments |

**NPCBusiness field addition:**
```cpp
float protection_cost_per_tick;  // 0.0 when no racket; set to demand_per_tick when paying.
                                  // Included in cost_per_tick for strategic decision calculations.
                                  // Referenced by GDD §12.12 scenario test (property_value/protection_cost).
```

**WorldState field addition:**
```cpp
std::vector<ProtectionRacket> protection_rackets;
```

**Named constants (`simulation_config.json → racket`):**

| Constant | Default | Effect |
|---|---|---|
| `demand_rate` | 0.08 | Racket demand as fraction of target's revenue_per_tick |
| `grievance_per_demand_unit` | 0.00001 | Grievance per currency unit of demand per tick |
| `incumbent_refuse_probability` | 0.40 | Base refusal rate for defensive_incumbent profile |
| `default_refuse_probability` | 0.20 | Base refusal rate for other profiles |

---

## 38. Weapons Trafficking [V1]

Weapons trafficking mirrors the drug economy architecture: same `RegionalMarket` infrastructure, same informal market layer, same supply chain pattern (§Commodities v2.3). What differs is the downstream effect system — weapons sold in the world appear in subsequent crimes, generating a traceable chain of custody evidence trail that investigators can follow.

### 38.1 — WeaponType Enum and Goods [V1]

```cpp
enum class WeaponType : uint8_t {
    small_arms       = 0,  // Handguns, rifles. High market breadth. Primary criminal
                            // market good. Sourced from: legal market diversion,
                            // corrupt military/police supply.
    ammunition       = 1,  // Consumable. High turnover. Needed for small_arms use.
                            // Legal to produce; criminal when sold to unlicensed buyers.
    heavy_weapons    = 2,  // RPGs, heavy machine guns, explosives (military grade).
                            // Arms embargo goods. Generates intelligence service attention.
                            // [EX] not produced in V1 — only appears as imported contraband.
    converted_legal  = 3,  // Legally-registered weapons illegally converted
                            // (e.g., semi-auto to automatic). Traceable to original owner.
                            // Generates additional evidence chain (legal owner → converter).
};
```

**Commodity additions (`goods.csv` — criminal goods table extension):**

| Key | Display Name | Primary Inputs | V1/EX | Notes |
|---|---|---|---|---|
| `small_arms` | Small Arms | `steel` + `precision_components` + facility | V1 | Legal manufacture; criminal when diverted to unlicensed buyers |
| `ammunition` | Ammunition | `steel` + `chemical_inputs` + facility | V1 | Consumable; produced legally; dual-use |
| `arms_cache` | Arms Cache | `small_arms` + `ammunition` | V1 | Bundled inventory unit for wholesale transfer |
| `heavy_weapons` | Heavy Weapons | [import only — no recipe] | EX | Arms embargo item; import consequence triggers intelligence response |

### 38.2 — Supply Chain [V1]

Weapons enter the criminal market through two V1 channels. No new facility type required — diversion uses existing formal-sector facilities with a `divert_to_informal` action.

**Channel 1 — Legal market diversion:** An NPCBusiness in `sector == manufacturing` producing `small_arms` or `ammunition` can be coerced or corrupted into diverting a fraction of output to the informal market. Player action: `negotiate_diversion(business_id, diversion_fraction)`. Requires: either `owner_id == player_id` (own business) or a trust > 0.6 obligation with the business's controlling NPC. Diversion fraction: 0.0–`config.weapons.max_diversion_fraction` (default 0.30). Evidence generated: `documentary` (inventory discrepancy) per diverted shipment.

**Channel 2 — Corrupt official procurement:** Player action: `procure_from_corrupt_source(weapon_type, quantity, official_npc_id)`. Requires: NPC with `role == military_officer` or `role == law_enforcement` and an `ObligationNode` with the player as creditor. Generates: one `physical` EvidenceToken (movement of goods) + one `testimonial` EvidenceToken (corrupt official as witness).

### 38.3 — Market and Pricing [V1]

Weapons goods price through the same `RegionalMarket` informal layer as drugs. Price is driven by demand (criminal org territorial conflict stage — higher conflict → higher demand) and supply (diversion rate + import availability).

```
informal_spot_price(weapon_type, province) =
    config.weapons.base_price[weapon_type]
    × (1.0 + territorial_conflict_demand_modifier(province))
    / max(supply_this_tick, config.weapons.price_floor_supply)

// territorial_conflict_demand_modifier:
//   TerritorialConflictStage.none:               0.0
//   economic through intelligence_harassment:    0.2
//   property_violence:                           0.5
//   personnel_violence:                          0.8
//   open_warfare:                                1.5
```

### 38.4 — Downstream Effect and Evidence Chain [V1]

When a weapon unit sold by the player (or an NPC the player supplied) is used in a crime, the simulation traces the chain of custody.

```
ON crime_event WHERE weapon_used == true AND weapon_source_id is known:
    // Generate evidence token linking the weapon to its last known owner
    token = EvidenceToken {
        type: physical,
        subject_npc_id: weapon_last_owner_id,  // may be player_id
        actionability: config.weapons.chain_custody_actionability,
        holder_npc_id: investigating_le_npc_id
    }
    evidence_pool += token
    // If weapon_last_owner_id is a known front NPC for the player,
    // this chain of custody eventually reaches player_id via identity_link discovery
```

**Arms embargo LE escalation (heavy_weapons):** Import or transfer of `heavy_weapons` generates an intelligence service response in addition to normal LE:
- Immediate: `InvestigatorMeter.current_level += config.weapons.embargo_meter_spike` for all LE NPCs in province
- Consequence: `embargo_investigation` consequence type; cannot be suppressed by local corruption (intelligence services operate at national level, outside local LE corruption network)

### 38.5 — WorldState Field Addition

```cpp
// FIELD ADDITION to WorldState:
// No new container needed — weapon goods flow through existing RegionalMarket informal layer.
// Weapon units are tracked via goods_id in RegionalMarket, same as drug goods.

// FIELD ADDITION to NPCBusiness (manufacturing sector):
float diversion_fraction;  // 0.0 = no diversion; > 0.0 = fraction of output diverted to informal market
                            // Set by player action or NPC strategic decision under coercion.
```

**Named constants (`simulation_config.json → weapons`):**

| Constant | Default | Effect |
|---|---|---|
| `base_price.small_arms` | 800 | Base informal price per unit |
| `base_price.ammunition` | 50 | Base informal price per unit |
| `base_price.arms_cache` | 5,000 | Bundle price |
| `price_floor_supply` | 1.0 | Supply floor to prevent division by zero |
| `max_diversion_fraction` | 0.30 | Maximum fraction of legal output that can be diverted |
| `chain_custody_actionability` | 0.60 | Actionability of chain-of-custody evidence tokens |
| `embargo_meter_spike` | 0.25 | InvestigatorMeter spike on heavy_weapons transfer |

---

## 39. Drug Economy Type System [V1]

The §16 V1 constraint names "drug supply chain" without naming which drugs. The Commodities doc has recipe keys. The TDD had no `DrugType` enum and no market-tier distinction. Both are added here. Closes P-1.

### 39.1 — DrugType Enum [V1]

```cpp
enum class DrugType : uint8_t {
    // --- V1 goods ---
    cannabis          = 0,  // good_key: "cannabis_processed"
                             // Legal in some provinces (depends on province.drug_policy).
                             // Same good, different legal status per jurisdiction.
                             // Lowest margin; highest volume.

    methamphetamine   = 1,  // good_key: "methamphetamine"
                             // High margin; high LE attention. Chemical waste signal
                             // is the primary detection surface.

    synthetic_opioid  = 2,  // good_key: "synthetic_opioid"
                             // Diversion from pharmaceutical precursors.
                             // Highest addiction rate → sustained demand.
                             // Medical/criminal supply chain overlap is the
                             // distinctive investigative challenge.

    designer_drug     = 3,  // good_key: "designer_drug"
                             // Produced by criminal R&D projects (§40).
                             // Legal until scheduled. See SchedulingStatus.
                             // Margin collapses on scheduling; pipeline must
                             // have successor compound ready.

    // --- EX goods (enum values reserved; no V1 mechanics) ---
    cocaine           = 4,  // good_key: "cocaine" [EX] — requires coca cultivation
    heroin            = 5,  // good_key: "heroin"  [EX] — requires poppy cultivation
};
```

**Drug policy field addition to Province:**
```cpp
// FIELD ADDITION to Province:
std::map<DrugType, bool> drug_legalization_status;
    // true = legal in this province; false = controlled.
    // Initialized from scenario file. Modified by legislation (§14 legislative cycle).
    // Effect: legal drugs price through RegionalMarket formal layer;
    //         illegal drugs through informal layer.
    // Cannabis defaults: varies by province (scenario seeded).
    // All others default false at world start.
```

### 39.2 — Market Tier Distinction [V1]

The GDD §12.2 wholesale/retail distinction is modelled as a `MarketTier` attribute on `NPCBusiness` criminal operations, not as a separate market layer. The same `RegionalMarket` holds both tiers' prices; the distinction determines which actors the business transacts with and their price point.

```cpp
enum class DrugMarketTier : uint8_t {
    wholesale = 0,  // Sells to distributors or mid-level operators.
                     // Volume: high. Margin: lower (typically 40–60% above production cost).
                     // LE contact: low (fewer transactions, larger quantities per transaction).
                     // Price: informal spot_price × config.drug.wholesale_price_fraction.

    retail    = 1,  // Sells directly to end consumers.
                     // Volume: low per transaction. Margin: high (typically 200–500% above production cost).
                     // LE contact: high (street-level patrol attention, controlled buy risk).
                     // Price: informal spot_price (the spot_price IS the retail reference price).
};

// FIELD ADDITION to NPCBusiness (criminal_sector == true businesses only):
DrugMarketTier drug_market_tier;
    // Determines transaction counterparts and pricing.
    // Wholesale businesses transact with other criminal businesses;
    // retail businesses transact with background NPC population cohorts.
    // NPC criminal org strategic decision can switch tier (e.g., vertical integration
    // when territorial control is high).
```

**Wholesale price formula:**
```
wholesale_price(drug_type, province) =
    RegionalMarket.spot_price(good_key(drug_type), province, informal_layer)
    × config.drug.wholesale_price_fraction   // default: 0.45
```

**Named constants (`simulation_config.json → drug`):**

| Constant | Default | Effect |
|---|---|---|
| `wholesale_price_fraction` | 0.45 | Wholesale price as fraction of retail spot price |

---

## 40. Designer Drug Scheduling Race [V1]

The R&D doc (§Part 7) defines `SchedulingProcess` and `CriminalRnDProject` structs which live in `GlobalTechnologyState`. The TDD needed a bridge struct connecting a synthesized designer compound to its `RegionalMarket` presence and tracking its scheduling lifecycle. Closes P-6.

### 40.1 — DesignerDrugRecord [V1]

```cpp
enum class SchedulingStatus : uint8_t {
    unscheduled      = 0,  // Compound is legal; prices through formal market layer.
                            // Full retail margin available. No LE criminality — only
                            // standard regulatory scrutiny for novel substance.

    review_initiated = 1,  // Regulators detected compound; scheduling review underway.
                            // `SchedulingProcess.detection_tick` is set.
                            // Duration = SchedulingProcess.review_duration ticks.
                            // Informal market begins forming in anticipation —
                            // suppliers holding inventory face risk if scheduled.

    scheduled        = 2,  // Compound is now a controlled substance.
                            // All existing inventory becomes criminal stock.
                            // Price moves to informal market layer immediately.
                            // Criminal R&D pipeline must have a successor or
                            // supply gap opens (informal market supply = 0).
};

struct DesignerDrugRecord {
    uint32_t    id;
    std::string compound_key;           // unique identifier; also the goods.csv key
                                         // for this compound's RegionalMarket entry.
                                         // Keys generated as "designer_drug_{id}".

    uint32_t    producing_actor_id;     // player_id or criminal_org_id that completed the R&D

    SchedulingStatus scheduling_status;

    uint32_t    synthesized_tick;       // when CriminalRnDProject completed
    uint32_t    detection_tick;         // 0 until regulators detect; set when
                                         // FacilitySignals accumulate enough evidence
                                         // that the compound is identified (see below)
    uint32_t    scheduling_enacted_tick; // 0 until scheduled

    float       market_margin_multiplier;
                                        // Starts at config.drug.designer_legal_margin_mult
                                        // while unscheduled (legal premium).
                                        // Falls to 1.0 on scheduling (same as other informal drugs).
                                        // Falls further to config.drug.designer_post_schedule_margin
                                        // if no successor compound — price competition from
                                        // remaining inventory liquidation.

    // --- R&D linkage ---
    uint32_t    rnd_project_id;         // CriminalRnDProject.id that produced this compound;
                                         // references R&D doc §Part 7

    // --- Scheduling process linkage ---
    uint32_t    scheduling_process_id;  // SchedulingProcess.id in GlobalTechnologyState;
                                         // 0 until review_initiated
};
```

### 40.2 — Detection Trigger [V1]

A designer drug is detected (triggering `review_initiated`) when the cumulative `EvidenceToken` financial + physical evidence attributed to its production exceeds a threshold visible to a regulator or LE NPC.

```
// Runs monthly (WorkType::designer_drug_detection_check):
FOR each DesignerDrugRecord WHERE scheduling_status == unscheduled:
    province = province_of(record.producing_actor_id)
    // Sum actionability of financial + physical tokens attributed to the producing actor
    //   that have been observed by any LE/regulator NPC in the province:
    evidence_weight = sum(token.actionability
                          for token in evidence_pool
                          WHERE token.subject_npc_id == record.producing_actor_id
                          AND token.type IN {financial, physical}
                          AND token.holder_npc_id has role IN {law_enforcement, regulator})

    IF evidence_weight >= config.drug.designer_detection_threshold:
        record.scheduling_status = review_initiated
        record.detection_tick = current_tick
        // Create SchedulingProcess in GlobalTechnologyState:
        process = SchedulingProcess {
            compound_key:     record.compound_key,
            detection_tick:   current_tick,
            review_duration:  config.drug.scheduling_review_duration
                              × political_delay_factor(province.nation_id),
            political_delay:  political_delay_factor(province.nation_id),
            is_enacted:       false
        }
        GlobalTechnologyState.active_scheduling_reviews += process
        record.scheduling_process_id = process.id
```

**On `scheduling_enacted`:**
```
// Runs each tick for active_scheduling_reviews:
FOR each SchedulingProcess p WHERE NOT p.is_enacted:
    IF current_tick >= p.detection_tick + p.review_duration:
        p.is_enacted = true
        record = DesignerDrugRecord WHERE compound_key == p.compound_key
        record.scheduling_status = scheduled
        record.scheduling_enacted_tick = current_tick
        // All inventory of this good in informal market is now criminal.
        // Price layer: moves from formal to informal RegionalMarket immediately.
        // Successor gap: IF no new DesignerDrugRecord WHERE producing_actor_id
        //                == record.producing_actor_id AND scheduling_status == unscheduled:
        //    → informal market supply for designer_drug drops to 0 for that actor
        //    → market gap opportunity for rival producers
```

**WorldState field addition:**
```cpp
std::vector<DesignerDrugRecord> designer_drug_records;
```

**Named constants (`simulation_config.json → drug`):**

| Constant | Default | Effect |
|---|---|---|
| `designer_detection_threshold` | 2.5 | Cumulative evidence weight triggering scheduling review |
| `scheduling_review_duration` | 180 | Ticks (6 months) for review in fast political system; scaled by `political_delay_factor` |
| `designer_legal_margin_mult` | 2.5 | Price multiplier while compound is unscheduled (legal premium) |
| `designer_post_schedule_margin` | 0.80 | Margin fraction when no successor in pipeline |

---

## 41. Antitrust Trigger Model [V1]

`antitrust_market_share_threshold = 0.40` exists in GDD §10.5 constants. The TDD has the threshold named but wired to nothing — exceeding it triggers no regulatory response. Wiring it to the existing `RegulatorScrutinyMeter` and the legislative system closes P-5.

### 41.1 — Antitrust Market Concentration Check [V1]

Runs monthly (`WorkType::antitrust_monthly_check`), separate from the per-tick facility scrutiny loop. Operates at actor level, not facility level.

```
// Monthly antitrust check — all provinces:
FOR each province:
    FOR each good_id traded in province (formal market layer only):
        FOR each actor (player or NPCBusiness owner) with supply in province:
            actor_supply_share = sum(output for facilities owned by actor
                                     producing good_id in province)
                                 / RegionalMarket.supply(good_id, province)

            // Tier 1: Preliminary inquiry (0.40 threshold)
            IF actor_supply_share >= config.antitrust.market_share_threshold:
                // Find antitrust-specialist regulator NPC in province:
                regulator = find_npc(province, role: regulator,
                                     specialization: antitrust)
                IF regulator exists:
                    // Fill InvestigatorMeter targeting this actor
                    regulator.investigator_meter.fill_rate +=
                        config.antitrust.meter_fill_per_threshold_tick
                    // Generate documentary EvidenceToken (market share data
                    // is public_info — formal market records are auditable):
                    IF regulator.investigator_meter.current_level
                       crosses config.investigator.formal_inquiry_threshold:
                        generate EvidenceToken {
                            type: documentary,
                            subject_npc_id: actor_id,
                            actionability: actor_supply_share
                                           - config.antitrust.market_share_threshold,
                            holder_npc_id: regulator.id
                        }

            // Tier 2: Dominant price-mover (0.70 threshold) —
            //         sustained dominance triggers legislative pressure
            IF actor_supply_share >= DOMINANT_PRICE_MOVER_THRESHOLD:
                // Increase probability of antitrust legislative proposal being
                // introduced this quarter:
                province.political.antitrust_proposal_pressure = min(1.0,
                    province.political.antitrust_proposal_pressure
                    + config.antitrust.dominance_proposal_pressure_per_tick)
```

**`DOMINANT_PRICE_MOVER_THRESHOLD`** is the existing constant from §5 (0.70). `antitrust_market_share_threshold` is `consequences.antitrust_market_share_threshold` (0.40) from GDD §10.5.

**Province field addition:**
```cpp
// FIELD ADDITION to RegionalPoliticalState (within Province):
float antitrust_proposal_pressure;  // 0.0–1.0; accumulates while dominant actor
                                     // maintains >70% market share; decays 0.01/tick
                                     // when share drops below threshold.
                                     // When >= config.antitrust.proposal_threshold:
                                     // a LegislativeProposal of type antitrust is
                                     // auto-generated by an NPC legislator this quarter.
```

**Named constants (`simulation_config.json → antitrust`):**

| Constant | Default | Effect |
|---|---|---|
| `market_share_threshold` | 0.40 | Triggers preliminary antitrust inquiry |
| `meter_fill_per_threshold_tick` | 0.002 | Monthly InvestigatorMeter fill from market concentration |
| `dominance_proposal_pressure_per_tick` | 0.005 | Monthly pressure accumulation at >70% share |
| `proposal_threshold` | 0.50 | Antitrust pressure level at which legislation is auto-generated |
| `proposal_pressure_decay_rate` | 0.01 | Monthly decay when share drops below 0.70 |

---

## 42. Commodity Trading Player Action [V1]

`RegionalMarket` spot prices are fully specced; the price model is complete. The missing piece is the player action to buy and sell commodity units as a pure investment activity (distinct from operating a production business). Closes P-7.

### 42.1 — CommodityPosition [V1]

```cpp
enum class PositionType : uint8_t {
    long_position  = 0,  // Bought units; profit if spot_price rises.
    short_position = 1,  // Sold units not yet owned (borrowed from market pool);
                          // profit if spot_price falls.
                          // V1: short positions simplified — no borrowing
                          // mechanism; short is a deferred sell at entry_price
                          // settled at market exit. No margin call mechanic in V1.
};

struct CommodityPosition {
    uint32_t  id;
    uint32_t  actor_id;             // player_id or npc_id
    uint32_t  good_id;              // references goods.csv
    uint32_t  province_id;          // which RegionalMarket this position is in
    PositionType position_type;
    float     quantity;             // units held
    float     entry_price;          // spot_price at position open
    float     current_value;        // derived: quantity × spot_price(current); not stored
    uint32_t  opened_tick;
    uint32_t  exit_tick;            // 0 while open; set on close
    float     realised_pnl;         // 0.0 while open; set on close
                                     // long:  (exit_price - entry_price) × quantity
                                     // short: (entry_price - exit_price) × quantity
};
```

### 42.2 — Player Actions [V1]

**Open position (buy):**
```
player action: open_commodity_position(good_id, province_id, quantity, position_type)

cost = quantity × RegionalMarket.spot_price(good_id, province_id)
IF player.wealth < cost: REJECT (insufficient funds)

player.wealth -= cost
create CommodityPosition { actor_id: player_id, entry_price: spot_price, ... }

// Market impact: large positions affect spot_price via supply/demand
IF quantity > RegionalMarket.supply(good_id, province_id) × config.trading.market_impact_threshold:
    // Buying: increases demand → pushes price up this tick
    RegionalMarket.demand(good_id, province_id) ×= (1.0 + config.trading.market_impact_scale
                                                     × quantity / RegionalMarket.supply(...))
```

**Close position (sell):**
```
player action: close_commodity_position(position_id)

exit_price = RegionalMarket.spot_price(good_id, province_id)
IF position_type == long_position:
    proceeds = quantity × exit_price
    player.wealth += proceeds
    position.realised_pnl = (exit_price - entry_price) × quantity
ELSE:  // short
    settlement_cost = quantity × exit_price
    player.wealth += quantity × entry_price   // original "sale" proceeds credited back
    player.wealth -= settlement_cost          // buy back at current price
    position.realised_pnl = (entry_price - exit_price) × quantity

position.exit_tick = current_tick
// Tax: realised_pnl > 0 → capital gains; collected at next provincial tax step (§31)
```

**WorldState field addition:**
```cpp
std::vector<CommodityPosition> commodity_positions;
```

**Named constants (`simulation_config.json → trading`):**

| Constant | Default | Effect |
|---|---|---|
| `market_impact_threshold` | 0.10 | Position size fraction above which market impact applies |
| `market_impact_scale` | 0.05 | Demand modifier per unit above impact threshold |

---

## 43. Remaining Scope and Absent System Decisions [V1]

Explicit design decisions for items flagged in the March 2026 audit as requiring a V1/EX/CUT ruling. These are recorded here to prevent the Bootstrapper from inferring the answer.

### 43.1 — S-1: NPC-to-NPC Obligation Scope [V1]

**Decision: V1 constraint is maintained with one explicit carve-out.**

The `ObligationNode` V1 constraint (player as creditor or debtor only) is confirmed as correct for the general case. The reasoning: tracking every NPC-to-NPC obligation at full fidelity at V1 scale (~2,000 significant NPCs) is computationally and complexity prohibitive.

**Carve-out:** NPC-to-NPC `ObligationNode` records ARE created when:
1. A criminal organization extorts an NPCBusiness — the extortion is modeled as a `ProtectionRacket` (§37), not an `ObligationNode`. No carve-out needed here — §37 covers it.
2. A player-adjacent NPC takes on a significant obligation with another NPC that the player would need to know about (e.g., a politician the player is allied with owes a favor to a rival). These are surfaced via the NPC's motivation weight adjustments, consistent with the V1 constraint.

**Explicit rule for Bootstrapper:** `WorldState.obligation_network` contains only `ObligationNode` entries where `creditor_npc_id == player_id` OR `debtor_npc_id == player_id` (using `player_id` as the sentinel value). NPC-to-NPC obligations are represented as motivation weight adjustments on the relevant NPC records, not as `ObligationNode` entries. This is a V1 constraint, not an architectural one — the full NPC-to-NPC obligation graph is expansion scope. The player-is-not-special principle is satisfied at the emergent level (NPCs genuinely respond to their obligation-equivalent motivation weights) rather than the structural level.

### 43.2 — Ab-1: Business Credit / Working Capital [V1 THIN]

**Decision: V1 thin model — single-field approximation.**

Full accounts receivable/payable with per-sector payment terms is EX scope. V1 adds one field to approximate working capital pressure:

```cpp
// FIELD ADDITION to NPCBusiness:
float accounts_payable_float;   // estimated outstanding supplier obligations.
                                 // Approximated as: cost_per_tick × config.business.payment_lag_ticks.
                                 // Conceptually: the business owes its last N ticks of input costs.
                                 // Used by: bankruptcy evaluation (if cash < accounts_payable_float,
                                 //   business is cash-flow insolvent even if technically profitable).
                                 // Updated monthly; not per-tick.
                                 // Full AR/AP model with invoice timing is EX scope.
```

**Named constant (`simulation_config.json → business`):**

| Constant | Default | Effect |
|---|---|---|
| `payment_lag_ticks` | 30 | Months of input costs treated as outstanding payables |

### 43.3 — Ab-2: Government Procurement / Contracting [V1 THIN]

**Decision: V1 thin model — procurement as special ObligationNode.**

A government contract award is modelled as an `ObligationNode` with `FavorType::contract_award` (new enum value) where:
- `creditor_npc_id` = the politician or official who awarded the contract
- The `original_value` field = contract monetary value
- The `ObligationNode.status` lifecycle governs payment (open → called_in when contract deliverable is due)

```cpp
// ENUM ADDITION to FavorType:
contract_award = 16,  // A government contract was awarded to the debtor's business.
                       // creditor = awarding official. debtor = player or NPCBusiness owner.
                       // original_value = contract value in currency.
                       // Evidence: documentary EvidenceToken at VisibilityScope::institutional
                       //   (procurement records) when award involves player.
                       // Corruption surface: if award_price > config.procurement.fair_market_multiple
                       //   × fair_market_estimate, a second token at higher actionability is generated.
```

**Named constant (`simulation_config.json → procurement`):**

| Constant | Default | Effect |
|---|---|---|
| `fair_market_multiple` | 1.30 | Contract price >30% above market estimate generates corruption evidence |

### 43.4 — Ab-3: Supply Chain Inventory / Buffer Stock [V1]

**Decision: V1 required — Facility must have an input buffer for the transit system to function correctly.**

The §18 transit system specifies "a 2-tick transit means the factory needs at least 2 ticks of buffer stock." Without a buffer struct on `Facility`, the production formula's `input_availability` check has no data to read. This is a structural gap that makes the transit system non-functional, not merely imprecise.

```cpp
// FIELD ADDITION to Facility:
std::map<uint32_t, float> input_buffer;
    // good_id → quantity currently held in facility.
    // Populated by: transit arrivals (tick step 2, DeferredWorkQueue transit_arrival).
    //   On transit_arrival: input_buffer[good_id] += quantity_arrived.
    // Consumed by: production tick (tick step 1).
    //   On production: input_buffer[good_id] -= recipe_input_quantity.
    //   Production only runs if input_buffer[good_id] >= recipe_input_quantity
    //   for ALL required inputs — this is the input_availability check.
    // Capacity: no hard cap in V1 (storage constraint is EX).
    //   Practical limit: NPCBusiness strategic decision avoids over-ordering
    //   via config.business.cash_surplus_months logic.

float buffer_target_ticks;
    // How many ticks of input the facility tries to hold in buffer.
    // Default: config.facility.default_buffer_target_ticks.
    // Drives the reorder quantity in the NPCBusiness purchasing decision:
    //   reorder_quantity = max(0,
    //       recipe_input_per_tick × buffer_target_ticks - input_buffer[good_id])
    // Set by player command for player-owned facilities;
    // set by NPC strategic decision for NPC facilities.
    // (TDD-SC2): Default = recipe.cycle_time × 2.0 (double-buffer). Criminal
    // facilities: recipe.cycle_time × 3.0 (triple-buffer for disruption risk).
    // Configurable per facility.
```

**Named constants (`simulation_config.json → facility`):**

| Constant | Default | Effect |
|---|---|---|
| `default_buffer_target_ticks` | 10 | Default ticks of input buffer each facility targets |

> Test: When a TransitShipment is intercepted (interception event at mid-transit), the destination facility's input_buffer depletes normally without replenishment. When input_buffer reaches 0 for a required input, production halts (output = 0) until a new shipment arrives or buffer is restocked.
>
> Seed setup: Factory with buffer_target_ticks = 60 (double-buffer). Single supplier with transit_time = 30 ticks. Intercept shipment at tick 15. Assert: factory continues production from buffer until ~tick 60, then halts. New order placed at tick 15 (reorder point), arrives tick 45. Production gap: ticks 60-45 = possible 15-tick gap if buffer was partially depleted.

### 43.5 — Ab-4: Seasonal Agricultural Price Effects [V1 — see §44]

**Decision: Reinstated as V1. Full specification in §44.**

Seasonal agriculture was initially cut but reinstated per design principle that systems diverge from realism only when technically necessary. Harvest-cycle seasonality is structurally required for the agricultural economy to produce realistic price dynamics across the 25-year simulation arc (including 2008 food price spike events). The V1 implementation uses the existing `RegionalMarket` equilibrium pricing to generate seasonal price variation — no new pricing machinery is required.

**Bootstrapper directive:** Farm facilities use `FarmSeasonState` (§44.1). Farm output is not continuous; it accumulates during the growing phase and releases at harvest. See §44 for the complete spec.

### 43.6 — Ab-5: Business Valuation / M&A Price Model [CLOSED]

**Decision: Closed by §5.1 (Pass 4 Session 1).**

The acquisition price formula `max(revenue_per_tick × TICKS_PER_MONTH × config.business.acquisition_revenue_multiple, 0.0f)` with a ±30% scene card negotiation overlay is the V1 M&A valuation model. No additional spec required.

### 43.7 — Ab-6: Consumer Debt / Household Leverage [CUT — V1 with future hook]

**Decision: CUT for V1 simulation mechanics. Future hook field added to `PopulationCohort`.**

Consumer credit cycles (expansion, overleveraging, contraction, default cascades) are macro-economic emergence properties that require a more complete banking model than V1 provides. Explicitly cutting mechanics; adding a dormant field for EX use.

```cpp
// FIELD ADDITION to PopulationCohort:
float debt_load;    // 0.0–1.0; ratio of cohort debt to annual income.
                     // V1: initialized to 0.0 at world load; not updated by any V1 mechanic.
                     //     Field exists as a future hook for the EX consumer credit system.
                     //     Do NOT write game logic that reads this field in V1.
                     // EX: Updated monthly from consumer credit module;
                     //     > 0.6 → consumer spending contracts;
                     //     > 0.8 → cohort defaults cascade into banking system.
```

**Bootstrapper directive:** Field is present; generate no V1 logic reading or writing `debt_load`. Mark with `// [EX]` comment in generated interface specs.

### 43.8 — P-2: Tax Collection Mechanism [CLOSED]

**Decision: Closed by §31 (Pass 4 Session 2).**

`WorkType::tax_collection_quarterly` with full pseudocode for national corporate tax, income tax by cohort, provincial business tax, and city revenue approximation is the V1 tax collection mechanism. Collection frequency is quarterly (90 ticks). Tax base for corporate is gross `revenue_per_tick × TICKS_PER_QUARTER`; income tax uses `PopulationCohort.median_income × cohort.size × cohort.employment_rate` as base with per-group effective rate modifiers.

### 43.9 — P-2 Supplemental: DrugType Formal Tax Treatment [V1]

One remaining gap in P-2: criminal revenue (`informal_cash`) is not subject to the quarterly income tax collection — it is unreported. The following rule is explicit:

```
// RULE: Criminal revenue tax treatment
// informal_cash on PlayerCharacter is NOT included in the income tax base.
// It is not reported to any tax authority.
// It becomes taxable ONLY after laundering (when it becomes player.wealth
// or NPCBusiness.cash via a LaunderingOperation).
// At that point it is indistinguishable from legitimate revenue —
// the tax system taxes it as formal income on the next quarterly collection.
// Laundering is therefore both an OPSEC action (hiding criminal origin)
// and a tax normalisation event.
```

---

## 44. Seasonal Agricultural Production [V1]

Farms operate on annual harvest cycles, not continuous per-tick output. A farm accumulates yield during its growing season and releases the full harvest to the `RegionalMarket` in a single batch. Seasonal price variation emerges from the existing equilibrium pricing formula reacting to supply bursts and inter-harvest depletion — no new pricing machinery is required.

This replaces the CUT ruling in §43.5.

### 44.1 — CropCategory Enum [V1]

Aligns with the `FARM_STRESS_SENSITIVITY` table in Commodities & Factories v2.3. Each recipe's CSV row carries a `crop_category` column.

```cpp
enum class CropCategory : uint8_t {
    annual_grain    = 0,  // Wheat, corn, rice, barley.
                           // One harvest per year in temperate climates.
                           // Two harvests per year in humid tropical (Af, Am, Aw)
                           //   with irrigation.
                           // Stress sensitivity: 0.55 (Commodities §Climate Effects).

    annual_oilseed  = 1,  // Soybean, rapeseed, sunflower.
                           // One harvest per year. Shorter growing season than grain.
                           // Stress sensitivity: 0.50.

    annual_fiber    = 2,  // Cotton.
                           // One harvest per year. Long growing season.
                           // Stress sensitivity: 0.60.

    perennial_tree  = 3,  // Coffee, rubber, cocoa, palm oil.
                           // Does NOT follow SeasonPhase cycle — produces continuously
                           //   with a seasonal yield_multiplier peak.
                           // Trees take 3–5 years to reach productive maturity (see §44.4).
                           // Stress sensitivity varies (coffee: 0.70, rubber: 0.65).

    sugarcane       = 4,  // Annual or ratoon cycle (replant every 3–5 years, ratoon
                           //   harvests annually in between). V1: modelled as annual.
                           // Stress sensitivity: 0.35.

    livestock       = 5,  // Cattle, pigs, poultry.
                           // Continuous output with seasonal feed-cost variation.
                           // Does NOT follow SeasonPhase cycle.
                           // Stress sensitivity: 0.30.

    timber          = 6,  // Softwood, hardwood plantation.
                           // Harvest cycle 8–30 years depending on species.
                           // V1 simplified: continuous output at slow rate.
                           // Stress sensitivity: 0.25.
};
```

**V1 seasonal cycle applies to:** `annual_grain`, `annual_oilseed`, `annual_fiber`, `sugarcane`.

**Continuous-with-modifier applies to:** `perennial_tree`, `livestock`, `timber` (see §44.5).

### 44.2 — SeasonPhase Enum [V1]

```cpp
enum class SeasonPhase : uint8_t {
    fallow   = 0,  // Off-season. No growth. Soil_health recovers at
                    // config.seasonal.fallow_soil_recovery_rate per tick.
                    // Duration: TICKS_PER_YEAR - growing_season_length - planting_ticks - harvest_ticks.

    planting = 1,  // Planting window (config.seasonal.planting_duration_ticks).
                    // Consumes seed_input from Facility.input_buffer.
                    // If seed not available → phase stays planting; farm cannot grow this year
                    //   → generates a supply gap that begins feeding into price the following
                    //      harvest period when supply fails to materialise.
                    // Transition to growing when seed consumed.

    growing  = 2,  // Active growth period. No output from the facility.
                    // Accumulates yield in FarmSeasonState.pending_harvest each tick:
                    //   daily_growth = base_growth_rate × drought_modifier × flood_modifier
                    //                  × soil_health × fertilizer_efficiency
                    //   pending_harvest += daily_growth
                    // Duration: growing_season_length_ticks (climate-zone dependent).
                    // Transition to harvest when growing season ends.

    harvest  = 3,  // Harvest window (config.seasonal.harvest_duration_ticks).
                    // Each tick during harvest window:
                    //   release_per_tick = pending_harvest / harvest_duration_ticks
                    //   RegionalMarket.supply(good_id, province_id) += release_per_tick
                    //   pending_harvest -= release_per_tick
                    // After harvest_duration_ticks: pending_harvest = 0; transition to fallow.
                    // Rationale for spreading harvest over multiple ticks rather than
                    //   one-tick burst: mechanised harvest takes days to weeks; spreading
                    //   over ~7 ticks prevents an artificial single-tick price crash.
};
```

### 44.3 — FarmSeasonState Struct [V1]

Added as a field on `Facility` for all facilities with `sector == agriculture` and `crop_category` in the annual-cycle set.

```cpp
struct FarmSeasonState {
    CropCategory    crop_category;
    SeasonPhase     current_phase;
    uint32_t        phase_started_tick;      // tick when current phase began
    uint32_t        growing_season_start;    // tick-of-year (0–364) when growing begins;
                                              // derived from province KoppenZone + CropCategory
                                              // at facility creation (see §44.3a)
    uint32_t        growing_season_length;   // ticks; from config.seasonal.growing_length table

    float           pending_harvest;         // accumulated yield not yet released to market;
                                              // units: same as recipe output_per_tick × TICKS_PER_YEAR.
                                              // Built during growing phase; depleted during harvest phase.

    float           base_growth_rate;        // output units per tick during growing phase.
                                              // = recipe.base_output_per_tick × TICKS_PER_YEAR
                                              //   / growing_season_length_ticks
                                              // Ensures total annual output equals the recipe's
                                              // intended annual production if no climate stress.
                                              // Re-derived when recipe or facility tech_tier changes.

    uint32_t        harvest_remaining_ticks; // countdown during harvest phase

    float           annual_yield_last;       // pending_harvest at start of last harvest;
                                              // used by NPCBusiness strategic decision
                                              // and UI display.
    bool            seed_planted;            // true once planting phase successfully consumed seed input.
    uint16_t        years_same_crop;         // consecutive years with same CropCategory;
                                              // monoculture penalty to soil_health applies above
                                              // config.seasonal.monoculture_penalty_threshold years.
};

// FIELD ADDITION to Facility:
std::optional<FarmSeasonState> farm_season_state;
    // Present only for agriculture-sector facilities with annual cycle.
    // Absent for perennial_tree, livestock, timber categories (§44.5).
    // Absent for all non-agriculture facilities.
```

### 44.3a — Growing Season Lookup [V1]

Growing season start (tick-of-year) and length by `KoppenZone` × `CropCategory`. Northern Hemisphere baseline; Southern Hemisphere offset by 182 ticks (6 months). Latitude-sign determines hemisphere from `GeographyProfile.latitude`.

```
// config.seasonal.growing_calendar[koppen_zone][crop_category] → {start_tick, length_ticks}
// All values in ticks (1 tick = 1 day)

Annual grain (annual_oilseed similar, oilseed ≈ grain − 30 days):
  Af, Am, Aw (humid tropical):   start=0,   length=300  // near-continuous with irrigation; 2 cycles modelled as one long season
  BWh, BSh (hot arid):           start=270, length=120  // winter/cool-season sowing
  BWk, BSk (cold arid):          start=90,  length=150
  Cfa, Cfb (oceanic/humid temp): start=90,  length=200
  Csa, Csb (Mediterranean):      start=270, length=180  // winter wheat dominant
  Cwa (subtropical monsoon):     start=0,   length=180  // winter dry; monsoon sowing
  Dfa, Dfb (humid continental):  start=100, length=180
  Dfc, Dfd (subarctic):          start=120, length=120
  ET, EF:                         no agriculture (arable_land_fraction ≈ 0; skip)

Annual fiber (cotton): same start as grain for zone, length = min(zone_grain_length, 210)
Sugarcane: Af/Am/Aw: start=0, length=330. Cfa/Cwa/BSh: start=60, length=270. Others: not viable (length=0)

// Zones with length=0 for a given crop: farm cannot operate that crop in that zone.
// Facility creation validation: if growing_season_length == 0 → facility rejected.
```

**Named constants (`simulation_config.json → seasonal`):**

| Constant | Default | Effect |
|---|---|---|
| `planting_duration_ticks` | 7 | Days in planting phase; seed must be available |
| `harvest_duration_ticks` | 14 | Days over which harvest is released to market |
| `fallow_soil_recovery_rate` | 0.003 | Soil health gain per fallow tick |
| `monoculture_penalty_threshold` | 3 | Years of same crop before soil health penalty applies |
| `monoculture_soil_penalty_rate` | 0.002 | Additional soil health loss per tick above threshold |
| `southern_hemisphere_offset` | 182 | Tick offset applied to NH growing_season_start for SH provinces |

### 44.4 — Per-Tick Farm Integration [V1]

Runs at tick step 1 (production) for all facilities with `farm_season_state` present.

```
FUNCTION tick_farm_facility(facility: Facility, province: Province):
    state = facility.farm_season_state
    tick_of_year = current_tick % TICKS_PER_YEAR

    // --- Phase transitions ---
    planting_start = state.growing_season_start - config.seasonal.planting_duration_ticks
    IF tick_of_year == (planting_start % TICKS_PER_YEAR):
        IF state.current_phase == fallow:
            state.current_phase = planting
            state.phase_started_tick = current_tick
            state.seed_planted = false
            state.pending_harvest = 0.0  // reset; last year's harvest already released

    IF state.current_phase == planting:
        // Attempt to consume seed from input_buffer
        IF facility.input_buffer[seed_good_id] >= recipe.seed_input_per_ha:
            facility.input_buffer[seed_good_id] -= recipe.seed_input_per_ha
            state.seed_planted = true
            state.current_phase = growing
            state.phase_started_tick = current_tick
        // else: remain in planting; seed not yet available; supply disruption

    ELIF state.current_phase == growing:
        IF state.seed_planted:
            // Accumulate yield
            fertilizer_efficiency = compute_fertilizer_efficiency(facility)
            daily_growth = state.base_growth_rate
                × province.conditions.drought_modifier
                × province.conditions.flood_modifier
                × facility.soil_health
                × fertilizer_efficiency
            state.pending_harvest += daily_growth

        // Soil health: monoculture check
        IF state.years_same_crop > config.seasonal.monoculture_penalty_threshold:
            facility.soil_health = max(0.5,
                facility.soil_health - config.seasonal.monoculture_soil_penalty_rate)

        // Transition to harvest at end of growing season
        ticks_growing = current_tick - state.phase_started_tick
        IF ticks_growing >= state.growing_season_length:
            state.current_phase = harvest
            state.annual_yield_last = state.pending_harvest
            state.harvest_remaining_ticks = config.seasonal.harvest_duration_ticks
            state.phase_started_tick = current_tick

    ELIF state.current_phase == harvest:
        // Release pending_harvest to market in equal daily instalments
        release = state.pending_harvest / state.harvest_remaining_ticks
        RegionalMarket.supply(facility.output_good_id, province.id) += release
        state.pending_harvest -= release
        state.harvest_remaining_ticks -= 1
        IF state.harvest_remaining_ticks == 0:
            state.current_phase = fallow
            state.phase_started_tick = current_tick
            state.years_same_crop += 1  // track monoculture

    ELIF state.current_phase == fallow:
        // Soil recovery
        facility.soil_health = min(1.0,
            facility.soil_health + config.seasonal.fallow_soil_recovery_rate)
```

**Crop rotation (player or NPC action `set_crop_category`):** Sets `FarmSeasonState.crop_category` for the next planting phase. If changed from previous year: `years_same_crop = 0`; monoculture penalty resets. The trade-off is real: rotating crops sacrifices yield specialisation and requires seed variety management for reduced price at optimal harvest window.

### 44.5 — Continuous-with-Modifier Categories [V1]

`perennial_tree`, `livestock`, and `timber` do not use `FarmSeasonState`. They use the original per-tick output formula from the Commodities doc, with one addition: a `seasonal_yield_multiplier` per tick derived from tick-of-year, replacing the implicit flat rate.

```cpp
// FIELD ADDITION to Facility (non-FarmSeasonState agricultural facilities):
float seasonal_yield_multiplier;
    // 0.5–1.3; recomputed monthly from tick_of_year + CropCategory profile.
    // Perennial tree (coffee, rubber): peaks at harvest month for the zone; 0.5 in off-peak.
    // Livestock: 0.85–1.05 (summer heat stress lowers output; spring flush for dairy).
    // Timber: constant 1.0 (multi-year cycle abstracted to flat rate in V1).
    //
    // Monthly update formula:
    //   seasonal_yield_multiplier = config.seasonal.perennial_base
    //       + config.seasonal.perennial_amplitude
    //         × cos(2π × (tick_of_year - peak_tick[crop_category][koppen_zone])
    //               / TICKS_PER_YEAR)
    // This is a cosine seasonal curve: 1 peak, 1 trough per year.

// Per-tick output for these facilities (replaces Commodities farm_output_actual):
facility_output_per_tick = recipe.base_output_per_tick
    × seasonal_yield_multiplier
    × province.conditions.drought_modifier
    × province.conditions.flood_modifier
    × facility.soil_health
    × fertilizer_efficiency
    × (1.0 - province.climate.climate_stress_current
           × FARM_STRESS_SENSITIVITY[crop_category])
// Output delivered directly to RegionalMarket.supply each tick (same as before).
```

**Named constants (`simulation_config.json → seasonal`):**

| Constant | Default | Effect |
|---|---|---|
| `perennial_base` | 0.85 | Cosine curve baseline (trough = base − amplitude) |
| `perennial_amplitude` | 0.25 | Cosine curve amplitude (peak = base + amplitude = 1.10) |

### 44.6 — Price Seasonality Emergence [V1]

No change to `RegionalMarket` pricing formula required. The mechanism:

1. During growing phase (~6 months): farm adds nothing to `RegionalMarket.supply`. Supply from stored grain (purchased and held as `CommodityPosition` by NPC traders or remaining from last harvest) depletes as demand is served. Supply ratio drops → equilibrium price rises.

2. At harvest: burst of supply added over 14 ticks. Supply ratio spikes → equilibrium price falls sharply. This is the harvest price dip.

3. Rational NPC traders (speculative buyers) buy `CommodityPosition` at harvest lows and close positions in the pre-harvest peak. This is the only behaviour needed to make agricultural commodity trading emergent — the price signal does the work.

4. Supply gaps from missed planting (seed shortage or drought) → supply never materialises → price doesn't fall at the expected harvest time → price spike. This is the food crisis emergence mechanism.

**[SCENARIO: scenario_seasonal_price_cycle]**
> Test: A province with grain farm facilities shows a regular annual price cycle: spot price peak before harvest, trough at harvest, mid-range during growing season.
> Seed setup: Province has ≥3 grain farms, no imports, no trade routes active; world start in fallow phase.
> Run length: 2 × TICKS_PER_YEAR.
> Assertion: In each of the 2 cycles, `RegionalMarket.spot_price(annual_grain, province)` is at least 20% higher in the pre-harvest quarter than in the harvest quarter.

**[SCENARIO: scenario_supply_gap_price_spike]**
> Test: A planting failure (seed shortage) causes a supply gap and price spike in the affected province.
> Seed setup: Province grain farm in planting phase; `input_buffer[seed_good_id] = 0`; no seed arriving via transit.
> Run length: TICKS_PER_YEAR.
> Assertion: Farm stays in `planting` phase, never transitions to `growing`; `RegionalMarket.spot_price(annual_grain, province)` at end of expected harvest window ≥ 1.35 × pre-scenario baseline.

### 44.7 — Commodities Doc Cross-Reference Note [V1]

The `farm_output_actual` formula in Commodities & Factories v2.3 §Climate Effects applies to **continuous-with-modifier** crops only (`perennial_tree`, `livestock`, `timber` — §44.5). For annual-cycle crops (`annual_grain`, `annual_oilseed`, `annual_fiber`, `sugarcane`), the `farm_output_actual` formula is replaced by the `pending_harvest` accumulation in §44.4. The climate modifiers (`drought_modifier`, `flood_modifier`, `soil_health`, `fertilizer_efficiency`) are identical in both paths; only the accumulation architecture differs.

Companion document update required: Commodities & Factories v2.3 §Climate Effects heading should be annotated: *"Continuous-with-modifier crops only. Annual-cycle crops: see TDD §44.4."*

---

## Change Log — Pass 4, Session 1 (Gap Audit Fixes: Player Business, PopulationCohort, Labor Market)

### Summary

First session of Pass 4. Addresses the highest-priority items from the comprehensive gap audit (March 2026). Closes S-2, P-3, S-3, and BLOCKER-D.

### Changes

| ID | Location | Change |
|---|---|---|
| S-2 | §5 after NPCBusiness prose | Added §5.1 Player-Owned Business Architecture. Canonical rule: player business = NPCBusiness with `owner_id = player_id`. Added `owner_id: uint32_t` field to NPCBusiness with full comment. Documented dispatch rule (player businesses dispatch on command; `dispatch_day_offset` unused). Closes S-2. |
| P-3 | §5.1 (new) | Specified monthly profit sweep algorithm: surplus above `config.business.cash_surplus_months × monthly_cost_estimate` transfers from `NPCBusiness.cash` to `PlayerCharacter.wealth`. Same threshold constant as NPC strategic decision matrix — no special-casing. Closes P-3. |
| P-4 | §5.1 (new) | Specified V1 thin acquisition model: `acquisition_price = max(revenue_per_tick × TICKS_PER_MONTH × config.business.acquisition_revenue_multiple, 0.0f)`. Player acquisition triggers scene card negotiation overlay (±30%). NPC-to-NPC acquisition resolved in strategic decision matrix. Named constants: `acquisition_revenue_multiple = 6.0`, `bankruptcy_grace_ticks = 10`. Partially closes P-4. |
| S-3 | §14 after DemographicGroup enum | Added `PopulationCohort` struct (8 fields: group, size, median_income, education_level, employment_rate, skill_supply map, political_lean, grievance_contribution). Added `RegionCohortStats` struct (cohorts map + 3 province-level aggregates: total_population, mean_income, gini_coefficient). Added group-skill prior table. Defined update cadence (monthly via DeferredWorkQueue WorkType::cohort_monthly_update). Named constants: `cohort_income_update_rate = 0.05`, `cohort_employment_update_rate = 0.02`. `RegionCohortStats` was previously referenced by `Province.cohort_stats` without definition — that missing struct is now resolved. Closes S-3. |
| BLOCKER-D | §30 (new) | Added full Labor Market and Hiring System section. Contents: `HiringChannel` enum (3 values), `JobPosting` struct, `WorkerApplication` struct, `regional_wage_by_skill` field added to Province, wage market equilibrium formula (monthly update), `generate_applicant_pool` algorithm (channel-specific NPC sourcing, background stub generation, reputation penalty), `employer_reputation` function (derived from worker_satisfaction of current and former employees), interview as scene card mechanic, hire action with memory entry generation, NPC simplified hire path, firing consequences, voluntary departure probability formula, tick integration table (4 WorkTypes). All ambiguities resolved: interview = scene card (consistent with §9). Named constants block in `simulation_config.json → labor`: 10 constants. Closes BLOCKER-D. |

### New Sections Added

| Section | Title |
|---|---|
| §5.1 | Player-Owned Business Architecture [V1] |
| §14 — PopulationCohort | PopulationCohort and RegionCohortStats [V1] |
| §30 | Labor Market and Hiring System [V1] |

### NPCBusiness Fields Added

| Field | Type | Note |
|---|---|---|
| `owner_id` | `uint32_t` | 0 = independent; player_id or npc_id if owned |

### Province Fields Added

| Field | Type | Note |
|---|---|---|
| `regional_wage_by_skill` | `std::map<SkillDomain, float>` | Per-domain wage market rate; updated monthly |

### Named Constants Added

All to `simulation_config.json`:

| Constant | Config path | Default |
|---|---|---|
| `acquisition_revenue_multiple` | business | 6.0 |
| `bankruptcy_grace_ticks` | business | 10 |
| `cohort_income_update_rate` | labor | 0.05 |
| `cohort_employment_update_rate` | labor | 0.02 |
| `wage_adjustment_rate` | labor | 0.03 |
| `wage_floor` | labor | 0.01 |
| `wage_ceiling_multiplier` | labor | 5.0 |
| `pool_size_public` | labor | 12 |
| `pool_size_professional` | labor | 5 |
| `pool_size_referral` | labor | 3 |
| `reputation_threshold` | labor | 0.3 |
| `reputation_pool_penalty_scale` | labor | 8.0 |
| `salary_premium_per_reputation_point` | labor | 0.5 |
| `voluntary_departure_threshold` | labor | 0.35 |
| `departure_base_rate` | labor | 0.08 |

### Audit Issues Closed This Session (Session 1)

| Audit ID | Issue |
|---|---|
| S-2 | Player Business Ownership Architecture |
| P-3 | Player Business Financial Model (profit transfer) — superseded by Session 2 |
| P-4 | Business Acquisition Mechanics (V1 thin model) |
| S-3 | Background Population Cohort Struct |
| BLOCKER-D | Labor Market / Hiring System |

### Audit Issues Remaining After Session 1

BLOCKER-A, BLOCKER-B, BLOCKER-C, BLOCKER-E, BLOCKER-F, BLOCKER-G, BLOCKER-H, BLOCKER-I, P-1, P-2, P-5, P-6, P-7, S-1, S-4, Ab-1 through Ab-6.

### Header Updated

- Title updated to v24, Pass 4 Session 1 complete.

---

## Change Log — Pass 4, Session 2 (Executive Compensation Realistic Model + BLOCKER-G Multi-Level Government Budget)

### Summary

Replaces the simplified P-3 profit-sweep model with a realistic executive compensation system covering four business scales and five extraction mechanisms. Adds §31 Multi-Level Government Budget System (BLOCKER-G), modelling city, provincial, and national budgets with independent revenue collection, intergovernmental transfers, spending consequences, and player authority strictly gated by held office.

### Changes

| ID | Location | Change |
|---|---|---|
| P-3 revised | §5.1a (replaces Session 1 profit sweep) | Full executive compensation model. Added `BusinessScale` enum (4 values: micro/small/medium/large) derived from monthly revenue thresholds. Added `CompensationMechanism` enum (5 values: owners_draw, salary_only, salary_bonus, salary_dividend, full_package). Added `EquityGrant` struct (cliff/vest schedule, strike price for options vs RSU). Added `ExecutiveCompensation` struct (salary_per_tick, bonus_rate, dividend_yield_target, equity_grants). Added tick integration table (5 events × cadence). Deferred salary liability field and wage_theft memory entry if salary unpaid > threshold. Board approval mechanic at medium/large: `BoardComposition` struct, independence_score, quarterly approval logic, CEO salary market multiples by scale. Visibility rules per mechanism (owners_draw above threshold → suspicious_transaction evidence token; full_package at listed company → public_info disclosure). OPSEC implication stated: compensation structure is an investigative signal. 11 named constants to `simulation_config.json → business`. Closes P-3 (supersedes Session 1 version). |
| BLOCKER-G | §31 (new) | Full Multi-Level Government Budget System. `GovernmentLevel` enum (city/province/national). `SpendingCategory` enum (8 categories). `GovernmentBudget` struct (revenue fields, spending_allocations map, spending_actual map, balance, debt, fiscal health indicators). `CityBudget` struct. Field additions: `national_budget` on Nation, `provincial_budget` + `city_budgets` on Province, `provincial_business_tax_rate` + `property_tax_revenue_estimate` on Province. Tax collection pseudocode per level (quarterly, DeferredWorkQueue WorkType::tax_collection_quarterly): national corporate + income tax by cohort (with per-group effective rate modifiers); provincial business tax + property tax estimate; city approximation as fraction of provincial. Intergovernmental transfer logic (national→provincial→city) with political consequence on unequal distribution. Spending consequence table (6 categories → province/city condition fields). Infrastructure decay formula. Player budget authority table by PoliticalOfficeType (6 office types × 3 authority columns) — access strictly limited to held office. Budget manipulation evidence generation for discretionary diversion. Fiscal stress/crisis event triggers (quarterly check, debt_to_revenue_ratio thresholds → fiscal_pressure_warning and fiscal_crisis consequences, cash insolvency handling). 11 named constants to `simulation_config.json → fiscal`. Closes BLOCKER-G. |

### New Sections Added

| Section | Title |
|---|---|
| §5.1a | Executive Compensation Model [V1] |
| §31 | Multi-Level Government Budget System [V1] |

### New Enums

`BusinessScale`, `CompensationMechanism`, `GovernmentLevel`, `SpendingCategory`

### New Structs

`EquityGrant`, `ExecutiveCompensation`, `BoardComposition`, `GovernmentBudget`, `CityBudget`

### Nation Fields Added

| Field | Type |
|---|---|
| `national_budget` | `GovernmentBudget` |

### Province Fields Added

| Field | Type |
|---|---|
| `provincial_budget` | `GovernmentBudget` |
| `city_budgets` | `std::vector<CityBudget>` |
| `provincial_business_tax_rate` | `float` |
| `property_tax_revenue_estimate` | `float` |

### Named Constants Added

`simulation_config.json → business`: `scale_micro_ceiling`, `scale_small_ceiling`, `scale_medium_ceiling`, `reasonable_salary_floor`, `board_approval_bonus_threshold`, `board_rubber_stamp_threshold`, `board_challenge_threshold`, `deferred_salary_max_ticks`, `draw_reporting_threshold` (9 new; `acquisition_revenue_multiple` and `bankruptcy_grace_ticks` carried from Session 1).

`simulation_config.json → fiscal`: `debt_warning_ratio`, `debt_crisis_ratio`, `infrastructure_decay_per_quarter`, `infrastructure_investment_scale`, `city_revenue_fraction_of_province`, `national_to_province_distribution_modifier`, `corruption_evidence_threshold`, `cohort_tax_rate_modifier.*` (4 group variants) — 11 new constants.

### Audit Issues Closed This Session (Session 2)

| Audit ID | Issue |
|---|---|
| P-3 | Player Business Financial Model — realistic executive compensation model (supersedes Session 1 version) |
| BLOCKER-G | Governing Budget / Fiscal System |

### Audit Issues Remaining After Session 2

BLOCKER-A (Money Laundering), BLOCKER-B (Real Estate Market), BLOCKER-C (Banking / Loan System), BLOCKER-E (Alternative Identity System), BLOCKER-F (Informant System), BLOCKER-H (Protection Rackets), BLOCKER-I (Weapons Trafficking), P-1, P-2, P-5, P-6, P-7, S-1, S-4, Ab-1 through Ab-6.

### Header Updated

- Title updated to v25, Pass 4 Session 2 complete.

---

## Change Log — Pass 4, Session 3 (BLOCKER-A Money Laundering System)

### Summary

Full money laundering system spec. Formally declares `EvidenceType` enum (previously referenced in `EvidenceToken` without definition). Adds §32 with all six GDD-described laundering methods, per-method evidence profiles, FIU pattern detection, shell company chain mechanics, crypto mixer infrastructure, and field additions to `CriminalOrganization`, `WorldState`, and `PlayerCharacter`.

### Changes

| ID | Location | Change |
|---|---|---|
| EvidenceType | §32.1 (new) | Formally declared `EvidenceType` enum: financial, testimonial, documentary, physical. Was used in `EvidenceToken.type` without definition — that missing enum is now resolved. |
| BLOCKER-A | §32 (new) | Full Money Laundering System. Contents: `LaunderingMethod` enum (6 values matching GDD §12.5); `LaunderingOperation` struct (12 fields: actor_id, method, dirty/clean amounts, rate, loss rate, destination, chain list, mixer ref, evidence tracking, pause/complete state); tick step 6 integration pseudocode; per-method evidence and capacity profile table (6 rows × 5 columns); shell company chain constraints and NPC informant risk linkage; `MixerNode` struct (traceability, operator NPC, FIU-flagged); crypto evidence generation formula (investigator-skill-scaled); FIU monthly pattern analysis pseudocode (3 patterns: structuring, round-trip chain, traffic/revenue discrepancy); `FIUPatternResult` struct; `CriminalOrganization.active_laundering_operation_ids` field addition; `WorldState.laundering_operations` and `WorldState.mixer_nodes` field additions; `PlayerCharacter.informal_cash` field addition; 16 named constants to `simulation_config.json → laundering`. Closes BLOCKER-A. |

### New Sections Added

| Section | Title |
|---|---|
| §32.1 | EvidenceType Enum [V1] (formal declaration) |
| §32.2 | Laundering Method Enum [V1] |
| §32.3 | LaunderingOperation [V1] |
| §32.4 | Per-Method Evidence and Capacity Profiles [V1] |
| §32.5 | Shell Company Chain [V1] |
| §32.6 | Crypto Mixing [V1] |
| §32.7 | FIU Detection System [V1] |
| §32.8 | CriminalOrganization / WorldState / PlayerCharacter Field Additions [V1] |
| §32.9 | Named Constants [V1] |

### New Enums

`EvidenceType` (formally declared), `LaunderingMethod`

### New Structs

`LaunderingOperation`, `MixerNode`, `FIUPatternResult`

### Field Additions

| Struct | Field | Type |
|---|---|---|
| `CriminalOrganization` | `active_laundering_operation_ids` | `std::vector<uint32_t>` |
| `WorldState` | `laundering_operations` | `std::vector<LaunderingOperation>` |
| `WorldState` | `mixer_nodes` | `std::vector<MixerNode>` |
| `PlayerCharacter` | `informal_cash` | `float` |

### Named Constants Added

`simulation_config.json → laundering`: `rate_structuring`, `rate_chain_per_node`, `rate_trade`, `rate_crypto`, `rate_commingle_max`, `commingle_capacity_fraction`, `max_chain_depth`, `shell_company_formation_cost`, `mixer_base_traceability`, `crypto_evidence_skill_divisor`, `structuring_deposit_count_threshold`, `traffic_revenue_ratio_threshold`, `fiu_token_threshold`, `fiu_meter_fill_scale`, `org_capacity_multiplier`, `structuring_token_interval` — 16 constants.

### Architectural Notes

- **Real estate laundering** (§32.4) is gated behind BLOCKER-B (PropertyListing struct). The method enum value and evidence profile are specced; the operation can be queued but cannot complete until BLOCKER-B resolves. Bootstrapper must generate a compile-time guard for this dependency.
- **`accelerate_laundering: true`** in the criminal org strategic decision matrix (§13) now has a concrete implementation target: creates a new `LaunderingOperation` with `method = shell_company_chain` and rate derived from the org's existing shell company infrastructure.
- **`EvidenceType`** was previously used in `EvidenceToken.type` as a comment annotation; it is now a formally declared enum. No struct changes required — `EvidenceToken.type` already had the correct field name and type.

### Audit Issues Closed This Session (Session 3)

| Audit ID | Issue |
|---|---|
| BLOCKER-A | Money Laundering System |

### Audit Issues Remaining After Session 3

BLOCKER-B (Real Estate Market), BLOCKER-C (Banking / Loan System), BLOCKER-E (Alternative Identity System), BLOCKER-F (Informant System), BLOCKER-H (Protection Rackets), BLOCKER-I (Weapons Trafficking), P-1, P-2, P-5, P-6, P-7, S-1, S-4, Ab-1 through Ab-6.

### Header Updated

- Title updated to v26, Pass 4 Session 3 complete.

---

## Change Log — Pass 4, Session 4 (All Remaining BLOCKERs: Real Estate, Banking, Alternative Identity, Informant, Protection Rackets, Weapons Trafficking)

### Summary

Closes all nine audit BLOCKERs across two sessions (Sessions 3–4). Adds six new sections (§33–38). All primary BLOCKER items from the March 2026 audit are now resolved. Remaining open items: seven PRECISION gaps (P-1, P-2, P-5, P-6, P-7), one SCOPE item (S-1), one resolved ambiguity (S-4 closed inline), and six ABSENT SYSTEMS decisions (Ab-1 through Ab-6).

### Changes

| ID | Location | Change |
|---|---|---|
| BLOCKER-B | §33 (new) | Real Estate Market. `PropertyType` enum (residential/commercial/industrial — closes S-4 ambiguity). `PropertyListing` struct (12 fields: type, province, owner, asking/market price, rental yield, rented, tenant, launder_eligible, purchase history). `market_value` formula driven by province prosperity, infrastructure, criminal_dominance_index, and active laundering pressure. Monthly `asking_price` convergence. Transaction mechanics (buy scene card ±15%, sell capital gain, automatic rental income per tick). `WorldState.property_listings` field addition. `Province.avg_property_value` field addition (referenced by GDD §12.12 scenario test). 13 named constants to `simulation_config.json → realestate`. Unblocks real estate LaunderingMethod from §32.4. Closes BLOCKER-B and S-4. |
| BLOCKER-C | §34 (new) | Banking and Loan System. `CreditProfile` struct (credit_score, total_debt_outstanding, debt_service_per_tick, debt_to_income_ratio). Field additions: `CreditProfile credit_profile` and `std::vector<uint32_t> loan_ids` to both `PlayerCharacter` and `NPCBusiness`. `LoanPurpose` enum (4 values: business_capital, property_purchase, personal, criminal_informal). `LoanRecord` struct (12 fields: borrower/lender, purpose, principal, outstanding balance, interest rate, repayment, originated/maturity ticks, default flag, collateral). Per-tick repayment integration with credit score update. `evaluate_loan` function (3 denial conditions, interest rate derivation, collateral discount). Credit denial player action (4 options: accept, add collateral, leverage obligation to override, seek informal). Player-owned bank coercion via `deny_credit` action. `WorldState.active_loans` field addition. 11 named constants to `simulation_config.json → banking`. Closes BLOCKER-C. |
| BLOCKER-E | §35 (new) | Alternative Identity System. `IdentityStatus` enum (active/burned/compromised). `AlternativeIdentity` struct (9 fields: real_actor_id, alias, province, status, timestamps, documentation_quality, attributed_evidence_token_ids, witness_npc_ids). Evidence attribution rule: actions under active alt identity → `token.subject_npc_id = alt_identity_id`. Identity switching mechanic. Burn mechanic: all attributed tokens get `identity_linked = false`; witness list becomes latent liability. `identity_link_discovery` trigger conditions (arrested witness, forensic analysis, rival actor). On discovery: `KnowledgeEntry{type: identity_link}` on LE NPC KnowledgeMap; all attributed tokens become actionable against real actor. `PlayerCharacter.alt_identities` and `PlayerCharacter.active_alt_identity_id` field additions. 4 named constants to `simulation_config.json → identity`. Closes BLOCKER-E. |
| BLOCKER-F | §36 (new) | Informant System. `InformantStatus` enum (potential/cooperating/protected/eliminated). `InformantRecord` struct (6 fields: npc, arresting LE NPC, status, flip probability, timestamps, disclosed knowledge). `compute_flip_probability` formula: base rate + risk_tolerance_factor + trust_factor − mutual_incrimination_suppression − compartmentalization_bonus; capped at `max_flip_probability`. Per-tick cooperation roll when potential. Cooperation consequence: KnowledgeMap entries → high-actionability EvidenceTokens held by LE NPC; InvestigatorMeter fill per disclosure. Four player countermeasures: pay_silence (ObligationNode + evidence trail), threaten_silence (risk_tolerance +0.10), relocate_witness (transit + LE pressure drop), eliminate (NPC death + violence multiplier). `WorldState.informant_records` field addition. 9 named constants to `simulation_config.json → informant`. Closes BLOCKER-F. |
| BLOCKER-H | §37 (new) | Protection Rackets. `RacketStatus` enum (active/refused/lapsed/expelled). `RacketEscalationStage` enum (demand_issued/warning/property_damage/violence/abandonment). `ProtectionRacket` struct (8 fields: org, target business, demand_per_tick, status, escalation stage, timestamps, grievance contribution). Per-tick collection and escalation logic. Refusal probability formula (profile-dependent, CDI-modulated). Escalation consequence table (warning → intimidation scene card; property_damage → facility_incident at 15 ticks; violence → personnel_violence + InvestigatorMeter ×3 at 30 ticks; abandonment → bankruptcy/exit at 60 ticks). `NPCBusiness.protection_cost_per_tick` field addition (referenced by GDD §12.12 scenario test). `WorldState.protection_rackets` field addition. 4 named constants to `simulation_config.json → racket`. Closes BLOCKER-H. |
| BLOCKER-I | §38 (new) | Weapons Trafficking. `WeaponType` enum (small_arms/ammunition/heavy_weapons/converted_legal). Commodity additions to `goods.csv` criminal goods table: `small_arms`, `ammunition`, `arms_cache` (V1); `heavy_weapons` (EX, import-only). Supply channel 1: legal market diversion via `negotiate_diversion` player action (requires ownership or obligation with controlling NPC; documentary evidence per shipment). Supply channel 2: corrupt official procurement via `procure_from_corrupt_source` (requires military/LE NPC obligor; physical + testimonial evidence). Market price formula: base price × territorial conflict demand modifier / supply. Downstream evidence chain: weapon used in crime → `physical` EvidenceToken against last known owner, traceable to player via identity links. Heavy weapons arms embargo escalation: InvestigatorMeter spike + `embargo_investigation` consequence (national-level; not suppressible by local corruption). `NPCBusiness.diversion_fraction` field addition. 7 named constants to `simulation_config.json → weapons`. Closes BLOCKER-I. |
| S-4 | §33.1 | Residential vs. Commercial Property ambiguity resolved: `PropertyType` enum with `residential`, `commercial`, `industrial` values on `PropertyListing`. Single struct, three types. Closes S-4. |

### New Sections Added

| Section | Title |
|---|---|
| §33 | Real Estate Market [V1] |
| §34 | Banking and Loan System [V1] |
| §35 | Alternative Identity System [V1] |
| §36 | Informant System [V1] |
| §37 | Protection Rackets [V1] |
| §38 | Weapons Trafficking [V1] |

### New Enums

`PropertyType`, `LoanPurpose`, `IdentityStatus`, `InformantStatus`, `RacketStatus`, `RacketEscalationStage`, `WeaponType`

### New Structs

`PropertyListing`, `CreditProfile`, `LoanRecord`, `AlternativeIdentity`, `InformantRecord`, `ProtectionRacket`

### Field Additions

| Struct | Field | Type |
|---|---|---|
| `PlayerCharacter` | `credit_profile` | `CreditProfile` |
| `PlayerCharacter` | `loan_ids` | `std::vector<uint32_t>` |
| `PlayerCharacter` | `alt_identities` | `std::vector<AlternativeIdentity>` |
| `PlayerCharacter` | `active_alt_identity_id` | `uint32_t` |
| `NPCBusiness` | `credit_profile` | `CreditProfile` |
| `NPCBusiness` | `loan_ids` | `std::vector<uint32_t>` |
| `NPCBusiness` | `protection_cost_per_tick` | `float` |
| `NPCBusiness` | `diversion_fraction` | `float` |
| `Province` | `avg_property_value` | `float` |
| `WorldState` | `property_listings` | `std::vector<PropertyListing>` |
| `WorldState` | `active_loans` | `std::vector<LoanRecord>` |
| `WorldState` | `informant_records` | `std::vector<InformantRecord>` |
| `WorldState` | `protection_rackets` | `std::vector<ProtectionRacket>` |

### Audit Issues Closed This Session (Session 4)

| Audit ID | Issue |
|---|---|
| BLOCKER-B | Real Estate Market |
| BLOCKER-C | Banking and Loan System |
| BLOCKER-E | Alternative Identity System |
| BLOCKER-F | Informant System |
| BLOCKER-H | Protection Rackets |
| BLOCKER-I | Weapons Trafficking |
| S-4 | Residential vs. Commercial Property (resolved inline in §33.1) |

### Audit Issues Remaining After Session 4

P-1 (Drug Economy Type System), P-2 (Tax Collection Mechanism — partially addressed in §31), P-5 (Antitrust Trigger Model), P-6 (Criminal R&D / Drug Scheduling Race), P-7 (Commodities Trading Player Action), S-1 (NPC-to-NPC Obligation V1 Boundary), Ab-1 through Ab-6 (Absent Systems requiring explicit V1/EX/CUT decisions).

### Header Updated

- Title updated to v27, Pass 4 Session 4 complete.

---

## Change Log — Pass 4, Session 5 (Precision Gaps, Scope Decision, Absent System Rulings)

### Summary

Closes all remaining open audit items from the March 2026 audit. Adds §§39–43. No BLOCKER items remain; this session resolves the seven PRECISION gaps, one SCOPE item, and six ABSENT system decisions. The TDD now has an explicit documented position on every audit finding.

### Changes

| ID | Location | Change |
|---|---|---|
| P-1 | §39 (new) | Drug Economy Type System. `DrugType` enum (6 values: cannabis, methamphetamine, synthetic_opioid, designer_drug as V1; cocaine and heroin as EX reserved). Maps each value to its `good_key` in the informal `RegionalMarket`. `Province.drug_legalization_status` field addition (`std::map<DrugType, bool>`): legalisation toggles price layer from informal to formal; modified by legislation. `DrugMarketTier` enum (wholesale / retail): wholesale prices at 0.45× retail spot; retail is the spot_price reference. `NPCBusiness.drug_market_tier` field addition for criminal-sector businesses. 1 named constant to `simulation_config.json → drug`. Closes P-1. |
| P-5 | §41 (new) | Antitrust Trigger Model. Monthly `WorkType::antitrust_monthly_check` loop across all province/good/actor combinations in formal market. Tier 1 (≥0.40 share): fills antitrust-specialist regulator NPC's `InvestigatorMeter`; generates `documentary` EvidenceToken when meter crosses formal-inquiry threshold. Tier 2 (≥0.70 share = `DOMINANT_PRICE_MOVER_THRESHOLD`): accumulates `antitrust_proposal_pressure` on `RegionalPoliticalState`; auto-generates `LegislativeProposal` of type antitrust when pressure ≥ 0.50. `RegionalPoliticalState.antitrust_proposal_pressure` field addition. 5 named constants to `simulation_config.json → antitrust`. Closes P-5. |
| P-6 | §40 (new) | Designer Drug Scheduling Race. `SchedulingStatus` enum (unscheduled / review_initiated / scheduled). `DesignerDrugRecord` struct (11 fields: compound_key, producing_actor, scheduling_status, timestamps, market_margin_multiplier, rnd_project_id linkage, scheduling_process_id linkage). Monthly `WorkType::designer_drug_detection_check`: sums evidence weight from financial + physical tokens held by LE/regulator NPCs; above `designer_detection_threshold` creates `SchedulingProcess` in `GlobalTechnologyState` (R&D doc §Part 7) and transitions to `review_initiated`. Per-tick scheduling enactment check: on enactment, compound moves to informal market layer; supply gap triggers market opportunity for rivals. `WorldState.designer_drug_records` field addition. 4 named constants to `simulation_config.json → drug`. Closes P-6. |
| P-7 | §42 (new) | Commodity Trading Player Action. `PositionType` enum (long / short). `CommodityPosition` struct (10 fields: actor, good, province, position type, quantity, entry price, opened/exit ticks, realised PnL). `open_commodity_position` player action: wealth debited, market-impact demand modifier applied for large positions. `close_commodity_position` player action: long and short settlement formulae; realised PnL feeds capital gains tax at next quarterly collection. `WorldState.commodity_positions` field addition. 2 named constants to `simulation_config.json → trading`. Closes P-7. |
| P-2 | §43.8 (new) | Formally closed. References §31 (Pass 4 Session 2) as the authoritative spec. |
| P-2 supplemental | §43.9 (new) | Criminal revenue tax treatment explicit rule: `informal_cash` is not included in income tax base; becomes taxable only after laundering converts it to `player.wealth` or `NPCBusiness.cash`. Laundering is therefore simultaneously an OPSEC action and a tax normalisation event. |
| P-3 | — | Closed in Pass 4 Session 2. No change this session. |
| P-4 | — | Closed in Pass 4 Session 1. No change this session. |
| S-1 | §43.1 (new) | NPC-to-NPC Obligation Scope. V1 constraint confirmed: `WorldState.obligation_network` contains only nodes where `creditor_npc_id == player_id` OR `debtor_npc_id == player_id`. NPC-to-NPC obligations represented as motivation weight adjustments on NPC records. Protection rackets (§37) cover the criminal-org-extorts-business case without ObligationNode. Player-is-not-special satisfied at emergent level. Explicit Bootstrapper rule added. Closes S-1. |
| Ab-1 | §43.2 (new) | Business Credit / Working Capital. V1 thin model: `NPCBusiness.accounts_payable_float` field addition (approximated as `cost_per_tick × config.business.payment_lag_ticks`, default 30 ticks). Bankruptcy evaluation reads this field: cash < accounts_payable_float → cash-flow insolvency even if profitable. Full AR/AP with invoice timing is EX. 1 named constant to `simulation_config.json → business`. |
| Ab-2 | §43.3 (new) | Government Procurement / Contracting. V1 thin model: new `FavorType::contract_award = 16` enum value. Contract award is an ObligationNode with creditor = awarding official, original_value = contract value. Corruption evidence generated when award price > 1.30× fair market estimate. 1 named constant to `simulation_config.json → procurement`. |
| Ab-3 | §43.4 (new) | Supply Chain Inventory / Buffer Stock. Ruled V1 required (structural gap — transit system non-functional without it). `Facility.input_buffer` field addition (`std::map<uint32_t, float>`: good_id → quantity held). `Facility.buffer_target_ticks` field addition (drives reorder quantity in NPC purchasing decision). Production only runs when `input_buffer[good_id] >= recipe_input_quantity` for all inputs. 1 named constant to `simulation_config.json → facility`. |
| Ab-4 | §43.5 (new) | Seasonal Agricultural Price Effects. CUT for V1. Explicit Bootstrapper directive: farm facilities produce at `output_per_tick` each tick; no harvest calendar; climate modifiers (drought/flood) and soil_health are the only agricultural output variables in V1. |
| Ab-5 | §43.6 (new) | Business Valuation / M&A Price Model. Closed by §5.1 (Pass 4 Session 1). Documented closure. |
| Ab-6 | §43.7 (new) | Consumer Debt / Household Leverage. CUT for V1 mechanics. `PopulationCohort.debt_load` field addition (dormant in V1; explicit `[EX]` comment; no V1 logic reads or writes it). Bootstrapper directive: generate no V1 logic for this field. |

### New Sections Added

| Section | Title |
|---|---|
| §39 | Drug Economy Type System [V1] |
| §40 | Designer Drug Scheduling Race [V1] |
| §41 | Antitrust Trigger Model [V1] |
| §42 | Commodity Trading Player Action [V1] |
| §43 | Remaining Scope and Absent System Decisions [V1] |

### New Enums

`DrugType`, `DrugMarketTier`, `SchedulingStatus`, `PositionType`

### New Structs

`DesignerDrugRecord`, `CommodityPosition`

### Field Additions

| Struct | Field | Type | Section |
|---|---|---|---|
| `Province` | `drug_legalization_status` | `std::map<DrugType, bool>` | §39.1 |
| `NPCBusiness` | `drug_market_tier` | `DrugMarketTier` | §39.2 |
| `RegionalPoliticalState` | `antitrust_proposal_pressure` | `float` | §41.1 |
| `WorldState` | `designer_drug_records` | `std::vector<DesignerDrugRecord>` | §40.2 |
| `WorldState` | `commodity_positions` | `std::vector<CommodityPosition>` | §42.1 |
| `NPCBusiness` | `accounts_payable_float` | `float` | §43.2 |
| `Facility` | `input_buffer` | `std::map<uint32_t, float>` | §43.4 |
| `Facility` | `buffer_target_ticks` | `float` | §43.4 |
| `PopulationCohort` | `debt_load` | `float` | §43.7 |
| `FavorType` | `contract_award = 16` | enum value | §43.3 |

### Enum Additions to Existing Types

| Enum | New Value | Section |
|---|---|---|
| `FavorType` | `contract_award = 16` | §43.3 |

### Audit Closure Summary — Pass 4 Complete

| Audit ID | Issue | Closed In |
|---|---|---|
| BLOCKER-A | Money Laundering | Session 3 |
| BLOCKER-B | Real Estate Market | Session 4 |
| BLOCKER-C | Banking / Loan System | Session 4 |
| BLOCKER-D | Labor Market / Hiring System | Session 1 |
| BLOCKER-E | Alternative Identity System | Session 4 |
| BLOCKER-F | Informant System | Session 4 |
| BLOCKER-G | Governing Budget / Fiscal System | Session 2 |
| BLOCKER-H | Protection Rackets | Session 4 |
| BLOCKER-I | Weapons Trafficking | Session 4 |
| P-1 | Drug Economy Type System | Session 5 |
| P-2 | Tax Collection Mechanism | Session 2 + Session 5 |
| P-3 | Player Business Financial Model | Session 1 |
| P-4 | Business Acquisition Mechanics | Session 1 |
| P-5 | Antitrust Trigger Model | Session 5 |
| P-6 | Criminal R&D / Designer Drug Scheduling Race | Session 5 |
| P-7 | Commodities Trading Player Action | Session 5 |
| S-1 | NPC-to-NPC Obligation V1 Boundary | Session 5 |
| S-2 | Player Business Ownership Architecture | Session 1 |
| S-3 | PopulationCohort Struct | Session 1 |
| S-4 | Residential vs. Commercial Property | Session 4 |
| Ab-1 | Business Credit / Working Capital | Session 5 |
| Ab-2 | Government Procurement / Contracting | Session 5 |
| Ab-3 | Supply Chain Inventory / Buffer Stock | Session 5 |
| Ab-4 | Seasonal Agricultural Price Effects | Session 5 (CUT) |
| Ab-5 | Business Valuation / M&A Price Model | Session 1 |
| Ab-6 | Consumer Debt / Household Leverage | Session 5 (CUT) |

**All 26 audit items resolved. Zero open audit findings.**

### Header Updated

- Title updated to v28, Pass 4 Session 5 complete.

---

## Change Log — Pass 4, Session 6 (§44 Seasonal Agricultural Production)

### Summary

Reinstates Ab-4 (Seasonal Agricultural Price Effects) as V1 per design principle that simulation diverges from realism only when technically necessary. Adds §44 (7 subsections, ~220 lines). No audit items change status — this is an additive reinstatement, not a reopening of a closed item.

### Changes

| ID | Location | Change |
|---|---|---|
| Ab-4 reinstated | §43.5 (revised) | CUT ruling replaced with forward reference to §44. |
| Ab-4 full spec | §44 (new) | Seasonal Agricultural Production. |
| — | §44.1 | `CropCategory` enum: 7 values (annual_grain, annual_oilseed, annual_fiber, perennial_tree, sugarcane, livestock, timber). Maps to FARM_STRESS_SENSITIVITY table in Commodities v2.3. V1 seasonal cycle: annual_grain, annual_oilseed, annual_fiber, sugarcane. Continuous-with-modifier: perennial_tree, livestock, timber. |
| — | §44.2 | `SeasonPhase` enum: 4 values (fallow, planting, growing, harvest). Full state-transition semantics. Planting phase: seed consumed from `input_buffer`; no seed → farm stays in planting, supply gap emerges. Harvest phase: `pending_harvest` released over `harvest_duration_ticks` (14 ticks) to avoid single-tick price crash. |
| — | §44.3 | `FarmSeasonState` struct: 10 fields including `crop_category`, `current_phase`, `pending_harvest`, `base_growth_rate` (derived from recipe so annual yield equals recipe's intended annual output regardless of growing season length), `annual_yield_last`, `seed_planted`, `years_same_crop`. Added as `std::optional<FarmSeasonState>` to `Facility`. |
| — | §44.3a | Growing season lookup table by `KoppenZone` × `CropCategory`. Northern Hemisphere baseline; Southern Hemisphere applies 182-tick offset from `GeographyProfile.latitude` sign. Zones where a crop is non-viable have `length=0` — facility creation rejects the crop/zone combination. |
| — | §44.4 | Per-tick farm integration replacing continuous output. Phase transition logic keyed to `tick_of_year`. Growing phase: accumulates into `pending_harvest` using existing climate modifiers (drought, flood, soil_health, fertilizer_efficiency). Harvest phase: releases to `RegionalMarket.supply` over 14 ticks. Fallow phase: soil_health recovery. Crop rotation action (`set_crop_category`) resets `years_same_crop`. |
| — | §44.5 | Continuous-with-modifier categories. `seasonal_yield_multiplier` field on Facility for perennial_tree, livestock, timber. Cosine formula (1 peak, 1 trough per year) keyed to crop × zone peak tick. Updated monthly. Timber constant at 1.0 (multi-year cycle abstracted). |
| — | §44.6 | Price seasonality emergence — no change to `RegionalMarket` pricing formula. Mechanism described: growing phase → supply depletion → price rise; harvest phase → supply burst → price fall. NPC traders using `CommodityPosition` (§42) arbitrage the gap. Supply gap (missed planting) → price spike at expected harvest time. Two scenario tests added: `scenario_seasonal_price_cycle` and `scenario_supply_gap_price_spike`. |
| — | §44.7 | Commodities doc cross-reference note. `farm_output_actual` formula applies only to continuous-with-modifier crops. Annual-cycle crops use §44.4. Companion doc annotation required. |

### New Section

§44 — Seasonal Agricultural Production [V1]

### New Enums

`CropCategory`, `SeasonPhase`

### New Structs

`FarmSeasonState`

### Field Additions

| Struct | Field | Type |
|---|---|---|
| `Facility` | `farm_season_state` | `std::optional<FarmSeasonState>` |
| `Facility` | `seasonal_yield_multiplier` | `float` (perennial/livestock/timber only) |

### New Named Constants (`simulation_config.json → seasonal`)

| Constant | Default |
|---|---|
| `planting_duration_ticks` | 7 |
| `harvest_duration_ticks` | 14 |
| `fallow_soil_recovery_rate` | 0.003 |
| `monoculture_penalty_threshold` | 3 |
| `monoculture_soil_penalty_rate` | 0.002 |
| `southern_hemisphere_offset` | 182 |
| `perennial_base` | 0.85 |
| `perennial_amplitude` | 0.25 |

### New Scenario Tests

`scenario_seasonal_price_cycle`, `scenario_supply_gap_price_spike`

### Header Updated

- Title updated to v29, Pass 4 Session 6 complete.

---

*EconLife Technical Design Document v29 — Pass 4 Complete (all audit items + seasonal agriculture) — Companion to GDD v1.7*
