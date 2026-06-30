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
// DemographicGroup — background-population segmentation for the population_aging
// module. 12 groups (canonical model; mirrored by population_aging_types.h).
// Enum order is the canonical iteration order for deterministic floating-point
// accumulation.
// ---------------------------------------------------------------------------
enum class DemographicGroup : uint8_t {
    youth_urban = 0,
    youth_rural = 1,
    working_urban_low = 2,
    working_urban_mid = 3,
    working_urban_high = 4,
    working_rural_low = 5,
    working_rural_mid = 6,
    working_rural_high = 7,
    retiree_urban = 8,
    retiree_rural = 9,
    student = 10,
    unemployed = 11,
};
inline constexpr uint8_t kDemographicGroupCount = 12;

// ---------------------------------------------------------------------------
// PopulationCohort — one background-population segment within a province.
// Owned and evolved by the population_aging module (income/employment
// convergence, education drift, births/deaths/aging). Per-cohort skill_supply
// (and the province aggregate_skill_supply) are deferred in V1.
// ---------------------------------------------------------------------------
struct PopulationCohort {
    DemographicGroup group = DemographicGroup::youth_urban;
    uint32_t size = 0;                    // headcount
    float median_income = 0.0f;           // currency per tick
    float education_level = 0.0f;         // 0.0-1.0
    float employment_rate = 0.0f;         // 0.0-1.0
    float political_lean = 0.0f;          // -1.0 to 1.0
    float grievance_contribution = 0.0f;  // 0.0-1.0
    float addiction_prevalence = 0.0f;    // 0.0-1.0
};

// ---------------------------------------------------------------------------
// RegionCohortStats — aggregated demographic statistics per region.
// Single home for all population-fraction monitors. Fields previously
// scattered across `RegionConditions` (addiction_rate, crime_rate,
// formal_employment_rate, criminal_dominance_index) were consolidated here
// in schema v5; new monitors (sick_rate, homeless_rate, unemployment_rate)
// were added at the same time. The denominator for every *_rate field is
// the base population (`total_population`), not the named-NPC sample.
//
// Updated each tick by domain modules through `RegionDelta` (the field-name
// mapping is identical: `addiction_rate_delta` writes into `addiction_rate`,
// `sick_rate_delta` writes into `sick_rate`, etc.). Modules emit additive
// deltas; `apply_region_deltas` accumulates and clamps to [0,1].
// ---------------------------------------------------------------------------
struct RegionCohortStats {
    // --- Population scalar ---
    uint32_t total_population = 0;

    // --- Background-population cohorts (population_aging module) ---
    // The 12 DemographicGroups; seeded at world gen, evolved monthly/annually.
    // `total_population` is recomputed as sum(cohort.size) after any change.
    std::map<DemographicGroup, PopulationCohort> cohorts;
    float mean_income = 0.0f;           // size-weighted mean of cohort median_income
    float gini_coefficient = 0.0f;      // income inequality across cohorts [0,1]
    float regional_wage_anchor = 0.0f;  // income-convergence target for cohorts
                                        //   (proxy for the labor wage market, which
                                        //   is module-private; seeded at world gen)

    // --- Age structure ---
    float median_age = 0.0f;
    float working_age_fraction = 0.0f;  // 0.0-1.0; fraction of population 18-65
    float dependency_ratio = 0.0f;      // dependents / working_age

    // --- Problem-state monitors (population fractions, 0.0-1.0) ---
    // Migrated from RegionConditions in schema v5:
    float addiction_rate = 0.0f;            // dependent + active + terminal stages
    float crime_rate = 0.0f;                // adults engaged in criminal activity
    float criminal_dominance_index = 0.0f;  // criminal economy's share of activity
    float formal_employment_rate = 0.0f;    // working-age in formal/taxed employment

