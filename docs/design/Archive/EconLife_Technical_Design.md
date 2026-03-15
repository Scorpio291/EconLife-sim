# EconLife — Technical Design Document v2 — Pass 2 Complete
*Companion document to GDD v0.9*
*Living document — updated in sync with GDD and Feature Tier List*
*Pass 2 additions integrated — resolves blockers B1–B12, B17; precision gaps P6–P10*

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
│   27-step daily tick                    │
│   NPC engine | Economy engine           │
│   Evidence engine | Consequence queue   │
└───────────────────┬─────────────────────┘
                    │
┌───────────────────▼─────────────────────┐
│            PERSISTENCE LAYER            │
│   Continuous autosave to disk           │
│   No save slots. No reload.             │
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

### Persistence Architecture Constraint: No Save Slots

The persistence layer **must not expose** any API equivalent to SaveGame(), LoadGame(), or RestoreToSnapshot() to the UI layer or to any player-facing interface. This is an architectural requirement, not a convention. The CI pipeline includes a test that scans the codebase for any direct access to snapshot restoration paths from outside the persistence module — any such access fails the build.

The world state is continuously written to disk. A player cannot undo a decision by quitting and reloading. This is the architectural expression of the causality-over-content design principle.

---

## 3. The Simulation Tick

The world runs on a **daily tick**. One tick = one in-game day = one real-time minute at normal speed (30x fast-forward = one tick per two real seconds).

The tick executes 27 sequential steps (see GDD Section 21). The steps are ordered to respect causality: production before prices, prices before financial distribution, NPC behavior after financial state is updated, consequence thresholds checked after NPC behavior, etc.

### Tick Budget

**Hard requirement:** At 30x fast-forward, a tick must complete in under 2,000ms (2 seconds). A tick exceeding 2,000ms causes fast-forward to fall behind real time.

**Target:** Tick completes in under **500ms at 2,000 significant NPCs** at 30x fast-forward. This leaves 1,500ms of headroom for UI updates, snapshot writes, and system overhead, and allows fast-forward to run comfortably without hitching.

**Prototype success/failure thresholds** (see Section 19) align with this target: 200ms target, 500ms acceptable, 1,000ms failing — at 2,000 NPCs. These thresholds are the gate criterion. Acceptable means the architecture is viable but needs optimization before production. Failing means the NPC model must be simplified before production.

**There is no separate "normal speed" budget.** At normal speed (1x), the simulation is throttled to one tick per real-time minute. The tick's actual execution time is irrelevant at normal speed as long as it's under 60 seconds. All performance engineering targets 30x fast-forward.

**[RISK]** This 500ms target is aggressive at full NPC scale. The prototype's primary output is a measured tick duration under load.

### Tick Parallelism

Several tick steps are parallelizable within the step:

- Step 1 (production outputs): Each facility is independent within the step — parallelizable across all owned and NPC facilities.
- Step 7 (NPC behavior): Each NPC evaluates independently — parallelizable across all significant NPCs. **[RISK]** NPCs that interact with each other within one tick create dependency — a simple rule is needed (e.g., NPC-to-NPC interactions are queued for the next tick rather than resolved within the same tick).
- Step 18 (population aging): Each cohort is independent — fully parallelizable.

Steps involving global state updates (prices, political cycle, regional conditions) are not parallelizable and must be sequential.

### Tick Architecture: Module Interface

Each step is a module conforming to a standard interface:

```cpp
struct TickModule {
    std::string name;
    void execute(WorldState& state, DeltaBuffer& delta);
    // Reads from WorldState, writes proposed changes to DeltaBuffer
    // DeltaBuffer is applied to WorldState between modules
};
```

This allows modules to be added, removed, or reordered without touching other modules. A new criminal system is a new module inserted at the appropriate tick position.

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

    // State
    NPCStatus status;                     // active, imprisoned, dead, fled
    ActionQueue pending_actions;          // delayed consequences queued for future ticks
};
```

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

Each tick, the NPC behavior engine runs each significant NPC through a decision loop:

```
For each NPC:
1. Filter memory log for actionable entries (decay > threshold, emotional_weight > baseline)
2. Evaluate motivations against available actions:
   - What do I want? (motivation vector)
   - What do I know that's relevant? (knowledge map)
   - What can I actually do? (resources + contacts)
   - What's the risk? (risk_tolerance × estimated consequence)
