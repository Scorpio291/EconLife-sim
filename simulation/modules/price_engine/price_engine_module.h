#pragma once

// Price Engine Module — province-parallel tick module that recalculates
// spot prices for all goods in all regional markets based on current
// supply and demand. Applies sticky price adjustment, government price
// floors/ceilings, and LOD 2 global modifiers.
//
// See docs/interfaces/price_engine/INTERFACE.md for the canonical specification.

#include <string_view>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "modules/price_engine/price_engine_types.h"

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;
struct RegionalMarket;
struct GlobalCommodityPriceIndex;

// ---------------------------------------------------------------------------
// PriceEngineModule — ITickModule implementation for price equilibrium
// ---------------------------------------------------------------------------
class PriceEngineModule : public ITickModule {
   public:
    explicit PriceEngineModule(const PriceModelConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "price_engine"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    std::vector<std::string_view> runs_after() const override {
        return {"supply_chain", "labor_market", "seasonal_agriculture"};
    }

    std::vector<std::string_view> runs_before() const override {
        return {"financial_distribution"};
    }

    bool is_province_parallel() const noexcept override { return true; }

    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override;

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Static utility for computing price components (exposed for testing) ---

    // Step 1: Compute the clamped equilibrium price for a single market.
    static float compute_equilibrium_price(const RegionalMarket& market);

    // Step 2: Compute the new spot price after sticky adjustment.
    static float compute_sticky_adjustment(float spot_price, float equilibrium_price,
                                           float adjustment_rate);

    // Step 3: Apply LOD 2 modifier (returns 1.0 if no modifier found).
    static float get_lod2_modifier(uint32_t good_id, const GlobalCommodityPriceIndex* index);

   private:
    PriceModelConfig cfg_;
};

}  // namespace econlife
