// EconLife CLI — standalone executable for live simulation.
// Creates a world using WorldGenerator (data-driven from CSV goods catalog),
// registers all 43 base game modules, and runs the full tick loop with
// per-tick metric output.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "core/config/package_config.h"
#include "core/tick/thread_pool.h"
#include "core/tick/tick_orchestrator.h"
#include "core/world_gen/world_generator.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/register_base_game_modules.h"

using namespace econlife;

struct CliArgs {
    uint64_t seed = 42;
    uint32_t npc_count = 2000;
    uint32_t province_count = 6;
    uint32_t ticks = 365;
    uint32_t threads = 1;
    uint32_t report_every = 30;  // print metrics every N ticks
    uint8_t max_good_tier = 1;   // tier 0-1 at game start
    bool verbose = false;
    bool use_test_world = false;  // fallback to test_world_factory
    std::string goods_dir;        // path to goods CSVs
    std::string config_dir;       // optional override for config JSON directory
};

static void print_usage(const char* prog) {
    std::printf("Usage: %s [options]\n", prog);
    std::printf("  --seed N          World seed (default: 42)\n");
    std::printf("  --npcs N          NPC count (default: 2000)\n");
    std::printf("  --provinces N     Province count (default: 6)\n");
    std::printf("  --ticks N         Ticks to simulate (default: 365)\n");
    std::printf("  --threads N       Thread pool size (default: 1)\n");
    std::printf("  --report-every N  Print metrics every N ticks (default: 30)\n");
    std::printf("  --max-tier N      Max good tier at start (default: 1)\n");
    std::printf("  --goods-dir PATH  Path to goods CSV directory\n");
    std::printf("  --config-dir PATH Path to JSON config directory (default: auto-detect)\n");
    std::printf("  --test-world      Use minimal test world factory instead\n");
    std::printf("  --verbose         Print per-tick timing\n");
    std::printf("  --help            Show this message\n");
}

static CliArgs parse_args(int argc, char* argv[]) {
    CliArgs args;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            std::exit(0);
        } else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            args.seed = std::strtoull(argv[++i], nullptr, 10);
        } else if (std::strcmp(argv[i], "--npcs") == 0 && i + 1 < argc) {
            args.npc_count = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--provinces") == 0 && i + 1 < argc) {
            args.province_count = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) {
            args.ticks = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            args.threads = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--report-every") == 0 && i + 1 < argc) {
            args.report_every = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--max-tier") == 0 && i + 1 < argc) {
            args.max_good_tier = static_cast<uint8_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--goods-dir") == 0 && i + 1 < argc) {
            args.goods_dir = argv[++i];
        } else if (std::strcmp(argv[i], "--config-dir") == 0 && i + 1 < argc) {
            args.config_dir = argv[++i];
        } else if (std::strcmp(argv[i], "--test-world") == 0) {
            args.use_test_world = true;
        } else if (std::strcmp(argv[i], "--verbose") == 0) {
            args.verbose = true;
        }
    }
    return args;
}

// Auto-detect a base_game subdirectory by searching upward.
static std::string find_base_game_path(const char* subpath) {
    namespace fs = std::filesystem;
    static const char* prefixes[] = {
        "packages/base_game",
        "../packages/base_game",
        "../../packages/base_game",
        "../../../packages/base_game",
    };
    for (const auto* prefix : prefixes) {
        auto path = fs::path(prefix) / subpath;
        if (fs::exists(path)) {
            return fs::canonical(path).string();
        }
    }
    return "";
}

static std::string find_goods_directory() {
    return find_base_game_path("goods");
}

static std::string find_config_directory() {
    return find_base_game_path("config");
}

static std::string find_recipes_directory() {
    return find_base_game_path("recipes");
}

static std::string find_facility_types_filepath() {
    return find_base_game_path("facility_types/facility_types.csv");
}

static std::string find_technology_directory() {
    return find_base_game_path("technology");
}

