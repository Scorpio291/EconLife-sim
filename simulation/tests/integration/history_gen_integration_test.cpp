// Integration tests for the mechanical history-generation driver (P1):
// generate a world, then run the orchestrator forward for pre-game years.
// Validates that the forward-run path is sound — founding-seed worlds advance
// cleanly and deterministically; full-seed economies survive a multi-year run.

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <string>

#include "core/config/package_config.h"
#include "core/world_gen/world_generator.h"
#include "core/world_state/world_state.h"
#include "modules/history_generator.h"

using namespace econlife;

namespace {
namespace fs = std::filesystem;

std::string find_dir(const char* leaf) {
    const std::string candidates[] = {
        std::string("packages/base_game/") + leaf,
        std::string("../packages/base_game/") + leaf,
        std::string("../../packages/base_game/") + leaf,
        std::string("../../../packages/base_game/") + leaf,
    };
    for (const auto& c : candidates) {
        if (fs::exists(c) && fs::is_directory(c))
            return c;
    }
    return "";
}

WorldGeneratorConfig base_config(uint64_t seed, bool founding) {
    WorldGeneratorConfig cfg{};
    cfg.seed = seed;
    cfg.province_count = 6;
    cfg.npc_count = 150;  // small for test speed
    cfg.founding_seed_mode = founding;
    cfg.goods_directory = find_dir("goods");
    cfg.recipes_directory = find_dir("recipes");
    return cfg;
}

bool all_capitals_finite(const WorldState& w) {
    for (const auto& n : w.significant_npcs) {
        if (!std::isfinite(n.capital))
            return false;
    }
    return true;
}
}  // namespace

TEST_CASE("history gen: founding-seed world runs forward, valid, deterministic",
          "[history][integration]") {
    constexpr uint32_t kYears = 2;
    auto run = [] {
        return generate_world_with_history(base_config(42, true), PackageConfig{}, kYears, 1);
    };

    WorldState w1 = run();
    // The forward run advanced the full span and left a valid, finite world.
    CHECK(w1.provinces.size() == 6);
    CHECK_FALSE(w1.significant_npcs.empty());
    CHECK(w1.current_tick == kYears * 365u);
    CHECK(all_capitals_finite(w1));

    // Deterministic: same seed → identical evolved world.
    WorldState w2 = run();
    REQUIRE(w1.significant_npcs.size() == w2.significant_npcs.size());
    if (!w1.significant_npcs.empty()) {
        CHECK(w1.significant_npcs[0].id == w2.significant_npcs[0].id);
        CHECK(w1.significant_npcs[0].capital == w2.significant_npcs[0].capital);
    }
    REQUIRE(w1.npc_businesses.size() == w2.npc_businesses.size());
}

TEST_CASE("history gen: founding-seed world bootstraps an economy from zero firms",
          "[history][integration]") {
    // A founding seed starts with population but no firms. Opportunity-driven
    // firm genesis (P2) must grow an economy: firms form from local founders who
    // commit capital, so by the end of a short forward run the world has businesses
    // it did not start with — and those firms are legitimate and located.
    constexpr uint32_t kYears = 3;

    // Sanity: the bare founding seed really does start with zero firms.
    WorldState fresh = generate_world_with_history(base_config(42, true), PackageConfig{}, 0, 1);
    REQUIRE(fresh.npc_businesses.empty());
    auto total_capital = [](const WorldState& w) {
        double t = 0.0;
        for (const auto& n : w.significant_npcs)
            t += n.capital;
        return t;
    };
    auto solvent = [](const WorldState& w) {
        int n = 0;
        for (const auto& npc : w.significant_npcs)
            if (npc.capital > 1000.0f)
                ++n;
        return n;
    };

    WorldState w = generate_world_with_history(base_config(42, true), PackageConfig{}, kYears, 1);
    CHECK(w.current_tick == kYears * 365u);
    CHECK(all_capitals_finite(w));

    // Firms emerged from nothing — one founding wave at least seeds each province
    // with a viable enterprise, so the count clears a low floor.
    CHECK(w.npc_businesses.size() >= w.provinces.size());
    for (const auto& b : w.npc_businesses) {
        CHECK(b.province_id < w.provinces.size());  // located
        CHECK_FALSE(b.criminal_sector);             // genesis seeds legitimate firms
        CHECK(b.revenue_per_tick > 0.0f);           // earns from real activity
    }

    // The economy functions, not just exists. A founding population has no firms,
    // hence no wages, so it spends down its seed savings (a deep year-1 dip); genesis
    // firms then circulate income and the developing economy RECOVERS — aggregate
    // capital grows year over year through the run, and a real share of the
    // population is solvent at the end (income reaches people, not just owners).
    WorldState w1 = generate_world_with_history(base_config(42, true), PackageConfig{}, 1, 1);
    CHECK(total_capital(w) > total_capital(w1));            // growing as it develops
    CHECK(solvent(w) > static_cast<int>(w.significant_npcs.size() / 4));  // broad-based income
}

TEST_CASE("history gen: full-seed economy survives a multi-year forward run",
          "[history][integration]") {
    constexpr uint32_t kYears = 2;
    WorldState w = generate_world_with_history(base_config(7, false), PackageConfig{}, kYears, 1);

    CHECK(w.current_tick == kYears * 365u);
    CHECK_FALSE(w.significant_npcs.empty());
    CHECK_FALSE(w.npc_businesses.empty());  // the seeded economy persisted through history
    CHECK(all_capitals_finite(w));
}

TEST_CASE("history gen: zero history years returns the freshly generated world",
          "[history][integration]") {
    WorldState w = generate_world_with_history(base_config(99, false), PackageConfig{}, 0, 1);
    CHECK(w.current_tick == 0u);
    CHECK_FALSE(w.npc_businesses.empty());
}
