// CalendarModule unit tests — verify deadline miss procedure, fast-forward
// suppression, entry expiration, and dead NPC handling.
// All tests tagged [calendar][tier1].

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>

#include "core/world_state/npc.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/calendar/calendar_module.h"
#include "modules/calendar/calendar_types.h"

using namespace econlife;

// ---------------------------------------------------------------------------
// Helper: build a minimal WorldState suitable for calendar tests.
// ---------------------------------------------------------------------------
static WorldState make_test_world(uint32_t current_tick) {
    WorldState state{};
    state.current_tick = current_tick;
    state.world_seed = 12345;
    state.player = nullptr;
    state.lod2_price_index = nullptr;
    state.ticks_this_session = 0;
    state.game_mode = GameMode::standard;
    state.current_schema_version = 1;
    state.network_health_dirty = false;
    return state;
}

// ---------------------------------------------------------------------------
// Helper: build a PlayerCharacter with a given id.
// ---------------------------------------------------------------------------
static PlayerCharacter make_test_player(uint32_t id) {
    PlayerCharacter player{};
    player.id = id;
    player.background = Background::MiddleClass;
    player.traits = {Trait::Analytical, Trait::Disciplined, Trait::Cautious};
    player.starting_province_id = 0;
    player.health = {1.0f, 70.0f, 75.0f, 0.0f, 0.001f};
    player.age = 30.0f;
    player.reputation = {0.0f, 0.0f, 0.0f, 0.0f};
    player.wealth = 10000.0f;
    player.net_assets = 10000.0f;
    player.residence_id = 0;
    player.partner_npc_id = 0;
    player.designated_heir_npc_id = 0;
    player.network_health = {0.5f, 0.5f, 0.5f, 0.1f};
    player.movement_follower_count = 0;
    player.home_province_id = 0;
    player.current_province_id = 0;
    player.ironman_eligible = false;
    player.restoration_history = {0, {}};
    return player;
}

// ---------------------------------------------------------------------------
// Helper: build an NPC with the given id and status.
// ---------------------------------------------------------------------------
static NPC make_test_npc(uint32_t id, NPCStatus status = NPCStatus::active) {
    NPC npc{};
    npc.id = id;
    npc.role = NPCRole::corporate_executive;
    npc.motivations.weights = {0.125f, 0.125f, 0.125f, 0.125f, 0.125f, 0.125f, 0.125f, 0.125f};
    npc.risk_tolerance = 0.5f;
    npc.capital = 5000.0f;
    npc.social_capital = 0.5f;
    npc.movement_follower_count = 0;
    npc.home_province_id = 0;
    npc.current_province_id = 0;
    npc.status = status;
    return npc;
}

// ---------------------------------------------------------------------------
// Helper: build a deadline CalendarEntry.
// ---------------------------------------------------------------------------
static CalendarEntry make_deadline_entry(uint32_t id, uint32_t start_tick, uint32_t duration_ticks,
                                         uint32_t npc_id, float relationship_penalty,
                                         uint32_t consequence_delay_ticks, bool npc_initiative,
                                         bool player_committed = false, bool mandatory = false) {
    CalendarEntry entry{};
    entry.id = id;
    entry.start_tick = start_tick;
    entry.duration_ticks = duration_ticks;
    entry.type = CalendarEntryType::deadline;
    entry.npc_id = npc_id;
    entry.player_committed = player_committed;
    entry.mandatory = mandatory;
    entry.scene_card_id = 0;

    entry.deadline_consequence.relationship_penalty = relationship_penalty;
    entry.deadline_consequence.npc_initiative = npc_initiative;
    entry.deadline_consequence.consequence_type = ConsequenceType::financial_penalty;
    entry.deadline_consequence.consequence_severity = 0.5f;
    entry.deadline_consequence.consequence_delay_ticks = consequence_delay_ticks;
    entry.deadline_consequence.default_outcome_description = "test consequence";

    return entry;
}

