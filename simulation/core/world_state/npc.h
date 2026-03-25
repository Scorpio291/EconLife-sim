#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace econlife {

// Forward declarations for types defined in other domain headers
enum class VisibilityScope : uint8_t;
enum class NPCTravelStatus : uint8_t;

// ============================================================================
// NPCRole — Complete role taxonomy for significant and named-background NPCs
// ============================================================================

enum class NPCRole : uint8_t {
    // --- Political and Institutional ---
    politician = 0,          // Holds or seeks elected office. Behavior engine: campaign
                             //   mechanics, coalition management, legislative voting.
    candidate = 1,           // In active election cycle; not yet an officeholder.
                             //   Enables: campaign scene cards, endorsement requests.
    regulator = 2,           // Government agency employee with enforcement authority.
                             //   Enables: formal inquiry, permit denial, fine issuance.
    law_enforcement = 3,     // Police, investigative unit. Drives InvestigatorMeter.
                             //   Enables: arrest consequence, surveillance, raid.
    prosecutor = 4,          // Legal authority to charge and try. Enables: charges filed,
                             //   plea negotiation, case advancement.
    judge = 5,               // Adjudicates legal proceedings. Enables: ruling consequences,
                             //   sentencing, bail decisions. Corruption node when applicable.
    appointed_official = 6,  // Central bank governor, agency head. [EX] — record
                             //   exists but active mechanics are expansion scope.

    // --- Business and Corporate ---
    corporate_executive = 7,  // C-suite NPC at a significant NPC business. Makes
                              //   strategic decisions for NPCBusiness. Acquirable as contact.
    middle_manager = 8,       // Below executive; carries operational knowledge.
                              //   Whistleblower risk source. Visible in workforce scene cards.
    worker = 9,               // Employee at facility (player or NPC). Drives satisfaction
                              //   model, strike risk, whistleblower emergence.
    accountant = 10,          // Access to financial records. Enables: audit findings,
                              //   money laundering facilitation, financial evidence creation.
    banker = 11,              // Enables: suspicious transaction flagging, FIU reporting,
                              //   money laundering facilitation, credit denial.
    lawyer = 12,              // Legal defense, advice, contract execution.
                              // Criminal defense lawyer is a distinct service type.
                              // General civil lawyer is this role.
    media_editor = 13,        // Senior editorial gatekeeper at media outlet. Controls
                              //   whether journalist's story is published.

    // --- Media and Civil Society ---
    journalist = 14,        // Investigates, publishes. Primary mechanism for evidence
                            //   tokens reaching public. Motivation drives story pursuit.
    ngo_investigator = 15,  // Non-governmental investigator (human rights, environmental,
                            //   financial crime). Cannot be neutralized by domestic
                            //   political corruption. Thin role in V1.
    community_leader = 16,  // Respected figure in a region's demographic community.
                            //   High social_capital; potential opposition org founder.
    union_organizer = 17,   // [Thin in V1] Organizes workers in player-owned or NPC businesses.
                            //   Enables: collective action events, strike scene cards.

    // --- Grey-Area and Criminal ---
    criminal_operator = 18,  // Runs criminal enterprise operations (drug production,
                             //   distribution coordination). Member of CriminalOrganization.
    criminal_enforcer = 19,  // Coercive capacity for criminal org. Enables: physical
                             //   threat consequences, territorial violence, protection collection.
    fixer = 20,              // Grey-area connector. Facilitates introductions, procures
                             //   services, maintains plausible deniability.
    bodyguard = 21,          // [EX] Personal protection NPC. Placeholder role in V1;
                             //   personal security system is expansion scope.

    // --- Personal Life ---
    family_member = 22,  // Partner, child, parent. Emotional weight modifier on
                         //   memory entries. Target for leverage against player.
};

// ============================================================================
// NPCStatus — lifecycle state of an NPC
// ============================================================================

enum class NPCStatus : uint8_t {
    active = 0,      // Normal operation; participates in tick processing
    imprisoned = 1,  // Removed from active behavior; may re-enter on release
    dead = 2,        // Permanently inactive; retained for forensic/memory references
    fled = 3,        // Left the region; may return under specific conditions
    waiting = 4,     // Active but chose inaction this tick (EV below threshold)
};

// ============================================================================
// MemoryType — classification of NPC memory entries
// ============================================================================

enum class MemoryType : uint8_t {
    interaction = 0,  // Direct interaction with another actor
    observation = 1,  // Witnessed event without direct involvement
    hearsay = 2,      // Information received from another NPC
    event = 3,        // Simulation event that affected this NPC

