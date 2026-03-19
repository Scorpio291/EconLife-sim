#pragma once

// protection_rackets module types.
// Module-specific types for the protection_rackets module (Tier 8).
//
// See docs/interfaces/protection_rackets/INTERFACE.md for the canonical specification.

#include <cstdint>
#include <string>
#include <vector>

namespace econlife {

// ---------------------------------------------------------------------------
// RacketStatus — lifecycle state of a protection racket
// ---------------------------------------------------------------------------
enum class RacketStatus : uint8_t {
    active   = 0,  // actively collecting payment
    refused  = 1,  // target refused payment; escalation in progress
    lapsed   = 2,  // org stopped collecting (business bankrupt, exited)
    expelled = 3,  // org expelled from province
};

// ---------------------------------------------------------------------------
// RacketEscalationStage — escalation stages when payment is refused
// Thresholds (ticks overdue): warning=5, property_damage=15, violence=30, abandonment=60
// ---------------------------------------------------------------------------
enum class RacketEscalationStage : uint8_t {
    demand_issued    = 0,
    warning          = 1,  // >= 5 ticks overdue; intimidation scene card
    property_damage  = 2,  // >= 15 ticks overdue; facility_incident severity 0.4
    violence         = 3,  // >= 30 ticks overdue; personnel violence; LE multiplier 3.0
    abandonment      = 4,  // >= 60 ticks overdue; business bankruptcy/exit
};

// ---------------------------------------------------------------------------
// ProtectionRacket — a single racket record
// ---------------------------------------------------------------------------
struct ProtectionRacket {
    uint32_t id;
    uint32_t criminal_org_id;
    uint32_t target_business_id;
    float demand_per_tick;                  // = target.revenue_per_tick * demand_rate
    RacketStatus status;
    RacketEscalationStage escalation_stage;
    uint32_t last_payment_tick;
    uint32_t demand_issued_tick;
    float community_grievance_contribution; // = grievance_per_demand_unit * demand_per_tick
};

}  // namespace econlife
