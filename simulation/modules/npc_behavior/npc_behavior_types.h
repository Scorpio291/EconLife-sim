#pragma once

// NPC behavior module types.
// Core NPC types (NPC, MotivationVector, MemoryEntry) are in core/world_state/npc.h.
// This header defines behavior-decision-specific types.

#include <cstdint>

namespace econlife {

// Daily action categories that NPCs choose between.
enum class DailyAction : uint8_t {
    work = 0,
    shop = 1,
    socialize = 2,
    rest = 3,
    migrate = 4,
    seek_employment = 5,
    criminal_activity = 6,
    whistleblow = 7,
};

// Expected value calculation result for a potential action.
struct ActionEvaluation {
    DailyAction action = DailyAction::work;
    float expected_value = 0.0f;  // Weighted sum of motivation contributions
    float risk_discount = 1.0f;   // Multiplier for risk-averse NPCs
    float net_utility = 0.0f;     // expected_value * risk_discount
};

}  // namespace econlife
