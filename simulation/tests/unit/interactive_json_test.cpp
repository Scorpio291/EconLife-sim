// Tests for interactive_json serialization and action parsing.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include "cli/interactive_json.h"
#include "core/world_state/player_action_queue.h"
#include "core/world_state/world_state.h"
#include "tests/test_world_factory.h"

using namespace econlife;
using namespace econlife::test;
using Catch::Matchers::WithinAbs;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static WorldState make_world() {
    WorldState world{};
    world.current_tick = 0;
    world.world_seed = 1;
    world.game_mode = GameMode::standard;
    world.current_schema_version = 1;
    world.network_health_dirty = false;
    world.provinces.push_back(create_test_province(0, 0, 0));
    world.provinces.push_back(create_test_province(1, 0, 0));
    auto player = create_test_player(0);
    world.player = std::make_unique<PlayerCharacter>(std::move(player));
    return world;
}

// ---------------------------------------------------------------------------
// tick_to_date
// ---------------------------------------------------------------------------

TEST_CASE("tick_to_date: tick 0 is 2000-01-01", "[interactive_json]") {
    REQUIRE(tick_to_date(0) == "2000-01-01");
}

TEST_CASE("tick_to_date: tick 365 is 2000-12-31", "[interactive_json]") {
    // 2000 is a leap year (366 days), so tick 365 = Dec 31 2000
    REQUIRE(tick_to_date(365) == "2000-12-31");
}

TEST_CASE("tick_to_date: tick 366 is 2001-01-01", "[interactive_json]") {
    // First day of 2001 — tick 366 skips all of leap-year 2000
    REQUIRE(tick_to_date(366) == "2001-01-01");
}

TEST_CASE("tick_to_date: tick 31 is 2000-02-01", "[interactive_json]") {
    REQUIRE(tick_to_date(31) == "2000-02-01");
}

// ---------------------------------------------------------------------------
// serialize_ui_state — structure
// ---------------------------------------------------------------------------

TEST_CASE("serialize_ui_state: top-level keys present", "[interactive_json]") {
    auto world = make_world();
    auto j = serialize_ui_state(world);
    REQUIRE(j.contains("tick"));
    REQUIRE(j.contains("date"));
    REQUIRE(j.contains("player"));
    REQUIRE(j.contains("pending_scene_cards"));
    REQUIRE(j.contains("calendar"));
    REQUIRE(j.contains("provinces"));
    REQUIRE(j.contains("businesses"));
    REQUIRE(j.contains("metrics"));
}

TEST_CASE("serialize_ui_state: tick and date match", "[interactive_json]") {
    auto world = make_world();
    world.current_tick = 10;
    auto j = serialize_ui_state(world);
    REQUIRE(j["tick"] == 10);
    REQUIRE(j["date"] == "2000-01-11");
}

TEST_CASE("serialize_ui_state: player fields correct", "[interactive_json]") {
    auto world = make_world();
    auto j = serialize_ui_state(world);
    const auto& p = j["player"];
    REQUIRE(p.contains("wealth"));
    REQUIRE(p.contains("health"));
    REQUIRE(p.contains("province_id"));
    REQUIRE(p.contains("travel_status"));
    REQUIRE(p.contains("reputation"));
    REQUIRE(p["travel_status"] == "resident");
    REQUIRE_THAT(p["wealth"].get<float>(), WithinAbs(50000.0f, 1.0f));
}

TEST_CASE("serialize_ui_state: provinces serialized", "[interactive_json]") {
    auto world = make_world();
    auto j = serialize_ui_state(world);
    REQUIRE(j["provinces"].size() == 2);
    const auto& prov = j["provinces"][0];
    REQUIRE(prov.contains("id"));
    REQUIRE(prov.contains("name"));
    REQUIRE(prov.contains("population"));
    REQUIRE(prov.contains("stability"));
    REQUIRE(prov.contains("crime"));
    REQUIRE(prov.contains("grievance"));
    REQUIRE(prov.contains("cohesion"));
}

TEST_CASE("serialize_ui_state: empty collections are arrays", "[interactive_json]") {
    auto world = make_world();
    auto j = serialize_ui_state(world);
    REQUIRE(j["pending_scene_cards"].is_array());
    REQUIRE(j["calendar"].is_array());
    REQUIRE(j["businesses"].is_array());
}

TEST_CASE("serialize_ui_state: metrics keys present", "[interactive_json]") {
    auto world = make_world();
    auto j = serialize_ui_state(world);
    const auto& m = j["metrics"];
    REQUIRE(m.contains("npc_count"));
    REQUIRE(m.contains("business_count"));
    REQUIRE(m.contains("avg_npc_capital"));
    REQUIRE(m.contains("avg_spot_price"));
}

