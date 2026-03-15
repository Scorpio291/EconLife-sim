# EconLife — Pass 1 Review: Precision Audit for the 99% Experiment
*First-pass audit of GDD v0.9, Technical Design, Feature Tier List, and 99% Experiment document*
*Purpose: identify every gap that would prevent the Bootstrapper from producing correct interface specs*

---

## Executive Summary

The documents are better than most game design documents. The GDD has real numeric thresholds in several places, the Technical Design has actual data structures, and the Feature Tier List is genuinely decisive. The 99% experiment is viable — but not yet.

**Issue count by priority:**
- Priority 1 (BLOCKERS — Bootstrapper cannot run without these): 18 issues
- Priority 2 (PRECISION GAPS — implementable but ambiguous): 14 issues
- Priority 3 (SCENARIO TAGS — behavioral assertions not yet tagged): 22 candidates
- Priority 4 (SCOPE MARKERS — V1/EX markers missing from Technical Design): 1 structural issue
- Priority 5 (CROSS-DOCUMENT — consistency issues): 6 issues

**Assessment:** The simulation layer is well-specified at the module level. The data structure specifications are good. The core problem is that several of the simulation's critical systems have no Technical Design sections at all — the GDD describes them in prose, the Feature Tier List assigns them to V1, and the Technical Design says nothing about them. The Bootstrapper cannot derive an interface spec from prose alone; it needs a Technical Design section with at least an algorithmic description and the relevant data structures. Fix the blockers, then run.

---

## PRIORITY 1 — BLOCKERS

*These prevent the Bootstrapper from generating correct interface specs. Nothing can ship until these are resolved.*

---

### B1 — Missing: WorldState and DeltaBuffer data structures

**Document:** Technical Design
**Location:** Referenced in Section 3 tick module interface, never defined

The tick module interface references `WorldState& state` and `DeltaBuffer& delta` but neither is defined anywhere. WorldState is the master container that every module reads from. DeltaBuffer is the staging area where module outputs land before being applied. The Bootstrapper cannot generate any module interface spec without knowing what these containers look like — every module input and output is defined relative to them.

**What's needed:** A Technical Design section defining:
- `WorldState` — the top-level struct containing all simulation state (references to Region array, NPC array, EvidencePool, ConsequenceQueue, ObligationNetwork, Player, etc.)
- `DeltaBuffer` — what proposed changes look like, how modules write to it, how it's applied to WorldState between steps

---

### B2 — Missing: Player / PlayerCharacter data structure

**Document:** Technical Design
**Location:** Not present anywhere

The NPC struct is specified in detail. The Player has stats (Health, Age, Wealth, Reputation, Exposure, Skills), personal life state (partner, children, residence), character traits, calendar, and a consequence queue. None of these are defined as data structures in the Technical Design. The player character is treated as implicit in the GDD but the simulation needs an actual struct.

**What's needed:** A `PlayerCharacter` struct in the Technical Design. At minimum:
- Identity fields (character creation outputs)
- The stats from GDD Section 4 as typed fields with ranges
- Skill map (per-skill float with decay rate)
- Health model fields (current health, degradation modifiers, lifespan projection)
- Reference to their calendar (CalendarEntry vector already defined)
- Reference to their obligation node list
- Reference to their evidence awareness map

Note: The GDD says the player's Exposure is not a stat — it's the set of evidence tokens they're aware of. The Technical Design needs to make this distinction explicit and specify how the player's evidence awareness map is updated.

---

### B3 — Missing: Region and Nation data structures

**Document:** Technical Design
**Location:** Not present anywhere

Regions are the fundamental geographic unit. Every simulation system references regions — markets are regional, community response is regional, political systems are regional, criminal dominance is regional. The Technical Design specifies data for goods at the regional level (`RegionalMarket`) but never defines the Region struct itself.

**What's needed:**
- `Region` struct: demographic attributes, resource deposit references, infrastructure rating, dynamic condition fields (stability, inequality, crime rate, addiction rate, criminal dominance index), political state, community cohesion/grievance/trust, NPC population references
- `Nation` struct: currency, tax code reference, government type, political cycle state, diplomatic relationships (placeholder for expansion)
- `WorldGenParameters` struct: the configurable parameters listed in GDD Section 21 with V1 default values explicitly stated

---

### B4 — Missing: Technical Design section for the Influence Network

**Document:** Technical Design
**Location:** Not present

