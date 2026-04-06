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
// All runtime-tunable constants have moved to SupplyChainModuleConfig in
// core/config/package_config.h. Only the compile-time sentinel below remains
// here because it is used as a template / switch-case constant that must be
// known at compile time.
// ---------------------------------------------------------------------------
struct SupplyChainConfig {
    // LOD 1 import transit: uses sea_speed and geographic centroid distance.
    // Compile-time sentinel — must remain static constexpr.
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
