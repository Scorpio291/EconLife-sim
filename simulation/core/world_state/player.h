#pragma once

#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace econlife {

// Complete type definitions needed for value members
#include "npc.h"             // Relationship (used in std::vector)
#include "shared_types.h"    // InfluenceNetworkHealth (used as value member)

// Forward declaration with underlying type — sufficient for value member
enum class NPCTravelStatus : uint8_t; // defined in trade_infrastructure/trade_types.h (§18)

// ============================================================================
// Trait — persistent character attributes (from EconLife_Trait_System.md §2)
//
// Full 25-value enum. NPCs draw from the full set using population frequency
// weights from simulation.json -> npc_trait_distribution. NPCs do not run
// the accumulator system; traits function as behavioral profile tags.
//
// Once earned, a trait is permanent unless displaced by a hard-incompatible
// trait via the Displacement mechanic (Trait System §4.3).
// ============================================================================

enum class Trait : uint8_t {
    // Cognitive
    Analytical          = 0,
    Creative            = 1,
    Scholarly            = 2,
    Intuitive           = 3,

    // Social
    Charismatic         = 4,
    Empathetic          = 5,
    Connected           = 6,
    Manipulative        = 7,

    // Volitional
    Disciplined         = 8,
    Ambitious           = 9,
    Patient             = 10,
    Impulsive           = 11,
    Resilient           = 12,
    Independent         = 13,

    // Risk / Moral
    Cautious            = 14,
    Ruthless            = 15,
    RiskTolerant        = 16,
    Paranoid            = 17,

    // Adaptive
    Adaptable           = 18,
    Stoic               = 19,
    Chameleon           = 20,

    // Physical / Domain
    PhysicallyRobust    = 21,
    StreetSmart         = 22,
    PoliticallyAstute   = 23,

    // Character
    Principled          = 24,
};

static constexpr uint8_t TRAIT_COUNT = 25;

// ============================================================================
// SkillDomain — player skill categories (GDD §4)
// ============================================================================

enum class SkillDomain : uint8_t {
    Business               = 0,
    Finance                = 1,
    Engineering            = 2,
    Politics               = 3,
    Management             = 4,
    Trade                  = 5,
    Intelligence           = 6,
    Persuasion             = 7,
    CriminalOperations     = 8,
    UndercoverInfiltration = 9,   // [EX] — see Feature Tier List
    SpecialtyCulinary      = 10,
    SpecialtyChemistry     = 11,
    SpecialtyCoding        = 12,
    SpecialtyAgriculture   = 13,
    SpecialtyConstruction  = 14,
};

// ============================================================================
// Background — socioeconomic origin selected at character creation
// ============================================================================

enum class Background : uint8_t {
    BornPoor      = 0,
    WorkingClass  = 1,
    MiddleClass   = 2,
    Wealthy       = 3,
};

// ============================================================================
// PlayerSkill — per-domain skill entry
//
// Decay rule: each tick where (current_tick - last_exercise_tick >
// SKILL_DECAY_GRACE_PERIOD), level decays by decay_rate.
// Invariant: level >= SKILL_DOMAIN_FLOOR (0.05); decay never reduces below floor.
// Using a skill sets last_exercise_tick to current_tick and applies a level gain
// proportional to the difficulty of the engagement.
// ============================================================================

static constexpr uint32_t SKILL_DECAY_GRACE_PERIOD = 30; // ticks (~1 in-game month)
static constexpr float    SKILL_DOMAIN_FLOOR       = 0.05f;

struct PlayerSkill {
    SkillDomain domain;
    float       level;                // 0.0–1.0; leveled through use
                                      //   Invariant: level >= SKILL_DOMAIN_FLOOR (0.05)
    float       decay_rate;           // per-tick reduction when domain not exercised
                                      //   default: 0.0002 per tick (~7% per in-game year of neglect)
    uint32_t    last_exercise_tick;   // used to compute accumulated decay between exercises
};

// ============================================================================
// ReputationState — public and underworld reputation axes
// ============================================================================

struct ReputationState {
    float public_business;            // -1.0 to 1.0
    float public_political;           // -1.0 to 1.0
    float public_social;              // -1.0 to 1.0
    float street;                     // -1.0 to 1.0; see criminal entry conditions (GDD §12.1)
                                      //   0.0 at character creation; built through criminal network activity
};

// ============================================================================
// HealthState — player health and lifespan tracking
//
// Invariant: current_health in [0.0, 1.0]; 0.0 = death.
// Invariant: exhaustion_accumulator in [0.0, 1.0];
//   above 0.7: performance penalty on high-stakes engagements.
// ============================================================================

struct HealthState {
    float current_health;             // 0.0–1.0; 0.0 = death
    float lifespan_projection;        // projected in-game years remaining; recalculated each tick
    float base_lifespan;              // set at character creation; 70.0–80.0 in-game years + trait modifier
    float exhaustion_accumulator;     // 0.0–1.0; above 0.7: performance penalty on high-stakes engagements
    float degradation_rate;           // composite: base age rate + violence damage + substance use modifier
};

// ============================================================================
// EvidenceAwarenessEntry — what evidence tokens the player knows about
//
// Exposure is NOT a stat. It is the set of evidence tokens the player is
// currently aware of. Tokens the player is unaware of remain only in
// WorldState.evidence_pool — "the player doesn't know about it."
//
// Updated by tick step 26 (device notifications). When an NPC contact with
// trust > EVIDENCE_SHARE_TRUST_THRESHOLD (0.45) learns of a new token,
// a ConsequenceDelta schedules a device notification with delay inversely
// proportional to trust level.
// ============================================================================