The Influence Network is a V1 system. The GDD Section 13 describes it in good detail (four influence types, trust/obligation/fear/movement, the floor principle). The Feature Tier List marks all four types as V1. But the Technical Design has no section on it.

The existing data structures already contain relationship data (`Relationship.trust`, `Relationship.fear`, `Relationship.obligation_balance`) but the Influence Network is described as something the player can *visualize* — a network readout showing relationship counts by type, network health. This requires either a derived computation from the relationship graph or a separate tracked structure.

**What's needed:**
- Specification of how the four influence type scores are derived (or tracked) per relationship
- Definition of "network health indicator" — what is it measuring, what's the formula
- Definition of the "floor principle" computationally: when a trust relationship drops catastrophically, what sets the permanent floor, and how is it stored
- Tick step assignment: which tick step updates influence network weights (tick step 22 references this — that step needs a Technical Design section)

---

### B5 — Missing: Technical Design section for Community Response

**Document:** Technical Design
**Location:** Tick step 9 and 10 reference community response; no section exists

The GDD Section 14.2 specifies 7 community response escalation stages and 5 intervention types. This is a V1 system. But the Technical Design has no section defining:
- What community cohesion, grievance, institutional trust, and resource access look like as data structures
- How individual NPC experiences (satisfaction, memory entries) aggregate into community-level metrics
- The escalation trigger conditions between stages
- What "organized opposition formation" produces as a simulation object

**What's needed:** A Technical Design section that takes the GDD's prose model and specifies it computationally. The key algorithmic question: how does the behavior engine go from "N NPCs in region R have high grievance AND a Significant NPC with motivation > threshold exists AND region has resources" to "opposition organization created"?

---

### B6 — Missing: Technical Design section for the Political Cycle

**Document:** Technical Design
**Location:** Tick step 19 references political cycle; no section exists

The political system is V1 at full depth (campaigns, elections, governing, legislation, approval). None of this has a Technical Design section. The GDD Section 15 has good prose descriptions but no data structures or algorithms.

**What's needed:**
- `PoliticalOffice` struct: office type, current holder, election cycle tick, approval tracking
- `LegislativeProposal` struct: proposal state machine, vote tracking, NPC legislator positions
- Election resolution algorithm: how "coalition strength × turnout × variance" produces a binary win/loss outcome
- Campaign data structure: how coalition commitments, endorsements, and obligations are tracked during a campaign
- Governing approval model: how demographic-level approval is tracked and updated

---

### B7 — Missing: Technical Design section for Criminal OPSEC scoring

**Document:** Technical Design
**Location:** Tick step 12 references criminal detection risk; no section exists

The GDD specifies that criminal facility design affects detection risk through power consumption patterns, chemical waste output, foot traffic, and olfactory signatures. The Feature Tier List marks OPSEC as V1. But there's no Technical Design section specifying how detection risk is calculated.

**What's needed:**
- Detection risk score computation (what inputs contribute, how they combine, what the output scale is)
- How facility design parameters (from the facility planning system) translate into OPSEC inputs
- How detection risk feeds into the investigator engine (tick step 13 — does high detection risk increase investigator meter fill rate? By how much?)
- How corruption coverage affects net detection risk

---

### B8 — Missing: Technical Design section for Criminal Organization

**Document:** Technical Design
**Location:** GDD Section 12.7 and 12.8 describe organizations; no struct defined

Pre-existing NPC criminal organizations are V1. The player can join, compete with, or fight them. But there's no `CriminalOrganization` struct anywhere, and the simulation needs to model their operations, territory, income, and behavior.

**What's needed:**
- `CriminalOrganization` struct: structure type (gang/syndicate/cartel), territory (region list), income sources, NPC member references, strategic decision state
- How NPC criminal organizations run their own supply chains (they use the same supply chain mechanics — this needs to be explicit)
- How territorial conflict escalation state is tracked

---

### B9 — Incomplete enum: NPCRole

**Document:** Technical Design, Section 4
**Location:** `NPCRole role;  // enum: politician, journalist, worker, etc.`

The "etc." is a Bootstrapper killer. NPCRole determines what actions are available to an NPC in the behavior engine. An NPC's role determines what social networks they move in, what investigative capabilities they have, what they can offer the player, and how they respond to different player actions.

