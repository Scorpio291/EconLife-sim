// Unit tests for the player_actions module and PlayerActionQueue.
//
// Tests cover: scene card choices, calendar commits, travel initiation,
// travel arrival, business creation, action ordering, invalid action
// rejection, and queue clearing.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/tick/drain_deferred_work.h"
#include "core/world_state/apply_deltas.h"
#include "core/world_state/player_action_queue.h"
#include "core/world_state/world_state.h"
#include "modules/player_actions/player_actions_module.h"
#include "tests/test_world_factory.h"

using namespace econlife;
using namespace econlife::test;
using Catch::Matchers::WithinAbs;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static WorldState make_minimal_world() {
    WorldState world{};
    world.current_tick = 0;
    world.world_seed = 42;
    world.game_mode = GameMode::standard;
    world.current_schema_version = 1;
    world.network_health_dirty = false;

    // Two provinces
    world.provinces.push_back(create_test_province(0, 0, 0));
    world.provinces.push_back(create_test_province(1, 0, 0));

    // Player in province 0
    auto player = create_test_player(0);
    world.player = std::make_unique<PlayerCharacter>(std::move(player));

    // One NPC in province 0
    world.significant_npcs.push_back(create_test_npc(100, NPCRole::corporate_executive, 0));

    return world;
}

// ---------------------------------------------------------------------------
// Scene card choice tests
// ---------------------------------------------------------------------------

TEST_CASE("Scene card choice sets chosen_choice_id", "[player_actions][unit]") {
    auto world = make_minimal_world();

    // Create a pending scene card with two choices.
    SceneCard card{};
    card.id = 10;
    card.type = SceneCardType::meeting;
    card.setting = SceneSetting::private_office;
    card.npc_id = 100;
    card.npc_presentation_state = 0.5f;
    card.chosen_choice_id = 0;
    card.choices.push_back(PlayerChoice{1, "Accept", "Accept the deal", 0});
    card.choices.push_back(PlayerChoice{2, "Decline", "Walk away", 0});
    world.pending_scene_cards.push_back(card);

    // Enqueue a choice action.
    enqueue_player_action(world, PlayerActionType::scene_card_choice,
                          SceneCardChoiceAction{10, 2});

    // Run the module.
    PlayerActionsModule module;
    DeltaBuffer delta;
    module.execute(world, delta);

    // Apply deltas.
    apply_deltas(world, delta);

    REQUIRE(world.pending_scene_cards.size() == 1);
    REQUIRE(world.pending_scene_cards[0].chosen_choice_id == 2);
}

TEST_CASE("Invalid scene card id is silently dropped", "[player_actions][unit]") {
    auto world = make_minimal_world();

    // No pending scene cards — action references non-existent card.
    enqueue_player_action(world, PlayerActionType::scene_card_choice,
                          SceneCardChoiceAction{999, 1});

    PlayerActionsModule module;
    DeltaBuffer delta;
    module.execute(world, delta);

    // Should produce no deltas and not crash.
    REQUIRE(delta.scene_card_choice_deltas.empty());
}

TEST_CASE("Invalid choice id on valid card is rejected", "[player_actions][unit]") {
    auto world = make_minimal_world();

    SceneCard card{};
    card.id = 10;
    card.type = SceneCardType::meeting;
    card.setting = SceneSetting::private_office;
    card.npc_id = 100;
    card.chosen_choice_id = 0;
    card.choices.push_back(PlayerChoice{1, "Only option", "desc", 0});
    world.pending_scene_cards.push_back(card);

    // Choice id 99 doesn't exist on this card.
    enqueue_player_action(world, PlayerActionType::scene_card_choice,
                          SceneCardChoiceAction{10, 99});

    PlayerActionsModule module;
    DeltaBuffer delta;
    module.execute(world, delta);

    REQUIRE(delta.scene_card_choice_deltas.empty());
}

// ---------------------------------------------------------------------------
// Calendar commit tests
// ---------------------------------------------------------------------------

TEST_CASE("Calendar commit sets player_committed", "[player_actions][unit]") {
    auto world = make_minimal_world();

    CalendarEntry entry{};
    entry.id = 20;
    entry.start_tick = 5;
    entry.duration_ticks = 3;
    entry.type = CalendarEntryType::meeting;
    entry.npc_id = 100;
    entry.player_committed = false;
    entry.mandatory = false;
    entry.scene_card_id = 0;
    world.calendar.push_back(entry);

    enqueue_player_action(world, PlayerActionType::calendar_commit,
                          CalendarCommitAction{20, true});

    PlayerActionsModule module;
    DeltaBuffer delta;
    module.execute(world, delta);
    apply_deltas(world, delta);

    REQUIRE(world.calendar.size() == 1);
    REQUIRE(world.calendar[0].player_committed == true);
}

