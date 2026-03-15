#pragma once

// Production module types.
// Core production types (NPCBusiness, recipes) are in economy_types.h.
// This header defines production-module-specific constants and types.

#include <cstdint>

namespace econlife {

// Technology tier bonuses applied during production.
struct ProductionConstants {
    static constexpr float tech_tier_output_bonus_per_tier = 0.08f;  // +8% output per tier
    static constexpr float tech_tier_cost_reduction_per_tier = 0.05f;  // -5% input cost per tier
};

}  // namespace econlife
