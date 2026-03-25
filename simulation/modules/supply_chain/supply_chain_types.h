#pragma once

// Supply chain module types.
// Transit/trade types are in trade_infrastructure/trade_types.h.
// This header defines supply-chain-specific matching, fulfillment, and
// configuration types.

#include <cstdint>
#include <string>

namespace econlife {

// ---------------------------------------------------------------------------
// Supply Chain Configuration Constants
//
// Transport cost and transit time formulas for V1.
// These would ultimately come from simulation_config.json; hardcoded as
// constexpr for the bootstrap phase.
// ---------------------------------------------------------------------------
struct SupplyChainConfig {
    // Transport cost = base_transport_rate * distance_km * (1 + terrain_roughness *
    // terrain_cost_coeff)
    static constexpr float base_transport_rate = 0.01f;  // cost per unit per km
    static constexpr float terrain_cost_coeff = 0.5f;    // multiplier for terrain roughness
    static constexpr float infra_speed_coeff = 0.5f;     // infrastructure bonus to speed

    // Transit time = ceil(distance_km / (mode_speed * (1 + infrastructure * infra_speed_coeff)))
    // Mode speeds in km per tick (1 tick = 1 day)
    static constexpr float road_speed = 300.0f;
    static constexpr float rail_speed = 600.0f;
    static constexpr float sea_speed = 500.0f;
    static constexpr float river_speed = 200.0f;
    static constexpr float air_speed = 2000.0f;

    // Criminal interception
    static constexpr float max_concealment_modifier = 0.40f;
    static constexpr float base_interception_risk = 0.05f;  // per-tick base risk for criminal

    // Perishable decay per tick (fraction of quantity_remaining lost per tick in transit)
    static constexpr float default_perishable_decay_rate = 0.02f;

    // LOD 1 import transit: uses sea_speed and geographic centroid distance
    static constexpr uint32_t lod1_sentinel_province_id = 0xFFFFFFFF;
};

// ---------------------------------------------------------------------------
// Order matching result for a single buy/sell pair.
// ---------------------------------------------------------------------------
struct MatchResult {
    uint32_t buyer_business_id = 0;
    uint32_t seller_business_id = 0;
    std::string good_id;
    float quantity = 0.0f;
    float unit_price = 0.0f;
    bool is_local = true;  // true if same-province, false if inter-province
};

// ---------------------------------------------------------------------------
// SellOffer / BuyOrder — per-tick market orders derived from production output
// and demand buffer. Used internally by the matching algorithm.
// ---------------------------------------------------------------------------
struct SellOffer {
    uint32_t business_id = 0;
    uint32_t province_id = 0;
    uint32_t good_id = 0;    // hashed good_id (uint32_t)
    float quantity = 0.0f;   // units available to sell
    float ask_price = 0.0f;  // minimum acceptable price
    bool is_criminal = false;
};

struct BuyOrder {
    uint32_t business_id = 0;
    uint32_t province_id = 0;
    uint32_t good_id = 0;    // hashed good_id (uint32_t)
    float quantity = 0.0f;   // units wanted
    float bid_price = 0.0f;  // maximum acceptable price
    bool is_criminal = false;
};

}  // namespace econlife
