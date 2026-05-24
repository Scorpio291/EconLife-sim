#pragma once

// Player action types — defines all V1 player actions as a tagged variant.
//
// External code (UI, CLI) enqueues PlayerAction structs via
// enqueue_player_action(). The player_actions module drains the queue
// each tick, validates actions against current WorldState, and translates
// them into DeltaBuffer writes and DeferredWorkQueue items.
//
// Determinism: each action carries a sequence_number assigned at enqueue
// time. Actions within a tick are processed in sequence_number order.

#include <cstdint>
#include <string>
#include <variant>

#include "core/world_state/delta_buffer.h"        // PaymentMethod
#include "modules/calendar/calendar_types.h"      // CalendarEntryType
#include "modules/economy/economy_types.h"        // BusinessSector
#include "modules/real_estate/real_estate_types.h"  // PropertyType

namespace econlife {

// ---------------------------------------------------------------------------
// Action payload structs — one per V1 action kind
// ---------------------------------------------------------------------------

// Select a choice on a pending scene card.
struct SceneCardChoiceAction {
    uint32_t scene_card_id;
    uint32_t choice_id;  // maps to PlayerChoice.id on the card
};

// Accept or decline a calendar entry.
struct CalendarCommitAction {
    uint32_t calendar_entry_id;
    bool accept;  // true = commit, false = decline
};

// Schedule a new calendar engagement.
struct CalendarScheduleAction {
    CalendarEntryType type;
    uint32_t npc_id;  // 0 for non-meeting entries
    uint32_t desired_start_tick;
    uint32_t duration_ticks;
};

// Initiate travel to another province.
struct TravelAction {
    uint32_t destination_province_id;
};

// Start a new business in the player's current province.
struct StartBusinessAction {
    BusinessSector sector;
    uint32_t province_id;  // must match player.current_province_id
};

// Change production settings on a player-owned business.
struct SetProductionAction {
    uint32_t business_id;
    uint32_t recipe_id;
    float target_output_rate;  // 0.0-1.0 capacity utilization
};

// Delegate business management to an NPC.
struct DelegateAction {
    uint32_t business_id;
    uint32_t manager_npc_id;
};

// Commercialize a researched technology.
struct CommercializeTechAction {
    uint32_t business_id;
    std::string node_key;
};

// Request a meeting with an NPC (generates a calendar entry).
struct InitiateContactAction {
    uint32_t target_npc_id;
};

// List an owned PropertyListing for sale at a specified asking price.
// Validates ownership in real_estate's drain step. Player must own the
// property and asking_price must be > 0.
struct ListPropertyForSaleAction {
    uint32_t property_id;
    float asking_price;
};

// Remove an owned PropertyListing from the market (sets listed_for_sale
// = false). Player must own the property.
struct UnlistPropertyAction {
    uint32_t property_id;
};

// Make an offer on a listed property. Phase 2 wires the close-delay
// lifecycle; Phase 3 wires below-asking negotiation; Phase 4 wires
// mortgage financing. Cash + Phase 4 mortgage use the same action; the
// payment_method field selects the payment path. Default
// payment_method=cash + down_payment_fraction=1.0 preserves Phase 1-3
// caller semantics. For mortgage: down_payment_fraction = 0.0
// (full-principal loan); for mixed: down_payment_fraction in (0, 1).
struct MakePropertyOfferAction {
    uint32_t property_id;
    float offer_price;
    PaymentMethod payment_method = PaymentMethod::cash;
    float down_payment_fraction = 1.0f;
};

// Cancel the currently-active PendingTransaction on a property. The
// player must be either the buyer or the seller. Aborts the deal
// before its close_tick; no transfer occurs. Phase 2.
struct CancelPendingTransactionAction {
    uint32_t property_id;
};

// Place a bid on an open ActiveAuction. Phase 6. Bid must exceed the
// auction's current_high_bid and the bidder must have enough cash to
// cover the bid amount (re-validated at settle time). Multiple bids
// per auction per tick from the same bidder are allowed (price
// escalation).
struct PlaceAuctionBidAction {
    uint32_t auction_id;
    float bid_amount;
};

// Apply to the local government to re-zone an owned property to a new
// permitted-use class. Phase 7. The player must own the property.
// Approval is decided by a deterministic government roll (minor vs
// major change + province corruption). On approval the property's
// zoned_use changes; on denial nothing happens (the player may
// re-apply later).
struct RequestZoningChangeAction {
    uint32_t property_id;
    PropertyType desired_use;
};

// ---------------------------------------------------------------------------
// PlayerActionType enum — mirrors variant index for type dispatch
// ---------------------------------------------------------------------------

enum class PlayerActionType : uint8_t {
    scene_card_choice = 0,
    calendar_commit = 1,
    calendar_schedule = 2,
    travel = 3,
    start_business = 4,
    set_production = 5,
    delegate = 6,
    commercialize_tech = 7,
    initiate_contact = 8,
    list_property_for_sale = 9,
    unlist_property = 10,
    make_property_offer = 11,
    cancel_pending_transaction = 12,
    place_auction_bid = 13,
    request_zoning_change = 14,
};

// ---------------------------------------------------------------------------
// PlayerActionPayload — variant of all action structs
// ---------------------------------------------------------------------------

using PlayerActionPayload =
    std::variant<SceneCardChoiceAction, CalendarCommitAction, CalendarScheduleAction, TravelAction,
                 StartBusinessAction, SetProductionAction, DelegateAction, CommercializeTechAction,
                 InitiateContactAction, ListPropertyForSaleAction, UnlistPropertyAction,
                 MakePropertyOfferAction, CancelPendingTransactionAction, PlaceAuctionBidAction,
                 RequestZoningChangeAction>;

// ---------------------------------------------------------------------------
// PlayerAction — one queued player action
// ---------------------------------------------------------------------------

struct PlayerAction {
    PlayerActionType type;
    PlayerActionPayload payload;
    uint32_t submitted_tick;   // tick the action was enqueued at
    uint32_t sequence_number;  // monotonic counter; deterministic ordering within a tick
};

}  // namespace econlife
