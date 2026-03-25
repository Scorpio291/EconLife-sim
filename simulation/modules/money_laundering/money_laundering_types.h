#pragma once

// money_laundering module types.
// Module-specific types for the money_laundering module (Tier 8).
// Core shared types are in their respective core headers.
//
// See docs/interfaces/money_laundering/INTERFACE.md for the canonical specification.

#include <cstdint>
#include <string>
#include <vector>

namespace econlife {

// ---------------------------------------------------------------------------
// LaunderingMethod -- six distinct methods for processing illicit cash.
// Each method has a unique evidence generation profile and risk/throughput
// trade-off.
// ---------------------------------------------------------------------------
enum class LaunderingMethod : uint8_t {
    structuring = 0,
    shell_company_chain = 1,
    real_estate = 2,
    trade_invoice = 3,
    crypto_mixing = 4,
    cash_commingling = 5,
};

// ---------------------------------------------------------------------------
// LaunderingOperation -- a single active operation converting dirty cash
// to clean cash through a specific laundering method.
// Processed once per tick by MoneyLaunderingModule.
// ---------------------------------------------------------------------------
struct LaunderingOperation {
    uint32_t id;
    uint32_t actor_id;
    LaunderingMethod method;
    float dirty_amount;
    float laundered_so_far;
    float launder_rate_per_tick;
    float conversion_loss_rate;
    uint32_t started_tick;
    uint32_t destination_business_id;  // 0 = direct to player wealth
    std::vector<uint32_t> shell_chain_business_ids;
    float evidence_generated_total;
    bool paused;
    bool completed;
};

// ---------------------------------------------------------------------------
// FIUPatternResult -- output from monthly Financial Intelligence Unit
// pattern analysis. Feeds evidence tokens to LE NPCs when suspicion
// exceeds fiu_token_threshold.
// ---------------------------------------------------------------------------
struct FIUPatternResult {
    uint32_t target_actor_id;
    float suspicion_score;
    LaunderingMethod inferred_method;
};

}  // namespace econlife
