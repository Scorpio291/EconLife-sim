// EL-SLICE-01 AC-06 — diagnostic baseline observer (NOT an assertion test).
//
// Runs the game's world (create_game_session, strict content policy, shipped
// package config, the real registered module set) for seeds 42, 43 and 44 at
// 6 provinces and 500 significant NPCs, and prints the telemetry the task asks
// for at initialisation, tick 1, tick 30, year 1 and every further year up to
// the requested horizon.
//
// For comparison it can also run the PRE-EL-SLICE-01 harness world — goods
// catalogue only, modules registered with header defaults, no set_config —
// reconstructed here verbatim so the two can be read side by side. That world
// is not the game; it is printed only so the difference is visible.
//
// Hidden from every gate. Invoke explicitly, from the repository root:
//   econlife_emergence_tests "[.el-slice-01-baseline]"
// Environment:
//   EL_YEARS        horizon in years (default 1; the task's long run is 10)
//   EL_BUDGET_S     wall-time budget per seed in seconds (default 0 = none);
//                   a run that hits it stops and says so
//   EL_SEEDS        comma-separated seeds (default 42,43,44)
//   EL_LEGACY       1 = also run the pre-EL-SLICE-01 harness world

#include <sys/resource.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include "tests/integration/emergence_harness.h"

using namespace econlife;
using namespace econlife::emergence;

namespace {

uint32_t env_u32(const char* name, uint32_t def) {
    const char* v = std::getenv(name);
    return v ? static_cast<uint32_t>(std::strtoul(v, nullptr, 10)) : def;
}

double env_f(const char* name, double def) {
    const char* v = std::getenv(name);
    return v ? std::strtod(v, nullptr) : def;
}

std::vector<uint64_t> env_seeds() {
    const char* v = std::getenv("EL_SEEDS");
    std::vector<uint64_t> out;
    std::stringstream ss(v ? v : "42,43,44");
    std::string tok;
    while (std::getline(ss, tok, ','))
        if (!tok.empty())
            out.push_back(std::strtoull(tok.c_str(), nullptr, 10));
    return out;
}

long peak_rss_kb() {
    struct rusage ru {};
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss;  // kilobytes on Linux
}

void print_series(const std::vector<Snapshot>& series) {
    std::printf(
        "  tick  | npcs active wait  dead imp fled | biz  fac staffed | stab(mean/min) "
        "prov@0 | population   | evid   consq | era\n");
    for (const auto& s : series) {
        float smin = 1.0f;
        int at_zero = 0;
        for (float v : s.province_stability) {
            smin = std::min(smin, v);
            at_zero += (v <= 0.0f) ? 1 : 0;
        }
        std::printf(
            "  %5u | %4zu %6d %4d %5d %3d %4d | %3zu %4zu %7zu | %.3f / %.3f    %d/%zu  | %12.0f | "
            "%6zu %5zu | %d\n",
            s.tick, s.npcs, s.active, s.waiting, s.dead, s.imprisoned, s.fled, s.businesses,
            s.facilities, s.facilities_staffed, s.mean_stability, static_cast<double>(smin),
            at_zero, s.provinces, s.total_population, s.evidence_pool, s.consequence_queue, s.era);
    }
    if (!series.empty()) {
        const auto& last = series.back();
        std::printf("  per-province at tick %u (stability | population):", last.tick);
        for (std::size_t i = 0; i < last.province_stability.size(); ++i)
            std::printf("  p%zu %.3f|%.0f", i, static_cast<double>(last.province_stability[i]),
                        last.province_population[i]);
        std::printf("\n");
    }
}

// The pre-EL-SLICE-01 emergence harness setup, verbatim except for the
// snapshot cadence. Reference only.
std::vector<Snapshot> run_legacy_world(uint64_t seed, uint32_t npcs, uint32_t ticks,
                                       const std::vector<uint32_t>& at) {
    WorldGeneratorConfig config{};
    config.seed = seed;
    config.province_count = 6;
    config.npc_count = npcs;
    config.criminal_baseline = 0.10f;  // the old harness default
    config.goods_directory = resolve_base_game_paths(find_base_game_root()).goods_dir;
    auto [world, player] = WorldGenerator::generate_with_player(config);
    world.player = std::make_unique<PlayerCharacter>(std::move(player));
    TickOrchestrator orch;
    register_base_game_modules(orch);
    orch.finalize_registration();
    ThreadPool pool(1);
    std::printf(
        "  legacy world: recipes=%zu facilities=%zu businesses=%zu markets=%zu modules=%zu\n",
        world.loaded_recipes.size(), world.facilities.size(), world.npc_businesses.size(),
        world.regional_markets.size(), orch.modules().size());
    const std::set<uint32_t> want(at.begin(), at.end());
    std::vector<Snapshot> out{capture(world)};
    for (uint32_t t = 1; t <= ticks; ++t) {
        orch.execute_tick(world, pool);
        if (want.count(t) || t % 365 == 0 || t == ticks)
            out.push_back(capture(world));
    }
    return out;
}

}  // namespace

TEST_CASE("EL-SLICE-01 AC-06: diagnostic baseline, seeds 42/43/44", "[.el-slice-01-baseline]") {
    const uint32_t years = env_u32("EL_YEARS", 1);
    const double budget = env_f("EL_BUDGET_S", 0.0);
    const bool legacy = env_u32("EL_LEGACY", 0) != 0;
    const std::vector<uint32_t> at = {1, 30};

    for (uint64_t seed : env_seeds()) {
        RunSpec spec;
        spec.seed = seed;
        spec.npc_count = 500;
        spec.province_count = 6;
        spec.ticks = years * 365u;
        spec.snapshot_every = 365;
        spec.snapshot_at = at;
        spec.wall_budget_seconds = budget;

        const EmergenceRun run = run_world(spec);
        std::printf("\n=== EL-SLICE-01 baseline — GAME WORLD, seed %llu ===\n",
                    static_cast<unsigned long long>(seed));
        std::printf("%s", format_bootstrap_report(run.bootstrap).c_str());
        print_series(run.series);
        std::printf(
            "  timing: init %.2f s, ticks %.1f s, completed %u of %u ticks%s, peak RSS %ld MB\n",
            run.init_seconds, run.run_seconds, run.ticks_completed, spec.ticks,
            run.stopped_on_budget ? " (STOPPED: wall budget reached)" : "", peak_rss_kb() / 1024);
        std::fflush(stdout);

        if (legacy) {
            std::printf(
                "\n--- pre-EL-SLICE-01 harness world (reference, not the game), seed %llu ---\n",
                static_cast<unsigned long long>(seed));
            print_series(run_legacy_world(seed, 500, spec.ticks, at));
            std::fflush(stdout);
        }
        CHECK(run.ticks_completed > 0);
    }
}
