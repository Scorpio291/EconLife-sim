#pragma once

// Technology types — data structures for the R&D and Technology module.
// See docs/design/EconLife_RnD_and_Technology_v22.md for the canonical spec.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// TechStage, TechHolding, and ActorTechnologyState are defined in shared_types.h
// (core header) to avoid circular dependencies. This header provides the
// module-specific types that build on top of them.
#include "core/world_state/shared_types.h"

namespace econlife {

// ---------------------------------------------------------------------------
// SimulationEra — ten eras from year 2000 to 2250+.
// V1 covers Eras 1-5. EX eras are engine-supported but content is post-V1.
// Era transitions are evaluated each tick via weighted scoring.
// ---------------------------------------------------------------------------
enum class SimulationEra : uint8_t {
    era_1_turn_of_millennium = 1,  // 2000-2007
    era_2_disruption = 2,          // 2007-2013
    era_3_acceleration = 3,        // 2013-2019
    era_4_fracture = 4,            // 2019-2024
    era_5_transition = 5,          // 2024-2035

    // EX eras
    era_6_convergence = 6,   // 2035-2050
    era_7_reckoning = 7,     // 2050-2075
    era_8_synthesis = 8,     // 2075-2100
    era_9_expansion = 9,     // 2100-2150
    era_10_divergence = 10,  // 2150-2250+
};

constexpr uint8_t MAX_ERA = 10;
constexpr uint8_t V1_MAX_ERA = 5;

// ---------------------------------------------------------------------------
// ResearchDomain — categorization of technology research.
// Domain knowledge levels accumulate globally from publications.
// ---------------------------------------------------------------------------
enum class ResearchDomain : uint8_t {
    // V1 domains
    materials_science = 0,
    semiconductor_physics = 1,
    chemical_synthesis = 2,
    energy_systems = 3,
    mechanical_engineering = 4,
    software_systems = 5,
    biotechnology = 6,
    climate_and_environment = 7,
    information_security = 8,
    social_media_platforms = 9,
    financial_instruments = 10,
    illicit_chemistry = 11,

    // EX domains
    nuclear_engineering = 12,
    advanced_transportation = 13,
    space_systems = 14,
    cognitive_science = 15,
    synthetic_biology = 16,
    geoengineering = 17,
    quantum_systems = 18,
};

constexpr uint8_t RESEARCH_DOMAIN_COUNT = 19;
constexpr uint8_t V1_RESEARCH_DOMAIN_COUNT = 12;

// ---------------------------------------------------------------------------
// TechnologyNode — definition of a single technology in the tree.
// Loaded from CSV; immutable after loading.
// ---------------------------------------------------------------------------
struct TechnologyNode {
    std::string node_key;
    std::string domain;         // maps to ResearchDomain name
    std::string display_name;
    uint8_t era_available = 1;  // earliest era this can be researched
    float difficulty = 1.0f;    // effort points; 0.1=days, 1.0=months, 10.0=years
    bool patentable = false;
    std::vector<std::string> prerequisites;  // node_keys that must be held first
    std::string outcome_type;   // "product_unlock", "process_improvement", "facility_unlock", "tech_tier_advance"
    std::string key_technology_node;  // node whose maturation caps quality (empty = no cap)
    std::string unlocks_recipe;       // recipe_key unlocked on research (empty = none)
    std::string unlocks_facility_type;  // facility_type_key unlocked (empty = none)
    bool is_baseline = false;   // true = available at game start, no research needed
};

// ---------------------------------------------------------------------------
// MaturationCeiling — era-gated ceiling values for a technology node.
// Loaded from CSV. A value of -1.0 means "not researchable in this era."
// ---------------------------------------------------------------------------
struct MaturationCeilingEntry {
    std::string node_key;
    float era_ceilings[MAX_ERA];  // indexed by (era - 1)
};

// ---------------------------------------------------------------------------
// ResearchProject — an active research effort targeting a node.
// Owned by a business, executed at a facility.
// ---------------------------------------------------------------------------
struct ResearchProject {
    std::string project_key;
    uint32_t business_id = 0;
    uint32_t facility_id = 0;
    std::string domain;
    std::string target_node_key;
    float difficulty = 1.0f;
    float progress = 0.0f;
    uint32_t researchers_assigned = 0;
    float funding_per_tick = 0.0f;
    float success_probability = 0.75f;
    uint32_t started_tick = 0;
    bool is_secret = false;
};

// ---------------------------------------------------------------------------
// MaturationProject — ongoing maturation investment for a held node.
// ---------------------------------------------------------------------------
struct MaturationProject {
    std::string node_key;
    uint32_t business_id = 0;
    uint32_t facility_id = 0;
    uint32_t researchers_assigned = 0;
    float funding_per_tick = 0.0f;
    float progress = 0.0f;
};

// ---------------------------------------------------------------------------
// EraTrigger — condition for era transition scoring.
// ---------------------------------------------------------------------------
struct EraTrigger {
    std::string name;
    float weight = 0.0f;
    // Condition evaluation is handled by the module; triggers are data-defined.
    // For V1, we use calendar-year as primary trigger with configurable thresholds.
    std::string condition_type;  // "calendar_year", "technology_maturation", "market_share", etc.
    float threshold = 0.0f;
    std::string parameter;  // e.g., node_key for technology conditions
};

// ---------------------------------------------------------------------------
// GlobalTechnologyState — simulation-wide technology tracking.
// Stored in WorldState. Updated by TechnologyModule each tick.
// ---------------------------------------------------------------------------
struct GlobalTechnologyState {
    SimulationEra current_era = SimulationEra::era_1_turn_of_millennium;
    uint32_t era_started_tick = 0;  // tick when current era began

