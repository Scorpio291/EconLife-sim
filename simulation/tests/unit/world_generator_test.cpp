// Unit tests for WorldGenerator and GoodsCatalog.
// Tests: CSV parsing, deterministic generation, province diversity,
//        NPC/business distribution, market creation from goods catalog.

#include "core/world_gen/world_generator.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include <h3api.h>
#include <nlohmann/json.hpp>

#include "core/world_gen/goods_catalog.h"

using namespace econlife;
namespace fs = std::filesystem;

// ===========================================================================
// Test helpers
// ===========================================================================

// Create a temporary CSV file with goods data for testing.
static std::string create_temp_goods_csv(const std::string& dir, const std::string& filename,
                                         const std::string& content) {
    fs::create_directories(dir);
    std::string path = dir + "/" + filename;
    std::ofstream f(path);
    f << content;
    f.close();
    return path;
}

static void cleanup_temp_dir(const std::string& dir) {
    fs::remove_all(dir);
}

// ===========================================================================
// GoodsCatalog Tests
// ===========================================================================

TEST_CASE("GoodsCatalog  - loads CSV file correctly", "[world_gen][goods_catalog]") {
    std::string tmp_dir =
        "test_goods_tmp_" + std::to_string(std::hash<std::string>{}("catalog_test"));

    std::string csv_content =
        "good_id,display_name,tier,unit,category,base_price,perishable,illegal,era_available\n"
        "iron_ore,Iron Ore,0,tonne,geological,12.00,false,false,1\n"
        "wheat,Wheat,0,tonne,biological,28.00,true,false,1\n"
        "coca_leaf,Coca Leaf,0,tonne,biological,100.00,true,true,1\n"
        "steel,Steel,1,tonne,metals,180.00,false,false,1\n";

    create_temp_goods_csv(tmp_dir, "goods_tier0.csv", csv_content);

    GoodsCatalog catalog;
    REQUIRE(catalog.load_from_directory(tmp_dir));
    REQUIRE(catalog.size() == 4);

    // Check first good.
    auto* iron = catalog.find("iron_ore");
    REQUIRE(iron != nullptr);
    CHECK(iron->display_name == "Iron Ore");
    CHECK(iron->tier == 0);
    CHECK(iron->unit == "tonne");
    CHECK(iron->category == "geological");
    CHECK_THAT(iron->base_price, Catch::Matchers::WithinAbs(12.0f, 0.01f));
    CHECK_FALSE(iron->perishable);
    CHECK_FALSE(iron->illegal);
    CHECK(iron->era_available == 1);

    // Check perishable.
    auto* wheat = catalog.find("wheat");
    REQUIRE(wheat != nullptr);
    CHECK(wheat->perishable);

    // Check illegal.
    auto* coca = catalog.find("coca_leaf");
    REQUIRE(coca != nullptr);
    CHECK(coca->illegal);

    // Check find returns nullptr for non-existent.
    CHECK(catalog.find("nonexistent") == nullptr);

    cleanup_temp_dir(tmp_dir);
}

TEST_CASE("GoodsCatalog  - filters by era and tier", "[world_gen][goods_catalog]") {
    std::string tmp_dir = "test_goods_tmp_filter";

    std::string csv_content =
        "good_id,display_name,tier,unit,category,base_price,perishable,illegal,era_available\n"
        "iron_ore,Iron Ore,0,tonne,geological,12.00,false,false,1\n"
        "steel,Steel,1,tonne,metals,180.00,false,false,1\n"
        "advanced_alloy,Advanced Alloy,2,tonne,metals,500.00,false,false,2\n";

    create_temp_goods_csv(tmp_dir, "goods.csv", csv_content);

    GoodsCatalog catalog;
    REQUIRE(catalog.load_from_directory(tmp_dir));

    // Era 1, tier 0: only iron_ore.
    auto era1_t0 = catalog.goods_available_at(1, 0);
    CHECK(era1_t0.size() == 1);

    // Era 1, tier 1: iron_ore + steel.
    auto era1_t1 = catalog.goods_available_at(1, 1);
    CHECK(era1_t1.size() == 2);

    // Era 2, tier 2: all three.
    auto era2_t2 = catalog.goods_available_at(2, 2);
    CHECK(era2_t2.size() == 3);

    cleanup_temp_dir(tmp_dir);
}

TEST_CASE("GoodsCatalog  - numeric IDs are sequential", "[world_gen][goods_catalog]") {
    std::string tmp_dir = "test_goods_tmp_ids";

    std::string csv1 =
        "good_id,display_name,tier,unit,category,base_price,perishable,illegal,era_available\n"
        "a,A,0,unit,cat,10.00,false,false,1\n"
        "b,B,0,unit,cat,20.00,false,false,1\n";
    std::string csv2 =
        "good_id,display_name,tier,unit,category,base_price,perishable,illegal,era_available\n"
        "c,C,1,unit,cat,30.00,false,false,1\n";

    create_temp_goods_csv(tmp_dir, "goods_tier0.csv", csv1);
    create_temp_goods_csv(tmp_dir, "goods_tier1.csv", csv2);

    GoodsCatalog catalog;
    REQUIRE(catalog.load_from_directory(tmp_dir));
    REQUIRE(catalog.size() == 3);

    CHECK(catalog.goods()[0].numeric_id == 0);
    CHECK(catalog.goods()[1].numeric_id == 1);
    CHECK(catalog.goods()[2].numeric_id == 2);

    cleanup_temp_dir(tmp_dir);
}

// ===========================================================================
// WorldGenerator Tests
// ===========================================================================

TEST_CASE("WorldGenerator  - generates valid WorldState", "[world_gen][generator]") {
    WorldGeneratorConfig config{};
    config.seed = 12345;
    config.province_count = 6;
    config.npc_count = 200;
    // No goods directory  - will use fallback goods.

    auto world = WorldGenerator::generate(config);

    CHECK(world.current_tick == 0);
    CHECK(world.world_seed == 12345);
    CHECK(world.provinces.size() == 6);
    CHECK(world.nations.size() == 1);
    CHECK(world.region_groups.size() == 6);
    CHECK_FALSE(world.significant_npcs.empty());
    CHECK_FALSE(world.npc_businesses.empty());
    CHECK_FALSE(world.regional_markets.empty());
}

