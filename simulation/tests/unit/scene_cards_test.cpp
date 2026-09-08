// SceneCards module unit tests — verify calendar triggers, NPC presentation
// state computation, physical presence validation, dead NPC filtering, and
// authored vs procedural card priority.
//
// All tests tagged [scene_cards][tier1].

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstdint>
#include <filesystem>
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

static NPC make_npc(uint32_t id, uint32_t province_id, float risk_tolerance = 0.5f,
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

static CalendarEntry make_calendar_entry(uint32_t id, uint32_t start_tick, uint32_t scene_card_id,
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

static SceneCard make_scene_card(uint32_t id, uint32_t npc_id, SceneSetting setting,
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

// finalize_new_cards drops cards the player cannot answer, so a card that is
// meant to survive admission needs at least one choice. Routing tests (physical
// presence, authored priority) use this; tests that assert the empty-choices
// rule deliberately do not.
static SceneCard make_admissible_card(uint32_t id, uint32_t npc_id, SceneSetting setting,
                                      bool is_authored = false,
                                      SceneCardType type = SceneCardType::meeting) {
    SceneCard card = make_scene_card(id, npc_id, setting, is_authored, type);
    card.choices.push_back(PlayerChoice{1, "Acknowledge", "", 0});
    return card;
}

// The shipped card copy. Calendar-triggered cards take their dialogue, class
// and setting from the authored catalog, so a module built without it raises
// no calendar card at all — the same configuration a real session runs is the
// one these tests have to run.
static std::string find_scene_cards_dir() {
    namespace fs = std::filesystem;
    const char* candidates[] = {
        "packages/base_game/scene_cards",
        "../packages/base_game/scene_cards",
        "../../packages/base_game/scene_cards",
        "../../../packages/base_game/scene_cards",
        "../../../../packages/base_game/scene_cards",
        "../../../../../packages/base_game/scene_cards",
    };
    for (const auto* c : candidates) {
        if (fs::is_directory(c))
            return fs::canonical(c).string();
    }
    return "";
}

static SceneCardsConfig with_catalog(SceneCardsConfig cfg = {}) {
    cfg.card_catalog_directory = find_scene_cards_dir();
    return cfg;
}

static WorldState make_base_state() {
    WorldState state{};
    state.current_tick = 10;
    state.world_seed = 42;
    state.player.reset();
    state.lod2_price_index.reset();
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
    state.player = std::make_unique<PlayerCharacter>(player);

    auto npc = make_npc(100, 3);  // Same province as player
    add_relationship(npc, 1, 0.5f);
    state.significant_npcs.push_back(npc);

    state.calendar.push_back(make_calendar_entry(1, 10, 42, 100));

    DeltaBuffer delta{};
    SceneCardsModule module(with_catalog());
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
    state.player = std::make_unique<PlayerCharacter>(player);

    auto npc = make_npc(100, 3, 0.5f);  // risk_tolerance = 0.5
    add_relationship(npc, 1, 0.9f);     // high trust toward player
    state.significant_npcs.push_back(npc);

    state.calendar.push_back(make_calendar_entry(1, 10, 42, 100));

    DeltaBuffer delta{};
    SceneCardsModule module(with_catalog());
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
    state.player = std::make_unique<PlayerCharacter>(player);

    auto npc = make_npc(100, 3, 0.2f);  // low risk_tolerance
    add_relationship(npc, 1, 0.1f);     // low trust toward player
    state.significant_npcs.push_back(npc);

    state.calendar.push_back(make_calendar_entry(1, 10, 42, 100));

    DeltaBuffer delta{};
    SceneCardsModule module(with_catalog());
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
    state.player = std::make_unique<PlayerCharacter>(player);

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
    SceneCardsModule module(with_catalog());
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
    state.player = std::make_unique<PlayerCharacter>(player);

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
    SceneCard phone_card = make_admissible_card(50, 100, SceneSetting::phone_call);
    delta.new_scene_cards.push_back(phone_card);

    // Now manually run the module's execute. The module will process
    // calendar (empty), then call finalize_new_cards which filters
    // the delta.new_scene_cards. Our phone_call card should survive.
    SceneCardsModule module(with_catalog());
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
    state.player = std::make_unique<PlayerCharacter>(player);

    auto dead_npc = make_npc(100, 3, 0.5f, NPCStatus::dead);
    add_relationship(dead_npc, 1, 0.5f);
    state.significant_npcs.push_back(dead_npc);

    // Calendar entry triggers scene card for the dead NPC.
    state.calendar.push_back(make_calendar_entry(1, 9, 42, 100));

    DeltaBuffer delta{};
    SceneCardsModule module(with_catalog());
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
    state.player = std::make_unique<PlayerCharacter>(player);

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
    SceneCardsModule module(with_catalog());

    // We need both cards in new_scene_cards to test priority.
    // Add the authored card to the delta before executing.
    SceneCard authored_card = make_admissible_card(99, 100, SceneSetting::boardroom, true);
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
    state.player = std::make_unique<PlayerCharacter>(player);

    auto npc = make_npc(100, 3);
    state.significant_npcs.push_back(npc);

    state.calendar.push_back(make_calendar_entry(1, 15, 42, 100));

    DeltaBuffer delta{};
    SceneCardsModule module(with_catalog());
    module.execute(state, delta);

    REQUIRE(delta.new_scene_cards.empty());
}

TEST_CASE("module reports correct name and dependencies", "[scene_cards][tier1]") {
    SceneCardsModule module(with_catalog());

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
    state.player.reset();

    state.calendar.push_back(make_calendar_entry(1, 10, 42, 100));

    DeltaBuffer delta{};
    SceneCardsModule module(with_catalog());
    module.execute(state, delta);

    // No cards should be generated when player is null.
    REQUIRE(delta.new_scene_cards.empty());
}

TEST_CASE("calendar entry with scene_card_id zero does not trigger", "[scene_cards][tier1]") {
    auto state = make_base_state();
    state.current_tick = 10;

    auto player = make_player(1, 3);
    state.player = std::make_unique<PlayerCharacter>(player);

    // Calendar entry with scene_card_id = 0
    state.calendar.push_back(make_calendar_entry(1, 10, 0, 100));

    DeltaBuffer delta{};
    SceneCardsModule module(with_catalog());
    module.execute(state, delta);

    REQUIRE(delta.new_scene_cards.empty());
}

TEST_CASE("resolved card generates consequence delta", "[scene_cards][tier1]") {
    // A pending scene card with a player choice made should generate
    // a consequence delta.

    auto state = make_base_state();
    state.current_tick = 10;

    auto player = make_player(1, 3);
    state.player = std::make_unique<PlayerCharacter>(player);

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
    SceneCardsModule module(with_catalog());
    module.execute(state, delta);

    // Should have a consequence delta for the choice.
    REQUIRE(delta.consequence_deltas.size() == 1);
    REQUIRE(delta.consequence_deltas[0].new_consequence.has_value());
    REQUIRE(delta.consequence_deltas[0].new_consequence->id == 777);

    // Should also have an NPC memory delta.
    REQUIRE(delta.npc_deltas.size() == 1);
    REQUIRE(delta.npc_deltas[0].npc_id == 100);
    REQUIRE(delta.npc_deltas[0].new_memory_entry.has_value());
    REQUIRE(delta.npc_deltas[0].new_memory_entry->type == MemoryType::interaction);
}

// ---------------------------------------------------------------------------
// Queue lifecycle — the guarantee that pending_scene_cards is bounded.
//
// Before this pass the queue was append-only at every layer: nothing ever
// removed a card, resolved or not, so a year of play accumulated cards the
// player could neither read nor clear. These tests pin each of the four
// retirement rules and the generation-time invariant behind them.
// ---------------------------------------------------------------------------

static SceneCard make_answerable_card(uint32_t id, uint32_t npc_id, CardClass klass,
                                      SceneSetting setting = SceneSetting::phone_call) {
    SceneCard card = make_scene_card(id, npc_id, setting, false, SceneCardType::call);
    card.card_class = klass;
    card.choices.push_back(PlayerChoice{1, "Engage", "", 0});
    card.choices.push_back(PlayerChoice{2, "Decline", "", 0});
    card.default_choice_id = 2;
    return card;
}

TEST_CASE("resolved card is retired the tick after it was resolved", "[scene_cards][lifecycle]") {
    WorldState state = make_base_state();
    state.player = std::make_unique<PlayerCharacter>(make_player(1, 0));
    state.significant_npcs.push_back(make_npc(100, 0));

    SceneCard card = make_answerable_card(7, 100, CardClass::ambient);
    card.chosen_choice_id = 1;
    card.resolved_tick = state.current_tick;  // resolved THIS tick
    state.pending_scene_cards.push_back(card);

    // Same tick as resolution: still visible, so consumers that run later in
    // the tick (real_estate negotiations, etc.) can still read the choice.
    DeltaBuffer delta{};
    SceneCardsModule module(with_catalog());
    module.execute(state, delta);
    REQUIRE(delta.retired_scene_card_ids.empty());

    // Next tick: retired.
    state.current_tick += 1;
    DeltaBuffer delta2{};
    module.execute(state, delta2);
    REQUIRE(delta2.retired_scene_card_ids.size() == 1);
    REQUIRE(delta2.retired_scene_card_ids[0] == 7);
}

TEST_CASE("card whose NPC died is discarded with no consequence", "[scene_cards][lifecycle]") {
    WorldState state = make_base_state();
    state.player = std::make_unique<PlayerCharacter>(make_player(1, 0));
    state.significant_npcs.push_back(make_npc(100, 0, 0.5f, NPCStatus::dead));

    state.pending_scene_cards.push_back(make_answerable_card(9, 100, CardClass::mandatory));

    DeltaBuffer delta{};
    SceneCardsModule module(with_catalog());
    module.execute(state, delta);

    REQUIRE(delta.retired_scene_card_ids.size() == 1);
    REQUIRE(delta.retired_scene_card_ids[0] == 9);
    REQUIRE(delta.consequence_deltas.empty());
}

TEST_CASE("expired timed-optional card fires its default outcome", "[scene_cards][lifecycle]") {
    WorldState state = make_base_state();
    state.player = std::make_unique<PlayerCharacter>(make_player(1, 0));
    state.significant_npcs.push_back(make_npc(100, 0));

    SceneCard card = make_answerable_card(11, 100, CardClass::timed_optional);
    card.expires_tick = state.current_tick - 1;  // already past
    state.pending_scene_cards.push_back(card);

    DeltaBuffer delta{};
    SceneCardsModule module(with_catalog());
    module.execute(state, delta);

    // The default fires as a real choice rather than the card vanishing:
    // dismissal is a decision with a result (Rulebook §3).
    REQUIRE(delta.scene_card_choice_deltas.size() == 1);
    REQUIRE(delta.scene_card_choice_deltas[0].scene_card_id == 11);
    REQUIRE(delta.scene_card_choice_deltas[0].chosen_choice_id == 2);
    REQUIRE(delta.retired_scene_card_ids.empty());  // retires next tick via the resolved path
}

TEST_CASE("expired timed-optional card with no default outcome is retired",
          "[scene_cards][lifecycle]") {
    WorldState state = make_base_state();
    state.player = std::make_unique<PlayerCharacter>(make_player(1, 0));
    state.significant_npcs.push_back(make_npc(100, 0));

    SceneCard card = make_answerable_card(12, 100, CardClass::timed_optional);
    card.default_choice_id = 0;  // nothing authored to fire
    card.expires_tick = state.current_tick - 1;
    state.pending_scene_cards.push_back(card);

    DeltaBuffer delta{};
    SceneCardsModule module(with_catalog());
    module.execute(state, delta);

    REQUIRE(delta.retired_scene_card_ids.size() == 1);
    REQUIRE(delta.retired_scene_card_ids[0] == 12);
}

TEST_CASE("mandatory card never expires and is never auto-retired", "[scene_cards][lifecycle]") {
    WorldState state = make_base_state();
    state.player = std::make_unique<PlayerCharacter>(make_player(1, 0));
    state.significant_npcs.push_back(make_npc(100, 0));

    SceneCard card = make_answerable_card(13, 100, CardClass::mandatory);
    card.expires_tick = state.current_tick - 100;  // even long past
    state.pending_scene_cards.push_back(card);

    DeltaBuffer delta{};
    SceneCardsModule module(with_catalog());
    module.execute(state, delta);

    REQUIRE(delta.retired_scene_card_ids.empty());
    REQUIRE(delta.scene_card_choice_deltas.empty());
}

TEST_CASE("ambient queue is held at its cap, oldest cleared first", "[scene_cards][lifecycle]") {
    WorldState state = make_base_state();
    state.player = std::make_unique<PlayerCharacter>(make_player(1, 0));
    state.significant_npcs.push_back(make_npc(100, 0));

    SceneCardsConfig cfg{};
    cfg.ambient_queue_cap = 3;
    SceneCardsModule module(with_catalog(cfg));

    // Five live ambient cards, created in ascending tick order.
    for (uint32_t i = 0; i < 5; ++i) {
        SceneCard card = make_answerable_card(100 + i, 100, CardClass::ambient);
        card.created_tick = i;  // 100 is oldest
        state.pending_scene_cards.push_back(card);
    }

    DeltaBuffer delta{};
    module.execute(state, delta);

    REQUIRE(delta.retired_scene_card_ids.size() == 2);
    REQUIRE(delta.retired_scene_card_ids[0] == 100);
    REQUIRE(delta.retired_scene_card_ids[1] == 101);
}

TEST_CASE("a card with no choices is never admitted to the queue", "[scene_cards][lifecycle]") {
    WorldState state = make_base_state();
    state.player = std::make_unique<PlayerCharacter>(make_player(1, 0));
    state.significant_npcs.push_back(make_npc(100, 0));

    DeltaBuffer delta{};
    // A card the player cannot answer would sit in the queue forever.
    delta.new_scene_cards.push_back(
        make_scene_card(0, 100, SceneSetting::phone_call, false, SceneCardType::call));

    SceneCardsModule module(with_catalog());
    module.execute(state, delta);

    REQUIRE(delta.new_scene_cards.empty());
}

TEST_CASE("timed-optional card gets an expiry; ambient and mandatory do not",
          "[scene_cards][lifecycle]") {
    WorldState state = make_base_state();
    state.player = std::make_unique<PlayerCharacter>(make_player(1, 0));
    state.significant_npcs.push_back(make_npc(100, 0));

    SceneCardsConfig cfg{};
    cfg.timed_optional_ttl_ticks = 7;
    SceneCardsModule module(with_catalog(cfg));

    DeltaBuffer delta{};
    delta.new_scene_cards.push_back(make_answerable_card(0, 100, CardClass::timed_optional));
    delta.new_scene_cards.push_back(make_answerable_card(0, 100, CardClass::ambient));
    delta.new_scene_cards.push_back(make_answerable_card(0, 100, CardClass::mandatory));

    module.execute(state, delta);

    REQUIRE(delta.new_scene_cards.size() == 3);
    REQUIRE(delta.new_scene_cards[0].expires_tick == state.current_tick + 7);
    REQUIRE(delta.new_scene_cards[1].expires_tick == 0);
    REQUIRE(delta.new_scene_cards[2].expires_tick == 0);
    for (const auto& c : delta.new_scene_cards)
        REQUIRE(c.created_tick == state.current_tick);
}

TEST_CASE("timed-optional cards past the tier cap are demoted to ambient",
          "[scene_cards][lifecycle]") {
    WorldState state = make_base_state();
    state.player = std::make_unique<PlayerCharacter>(make_player(1, 0));
    state.significant_npcs.push_back(make_npc(100, 0));

    SceneCardsConfig cfg{};
    cfg.timed_optional_queue_cap = 2;
    cfg.max_scene_cards_per_tick = 10;
    SceneCardsModule module(with_catalog(cfg));

    // One already live, so only one more slot remains in the tier.
    state.pending_scene_cards.push_back(make_answerable_card(50, 100, CardClass::timed_optional));

    DeltaBuffer delta{};
    delta.new_scene_cards.push_back(make_answerable_card(0, 100, CardClass::timed_optional));
    delta.new_scene_cards.push_back(make_answerable_card(0, 100, CardClass::timed_optional));

    module.execute(state, delta);

    REQUIRE(delta.new_scene_cards.size() == 2);
    REQUIRE(delta.new_scene_cards[0].card_class == CardClass::timed_optional);
    REQUIRE(delta.new_scene_cards[1].card_class == CardClass::ambient);
    REQUIRE(delta.new_scene_cards[1].expires_tick == 0);
}

TEST_CASE("a default outcome naming a choice the card lacks is dropped",
          "[scene_cards][lifecycle]") {
    WorldState state = make_base_state();
    state.player = std::make_unique<PlayerCharacter>(make_player(1, 0));
    state.significant_npcs.push_back(make_npc(100, 0));

    SceneCard card = make_answerable_card(0, 100, CardClass::timed_optional);
    card.default_choice_id = 99;  // no such choice
    DeltaBuffer delta{};
    delta.new_scene_cards.push_back(card);

    SceneCardsModule module(with_catalog());
    module.execute(state, delta);

    REQUIRE(delta.new_scene_cards.size() == 1);
    REQUIRE(delta.new_scene_cards[0].default_choice_id == 0);
}

// ---------------------------------------------------------------------------
// The seed channel: producers name a template, scene_cards writes the card.
// ---------------------------------------------------------------------------

TEST_CASE("a seed becomes a card with its parameters injected", "[scene_cards][catalog]") {
    WorldState state = make_base_state();
    state.player = std::make_unique<PlayerCharacter>(make_player(1, 0));

    SceneCardSeedDelta seed{};
    seed.card_key = "sale_closed";
    seed.params.emplace_back("subject", "The mill");
    state.pending_scene_card_seeds.push_back(seed);

    DeltaBuffer delta{};
    SceneCardsModule module(with_catalog());
    module.execute(state, delta);

    REQUIRE(delta.new_scene_cards.size() == 1);
    const SceneCard& card = delta.new_scene_cards[0];
    REQUIRE(card.id == 0);  // apply_deltas allocates it
    REQUIRE_FALSE(card.dialogue.empty());
    REQUIRE(card.dialogue[0].text == "The sale closed. The mill is yours.");
    REQUIRE_FALSE(card.choices.empty());

    // The queue is drained: the same seed must not raise a second card.
    REQUIRE(state.pending_scene_card_seeds.empty());
}

TEST_CASE("a seed naming a template nobody wrote raises nothing and does not wedge",
          "[scene_cards][catalog]") {
    // The catalog is an indirection, and the honest failure for a missing
    // template is a missing card — not a sentence invented at runtime, and not
    // a seed that sits in the queue retrying forever.
    WorldState state = make_base_state();
    state.player = std::make_unique<PlayerCharacter>(make_player(1, 0));

    SceneCardSeedDelta seed{};
    seed.card_key = "no_such_template_was_ever_written";
    state.pending_scene_card_seeds.push_back(seed);

    DeltaBuffer delta{};
    SceneCardsModule module(with_catalog());
    module.execute(state, delta);

    REQUIRE(delta.new_scene_cards.empty());
    REQUIRE(state.pending_scene_card_seeds.empty());
}

TEST_CASE("a calendar summons is mandatory and carries no default outcome",
          "[scene_cards][catalog]") {
    // Rulebook §2: a summons has to be engaged. Expiring into "skip" would
    // make the mandatory class decorative, and the difference between missing
    // a lunch and not answering a summons is one the world acts on.
    WorldState state = make_base_state();
    state.current_tick = 10;
    state.player = std::make_unique<PlayerCharacter>(make_player(1, 3));
    state.significant_npcs.push_back(make_npc(100, 3));

    CalendarEntry entry = make_calendar_entry(1, 10, 42, 100);
    entry.mandatory = true;
    entry.deadline_consequence.default_outcome_description = "The court proceeds without you.";
    state.calendar.push_back(entry);

    DeltaBuffer delta{};
    SceneCardsModule module(with_catalog());
    module.execute(state, delta);

    REQUIRE(delta.new_scene_cards.size() == 1);
    const SceneCard& card = delta.new_scene_cards[0];
    REQUIRE(card.id == 42);
    REQUIRE(card.card_class == CardClass::mandatory);
    REQUIRE(card.default_choice_id == 0);
    REQUIRE(card.expires_tick == 0);
    // The world already knew what happens if the player does not turn up; the
    // card now says so instead of arriving with no text at all.
    REQUIRE_FALSE(card.dialogue.empty());
    REQUIRE(card.dialogue[0].text.find("The court proceeds without you.") != std::string::npos);
}