**What's needed:** Complete enumeration of all V1 NPCRole values. Based on the GDD, these include at minimum:
`politician, journalist, regulator, law_enforcement, prosecutor, judge, corporate_executive, middle_manager, worker, criminal_operator, criminal_enforcer, fixer, lawyer, accountant, banker, union_organizer, community_leader, media_editor, investigator_ngo, candidate, bodyguard, family_member`

Each role needs a brief specification of what it enables in the behavior engine.

---

### B10 — Missing enum: ConsequenceType

**Document:** Technical Design, Section 6
**Location:** `ConsequenceType type;` in ConsequenceEntry struct — never enumerated

The ConsequenceType determines what the consequence engine does when an entry fires. Different types will have different payload structures and different execution logic.

**What's needed:** Complete V1 enumeration. Based on the GDD, these include at minimum:
`npc_action` (an NPC takes a specific action), `investigation_opens`, `evidence_token_created`, `obligation_call_in`, `relationship_change`, `media_story_breaks`, `legal_process_advance`, `community_escalation`, `criminal_retaliation`, `whistleblower_contacts_authority`, `random_event_fires`

---

### B11 — Missing enum: FavorType

**Document:** Technical Design, Section 7
**Location:** `FavorType favor_type;` in ObligationNode — never enumerated

Favor type determines the creditor NPC's escalation behavior. A financial favor escalates toward financial demands; a career favor toward career demands; a protection favor toward criminal exposure. This is referenced in the GDD's description of obligation escalation but never enumerated.

**What's needed:** Complete V1 enumeration: `financial_loan`, `financial_contribution`, `legal_defense`, `career_advancement`, `political_endorsement`, `criminal_protection`, `information_provided`, `evidence_suppressed`, `introduction_made`, `physical_protection`

---

### B12 — Incomplete enum: SceneSetting

**Document:** Technical Design, Section 9
**Location:** `SceneSetting setting; // enum: boardroom, cafe, parking_garage, home, prison, etc.`

"etc." again. The setting determines the visual framing and atmospheric signal of scene cards — it's explicitly described as part of the information delivered to the player, not just cosmetic. The Bootstrapper can't generate the scene card system correctly without knowing the complete V1 setting list.

**What's needed:** Complete V1 enum. Based on GDD descriptions and career paths:
`boardroom, office, cafe, restaurant, parking_garage, street_corner, nightclub, government_office, courthouse, prison, hospital, home_dining, home_office, phone_call, construction_site, warehouse, lab, car_moving, hotel_lobby, public_park`

---

### B13 — Missing: Equilibrium price formula

**Document:** Technical Design, Section 5
**Location:** `equilibrium = f(supply, demand_buffer, import_ceiling, export_floor)` — f is undefined

"f" is shorthand for a function that isn't specified. The price model description says "each tick, spot price adjusts toward equilibrium at a configurable rate" — but the equilibrium calculation itself is not given. This is the core of the economy simulation.

**What's needed:** The actual formula. A reasonable starting point for derivation from the GDD:
```
equilibrium = clamp(base_price × (demand_buffer / supply), import_price_ceiling, export_price_floor)
```
But this needs to be decided and written down explicitly. `base_price` is also undefined — is equilibrium computed entirely from supply/demand ratio, or does it have a reference price?

---

### B14 — Missing: NPC expected_value calculation

**Document:** Technical Design, Section 4
**Location:** Behavior generation pseudocode: `if best_action.expected_value > inaction_threshold`

How is `expected_value` calculated? The pseudocode filters memories, evaluates motivations, lists available actions — but doesn't specify how expected value is computed. Without this, the behavior engine module can't be implemented. This is the most important algorithm in the game.

**What's needed:** A specification of how expected value is computed for a candidate action. At minimum:
- How motivation weights translate into value multipliers (a high-money-motivation NPC values financial outcomes more)
- How risk tolerance scales the expected value when the action carries risk
- How relationship quality with the action's target affects expected outcome
- What "inaction_threshold" is and whether it's fixed or NPC-specific

---

### B15 — Missing: Obligation escalation algorithm

**Document:** Technical Design, Section 7
**Location:** "escalation is driven by creditor NPC's motivation model and passage of time" — not a computable formula

The GDD says escalation "emerges from motivation × time × the player's current resources." The Technical Design says escalation is driven by "motivation model + time." Neither is an algorithm. The Bootstrapper cannot write an interface spec for the obligation network module without knowing how escalation is computed each tick.