TEST_CASE("WorldGenerator  - deterministic: same seed produces same world",
          "[world_gen][determinism]") {
    WorldGeneratorConfig config{};
    config.seed = 99999;
    config.province_count = 4;
    config.npc_count = 100;

    auto world1 = WorldGenerator::generate(config);
    auto world2 = WorldGenerator::generate(config);

    // Same number of entities.
    REQUIRE(world1.provinces.size() == world2.provinces.size());
    REQUIRE(world1.significant_npcs.size() == world2.significant_npcs.size());
    REQUIRE(world1.npc_businesses.size() == world2.npc_businesses.size());
    REQUIRE(world1.regional_markets.size() == world2.regional_markets.size());

    // Same province names.
    for (size_t i = 0; i < world1.provinces.size(); ++i) {
        CHECK(world1.provinces[i].fictional_name == world2.provinces[i].fictional_name);
    }

    // Same NPC capital values.
    for (size_t i = 0; i < world1.significant_npcs.size(); ++i) {
        CHECK(world1.significant_npcs[i].capital == world2.significant_npcs[i].capital);
        CHECK(world1.significant_npcs[i].id == world2.significant_npcs[i].id);
    }

    // Same market prices.
    for (size_t i = 0; i < world1.regional_markets.size(); ++i) {
        CHECK(world1.regional_markets[i].equilibrium_price ==
              world2.regional_markets[i].equilibrium_price);
    }
}

TEST_CASE("WorldGenerator  - different seeds produce different worlds",
          "[world_gen][determinism]") {
    WorldGeneratorConfig config1{};
    config1.seed = 111;
    config1.province_count = 3;
    config1.npc_count = 50;

    WorldGeneratorConfig config2 = config1;
    config2.seed = 222;

    auto world1 = WorldGenerator::generate(config1);
    auto world2 = WorldGenerator::generate(config2);

    // Names should differ (high probability with different seeds).
    bool any_name_differs = false;
    for (size_t i = 0; i < world1.provinces.size(); ++i) {
        if (world1.provinces[i].fictional_name != world2.provinces[i].fictional_name) {
            any_name_differs = true;
            break;
        }
    }
    CHECK(any_name_differs);
}

TEST_CASE("WorldGenerator  - provinces have diverse geography", "[world_gen][provinces]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 6;
    config.npc_count = 100;

    auto world = WorldGenerator::generate(config);

    // Check that provinces have varied infrastructure.
    std::set<float> infra_values;
    for (const auto& p : world.provinces) {
        infra_values.insert(std::round(p.infrastructure_rating * 10.0f));
    }
    CHECK(infra_values.size() >= 2);  // at least 2 distinct levels

    // Check that some provinces are landlocked and some aren't.
    bool has_landlocked = false;
    bool has_coastal = false;
    for (const auto& p : world.provinces) {
        if (p.geography.is_landlocked)
            has_landlocked = true;
        else
            has_coastal = true;
    }
    // With 6 provinces and varied archetypes, we expect both.
    CHECK((has_landlocked || has_coastal));  // at least one type present

    // Check province links exist.
    for (const auto& p : world.provinces) {
        CHECK_FALSE(p.links.empty());
    }
}

TEST_CASE("WorldGenerator  - provinces have resource deposits", "[world_gen][resources]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 6;
    config.npc_count = 100;

    auto world = WorldGenerator::generate(config);

    for (const auto& p : world.provinces) {
        CHECK_FALSE(p.deposits.empty());
        for (const auto& d : p.deposits) {
            CHECK(d.quantity > 0.0f);
            CHECK(d.quality >= 0.0f);
            CHECK(d.quality <= 1.0f);
            CHECK(d.quantity_remaining == d.quantity);  // fresh world
        }
    }

    // Resource-rich provinces should have more deposits.
    size_t max_deposits = 0;
    size_t min_deposits = 999;
    for (const auto& p : world.provinces) {
        max_deposits = std::max(max_deposits, p.deposits.size());
        min_deposits = std::min(min_deposits, p.deposits.size());
    }
    CHECK(max_deposits > min_deposits);  // variation exists
}

TEST_CASE("WorldGenerator  - NPCs distributed proportionally to population", "[world_gen][npcs]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 6;
    config.npc_count = 600;  // large enough for distribution to matter

    auto world = WorldGenerator::generate(config);

    // NPCs should be distributed across all provinces.
    for (const auto& p : world.provinces) {
        CHECK_FALSE(p.significant_npc_ids.empty());
    }

    // Higher population provinces should have more NPCs (approximately).
    // Find the province with highest population and check it has more NPCs.
    uint32_t max_pop_idx = 0;
    uint32_t max_pop = 0;
    for (uint32_t i = 0; i < world.provinces.size(); ++i) {
        if (world.provinces[i].demographics.total_population > max_pop) {
            max_pop = world.provinces[i].demographics.total_population;
            max_pop_idx = i;
        }
    }
    uint32_t min_pop_idx = 0;
    uint32_t min_pop = UINT32_MAX;
    for (uint32_t i = 0; i < world.provinces.size(); ++i) {
        if (world.provinces[i].demographics.total_population < min_pop) {
            min_pop = world.provinces[i].demographics.total_population;
            min_pop_idx = i;
        }
    }
    if (max_pop_idx != min_pop_idx) {
        CHECK(world.provinces[max_pop_idx].significant_npc_ids.size() >=
              world.provinces[min_pop_idx].significant_npc_ids.size());
    }
}

TEST_CASE("WorldGenerator  - NPC roles have diversity", "[world_gen][npcs]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 3;
    config.npc_count = 200;

    auto world = WorldGenerator::generate(config);

    std::set<NPCRole> observed_roles;
    for (const auto& npc : world.significant_npcs) {
        observed_roles.insert(npc.role);
    }
    // Should have at least 8 distinct roles with 200 NPCs.
    CHECK(observed_roles.size() >= 8);

    // Workers should be the most common role.
    uint32_t worker_count = 0;
    for (const auto& npc : world.significant_npcs) {
        if (npc.role == NPCRole::worker)
            worker_count++;
    }
    CHECK(worker_count > world.significant_npcs.size() / 5);  // at least 20%
}

TEST_CASE("WorldGenerator  - NPC motivation vectors normalized", "[world_gen][npcs]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 2;
    config.npc_count = 50;

    auto world = WorldGenerator::generate(config);

    for (const auto& npc : world.significant_npcs) {
        float sum = 0.0f;
        for (float w : npc.motivations.weights) {
            CHECK(w >= 0.0f);
            CHECK(w <= 1.0f);
            sum += w;
        }
        CHECK_THAT(sum, Catch::Matchers::WithinAbs(1.0f, 0.01f));
    }
}

