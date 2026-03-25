#pragma once

// Commodity Trading Module -- sequential tick module that processes
// speculative commodity positions for players and NPCs.
//
// Handles: opening long/short positions, closing positions with P&L
// settlement, market impact from large positions, and capital gains
// tax flagging on realized profits.
//
// See docs/interfaces/commodity_trading/INTERFACE.md for the canonical specification.

#include <algorithm>
#include <string_view>
#include <vector>

#include "core/tick/tick_module.h"
#include "modules/commodity_trading/commodity_trading_types.h"
#include "modules/economy/financial_types.h"

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;

// ---------------------------------------------------------------------------
// CommodityTradingModule -- ITickModule implementation for speculative
// commodity position management and settlement.
// ---------------------------------------------------------------------------
class CommodityTradingModule : public ITickModule {
   public:
    std::string_view name() const noexcept override { return "commodity_trading"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    bool is_province_parallel() const noexcept override { return false; }

    std::vector<std::string_view> runs_after() const override { return {"price_engine"}; }

    std::vector<std::string_view> runs_before() const override { return {"npc_behavior"}; }

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // Province-parallel not used; no-op.
    void execute_province(uint32_t /*province_idx*/, const WorldState& /*state*/,
                          DeltaBuffer& /*province_delta*/) override {}

    // --- Position management (exposed for testing) ---

    // Add a new position to internal storage.
    void open_position(CommodityPosition position);

    // Close a position by id; computes P&L and sets exit_tick.
    // Returns a SettlementResult with realized P&L and capital gains tax.
    SettlementResult close_position(uint32_t position_id, float exit_price, uint32_t tick);

    // Read-only access to all positions (sorted by id ascending).
    const std::vector<CommodityPosition>& positions() const;

    // --- Static utility functions (exposed for testing) ---

    // Compute the market impact of a position given current market supply.
    // Returns zero impact if position is below threshold.
    static MarketImpact compute_market_impact(const CommodityPosition& pos, float market_supply);

    // Compute P&L for a position given entry/exit prices and quantity.
    static float compute_pnl(PositionType type, float entry_price, float exit_price,
                             float quantity);

   private:
    // Internal position storage (WorldState does not hold these).
    // Kept sorted by id ascending for deterministic processing.
    std::vector<CommodityPosition> positions_;
};

}  // namespace econlife
