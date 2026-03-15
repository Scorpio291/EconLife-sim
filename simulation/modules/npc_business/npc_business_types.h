#pragma once

// NPC business decision module types.
// Core business types (NPCBusiness, BoardComposition) are in economy_types.h.
// This header defines business-decision-specific types.

#include <cstdint>

namespace econlife {

// Business strategy profiles that drive quarterly decisions.
enum class BusinessProfile : uint8_t {
    cost_cutter = 0,
    quality_player = 1,
    fast_expander = 2,
    defensive_incumbent = 3,
};

// Result of a quarterly business decision evaluation.
struct BusinessDecisionResult {
    uint32_t business_id = 0;
    bool expand = false;
    bool contract = false;
    bool enter_new_market = false;
    int32_t hiring_target_change = 0;
    float rd_investment_rate = 0.0f;
    bool board_approved = true;
};

}  // namespace econlife