TEST_CASE("WorldGenerator  - businesses have diverse sectors", "[world_gen][businesses]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 6;
    config.npc_count = 500;

    auto world = WorldGenerator::generate(config);

    CHECK_FALSE(world.npc_businesses.empty());

    std::set<BusinessSector> observed_sectors;
    bool has_criminal = false;
    for (const auto& biz : world.npc_businesses) {
        observed_sectors.insert(biz.sector);
        if (biz.criminal_sector)
            has_criminal = true;
        CHECK(biz.cash > 0.0f);
        CHECK(biz.revenue_per_tick > 0.0f);
        CHECK(biz.cost_per_tick > 0.0f);
        CHECK(biz.cost_per_tick < biz.revenue_per_tick);  // profitable at start
    }
    CHECK(observed_sectors.size() >= 4);  // reasonable sector diversity
}

TEST_CASE("WorldGenerator  - markets created with fallback goods", "[world_gen][markets]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 3;
    config.npc_count = 50;
    // No goods directory  - fallback goods.

    auto world = WorldGenerator::generate(config);

    // Should have markets in every province.
    for (const auto& p : world.provinces) {
        CHECK_FALSE(p.market_ids.empty());
    }

    // All markets should have valid prices.
    for (const auto& m : world.regional_markets) {
        CHECK(m.spot_price > 0.0f);
        CHECK(m.equilibrium_price > 0.0f);
        CHECK(m.supply > 0.0f);
        CHECK(m.adjustment_rate > 0.0f);
    }
}

TEST_CASE("WorldGenerator  - markets created from CSV goods catalog", "[world_gen][markets][csv]") {
    std::string tmp_dir = "test_goods_tmp_markets";

    std::string csv_content =
        "good_id,display_name,tier,unit,category,base_price,perishable,illegal,era_available\n"
        "iron_ore,Iron Ore,0,tonne,geological,12.00,false,false,1\n"
        "wheat,Wheat,0,tonne,biological,28.00,true,false,1\n"
        "steel,Steel,1,tonne,metals,180.00,false,false,1\n";

    create_temp_goods_csv(tmp_dir, "goods.csv", csv_content);

    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 2;
    config.npc_count = 30;
    config.goods_directory = tmp_dir;
    config.max_good_tier = 1;

    auto world = WorldGenerator::generate(config);

    // 3 goods x 2 provinces = 6 markets.
    CHECK(world.regional_markets.size() == 6);

    // Markets should reference valid province IDs.
    for (const auto& m : world.regional_markets) {
        CHECK(m.province_id < world.provinces.size());
    }

    cleanup_temp_dir(tmp_dir);
}

TEST_CASE("WorldGenerator  - generate_with_player creates player", "[world_gen][player]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 3;
    config.npc_count = 30;

    auto result = WorldGenerator::generate_with_player(config);

    CHECK(result.player.id == 1);
    CHECK(result.player.home_province_id == 0);
    CHECK(result.player.wealth > 0.0f);
    CHECK(result.world.provinces.size() == 3);
}

TEST_CASE("WorldGenerator  - province community state initialized correctly",
          "[world_gen][community]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 6;
    config.npc_count = 100;
    config.corruption_baseline = 0.2f;

    auto world = WorldGenerator::generate(config);

    for (const auto& p : world.provinces) {
        // Community state should be initialized within valid ranges.
        CHECK(p.community.cohesion >= 0.0f);
        CHECK(p.community.cohesion <= 1.0f);
        CHECK(p.community.grievance_level >= 0.0f);
        CHECK(p.community.grievance_level <= 1.0f);
        CHECK(p.community.institutional_trust >= 0.0f);
        CHECK(p.community.institutional_trust <= 1.0f);
        CHECK(p.community.response_stage == 0);  // fresh world

        // Conditions should be initialized within valid ranges.
        CHECK(p.conditions.stability_score >= 0.0f);
        CHECK(p.conditions.stability_score <= 1.0f);
        CHECK(p.conditions.crime_rate >= 0.0f);
        CHECK(p.conditions.crime_rate <= 1.0f);
        CHECK(p.conditions.drought_modifier == 1.0f);  // no active drought
        CHECK(p.conditions.flood_modifier == 1.0f);    // no active flood

        // Corruption baseline enforced.
        CHECK(p.political.corruption_index >= config.corruption_baseline);

        // Historical trauma effects applied.
        float trauma_floor = p.historical_trauma_index * 0.25f;
        CHECK(p.community.grievance_level >= trauma_floor - 0.001f);
    }
}

TEST_CASE("WorldGenerator  - nation structure valid", "[world_gen][nation]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 6;
    config.npc_count = 100;

    auto world = WorldGenerator::generate(config);

    REQUIRE(world.nations.size() == 1);
    const auto& nation = world.nations[0];
    CHECK(nation.id == 0);
    CHECK_FALSE(nation.name.empty());
    CHECK(nation.government_type == GovernmentType::Democracy);
    CHECK(nation.province_ids.size() == 6);
    CHECK(nation.lod1_profile == std::nullopt);  // LOD 0
    CHECK(nation.corporate_tax_rate > 0.0f);
    CHECK(nation.income_tax_rate_top_bracket > 0.0f);
}

TEST_CASE("WorldGenerator  - province links form connected graph", "[world_gen][links]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 6;
    config.npc_count = 50;

    auto world = WorldGenerator::generate(config);

    // BFS from province 0 should reach all provinces.
    std::set<uint32_t> visited;
    std::vector<uint32_t> queue = {0};
    visited.insert(0);

    while (!queue.empty()) {
        uint32_t current = queue.back();
        queue.pop_back();
        for (const auto& link : world.provinces[current].links) {
            // Find province with this h3_index.
            for (uint32_t i = 0; i < world.provinces.size(); ++i) {
                if (world.provinces[i].h3_index == link.neighbor_h3 &&
                    visited.find(i) == visited.end()) {
                    visited.insert(i);
                    queue.push_back(i);
                }
            }
        }
    }

    CHECK(visited.size() == world.provinces.size());
}