3. If best_action.expected_value > inaction_threshold:
   queue action to pending_actions with appropriate delay
4. Update relationship scores based on recent interactions
```

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

### Price Model

Each tradeable good in each region has:

```cpp
struct RegionalMarket {
    uint32_t good_id;
    uint32_t region_id;
    float spot_price;
    float equilibrium_price;              // recalculated each tick
    float adjustment_rate;               // good-type dependent: financial fast, housing slow
    float supply;                         // sum of production outputs this tick
    float demand_buffer;                  // from previous tick (one-tick lag)
    float import_price_ceiling;
    float export_price_floor;
};
```

Price update per tick:
```
equilibrium = f(supply, demand_buffer, import_ceiling, export_floor)
spot_price += (equilibrium - spot_price) × adjustment_rate
```

`adjustment_rate` is the key tunable parameter — high for financial assets, very low for housing, medium for commodities. This stickiness is what creates arbitrage opportunities.

**[BLOCKER — B13 UNRESOLVED]** The equilibrium function `f()` is not yet specified. See Pass 2 Review for Pass 3 priority.

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
    uint32_t strategic_decision_tick;    // when they next make a quarterly decision
    float technology_tier;               // affects cost and quality competitiveness
    bool criminal_sector;                // true for businesses in criminal supply chain;
                                          // uses informal market price rather than formal spot price
};
```

Strategic decisions quarterly; behavioral profile determines intra-quarter behavior. This gives NPC businesses predictable but not scripted patterns — the player can learn a profile and exploit it.

**[PRECISION GAP — P11 PARTIAL]** The behavioral profile decision matrix for legitimate NPC businesses (cost_cutter, quality_player, fast_expander, defensive_incumbent) is not yet specified. The criminal organization decision matrix (Section 17) uses the same architecture and serves as the template. Pass 3 must supply the legitimate business equivalent.

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
    uint32_t subject_npc_id;             // who this is evidence against (usually player)
    uint32_t holder_npc_id;             // who currently holds / knows this
    uint32_t location_id;                // if physical
    float actionability;                 // how usable this evidence is (degrades if holder is discredited)
    bool player_aware;                   // is this on the player's evidence map?
    uint32_t created_tick;
    std::vector<uint32_t> secondary_holders; // NPCs who also know this evidence exists
};
```

Evidence tokens propagate through the NPC social graph when their holder shares them with contacts. The player sees tokens they created directly; secondary tokens (created by NPCs observing or documenting player actions) are invisible until discovered through intelligence investment.

### Consequence Queue

```cpp
struct ConsequenceEntry {
    uint32_t target_tick;                // when this fires
    ConsequenceType type;
    uint32_t source_npc_id;             // who triggered it
    ConsequencePayload payload;          // varies by type
    bool is_cancellable;                 // can player action prevent this?
    uint32_t cancellation_window_ticks; // how long the player has to act
};
```

The queue is sorted by `target_tick`. Each tick, the consequence engine pops entries with `target_tick <= current_tick` and executes them. Cancellable entries check whether the cancellation condition has been met (e.g., the witness was bought off) before executing.

**The queue survives player character death.** This is a hard requirement — consequences queued during the player's lifetime continue firing after death. The heir inherits a consequence queue, not a clean slate.

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

Obligation escalation is driven by the creditor NPC's motivation model and the passage of time. A creditor with a high money motivation escalates toward financial demands. A creditor with a power motivation escalates toward political influence demands. The escalation path is not scripted — it emerges from motivation × time × the player's current resources (what looks achievable to ask for).

**[BLOCKER — B15 UNRESOLVED]** The computable escalation algorithm (how `current_demand` grows per tick, when `status` transitions to `escalated`) is not yet specified. See Pass 2 Review for Pass 3 priority.

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

**DeadlineConsequence** is a lightweight ConsequenceEntry that fires if the CalendarEntry reaches its deadline without the player committing. The `default_outcome` field describes what happens — NPC acts on their own initiative, legal deadline passes, escalation fires. Stored inline as: `struct DeadlineConsequence { ConsequenceType type; ConsequencePayload default_outcome; };`

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
    std::vector<Region> regions;                    // V1: exactly 4 regions; see Section 12

    // --- NPC Population ---
    std::vector<NPC> significant_npcs;              // full model; see Section 4
    std::vector<NPC> named_background_npcs;         // simplified model; same struct, LOD flag set
    // Background population is not stored per-individual; it is aggregated in Region.cohort_stats

    // --- Player ---
    PlayerCharacter player;                         // see Section 11

    // --- Economy ---
    std::vector<RegionalMarket> regional_markets;   // one entry per (good_id × region_id); see Section 5
    std::vector<NPCBusiness> npc_businesses;        // see Section 5

    // --- Evidence and Consequence ---
    std::vector<EvidenceToken> evidence_pool;       // all active tokens in the world; see Section 6
    std::vector<ConsequenceEntry> consequence_queue; // sorted by target_tick; see Section 6

    // --- Obligation Network ---
    std::vector<ObligationNode> obligation_network; // all open obligation nodes; see Section 7

    // --- Scheduling ---
    std::vector<CalendarEntry> calendar;            // merged calendar: player + NPC commitments
                                                    // filtered per-owner at read time

    // --- Scene Cards ---
    std::vector<SceneCard> pending_scene_cards;     // generated this tick, awaiting UI delivery

    // --- Global Tick Metadata ---
    uint32_t ticks_this_session;                    // monotonic counter reset on load; for WAL
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
    float street;                 // -1.0 to 1.0
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
    uint32_t starting_region_id;

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
};
```

