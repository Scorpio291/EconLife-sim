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

#include "modules/calendar/calendar_types.h"  // CalendarEntryType
#include "modules/economy/economy_types.h"    // BusinessSector

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
};

// ---------------------------------------------------------------------------
// PlayerActionPayload — variant of all action structs
// ---------------------------------------------------------------------------

using PlayerActionPayload =
    std::variant<SceneCardChoiceAction, CalendarCommitAction, CalendarScheduleAction, TravelAction,
                 StartBusinessAction, SetProductionAction, DelegateAction, CommercializeTechAction,
                 InitiateContactAction>;

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
