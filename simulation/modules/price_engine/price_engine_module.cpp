// Price Engine Module — implementation.
// See price_engine_module.h for class declarations and
// docs/interfaces/price_engine/INTERFACE.md for the canonical specification.
//
// 3-Step Supply-Demand Equilibrium per regional market:
//   Step 1: Compute equilibrium price from supply/demand ratio, clamp to floor/ceiling.
//   Step 2: Apply sticky price adjustment toward equilibrium.
//   Step 3: Apply LOD 2 global commodity modifier.

#include "modules/price_engine/price_engine_module.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"
#include "modules/trade_infrastructure/trade_types.h"

namespace econlife {

// ===========================================================================
// PriceEngineModule — tick execution
// ===========================================================================

void PriceEngineModule::execute_province(uint32_t province_idx, const WorldState& state,
                                         DeltaBuffer& province_delta) {
    // Collect regional markets for this province, then sort by good_id ascending
    // for deterministic floating-point accumulation order.
    std::vector<const RegionalMarket*> province_markets;
    for (const auto& market : state.regional_markets) {
        if (market.province_id == province_idx) {
            province_markets.push_back(&market);
        }
    }

    // Sort by good_id ascending (canonical order per CLAUDE.md).
    std::sort(
        province_markets.begin(), province_markets.end(),
        [](const RegionalMarket* a, const RegionalMarket* b) { return a->good_id < b->good_id; });

    // Process each market through the 3-step algorithm.
    for (const RegionalMarket* market : province_markets) {
        // Step 1: Equilibrium computation.
        float equilibrium_price = compute_equilibrium_price(*market);

        // Step 2: Sticky price adjustment.
        float adjustment_rate = market->adjustment_rate;
        if (adjustment_rate <= 0.0f) {
            adjustment_rate = PriceEngineConstants::default_price_adjustment_rate;
        }
        float new_spot_price =
            compute_sticky_adjustment(market->spot_price, equilibrium_price, adjustment_rate);

        // Step 3: LOD 2 modifier.
        float lod2_modifier = get_lod2_modifier(market->good_id, state.lod2_price_index);
        new_spot_price *= lod2_modifier;

        // Ensure non-negative.
        if (new_spot_price < 0.0f) {
            new_spot_price = 0.0f;
        }

        // Write MarketDelta with spot_price_override only.
        // equilibrium_price is NOT overridden — it serves as the stable base_price
        // reference for future ticks. Overwriting it would create a self-referential
        // decay spiral where the price floor erodes each tick.
        MarketDelta delta{};
        delta.good_id = market->good_id;
        delta.region_id = market->province_id;
        delta.spot_price_override = new_spot_price;
        province_delta.market_deltas.push_back(delta);
    }
}

void PriceEngineModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Province-parallel modules dispatch through execute_province().
    // This fallback processes all provinces sequentially if called directly.
    for (uint32_t p = 0; p < static_cast<uint32_t>(state.provinces.size()); ++p) {
        execute_province(p, state, delta);
    }
}

// ===========================================================================
// PriceEngineModule — Step 1: Equilibrium Computation
// ===========================================================================

float PriceEngineModule::compute_equilibrium_price(const RegionalMarket& market) {
    // Use equilibrium_price as proxy for base_price (RegionalMarket has no
    // base_price field). Fall back to default if zero or negative.
    float base_price = market.equilibrium_price;
    if (base_price <= 0.0f) {
        base_price = PriceEngineConstants::default_base_price;
    }

    // Effective supply: prevent division by zero.
    float effective_supply = std::max(market.supply, PriceEngineConstants::supply_floor);

    // Demand/supply ratio drives equilibrium.
    float demand_supply_ratio = market.demand_buffer / effective_supply;

    // Raw equilibrium price.
    float eq_price = base_price * demand_supply_ratio;

    // Compute effective floor.
    float floor_price;
    if (market.export_price_floor > 0.0f) {
        floor_price = market.export_price_floor;
    } else {
        floor_price = base_price * PriceEngineConstants::export_floor_coeff;
    }

    // Compute effective ceiling.
    float ceiling_price;
    if (market.import_price_ceiling > 0.0f) {
        ceiling_price = market.import_price_ceiling;
    } else {
        ceiling_price = base_price * PriceEngineConstants::import_ceiling_coeff;
    }

    // Clamp to [floor, ceiling].
    eq_price = std::max(eq_price, floor_price);
    eq_price = std::min(eq_price, ceiling_price);

    return eq_price;
}

// ===========================================================================
// PriceEngineModule — Step 2: Sticky Price Adjustment
// ===========================================================================

float PriceEngineModule::compute_sticky_adjustment(float spot_price, float equilibrium_price,
                                                   float adjustment_rate) {
    float gap = equilibrium_price - spot_price;
    float adjustment = gap * adjustment_rate;

    // Clamp adjustment magnitude to max_price_change_per_tick * spot_price.
    float max_change = PriceEngineConstants::max_price_change_per_tick * spot_price;
    if (adjustment > max_change) {
        adjustment = max_change;
    } else if (adjustment < -max_change) {
        adjustment = -max_change;
    }

    float new_spot_price = spot_price + adjustment;

    // Ensure non-negative.
    if (new_spot_price < 0.0f) {
        new_spot_price = 0.0f;
    }

    return new_spot_price;
}

// ===========================================================================
// PriceEngineModule — Step 3: LOD 2 Modifier
// ===========================================================================

float PriceEngineModule::get_lod2_modifier(uint32_t good_id,
                                           const GlobalCommodityPriceIndex* index) {
    if (index == nullptr) {
        return 1.0f;
    }

    auto it = index->lod2_price_modifier.find(good_id);
    if (it != index->lod2_price_modifier.end()) {
        return it->second;
    }

    return 1.0f;
}

}  // namespace econlife
