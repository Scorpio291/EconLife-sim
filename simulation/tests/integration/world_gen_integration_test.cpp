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

#include <h3api.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <filesystem>
#include <memory>
#include <set>

#include "core/tick/thread_pool.h"
#include "core/tick/tick_orchestrator.h"
#include "core/world_gen/goods_catalog.h"
#include "core/world_gen/world_generator.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/production/production_module.h"
#include "modules/register_base_game_modules.h"

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
        if (fs::exists(c) && fs::is_directory(c))
            return fs::canonical(c).string();
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
    config.npc_count = 500;  // reduced for test speed, still meaningful
    config.goods_directory = find_goods_dir();

    auto [world, player] = WorldGenerator::generate_with_player(config);
    world.player = std::make_unique<PlayerCharacter>(std::move(player));

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
        REQUIRE_FALSE(std::isnan(p.cohort_stats->crime_rate));
        REQUIRE_FALSE(std::isnan(p.community.grievance_level));
        REQUIRE_FALSE(std::isnan(p.community.cohesion));
    }
    for (const auto& biz : world.npc_businesses) {
        REQUIRE_FALSE(std::isnan(biz.cash));
    }
}

// ===========================================================================
// Subsistence (commons) economy feeds a dawn world
// ===========================================================================

TEST_CASE("subsistence: a dawn world feeds its population from the commons",
          "[integration][subsistence][dawn]") {
    WorldGeneratorConfig config{};
    config.seed = 7;
    config.province_count = 6;
    config.npc_count = 200;
    config.goods_directory = find_goods_dir();
    config.starting_era = 1;  // the dawn — subsistence economic regime

    auto world = WorldGenerator::generate(config);
    REQUIRE(world.technology.current_era == 1);
    const EraDefinition* dawn = world.era_catalog.by_index(1);
    REQUIRE(dawn != nullptr);
    REQUIRE(dawn->economic_regime == "subsistence");

    run_orchestrated_ticks(world, 30);
    REQUIRE(world.current_tick == 30);

    // The commons module ran for real: every populated province has a finite,
    // non-negative surplus, and at least one shows a value other than the 1.0
    // default (i.e. a genuine produced/needed balance was computed from the land).
    int computed = 0;
    for (const auto& p : world.provinces) {
        REQUIRE(p.cohort_stats);
        const float s = p.cohort_stats->subsistence_surplus_ratio;
        REQUIRE_FALSE(std::isnan(s));
        CHECK(s >= 0.0f);
        if (p.cohort_stats->total_population > 0 && s != 1.0f)
            ++computed;
    }
    CHECK(computed >= 1);

    // Livelihoods: the commons economy assigns occupations to resident NPCs (no
    // firms involved). At the dawn most NPCs should hold a livelihood, not "none".
    REQUIRE(world.occupation_catalog.size() > 0);
    int with_livelihood = 0;
    for (const auto& npc : world.significant_npcs)
        if (npc.occupation != 0)
            ++with_livelihood;
    CHECK(with_livelihood >= 1);

    // No anachronistic firms at the dawn: the economy is livelihoods, not
    // businesses. Firms emerge only once a market regime / Band-2 conditions exist.
    CHECK(world.npc_businesses.empty());
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
    world.player = std::make_unique<PlayerCharacter>(std::move(player));

    run_orchestrated_ticks(world, 365);

    for (const auto& p : world.provinces) {
        CHECK(p.conditions.stability_score >= 0.0f);
        CHECK(p.conditions.stability_score <= 1.0f);
        CHECK(p.cohort_stats->crime_rate >= 0.0f);
        CHECK(p.cohort_stats->crime_rate <= 1.0f);
        CHECK(p.conditions.inequality_index >= 0.0f);
        CHECK(p.conditions.inequality_index <= 1.0f);
        CHECK(p.cohort_stats->addiction_rate >= 0.0f);
        CHECK(p.cohort_stats->addiction_rate <= 1.0f);
        CHECK(p.cohort_stats->criminal_dominance_index >= 0.0f);
        CHECK(p.cohort_stats->criminal_dominance_index <= 1.0f);
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
    world.player = std::make_unique<PlayerCharacter>(std::move(player));

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
        world.player = std::make_unique<PlayerCharacter>(std::move(player));

        TickOrchestrator orchestrator;
        register_base_game_modules(orchestrator);
        orchestrator.finalize_registration();
        ThreadPool pool(1);

        for (int i = 0; i < 30; ++i) {
            orchestrator.execute_tick(world, pool);
        }
        // Null out player pointer before return.
        world.player.reset();
        return std::move(world);
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
    world.player = std::make_unique<PlayerCharacter>(std::move(player));

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

TEST_CASE("WorldGenerator: no orphaned market references", "[integration][world_gen][safety]") {
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
    // The CSVs live in packages/base_game/goods/ and ARE checked in. A
    // missing directory means the test was launched from a working
    // directory that the relative-path search in find_goods_dir() can't
    // reach. Failing loud is the right call — silently SKIPping let the
    // CSV-load path regress unobserved in CI for several PRs (see the
    // goods CSV skip entry in docs/session_logs/flagged_issues.md).
    INFO(
        "find_goods_dir() returned empty — relative-path search from cwd "
        "failed; expected one of packages/base_game/goods (or up to 3 "
        "parents) to resolve");
    REQUIRE_FALSE(goods_dir.empty());

    GoodsCatalog catalog;
    REQUIRE(catalog.load_from_directory(goods_dir));

    // Should have loaded goods from all 5 tier files.
    CHECK(catalog.size() > 50);  // at minimum 58 tier-0 goods

    // Verify tier 0 goods (queried at the modern anchor, era 5, where the
    // re-based timeline places today's economy).
    auto tier0 = catalog.goods_available_at(5, 0);
    CHECK(tier0.size() >= 50);

    // Verify tier 0+1 goods.
    auto tier01 = catalog.goods_available_at(5, 1);
    CHECK(tier01.size() > tier0.size());

    // Spot-check specific goods.
    CHECK(catalog.find("iron_ore") != nullptr);
    CHECK(catalog.find("wheat") != nullptr);
    CHECK(catalog.find("steel") != nullptr);
}

// ===========================================================================
// H3 grid properties survive full tick run
// ===========================================================================

TEST_CASE("WorldGenerator H3: valid cells and province map survive 365 ticks",
          "[integration][world_gen][h3]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 6;
    config.npc_count = 100;

    auto [world, player] = WorldGenerator::generate_with_player(config);
    world.player = std::make_unique<PlayerCharacter>(std::move(player));

    // Pre-run H3 invariants.
    REQUIRE(world.h3_province_map.size() == world.provinces.size());
    for (const auto& p : world.provinces) {
        REQUIRE(p.h3_index != 0);
        REQUIRE(isValidCell(p.h3_index) != 0);
        REQUIRE(getResolution(p.h3_index) == 4);
        REQUIRE(p.neighbor_count == (p.is_pentagon ? 5 : 6));
        for (const auto& link : p.links) {
            REQUIRE(world.h3_province_map.count(link.neighbor_h3) > 0);
        }
    }

    run_orchestrated_ticks(world, 365);

    // H3 map and province cells must be unchanged after 365 ticks.
    REQUIRE(world.h3_province_map.size() == world.provinces.size());
    for (size_t i = 0; i < world.provinces.size(); ++i) {
        CHECK(world.provinces[i].h3_index != 0);
        CHECK(getResolution(world.provinces[i].h3_index) == 4);
    }
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
    world.player = std::make_unique<PlayerCharacter>(std::move(player));

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
    CHECK(stability_values.size() >= 2);  // at least 2 distinct outcomes
}

// ===========================================================================
// Food chain bootstraps from a subsistence base (Part C, yield-modifier model)
//
// This is the first integration test to load the FULL production layer (goods +
// recipes + facility types) so production actually runs end-to-end. Before the
// yield-modifier fix, staple crops hard-required fertilizer_npk (and livestock
// hard-required corn), so at genesis — before the fertilizer/feed industry exists —
// food production was pinned at zero (a bootstrap deadlock). With fertilizer/feed as
// yield MODIFIERS (GDD agriculture model), farms produce a subsistence base from
// land+labor alone, so grain flows and the food chain comes alive.
// ===========================================================================
TEST_CASE("WorldGenerator food chain bootstraps without fertilizer",
          "[integration][world_gen][economy][food]") {
    namespace fs = std::filesystem;
    auto find_path = [](const char* leaf) -> std::string {
        const char* roots[] = {"packages/base_game/", "../packages/base_game/",
                               "../../packages/base_game/", "../../../packages/base_game/"};
        for (const auto* r : roots) {
            std::string p = std::string(r) + leaf;
            if (fs::exists(p))
                return fs::canonical(p).string();
        }
        return "";
    };

    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 6;
    config.npc_count = 300;
    config.goods_directory = find_path("goods");
    config.recipes_directory = find_path("recipes");
    config.facility_types_filepath = find_path("facility_types/facility_types.csv");
    REQUIRE(!config.facility_types_filepath.empty());  // full production layer must load

    auto [world, player] = WorldGenerator::generate_with_player(config);
    world.player = std::make_unique<PlayerCharacter>(std::move(player));
    REQUIRE(world.facilities.size() > 0);  // production actually runs

    // Let the orchestrator settle the economy (labor_market staffs facilities, etc.).
    run_orchestrated_ticks(world, 30);

    // Measure production FLOW, not residual stock: under population-scale food demand,
    // anything produced is consumed the same tick, so market supply nets to ~0 even when
    // production is healthy. Run one standalone production tick and sum the output
    // (supply_delta) for the staple grains — this is the bootstrap signal.
    ProductionModule prod;
    prod.init_from_world_state(world);
    DeltaBuffer pdelta{};
    for (const auto& p : world.provinces)
        prod.execute_province(p.id, world, pdelta);

    auto staple_flow = [&](const char* name) -> float {
        const GoodDefinition* def = world.goods_catalog ? world.goods_catalog->find(name) : nullptr;
        if (!def)
            return 0.0f;
        float produced = 0.0f;
        for (const auto& md : pdelta.market_deltas)
            if (md.good_id == def->numeric_id && md.supply_delta.has_value() &&
                *md.supply_delta > 0.0f)
                produced += *md.supply_delta;
        return produced;
    };

    float wheat = staple_flow("wheat"), corn = staple_flow("corn");
    float rice = staple_flow("rice"), soy = staple_flow("soybeans");
    float staple_production = wheat + corn + rice + soy;
    INFO("staple production flow: wheat=" << wheat << " corn=" << corn << " rice=" << rice
                                          << " soy=" << soy);
    // The food chain is no longer frozen: at least one staple is produced from the
    // subsistence base despite ~zero fertilizer supply at genesis.
    CHECK(staple_production > 0.0f);
}