### Evidence Awareness Update

The player's `evidence_awareness_map` is updated by tick step 26 (device notifications). When an NPC contact with trust > `EVIDENCE_SHARE_TRUST_THRESHOLD` (= 0.45) learns of a new evidence token during tick step 11, a ConsequenceDelta schedules a device notification at `current_tick + contact_delay_ticks`. Contact delay is derived from the contact NPC's trust — higher trust = shorter delay. Tokens the player is unaware of remain only in the evidence_pool. This is the operational definition of "the player doesn't know about it."

### Skill Decay

Each tick, for every PlayerSkill where `current_tick - last_exercise_tick > SKILL_DECAY_GRACE_PERIOD` (= 30 ticks, ~one in-game month), level decays by `decay_rate`. Minimum floor: `SKILL_DOMAIN_FLOOR` (= 0.05). Using a skill sets `last_exercise_tick` to current_tick and applies a level gain proportional to the difficulty of the engagement.

---

## 12. Region and Nation [V1]

### WorldGenParameters

```cpp
namespace WorldGenDefaults {
    constexpr int    NATION_COUNT             = 1;
    constexpr int    REGION_COUNT             = 4;
    constexpr float  STARTING_INEQUALITY      = 0.45f;
    constexpr float  RESOURCE_RICHNESS        = 0.50f;
    constexpr float  CORRUPTION_BASELINE      = 0.35f;
    constexpr float  CRIMINAL_ACTIVITY_BASE   = 0.25f;
    constexpr float  CLIMATE_STRESS_FRACTION  = 0.25f; // fraction of regions productivity-degraded
    constexpr float  CLIMATE_GAIN_FRACTION    = 0.25f; // fraction of regions productivity-enhanced
}

struct WorldGenParameters {
    int   nation_count             = WorldGenDefaults::NATION_COUNT;
    int   region_count             = WorldGenDefaults::REGION_COUNT;
    float starting_inequality      = WorldGenDefaults::STARTING_INEQUALITY;
    float resource_richness        = WorldGenDefaults::RESOURCE_RICHNESS;
    float corruption_baseline      = WorldGenDefaults::CORRUPTION_BASELINE;
    float criminal_activity_base   = WorldGenDefaults::CRIMINAL_ACTIVITY_BASE;
    float climate_stress_fraction  = WorldGenDefaults::CLIMATE_STRESS_FRACTION;
    float climate_gain_fraction    = WorldGenDefaults::CLIMATE_GAIN_FRACTION;
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
    float accessibility;          // infrastructure requirement before extraction is viable
    float depletion_rate;         // fraction depleted per tick at full production
    float quantity_remaining;
};
```

