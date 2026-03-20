#pragma once

// trust_updates module types.
// Module-specific types for the trust_updates module (Tier 10).

#include <cstdint>

namespace econlife {

struct TrustDeltaRecord {
    uint64_t source_npc_id = 0;
    uint64_t target_npc_id = 0;
    float trust_delta = 0.0f;
    bool catastrophic = false;
    float new_recovery_ceiling = 1.0f;
};

struct TrustChangeSource {
    enum class Type : uint8_t {
        positive_interaction,
        negative_interaction,
        betrayal,
        promise_fulfilled,
        promise_broken,
        evidence_discovered,
        community_event
    };
    Type type = Type::positive_interaction;
    float magnitude = 0.0f;
};

}  // namespace econlife
