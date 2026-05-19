#pragma once

// Pending transaction state for the real-estate market.
//
// Phase 2 of the player asset acquisition design: replaces Phase 1's
// instant settlement with a multi-tick lifecycle. Buy requests (from
// player_actions or from real_estate's NPC opportunistic-buy scan)
// produce a PendingTransaction in stage = pending. Each tick,
// real_estate's global post-pass checks for entries whose
// close_tick <= current_tick and settles them (transferring ownership,
// moving cash, emitting evidence). If the buyer's funds are no longer
// sufficient at close, the transaction transitions to expired and no
// transfer occurs. Players may CancelPendingTransactionAction the
// transaction (as buyer or seller) before close to abort.
//
// Settled, cancelled, and expired transactions are pruned at the end
// of the same tick they reach a terminal state.

#include <cstdint>

namespace econlife {

enum class PendingTxStage : uint8_t {
    pending = 0,     // accepted, awaiting close_tick
    settled = 1,     // close_tick reached, ownership transferred
    cancelled = 2,   // player cancelled before close (buyer or seller)
    expired = 3,     // close_tick reached but buyer can no longer afford
};

struct PendingTransaction {
    uint32_t id;
    uint32_t property_id;
    uint32_t buyer_id;
    uint32_t seller_id;     // resolved at create time from property.owner_id
    float offer_price;
    uint32_t offered_tick;
    uint32_t close_tick;
    PendingTxStage stage;
};

}  // namespace econlife
