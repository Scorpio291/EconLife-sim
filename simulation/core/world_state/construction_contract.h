#pragma once

// Construction contracts (Phase 11) — the player (or an NPC) hires a
// construction-sector firm to build a Facility on a developed parcel.
// Construction is a traded service: contractors bid, the client awards a
// bid, money is escrowed, and on completion the Facility is delivered
// and the contractor's business is paid. A player who owns the awarded
// contractor effectively builds at internal cost (the payment routes
// back into a business they own).
//
// Lifecycle: bidding → awarded → in_progress → completed (or cancelled).
// Bids are collected when the contract opens. The client awards one bid;
// the contract then runs until expected_completion_tick.

#include <cstdint>
#include <string>
#include <vector>

namespace econlife {

enum class ContractStage : uint8_t {
    bidding = 0,      // open for bids; awaiting an award
    awarded = 1,      // a bid was accepted; escrow taken
    in_progress = 2,  // building; awaiting completion tick
    completed = 3,    // facility delivered, contractor paid
    cancelled = 4,    // aborted (no valid bids / award expired / client gone)
};

struct ConstructionBid {
    uint32_t contractor_business_id;
    float bid_amount;
    uint32_t completion_ticks;  // how long this contractor needs to build
};

struct ConstructionContract {
    uint32_t id;
    uint32_t client_id;             // player_id (or NPC) commissioning the build
    uint32_t property_id;           // parcel to build on
    std::string facility_type_key;  // what to build
    std::string recipe_id;          // initial recipe the facility runs
    std::vector<ConstructionBid> bids;
    uint32_t awarded_bid_index;  // index into bids once awarded; else 0
    ContractStage stage;
    uint32_t bidding_deadline_tick;     // award must happen by this tick or it cancels
    uint32_t expected_completion_tick;  // set on award
};

}  // namespace econlife
