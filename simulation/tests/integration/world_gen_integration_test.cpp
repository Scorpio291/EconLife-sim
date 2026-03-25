// World Generator integration tests — verify that WorldGenerator-created worlds
// run through the full orchestrator without crashes, NaN contamination, or
// value range violations at V1 scale (6 provinces, 2000 NPCs).
//
// These tests validate:
// 1. WorldGenerator output feeds cleanly into the tick orchestrator
// 2. Full-year simulation (365 ticks) at V1 scale completes
// 3. CSV-loaded goods create valid market dynamics
// 4. Province diversity produces meaningful economic differentiation
// 5. Edge cases: orphaned refs, extreme values, empty provinces

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_gen/world_generator.h"
#include "core/world_gen/goods_catalog.h"
#include "core/world_state/world_state.h"
#include "core/world_state/player.h"
#include "core/tick/tick_orchestrator.h"
#include "core/tick/thread_pool.h"
#include "modules/register_base_game_modules.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>

using namespace econlife;
using Catch::Matchers::WithinAbs;

// ---------------------------------------------------------------------------
// Helper: find goods CSV directory
// ---------------------------------------------------------------------------
static std::string find_goods_dir() {
    namespace fs = std::filesystem;
    static const char* candidates[] = {
        "packages/base_game/goods",
        "../packages/base_game/goods",
        "../../packages/base_game/goods",
        "../../../packages/base_game/goods",
    };
    for (const auto* c : candidates) {
        if (fs::exists(c) && fs::is_directory(c)) return fs::canonical(c).string();
    }
    return "";
}

// ---------------------------------------------------------------------------
// Helper: run N ticks using full orchestrator
// ---------------------------------------------------------------------------
static void run_orchestrated_ticks(WorldState& world, uint32_t tick_count) {
    TickOrchestrator orchestrator;
    register_base_game_modules(orchestrator);
    orchestrator.finalize_registration();
    ThreadPool pool(1);

    for (uint32_t t = 0; t < tick_count; ++t) {
        orchestrator.execute_tick(world, pool);
    }
}

// ===========================================================================
// V1-scale full-year test with WorldGenerator
// ===========================================================================

TEST_CASE("WorldGenerator world runs 365 ticks at V1 scale", "[integration][world_gen][year]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 6;
    config.npc_count = 500; // reduced for test speed, still meaningful
    config.goods_directory = find_goods_dir();

    auto [world, player] = WorldGenerator::generate_with_player(config);
    world.player = &player;

    REQUIRE(world.provinces.size() == 6);
    REQUIRE(world.significant_npcs.size() >= 450);

    run_orchestrated_ticks(world, 365);

    REQUIRE(world.current_tick == 365);

    // No NaN contamination after full year.
    for (const auto& npc : world.significant_npcs) {
        REQUIRE_FALSE(std::isnan(npc.capital));
        for (float w : npc.motivations.weights) {
            REQUIRE_FALSE(std::isnan(w));
        }
    }
    for (const auto& m : world.regional_markets) {
        REQUIRE_FALSE(std::isnan(m.spot_price));
        REQUIRE_FALSE(std::isnan(m.supply));
        REQUIRE_FALSE(std::isnan(m.equilibrium_price));
    }
    for (const auto& p : world.provinces) {
        REQUIRE_FALSE(std::isnan(p.conditions.stability_score));
        REQUIRE_FALSE(std::isnan(p.conditions.crime_rate));
        REQUIRE_FALSE(std::isnan(p.community.grievance_level));
        REQUIRE_FALSE(std::isnan(p.community.cohesion));
    }
    for (const auto& biz : world.npc_businesses) {
        REQUIRE_FALSE(std::isnan(biz.cash));
    }
}

// ===========================================================================
// Province conditions stay clamped over full year
// ===========================================================================