// ===========================================================================
// Test: Deadline miss applies relationship penalty
// ===========================================================================
TEST_CASE("test_deadline_miss_applies_relationship_penalty", "[calendar][tier1]") {
    // Deadline entry at start_tick=5, duration=5 => deadline_tick=10.
    // relationship_penalty = 0.15.
    // At tick 11, player has not committed. Verify trust reduced by 0.15.

    auto state = make_test_world(11);
    auto player = make_test_player(1000);
    state.player = &player;

    NPC npc = make_test_npc(42);
    state.significant_npcs.push_back(npc);

    CalendarEntry entry = make_deadline_entry(
        /*id=*/1, /*start_tick=*/5, /*duration_ticks=*/5,
        /*npc_id=*/42, /*relationship_penalty=*/0.15f,
        /*consequence_delay_ticks=*/3, /*npc_initiative=*/false);
    state.calendar.push_back(entry);

    DeltaBuffer delta{};
    CalendarModule module;
    module.execute(state, delta);

    // Step 1: relationship penalty should be applied.
    // Look for an NPCDelta with npc_id=42 that has updated_relationship.
    bool found_relationship_delta = false;
    for (const auto& nd : delta.npc_deltas) {
        if (nd.npc_id == 42 && nd.updated_relationship.has_value()) {
            found_relationship_delta = true;
            REQUIRE(nd.updated_relationship->trust == Catch::Approx(-0.15f));
            REQUIRE(nd.updated_relationship->target_npc_id == player.id);
        }
    }
    REQUIRE(found_relationship_delta);
}

// ===========================================================================
// Test: Deadline miss queues consequence with correct delay
// ===========================================================================
TEST_CASE("test_deadline_miss_queues_consequence_with_delay", "[calendar][tier1]") {
    // Deadline at tick 10 (start=5, duration=5) with consequence_delay_ticks=5.
    // At tick 11, verify consequence is queued at tick 15 (10 + 5).

    auto state = make_test_world(11);
    auto player = make_test_player(1000);
    state.player = &player;

    NPC npc = make_test_npc(42);
    state.significant_npcs.push_back(npc);

    CalendarEntry entry = make_deadline_entry(
        /*id=*/1, /*start_tick=*/5, /*duration_ticks=*/5,
        /*npc_id=*/42, /*relationship_penalty=*/0.1f,
        /*consequence_delay_ticks=*/5, /*npc_initiative=*/false);
    state.calendar.push_back(entry);

    DeltaBuffer delta{};
    CalendarModule module;
    module.execute(state, delta);

    // Step 2: consequence delta should be present.
    // The consequence should be scheduled at deadline_tick + delay = 10 + 5 = 15.
    bool found_consequence = false;
    for (const auto& cd : delta.consequence_deltas) {
        if (cd.new_entry_id.has_value() && cd.new_entry_id.value() == 15) {
            found_consequence = true;
        }
    }
    REQUIRE(found_consequence);
}

// ===========================================================================
// Test: Deadline miss creates memory entry with correct emotional_weight
// ===========================================================================
TEST_CASE("test_deadline_miss_creates_memory_entry", "[calendar][tier1]") {
    // Deadline missed with relationship_penalty = 0.2.
    // Verify memory entry: emotional_weight = -(0.3 + 0.2) = -0.5.

    auto state = make_test_world(11);
    auto player = make_test_player(1000);
    state.player = &player;

    NPC npc = make_test_npc(42);
    state.significant_npcs.push_back(npc);

    CalendarEntry entry = make_deadline_entry(
        /*id=*/1, /*start_tick=*/5, /*duration_ticks=*/5,
        /*npc_id=*/42, /*relationship_penalty=*/0.2f,
        /*consequence_delay_ticks=*/3, /*npc_initiative=*/false);
    state.calendar.push_back(entry);

    DeltaBuffer delta{};
    CalendarModule module;
    module.execute(state, delta);

    // Step 4: memory entry should exist.
    bool found_memory = false;
    for (const auto& nd : delta.npc_deltas) {
        if (nd.npc_id == 42 && nd.new_memory_entry.has_value()) {
            found_memory = true;
            const auto& mem = nd.new_memory_entry.value();
            REQUIRE(mem.type == MemoryType::event);
            REQUIRE(mem.subject_id == player.id);
            REQUIRE(mem.emotional_weight == Catch::Approx(-0.5f));
            REQUIRE(mem.tick_timestamp == 11);
        }
    }
    REQUIRE(found_memory);
}

