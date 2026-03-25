#pragma once

// alternative_identity module types.
// See docs/interfaces/alternative_identity/INTERFACE.md for the canonical specification.

#include <cstdint>
#include <string>
#include <vector>

namespace econlife {

// ---------------------------------------------------------------------------
// IdentityStatus — lifecycle state of an alternative identity
// ---------------------------------------------------------------------------
enum class IdentityStatus : uint8_t {
    active = 0,   // currently in use; evidence routes to alias_id
    dormant = 1,  // established but not active
    burned = 2,   // compromised; identity link discovered
    retired = 3,  // voluntarily deactivated; no longer maintained
};

// ---------------------------------------------------------------------------
// AlternativeIdentity — a single alias record
// ---------------------------------------------------------------------------
struct AlternativeIdentity {
    uint32_t alias_id;       // unique id for this identity
    uint32_t real_actor_id;  // the actual person behind the alias
    IdentityStatus status;
    float documentation_quality;  // 0.0-1.0; decays 0.001/tick, builds 0.005/action
    uint32_t created_tick;
    uint32_t last_maintenance_tick;
    uint32_t burn_tick;  // tick when identity was burned (0 = not burned)
    std::vector<uint32_t> attributed_evidence_ids;  // evidence tokens pointing to alias_id
    std::vector<uint32_t> witness_npc_ids;          // NPCs who know the identity link
};

}  // namespace econlife