TEST_CASE("Calendar commit for expired entry is rejected", "[player_actions][unit]") {
    auto world = make_minimal_world();
    world.current_tick = 100;

    CalendarEntry entry{};
    entry.id = 20;
    entry.start_tick = 5;
    entry.duration_ticks = 3;  // expired at tick 8
    entry.type = CalendarEntryType::meeting;
    entry.npc_id = 100;
    entry.player_committed = false;
    world.calendar.push_back(entry);

    enqueue_player_action(world, PlayerActionType::calendar_commit,
                          CalendarCommitAction{20, true});

    PlayerActionsModule module;
    DeltaBuffer delta;
    module.execute(world, delta);

    REQUIRE(delta.calendar_commit_deltas.empty());
}

// ---------------------------------------------------------------------------
// Travel tests
// ---------------------------------------------------------------------------

TEST_CASE("Travel action sets in_transit and schedules arrival", "[player_actions][unit]") {
    auto world = make_minimal_world();

    enqueue_player_action(world, PlayerActionType::travel,
                          TravelAction{1});  // travel to province 1

    PlayerActionsModule module;
    DeltaBuffer delta;
    module.execute(world, delta);

    // Player should be set to in_transit.
    REQUIRE(delta.player_delta.new_travel_status.has_value());
    REQUIRE(*delta.player_delta.new_travel_status == NPCTravelStatus::in_transit);

    // DeferredWorkQueue should have a player_travel_arrival item.
    REQUIRE(!world.deferred_work_queue.empty());
    auto item = world.deferred_work_queue.top();
    REQUIRE(item.type == WorkType::player_travel_arrival);
    REQUIRE(item.due_tick == world.current_tick + 3);  // 3-tick travel time
}

TEST_CASE("Travel to current province is rejected", "[player_actions][unit]") {
    auto world = make_minimal_world();
    // Player starts in province 0.

    enqueue_player_action(world, PlayerActionType::travel,
                          TravelAction{0});  // same province

    PlayerActionsModule module;
    DeltaBuffer delta;
    module.execute(world, delta);

    REQUIRE_FALSE(delta.player_delta.new_travel_status.has_value());
    REQUIRE(world.deferred_work_queue.empty());
}

TEST_CASE("Travel while in_transit is rejected", "[player_actions][unit]") {
    auto world = make_minimal_world();
    world.player->travel_status = NPCTravelStatus::in_transit;

    enqueue_player_action(world, PlayerActionType::travel,
                          TravelAction{1});

    PlayerActionsModule module;
    DeltaBuffer delta;
    module.execute(world, delta);

    REQUIRE_FALSE(delta.player_delta.new_travel_status.has_value());
}

TEST_CASE("Travel to nonexistent province is rejected", "[player_actions][unit]") {
    auto world = make_minimal_world();

    enqueue_player_action(world, PlayerActionType::travel,
                          TravelAction{999});  // doesn't exist

    PlayerActionsModule module;
    DeltaBuffer delta;
    module.execute(world, delta);

    REQUIRE_FALSE(delta.player_delta.new_travel_status.has_value());
}

// ---------------------------------------------------------------------------
// Player travel arrival (DeferredWorkQueue integration)
// ---------------------------------------------------------------------------

TEST_CASE("Player travel arrival updates province_id", "[player_actions][unit]") {
    auto world = make_minimal_world();
    world.player->travel_status = NPCTravelStatus::in_transit;

    // Manually push a travel arrival item.
    world.deferred_work_queue.push({world.current_tick, WorkType::player_travel_arrival,
                                    world.player->id,
                                    PlayerTravelPayload{1}});

    // Drain deferred work (simulates what tick orchestrator does).
    DeltaBuffer delta;
    DrainConfig cfg;
    drain_deferred_work(world, delta, cfg);
    apply_deltas(world, delta);

    REQUIRE(world.player->current_province_id == 1);
    REQUIRE(world.player->travel_status == NPCTravelStatus::resident);
}

// ---------------------------------------------------------------------------
// Start business tests
// ---------------------------------------------------------------------------

TEST_CASE("Start business creates new business and deducts wealth", "[player_actions][unit]") {
    auto world = make_minimal_world();

    size_t biz_count_before = world.npc_businesses.size();

    enqueue_player_action(world, PlayerActionType::start_business,
                          StartBusinessAction{BusinessSector::retail, 0});

    PlayerActionsModule module;
    DeltaBuffer delta;
    module.execute(world, delta);
    apply_deltas(world, delta);

    REQUIRE(world.npc_businesses.size() == biz_count_before + 1);

    const auto& new_biz = world.npc_businesses.back();
    REQUIRE(new_biz.sector == BusinessSector::retail);
    REQUIRE(new_biz.owner_id == world.player->id);
    REQUIRE(new_biz.province_id == 0);

    // Wealth deducted.
    REQUIRE_THAT(world.player->wealth, WithinAbs(50000.0f - 10000.0f, 0.01f));
}