### Region

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
};

struct Region {
    uint32_t id;
    std::string name;
    RegionDemographics demographics;
    std::vector<ResourceDeposit> deposits;
    float infrastructure_rating;       // 0.0–1.0
    CommunityState community;
    RegionalPoliticalState political;
    RegionConditions conditions;
    float base_institutional_trust;    // world-gen assigned; community.institutional_trust
                                       // converges toward this modified by corruption_index
    std::vector<uint32_t> significant_npc_ids;
    std::vector<uint32_t> named_background_npc_ids;
    std::vector<uint32_t> market_ids;  // convenience index; not authoritative
    OppositionOrganization* opposition_organization; // nullptr if no active org
};
```

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
    std::vector<uint32_t> region_ids;
    float corporate_tax_rate;
    float income_tax_rate_top_bracket;
    std::map<uint32_t, float> diplomatic_relations; // [EX] nation_id → -1.0 to 1.0; empty in V1
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

### Tick Step 22 Module Specification

```
Module name: "InfluenceNetworkWeightUpdate"
Tick position: 22

Read from WorldState:
  - player.relationships (all)
  - obligation_network (all open nodes)
  - player.movement_follower_count

Write to DeltaBuffer:
  - player_delta: updated InfluenceNetworkHealth snapshot
  - npc_deltas: fear decay — FEAR_DECAY_RATE = 0.002/tick when no reinforcing event this tick
  - npc_deltas: obligation trust erosion — overdue open obligations: trust_delta = -0.001/tick

Execution:
  1. Recompute InfluenceNetworkHealth from current WorldState
  2. Apply fear decay deltas for all fear-based relationships
  3. Apply obligation trust erosion for all overdue obligation nodes
  4. Write updated InfluenceNetworkHealth to player_delta
  5. Clear stale movement_ally flags for NPCs whose movement participation lapsed

[RISK] Dirty-flag optimization: only reclassify NPCs whose relationship changed this tick.
```

---

## 14. Community Response System [V1]

### Community State

`CommunityState` fields are embedded in the `Region` struct (Section 12). They are four scalar fields updated in-place each tick. See the Region struct for field definitions.

### Aggregation from NPC State to Community Metrics (Tick Step 9)

EMA smoothing factor: `COMMUNITY_EMA_ALPHA = 0.05`. At this value, a sustained shift takes approximately 20 ticks to substantially shift community metrics.

**Cohesion:**
```
cohesion_sample_i = clamp(npc.social_capital / SOCIAL_CAPITAL_MAX, 0.0, 1.0)
                  × clamp(npc.motivations[stability], 0.0, 1.0)
npc_cohesion_mean = mean(cohesion_sample_i for all significant NPCs in region)
region.community.cohesion += COMMUNITY_EMA_ALPHA × (npc_cohesion_mean - region.community.cohesion)
```

**Grievance Level:**
Driven by memory entries with negative emotional weight attributed to player actors (player operations, player-owned businesses, player-controlled politicians). Non-player attributed memories do not contribute.
```
negative_attribution_weight_i = sum(abs(entry.emotional_weight))
    for memory entries where: type in {interaction, observation}
    AND emotional_weight < 0
    AND subject_id in player_actor_ids
    AND decay > MEMORY_DECAY_FLOOR