TEST_CASE("WorldGenerator world: province conditions stay in [0,1] over 365 ticks",
          "[integration][world_gen][safety]") {
    WorldGeneratorConfig config{};
    config.seed = 77777;
    config.province_count = 6;
    config.npc_count = 200;
    config.goods_directory = find_goods_dir();

    auto [world, player] = WorldGenerator::generate_with_player(config);
    world.player = &player;

    run_orchestrated_ticks(world, 365);

    for (const auto& p : world.provinces) {
        CHECK(p.conditions.stability_score >= 0.0f);
        CHECK(p.conditions.stability_score <= 1.0f);
        CHECK(p.conditions.crime_rate >= 0.0f);
        CHECK(p.conditions.crime_rate <= 1.0f);
        CHECK(p.conditions.inequality_index >= 0.0f);
        CHECK(p.conditions.inequality_index <= 1.0f);
        CHECK(p.conditions.addiction_rate >= 0.0f);
        CHECK(p.conditions.addiction_rate <= 1.0f);
        CHECK(p.conditions.criminal_dominance_index >= 0.0f);
        CHECK(p.conditions.criminal_dominance_index <= 1.0f);
        CHECK(p.community.grievance_level >= 0.0f);
        CHECK(p.community.grievance_level <= 1.0f);
        CHECK(p.community.cohesion >= 0.0f);
        CHECK(p.community.cohesion <= 1.0f);
        CHECK(p.community.institutional_trust >= 0.0f);
        CHECK(p.community.institutional_trust <= 1.0f);
    }
}

// ===========================================================================
// Market prices remain positive over full year
// ===========================================================================

TEST_CASE("WorldGenerator world: market prices stay positive over 365 ticks",
          "[integration][world_gen][economy]") {
    WorldGeneratorConfig config{};
    config.seed = 12345;
    config.province_count = 3;
    config.npc_count = 100;
    config.goods_directory = find_goods_dir();

    auto [world, player] = WorldGenerator::generate_with_player(config);
    world.player = &player;

    run_orchestrated_ticks(world, 365);

    for (const auto& m : world.regional_markets) {
        CHECK(m.spot_price > 0.0f);
        CHECK(m.equilibrium_price > 0.0f);
        CHECK(m.supply >= 0.0f);
    }
}

// ===========================================================================
// Determinism: WorldGenerator + orchestrator produces same output
// ===========================================================================

TEST_CASE("WorldGenerator determinism: same seed, same output after 30 ticks",
          "[integration][world_gen][determinism]") {
    auto run_30 = [](uint64_t seed) -> WorldState {
        WorldGeneratorConfig config{};
        config.seed = seed;
        config.province_count = 3;
        config.npc_count = 50;
        config.goods_directory = find_goods_dir();

        auto [world, player] = WorldGenerator::generate_with_player(config);
        world.player = &player;

        TickOrchestrator orchestrator;
        register_base_game_modules(orchestrator);
        orchestrator.finalize_registration();
        ThreadPool pool(1);

        for (int i = 0; i < 30; ++i) {
            orchestrator.execute_tick(world, pool);
        }
        // Null out player pointer before return (it's a local reference).
        world.player = nullptr;
        return world;
    };

    auto a = run_30(55555);
    auto b = run_30(55555);

    REQUIRE(a.current_tick == b.current_tick);
    REQUIRE(a.significant_npcs.size() == b.significant_npcs.size());

    for (size_t i = 0; i < a.significant_npcs.size(); ++i) {
        CHECK(a.significant_npcs[i].capital == b.significant_npcs[i].capital);
    }
    for (size_t i = 0; i < a.regional_markets.size(); ++i) {
        CHECK(a.regional_markets[i].spot_price == b.regional_markets[i].spot_price);
    }
    for (size_t i = 0; i < a.provinces.size(); ++i) {
        CHECK(a.provinces[i].conditions.stability_score ==
              b.provinces[i].conditions.stability_score);
    }
}

// ===========================================================================
// Edge case: minimal world (1 province, 5 NPCs)
// ===========================================================================

TEST_CASE("WorldGenerator minimal world: 1 province 5 NPCs runs 100 ticks",
          "[integration][world_gen][edge]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 1;
    config.npc_count = 5;
    // No goods dir — fallback goods.

    auto [world, player] = WorldGenerator::generate_with_player(config);
    world.player = &player;

    REQUIRE(world.provinces.size() == 1);
    REQUIRE(world.significant_npcs.size() >= 1);

    run_orchestrated_ticks(world, 100);

    REQUIRE(world.current_tick == 100);
    for (const auto& npc : world.significant_npcs) {
        REQUIRE_FALSE(std::isnan(npc.capital));
    }
}

// ===========================================================================
// Edge case: all NPC IDs referenced in province exist in significant_npcs
// ===========================================================================

