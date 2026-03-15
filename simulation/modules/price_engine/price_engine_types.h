#pragma once

// Price engine module types.
// Core market types (RegionalMarket) are in economy_types.h.
// This header defines price-engine-specific calculation types.

#include <cstdint>

namespace econlife {

// Constants for price calculation.
struct PriceEngineConstants {
    static constexpr float supply_floor = 0.01f;          // Prevents division by zero
    static constexpr float default_price_adjustment_rate = 0.10f;  // 10% per tick toward equilibrium
    static constexpr float max_price_change_per_tick = 0.25f;  // 25% max swing
};

}  // namespace econlife