TEST_CASE("serialize_ui_state: only player-owned businesses included", "[interactive_json]") {
    auto world = make_world();

    NPCBusiness biz1{};
    biz1.id = 1;
    biz1.owner_id = world.player->id;  // player-owned
    biz1.sector = BusinessSector::retail;
    biz1.province_id = 0;
    biz1.cash = 1000.0f;
    world.npc_businesses.push_back(biz1);

    NPCBusiness biz2{};
    biz2.id = 2;
    biz2.owner_id = 999;  // NPC-owned
    biz2.sector = BusinessSector::manufacturing;
    biz2.province_id = 0;
    biz2.cash = 5000.0f;
    world.npc_businesses.push_back(biz2);

    auto j = serialize_ui_state(world);
    REQUIRE(j["businesses"].size() == 1);
    REQUIRE(j["businesses"][0]["id"] == 1);
}

// ---------------------------------------------------------------------------
// compute_avg helpers
// ---------------------------------------------------------------------------

TEST_CASE("compute_avg_npc_capital: empty returns 0", "[interactive_json]") {
    WorldState world{};
    REQUIRE_THAT(compute_avg_npc_capital(world), WithinAbs(0.0f, 0.001f));
}

TEST_CASE("compute_avg_spot_price: empty returns 0", "[interactive_json]") {
    WorldState world{};
    REQUIRE_THAT(compute_avg_spot_price(world), WithinAbs(0.0f, 0.001f));
}

// ---------------------------------------------------------------------------
// parse_and_enqueue_action
// ---------------------------------------------------------------------------

TEST_CASE("parse_and_enqueue_action: missing fields returns false", "[interactive_json]") {
    auto world = make_world();
    nlohmann::json bad = {{"cmd", "action"}};  // no action_type or payload
    REQUIRE_FALSE(parse_and_enqueue_action(bad, world));
    REQUIRE(world.player_action_queue.empty());
}

TEST_CASE("parse_and_enqueue_action: unknown type returns false", "[interactive_json]") {
    auto world = make_world();
    nlohmann::json cmd = {{"action_type", "explode"}, {"payload", {}}};
    REQUIRE_FALSE(parse_and_enqueue_action(cmd, world));
}

TEST_CASE("parse_and_enqueue_action: travel enqueues correctly", "[interactive_json]") {
    auto world = make_world();
    nlohmann::json cmd = {
        {"action_type", "travel"},
        {"payload", {{"destination_province_id", 1u}}}
    };
    REQUIRE(parse_and_enqueue_action(cmd, world));
    REQUIRE(world.player_action_queue.size() == 1);
    REQUIRE(world.player_action_queue.front().type == PlayerActionType::travel);
}

TEST_CASE("parse_and_enqueue_action: scene_card_choice enqueues", "[interactive_json]") {
    auto world = make_world();
    nlohmann::json cmd = {
        {"action_type", "scene_card_choice"},
        {"payload", {{"scene_card_id", 5u}, {"choice_id", 2u}}}
    };
    REQUIRE(parse_and_enqueue_action(cmd, world));
    REQUIRE(world.player_action_queue.front().type == PlayerActionType::scene_card_choice);
}

TEST_CASE("parse_and_enqueue_action: calendar_commit enqueues", "[interactive_json]") {
    auto world = make_world();
    nlohmann::json cmd = {
        {"action_type", "calendar_commit"},
        {"payload", {{"calendar_entry_id", 3u}, {"accept", true}}}
    };
    REQUIRE(parse_and_enqueue_action(cmd, world));
    REQUIRE(world.player_action_queue.front().type == PlayerActionType::calendar_commit);
}

TEST_CASE("parse_and_enqueue_action: start_business all 14 sectors", "[interactive_json]") {
    const std::vector<std::string> sectors = {
        "retail", "manufacturing", "food_beverage", "services", "real_estate",
        "agriculture", "energy", "technology", "finance", "transport_logistics",
        "media", "security", "research", "criminal"
    };
    for (const auto& sector : sectors) {
        auto world = make_world();
        nlohmann::json cmd = {
            {"action_type", "start_business"},
            {"payload", {{"sector", sector}, {"province_id", 0u}}}
        };
        INFO("sector: " << sector);
        REQUIRE(parse_and_enqueue_action(cmd, world));
        REQUIRE(world.player_action_queue.front().type == PlayerActionType::start_business);
        auto payload = std::get<StartBusinessAction>(world.player_action_queue.front().payload);
        // sector string must round-trip — the enqueued sector must not all default to retail
        if (sector != "retail") {
            REQUIRE(payload.sector != BusinessSector::retail);
        }
    }
}

TEST_CASE("parse_and_enqueue_action: initiate_contact enqueues", "[interactive_json]") {
    auto world = make_world();
    nlohmann::json cmd = {
        {"action_type", "initiate_contact"},
        {"payload", {{"target_npc_id", 42u}}}
    };
    REQUIRE(parse_and_enqueue_action(cmd, world));
    REQUIRE(world.player_action_queue.front().type == PlayerActionType::initiate_contact);
}
