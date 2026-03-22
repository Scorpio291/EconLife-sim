#pragma once

// antitrust module types.
// Module-specific types for the antitrust module (Tier 7).
//
// See docs/interfaces/antitrust/INTERFACE.md for the canonical specification.

#include <cstdint>
#include <string>
#include <vector>

namespace econlife {

// ---------------------------------------------------------------------------
// AntitrustCheckResult — result of checking one actor in one market
// ---------------------------------------------------------------------------
struct AntitrustCheckResult {
    uint32_t actor_id;
    uint32_t good_id;
    uint32_t province_id;
    float actor_supply_share;    // 0.0-1.0
    bool tier1_triggered;        // >= market_share_threshold (0.40)
    bool tier2_triggered;        // >= dominant_price_mover (0.70)
};

// ---------------------------------------------------------------------------
// AntitrustProposal — auto-generated antitrust legislation
// ---------------------------------------------------------------------------
struct AntitrustProposal {
    uint32_t id;
    uint32_t province_id;
    uint32_t proposer_npc_id;   // NPC legislator who authored
    uint32_t created_tick;
    float    target_market_share_cap;  // proposed cap
};

}  // namespace econlife