    // Satisfaction-relevant subtypes (used by worker_satisfaction())
    employment_positive = 4,         // Fair treatment, raise, recognition, promotion
    employment_negative = 5,         // Pay cut, overwork, unfair treatment, demotion
    witnessed_illegal_activity = 6,  // Criminal activity at facility
    witnessed_safety_violation = 7,  // Dangerous conditions; legal or illegal
    witnessed_wage_theft = 8,        // Payroll fraud, unpaid overtime, illegal deductions
    physical_hazard = 9,             // Workplace injury, unsafe conditions
    facility_quality = 10,           // Sign depends on quality improvement or degradation
    retaliation_experienced = 11,    // Fired for organizing, threatened
};

// ============================================================================
// KnowledgeType — classification of what an NPC knows
// ============================================================================

enum class KnowledgeType : uint8_t {
    evidence_token = 0,  // NPC is aware of an EvidenceToken (token_id stored as subject_id).
                         //   Does not mean they possess it — only that they know it exists.
    relationship = 1,    // NPC knows about a relationship between two actors.
                         //   subject_id = first actor; secondary_subject_id = second actor.
    activity = 2,        // NPC knows about an ongoing activity (business op, criminal op,
                         //   investigation). subject_id = activity owner's npc_id.
    identity_link = 3,   // NPC knows a burned identity is connected to a real identity.
                         //   subject_id = cover_identity_id; secondary_subject_id = real_actor_id.
};

// ============================================================================
// KnowledgeEntry — single unit of NPC knowledge
// ============================================================================

struct KnowledgeEntry {
    uint32_t subject_id;            // primary entity this knowledge is about
    uint32_t secondary_subject_id;  // second entity, for relational knowledge types
                                    //   (0 if unused; relationship and identity_link types)
    KnowledgeType type;
    float confidence;                // 0.0–1.0; certainty. Direct observation = 1.0;
                                     //   hearsay from trusted source = 0.6–0.8;
                                     //   rumour = 0.2–0.4. Decays at
                                     //   config.knowledge.confidence_decay_rate per tick
                                     //   when not reinforced by new corroborating entries.
    uint32_t acquired_at_tick;       // when this NPC first obtained this knowledge
    uint32_t source_npc_id;          // 0 = direct observation; else npc_id of informant
    VisibilityScope original_scope;  // scope of the underlying activity/token when observed
};

// KnowledgeMap — container for NPC knowledge entries.
// Two separate maps per NPC: known_evidence (lookup by token_id) and
// known_relationships (lookup by actor pair). Separate maps because
// evidence and relationship knowledge have different access patterns
// in the behavior engine.
using KnowledgeMap = std::vector<KnowledgeEntry>;

// ============================================================================
// OutcomeType — what an NPC action might produce
// ============================================================================

enum class OutcomeType : uint8_t {
    financial_gain = 0,       // money outcomes
    security_gain = 1,        // reduces threat or uncertainty
    career_advance = 2,       // status, promotion, influence increase
    revenge = 3,              // punishes a specific party
    ideological = 4,          // advances a belief or cause
    relationship_repair = 5,  // improves a valued relationship
    self_preservation = 6,    // survival outcomes
    loyalty_obligation = 7,   // fulfills a felt duty
};

// ============================================================================
// ActionOutcome — atomic unit of what an action might produce
// ============================================================================

struct ActionOutcome {
    OutcomeType type;
    float probability;  // 0.0–1.0; estimated likelihood this outcome occurs
    float magnitude;    // 0.0–1.0; how impactful if it occurs
};

// ============================================================================
// MotivationVector — weighted motivation profile
// Invariant: weights MUST sum to 1.0 across all OutcomeType values.
// Initialized at NPC creation; can shift slowly from memory entries.
// ============================================================================

struct MotivationVector {
    // Indexed by OutcomeType; weights[static_cast<size_t>(type)] gives the weight.
    // Invariant: sum of all elements == 1.0
    std::array<float, 8> weights;  // 8 = number of OutcomeType values
};

// ============================================================================
// MemoryEntry — timestamped NPC memory
// Most expensive component per NPC. Capped at MAX_MEMORY_ENTRIES per NPC.
// Entries below decay threshold are archived (retained for forensic purposes
// like investigation mechanics, but no longer driving behavior).
// ============================================================================

static constexpr uint32_t MAX_MEMORY_ENTRIES = 500;

