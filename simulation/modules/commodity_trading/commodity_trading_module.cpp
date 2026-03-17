// Commodity Trading Module -- implementation.
// See commodity_trading_module.h for class declarations and
// docs/interfaces/commodity_trading/INTERFACE.md for the canonical specification.
//
// Processing order per tick:
//   1. Update current_value for all open positions using latest spot prices.
//   2. Compute market impact for open positions exceeding the threshold.
//   3. Emit MarketDelta entries for supply/demand impact.
//
// Position open/close is handled via open_position() and close_position()
// which are invoked by the behavior engine or player action queue (not
// directly during execute()). The execute() step handles valuation updates
// and market impact propagation for all currently open positions.

#include "modules/commodity_trading/commodity_trading_module.h"
#include "core/world_state/world_state.h"
#include "core/world_state/delta_buffer.h"

#include <algorithm>
#include <cmath>

namespace econlife {

// ===========================================================================
// CommodityTradingModule -- tick execution
// ===========================================================================

void CommodityTradingModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Process all open positions in id-ascending order (deterministic).
    // Positions are already kept sorted by id.
    for (auto& pos : positions_) {
        // Skip already-closed positions.
        if (pos.exit_tick != 0) {
            continue;
        }

        // Find the regional market for this position's good and province.
        const RegionalMarket* market = nullptr;
        for (const auto& rm : state.regional_markets) {
            if (rm.good_id == pos.good_id && rm.province_id == pos.province_id) {
                market = &rm;
                break;
            }
        }

        if (market == nullptr) {
            // Invalid market reference; skip position (failure mode per spec).
            continue;
        }

        // Update current_value (derived; recomputed each tick).
        pos.current_value = pos.quantity * market->spot_price;

        // Compute and emit market impact for large positions.
        MarketImpact impact = compute_market_impact(pos, market->supply);

        if (impact.demand_impact != 0.0f || impact.supply_impact != 0.0f) {
            MarketDelta md{};
            md.good_id = pos.good_id;
            md.region_id = pos.province_id;

            if (impact.demand_impact != 0.0f) {
                md.demand_buffer_delta = impact.demand_impact;
            }
            if (impact.supply_impact != 0.0f) {
                md.supply_delta = impact.supply_impact;
            }

            delta.market_deltas.push_back(md);
        }
    }
}

// ===========================================================================
// Position Management
// ===========================================================================

void CommodityTradingModule::open_position(CommodityPosition position) {
    // Ensure exit_tick and realised_pnl are zero for a newly opened position.
    position.exit_tick = 0;
    position.realised_pnl = 0.0f;

    positions_.push_back(position);

    // Maintain sorted order by id ascending for deterministic processing.
    std::sort(positions_.begin(), positions_.end(),
              [](const CommodityPosition& a, const CommodityPosition& b) {
                  return a.id < b.id;
              });
}

SettlementResult CommodityTradingModule::close_position(uint32_t position_id,
                                                         float exit_price,
                                                         uint32_t tick) {
    SettlementResult result{};
    result.position_id = position_id;
    result.position_closed = false;

    // Find the position by id.
    for (auto& pos : positions_) {
        if (pos.id == position_id) {
            // Reject duplicate close (exit_tick already set).
            if (pos.exit_tick != 0) {
                return result;
            }

            // Compute realized P&L.
            float pnl = compute_pnl(pos.position_type, pos.entry_price, exit_price, pos.quantity);

            // Update position state.
            pos.exit_tick = tick;
            pos.realised_pnl = pnl;

            // Build settlement result.
            result.holder_npc_id = pos.actor_id;
            result.realized_pnl = pnl;
            result.position_closed = true;

            // Capital gains tax: applied only on positive realized gains.
            float taxable_gain = std::max(0.0f, pnl);
            result.capital_gains_tax = taxable_gain * CommodityTradingConstants::capital_gains_tax_rate;

            return result;
        }
    }

    // Position not found; return empty result.
    return result;
}

const std::vector<CommodityPosition>& CommodityTradingModule::positions() const {
    return positions_;
}

// ===========================================================================
// Static Utility: Market Impact Calculation
// ===========================================================================

MarketImpact CommodityTradingModule::compute_market_impact(const CommodityPosition& pos,
                                                            float market_supply) {
    MarketImpact impact{};
    impact.good_id_hash = pos.good_id;

    // Prevent division by zero; no impact if supply is zero or negative.
    if (market_supply <= 0.0f) {
        return impact;
    }

    // Fraction of supply this position represents.
    float fraction = pos.quantity / market_supply;

    // No impact below the threshold.
    if (fraction <= CommodityTradingConstants::market_impact_threshold) {
        return impact;
    }

    // Impact magnitude: coefficient * quantity beyond the threshold.
    float excess_quantity = pos.quantity - (market_supply * CommodityTradingConstants::market_impact_threshold);
    float impact_magnitude = CommodityTradingConstants::market_impact_coefficient * excess_quantity;

    // Long positions increase demand; short positions increase supply pressure.
    if (pos.position_type == PositionType::long_position) {
        impact.demand_impact = impact_magnitude;
    } else {
        impact.supply_impact = impact_magnitude;
    }

    return impact;
}

// ===========================================================================
// Static Utility: P&L Calculation
// ===========================================================================

float CommodityTradingModule::compute_pnl(PositionType type,
                                           float entry_price,
                                           float exit_price,
                                           float quantity) {
    if (type == PositionType::long_position) {
        // Long: profit when price rises.
        return (exit_price - entry_price) * quantity;
    } else {
        // Short: profit when price falls.
        return (entry_price - exit_price) * quantity;
    }
}

}  // namespace econlife
