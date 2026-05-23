#pragma once

// ActiveAuction state for the real-estate market (Phase 6).
//
// Auctions consign a single asset (PropertyListing today; future:
// NPCBusiness when business acquisition lands in Phase 11) for open
// public bidding over a fixed window. Bidders (player + NPCs in the
// same province) submit AuctionBids; at close_tick real_estate settles
// (highest bid above reserve wins, ownership transfers, money flows) or
// marks the auction `closed_no_reserve` (consigner keeps the asset).
//
// V1 sources of auctions:
//   * Bank-foreclosed properties (Phase 5 → Phase 6): the bank's
//     liquidation policy auto-opens an auction at market_value reserve.
//   * (Future) Tax sales (Phase 13).
//   * (Future) Player-initiated auctions on owned assets.
//
// Multiple bids per tick from the same bidder are allowed (price war
// escalation). The current_high_bidder_id / current_high_bid fields
// cache the leading bid for cheap lookup during NPC bid scans.

#include <cstdint>
#include <vector>

namespace econlife {

enum class AuctionStatus : uint8_t {
    open = 0,
    closed_sold = 1,         // close_tick reached, high bid >= reserve
    closed_no_reserve = 2,   // close_tick reached, no bid met reserve
    cancelled = 3,           // consigner cancelled (reserved for player-initiated)
};

struct AuctionBid {
    uint32_t bidder_id;
    float bid_amount;
    uint32_t placed_tick;
};

struct ActiveAuction {
    uint32_t id;
    uint32_t asset_id;             // PropertyListing.id (V1)
    uint32_t consigner_id;         // who put it up (lender_id for foreclosure;
                                   // future: player or government)
    float reserve_price;           // minimum acceptable winning bid
    uint32_t opened_tick;
    uint32_t closes_tick;
    AuctionStatus status;
    uint32_t current_high_bidder_id;  // 0 = no bids yet
    float current_high_bid;           // 0 = no bids yet
    std::vector<AuctionBid> bids;
};

}  // namespace econlife