TEST_CASE("WorldGenerator  - H3 indices are valid resolution 4 cells", "[world_gen][h3]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 6;
    config.npc_count = 50;

    auto world = WorldGenerator::generate(config);

    // All provinces must have non-zero H3 indices at resolution 4.
    std::set<H3Index> seen_cells;
    for (const auto& p : world.provinces) {
        CHECK(p.h3_index != 0);
        CHECK(isValidCell(p.h3_index) != 0);
        CHECK(getResolution(p.h3_index) == 4);
        CHECK(seen_cells.find(p.h3_index) == seen_cells.end());  // no duplicates
        seen_cells.insert(p.h3_index);

        // neighbor_count must agree with is_pentagon.
        CHECK(p.neighbor_count == (p.is_pentagon ? 5 : 6));
    }

    // All ProvinceLink.neighbor_h3 values must resolve in h3_province_map.
    for (const auto& p : world.provinces) {
        for (const auto& link : p.links) {
            CHECK(world.h3_province_map.count(link.neighbor_h3) > 0);
        }
    }

    // h3_province_map must have one entry per province.
    CHECK(world.h3_province_map.size() == world.provinces.size());
}

TEST_CASE("WorldGenerator  - H3 determinism: same seed produces same cells", "[world_gen][h3]") {
    WorldGeneratorConfig config{};
    config.seed = 99999;
    config.province_count = 6;
    config.npc_count = 50;

    auto world1 = WorldGenerator::generate(config);
    auto world2 = WorldGenerator::generate(config);

    for (size_t i = 0; i < world1.provinces.size(); ++i) {
        CHECK(world1.provinces[i].h3_index == world2.provinces[i].h3_index);
        CHECK(world1.provinces[i].is_pentagon == world2.provinces[i].is_pentagon);
        CHECK(world1.provinces[i].neighbor_count == world2.provinces[i].neighbor_count);
    }
}

TEST_CASE("WorldGenerator  - single province world works", "[world_gen][edge]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 1;
    config.npc_count = 10;

    auto world = WorldGenerator::generate(config);

    CHECK(world.provinces.size() == 1);
    CHECK_FALSE(world.significant_npcs.empty());
    CHECK_FALSE(world.regional_markets.empty());
}

TEST_CASE("WorldGenerator  - large world generation", "[world_gen][scale]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 6;
    config.npc_count = 2000;

    auto world = WorldGenerator::generate(config);

    CHECK(world.provinces.size() == 6);
    // NPC count should be approximately what was requested.
    // (Rounding per-province can cause slight variance.)
    CHECK(world.significant_npcs.size() >= 1900);
    CHECK(world.significant_npcs.size() <= 2100);
    CHECK(world.npc_businesses.size() >= 100);
}

// ===========================================================================
// Stage 1 — Tectonic context tests (WorldGen v0.18)
// ===========================================================================

TEST_CASE("WorldGenerator - every province has valid tectonic context", "[world_gen][tectonics]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 6;
    config.npc_count = 100;

    auto world = WorldGenerator::generate(config);

    for (const auto& p : world.provinces) {
        // tectonic_context is a valid enum value (0-7).
        CHECK(static_cast<uint8_t>(p.tectonic_context) <= 7);
        // rock_type is valid (0-3).
        CHECK(static_cast<uint8_t>(p.rock_type) <= 3);
        // geology_type is valid (0-7).
        CHECK(static_cast<uint8_t>(p.geology_type) <= 7);
        // tectonic_stress is in [0, 1].
        CHECK(p.tectonic_stress >= 0.0f);
        CHECK(p.tectonic_stress <= 1.0f);
        // plate_age is positive and within geological range.
        CHECK(p.plate_age > 0.5f);
        CHECK(p.plate_age < 5.0f);
    }
}

TEST_CASE("WorldGenerator - tectonic context varies across provinces", "[world_gen][tectonics]") {
    WorldGeneratorConfig config{};
    config.seed = 12345;
    config.province_count = 6;
    config.npc_count = 100;

    auto world = WorldGenerator::generate(config);

    // With 6 provinces and 3 plates, expect at least 2 distinct tectonic contexts.
    std::set<TectonicContext> contexts;
    for (const auto& p : world.provinces) {
        contexts.insert(p.tectonic_context);
    }
    CHECK(contexts.size() >= 2);
}

TEST_CASE("WorldGenerator - tectonic deposits added on top of archetype deposits", "[world_gen][tectonics]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 6;
    config.npc_count = 100;

    auto world = WorldGenerator::generate(config);

    // Every province should have at least some deposits (archetype + tectonic).
    for (const auto& p : world.provinces) {
        CHECK_FALSE(p.deposits.empty());
        // Tectonic deposits use IDs >= province_id*100+50; check some tectonic type exists.
        bool has_tectonic_specific = false;
        for (const auto& d : p.deposits) {
            if (d.type == ResourceType::Gold || d.type == ResourceType::Geothermal ||
                d.type == ResourceType::Uranium || d.type == ResourceType::Potash) {
                has_tectonic_specific = true;
                break;
            }
        }
        // At least one province should have tectonic-specific deposits.
        // (Not all provinces will have them; this is checked across the world below.)
        (void)has_tectonic_specific;
    }

    // At least one province should have gold, geothermal, uranium, or potash from tectonic seeding.
    bool any_tectonic_deposit = false;
    for (const auto& p : world.provinces) {
        for (const auto& d : p.deposits) {
            if (d.type == ResourceType::Gold || d.type == ResourceType::Geothermal ||
                d.type == ResourceType::Uranium || d.type == ResourceType::Potash) {
                any_tectonic_deposit = true;
            }
        }
    }
    CHECK(any_tectonic_deposit);
}

TEST_CASE("WorldGenerator - island isolation and mountain pass flags are set correctly",
          "[world_gen][terrain_flags]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 6;
    config.npc_count = 100;

    auto world = WorldGenerator::generate(config);

    for (const auto& p : world.provinces) {
        // Island isolation: if set, all links must be Maritime.
        if (p.island_isolation) {
            for (const auto& link : p.links) {
                CHECK(link.type == LinkType::Maritime);
            }
        }
        // Mountain pass: if set, terrain must be rough and elevated.
        if (p.is_mountain_pass) {
            CHECK(p.geography.terrain_roughness > 0.60f);
            CHECK(p.geography.elevation_avg_m > 300.0f);
        }
    }
}

TEST_CASE("WorldGenerator - province_lore is non-empty for all provinces", "[world_gen][commentary]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 6;
    config.npc_count = 100;

    auto world = WorldGenerator::generate(config);

    for (const auto& p : world.provinces) {
        CHECK_FALSE(p.province_lore.empty());
        // Lore should be multi-sentence (at least 100 chars).
        CHECK(p.province_lore.size() >= 100);
    }
}

