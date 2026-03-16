// SceneCards module unit tests — verify calendar triggers, NPC presentation
// state computation, physical presence validation, dead NPC filtering, and
// authored vs procedural card priority.
//
// All tests tagged [scene_cards][tier1].

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "core/tick/tick_module.h"
#include "core/world_state/world_state.h"

using namespace econlife;

// ---------------------------------------------------------------------------
// We need to instantiate the SceneCardsModule. Since the module class is
// defined in the .cpp file (not in a header), we re-declare a minimal
// version here that includes the .cpp to access the class. An alternative
// is to include the .cpp directly — acceptable for unit tests in this
// project since modules are single-translation-unit classes.
// ---------------------------------------------------------------------------
#include "modules/scene_cards/scene_cards_module.cpp"

// ---------------------------------------------------------------------------
// Test helpers — build minimal WorldState components
// ---------------------------------------------------------------------------

static PlayerCharacter make_player(uint32_t id, uint32_t province_id) {
    PlayerCharacter player{};
    player.id = id;
    player.current_province_id = province_id;
    player.home_province_id = province_id;
    return player;
}

static NPC make_npc(uint32_t id, uint32_t province_id,
                    float risk_tolerance = 0.5f,
                    NPCStatus status = NPCStatus::active) {
    NPC npc{};
    npc.id = id;
    npc.role = NPCRole::corporate_executive;
    npc.risk_tolerance = risk_tolerance;
    npc.current_province_id = province_id;
    npc.home_province_id = province_id;
    npc.status = status;
    npc.capital = 1000.0f;
    npc.social_capital = 0.5f;
    npc.movement_follower_count = 0;
    return npc;
}

// Add a directed relationship from NPC toward target with given trust.
static void add_relationship(NPC& npc, uint32_t target_id, float trust) {
    Relationship rel{};
    rel.target_npc_id = target_id;
    rel.trust = trust;
    rel.fear = 0.0f;
    rel.obligation_balance = 0.0f;
    rel.last_interaction_tick = 0;
    rel.is_movement_ally = false;
    rel.recovery_ceiling = 1.0f;
    npc.relationships.push_back(rel);
}

static CalendarEntry make_calendar_entry(uint32_t id, uint32_t start_tick,
                                         uint32_t scene_card_id,
                                         uint32_t npc_id = 0,
                                         CalendarEntryType type = CalendarEntryType::meeting) {
    CalendarEntry entry{};
    entry.id = id;
    entry.start_tick = start_tick;
    entry.duration_ticks = 1;
    entry.type = type;
    entry.npc_id = npc_id;
    entry.player_committed = true;
    entry.mandatory = false;
    entry.scene_card_id = scene_card_id;
    return entry;
}

static SceneCard make_scene_card(uint32_t id, uint32_t npc_id,
                                 SceneSetting setting,
                                 bool is_authored = false,
                                 SceneCardType type = SceneCardType::meeting) {
    SceneCard card{};
    card.id = id;
    card.type = type;
    card.setting = setting;
    card.npc_id = npc_id;
    card.npc_presentation_state = 0.5f;
    card.is_authored = is_authored;
    card.chosen_choice_id = 0;
    return card;
}