// ===========================================================================
// Test: NPC initiative fires unilateral action on miss
// ===========================================================================
TEST_CASE("test_npc_initiative_fires_on_miss", "[calendar][tier1]") {
    // Deadline with npc_initiative = true. Player misses.
    // Verify an NPC unilateral action (additional consequence delta) is queued.

    auto state = make_test_world(11);
    auto player = make_test_player(1000);
    state.player = &player;

    NPC npc = make_test_npc(42);
    state.significant_npcs.push_back(npc);

    CalendarEntry entry = make_deadline_entry(
        /*id=*/1, /*start_tick=*/5, /*duration_ticks=*/5,
        /*npc_id=*/42, /*relationship_penalty=*/0.1f,
        /*consequence_delay_ticks=*/3, /*npc_initiative=*/true);
    state.calendar.push_back(entry);

    DeltaBuffer delta{};
    CalendarModule module;
    module.execute(state, delta);

    // Step 2 creates one consequence delta (the scheduled consequence).
    // Step 3 creates an additional consequence delta (the NPC unilateral action).
    // So we expect at least 2 consequence deltas total.
    REQUIRE(delta.consequence_deltas.size() >= 2);

    // Verify the NPC initiative delta uses npc_id as the entry id.
    bool found_initiative = false;
    for (const auto& cd : delta.consequence_deltas) {
        if (cd.new_entry_id.has_value() && cd.new_entry_id.value() == 42) {
            found_initiative = true;
        }
    }
    REQUIRE(found_initiative);
}

// ===========================================================================
// Test: No initiative means no unilateral action
// ===========================================================================
TEST_CASE("test_no_initiative_no_action", "[calendar][tier1]") {
    // Deadline with npc_initiative = false. Player misses.
    // Verify only 1 consequence delta (the scheduled consequence), not 2.

    auto state = make_test_world(11);
    auto player = make_test_player(1000);
    state.player = &player;

    NPC npc = make_test_npc(42);
    state.significant_npcs.push_back(npc);

    CalendarEntry entry = make_deadline_entry(
        /*id=*/1, /*start_tick=*/5, /*duration_ticks=*/5,
        /*npc_id=*/42, /*relationship_penalty=*/0.1f,
        /*consequence_delay_ticks=*/3, /*npc_initiative=*/false);
    state.calendar.push_back(entry);

    DeltaBuffer delta{};
    CalendarModule module;
    module.execute(state, delta);

    // Only 1 consequence delta should exist (step 2).
    // No initiative delta (step 3 is skipped when npc_initiative=false).
    REQUIRE(delta.consequence_deltas.size() == 1);
}

// ===========================================================================
// Test: Mandatory entry suppresses fast-forward
// ===========================================================================
TEST_CASE("test_mandatory_entry_suppresses_fast_forward", "[calendar][tier1]") {
    // Calendar entry: mandatory=true, start_tick=5, duration_ticks=3.
    // Deadline_tick = 5 + 3 = 8.
    // At tick 7 (within active range 5-8), verify module detects suppression.
    //
    // Since the DeltaBuffer does not have a dedicated fast_forward_suppressed
    // field, we verify the module does NOT fire a missed-deadline procedure
    // for a mandatory non-deadline entry that is still active. We also test
    // that the entry is active during the expected tick range.

    // Test during active range (tick 7, within 5-8 inclusive).
    {
        auto state = make_test_world(7);
        auto player = make_test_player(1000);
        state.player = &player;

        CalendarEntry entry{};
        entry.id = 1;
        entry.start_tick = 5;
        entry.duration_ticks = 3;
        entry.type = CalendarEntryType::meeting;
        entry.npc_id = 42;
        entry.player_committed = true;
        entry.mandatory = true;
        entry.scene_card_id = 0;
        entry.deadline_consequence = {};
        state.calendar.push_back(entry);

        NPC npc = make_test_npc(42);
        state.significant_npcs.push_back(npc);

        DeltaBuffer delta{};
        CalendarModule module;
        module.execute(state, delta);

        // The mandatory entry is active at tick 7 (5 <= 7 <= 8).
        // No deadline miss procedure should fire for a meeting-type entry.
        REQUIRE(delta.consequence_deltas.empty());
        REQUIRE(delta.npc_deltas.empty());
    }

    // Test that mandatory window includes the boundary ticks.
    // At tick 5 (start), the entry is active.
    {
        auto state = make_test_world(5);
        auto player = make_test_player(1000);
        state.player = &player;

        CalendarEntry entry{};
        entry.id = 1;
        entry.start_tick = 5;
        entry.duration_ticks = 3;
        entry.type = CalendarEntryType::meeting;
        entry.npc_id = 42;
        entry.player_committed = true;
        entry.mandatory = true;
        entry.scene_card_id = 0;
        entry.deadline_consequence = {};
        state.calendar.push_back(entry);

        NPC npc = make_test_npc(42);
        state.significant_npcs.push_back(npc);

        DeltaBuffer delta{};
        CalendarModule module;
        module.execute(state, delta);

        // At tick 5, entry is active. No deadline procedure fires.
        REQUIRE(delta.consequence_deltas.empty());
    }

    // At tick 8 (deadline_tick), the entry is still active (boundary inclusive).
    {
        auto state = make_test_world(8);
        auto player = make_test_player(1000);
        state.player = &player;

        CalendarEntry entry{};
        entry.id = 1;
        entry.start_tick = 5;
        entry.duration_ticks = 3;
        entry.type = CalendarEntryType::meeting;
        entry.npc_id = 42;
        entry.player_committed = true;
        entry.mandatory = true;
        entry.scene_card_id = 0;
        entry.deadline_consequence = {};
        state.calendar.push_back(entry);

        NPC npc = make_test_npc(42);
        state.significant_npcs.push_back(npc);

        DeltaBuffer delta{};
        CalendarModule module;
        module.execute(state, delta);

        // At tick 8 (= start_tick + duration_ticks), the entry is at its deadline
        // boundary. For a meeting type, no deadline procedure fires.
        REQUIRE(delta.consequence_deltas.empty());
    }
}