TEST_CASE("Start business in wrong province is rejected", "[player_actions][unit]") {
    auto world = make_minimal_world();

    enqueue_player_action(world, PlayerActionType::start_business,
                          StartBusinessAction{BusinessSector::retail, 1});  // player is in province 0

    PlayerActionsModule module;
    DeltaBuffer delta;
    module.execute(world, delta);

    REQUIRE(delta.new_businesses.empty());
}

TEST_CASE("Start business with insufficient wealth is rejected", "[player_actions][unit]") {
    auto world = make_minimal_world();
    world.player->wealth = 5000.0f;  // less than 10,000 minimum

    enqueue_player_action(world, PlayerActionType::start_business,
                          StartBusinessAction{BusinessSector::retail, 0});

    PlayerActionsModule module;
    DeltaBuffer delta;
    module.execute(world, delta);

    REQUIRE(delta.new_businesses.empty());
}

// ---------------------------------------------------------------------------
// Action ordering and queue management
// ---------------------------------------------------------------------------

TEST_CASE("Actions are processed in sequence_number order", "[player_actions][unit]") {
    auto world = make_minimal_world();

    // Create two scene cards.
    SceneCard card1{};
    card1.id = 10;
    card1.type = SceneCardType::meeting;
    card1.setting = SceneSetting::private_office;
    card1.npc_id = 100;
    card1.chosen_choice_id = 0;
    card1.choices.push_back(PlayerChoice{1, "A", "desc", 0});
    world.pending_scene_cards.push_back(card1);

    SceneCard card2{};
    card2.id = 11;
    card2.type = SceneCardType::meeting;
    card2.setting = SceneSetting::private_office;
    card2.npc_id = 100;
    card2.chosen_choice_id = 0;
    card2.choices.push_back(PlayerChoice{5, "B", "desc", 0});
    world.pending_scene_cards.push_back(card2);

    // Enqueue actions — sequence numbers assigned in order.
    uint32_t seq1 = enqueue_player_action(world, PlayerActionType::scene_card_choice,
                                           SceneCardChoiceAction{10, 1});
    uint32_t seq2 = enqueue_player_action(world, PlayerActionType::scene_card_choice,
                                           SceneCardChoiceAction{11, 5});
    REQUIRE(seq1 < seq2);

    PlayerActionsModule module;
    DeltaBuffer delta;
    module.execute(world, delta);
    apply_deltas(world, delta);

    REQUIRE(world.pending_scene_cards[0].chosen_choice_id == 1);
    REQUIRE(world.pending_scene_cards[1].chosen_choice_id == 5);
}

TEST_CASE("Queue is cleared after processing", "[player_actions][unit]") {
    auto world = make_minimal_world();

    enqueue_player_action(world, PlayerActionType::travel,
                          TravelAction{1});

    REQUIRE(has_pending_player_actions(world));

    PlayerActionsModule module;
    DeltaBuffer delta;
    module.execute(world, delta);

    REQUIRE_FALSE(has_pending_player_actions(world));
}

TEST_CASE("Empty queue produces no effects", "[player_actions][unit]") {
    auto world = make_minimal_world();

    PlayerActionsModule module;
    DeltaBuffer delta;
    module.execute(world, delta);

    REQUIRE(delta.scene_card_choice_deltas.empty());
    REQUIRE(delta.calendar_commit_deltas.empty());
    REQUIRE(delta.new_calendar_entries.empty());
    REQUIRE(delta.new_businesses.empty());
    REQUIRE_FALSE(delta.player_delta.new_travel_status.has_value());
}

// ---------------------------------------------------------------------------
// Initiate contact test
// ---------------------------------------------------------------------------

TEST_CASE("Initiate contact creates calendar entry", "[player_actions][unit]") {
    auto world = make_minimal_world();

    enqueue_player_action(world, PlayerActionType::initiate_contact,
                          InitiateContactAction{100});

    PlayerActionsModule module;
    DeltaBuffer delta;
    module.execute(world, delta);

    REQUIRE(delta.new_calendar_entries.size() == 1);
    REQUIRE(delta.new_calendar_entries[0].npc_id == 100);
    REQUIRE(delta.new_calendar_entries[0].type == CalendarEntryType::meeting);
    REQUIRE(delta.new_calendar_entries[0].player_committed == true);
}

TEST_CASE("Initiate contact with dead NPC is rejected", "[player_actions][unit]") {
    auto world = make_minimal_world();
    world.significant_npcs[0].status = NPCStatus::dead;

    enqueue_player_action(world, PlayerActionType::initiate_contact,
                          InitiateContactAction{100});

    PlayerActionsModule module;
    DeltaBuffer delta;
    module.execute(world, delta);

    REQUIRE(delta.new_calendar_entries.empty());
}
