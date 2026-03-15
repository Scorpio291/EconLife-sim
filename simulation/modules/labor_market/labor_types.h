#pragma once

#include <cstdint>
#include <vector>

namespace econlife {

// =============================================================================
// Labor Market and Hiring Types — TDD §30
// =============================================================================

// Forward declarations for types defined in other modules
enum class SkillDomain : uint8_t;  // defined in NPC/population types (§4/§14)

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

}  // namespace econlife