TEST_CASE("WorldGenerator: no orphaned NPC references in provinces",
          "[integration][world_gen][safety]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 6;
    config.npc_count = 300;

    auto world = WorldGenerator::generate(config);

    // Build set of all NPC IDs.
    std::set<uint32_t> npc_ids;
    for (const auto& npc : world.significant_npcs) {
        npc_ids.insert(npc.id);
    }

    // Every NPC ID referenced in a province must exist.
    for (const auto& p : world.provinces) {
        for (uint32_t npc_id : p.significant_npc_ids) {
            CHECK(npc_ids.count(npc_id) > 0);
        }
    }

    // Every NPC's home_province_id must be a valid province.
    for (const auto& npc : world.significant_npcs) {
        CHECK(npc.home_province_id < world.provinces.size());
        CHECK(npc.current_province_id < world.provinces.size());
    }
}

// ===========================================================================
// Edge case: all business owners exist as NPCs
// ===========================================================================

TEST_CASE("WorldGenerator: no orphaned business owner references",
          "[integration][world_gen][safety]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 6;
    config.npc_count = 300;

    auto world = WorldGenerator::generate(config);

    std::set<uint32_t> npc_ids;
    for (const auto& npc : world.significant_npcs) {
        npc_ids.insert(npc.id);
    }

    for (const auto& biz : world.npc_businesses) {
        CHECK(npc_ids.count(biz.owner_id) > 0);
        CHECK(biz.province_id < world.provinces.size());
    }
}

// ===========================================================================
// Edge case: all market_ids in provinces point to valid markets
// ===========================================================================

TEST_CASE("WorldGenerator: no orphaned market references",
          "[integration][world_gen][safety]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 6;
    config.npc_count = 100;
    config.goods_directory = find_goods_dir();

    auto world = WorldGenerator::generate(config);

    for (const auto& p : world.provinces) {
        for (uint32_t market_id : p.market_ids) {
            REQUIRE(market_id < world.regional_markets.size());
            // Market's province_id should match this province.
            CHECK(world.regional_markets[market_id].province_id == p.id);
        }
    }
}

// ===========================================================================
// CSV Goods Catalog loads expected number of goods
// ===========================================================================

TEST_CASE("GoodsCatalog loads all tier 0-4 goods from base_game CSVs",
          "[integration][world_gen][csv]") {
    std::string goods_dir = find_goods_dir();
    if (goods_dir.empty()) {
        SKIP("Goods CSV directory not found");
    }

    GoodsCatalog catalog;
    REQUIRE(catalog.load_from_directory(goods_dir));

    // Should have loaded goods from all 5 tier files.
    CHECK(catalog.size() > 50);  // at minimum 58 tier-0 goods

    // Verify tier 0 goods.
    auto tier0 = catalog.goods_available_at(1, 0);
    CHECK(tier0.size() >= 50);

    // Verify tier 0+1 goods.
    auto tier01 = catalog.goods_available_at(1, 1);
    CHECK(tier01.size() > tier0.size());

    // Spot-check specific goods.
    CHECK(catalog.find("iron_ore") != nullptr);
    CHECK(catalog.find("wheat") != nullptr);
    CHECK(catalog.find("steel") != nullptr);
}

// ===========================================================================
// Province economic diversity after full year
// ===========================================================================

TEST_CASE("WorldGenerator provinces show economic differentiation after 30 ticks",
          "[integration][world_gen][economy]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 6;
    config.npc_count = 300;

    auto [world, player] = WorldGenerator::generate_with_player(config);
    world.player = &player;

    // Record initial province diversity.
    float initial_infra_spread = 0.0f;
    {
        float min_infra = 1.0f, max_infra = 0.0f;
        for (const auto& p : world.provinces) {
            min_infra = std::min(min_infra, p.infrastructure_rating);
            max_infra = std::max(max_infra, p.infrastructure_rating);
        }
        initial_infra_spread = max_infra - min_infra;
    }

    run_orchestrated_ticks(world, 30);

    // Provinces should still have varied infrastructure (not collapsed to uniform).
    float final_infra_spread = 0.0f;
    {
        float min_infra = 1.0f, max_infra = 0.0f;
        for (const auto& p : world.provinces) {
            min_infra = std::min(min_infra, p.infrastructure_rating);
            max_infra = std::max(max_infra, p.infrastructure_rating);
        }
        final_infra_spread = max_infra - min_infra;
    }

    // Infrastructure is static at runtime, so spread should be exactly the same.
    CHECK_THAT(final_infra_spread, WithinAbs(initial_infra_spread, 0.001f));

    // Provinces should have different stability outcomes after 30 ticks.
    std::set<float> stability_values;
    for (const auto& p : world.provinces) {
        stability_values.insert(std::round(p.conditions.stability_score * 100.0f));
    }
    CHECK(stability_values.size() >= 2); // at least 2 distinct outcomes
}
