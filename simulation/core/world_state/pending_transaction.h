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

#include "delta_buffer.h"  // PaymentMethod

namespace econlife {

enum class PendingTxStage : uint8_t {
    pending = 0,    // accepted, awaiting close_tick
    settled = 1,    // close_tick reached, ownership transferred
    cancelled = 2,  // player cancelled before close (buyer or seller)
    expired = 3,    // close_tick reached but buyer can no longer afford
};

struct PendingTransaction {
    uint32_t id;
    uint32_t property_id;
    uint32_t buyer_id;
    uint32_t seller_id;  // resolved at create time from property.owner_id
    float offer_price;
    uint32_t offered_tick;
    uint32_t close_tick;
    PendingTxStage stage;

    // Phase 4 — payment fields. Cash uses payment_method=cash and
    // down_payment_fraction=1.0 (entire price paid at close).
    // Mortgage uses payment_method=mortgage and down_payment_fraction=0
    // (full price financed; LoanRecord created at close). Mixed uses
    // payment_method=mixed and down_payment_fraction in (0, 1).
    PaymentMethod payment_method = PaymentMethod::cash;
    float down_payment_fraction = 1.0f;
    float interest_rate = 0.0f;        // captured at offer time so close uses approved rate
    uint32_t loan_maturity_ticks = 0;  // 0 = N/A for cash; non-zero for mortgage/mixed
};

// Phase 10 — in-flight business acquisition. Structurally parallel to
// PendingTransaction but settles ownership of an NPCBusiness (and all
// its facilities, which follow via the unchanged Facility.business_id
// link) rather than a PropertyListing. Created when an NPC owner accepts
// the player's acquisition offer; settles after a due-diligence delay.
// Reuses PendingTxStage and PaymentMethod. Persisted (schema v13+).
struct PendingBusinessAcquisition {
    uint32_t id;
    uint32_t business_id;
    uint32_t buyer_id;
    uint32_t seller_id;  // current owner (0 = independent business)
    float price;         // computed acquisition price (revenue × months × multiple)
    uint32_t offered_tick;
    uint32_t close_tick;
    PendingTxStage stage;
    PaymentMethod payment_method = PaymentMethod::cash;
    float down_payment_fraction = 1.0f;
    float interest_rate = 0.0f;
    uint32_t loan_maturity_ticks = 0;
};

}  // namespace econlife