    // New in schema v5:
    float sick_rate = 0.0f;          // fraction whose health is below the
                                     // illness threshold (config.healthcare.
                                     // illness_health_threshold, default 0.4).
                                     // Updated by healthcare module.
    float homeless_rate = 0.0f;      // fraction without stable housing.
                                     // Owner module TBD; field allocated so
                                     // consumers can read without re-plumbing.
    float unemployment_rate = 0.0f;  // working-age neither in formal nor
                                     // informal employment. Updated by
                                     // labor_market; distinct from
                                     // `formal_employment_rate` (which
                                     // excludes the informal economy).

    // War mortality multiplier (>= 1.0) for this province this year, published by the
    // warfare module when the province is in a war (attacker or defender) — folded into
    // mortality by population_aging (the population war-dips). 1.0 = at peace. Transient
    // (recomputed each tick; not persisted).
    float war_mortality = 1.0f;

    // Per-province territorial-conflict intensity (0-5), the demand-relevant
    // maximum of TerritorialConflictStage across the criminal organizations
    // operating in this province. Published each tick by criminal_operations
    // from the real org conflict_state (resolution maps to 0 — post-conflict, no
    // demand). Read by weapons_trafficking as the grounded conflict-demand driver
    // (replaces the criminal_dominance_index proxy). Transient: recomputed every
    // tick from the persisted orgs, so it is NOT serialized (defaults to 0 = none
    // on a fresh load, repopulated on the first tick).
    uint8_t territorial_conflict_stage = 0;

    // Subsistence food surplus: produced / needed for the whole province
    // population this tick, written by the subsistence (commons) module in
    // pre-market eras. 1.0 = exactly fed; >1.0 = a surplus that frees labor for
    // specialists/trade; <1.0 = a deficit. Defaults to 1.0 ("fed") so the field
    // is behaviour-neutral when the module is inert (modern, market-fed eras).
    float subsistence_surplus_ratio = 1.0f;

    // --- Grain logistics (the tyranny of the ox; medieval band §3.5) ---
    // Absolute haulable grain surplus this tick (output - need), published by the
    // subsistence module — the grain available to move / feed non-farmers. Zero
    // without real surplus. Transient (recomputed each tick; not persisted).
    float grain_surplus = 0.0f;
    // Per-province NET FEEDABLE SURPLUS: own grain_surplus plus what neighbours can
    // deliver across ProvinceLinks before the draft teams eat it (water >> land).
    // Published by grain_logistics; the catchment surplus a town/castle can draw on.
    // Transient (recomputed each tick; not persisted).
    float net_feedable_surplus = 0.0f;
    // URBAN (non-farm) POPULATION the catchment sustains (= net_feedable_surplus /
    // per-capita food, capped at total_population), published by grain_logistics —
    // the aggregate medieval town economy (river hubs grow towns; stranded inland
    // stays rural). Transient (recomputed each tick; not persisted).
    float urban_population = 0.0f;

    // Granary: the province's stored food (a conserved, located resource). The
    // commons food economy banks each year's production surplus here, up to a finite
    // capacity, and draws it down in deficit years — so a bad harvest doesn't
    // immediately starve the population (or its non-farming specialists). It is the
    // grounded buffer that lets a knowledge-elite survive lean years instead of a
    // hand-placed floor. Units: food (same as one tick's production/consumption);
    // empty (0) means no reserves. Behaviour-neutral in market eras (not updated).
    float food_store = 0.0f;

    // Population hardiness: how adapted this people is to its world's hazards
    // (Earth-normalized, 1.0 = Earth-adapted). A people native to a harsh world has
    // high hardiness; one bred on a gentle world, low. It DRIFTS over generations
    // toward the world's hazard level. Mortality from hazards scales with how far
    // hardiness falls short of what the world demands — so a soft people transplanted
    // onto a hard world suffer until they adapt, while natives cope.
    float hardiness = 1.0f;

    // Zero all fields. Use after construction or when resetting a province.
    void reset() { *this = RegionCohortStats{}; }
};

}  // namespace econlife