static constexpr float EVIDENCE_SHARE_TRUST_THRESHOLD = 0.45f;

struct EvidenceAwarenessEntry {
    uint32_t token_id;                // references EvidenceToken in WorldState.evidence_pool
    uint32_t discovery_tick;
    uint32_t source_npc_id;           // 0 = discovered directly
};

// ============================================================================
// MilestoneType — organic milestone categories
//
// Milestones are never announced during play. They are silently recorded as the
// player reaches them and appear retrospectively in the character legacy view
// and heir inheritance screen. The player discovers them; the game never
// points at them.
// ============================================================================

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

// ============================================================================
// MilestoneRecord — recorded achievement with context
// ============================================================================

struct MilestoneRecord {
    MilestoneType type;
    uint32_t      achieved_tick;
    std::string   context_summary;    // generated at achievement; e.g., "Valdoria Iron Works, Year 2003"
};

// ============================================================================
// ModifierSource — origin of a time-bounded calendar capacity modifier
// ============================================================================

enum class ModifierSource : uint8_t {
    disruption    = 0,  // timeline restoration disruption penalty
    health_event  = 1,  // injury, illness, hospitalization
    imprisonment  = 2,  // incarcerated; severe capacity reduction
    exhaustion    = 3,  // burnout / overwork
    travel        = 4,  // in-transit movement penalty
};

// ============================================================================
// TimeBoundedModifier — temporary calendar capacity overlay
// ============================================================================

struct TimeBoundedModifier {
    float          delta;             // e.g., -1 calendar slot/day; negative
    uint32_t       expires_tick;
    ModifierSource source;            // disruption, health_event, imprisonment, etc.
};

// ============================================================================
// TimelineRestorationRecord — one timeline restoration event
// ============================================================================

struct TimelineRestorationRecord {
    uint32_t restoration_index;       // 1-based
    uint32_t restored_to_tick;        // snapshot tick that was loaded
    uint32_t restoration_real_tick;   // tick the player was at when they restored
    uint32_t ticks_erased;            // restoration_real_tick - restored_to_tick
    uint8_t  tier_applied;            // 1, 2, or 3
};

// ============================================================================
// RestorationHistory — cumulative record of all timeline restorations
// ============================================================================

struct RestorationHistory {
    uint32_t restoration_count;                         // 0 in ironman mode; increments on each restoration
    std::vector<TimelineRestorationRecord> records;
};

// ============================================================================
// PlayerCharacter — the player's simulation agent
//
// Richer data model than a significant NPC. Stats, health state, skills,
// and relationship data are full simulation objects that the tick advances
// each day, independent of player input.
//
// Physical location obeys the physics principle (§18.14): the player can
// only take actions requiring physical presence in current_province_id.
// Calendar engagements in other provinces are only reachable if travel
// completes before due_tick.
// ============================================================================

struct PlayerCharacter {
    uint32_t id;

    // --- Character creation outputs ---
    Background           background;
    std::array<Trait, 3> traits;
    uint32_t             starting_province_id;

    // --- Stats ---
    HealthState     health;
    float           age;                    // in-game years; increments each tick by (1.0 / 365.0)
    ReputationState reputation;
    float           wealth;                 // liquid cash; can be negative (debt)
    float           net_assets;             // derived; not authoritative for transactions

    // --- Skills ---
    std::vector<PlayerSkill> skills;        // one entry per SkillDomain

    // --- Evidence awareness (Exposure) ---
    std::vector<EvidenceAwarenessEntry> evidence_awareness_map;

    // --- Obligation network ---
    std::vector<uint32_t> obligation_node_ids;

    // --- Scheduling ---
    std::vector<uint32_t> calendar_entry_ids;

    // --- Personal life ---
    uint32_t              residence_id;
    uint32_t              partner_npc_id;        // 0 = no current partner
    std::vector<uint32_t> children_npc_ids;
    uint32_t              designated_heir_npc_id; // 0 = no heir established

    // --- Relationships ---
    std::vector<Relationship> relationships;     // player's directed view of all NPCs

    // --- Influence network summary (computed by tick step 22) ---
    InfluenceNetworkHealth network_health;
    uint32_t               movement_follower_count;

    // --- Milestone tracking (see §11 "Organic Milestone Tracking") ---
    // Invariant: each MilestoneType appears at most once in achieved_milestones.
    // milestone_log is chronological; populated by tick step 27.
    std::vector<MilestoneRecord> milestone_log;
    std::set<MilestoneType>      achieved_milestones; // O(1) lookup; prevents duplicate entries

    // --- Physical location — obeys physics principle (§18.14) ---
    uint32_t        home_province_id;             // player's base province
    uint32_t        current_province_id;          // where player physically is this tick
    NPCTravelStatus travel_status;                // resident, in_transit, visiting (see §18.15)

    // --- Timeline restoration ---
    RestorationHistory              restoration_history;          // see §22a
    std::vector<TimeBoundedModifier> calendar_capacity_modifiers; // time-bounded overlays on calendar
    bool                            ironman_eligible;             // true if game_mode == ironman
                                                                  //   AND restoration_count == 0
};

}  // namespace econlife
