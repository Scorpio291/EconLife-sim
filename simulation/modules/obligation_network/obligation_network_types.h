#pragma once

// obligation_network module types.
// Module-specific types for the obligation_network module (Tier 6).
// Core shared types (ObligationNode, FavorType) are in shared_types.h.
//
// See docs/interfaces/obligation_network/INTERFACE.md for the canonical specification.

#include <cstdint>
#include <string>
#include <vector>

namespace econlife {

// ---------------------------------------------------------------------------
// ObligationStatus — lifecycle state of an obligation
// ---------------------------------------------------------------------------
enum class ObligationStatus : uint8_t {
    open      = 0,  // Obligation active, not yet overdue or below escalation threshold
    escalated = 1,  // current_demand > original_value * escalation_threshold (1.5x)
    critical  = 2,  // current_demand > original_value * critical_threshold (3.0x)
    hostile   = 3,  // critical + creditor risk_tolerance > hostile_threshold
    fulfilled = 4,  // Obligation settled by debtor
    forgiven  = 5,  // Obligation written off (creditor dead, timeout, renegotiation)
};

// ---------------------------------------------------------------------------
// EscalationStep — history entry for a status transition
// ---------------------------------------------------------------------------
struct EscalationStep {
    uint32_t         tick;
    ObligationStatus from_status;
    ObligationStatus to_status;
};

}  // namespace econlife