static void print_header() {
    std::printf("%-6s | %-10s | %-10s | %-10s | %-10s | %-10s | %-10s | %-10s\n", "Tick",
                "AvgCapital", "AvgPrice", "Stability", "Crime", "Grievance", "Cohesion", "BizCash");
    std::printf(
        "-------+------------+------------+------------+------------+------------+------------+----"
        "-------\n");
}

static void print_metrics(const WorldState& world) {
    // Average NPC capital
    float total_capital = 0.0f;
    for (const auto& npc : world.significant_npcs) {
        total_capital += npc.capital;
    }
    float avg_capital = world.significant_npcs.empty()
                            ? 0.0f
                            : total_capital / static_cast<float>(world.significant_npcs.size());

    // Average market spot price
    float total_price = 0.0f;
    for (const auto& m : world.regional_markets) {
        total_price += m.spot_price;
    }
    float avg_price = world.regional_markets.empty()
                          ? 0.0f
                          : total_price / static_cast<float>(world.regional_markets.size());

    // Province 0 metrics (representative)
    float stability = 0.0f, crime = 0.0f, grievance = 0.0f, cohesion = 0.0f;
    if (!world.provinces.empty()) {
        stability = world.provinces[0].conditions.stability_score;
        crime = world.provinces[0].conditions.crime_rate;
        grievance = world.provinces[0].community.grievance_level;
        cohesion = world.provinces[0].community.cohesion;
    }

    // Average business cash
    float total_biz_cash = 0.0f;
    for (const auto& biz : world.npc_businesses) {
        total_biz_cash += biz.cash;
    }
    float avg_biz_cash = world.npc_businesses.empty()
                             ? 0.0f
                             : total_biz_cash / static_cast<float>(world.npc_businesses.size());

    std::printf("%-6u | %10.1f | %10.2f | %10.3f | %10.3f | %10.3f | %10.3f | %10.1f\n",
                world.current_tick, avg_capital, avg_price, stability, crime, grievance, cohesion,
                avg_biz_cash);
}

static bool check_nan_contamination(const WorldState& world) {
    for (const auto& npc : world.significant_npcs) {
        if (std::isnan(npc.capital))
            return true;
        for (float w : npc.motivations.weights) {
            if (std::isnan(w))
                return true;
        }
    }
    for (const auto& m : world.regional_markets) {
        if (std::isnan(m.spot_price) || std::isnan(m.supply))
            return true;
    }
    for (const auto& p : world.provinces) {
        if (std::isnan(p.conditions.stability_score) || std::isnan(p.conditions.crime_rate))
            return true;
    }
    return false;
}

