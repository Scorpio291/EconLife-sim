#pragma once

// npc_spending module types.
// Module-specific types for the npc_spending module (Tier 6).
// Core shared types are in their respective core headers.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "modules/economy/economy_types.h"  // BuyerType

namespace econlife {

// ---------------------------------------------------------------------------
// GoodsData -- per-good metadata loaded from goods data file at startup.
//
// Keyed by good_id. Provides the demand model parameters used by the
// consumer demand formula:
//   demand_contribution(npc, g) = base_consumer_demand_units
//                                * income_factor * price_factor * quality_factor
// ---------------------------------------------------------------------------
struct GoodsData {
    uint32_t good_id;
    float base_consumer_demand_units;  // default demand units per tick per NPC
    float income_elasticity;           // exponent on income_factor power function
    float price_elasticity;            // exponent on price_factor (sign: negative = normal good)
    float base_price;                  // reference base price for price_factor denominator
};

// ---------------------------------------------------------------------------
// DemandConfig -- tuning constants for the demand model.
//
// Loaded from simulation_config.json -> demand section. Defaults here
// match the canonical INTERFACE.md specification.
// ---------------------------------------------------------------------------
struct DemandConfig {
    float reference_income = 1000.0f;  // daily reference income for income_factor normalization
    float max_income_factor = 5.0f;    // ceiling clamp on income_factor
    float min_price_factor = 0.05f;    // floor clamp on price_factor
};

// ---------------------------------------------------------------------------
// NPCBuyerProfile -- per-NPC buyer type assignment.
//
// NPC struct does not carry BuyerType; the npc_spending module manages
// the mapping internally (similar to BankingModule owning LoanRecords).
// Updated quarterly (every 90 ticks).
// ---------------------------------------------------------------------------
struct NPCBuyerProfile {
    uint32_t npc_id;
    BuyerType buyer_type;
};

}  // namespace econlife