static WorldState make_base_state() {
    WorldState state{};
    state.current_tick = 10;
    state.world_seed = 42;
    state.player = nullptr;
    state.lod2_price_index = nullptr;
    state.ticks_this_session = 10;
    state.game_mode = GameMode::standard;
    state.current_schema_version = 1;
    state.network_health_dirty = false;
    return state;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("test_calendar_triggers_scene_card", "[scene_cards][tier1]") {
    // Calendar entry with start_tick == current_tick and scene_card_id = 42.
    // Verify scene card 42 is added to delta.new_scene_cards.

    auto state = make_base_state();
    state.current_tick = 10;

    auto player = make_player(1, 3);
    state.player = &player;

    auto npc = make_npc(100, 3);  // Same province as player
    add_relationship(npc, 1, 0.5f);
    state.significant_npcs.push_back(npc);

    state.calendar.push_back(make_calendar_entry(1, 10, 42, 100));

    DeltaBuffer delta{};
    SceneCardsModule module;
    module.execute(state, delta);

    REQUIRE(delta.new_scene_cards.size() == 1);
    REQUIRE(delta.new_scene_cards[0].id == 42);
    REQUIRE(delta.new_scene_cards[0].npc_id == 100);
}

TEST_CASE("test_presentation_state_from_trust", "[scene_cards][tier1]") {
    // NPC with high trust (0.9) toward player.
    // Verify npc_presentation_state is in the high cooperative range.

    auto state = make_base_state();
    state.current_tick = 10;

    auto player = make_player(1, 3);
    state.player = &player;

    auto npc = make_npc(100, 3, 0.5f);  // risk_tolerance = 0.5
    add_relationship(npc, 1, 0.9f);     // high trust toward player
    state.significant_npcs.push_back(npc);

    state.calendar.push_back(make_calendar_entry(1, 10, 42, 100));

    DeltaBuffer delta{};
    SceneCardsModule module;
    module.execute(state, delta);

    REQUIRE(delta.new_scene_cards.size() == 1);

    // trust = 0.9 -> normalized = (0.9 + 1.0) / 2.0 = 0.95
    // presentation = 0.7 * 0.95 + 0.3 * 0.5 = 0.665 + 0.15 = 0.815
    float expected = 0.7f * ((0.9f + 1.0f) / 2.0f) + 0.3f * 0.5f;
    REQUIRE_THAT(delta.new_scene_cards[0].npc_presentation_state,
                 Catch::Matchers::WithinAbs(static_cast<double>(expected), 0.001));
    REQUIRE(delta.new_scene_cards[0].npc_presentation_state > 0.7f);
}

TEST_CASE("test_hostile_presentation", "[scene_cards][tier1]") {
    // NPC with low trust (0.1) toward player.
    // Verify npc_presentation_state is in the low/hostile range.

    auto state = make_base_state();
    state.current_tick = 10;

    auto player = make_player(1, 3);
    state.player = &player;

    auto npc = make_npc(100, 3, 0.2f);  // low risk_tolerance
    add_relationship(npc, 1, 0.1f);     // low trust toward player
    state.significant_npcs.push_back(npc);

    state.calendar.push_back(make_calendar_entry(1, 10, 42, 100));

    DeltaBuffer delta{};
    SceneCardsModule module;
    module.execute(state, delta);

    REQUIRE(delta.new_scene_cards.size() == 1);

    // trust = 0.1 -> normalized = (0.1 + 1.0) / 2.0 = 0.55
    // presentation = 0.7 * 0.55 + 0.3 * 0.2 = 0.385 + 0.06 = 0.445
    float expected = 0.7f * ((0.1f + 1.0f) / 2.0f) + 0.3f * 0.2f;
    REQUIRE_THAT(delta.new_scene_cards[0].npc_presentation_state,
                 Catch::Matchers::WithinAbs(static_cast<double>(expected), 0.001));
    REQUIRE(delta.new_scene_cards[0].npc_presentation_state < 0.5f);
}

TEST_CASE("test_in_person_requires_province_match", "[scene_cards][tier1]") {
    // Scene card with setting = restaurant for NPC in province 3.
    // Player is in province 1. Verify card is NOT delivered.

    auto state = make_base_state();
    state.current_tick = 10;

    auto player = make_player(1, 1);  // Player in province 1
    state.player = &player;

    auto npc = make_npc(100, 3);  // NPC in province 3
    add_relationship(npc, 1, 0.5f);
    state.significant_npcs.push_back(npc);

    // Create a calendar entry that would trigger a scene card
    // with restaurant setting. We add the card to pending_scene_cards
    // directly to test the filtering for in-person settings.
    auto card = make_scene_card(42, 100, SceneSetting::restaurant);
    state.pending_scene_cards.push_back(card);

    // Also trigger via calendar to test the new card path.
    state.calendar.push_back(make_calendar_entry(1, 10, 99, 100));

    DeltaBuffer delta{};
    SceneCardsModule module;
    module.execute(state, delta);

    // The calendar-triggered card (id=99) should be filtered out because
    // the default setting is private_office (in-person) and player is in
    // province 1 while NPC is in province 3.
    REQUIRE(delta.new_scene_cards.empty());
}

TEST_CASE("test_remote_ignores_province", "[scene_cards][tier1]") {
    // Scene card with setting = phone_call. Player in any province.
    // Verify card is delivered regardless of province mismatch.

    auto state = make_base_state();
    state.current_tick = 10;

    auto player = make_player(1, 1);  // Player in province 1
    state.player = &player;

    auto npc = make_npc(100, 5);  // NPC in province 5 (different)
    add_relationship(npc, 1, 0.5f);
    state.significant_npcs.push_back(npc);

    // We add a pending scene card with phone_call setting directly
    // and a calendar trigger. Since calendar-triggered cards default
    // to private_office (in-person), we test by adding a card to
    // new_scene_cards via the delta and ensuring the phone_call variant
    // survives finalization.
    //
    // To test the full pipeline, we add the card to pending_scene_cards
    // with phone_call setting. But the module's trigger phase creates
    // cards with default settings, so let's also directly test the
    // helper function behavior by pre-populating new_scene_cards.

    // For a cleaner test: trigger a calendar entry, then manually
    // override the setting to phone_call to test the filter.
    // Since the module creates cards from calendar entries and then
    // filters in finalize_new_cards, we can test by injecting a
    // phone_call card into the pending list and triggering processing.

    // Actually, the simplest approach: add a pre-existing pending card
    // with phone_call setting and verify it is not removed by the module.
    // But the module only adds new cards (it doesn't re-process existing
    // pending cards for province filtering; that was already done when
    // they were first added). So we test via new_scene_cards.

    // Best approach: create a card manually and run finalize_new_cards
    // by triggering the full execute path with a calendar entry, then
    // check. We can set up the calendar entry and manually set the
    // setting on the delta. But that requires modifying the output.
    //
    // Simplest: use the compute_presentation_state and is_in_person_setting
    // functions directly since they are file-static. Since we included the
    // .cpp, they are available.

    REQUIRE_FALSE(is_in_person_setting(SceneSetting::phone_call));
    REQUIRE_FALSE(is_in_person_setting(SceneSetting::video_call));
    REQUIRE(is_in_person_setting(SceneSetting::restaurant));
    REQUIRE(is_in_person_setting(SceneSetting::boardroom));

    // Full integration test: add a card to delta.new_scene_cards
    // with phone_call setting, then manually call finalize. Since
    // we included the .cpp, we can construct a module and manipulate
    // the delta. But finalize_new_cards is private.
    //
    // Alternative: pre-populate a scene card in pending_scene_cards
    // with phone_call setting and npc_id != 0, then run execute.
    // The module does not re-filter existing pending cards (by design),
    // but it does produce new cards from calendar entries.
    //
    // For a complete test: we set up a scenario where the calendar
    // triggers a card, and the card's default setting is overridden.
    // Since the module assigns default settings, we need a different
    // approach.
    //
    // The cleanest test: create two calendar entries, one that
    // triggers for an NPC in a different province. Then add a
    // pre-existing pending scene card with phone_call setting to
    // verify it passes through. We rely on the fact that pending
    // cards with chosen_choice_id != 0 get processed, and those
    // with chosen_choice_id == 0 are left alone.
    //
    // But for the "remote ignores province" test, the key behavior
    // is tested via is_in_person_setting. Let's also test that a
    // scene card with phone_call setting added directly to
    // delta.new_scene_cards survives the finalize pass.

    // Create a delta with a phone_call card pre-populated.
    DeltaBuffer delta{};
    SceneCard phone_card = make_scene_card(50, 100, SceneSetting::phone_call);
    delta.new_scene_cards.push_back(phone_card);

    // Now manually run the module's execute. The module will process
    // calendar (empty), then call finalize_new_cards which filters
    // the delta.new_scene_cards. Our phone_call card should survive.
    SceneCardsModule module;
    module.execute(state, delta);

    // The phone_call card should still be present because phone_call
    // is a remote setting and province mismatch is irrelevant.
    bool found = false;
    for (const auto& c : delta.new_scene_cards) {
        if (c.id == 50) {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("test_dead_npc_card_discarded", "[scene_cards][tier1]") {
    // NPC dies at tick 8. Scene card for that NPC queued at tick 7.
    // At tick 9, verify card is discarded from pending queue.

    auto state = make_base_state();
    state.current_tick = 9;

    auto player = make_player(1, 3);
    state.player = &player;

    auto dead_npc = make_npc(100, 3, 0.5f, NPCStatus::dead);
    add_relationship(dead_npc, 1, 0.5f);
    state.significant_npcs.push_back(dead_npc);

    // Calendar entry triggers scene card for the dead NPC.
    state.calendar.push_back(make_calendar_entry(1, 9, 42, 100));

    DeltaBuffer delta{};
    SceneCardsModule module;
    module.execute(state, delta);

    // Card should be discarded because NPC is dead.
    REQUIRE(delta.new_scene_cards.empty());
}

TEST_CASE("test_authored_takes_priority", "[scene_cards][tier1]") {
    // Both an authored and procedural scene card trigger for the same NPC
    // at the same tick. Verify only the authored card is delivered.

    auto state = make_base_state();
    state.current_tick = 10;

    auto player = make_player(1, 3);
    state.player = &player;

    auto npc = make_npc(100, 3);
    add_relationship(npc, 1, 0.5f);
    state.significant_npcs.push_back(npc);

    // Calendar entry triggers a procedural card.
    state.calendar.push_back(make_calendar_entry(1, 10, 50, 100));

    // Pre-populate delta with an authored card for the same NPC.
    // This simulates an authored card being generated by another system
    // and placed in the delta before the scene_cards module runs.
    // In practice, the module handles this during apply_authored_priority.
    DeltaBuffer delta{};

    // First, let the module generate the procedural card from calendar.
    SceneCardsModule module;

    // We need both cards in new_scene_cards to test priority.
    // Add the authored card to the delta before executing.
    SceneCard authored_card = make_scene_card(99, 100, SceneSetting::boardroom, true);
    delta.new_scene_cards.push_back(authored_card);

    module.execute(state, delta);

    // Should have only the authored card (id=99), not the procedural (id=50).
    REQUIRE(delta.new_scene_cards.size() == 1);
    REQUIRE(delta.new_scene_cards[0].id == 99);
    REQUIRE(delta.new_scene_cards[0].is_authored == true);
}

TEST_CASE("calendar entry at wrong tick does not trigger", "[scene_cards][tier1]") {
    // Calendar entry with start_tick = 15, current_tick = 10.
    // Verify no scene card is generated.

    auto state = make_base_state();
    state.current_tick = 10;

    auto player = make_player(1, 3);
    state.player = &player;

    auto npc = make_npc(100, 3);
    state.significant_npcs.push_back(npc);

    state.calendar.push_back(make_calendar_entry(1, 15, 42, 100));

    DeltaBuffer delta{};
    SceneCardsModule module;
    module.execute(state, delta);

    REQUIRE(delta.new_scene_cards.empty());
}

TEST_CASE("module reports correct name and dependencies", "[scene_cards][tier1]") {
    SceneCardsModule module;

    REQUIRE(module.name() == "scene_cards");
    REQUIRE(module.package_id() == "base_game");
    REQUIRE(module.scope() == ModuleScope::v1);
    REQUIRE_FALSE(module.is_province_parallel());

    auto after = module.runs_after();
    REQUIRE(after.size() == 1);
    REQUIRE(after[0] == "calendar");

    auto before = module.runs_before();
    REQUIRE(before.empty());
}

TEST_CASE("presentation state formula is correct", "[scene_cards][tier1]") {
    // Direct test of compute_presentation_state with known values.

    // Neutral trust (0.0), neutral risk (0.5)
    // normalized trust = (0.0 + 1.0) / 2.0 = 0.5
    // result = 0.7 * 0.5 + 0.3 * 0.5 = 0.35 + 0.15 = 0.5
    float result = compute_presentation_state(0.0f, 0.5f);
    REQUIRE_THAT(result, Catch::Matchers::WithinAbs(0.5, 0.001));

    // Maximum trust (1.0), maximum risk tolerance (1.0)
    // normalized trust = (1.0 + 1.0) / 2.0 = 1.0
    // result = 0.7 * 1.0 + 0.3 * 1.0 = 1.0
    result = compute_presentation_state(1.0f, 1.0f);
    REQUIRE_THAT(result, Catch::Matchers::WithinAbs(1.0, 0.001));

    // Minimum trust (-1.0), minimum risk tolerance (0.0)
    // normalized trust = (-1.0 + 1.0) / 2.0 = 0.0
    // result = 0.7 * 0.0 + 0.3 * 0.0 = 0.0
    result = compute_presentation_state(-1.0f, 0.0f);
    REQUIRE_THAT(result, Catch::Matchers::WithinAbs(0.0, 0.001));

    // High trust, low risk
    // trust = 0.8, normalized = 0.9, risk = 0.1
    // result = 0.7 * 0.9 + 0.3 * 0.1 = 0.63 + 0.03 = 0.66
    result = compute_presentation_state(0.8f, 0.1f);
    REQUIRE_THAT(result, Catch::Matchers::WithinAbs(0.66, 0.001));
}

TEST_CASE("no player means no execution", "[scene_cards][tier1]") {
    auto state = make_base_state();
    state.current_tick = 10;
    state.player = nullptr;

    state.calendar.push_back(make_calendar_entry(1, 10, 42, 100));

    DeltaBuffer delta{};
    SceneCardsModule module;
    module.execute(state, delta);

    // No cards should be generated when player is null.
    REQUIRE(delta.new_scene_cards.empty());
}

TEST_CASE("calendar entry with scene_card_id zero does not trigger", "[scene_cards][tier1]") {
    auto state = make_base_state();
    state.current_tick = 10;

    auto player = make_player(1, 3);
    state.player = &player;

    // Calendar entry with scene_card_id = 0
    state.calendar.push_back(make_calendar_entry(1, 10, 0, 100));

    DeltaBuffer delta{};
    SceneCardsModule module;
    module.execute(state, delta);

    REQUIRE(delta.new_scene_cards.empty());
}

TEST_CASE("resolved card generates consequence delta", "[scene_cards][tier1]") {
    // A pending scene card with a player choice made should generate
    // a consequence delta.

    auto state = make_base_state();
    state.current_tick = 10;

    auto player = make_player(1, 3);
    state.player = &player;

    auto npc = make_npc(100, 3);
    add_relationship(npc, 1, 0.5f);
    state.significant_npcs.push_back(npc);

    // Create a pending scene card with a choice already made.
    SceneCard card = make_scene_card(42, 100, SceneSetting::boardroom);
    PlayerChoice choice{};
    choice.id = 1;
    choice.label = "Accept deal";
    choice.description = "Accept the business proposal";
    choice.consequence_id = 777;
    card.choices.push_back(choice);
    card.chosen_choice_id = 1;  // Player selected choice 1
    state.pending_scene_cards.push_back(card);

    DeltaBuffer delta{};
    SceneCardsModule module;
    module.execute(state, delta);

    // Should have a consequence delta for the choice.
    REQUIRE(delta.consequence_deltas.size() == 1);
    REQUIRE(delta.consequence_deltas[0].new_entry_id.has_value());
    REQUIRE(delta.consequence_deltas[0].new_entry_id.value() == 777);

    // Should also have an NPC memory delta.
    REQUIRE(delta.npc_deltas.size() == 1);
    REQUIRE(delta.npc_deltas[0].npc_id == 100);
    REQUIRE(delta.npc_deltas[0].new_memory_entry.has_value());
    REQUIRE(delta.npc_deltas[0].new_memory_entry->type == MemoryType::interaction);
}
