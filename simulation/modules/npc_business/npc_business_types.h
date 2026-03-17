#pragma once

// NPC business decision module types.
// Core business types (NPCBusiness, BoardComposition, BusinessProfile) are
// in economy_types.h. This header defines business-decision-specific types.
//
// NOTE: BusinessProfile is defined in economy_types.h (included transitively
// via world_state.h). It is NOT redefined here to avoid ODR violations.

#include <cstdint>

namespace econlife {

// Result of a quarterly business decision evaluation.
struct BusinessDecisionResult {
    uint32_t business_id = 0;
    bool expand = false;
    bool contract = false;
    bool enter_new_market = false;
    int32_t hiring_target_change = 0;
    float rd_investment_rate = 0.0f;
    bool board_approved = true;
    float cash_spent = 0.0f;            // investment amount deducted from cash
    float cost_per_tick_delta = 0.0f;   // change in operating costs
};

}  // namespace econlife