    // Domain knowledge levels (0.0-1.0 per domain, rising from publications).
    float domain_knowledge[RESEARCH_DOMAIN_COUNT] = {};

    // Active research projects across all businesses.
    std::vector<ResearchProject> active_research_projects;

    // Active maturation projects across all businesses.
    std::vector<MaturationProject> active_maturation_projects;

    // Era transition triggers (loaded from config).
    std::map<uint8_t, std::vector<EraTrigger>> era_triggers;  // target_era -> triggers

    // Calendar year tracking (derived from tick: year = 2000 + tick / 365).
    uint32_t base_year = 2000;
};

// ---------------------------------------------------------------------------
// TechnologyConfig — runtime constants for the R&D system.
// Loaded from rnd_config.json. All values have sensible defaults.
// ---------------------------------------------------------------------------
struct TechnologyConfig {
    // Maturation constants
    float maturation_rate_coeff = 0.40f;
    float maturation_difficulty_per_level = 2.0f;

    // Research constants
    float base_research_success_rate = 0.75f;
    float domain_knowledge_bonus_coeff = 0.30f;
    float unexpected_discovery_probability = 0.05f;
    float patent_preemption_check_rate = 0.02f;

    // Domain knowledge
    float publication_knowledge_increment = 0.01f;
    float transfer_knowledge_increment = 0.005f;
    float knowledge_decay_rate = 0.0001f;  // per-tick

    // Maturation transfer
    float maturation_transfer_license = 0.80f;
    float maturation_transfer_reverse_eng = 0.50f;
    float maturation_transfer_public = 0.60f;
    float maturation_transfer_espionage = 0.90f;

    // Era transition
    float era_transition_threshold = 0.70f;

    // Researcher quality
    float max_researcher_skill = 20.0f;

    // Secrecy penalty
    float secrecy_penalty = 0.10f;

    // Patent duration in ticks (20 years * 365 days)
    uint32_t patent_duration_ticks = 7300;

    // Initial domain knowledge levels for Era 1 (year 2000)
    float era1_domain_knowledge[RESEARCH_DOMAIN_COUNT] = {
        0.40f,  // materials_science — mature
        0.35f,  // semiconductor_physics — active frontier
        0.40f,  // chemical_synthesis — mature
        0.35f,  // energy_systems — conventional mature, renewables early
        0.40f,  // mechanical_engineering — mature
        0.25f,  // software_systems — growing rapidly
        0.30f,  // biotechnology — genome era beginning
        0.15f,  // climate_and_environment — early stage
        0.20f,  // information_security — growing
        0.05f,  // social_media_platforms — barely exists
        0.30f,  // financial_instruments — mature, derivatives growing
        0.15f,  // illicit_chemistry — underground knowledge
        // EX domains start at 0
        0.10f,  // nuclear_engineering
        0.10f,  // advanced_transportation
        0.05f,  // space_systems
        0.05f,  // cognitive_science
        0.02f,  // synthetic_biology
        0.01f,  // geoengineering
        0.01f,  // quantum_systems
    };
};

}  // namespace econlife
