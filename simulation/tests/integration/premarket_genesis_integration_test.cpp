// M7 — entry materialization (medieval band, design §5.5 / M7).
//
// A fresh pre-modern, non-founding start ("start in 500 CE") must open with the
// town economy already standing — workshops and manors materialized from the SAME
// laws the climb runs on (subsistence surplus → ox-cart catchment → urban heads →
// workshops at real headcounts; wealth-ranked lords → manors), not conjured.
// These tests pin the grounding contract:
//   - entities exist and are era-consistent (no anachronistic recipes),
//   - founder endowments are CONSERVED transfers (no wealth created or destroyed),
//   - manors appear exactly in manorial regimes,
//   - the whole materialization is deterministic.

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <map>
#include <string>

#include "core/world_gen/world_generator.h"
#include "core/world_state/world_state.h"

using namespace econlife;

namespace {
namespace fs = std::filesystem;

std::string find_path(const char* leaf) {
    const char* roots[] = {"packages/base_game/", "../packages/base_game/",
                           "../../packages/base_game/", "../../../packages/base_game/"};
    for (const auto* r : roots) {
        std::string p = std::string(r) + leaf;
        if (fs::exists(p))
            return fs::canonical(p).string();
    }
    return "";
}

// A fresh (non-founding) start in the given era, with the full production layer
// loaded so materialization has era content to draw on.
WorldGeneratorConfig medieval_config(uint64_t seed, uint8_t era) {
    WorldGeneratorConfig cfg{};
    cfg.seed = seed;
    cfg.province_count = 6;
    cfg.npc_count = 300;
    cfg.starting_era = era;
    cfg.founding_seed_mode = false;
    cfg.goods_directory = find_path("goods");
    cfg.recipes_directory = find_path("recipes");
    cfg.facility_types_filepath = find_path("facility_types/facility_types.csv");
    return cfg;
}

double total_npc_capital(const WorldState& w) {
    double sum = 0.0;
    for (const auto& n : w.significant_npcs)
        sum += static_cast<double>(n.capital);
    return sum;
}

double total_business_cash(const WorldState& w) {
    double sum = 0.0;
    for (const auto& b : w.npc_businesses)
        sum += static_cast<double>(b.cash);
    return sum;
}
}  // namespace

TEST_CASE("M7: medieval fresh start materializes an era-consistent town economy",
          "[integration][world_gen][premarket][m7]") {
    auto cfg = medieval_config(42, 5);  // era 5 = medieval (feudal — manorial)
    REQUIRE(!cfg.facility_types_filepath.empty());

    WorldState world = WorldGenerator::generate(cfg);

    // Entities materialized: the start is not a firm-less vacuum.
    REQUIRE(!world.npc_businesses.empty());
    REQUIRE(!world.facilities.empty());
    CHECK(world.facilities.size() == world.npc_businesses.size());  // one shop, one building

    // Recipe era index: everything standing must have existed by era 5.
    std::map<std::string, uint8_t> recipe_era;
    for (const auto& r : world.loaded_recipes)
        recipe_era[r.id] = r.era_available;

    bool saw_manor = false;
    for (const auto& f : world.facilities) {
        auto it = recipe_era.find(f.recipe_id);
        REQUIRE(it != recipe_era.end());  // every facility runs a real loaded recipe
        CHECK(it->second <= 5);           // no anachronisms in 500 CE
        CHECK(f.is_operational);
        CHECK(f.worker_count > 0);  // real headcounts, not ghost shops
        if (f.recipe_id == "manor_farming")
            saw_manor = true;
    }
    // Feudal is a manorial regime: the lord stratum holds estates.
    CHECK(saw_manor);

    // Every firm has a real founder among the significant population, and the
    // endowment actually landed (funded, not conjured-then-empty).
    std::map<uint32_t, const NPC*> npc_by_id;
    for (const auto& n : world.significant_npcs)
        npc_by_id[n.id] = &n;
    for (const auto& b : world.npc_businesses) {
        auto it = npc_by_id.find(b.owner_id);
        REQUIRE(it != npc_by_id.end());
        CHECK(b.cash > 0.0f);
        CHECK(it->second->capital >= 0.0f);  // endowment never overdraws the founder
        CHECK(b.revenue_per_tick == 0.0f);   // production earns it; nothing conjured
    }
}

TEST_CASE("M7: founder endowments are conserved transfers (no wealth minted)",
          "[integration][world_gen][premarket][m7]") {
    // Control: identical config but with no production catalogs, so entry
    // materialization is skipped. The NPC layer is generated from the same
    // stream in both runs (catalogs load later and use forked streams), so the
    // founding population and its wealth are identical — any difference in total
    // wealth would be money minted or burned by materialization.
    auto cfg = medieval_config(42, 5);
    auto control_cfg = cfg;
    control_cfg.recipes_directory.clear();
    control_cfg.facility_types_filepath.clear();

    WorldState with_entry = WorldGenerator::generate(cfg);
    WorldState control = WorldGenerator::generate(control_cfg);

    REQUIRE(!with_entry.npc_businesses.empty());
    REQUIRE(control.npc_businesses.empty());  // control skipped materialization
    REQUIRE(with_entry.significant_npcs.size() == control.significant_npcs.size());

    const double before = total_npc_capital(control);
    const double after = total_npc_capital(with_entry) + total_business_cash(with_entry);
    // Conserved: founder capital + firm cash == the same wealth, relocated.
    CHECK(std::abs(after - before) <= before * 1e-5);
}

TEST_CASE("M7: pre-manorial and founding-seed starts materialize nothing",
          "[integration][world_gen][premarket][m7]") {
    // Bronze-age start (era 2, barter): the commons economy — livelihoods, not
    // firms. Materializing workshops there would be an anachronism.
    WorldState bronze = WorldGenerator::generate(medieval_config(42, 2));
    CHECK(bronze.npc_businesses.empty());
    CHECK(bronze.facilities.empty());

    // Founding-seed mode: the climb carries aggregates; entities appear at
    // entry/full-LOD later, never at genesis.
    auto founding_cfg = medieval_config(42, 5);
    founding_cfg.founding_seed_mode = true;
    WorldState founding = WorldGenerator::generate(founding_cfg);
    CHECK(founding.npc_businesses.empty());
    CHECK(founding.facilities.empty());
}

TEST_CASE("M7: entry materialization is deterministic", "[integration][world_gen][premarket][m7]") {
    WorldState a = WorldGenerator::generate(medieval_config(1234, 5));
    WorldState b = WorldGenerator::generate(medieval_config(1234, 5));

    REQUIRE(a.npc_businesses.size() == b.npc_businesses.size());
    REQUIRE(a.facilities.size() == b.facilities.size());
    REQUIRE(!a.npc_businesses.empty());
    for (size_t i = 0; i < a.npc_businesses.size(); ++i) {
        CHECK(a.npc_businesses[i].id == b.npc_businesses[i].id);
        CHECK(a.npc_businesses[i].owner_id == b.npc_businesses[i].owner_id);
        CHECK(a.npc_businesses[i].province_id == b.npc_businesses[i].province_id);
        CHECK(a.npc_businesses[i].cash == b.npc_businesses[i].cash);
    }
    for (size_t i = 0; i < a.facilities.size(); ++i) {
        CHECK(a.facilities[i].recipe_id == b.facilities[i].recipe_id);
        CHECK(a.facilities[i].business_id == b.facilities[i].business_id);
    }
}