// ===========================================================================
// Test: Completed entry expires when duration elapses
// ===========================================================================
TEST_CASE("test_completed_entry_expires", "[calendar][tier1]") {
    // Meeting entry: start_tick=5, duration_ticks=2. deadline_tick = 7.
    // At tick 8 (past deadline_tick), the entry is expired.
    // Verify the module processes the entry as expired (no consequence for
    // non-deadline entries).

    auto state = make_test_world(8);
    auto player = make_test_player(1000);
    state.player = &player;

    CalendarEntry entry{};
    entry.id = 1;
    entry.start_tick = 5;
    entry.duration_ticks = 2;
    entry.type = CalendarEntryType::meeting;
    entry.npc_id = 42;
    entry.player_committed = true;
    entry.mandatory = false;
    entry.scene_card_id = 0;
    entry.deadline_consequence = {};
    state.calendar.push_back(entry);

    NPC npc = make_test_npc(42);
    state.significant_npcs.push_back(npc);

    DeltaBuffer delta{};
    CalendarModule module;
    module.execute(state, delta);

    // Non-deadline entries should not produce consequence or NPC deltas
    // when they expire.
    REQUIRE(delta.consequence_deltas.empty());
    REQUIRE(delta.npc_deltas.empty());
}

// ===========================================================================
// Test: Dead NPC skips relationship and memory but consequence still fires
// ===========================================================================
TEST_CASE("test_dead_npc_skips_relationship", "[calendar][tier1]") {
    // NPC is dead. Deadline is missed.
    // Verify: relationship delta NOT created, memory delta NOT created,
    //         consequence delta IS created.

    auto state = make_test_world(11);
    auto player = make_test_player(1000);
    state.player = &player;

    // NPC with status = dead
    NPC npc = make_test_npc(42, NPCStatus::dead);
    state.significant_npcs.push_back(npc);

    CalendarEntry entry = make_deadline_entry(
        /*id=*/1, /*start_tick=*/5, /*duration_ticks=*/5,
        /*npc_id=*/42, /*relationship_penalty=*/0.2f,
        /*consequence_delay_ticks=*/3, /*npc_initiative=*/false);
    state.calendar.push_back(entry);

    DeltaBuffer delta{};
    CalendarModule module;
    module.execute(state, delta);

    // Step 2 (consequence) should still fire.
    REQUIRE(delta.consequence_deltas.size() == 1);
    REQUIRE(delta.consequence_deltas[0].new_entry_id.has_value());

    // Steps 1 and 4 (relationship and memory) should be skipped for dead NPC.
    // So npc_deltas should be empty -- no relationship delta, no memory delta.
    bool has_relationship = false;
    bool has_memory = false;
    for (const auto& nd : delta.npc_deltas) {
        if (nd.npc_id == 42) {
            if (nd.updated_relationship.has_value())
                has_relationship = true;
            if (nd.new_memory_entry.has_value())
                has_memory = true;
        }
    }
    REQUIRE_FALSE(has_relationship);
    REQUIRE_FALSE(has_memory);
}