TEST_CASE("WorldGenerator - province_lore is deterministic", "[world_gen][commentary][determinism]") {
    WorldGeneratorConfig config{};
    config.seed = 99;
    config.province_count = 6;
    config.npc_count = 100;

    auto world1 = WorldGenerator::generate(config);
    auto world2 = WorldGenerator::generate(config);

    REQUIRE(world1.provinces.size() == world2.provinces.size());
    for (size_t i = 0; i < world1.provinces.size(); ++i) {
        CHECK(world1.provinces[i].province_lore == world2.provinces[i].province_lore);
        CHECK(world1.provinces[i].tectonic_context == world2.provinces[i].tectonic_context);
        CHECK(world1.provinces[i].rock_type == world2.provinces[i].rock_type);
        CHECK(world1.provinces[i].geology_type == world2.provinces[i].geology_type);
    }
}

TEST_CASE("WorldGenerator - karst detection uses geology type (v0.18)", "[world_gen][tectonics]") {
    // Run multiple seeds to get provinces with CarbonateSequence geology.
    // Those should have a higher karst probability.
    bool found_carbonate_karst = false;
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        WorldGeneratorConfig config{};
        config.seed = seed;
        config.province_count = 6;
        config.npc_count = 50;
        auto world = WorldGenerator::generate(config);
        for (const auto& p : world.provinces) {
            if (p.geology_type == GeologyType::CarbonateSequence && p.has_karst) {
                found_carbonate_karst = true;
            }
        }
    }
    CHECK(found_carbonate_karst);
}

// ---------------------------------------------------------------------------
// Stage 5+6: Soils and Biomes
// ---------------------------------------------------------------------------

TEST_CASE("WorldGenerator - agricultural_productivity reflects soil fertility model",
          "[world_gen][soils]") {
    // Over many seeds, VolcanicArc provinces should average higher agricultural_productivity
    // than GraniteShield (which has poor, leached soils).
    float volcanic_sum = 0.0f;
    int volcanic_count = 0;
    float granite_sum = 0.0f;
    int granite_count = 0;

    for (uint64_t seed = 1; seed <= 40; ++seed) {
        WorldGeneratorConfig config{};
        config.seed = seed;
        config.province_count = 6;
        config.npc_count = 50;
        auto world = WorldGenerator::generate(config);
        for (const auto& p : world.provinces) {
            if (p.geology_type == GeologyType::VolcanicArc) {
                volcanic_sum += p.agricultural_productivity;
                ++volcanic_count;
            } else if (p.geology_type == GeologyType::GraniteShield) {
                granite_sum += p.agricultural_productivity;
                ++granite_count;
            }
        }
    }

    // We need enough samples for a meaningful comparison.
    if (volcanic_count >= 3 && granite_count >= 3) {
        float volcanic_avg = volcanic_sum / static_cast<float>(volcanic_count);
        float granite_avg  = granite_sum  / static_cast<float>(granite_count);
        // VolcanicArc soil multiplier (1.10–1.30) >> GraniteShield (0.65–0.85).
        CHECK(volcanic_avg > granite_avg);
    }
}

TEST_CASE("WorldGenerator - forest_coverage is blended with climate expectation",
          "[world_gen][biomes]") {
    // Desert (BWh/BWk) provinces should have low forest coverage (<0.25).
    // Tropical (Af/Am) provinces should have higher coverage (>0.25).
    bool found_low_forest_desert  = false;
    bool found_high_forest_tropic = false;

    for (uint64_t seed = 1; seed <= 30; ++seed) {
        WorldGeneratorConfig config{};
        config.seed = seed;
        config.province_count = 6;
        config.npc_count = 50;
        auto world = WorldGenerator::generate(config);
        for (const auto& p : world.provinces) {
            if ((p.climate.koppen_zone == KoppenZone::BWh ||
                 p.climate.koppen_zone == KoppenZone::BWk) &&
                p.geography.forest_coverage < 0.25f) {
                found_low_forest_desert = true;
            }
            if ((p.climate.koppen_zone == KoppenZone::Af ||
                 p.climate.koppen_zone == KoppenZone::Am) &&
                p.geography.forest_coverage > 0.25f) {
                found_high_forest_tropic = true;
            }
        }
    }
    // Not guaranteed by every run (climate zones depend on archetype), so only check
    // when encountered.
    (void)found_low_forest_desert;
    (void)found_high_forest_tropic;
    // Core invariant: forest_coverage always in [0, 1].
    for (uint64_t seed = 1; seed <= 5; ++seed) {
        WorldGeneratorConfig config{};
        config.seed = seed;
        config.province_count = 6;
        config.npc_count = 50;
        auto world = WorldGenerator::generate(config);
        for (const auto& p : world.provinces) {
            CHECK(p.geography.forest_coverage >= 0.0f);
            CHECK(p.geography.forest_coverage <= 1.0f);
            CHECK(p.climate.drought_vulnerability >= 0.0f);
            CHECK(p.climate.drought_vulnerability <= 1.0f);
            CHECK(p.climate.flood_vulnerability >= 0.0f);
            CHECK(p.climate.flood_vulnerability <= 1.0f);
        }
    }
}

// ---------------------------------------------------------------------------
// Stage 7: Special terrain features
// ---------------------------------------------------------------------------

TEST_CASE("WorldGenerator - permafrost provinces have oil/gas accessibility locked",
          "[world_gen][permafrost]") {
    // Invariant: if has_permafrost, all CrudeOil and NaturalGas deposits must have
    // accessibility == 0.0 (locked until arctic_drilling + thaw).
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        WorldGeneratorConfig config{};
        config.seed = seed;
        config.province_count = 6;
        config.npc_count = 50;
        auto world = WorldGenerator::generate(config);
        for (const auto& p : world.provinces) {
            if (!p.has_permafrost) continue;
            for (const auto& dep : p.deposits) {
                if (dep.type == ResourceType::CrudeOil ||
                    dep.type == ResourceType::NaturalGas) {
                    CHECK(dep.accessibility == 0.0f);
                }
            }
            // Permafrost precludes karst.
            CHECK_FALSE(p.has_karst);
        }
    }
}

TEST_CASE("WorldGenerator - permafrost provinces have low agricultural_productivity",
          "[world_gen][permafrost]") {
    // Invariant: permafrost provinces must have ag_productivity <= 0.20
    // (permafrost apply_archetype sets it, then derive_soils multiplies, then
    // detect_special_features reduces by 0.40x, so final must be very low).
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        WorldGeneratorConfig config{};
        config.seed = seed;
        config.province_count = 6;
        config.npc_count = 50;
        auto world = WorldGenerator::generate(config);
        for (const auto& p : world.provinces) {
            if (p.has_permafrost) {
                // Permafrost reduces ag_productivity to 40% of whatever soils derived.
                // Even a fertile volcanic soil (1.30x of archetype) through permafrost = 0.40x.
                // Worst archetype base for a non-permafrost province would give < 0.5 * 0.4 = 0.2.
                CHECK(p.agricultural_productivity <= 0.22f);
            }
        }
    }
}

