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
#include <map>
#include <string>
#include <vector>

namespace econlife {

// ---------------------------------------------------------------------------
// ConsequenceType — used by calendar_types.h (DeadlineConsequence)
// Will be expanded during evidence/consequence module implementation (Tier 6).
// ---------------------------------------------------------------------------
enum class ConsequenceType : uint8_t {
    legal_proceeding = 0,
    regulatory_action = 1,
    financial_penalty = 2,
    reputation_impact = 3,
    relationship_change = 4,
    physical_harm = 5,
    property_damage = 6,
    investigation_event = 7,
};

// ---------------------------------------------------------------------------
// EvidenceType — classification of evidence tokens
// Will be expanded during evidence module implementation (Tier 6).
// ---------------------------------------------------------------------------
enum class EvidenceType : uint8_t {
    financial = 0,    // money trail, account records, transaction logs
    physical = 1,     // material evidence, forensics, contraband
    testimonial = 2,  // witness statement, informant disclosure
    documentary = 3,  // documents, contracts, communications
    digital = 4,      // electronic records, surveillance data
};

// ---------------------------------------------------------------------------
// EvidenceToken — unit of evidence in the simulation
// Used in DeltaBuffer (std::optional) and WorldState (std::vector).
// Will be expanded during evidence module implementation (Tier 6).
// ---------------------------------------------------------------------------
struct EvidenceToken {
    uint32_t id;
    EvidenceType type;
    uint32_t source_npc_id;  // NPC who generated or holds this evidence
    uint32_t target_npc_id;  // NPC this evidence is about (0 = general)
    float actionability;     // 0.0-1.0; how actionable by investigators
    float decay_rate;        // per-tick decay of actionability
    uint32_t created_tick;
    uint32_t province_id;  // where the evidence originated
    bool is_active;        // false = retired/suppressed
};

// ---------------------------------------------------------------------------
// FavorType — classification of obligation/favor types
// Will be expanded during obligation_network module implementation (Tier 6).
// ---------------------------------------------------------------------------
enum class FavorType : uint8_t {
    financial_loan = 0,
    political_support = 1,
    evidence_suppressed = 2,
    whistleblower_silenced = 3,
    business_favor = 4,
    personal_favor = 5,
    criminal_cooperation = 6,
};

// ---------------------------------------------------------------------------
// ObligationNode — directed obligation between two actors
// Used in DeltaBuffer (std::vector) and WorldState (std::vector).
// Will be expanded during obligation_network module implementation (Tier 6).
// ---------------------------------------------------------------------------
struct ObligationNode {
    uint32_t id;
    uint32_t creditor_npc_id;  // who is owed
    uint32_t debtor_npc_id;    // who owes
    FavorType favor_type;
    float weight;  // 0.0-1.0; significance of the obligation
    uint32_t created_tick;
    bool is_active;  // false = fulfilled or expired
};

// ---------------------------------------------------------------------------
// InfluenceNetworkHealth — summary of player's influence network
// Used in PlayerCharacter. Computed by tick step 22.
// Will be expanded during influence_network module implementation (Tier 10).
// ---------------------------------------------------------------------------
struct InfluenceNetworkHealth {
    float overall_score;    // 0.0-1.0; composite health metric
    float network_reach;    // 0.0-1.0; how many actors player can influence
    float network_density;  // 0.0-1.0; interconnectedness of contacts
    float vulnerability;    // 0.0-1.0; how exposed the network is
};

// ---------------------------------------------------------------------------
// TechStage — lifecycle stage of a technology holding.
// ---------------------------------------------------------------------------
enum class TechStage : uint8_t {
    researched = 0,      // actor has capability; internal production only
    commercialized = 1,  // product on open market; observable by competitors
};

// ---------------------------------------------------------------------------
// TechHolding — per-actor ownership of a technology node.
// Each business that has researched or acquired a node holds one.
// ---------------------------------------------------------------------------
struct TechHolding {
    std::string node_key;
    uint32_t holder_id = 0;  // business_id
    TechStage stage = TechStage::researched;
    float maturation_level = 0.0f;    // 0.0-1.0; rises with continued investment
    float maturation_ceiling = 0.0f;  // era-gated max; recomputed each tick
    uint32_t researched_tick = 0;
    uint32_t commercialized_tick = 0;  // 0 if still Stage 1
    bool has_patent = false;
    bool internal_use_only = false;  // true = no market listing despite commercialized
};

// ---------------------------------------------------------------------------
// ActorTechnologyState — per-actor technology portfolio.
// Used in NPCBusiness. See R&D and Technology doc Part 3.5.
// ---------------------------------------------------------------------------
struct ActorTechnologyState {
    float effective_tech_tier = 0.0f;  // derived; max over active facilities

    // Per-node technology holdings. Key = node_key.
    std::map<std::string, TechHolding> holdings;

    // Query methods (inline for header-only availability).
    bool has_researched(const std::string& node_key) const { return holdings.count(node_key) > 0; }

    bool has_commercialized(const std::string& node_key) const {
        auto it = holdings.find(node_key);
        if (it == holdings.end())
            return false;
        return it->second.stage == TechStage::commercialized;
    }

    // Returns maturation_level for the given node, or 0.0 if not held.
    float maturation_of(const std::string& node_key) const {
        auto it = holdings.find(node_key);
        if (it == holdings.end())
            return 0.0f;
        return it->second.maturation_level;
    }
};

// ---------------------------------------------------------------------------
// DialogueLine — NPC dialogue line in a scene card
// Used in SceneCard (std::vector).
// Will be expanded during scene_cards module implementation (Tier 1).
// ---------------------------------------------------------------------------
struct DialogueLine {
    uint32_t speaker_npc_id;
    std::string text;
    float emotional_tone;  // -1.0 (hostile) to 1.0 (warm)
};

// ---------------------------------------------------------------------------
// PlayerChoice — player choice option in a scene card
// Used in SceneCard (std::vector).
// Will be expanded during scene_cards module implementation (Tier 1).
// ---------------------------------------------------------------------------
struct PlayerChoice {
    uint32_t id;
    std::string label;
    std::string description;
    uint32_t consequence_id;  // links to consequence system (0 = no consequence)
};

// ---------------------------------------------------------------------------
// RegionCohortStats — aggregated demographic statistics per region
// Used in Province (as pointer). Updated by population_aging module.
// Will be expanded during population_aging module implementation (Tier 11).
// ---------------------------------------------------------------------------
struct RegionCohortStats {
    uint32_t total_population;
    float median_age;
    float working_age_fraction;  // 0.0-1.0; fraction of population 18-65
    float dependency_ratio;      // dependents / working_age
};

}  // namespace econlife
