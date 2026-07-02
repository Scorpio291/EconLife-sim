// Warfare integration — the war engine under the REAL orchestrator at daily ticks.
//
// The unit tests drive WarfareModule::execute directly at year boundaries; this
// scenario runs the full module set one tick per day (the shipping cadence) on a
// founding-seed world and locks in the two invariants that only show up end-to-end:
//   1. Annual cadence: a war's plunder is applied exactly ONCE per year — not
//      re-fired and compounded on all 365 ticks of the year (review finding).
//   2. Conservation: plunder moves wealth between residents; the world total is
//      unchanged (nothing minted, nothing destroyed).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

#include "core/tick/thread_pool.h"
#include "core/tick/tick_orchestrator.h"
#include "core/world_gen/world_generator.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/register_base_game_modules.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

namespace {
std::string find_goods_dir_warfare() {
    std::string rel = "packages/base_game/goods";
    for (int up = 0; up < 4; ++up) {
        if (std::filesystem::exists(rel))
            return rel;
        rel = "../" + rel;
    }
    return "";
}
}  // namespace

TEST_CASE("Warfare: annual cadence and conserved plunder under the real orchestrator",
          "[integration][warfare][orchestrator]") {
    // Same seed, two runs: a CONTROL world with warfare's aggression zeroed and a WAR
    // world where the qualifying attack always fires. Other modules behave near-
    // identically (same seed), so the weak province's relative wealth isolates the
    // war effect end-to-end: ONE annual plunder leaves ~80% of the control wealth; a
    // per-tick re-fire (the review finding) would compound 0.8^365 to ~0.
    auto run_world = [](float aggression_prob, uint32_t& weak_out,
                        double& weak_capital_out, double& total_capital_out,
                        float& weak_war_mortality_out) {
        WorldGeneratorConfig config{};
        config.seed = 777;
        config.province_count = 6;
        config.npc_count = 120;
        config.founding_seed_mode = true;  // no hand-seeded economy
        config.starting_era = 1;           // the dawn (subsistence regime — warfare active)
        config.goods_directory = find_goods_dir_warfare();

        auto [world, player] = WorldGenerator::generate_with_player(config);
        world.player = std::make_unique<PlayerCharacter>(std::move(player));
        const uint32_t n = static_cast<uint32_t>(world.provinces.size());
        REQUIRE(n >= 2);

        // Pick a hegemon with a resolvable neighbour; that neighbour is the target.
        const auto h3_to_idx = build_h3_to_province_index(world.provinces);
        uint32_t strong = n, weak = n;
        for (uint32_t i = 0; i < n && strong == n; ++i) {
            for (const auto& link : world.provinces[i].links) {
                auto it = h3_to_idx.find(link.neighbor_h3);
                if (it != h3_to_idx.end()) {
                    strong = i;
                    weak = it->second;
                    break;
                }
            }
        }
        REQUIRE(strong < n);

        // Engineer the power landscape: every province equal (4,000 — parity, no wars
        // among themselves) except the hegemon (100,000). Only the hegemon attacks.
        auto set_pop = [&](uint32_t p, uint32_t pop) {
            REQUIRE(world.provinces[p].cohort_stats != nullptr);
            auto& cs = *world.provinces[p].cohort_stats;
            cs.cohorts.clear();
            PopulationCohort workers{};
            workers.size = pop;
            cs.cohorts[DemographicGroup::working_urban_mid] = workers;
            cs.total_population = pop;
        };
        for (uint32_t i = 0; i < n; ++i)
            set_pop(i, i == strong ? 100000u : 4000u);

        // Known capitals: the weak province's residents hold 100 each; all others 0.
        for (auto& npc : world.significant_npcs)
            npc.capital = (npc.home_province_id == weak) ? 100.0f : 0.0f;

        PackageConfig pkg{};
        pkg.subsistence.proto_capital_rate = 0.0f;      // no proto-capital confound
        pkg.warfare.base_aggression_prob = aggression_prob;

        TickOrchestrator orch;
        register_base_game_modules(orch, pkg);
        orch.finalize_registration();
        ThreadPool pool(2);

        // A year and a day at DAILY resolution — two annual decision ticks (0 and
        // 365). At tick 0 npc_indices_by_home_province is not yet populated (world
        // gen leaves it to the first apply_deltas rebuild), so the year-365 pass is
        // the one that exercises plunder with real resident wealth.
        for (uint32_t t = 0; t <= 365; ++t)
            orch.execute_tick(world, pool);

        weak_out = weak;
        weak_capital_out = 0.0;
        total_capital_out = 0.0;
        for (const auto& npc : world.significant_npcs) {
            if (npc.home_province_id == weak)
                weak_capital_out += npc.capital;
            total_capital_out += npc.capital;
        }
        weak_war_mortality_out = world.provinces[weak].cohort_stats->war_mortality;
    };

    uint32_t weak_ctl = 0, weak_war = 0;
    double weak_cap_ctl = 0, total_ctl = 0, weak_cap_war = 0, total_war = 0;
    float wm_ctl = 0, wm_war = 0;
    run_world(0.0f, weak_ctl, weak_cap_ctl, total_ctl, wm_ctl);
    run_world(1.0f, weak_war, weak_cap_war, total_war, wm_war);
    REQUIRE(weak_ctl == weak_war);  // same seed -> same geography/target

    // Control: no war fired; war world: the hegemon attacked the weak neighbour.
    CHECK(wm_ctl == 1.0f);
    CHECK(wm_war > 1.0f);
    REQUIRE(weak_cap_ctl > 0.0);

    // Exactly ONE plunder for the whole year: the weak province holds ~80% of its
    // control-run wealth (plunder_fraction 0.2) — not the ~0% a per-tick re-fire
    // would compound to, and not 100% (the war did bite).
    const double ratio = weak_cap_war / weak_cap_ctl;
    CHECK(ratio > 0.72);
    CHECK(ratio < 0.88);

    // Conservation sanity: war TRANSFERS wealth; the two runs' world totals stay
    // close (divergence only from war-induced demographic knock-ons downstream).
    CHECK_THAT(total_war, WithinAbs(total_ctl, total_ctl * 0.05 + 1.0));
}
