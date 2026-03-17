#pragma once

// Commodity trading module types.
// Core position types (CommodityPosition, PositionType) are in economy/financial_types.h.
// This header defines trading-specific types.

#include <cstdint>

namespace econlife {

// Result of settling a commodity position.
struct SettlementResult {
    uint32_t position_id = 0;
    uint32_t holder_npc_id = 0;
    float realized_pnl = 0.0f;       // Positive = profit, negative = loss
    float capital_gains_tax = 0.0f;   // Flagged for government_budget
    bool position_closed = false;
};

// Market impact from speculative volume.
struct MarketImpact {
    uint32_t good_id_hash = 0;
    float supply_impact = 0.0f;   // Added to next tick's supply/demand
    float demand_impact = 0.0f;
};

// Constants for commodity trading calculations.
struct CommodityTradingConstants {
    static constexpr float market_impact_threshold = 0.05f;   // 5% of market supply triggers impact
    static constexpr float market_impact_coefficient = 0.01f; // impact per unit beyond threshold
    static constexpr float capital_gains_tax_rate = 0.15f;    // 15% on realized gains
};

}  // namespace econlife
