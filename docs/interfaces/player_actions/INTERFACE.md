# Module: player_actions

## Purpose
Drains the PlayerActionQueue each tick, validates player actions against current WorldState, and translates them into DeltaBuffer writes and DeferredWorkQueue items. This module bridges external input (UI IPC, CLI) with the deterministic simulation core.

## Inputs (from WorldState)
- `player_action_queue` — ordered vector of PlayerAction structs enqueued by external code between ticks
- `pending_scene_cards` — for validating SceneCardChoiceAction (card exists, choice_id valid)
- `calendar` — for validating CalendarCommitAction (entry exists, not expired)
- `player` — player state for validation (current_province_id, travel_status, wealth, relationships)
- `significant_npcs` — NPC state for validation (alive, province, trust)
- `provinces` — province existence validation for travel
- `npc_businesses` — business ownership validation
- `deferred_work_queue` — for scheduling travel arrival and tech commercialization

## Outputs (to DeltaBuffer)
- `SceneCardChoiceDelta` — sets chosen_choice_id on pending scene cards
- `CalendarCommitDelta` — sets player_committed on calendar entries
- `CalendarEntry` (via new_calendar_entries) — new engagements from CalendarScheduleAction and InitiateContactAction
- `PlayerDelta` — travel_status change (in_transit), wealth deduction for business startup
- `NewBusinessDelta` — new player-owned business from StartBusinessAction
- `BusinessDelta` — production rate changes from SetProductionAction
- `NPCDelta` — NPC memory entries from DelegateAction
- `DeferredWorkItem` (via deferred_work_queue) — player_travel_arrival, commercialize_technology

## Preconditions
- Player exists in WorldState (world.player is not null).
- Actions have been enqueued via enqueue_player_action() between ticks.
- Each action has a valid sequence_number assigned at enqueue time.

## Postconditions
- All valid actions have been translated to DeltaBuffer writes.
- All invalid actions have been silently dropped.
- The player_action_queue is empty after processing.
- Downstream modules (calendar, scene_cards) will see the effects of player input after delta application.

## Invariants
- Actions are always processed in ascending sequence_number order.
- Physical presence is enforced: TravelAction, StartBusinessAction, and in-person InitiateContactAction check player.current_province_id and player.travel_status.
- Scene card choices are final: a card with chosen_choice_id != 0 cannot be overwritten.
- Travel time is a fixed 3 ticks for V1 (domestic same-nation travel).
- Business startup requires minimum 10,000 liquid cash.
- Delegation requires NPC trust >= 0.3.
- Determinism: same actions + same seed + same tick = identical outputs.

## Failure Modes
- Action references nonexistent entity (scene card, calendar entry, NPC, province, business): silently dropped.
- Action violates physical presence constraint: silently dropped.
- Action violates state constraint (already in transit, insufficient wealth, already chosen): silently dropped.
- No actions queued: module returns immediately with empty delta.

## Performance Contract
- Sequential execution (not province-parallel).
- Target: < 1ms per tick for up to 10 queued actions.
- Validation is O(N) scans over small collections (pending_scene_cards < 10, calendar < 50).

## Dependencies
- runs_after: [] (first module in topological order)
- runs_before: ["calendar", "scene_cards"]

## V1 Action Types
| Type | Payload | Description |
|------|---------|-------------|
| scene_card_choice | SceneCardChoiceAction | Select a choice on a pending scene card |
| calendar_commit | CalendarCommitAction | Accept or decline a calendar entry |
| calendar_schedule | CalendarScheduleAction | Schedule a new engagement |
| travel | TravelAction | Initiate travel to another province |
| start_business | StartBusinessAction | Start a new business |
| set_production | SetProductionAction | Change production settings |
| delegate | DelegateAction | Delegate business management |
| commercialize_tech | CommercializeTechAction | Commercialize researched technology |
| initiate_contact | InitiateContactAction | Request meeting with an NPC |

## Test Scenarios
- `test_scene_card_choice_sets_chosen_id`: Enqueue SceneCardChoiceAction, verify chosen_choice_id is set on the card after delta application.
- `test_invalid_scene_card_id_silently_dropped`: Bad card id, verify no crash and no delta.
- `test_invalid_choice_id_rejected`: Valid card but invalid choice_id, verify no delta.
- `test_calendar_commit_sets_committed`: Enqueue CalendarCommitAction, verify player_committed is set.
- `test_calendar_commit_expired_entry_rejected`: Entry has expired, verify no delta.
- `test_travel_sets_in_transit_and_schedules_arrival`: Travel action sets travel_status and pushes DeferredWorkItem.
- `test_travel_same_province_rejected`: Travel to current province, verify no effect.
- `test_travel_while_in_transit_rejected`: Player already in_transit, verify rejection.
- `test_travel_nonexistent_province_rejected`: Invalid province_id, verify rejection.
- `test_player_travel_arrival_updates_province`: DeferredWorkQueue arrival item sets current_province_id.
- `test_start_business_creates_and_deducts`: Business created with correct fields, wealth deducted.
- `test_start_business_wrong_province_rejected`: Province mismatch, verify rejection.
- `test_start_business_insufficient_wealth_rejected`: Below minimum capital, verify rejection.
- `test_actions_processed_in_sequence_order`: Multiple actions processed in sequence_number order.
- `test_queue_cleared_after_processing`: Queue is empty after module executes.
- `test_empty_queue_no_effects`: No actions queued, no deltas produced.
- `test_initiate_contact_creates_calendar_entry`: Creates meeting entry for the NPC.
- `test_initiate_contact_dead_npc_rejected`: Dead NPC, verify no entry created.