npc_grievance_mean = mean(clamp(negative_attribution_weight_i / GRIEVANCE_NORMALIZER, 0.0, 1.0))
region.community.grievance_level += COMMUNITY_EMA_ALPHA × (npc_grievance_mean - region.community.grievance_level)
```
`GRIEVANCE_NORMALIZER` = 5.0 (default). **Fast-path for shock events:** If a single tick produces > `GRIEVANCE_SHOCK_THRESHOLD` (= 0.15) of raw grievance increase, the EMA is bypassed and grievance jumps directly.

**Institutional Trust:**
```
trust_target = clamp(
    region.base_institutional_trust
    - region.corruption_index × CORRUPTION_TRUST_PENALTY
    + recent_institutional_successes × TRUST_SUCCESS_BONUS
    - recent_institutional_failures × TRUST_FAILURE_PENALTY,
    0.0, 1.0
)
region.community.institutional_trust += COMMUNITY_EMA_ALPHA × (trust_target - region.community.institutional_trust)
```

**Resource Access:**
```
npc_resource_mean = mean(clamp(npc.capital / CAPITAL_NORMALIZER + npc.social_capital / SOCIAL_NORMALIZER, 0.0, 1.0))
region.community.resource_access += COMMUNITY_EMA_ALPHA × (npc_resource_mean - region.community.resource_access)
```

### Community Response Escalation Stages

```cpp
enum class CommunityResponseStage : uint8_t {
    quiescent             = 0,  // Threshold: grievance < 0.15 OR cohesion < 0.10
    informal_complaint    = 1,  // Threshold: grievance >= 0.15 AND cohesion >= 0.10
    organized_complaint   = 2,  // Threshold: grievance >= 0.28 AND cohesion >= 0.25
                                //   Generates regulatory complaint consequence entries.
    political_mobilization = 3, // Threshold: grievance >= 0.42 AND institutional_trust >= 0.20
    economic_resistance   = 4,  // Threshold: grievance >= 0.56 AND resource_access >= 0.25
                                //   RESISTANCE_REVENUE_PENALTY = -0.15 modifier on player revenue.
    direct_action         = 5,  // Threshold: grievance >= 0.70 AND cohesion >= 0.45
    sustained_opposition  = 6,  // Threshold: grievance >= 0.85 AND leadership_npc exists
                                //             AND resource_access >= 0.35
};
```

**Stage transition logic:** Each tick, evaluate conditions in order from highest to lowest. Stage = highest stage whose threshold is fully satisfied. Stages cannot skip — one stage per tick maximum. **Regression:** one stage per 7 ticks minimum. `sustained_opposition` (stage 6) does not regress automatically once an OppositionOrganization is created.

### Opposition Organization

```cpp
struct OppositionOrganization {
    uint32_t id;
    uint32_t founding_npc_id;
    uint32_t region_id;
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
IF region.community.stage == sustained_opposition AND region.opposition_organization == nullptr:
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
    address_grievance   = 0,  // Fix underlying cause. Grievance drops by GRIEVANCE_ADDRESS_RATE/tick.
                               //   OppositionOrganization may dissolve if grievance < 0.15.
    deflect             = 1,  // Pauses grievance accumulation for DEFLECT_PAUSE_TICKS (= 30).
                               //   Resumes at same rate. Overuse reduces institutional_trust.
    suppress            = 2,  // Reduces OppositionOrganization.resource_level immediately.
                               //   Negative memory entries in all members. Evidence token generated.
    co_opt_leadership   = 3,  // Offer founding_npc employment or favor. If accepted: NPC exits,
                               //   resource_level drops, new leader promoted from member_ids.
                               //   Risk: detectable; may spike grievance (betrayal memory).
    ignore              = 4,  // Grievance accumulates. resource_level grows by
                               //   OPPOSITION_GROWTH_RATE × region.community.resource_access/tick.
};
```

---

## 15. Political Cycle System [V1]

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

passed = (votes_for / (votes_for + votes_against)) > MAJORITY_THRESHOLD
MAJORITY_THRESHOLD = 0.5 for standard legislation; 0.67 for constitutional [EX in V1]
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
// RESOURCE_MAX_EFFECT = 0.15 (money cannot swing election by more than ±15 points alone)
resource_modifier = clamp(tanh(resource_deployment × RESOURCE_SCALE) × RESOURCE_MAX_EFFECT,
                          -RESOURCE_MAX_EFFECT, RESOURCE_MAX_EFFECT)

// Step 4: Event modifiers (capped at ±0.20)
event_total = clamp(sum(event_modifiers), -0.20, 0.20)

// Step 5: Final
player_vote_share = clamp(raw_share + resource_modifier + event_total, 0.0, 1.0)
won = (player_vote_share > office.win_threshold)
```

