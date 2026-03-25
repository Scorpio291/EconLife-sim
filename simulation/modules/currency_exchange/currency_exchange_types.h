#pragma once

// currency_exchange module types.

#include <cstdint>
#include <string>

namespace econlife {

struct CurrencyRecord {
    uint32_t nation_id = 0;
    std::string iso_code;
    float usd_rate = 1.0f;           // units per 1 USD
    float usd_rate_baseline = 1.0f;  // immutable after load
    float volatility = 0.01f;
    float foreign_reserves = 1.0f;  // 0.0-1.0
    bool pegged = false;
    float peg_rate = 1.0f;
};

}  // namespace econlife
