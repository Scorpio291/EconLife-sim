#pragma once

// criminal_operations module types.
// Module-specific types for the criminal_operations module (Tier 7).
//
// See docs/interfaces/criminal_operations/INTERFACE.md for the canonical specification.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace econlife {

// ---------------------------------------------------------------------------
// TerritorialConflictStage — escalation states for criminal org conflicts
// ---------------------------------------------------------------------------
enum class TerritorialConflictStage : uint8_t {
    none                     = 0,
    economic                 = 1,  // price undercutting, supplier competition
    intelligence_harassment  = 2,  // surveillance, intimidation, info theft
    property_violence        = 3,  // arson, sabotage, facility destruction
    personnel_violence       = 4,  // targeted attacks on personnel
    open_warfare             = 5,  // full-scale territorial violence
    resolution               = 6,  // de-escalation or one party eliminated
};

// ---------------------------------------------------------------------------
// CriminalStrategicDecision — output of quarterly decision evaluation
// ---------------------------------------------------------------------------
enum class CriminalStrategicDecision : uint8_t {
    maintain              = 0,
    reduce_activity       = 1,  // lower profile due to LE heat
    expand_territory      = 2,  // dispatch team to adjacent province
    initiate_conflict     = 3,  // start territorial conflict with rival
    reduce_headcount      = 4,  // cut costs when cash is low
};

// ---------------------------------------------------------------------------
// CriminalOrganization — full org record
// ---------------------------------------------------------------------------
struct CriminalOrganization {
    uint32_t id;
    uint32_t leadership_npc_id;
    std::vector<uint32_t> member_npc_ids;
    std::vector<uint32_t> income_source_ids;  // NPCBusiness ids
    float cash;
    uint32_t strategic_decision_tick;
    uint8_t  decision_day_offset;       // hash(id) % 90

    // Per-province dominance
    std::map<uint32_t, float> dominance_by_province;

    // Conflict state
    TerritorialConflictStage conflict_state;
    uint32_t conflict_rival_org_id;     // 0 = none
};

// ---------------------------------------------------------------------------
// ExpansionTeam — dispatched expansion operation
// ---------------------------------------------------------------------------
struct ExpansionTeam {
    uint32_t org_id;
    uint32_t target_province_id;
    std::vector<uint32_t> member_npc_ids;
    float investment;
    uint32_t arrival_tick;
};

}  // namespace econlife