---

## 16. Criminal OPSEC Scoring [V1]

### Facility OPSEC Data Structure

```cpp
struct FacilityOPSEC {
    float power_consumption_anomaly;    // 0.0–1.0; deviation from legitimate baseline
    float chemical_waste_signature;     // 0.0–1.0; observable waste streams
    float foot_traffic_visibility;      // 0.0–1.0; observable traffic patterns
    float olfactory_signature;          // 0.0–1.0; direct smell at facility; distance-weighted
    float base_detection_risk;          // computed from four signals each tick (see G.2)
    float corruption_coverage;          // 0.0–1.0; reduces net risk
                                        // = clamp(sum(corrupted_le_npc.authority_weight), 0.0, 1.0)
    float net_detection_risk;           // = max(0.0, base_detection_risk - corruption_coverage)
};
```

### Detection Risk Formula

```cpp
constexpr float W_POWER_CONSUMPTION  = 0.30f;
constexpr float W_CHEMICAL_WASTE     = 0.25f;
constexpr float W_FOOT_TRAFFIC       = 0.20f;
constexpr float W_OLFACTORY          = 0.25f;

// Computed each tick in tick step 12:
facility.opsec.base_detection_risk = clamp(
    W_POWER_CONSUMPTION * facility.opsec.power_consumption_anomaly
  + W_CHEMICAL_WASTE    * facility.opsec.chemical_waste_signature
  + W_FOOT_TRAFFIC      * facility.opsec.foot_traffic_visibility
  + W_OLFACTORY         * facility.opsec.olfactory_signature,
    0.0f, 1.0f
);

facility.opsec.net_detection_risk = max(0.0f,
    facility.opsec.base_detection_risk - facility.opsec.corruption_coverage);
```

### Investigator Meter Integration (Tick Step 13)

```
For each law enforcement NPC in region r:
    regional_signal = sum(facility.opsec.net_detection_risk for all criminal facilities in r)
                    / FACILITY_COUNT_NORMALIZER
    le_npc.investigator_meter.fill_rate =
        clamp(regional_signal × DETECTION_TO_FILL_RATE_SCALE, 0.0f, FILL_RATE_MAX)
    // DETECTION_TO_FILL_RATE_SCALE = 0.005 (default):
    //   net_detection_risk 0.50 on single facility → fill_rate 0.0025/tick
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
    inactive       = 0,  // level < 0.30. No formal action.
    surveillance   = 1,  // level >= 0.30. Observation begins. Evidence token (physical) created.
    formal_inquiry = 2,  // level >= 0.60. Formal investigation opens.
                          //   ConsequenceEntry: investigation_opens.
                          //   Warrant possible if institutional_trust >= 0.30 in region.
    raid_imminent  = 3,  // level >= 0.80. Raid planned.
                          //   Consequence queued; delay = 7–30 ticks (seed-deterministic).
};

struct InvestigatorMeter {
    uint32_t investigator_npc_id;
    uint32_t target_id;
    float current_level;           // 0.0–1.0
    float fill_rate;               // per-tick increment
    float decay_rate;              // per-tick reduction when detection risk drops; default: 0.001
    InvestigatorStatus status;     // derived from thresholds above
    uint32_t opened_tick;          // tick when status first became formal_inquiry
    std::vector<uint32_t> evidence_token_ids;
};
```

**Status derivation:** `>= 0.80` → raid_imminent; `>= 0.60` → formal_inquiry; `>= 0.30` → surveillance; else inactive.

