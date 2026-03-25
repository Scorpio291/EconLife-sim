#pragma once

// community_response module types.
// Module-specific types for the community_response module (Tier 6).
// Core shared types (CommunityState) are in geography.h.
//
// See docs/interfaces/community_response/INTERFACE.md for the canonical specification.

#include <cstdint>
#include <string>
#include <vector>

namespace econlife {

// ---------------------------------------------------------------------------
// CommunityResponseStage — 7-stage escalation state machine
// ---------------------------------------------------------------------------
enum class CommunityResponseStage : uint8_t {
    quiescent = 0,               // grievance < 0.15 OR cohesion < 0.10
    informal_complaint = 1,      // grievance >= 0.15 AND cohesion >= 0.10
    organized_complaint = 2,     // grievance >= 0.28 AND cohesion >= 0.25
    political_mobilization = 3,  // grievance >= 0.42 AND institutional_trust >= 0.20
    economic_resistance = 4,     // grievance >= 0.56 AND resource_access >= 0.25
    direct_action = 5,           // grievance >= 0.70 AND cohesion >= 0.45
    sustained_opposition = 6,    // grievance >= 0.85 AND resource_access >= 0.35
};

// ---------------------------------------------------------------------------
// OppositionOrganization — formed at sustained_opposition stage
// ---------------------------------------------------------------------------
struct OppositionOrganization {
    uint32_t id;
    uint32_t province_id;
    uint32_t founding_npc_id;
    std::vector<uint32_t> member_npc_ids;
    uint32_t formed_tick;
    bool is_active;
};

}  // namespace econlife