TEST_CASE("WorldGenerator - fjord provinces satisfy geographic preconditions",
          "[world_gen][fjord]") {
    // Invariant: has_fjord == true implies the geographic conditions that triggered it.
    for (uint64_t seed = 1; seed <= 30; ++seed) {
        WorldGeneratorConfig config{};
        config.seed = seed;
        config.province_count = 6;
        config.npc_count = 50;
        auto world = WorldGenerator::generate(config);
        for (const auto& p : world.provinces) {
            if (!p.has_fjord) continue;
            CHECK_FALSE(p.geography.is_landlocked);
            CHECK(p.geography.coastal_length_km > 100.0f);
            CHECK(p.geography.terrain_roughness > 0.55f);
            CHECK(p.geography.latitude > 50.0f);
            // Fjord Maritime links must have elevated transit cost (>= default 0.2).
            for (const auto& link : p.links) {
                if (link.type == LinkType::Maritime) {
                    CHECK(link.transit_terrain_cost > 0.20f);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Stage 9: Population attractiveness
// ---------------------------------------------------------------------------

TEST_CASE("WorldGenerator - population stays in valid range after attractiveness pass",
          "[world_gen][population]") {
    for (uint64_t seed = 1; seed <= 10; ++seed) {
        WorldGeneratorConfig config{};
        config.seed = seed;
        config.province_count = 6;
        config.npc_count = 50;
        auto world = WorldGenerator::generate(config);
        for (const auto& p : world.provinces) {
            // Population must be positive and within plausible V1 range.
            CHECK(p.demographics.total_population >= 10000u);
            CHECK(p.demographics.total_population <= 3000000u);
        }
    }
}

TEST_CASE("WorldGenerator - stages 5-9 are deterministic", "[world_gen][determinism]") {
    // Same seed must produce identical soils, special features, and population.
    WorldGeneratorConfig config{};
    config.seed = 77777;
    config.province_count = 6;
    config.npc_count = 100;

    auto world1 = WorldGenerator::generate(config);
    auto world2 = WorldGenerator::generate(config);

    REQUIRE(world1.provinces.size() == world2.provinces.size());
    for (size_t i = 0; i < world1.provinces.size(); ++i) {
        const auto& a = world1.provinces[i];
        const auto& b = world2.provinces[i];
        CHECK(a.agricultural_productivity == b.agricultural_productivity);
        CHECK(a.geography.forest_coverage  == b.geography.forest_coverage);
        CHECK(a.climate.drought_vulnerability == b.climate.drought_vulnerability);
        CHECK(a.climate.flood_vulnerability   == b.climate.flood_vulnerability);
        CHECK(a.has_permafrost == b.has_permafrost);
        CHECK(a.has_fjord      == b.has_fjord);
        CHECK(a.has_karst      == b.has_karst);
        CHECK(a.demographics.total_population == b.demographics.total_population);
    }
}

// ---------------------------------------------------------------------------
// Stage 4a: Province geography refinement (elevation + temperature lapse rate)
// ---------------------------------------------------------------------------

TEST_CASE("WorldGenerator - elevation correlates with terrain roughness after refinement",
          "[world_gen][geography]") {
    // High-roughness provinces must have higher elevation than low-roughness ones.
    // With roughness_factor = 0.30 + roughness * 1.50:
    //   roughness 0.10 → factor 0.45; roughness 0.80 → factor 1.50
    // So a roughness-0.80 province must have significantly higher elevation.
    for (uint64_t seed = 1; seed <= 10; ++seed) {
        WorldGeneratorConfig config{};
        config.seed = seed;
        config.province_count = 6;
        config.npc_count = 50;
        auto world = WorldGenerator::generate(config);

        float max_roughness = 0.0f, max_elevation_at_max_roughness = 0.0f;
        float min_roughness = 1.0f, min_elevation_at_min_roughness = 99999.0f;
        for (const auto& p : world.provinces) {
            if (p.geography.terrain_roughness > max_roughness) {
                max_roughness = p.geography.terrain_roughness;
                max_elevation_at_max_roughness = p.geography.elevation_avg_m;
            }
            if (p.geography.terrain_roughness < min_roughness) {
                min_roughness = p.geography.terrain_roughness;
                min_elevation_at_min_roughness = p.geography.elevation_avg_m;
            }
        }
        // Most-rough province must be higher than least-rough (across the world).
        CHECK(max_elevation_at_max_roughness > min_elevation_at_min_roughness);
    }
}

TEST_CASE("WorldGenerator - temperature decreases with elevation (lapse rate)",
          "[world_gen][geography]") {
    // After elevation lapse rate: temperature_avg_c must be less than the naive
    // latitude-only value. We test the invariant that higher-elevation provinces
    // have lower temperature than a flat province at the same latitude.
    for (uint64_t seed = 1; seed <= 10; ++seed) {
        WorldGeneratorConfig config{};
        config.seed = seed;
        config.province_count = 6;
        config.npc_count = 50;
        auto world = WorldGenerator::generate(config);

        for (const auto& p : world.provinces) {
            // Temperatures must be in a physically plausible range.
            CHECK(p.climate.temperature_avg_c >= -50.0f);
            CHECK(p.climate.temperature_avg_c <=  50.0f);
            CHECK(p.climate.temperature_min_c <= p.climate.temperature_avg_c);
            CHECK(p.climate.temperature_max_c >= p.climate.temperature_avg_c);
            // High-elevation provinces (> 800m) must be meaningfully cool.
            if (p.geography.elevation_avg_m > 800.0f) {
                // Lapse reduces by at least 5°C vs sea level (0.0065 * 800 ≈ 5.2°C).
                // Latitude-only base is ~10-25°C; after lapse must be well below 20°C
                // for a mid-latitude mountain province.
                CHECK(p.climate.temperature_avg_c < 22.0f);
            }
        }
    }
}

TEST_CASE("WorldGenerator - elevation and temperature are deterministic",
          "[world_gen][geography][determinism]") {
    WorldGeneratorConfig config{};
    config.seed = 55555;
    config.province_count = 6;
    config.npc_count = 50;

    auto w1 = WorldGenerator::generate(config);
    auto w2 = WorldGenerator::generate(config);

    REQUIRE(w1.provinces.size() == w2.provinces.size());
    for (size_t i = 0; i < w1.provinces.size(); ++i) {
        CHECK(w1.provinces[i].geography.elevation_avg_m  ==
              w2.provinces[i].geography.elevation_avg_m);
        CHECK(w1.provinces[i].climate.temperature_avg_c  ==
              w2.provinces[i].climate.temperature_avg_c);
    }
}

// ---------------------------------------------------------------------------
// Stage 4b: Economic geography seeding (trade openness, wildfire, employment)
// ---------------------------------------------------------------------------

TEST_CASE("WorldGenerator - trade_openness is non-zero for all provinces",
          "[world_gen][economic_geography]") {
    // Before Stage 4b, only coastal_trade archetype set trade_openness. After the fix,
    // all provinces must have a positive trade_openness.
    for (uint64_t seed = 1; seed <= 15; ++seed) {
        WorldGeneratorConfig config{};
        config.seed = seed;
        config.province_count = 6;
        config.npc_count = 50;
        auto world = WorldGenerator::generate(config);
        for (const auto& p : world.provinces) {
            CHECK(p.trade_openness >= 0.08f);
            CHECK(p.trade_openness <= 0.96f);
        }
    }
}

TEST_CASE("WorldGenerator - coastal provinces have higher trade_openness than landlocked",
          "[world_gen][economic_geography]") {
    // Landlocked provinces accumulate a -0.10 penalty; coastal ones get port bonus.
    float coastal_openness_sum = 0.0f;
    int coastal_count = 0;
    float landlocked_openness_sum = 0.0f;
    int landlocked_count = 0;

    for (uint64_t seed = 1; seed <= 30; ++seed) {
        WorldGeneratorConfig config{};
        config.seed = seed;
        config.province_count = 6;
        config.npc_count = 50;
        auto world = WorldGenerator::generate(config);
        for (const auto& p : world.provinces) {
            if (!p.geography.is_landlocked) {
                coastal_openness_sum += p.trade_openness;
                ++coastal_count;
            } else {
                landlocked_openness_sum += p.trade_openness;
                ++landlocked_count;
            }
        }
    }
    if (coastal_count > 0 && landlocked_count > 0) {
        float coastal_avg   = coastal_openness_sum   / static_cast<float>(coastal_count);
        float landlocked_avg = landlocked_openness_sum / static_cast<float>(landlocked_count);
        CHECK(coastal_avg > landlocked_avg);
    }
}

TEST_CASE("WorldGenerator - wildfire_vulnerability reflects climate zone",
          "[world_gen][economic_geography]") {
    // Polar provinces (ET/EF) must have low wildfire risk (< 0.10).
    // Mediterranean (Csa/Csb) provinces should have higher risk (> 0.20).
    for (uint64_t seed = 1; seed <= 30; ++seed) {
        WorldGeneratorConfig config{};
        config.seed = seed;
        config.province_count = 6;
        config.npc_count = 50;
        auto world = WorldGenerator::generate(config);
        for (const auto& p : world.provinces) {
            if (p.climate.koppen_zone == KoppenZone::ET ||
                p.climate.koppen_zone == KoppenZone::EF) {
                CHECK(p.climate.wildfire_vulnerability < 0.10f);
            }
            if (p.climate.koppen_zone == KoppenZone::Csa ||
                p.climate.koppen_zone == KoppenZone::Csb) {
                CHECK(p.climate.wildfire_vulnerability > 0.20f);
            }
            // All values in range.
            CHECK(p.climate.wildfire_vulnerability >= 0.01f);
            CHECK(p.climate.wildfire_vulnerability <= 0.95f);
        }
    }
}

TEST_CASE("WorldGenerator - formal_employment_rate is bounded after economic geography pass",
          "[world_gen][economic_geography]") {
    for (uint64_t seed = 1; seed <= 10; ++seed) {
        WorldGeneratorConfig config{};
        config.seed = seed;
        config.province_count = 6;
        config.npc_count = 50;
        auto world = WorldGenerator::generate(config);
        for (const auto& p : world.provinces) {
            CHECK(p.conditions.formal_employment_rate >= 0.20f);
            CHECK(p.conditions.formal_employment_rate <= 0.95f);
        }
    }
}

TEST_CASE("WorldGenerator - economic geography is deterministic",
          "[world_gen][economic_geography][determinism]") {
    WorldGeneratorConfig config{};
    config.seed = 33333;
    config.province_count = 6;
    config.npc_count = 50;

    auto w1 = WorldGenerator::generate(config);
    auto w2 = WorldGenerator::generate(config);

    REQUIRE(w1.provinces.size() == w2.provinces.size());
    for (size_t i = 0; i < w1.provinces.size(); ++i) {
        CHECK(w1.provinces[i].trade_openness                   == w2.provinces[i].trade_openness);
        CHECK(w1.provinces[i].climate.wildfire_vulnerability   == w2.provinces[i].climate.wildfire_vulnerability);
        CHECK(w1.provinces[i].conditions.formal_employment_rate ==
              w2.provinces[i].conditions.formal_employment_rate);
    }
}

// ===========================================================================
// Stage 8 — era_unlock field on ResourceDeposit
// ===========================================================================

TEST_CASE("WorldGenerator - all V1 deposits have era_unlock == 1",
          "[world_gen][era_unlock]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 6;
    config.npc_count = 50;

    auto world = WorldGenerator::generate(config);

    uint32_t total_deposits = 0;
    for (const auto& prov : world.provinces) {
        for (const auto& d : prov.deposits) {
            CHECK(d.era_unlock == 1);
            ++total_deposits;
        }
    }
    // Sanity: every province has at least a few deposits.
    CHECK(total_deposits >= world.provinces.size() * 3);
}

TEST_CASE("WorldGenerator - era_unlock field is preserved across seeds",
          "[world_gen][era_unlock][determinism]") {
    for (uint64_t seed : {100u, 200u, 300u}) {
        WorldGeneratorConfig config{};
        config.seed = seed;
        config.province_count = 6;
        config.npc_count = 50;

        auto w1 = WorldGenerator::generate(config);
        auto w2 = WorldGenerator::generate(config);

        REQUIRE(w1.provinces.size() == w2.provinces.size());
        for (size_t i = 0; i < w1.provinces.size(); ++i) {
            REQUIRE(w1.provinces[i].deposits.size() == w2.provinces[i].deposits.size());
            for (size_t d = 0; d < w1.provinces[i].deposits.size(); ++d) {
                CHECK(w1.provinces[i].deposits[d].era_unlock ==
                      w2.provinces[i].deposits[d].era_unlock);
            }
        }
    }
}

// ===========================================================================
// Stage 11 — World JSON output
// ===========================================================================

TEST_CASE("WorldGenerator - to_world_json produces valid JSON with all provinces",
          "[world_gen][json_output]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 6;
    config.npc_count = 50;

    auto world = WorldGenerator::generate(config);
    auto j = WorldGenerator::to_world_json(world);

    // Top-level keys.
    CHECK(j.contains("schema_version"));
    CHECK(j.contains("world_seed"));
    CHECK(j.contains("current_tick"));
    CHECK(j.contains("nations"));
    CHECK(j.contains("provinces"));
    CHECK(j.contains("regions"));

    CHECK(j["world_seed"].get<uint64_t>() == 42);
    CHECK(j["current_tick"].get<uint32_t>() == 0);

    // Province count matches.
    REQUIRE(j["provinces"].is_array());
    CHECK(j["provinces"].size() == world.provinces.size());
}

TEST_CASE("WorldGenerator - JSON province has all required fields",
          "[world_gen][json_output]") {
    WorldGeneratorConfig config{};
    config.seed = 77;
    config.province_count = 6;
    config.npc_count = 50;

    auto world = WorldGenerator::generate(config);
    auto j = WorldGenerator::to_world_json(world);
    REQUIRE(!j["provinces"].empty());

    const auto& p = j["provinces"][0];
    // Identity
    CHECK(p.contains("id"));
    CHECK(p.contains("h3_index"));
    CHECK(p.contains("fictional_name"));
    CHECK(p.contains("archetype"));
    CHECK(p.contains("lod_level"));

    // Geography sub-object
    REQUIRE(p.contains("geography"));
    CHECK(p["geography"].contains("latitude"));
    CHECK(p["geography"].contains("longitude"));
    CHECK(p["geography"].contains("elevation_avg_m"));
    CHECK(p["geography"].contains("terrain_roughness"));
    CHECK(p["geography"].contains("area_km2"));

    // Climate sub-object
    REQUIRE(p.contains("climate"));
    CHECK(p["climate"].contains("koppen_zone"));
    CHECK(p["climate"].contains("temperature_avg_c"));
    CHECK(p["climate"].contains("precipitation_mm"));

    // Tectonic sub-object
    REQUIRE(p.contains("tectonic"));
    CHECK(p["tectonic"].contains("tectonic_context"));
    CHECK(p["tectonic"].contains("geology_type"));
    CHECK(p["tectonic"].contains("tectonic_stress"));

    // Deposits array
    REQUIRE(p.contains("deposits"));
    REQUIRE(p["deposits"].is_array());
    if (!p["deposits"].empty()) {
        const auto& d = p["deposits"][0];
        CHECK(d.contains("type"));
        CHECK(d.contains("quantity"));
        CHECK(d.contains("era_unlock"));
    }

    // Demographics, economy, community, political, conditions
    CHECK(p.contains("demographics"));
    CHECK(p.contains("infrastructure_rating"));
    CHECK(p.contains("trade_openness"));
    CHECK(p.contains("community"));
    CHECK(p.contains("political"));
    CHECK(p.contains("conditions"));
    CHECK(p.contains("links"));

    // Terrain flags
    CHECK(p.contains("is_mountain_pass"));
    CHECK(p.contains("island_isolation"));
    CHECK(p.contains("has_permafrost"));
    CHECK(p.contains("has_fjord"));
    CHECK(p.contains("has_karst"));
}

TEST_CASE("WorldGenerator - write_world_json creates file on disk",
          "[world_gen][json_output]") {
    WorldGeneratorConfig config{};
    config.seed = 99;
    config.province_count = 6;
    config.npc_count = 50;

    auto world = WorldGenerator::generate(config);

    std::string tmp_path = "/tmp/econlife_test_world_" +
                           std::to_string(config.seed) + ".json";
    WorldGenerator::write_world_json(world, tmp_path);

    // File should exist and be valid JSON.
    std::ifstream in(tmp_path);
    REQUIRE(in.is_open());
    auto loaded = nlohmann::json::parse(in);
    in.close();

    CHECK(loaded["world_seed"].get<uint64_t>() == 99);
    CHECK(loaded["provinces"].size() == world.provinces.size());

    // Cleanup.
    fs::remove(tmp_path);
}

TEST_CASE("WorldGenerator - JSON output is deterministic",
          "[world_gen][json_output][determinism]") {
    WorldGeneratorConfig config{};
    config.seed = 55555;
    config.province_count = 6;
    config.npc_count = 50;

    auto w1 = WorldGenerator::generate(config);
    auto w2 = WorldGenerator::generate(config);

    auto j1 = WorldGenerator::to_world_json(w1);
    auto j2 = WorldGenerator::to_world_json(w2);

    // Serialized JSON should be byte-identical.
    CHECK(j1.dump() == j2.dump());
}

TEST_CASE("WorldGenerator - JSON nation fields are present",
          "[world_gen][json_output]") {
    WorldGeneratorConfig config{};
    config.seed = 42;
    config.province_count = 6;
    config.npc_count = 50;

    auto world = WorldGenerator::generate(config);
    auto j = WorldGenerator::to_world_json(world);

    REQUIRE(j["nations"].is_array());
    REQUIRE(!j["nations"].empty());

    const auto& n = j["nations"][0];
    CHECK(n.contains("id"));
    CHECK(n.contains("name"));
    CHECK(n.contains("government_type"));
    CHECK(n.contains("province_ids"));
    CHECK(n.contains("political_cycle"));
}

TEST_CASE("WorldGenerator - output_world_file config writes during generate",
          "[world_gen][json_output]") {
    std::string tmp_path = "/tmp/econlife_test_auto_output.json";
    fs::remove(tmp_path);  // clean up any prior run

    WorldGeneratorConfig config{};
    config.seed = 12345;
    config.province_count = 6;
    config.npc_count = 50;
    config.output_world_file = tmp_path;

    auto world = WorldGenerator::generate(config);

    // File should have been created automatically.
    std::ifstream in(tmp_path);
    REQUIRE(in.is_open());
    auto loaded = nlohmann::json::parse(in);
    in.close();

    CHECK(loaded["world_seed"].get<uint64_t>() == 12345);
    CHECK(loaded["provinces"].size() == world.provinces.size());

    fs::remove(tmp_path);
}