struct MemoryEntry {
    uint32_t tick_timestamp;
    MemoryType type;         // interaction, observation, hearsay, event, or subtype
    uint32_t subject_id;     // who/what this memory is about
    float emotional_weight;  // how significant; affects decay rate.
                             //   Sign convention for employment contexts:
                             //     positive = favorable experience
                             //     negative = unfavorable experience
                             //   Magnitude ranges per MemoryType documented in §4.5.
    float decay;             // 0.0–1.0; decays over time toward floor
    bool is_actionable;      // does this memory motivate action?
};

// ============================================================================
// Relationship — directed relationship from this NPC to another
// Stored as sparse directed graph: each NPC holds relationships only
// for NPCs they've interacted with or know about.
// Average significant NPC has 20–100 meaningful relationships.
// Graph is O(N x avg_relationships) rather than O(N^2).
// ============================================================================

struct Relationship {
    uint32_t target_npc_id;
    float trust;                           // -1.0 to 1.0
    float fear;                            // 0.0 to 1.0
    float obligation_balance;              // positive = they owe this NPC;
                                           //   negative = this NPC owes them
    std::vector<uint32_t> shared_secrets;  // evidence token ids both parties hold
    uint32_t last_interaction_tick;
    bool is_movement_ally;   // true if NPC is active collaborator in a movement
    float recovery_ceiling;  // 1.0 default (no ceiling); set below 1.0 on
                             //   catastrophic trust loss. Trust can never rebuild
                             //   above this value. See §13 floor principle.
                             //   Invariant: recovery_ceiling >= RECOVERY_CEILING_MINIMUM (0.15)
};

// RelationshipGraph — sparse directed graph container for NPC relationships.
using RelationshipGraph = std::vector<Relationship>;

// ============================================================================
// NPC — core data structure for a significant simulation NPC
//
// At full V1 scale: ~750 significant NPCs per region, ~2,000 total.
// Named background NPCs use the same struct with simplified population
// (no full memory_log, single relationship to player).
//
// Worker satisfaction is NOT a stored field — it is computed fresh from
// the memory log when needed via worker_satisfaction(). This ensures
// satisfaction is always consistent with memory state.
//
// NPC delayed actions are DeferredWorkItem entries with
// type == WorkType::consequence and subject_id == npc.id.
// There is no separate ActionQueue. See §3.3.
// ============================================================================

struct NPC {
    uint32_t id;
    NPCRole role;  // see NPCRole enum

    // --- Motivation model ---
    MotivationVector motivations;  // weighted map of: money, career, ideology,
                                   //   revenge, stability, power, survival.
                                   //   Invariant: weights sum to 1.0.
    float risk_tolerance;          // 0.0–1.0; how likely to act on knowledge

    // --- Memory ---
    std::vector<MemoryEntry>
        memory_log;  // timestamped entries, capped at MAX_MEMORY_ENTRIES
                     //   Invariant: memory_log.size() <= MAX_MEMORY_ENTRIES (500)

    // --- Knowledge ---
    KnowledgeMap known_evidence;       // evidence tokens this NPC is aware of
    KnowledgeMap known_relationships;  // what they know about who knows whom

    // --- Relationships ---
    RelationshipGraph relationships;  // directed: NPC's view of each other NPC
                                      //   not stored as full matrix — sparse representation

    // --- Resources ---
    float capital;                      // money available
    float social_capital;               // platform / access / authority
    std::vector<uint32_t> contact_ids;  // who they can actually reach

    // --- Movement leadership (role-conditional) ---
    uint32_t movement_follower_count;  // count of background population NPCs
                                       //   in a movement this NPC leads or co-leads.
                                       //   0 for NPCs not in movement-capable roles.
                                       //   Meaningful for: community_leader, politician,
                                       //   union_organizer, ngo_investigator.
                                       //   Updated by tick step 22.

    // --- Location — physical presence ---
    uint32_t home_province_id;      // province where NPC is based and returns to
    uint32_t current_province_id;   // province NPC is physically in this tick
                                    //   == home_province_id when not travelling.
                                    //   Updated to destination when
                                    //   DeferredWorkItem(npc_travel_arrival) fires.
    NPCTravelStatus travel_status;  // resident, in_transit, visiting (see §18.15)

    // --- State ---
    NPCStatus status;  // active, imprisoned, dead, fled, waiting
    // NOTE: NPC delayed actions are DeferredWorkItem entries with
    // type == WorkType::consequence and subject_id == npc.id.
    // There is no separate ActionQueue. See §3.3.
};

}  // namespace econlife