**What's needed:** A computable escalation rule. Example form:
```
demand_increase_rate = creditor.motivation[dominant_type] × time_since_favor × (1.0 + player.visible_wealth × demand_multiplier)
if current_tick - last_escalation_tick > escalation_interval:
    current_demand += demand_increase_rate
    add escalation step to history
    set status = escalated if current_demand > original_value × escalation_threshold
```
The exact values need to be set. The form needs to be decided.

---

### B16 — Missing: Worker satisfaction model

**Document:** Technical Design, Section 5
**Location:** GDD Section 6.8 says "Full satisfaction model in Technical Design Section 5" — it's not there

GDD Section 6.8 gives a precise whistleblower-eligible condition: `satisfaction < 0.35 AND witnessed_illegal_activity entry with emotional_weight > 0.6 AND risk_tolerance > 0.4`. This is exactly the kind of precision we need everywhere. But Technical Design Section 5 only covers price models and NPC businesses — there's no worker satisfaction model.

**What's needed:** A Technical Design section specifying:
- How satisfaction (0.0–1.0) is derived from NPC memory entries (it's described as "derived from memory entries about employment situation," not a separate tracked field)
- Which memory entry types affect satisfaction and by how much
- Whether satisfaction is computed fresh each tick or stored and updated incrementally

---

### B17 — Missing: Election resolution algorithm

**Document:** Technical Design (implied); GDD Section 15
**Location:** GDD says "outcome is probabilistic based on coalition strength × turnout × variance" — formula undefined

The election is the V1 climax of the political path. The GDD describes the inputs but not how they combine into an outcome probability. The Bootstrapper needs to generate a `political_cycle` module interface spec — and that spec requires knowing the resolution algorithm.

**What's needed:** An election resolution formula. At minimum:
- How demographic coalition support and turnout propensity combine into a vote count
- How campaign resources shift support in final days (or do they? The GDD implies money shifts things but real trust is more durable)
- What "random variance from campaign events" means mechanically (a noise term? specific event modifiers?)
- The minimum required to specify: `player_vote_share = f(coalition_support[], turnout_weights[], resource_deployment, event_modifiers)`

---

### B18 — Missing: Random event model

**Document:** Technical Design (should exist); GDD Section 17
**Location:** GDD describes 4 types of random events, says they're "genuinely random in timing" — no model

Random events are V1. But "genuinely random in timing" with no model specified means the Bootstrapper can't write an interface spec for the random event module (tick step 25). What is the base rate? What distribution governs timing? What determines which event type fires?

**What's needed:**
- Event probability model: is it a per-region per-tick probability? Poisson process? Fixed intervals with jitter?
- How does world state affect event probability (a drought is more likely in water-stressed regions)
- Event type distribution: what controls whether a natural event vs. human event vs. economic event fires
- Effect model: when an event fires, what does it write to the delta buffer

---

## PRIORITY 2 — PRECISION GAPS

*These systems are described but not precisely enough for the Bootstrapper to derive unambiguous specs. Needs tightening, not full writing.*

---

### P1 — Memory cap is approximate

**Location:** GDD Section 3 and Technical Design Section 4
**Issue:** Both say "~500 entries." The tilde (approximately) must become an exact number. `MAX_MEMORY_ENTRIES = 500` is a constant that needs to be declared explicitly and referenced in all interface specs.

**Fix:** Replace "~500" with exactly 500 everywhere. If 500 is wrong, decide the right number and use that. The constant should be defined once in a shared header.

---

### P2 — NPC count range needs a V1 fixed value

**Location:** GDD Section 3, Technical Design Section 4
**Issue:** "500–1,000 significant NPCs per region" is a range. The Bootstrapper needs to know the design target for V1 data structure sizing and benchmark planning. The prototype will produce a number; for spec generation purposes, 750 should be used as the planning assumption.

**Fix:** Add a note: "V1 planning assumption: 750 significant NPCs per region. Prototype validates viability. This value is adjustable based on prototype results."

---

### P3 — Informal economy connection to formal economy

**Location:** GDD Section 8.8
**Issue:** "Black market prices respond to formal market conditions — smuggling is more profitable when tariffs are high." But how? Is there a formula? Is the black market price derived from the formal spot price adjusted by a tariff factor? Is it a separate price model running on the same supply/demand logic but with different goods?

**Fix:** Add one paragraph to Technical Design Section 5 specifying: "Black market prices for goods that have legal equivalents are modeled as `black_market_price = formal_spot_price × (1 + tariff_rate + enforcement_risk_premium)`. The enforcement risk premium is a per-good, per-region float that increases with law enforcement activity and decreases with corruption coverage."

---

### P4 — Technology tiers: efficiency differential undefined

**Location:** GDD Section 9.1
**Issue:** "A manufacturing plant running Tier 3 equipment is less efficient and competitive than one running Tier 5." By how much? The technology tier affects production efficiency and cost competitiveness but no conversion is specified.

**Fix:** Define the efficiency conversion: "Each technology tier above tier 1 provides a `TECH_TIER_EFFICIENCY_BONUS` (proposed: 8% production efficiency gain per tier, configurable). The cost competitiveness advantage is expressed as a `cost_per_unit` reduction of `TECH_TIER_COST_REDUCTION` per tier (proposed: 5%)."

---

### P5 — Evidence actionability decay: no formula

**Location:** Technical Design Section 6
**Issue:** The EvidenceToken struct has `actionability` as a float, with the note that "actionability degrades if holder is discredited." But there's no formula for how it degrades, or what "discredited" means as a state.

**Fix:** Specify: "actionability decays at rate `BASE_DECAY_RATE` per tick (proposed: 0.001 per tick, meaning a token loses half its actionability in ~700 ticks / 2 in-game years). If holder NPC has is_credible = false, decay rate multiplies by `DISCREDIT_DECAY_MULTIPLIER` (proposed: 5×). Minimum actionability floor: 0.1 (evidence never fully disappears, it just becomes harder to use)."

---

### P6 — Influence floor principle: no stored representation

**Location:** GDD Section 13.4
**Issue:** "A player who lost 70% of trust-based Influence through a major scandal may recover 50–60% of the original level — the remaining gap is permanent." This is described qualitatively. But how is it stored? Is there a `recovery_ceiling` field on the relationship? What sets it?

**Fix:** Add a `recovery_ceiling` float (0.0–1.0) to the Relationship struct. Set at time of catastrophic trust loss: `recovery_ceiling = trust_at_loss_time × CATASTROPHIC_TRUST_RECOVERY_LIMIT` (proposed: 0.7 — you can recover 70% of what you had when the catastrophic event happened). The trust rebuild logic caps at recovery_ceiling rather than 1.0.

---

### P7 — Journalist investigation meter: undefined

**Location:** GDD Section 10.3
**Issue:** "Investigation meters fill over time when the player is a high-profile target in their beat." What is the investigation meter? What is its scale? What fills it? What are its threshold effects? This is referenced as a central mechanic in multiple places but never given a data structure or computation rule.

**Fix:** Define the investigator engine data structures in Technical Design. At minimum:
```cpp
struct InvestigatorMeter {
    uint32_t investigator_npc_id;
    uint32_t target_id;          // player or NPC
    float current_level;          // 0.0 to 1.0
    float fill_rate;              // per tick, based on available evidence and motivation
    InvestigatorStatus status;    // dormant, building, active, filed, completed
};
```
Threshold effects: `current_level > 0.3` → investigator requests records; `> 0.6` → investigator conducts interviews; `> 0.8` → investigator approaches law enforcement or publishes.

---

### P8 — World generation default values not specified

**Location:** GDD Section 21 (Procedural World Generation), Feature Tier List
**Issue:** The world generation configurable parameters are listed (number of nations, inequality distribution, resource richness, etc.) but no V1 default values are given. The FTL says "V1 ships with one well-tuned default world; full parameter control is expansion." The default values need to be explicit.

**Fix:** Add a "V1 Default World Parameters" table to the Technical Design. Example:
| Parameter | V1 Default |
|---|---|
| Nations | 1 |
| Regions | 4 |
| Starting inequality | High (Gini 0.45) |
| Resource richness | Medium |
| Corruption baseline | Medium |
| Criminal activity baseline | Established (2–3 pre-existing organizations) |
| Climate stress | One water-stressed region, one productivity-gaining region |

---

### P9 — Skill progression and decay: no model

**Location:** GDD Section 4; Feature Tier List ("Skill leveling by doing and skill rust by neglect — V1")
**Issue:** Skills are described as leveled by doing and rusted through neglect. No numeric model exists. What is the scale? What is the decay rate? How does "doing" translate to advancement?

**Fix:** Add to Technical Design: "Skills are tracked as floats on a 0.0–100.0 scale. Advancement: each use of a skill in a scene card interaction advances the relevant skill by `SKILL_USE_GAIN` (proposed: 0.1). Decay: each tick without use reduces the skill by `SKILL_DECAY_RATE` (proposed: 0.001 per tick, approximately 10% over a year of non-use). Floor: skills decay to 50% of their peak level at minimum (you can't forget everything)."

---

### P10 — BusinessSector enum undefined

**Location:** Technical Design Section 5 `BusinessSector sector;`
**Issue:** BusinessSector is referenced but never enumerated.

**Fix:** Enumerate: `manufacturing, food_beverage, retail, services, real_estate, agriculture, energy, technology, finance, transport_logistics, media, security, research, criminal`

---

### P11 — NPC business strategic decision logic undefined

**Location:** Technical Design Section 5, GDD Section 8.7
**Issue:** "NPC businesses make strategic decisions quarterly. Between decisions, they follow their behavioral profile within current constraints." What does a strategic decision produce? The behavioral profiles are described (cost_cutter, quality_player, etc.) but the decision logic — how a profile + market conditions + cash state → a specific strategic choice — isn't specified.

**Fix:** Define the decision matrix for each behavioral profile. Example for `cost_cutter`: "If market_share < threshold AND competitor_quality > player_quality: reduce labor costs; else if cash < 2×monthly_costs: defer investment; else: seek lower-cost inputs." Each profile needs 3–5 decision rules specified.

---

### P12 — DeadlineConsequence: referenced but undefined

**Location:** Technical Design Section 8 `DeadlineConsequence deadline_consequence;`
**Issue:** Calendar entries have a DeadlineConsequence for what happens if the player misses them, but this type is never defined.

**Fix:** Define: a DeadlineConsequence is a lightweight ConsequenceEntry that fires if the CalendarEntry reaches its deadline without the player committing. The `default_outcome` field describes what happens (NPC acts on their own initiative, legal deadline passes, etc.).

---

### P13 — Organic milestone tracking: undefined

**Location:** Feature Tier List ("Organic milestone tracking (silent) — V1")
**Issue:** Milestone tracking is V1 but there's no specification of what the milestones are, what triggers them, or how they're stored.

**Fix:** Add a milestone definition list. Examples: `first_profitable_business`, `first_arrest`, `first_election_won`, `first_law_passed`, `first_cover_up`, `first_cartel_established`. Each is a boolean condition evaluated once, permanently recorded on the character timeline when triggered.

---

### P14 — Criminal entry conditions undefined

**Location:** GDD Section 12.1
**Issue:** "No criminal operation begins by clicking a menu option — it begins when the player has the right relationships, the right contacts, and the right reputation to make it possible." What are the specific conditions? What rep score? What contact types?

**Fix:** For each V1 criminal operation type, specify the minimum entry condition as a checkable state. Example for drug distribution: "Player must have street_reputation > 30 AND at least one contact with NPC role in {criminal_operator, fixer} AND that contact's trust score with player > 0.4 AND player knows about a supply source (has at least one memory entry of type criminal_introduction for drug supply)."

---

## PRIORITY 3 — SCENARIO TAG CANDIDATES

*These are behavioral assertions in the GDD that should be tagged [SCENARIO] so the Bootstrapper converts them to scenario tests. Listed with the test description the tag would generate.*

1. "A journalist with career motivation will pursue a story if career upside outweighs legal risk" → `scenario_journalist_pursues_story_on_career_motivation`
2. "If player buys the outlet a journalist works for, she takes her notes elsewhere and is specifically hostile" → `scenario_journalist_leaves_hostile_after_outlet_purchase`
3. "A regulator whose supervisor answers to a politician the player controls files preliminary inquiry but moves slowly" → `scenario_regulator_slows_when_supervisor_controlled`
4. "Wage cut → union organizer quietly working the floor 6 months later" → `scenario_wage_cut_produces_union_organizer`
5. "Reaching significant net worth → journalist adds player to watchlist" → `scenario_journalist_watchlist_on_wealth_threshold`
6. "Controlling 40%+ market → competition regulator opens preliminary inquiry" → `scenario_antitrust_inquiry_on_market_concentration`
7. "Becoming largest regional employer → union organizers identify player as highest-value target" → `scenario_union_targets_largest_employer`
8. "Fired journalist takes notes elsewhere and is now specifically hostile" → `scenario_fired_journalist_becomes_hostile_investigator`
9. "A witness too frightened in year one may come forward in year four when circumstances change" → `scenario_witness_delayed_cooperation_on_circumstance_change`
10. "Criminal warfare activates investigation capacity that was previously dormant" → `scenario_criminal_warfare_activates_dormant_investigation`
11. "A worker present when a sensitive financial conversation happened has a memory entry for it" → `scenario_worker_observes_sensitive_event_creates_memory`
12. "NPC businesses that reach cash depletion enter bankruptcy" → `scenario_npc_business_bankruptcy_on_cash_depletion`
13. "Whistleblower-eligible condition: satisfaction < 0.35 AND witnessed_illegal AND risk_tolerance > 0.4" → `scenario_whistleblower_emerges_on_eligible_conditions`
14. "Drug market at 15%+ regional addiction → visible economic degradation" → `scenario_addiction_rate_produces_economic_degradation`
15. "A player who fires a worker carrying sensitive knowledge → worker acts months to years later" → `scenario_fired_worker_with_knowledge_delayed_consequence`
16. "Opposition formation requires grievance + leadership + resources to converge" → `scenario_opposition_forms_on_convergence`
17. "Criminal dominance → property values fall, legitimate businesses face protection costs" → `scenario_criminal_dominance_degrades_regional_economy`
18. "Operating trafficking network → international agencies flag anomalies [expansion — skip for V1 Bootstrapper]" → `scenario_expansion_trafficking_international_attention`
19. "A politician who has never accepted anything improper cannot be threatened" → `scenario_clean_politician_immune_to_obligation_leverage`
20. "Drug legalization → criminal market margins compress over game months" → `scenario_legalization_compresses_criminal_market`
21. "A player who invests in addiction treatment while dealing drugs builds community influence" → `scenario_treatment_investment_generates_influence_despite_source`
22. "NPC satisfaction affects behavior: dissatisfied worker is more likely to leave, whistle-blow, organize" → `scenario_low_satisfaction_produces_npc_adverse_action`

*These 22 scenario tests are the most important tests in the project. They directly encode design intent. Section 12.3 (trafficking) and Section 12.9 (infiltration) scenarios should be tagged but marked expansion-only so the Bootstrapper skips them.*

---

## PRIORITY 4 — SCOPE MARKERS

*One structural issue that affects the entire Technical Design.*

### S1 — Technical Design has no V1 / expansion markers

**Issue:** The GDD marks expansion and cut content with explicit tags. The Feature Tier List is the definitive scope document. But the Technical Design has no markers indicating which sections apply to V1 and which are speculative (expansion). The Bootstrapper reads the Technical Design to generate interface specs — it could generate specs for expansion systems if it can't identify which sections are in scope.

**Fix:** Add a scope marker to every Technical Design section header:
- `[V1]` — in V1 scope, Bootstrapper should generate interface spec
- `[EX]` — expansion scope, Bootstrapper should skip
- `[CORE]` — architectural foundation, applies to all tiers

Example: Section 4 "NPC Architecture [V1]", Section 9 "Scene Card System [V1]", any future section on trafficking NPCs "[EX]"

Also: add one sentence to the Technical Design's purpose statement: "Sections marked [V1] are in scope for the simulation prototype and V1 build. Sections marked [EX] are included for architectural continuity but are not implemented until the relevant expansion. The Bootstrapper generates interface specs only for [V1] and [CORE] sections."

---

## PRIORITY 5 — CROSS-DOCUMENT INCONSISTENCIES

---

### C1 — Document counterfeiting: V1 or EX?

**Location:** Feature Tier List
**Issue:** The FTL entry for counterfeiting says "EX — document fraud needed for alternative identities — can be a simple unlock in V1." This is ambiguous: is basic document fraud for alternative identities in V1 or not? The alternative identity system is V1 (clearly), so document fraud as its enabler needs a definitive answer.

**Fix:** Decide and state clearly: "Alternative identity creation requires basic document fraud capability. For V1, document fraud is implemented as a minimal unlock — a scene card interaction with a contact who has forger access, producing an AlternativeIdentity record. The full counterfeiting economy (currency, pharmaceuticals) is EX."

---

### C2 — Personal security: Feature Tier List says EX, GDD Section 14.5 describes as present

**Location:** Feature Tier List, GDD Section 14.5
**Issue:** FTL says personal security is EX ("High-threat scenarios are late game; can ship with basic implementation in V1"). GDD Section 14.5 describes the personal security system in full without marking it expansion. The Bootstrapper will read both and be confused.

**Fix:** Add [EXPANSION] tag to GDD Section 14.5 matching the FTL entry: "Personal security system described here is Expansion scope. V1 models physical threat as a consequence queue entry only — the player receives a consequence event for high-threat states, but the protective detail management and secure location systems are not V1 features."

---

### C3 — Technical Design Section 5 reference gap

**Location:** GDD Section 6.8 says "Full satisfaction model in Technical Design Section 5"
**Issue:** Technical Design Section 5 covers economy (price model, supply chain, NPC businesses). There is no worker satisfaction model there. The cross-reference is broken.

**Fix:** Either add the worker satisfaction model to Technical Design Section 5 (as a subsection of the NPC system — arguably it belongs in Section 4 since it's derived from the NPC motivation model) or update the GDD cross-reference to point to the correct section once it's written.

---

### C4 — Player market influence thresholds not in Technical Design

**Location:** GDD Section 8.2 specifies: below 20% = price-taker, 20-50% = minor impact, 50%+ = price-mover, 70%+ = sets price floor
**Issue:** These are precise thresholds in the GDD but they're not represented anywhere in the Technical Design's economy engine section. They need to be in the code.

**Fix:** Add these thresholds as named constants to the Technical Design price model section:
```
PRICE_TAKER_THRESHOLD = 0.20
MINOR_IMPACT_THRESHOLD = 0.50
DOMINANT_THRESHOLD = 0.70
```
And specify how they affect the equilibrium calculation — does a dominant player's production target become an input to the equilibrium function?

---

### C5 — "Full satisfaction model" referenced but not written is a broken promise

**Location:** Multiple places in GDD reference Technical Design sections that don't exist
**Issue:** Beyond the satisfaction model (C3 above), the GDD makes references to Technical Design sections for: the full community response model, the full investigator engine specification, and the criminal OPSEC model. All of these are missing. Any cross-reference in the GDD pointing to a Technical Design section should have a corresponding section. Currently it doesn't.

**Fix:** Either write the missing sections (Blockers B5, B6, B7 above cover the major ones) or add placeholder sections with "TO BE WRITTEN" markers so the Bootstrapper knows they exist but are incomplete.

---

### C6 — NPC business behavioral profiles: GDD and Technical Design mismatch

**Location:** GDD Section 8.7, Technical Design Section 5
**Issue:** GDD names four profiles: cost-cutter, quality-player, fast-expander, defensive-incumbent. Technical Design uses: cost_cutter, quality_player, fast_expander, defensive_incumbent. Names are consistent. But the GDD describes what each profile is (personality), not what each profile does as an algorithm. The Technical Design has the struct but no decision logic. See P11 above.

**Fix:** This is covered by P11. The Technical Design needs the decision matrix per profile.

---

## What Passes 2 and 3 Should Target

**Pass 2 (highest leverage, do these first):**
- Write missing Technical Design sections for WorldState/DeltaBuffer, PlayerCharacter, Region/Nation, Influence Network, Community Response, Political Cycle, Criminal OPSEC, Criminal Organization (B1–B8)
- Complete all enum values (B9–B12)
- Specify equilibrium price formula and NPC expected_value calculation (B13, B14)

**Pass 3 (precision and tagging):**
- Specify obligation escalation algorithm, worker satisfaction model, election resolution algorithm, random event model (B15–B18)
- Apply all precision gap fixes (P1–P14)
- Add [SCENARIO] tags to all 22 scenario candidates
- Add [V1]/[EX] scope markers to Technical Design sections

**After Pass 3:** Documents are ready for the Bootstrapper. The Bootstrapper's prompt should instruct it to skip any [EXPANSION] tagged sections entirely and to generate stub "TO_BE_IMPLEMENTED" interfaces for any section marked [V1] that still has gaps (which should be none if Pass 3 is complete).

---

*EconLife Pass 1 Review — Document produced for multi-pass revision process*
*Issues: 18 Blockers + 14 Precision Gaps + 22 Scenario Tags + 1 Scope Marker + 6 Consistency issues = 61 total*