**Decay:** When `net_detection_risk` drops to 0.0, meter decays at `decay_rate`. Formally opened investigations do not close on signal drop alone — require active intervention.

---

## 17. Criminal Organization [V1]

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
    std::vector<uint32_t> territory_region_ids;
    std::vector<uint32_t> income_source_ids;     // NPCBusiness ids with criminal_sector = true
    std::vector<uint32_t> member_npc_ids;
    uint32_t leadership_npc_id;
    uint32_t strategic_decision_tick;            // next quarterly decision; = current_tick + 90
    float cash;
    TerritorialConflictState conflict_state;
    std::map<uint32_t, float> dominance_by_region; // region_id → 0.0–1.0
};
```

### Territorial Conflict State

```cpp
enum class TerritorialConflictStage : uint8_t {
    none                    = 0,
    economic                = 1,
    intelligence_harassment = 2,
    property_violence       = 3,  // Law enforcement attention rises regardless of corruption coverage
    personnel_violence      = 4,  // InvestigatorMeter fill_rate × PERSONNEL_VIOLENCE_MULTIPLIER (= 3.0)
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
    float territory_pressure;    // sum of competing orgs' dominance_by_region in shared regions
    float cash_level;            // CriminalOrganization.cash / CASH_COMFORTABLE_THRESHOLD
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

## 18. Persistence Architecture [CORE]

### Autosave

The world state is written to disk continuously. Architecture:

- **Write-ahead log (WAL):** Every delta to world state is appended to a WAL before being applied. If the game crashes mid-tick, the WAL allows reconstruction.
- **Periodic snapshots:** Full world state snapshot every in-game month (every 30 ticks). Snapshots allow fast load without replaying the full WAL.
- **No manual save / no reload:** The persistence layer does not expose a save/load API to the UI layer. The UI can display "autosaved" status but cannot trigger a manual save or restore.

### World State Size Estimate

| Component | Estimated size |
|---|---|
| 3,000 significant NPCs × 500 memory entries | ~6MB |
| Regional market state (50 goods × 20 regions) | ~100KB |
| Evidence token pool | ~500KB |
| Consequence queue | ~200KB |
| NPC relationship graph (sparse) | ~2MB |
| Facility and business state | ~1MB |
| **Total estimated active state** | **~10MB** |

This is well within manageable persistence scope. Snapshot compression (LZ4 or similar) will reduce disk footprint further.

---

## 19. The Simulation Prototype — Specification [PROTOTYPE]

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

## 20. Known Technical Risks [CORE]

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

## 21. Development Milestones [CORE]

| Milestone | Timing | Deliverable |
|---|---|---|
| Simulation prototype complete | Month 3–4 | Performance benchmark, architecture memo, reference implementation |
| V1 vertical slice (core economy + 1 career path) | Month 12 | Playable build: one region, business path, basic NPC simulation |
| Closed alpha (3 paths, full economy) | Month 24 | Business + criminal + political paths, full consequence system, 3 regions |
| Beta | Month 36 | Full V1 feature set, one nation, content complete for V1 |
| V1 ship | Month 42–48 | Gold |

Milestones compress or extend based on prototype results. If the prototype fails, the Month 12 milestone date shifts right while the architecture is redesigned.

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
| 16 | Criminal OPSEC Scoring | Pass 2B — Resolves B7, P7 |
| 17 | Criminal Organization | Pass 2B — Resolves B8 |

### Enums Completed (replacing "etc." stubs)

| Enum | Location | Source |
|---|---|---|
| NPCRole | Section 4 | Pass 2B ENUM 1 — Resolves B9 |
| ConsequenceType | Section 6 | Pass 2B ENUM 2 — Resolves B10 |
| FavorType | Section 7 | Pass 2B ENUM 3 — Resolves B11 |
| SceneSetting | Section 9 | Pass 2B ENUM 4 — Resolves B12 |
| BusinessSector | Section 5 | Pass 2B ENUM 5 — Resolves P10 |

### Sections Renumbered

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

*EconLife Technical Design Document v2 — Pass 2 Complete — Companion to GDD v0.9*
