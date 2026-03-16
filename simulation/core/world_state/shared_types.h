#pragma once

// Shared stub types — types that are needed by core headers (world_state.h,
// delta_buffer.h, player.h) but are properly defined in module headers that
// haven't been implemented yet. Each stub contains the minimum fields needed
// for the core to compile and for initial integration tests.
//
// As modules are implemented, these stubs will be expanded with full fields
// from INTERFACE.md specs. The canonical definitions remain here to avoid
// circular dependencies between core and module headers.

#include <cstdint>
#include <string>
#include <vector>

namespace econlife {

// ---------------------------------------------------------------------------
// ConsequenceType — used by calendar_types.h (DeadlineConsequence)
// Will be expanded during evidence/consequence module implementation (Tier 6).
// ---------------------------------------------------------------------------
enum class ConsequenceType : uint8_t {
    legal_proceeding    = 0,
    regulatory_action   = 1,
    financial_penalty   = 2,
    reputation_impact   = 3,
    relationship_change = 4,
    physical_harm       = 5,
    property_damage     = 6,
    investigation_event = 7,
};

// ---------------------------------------------------------------------------
// EvidenceType — classification of evidence tokens
// Will be expanded during evidence module implementation (Tier 6).
// ---------------------------------------------------------------------------
enum class EvidenceType : uint8_t {
    financial   = 0,  // money trail, account records, transaction logs
    physical    = 1,  // material evidence, forensics, contraband
    testimonial = 2,  // witness statement, informant disclosure
    documentary = 3,  // documents, contracts, communications
    digital     = 4,  // electronic records, surveillance data
};

// ---------------------------------------------------------------------------
// EvidenceToken — unit of evidence in the simulation
// Used in DeltaBuffer (std::optional) and WorldState (std::vector).
// Will be expanded during evidence module implementation (Tier 6).
// ---------------------------------------------------------------------------
struct EvidenceToken {
    uint32_t     id;
    EvidenceType type;
    uint32_t     source_npc_id;       // NPC who generated or holds this evidence
    uint32_t     target_npc_id;       // NPC this evidence is about (0 = general)
    float        actionability;       // 0.0-1.0; how actionable by investigators
    float        decay_rate;          // per-tick decay of actionability
    uint32_t     created_tick;
    uint32_t     province_id;         // where the evidence originated
    bool         is_active;           // false = retired/suppressed
};

// ---------------------------------------------------------------------------
// FavorType — classification of obligation/favor types
// Will be expanded during obligation_network module implementation (Tier 6).
// ---------------------------------------------------------------------------
enum class FavorType : uint8_t {
    financial_loan        = 0,
    political_support     = 1,
    evidence_suppressed   = 2,
    whistleblower_silenced = 3,
    business_favor        = 4,
    personal_favor        = 5,
    criminal_cooperation  = 6,
};

// ---------------------------------------------------------------------------
// ObligationNode — directed obligation between two actors
// Used in DeltaBuffer (std::vector) and WorldState (std::vector).
// Will be expanded during obligation_network module implementation (Tier 6).
// ---------------------------------------------------------------------------
struct ObligationNode {
    uint32_t  id;
    uint32_t  creditor_npc_id;    // who is owed
    uint32_t  debtor_npc_id;      // who owes
    FavorType favor_type;
    float     weight;             // 0.0-1.0; significance of the obligation
    uint32_t  created_tick;
    bool      is_active;          // false = fulfilled or expired
};

// ---------------------------------------------------------------------------
// InfluenceNetworkHealth — summary of player's influence network
// Used in PlayerCharacter. Computed by tick step 22.
// Will be expanded during influence_network module implementation (Tier 10).
// ---------------------------------------------------------------------------
struct InfluenceNetworkHealth {
    float overall_score;       // 0.0-1.0; composite health metric
    float network_reach;       // 0.0-1.0; how many actors player can influence
    float network_density;     // 0.0-1.0; interconnectedness of contacts
    float vulnerability;       // 0.0-1.0; how exposed the network is
};

// ---------------------------------------------------------------------------
// ActorTechnologyState — per-actor technology portfolio
// Used in NPCBusiness. See R&D and Technology doc Part 3.5.
// Will be expanded during R&D/technology module implementation.
// ---------------------------------------------------------------------------
struct ActorTechnologyState {
    float effective_tech_tier;   // derived; max over active facilities
    // MaturationProject tracking, research_investment, unlocked nodes
    // will be added during R&D module implementation
};

// ---------------------------------------------------------------------------
// DialogueLine — NPC dialogue line in a scene card
// Used in SceneCard (std::vector).
// Will be expanded during scene_cards module implementation (Tier 1).
// ---------------------------------------------------------------------------
struct DialogueLine {
    uint32_t    speaker_npc_id;
    std::string text;
    float       emotional_tone;    // -1.0 (hostile) to 1.0 (warm)
};

// ---------------------------------------------------------------------------
// PlayerChoice — player choice option in a scene card
// Used in SceneCard (std::vector).
// Will be expanded during scene_cards module implementation (Tier 1).
// ---------------------------------------------------------------------------
struct PlayerChoice {
    uint32_t    id;
    std::string label;
    std::string description;
    uint32_t    consequence_id;    // links to consequence system (0 = no consequence)
};

// ---------------------------------------------------------------------------
// RegionCohortStats — aggregated demographic statistics per region
// Used in Province (as pointer). Updated by population_aging module.
// Will be expanded during population_aging module implementation (Tier 11).
// ---------------------------------------------------------------------------
struct RegionCohortStats {
    uint32_t total_population;
    float    median_age;
    float    working_age_fraction;  // 0.0-1.0; fraction of population 18-65
    float    dependency_ratio;      // dependents / working_age
};

}  // namespace econlife