int main(int argc, char* argv[]) {
    CliArgs args = parse_args(argc, argv);

    std::printf("EconLife CLI — seed=%llu, npcs=%u, provinces=%u, ticks=%u, threads=%u\n",
                static_cast<unsigned long long>(args.seed), args.npc_count, args.province_count,
                args.ticks, args.threads);

    // 1. Create world using WorldGenerator
    WorldGeneratorConfig gen_config{};
    gen_config.seed = args.seed;
    gen_config.province_count = args.province_count;
    gen_config.npc_count = args.npc_count;
    gen_config.max_good_tier = args.max_good_tier;

    // Resolve goods directory.
    if (!args.goods_dir.empty()) {
        gen_config.goods_directory = args.goods_dir;
    } else {
        gen_config.goods_directory = find_goods_directory();
    }

    gen_config.recipes_directory = find_recipes_directory();
    gen_config.facility_types_filepath = find_facility_types_filepath();
    gen_config.technology_directory = find_technology_directory();

    if (!gen_config.goods_directory.empty()) {
        std::printf("Goods directory: %s\n", gen_config.goods_directory.c_str());
    } else {
        std::printf("Goods directory: not found (using fallback goods)\n");
    }
    if (!gen_config.recipes_directory.empty()) {
        std::printf("Recipes directory: %s\n", gen_config.recipes_directory.c_str());
    }
    if (!gen_config.facility_types_filepath.empty()) {
        std::printf("Facility types: %s\n", gen_config.facility_types_filepath.c_str());
    }
    if (!gen_config.technology_directory.empty()) {
        std::printf("Technology data: %s\n", gen_config.technology_directory.c_str());
    }

    auto [world, player] = WorldGenerator::generate_with_player(gen_config);
    world.player = std::make_unique<PlayerCharacter>(std::move(player));

    std::printf(
        "World generated: %zu provinces, %zu NPCs, %zu businesses, %zu markets, "
        "%zu facilities, %zu recipes\n\n",
        world.provinces.size(), world.significant_npcs.size(), world.npc_businesses.size(),
        world.regional_markets.size(), world.facilities.size(), world.loaded_recipes.size());

    // 2. Load config (auto-detect or use override path).
    std::string config_dir = args.config_dir.empty() ? find_config_directory() : args.config_dir;
    PackageConfig pkg_config = load_package_config(config_dir);
    if (!config_dir.empty()) {
        std::printf("Config directory: %s\n", config_dir.c_str());
    } else {
        std::printf("Config directory: not found (using spec defaults)\n");
    }

    // 3. Set up orchestrator
    TickOrchestrator orchestrator;
    register_base_game_modules(orchestrator, pkg_config);
    orchestrator.finalize_registration();
    std::printf("Registered %zu modules, topological sort OK.\n\n", orchestrator.modules().size());

    // 4. Create thread pool
    ThreadPool pool(args.threads);

    // 5. Run ticks
    print_header();
    print_metrics(world);  // tick 0 baseline

    auto wall_start = std::chrono::steady_clock::now();
    double total_tick_ms = 0.0;
    double max_tick_ms = 0.0;

    for (uint32_t t = 0; t < args.ticks; ++t) {
        auto tick_start = std::chrono::steady_clock::now();
        orchestrator.execute_tick(world, pool);
        auto tick_end = std::chrono::steady_clock::now();

        double tick_ms = std::chrono::duration<double, std::milli>(tick_end - tick_start).count();
        total_tick_ms += tick_ms;
        if (tick_ms > max_tick_ms)
            max_tick_ms = tick_ms;

        if (args.verbose) {
            std::printf("  tick %u: %.2f ms\n", world.current_tick, tick_ms);
        }

        if (args.report_every > 0 && world.current_tick % args.report_every == 0) {
            print_metrics(world);
        }

        // NaN early abort
        if (check_nan_contamination(world)) {
            std::printf("\n*** NaN DETECTED at tick %u — aborting. ***\n", world.current_tick);
            return 1;
        }
    }

    auto wall_end = std::chrono::steady_clock::now();
    double wall_ms = std::chrono::duration<double, std::milli>(wall_end - wall_start).count();

    // Final report
    std::printf("\n--- Final state (tick %u) ---\n", world.current_tick);
    print_metrics(world);

    std::printf("\n--- Performance ---\n");
    std::printf("Wall time:    %.1f ms (%.1f ms/tick avg, %.1f ms max)\n", wall_ms,
                total_tick_ms / args.ticks, max_tick_ms);
    std::printf("NPCs:         %zu\n", world.significant_npcs.size());
    std::printf("Businesses:   %zu\n", world.npc_businesses.size());
    std::printf("Markets:      %zu\n", world.regional_markets.size());
    std::printf("Evidence:     %zu\n", world.evidence_pool.size());
    std::printf("Obligations:  %zu\n", world.obligation_network.size());

    // Province summary
    std::printf("\n--- Provinces ---\n");
    for (const auto& p : world.provinces) {
        std::printf("  [%u] %-20s pop=%u infra=%.2f agri=%.2f deposits=%zu markets=%zu npcs=%zu\n",
                    p.id, p.fictional_name.c_str(), p.demographics.total_population,
                    p.infrastructure_rating, p.agricultural_productivity, p.deposits.size(),
                    p.market_ids.size(), p.significant_npc_ids.size());
    }

    return 0;
}
