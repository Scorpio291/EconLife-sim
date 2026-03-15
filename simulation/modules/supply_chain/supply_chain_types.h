#pragma once

// Supply chain module types.
// Transit/trade types are in trade_infrastructure/trade_types.h.
// This header defines supply-chain-specific matching and fulfillment types.

#include <cstdint>
#include <string>

namespace econlife {

// Order matching result for a single buy/sell pair.
struct MatchResult {
    uint32_t buyer_business_id = 0;
    uint32_t seller_business_id = 0;
    std::string good_id;
    float quantity = 0.0f;
    float unit_price = 0.0f;
    bool is_local = true;  // true if same-province, false if inter-province
};

}  // namespace econlife
