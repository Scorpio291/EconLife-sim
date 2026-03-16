#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace econlife {

// =============================================================================
// Labor Market and Hiring Types — TDD §30
// =============================================================================

// Forward declarations for types defined in other modules
enum class SkillDomain : uint8_t;  // defined in player.h (§4/§14)

// --- §30.1 — HiringChannel ---

enum class HiringChannel : uint8_t {
    public_board        = 0,  // Maximum applicant pool; zero employer control over who applies.
                               // Generates the broadest skill distribution. Employer reputation
                               // visible to all applicants -- bad reputation suppresses quality.

    professional_network = 1, // Narrower pool (professional + corporate cohorts only).
                               // Higher average skill level. Requires employer.relationship_network
                               // to contain at least one NPC in professional or corporate group.
                               // Unavailable to criminal businesses (criminal_sector == true).

    personal_referral   = 2,  // Smallest pool: drawn from the NPC social graph of
                               // the hiring actor's existing relationships (trust > 0.4).
                               // Highest average trust, lowest average skill variance.
                               // No reputation visibility to applicants -- applicant trusts referrer.
};

// --- §30.1 — JobPosting ---

struct JobPosting {
    uint32_t id;
    uint32_t owner_id;                  // player_id or npc_id posting the job
    uint32_t business_id;               // NPCBusiness this role is for
    uint32_t province_id;               // province where role is located; constrains applicant pool
    SkillDomain required_domain;        // primary skill domain sought
    float    min_skill_level;           // 0.0-1.0; applicants below this are filtered
    float    offered_wage;              // per-tick; compared against regional_wage_by_skill
    HiringChannel channel;
    uint32_t posted_tick;
    uint32_t expires_tick;              // posting auto-closes at this tick if unfilled
    std::vector<uint32_t> applicant_ids; // NPC ids generated at post time (§30.2)
    bool filled;

    // Invariants:
    //   min_skill_level in [0.0, 1.0]
    //   offered_wage > 0.0
    //   expires_tick > posted_tick
    //   If filled == true: at least one applicant_id was hired
};

// --- §30.1 — WorkerApplication ---

struct WorkerApplication {
    uint32_t applicant_npc_id;
    float    skill_level;               // NPC's actual level in required_domain; not shown to employer
                                        // until interview scene card fires
    float    salary_expectation;        // per-tick; NPC will not accept offers below this
                                        // computed: regional_wage * (1.0 + npc.motivation.money * 0.3)
    float    loyalty_prior;             // 0.0-1.0; estimated trust employer has with this applicant
                                        // before interview. personal_referral: referrer.trust * 0.8.
                                        // public_board/professional_network: 0.0 (unknown).
    bool     background_visible;        // true after interview scene card completes

    // Invariants:
    //   skill_level in [0.0, 1.0]
    //   salary_expectation > 0.0
    //   loyalty_prior in [0.0, 1.0]
};

// =============================================================================
// Employment Record — V1 employment tracking per NPC
//
// Stored in LaborMarketModule member state rather than on NPC struct
// (NPC struct does not yet have employer_business_id; this avoids core
// header churn during early prototyping). When the NPC struct is expanded,
// these records migrate there.
// =============================================================================

struct EmploymentRecord {
    uint32_t npc_id;
    uint32_t employer_business_id;       // 0 = unemployed
    float    offered_wage;               // per-tick wage being paid
    uint32_t hired_tick;                 // tick when employment started
    uint32_t deferred_salary_ticks;      // consecutive ticks salary was deferred
};

// =============================================================================
// NPC Skill Record — V1 per-domain skill level for an NPC
//
// Stored in LaborMarketModule. The NPC struct does not currently carry
// per-SkillDomain skill levels (only the player has PlayerSkill). For V1,
// NPC skill levels are seeded at world gen and stored here.
// =============================================================================

struct NPCSkillEntry {
    SkillDomain domain;
    float       level;     // 0.0-1.0
};

// =============================================================================
// Labor Configuration Constants — §30 / INTERFACE.md
// =============================================================================

struct LaborConfig {
    static constexpr float wage_adjustment_rate            = 0.03f;
    static constexpr float wage_floor                      = 0.01f;
    static constexpr float wage_ceiling_multiplier         = 5.0f;
    static constexpr uint32_t pool_size_public             = 12;
    static constexpr uint32_t pool_size_professional       = 5;
    static constexpr uint32_t pool_size_referral           = 3;
    static constexpr float reputation_threshold            = 0.3f;
    static constexpr float reputation_pool_penalty_scale   = 8.0f;
    static constexpr float salary_premium_per_rep_point    = 0.5f;
    static constexpr float voluntary_departure_threshold   = 0.35f;
    static constexpr float departure_base_rate             = 0.08f;
    static constexpr float reputation_default              = 0.5f;
    static constexpr uint32_t deferred_salary_max_ticks    = 30;
    static constexpr float personal_referral_trust_min     = 0.4f;
    static constexpr uint32_t monthly_tick_interval        = 30;
};

// =============================================================================
// Per-province wage state — stored in module member state
// Key: (province_id, SkillDomain) -> regional wage per tick
// =============================================================================

struct ProvinceSkillKey {
    uint32_t    province_id;
    SkillDomain domain;

    bool operator==(const ProvinceSkillKey& o) const noexcept {
        return province_id == o.province_id
            && domain == o.domain;
    }
};

struct ProvinceSkillKeyHash {
    std::size_t operator()(const ProvinceSkillKey& k) const noexcept {
        // Combine province_id and domain into a single hash.
        return std::hash<uint64_t>{}(
            (static_cast<uint64_t>(k.province_id) << 8)
            | static_cast<uint64_t>(k.domain));
    }
};

using RegionalWageMap = std::unordered_map<ProvinceSkillKey,
                                            float,
                                            ProvinceSkillKeyHash>;

}  // namespace econlife
