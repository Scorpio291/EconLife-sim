#pragma once

// game_bootstrap — the ONE way a base-game session is initialised.
//
// A play session (econlife_cli, and through it the UI bridge) and every
// behavioural integration test build their world through create_game_session().
// Before this existed, the CLI and player_loop_harness.h each had their own copy
// of the content lookup, and emergence_harness.h had a third that loaded only the
// goods catalogue and no package config at all. The behavioural suite therefore
// ran a world with 0 recipes, 0 facilities and header-default module parameters,
// and its green result said nothing about the world the player gets (EL-SLICE-01).
//
// What this file owns:
//   - where base-game content is found (the CLI's upward search, unchanged);
//   - which WorldGeneratorConfig fields a session sets from that content;
//   - loading PackageConfig and registering the base-game module set with it;
//   - an initialisation report (what was loaded, what was instantiated).
//
// What it deliberately does NOT own: any new parser. Catalogues are read by the
// same loaders world generation uses, and PackageConfig by load_package_config.
//
// Two content policies:
//   lenient — today's game behaviour, unchanged. Missing content falls back to
//             whatever world generation and load_package_config substitute
//             (built-in eras, header-default config, no facilities). The
//             substitution is recorded in the report so a caller can print it.
//   strict  — for anything that claims to exercise the production economy.
//             Missing package files, catalogues that load empty, unparsable
//             or empty config files, config files lacking a required section
//             or critical key, undeclared world-override hooks and a generated
//             world with zero facilities throw BootstrapError instead of being
//             substituted. Not covered: a leaf config key outside the required
//             list still falls back to its header default (see
//             required_config_keys() in the .cpp and review N10).

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_orchestrator.h"
#include "core/world_gen/world_generator.h"
#include "core/world_state/world_state.h"

namespace econlife {

// Thrown by create_game_session() under ContentPolicy::strict.
struct BootstrapError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

enum class ContentPolicy : uint8_t {
    lenient,  // game default: record substitutions, do not fail
    strict,   // production-economy tests: fail on any missing/empty mandatory input
};

// Every input a base-game session reads. Empty string = not found.
struct BaseGamePaths {
    std::string package_root;        // packages/base_game
    std::string goods_dir;           // goods/
    std::string recipes_dir;         // recipes/
    std::string facility_types_csv;  // facility_types/facility_types.csv
    std::string technology_dir;      // technology/
    std::string eras_dir;            // eras/
    std::string occupations_dir;     // occupations/
    std::string config_dir;          // config/
    std::string scene_cards_dir;     // scene_cards/
};

// The JSON files load_package_config() reads. A file not listed here is not read
// by the engine at all (simulation_config.json is the known case, review N10).
const std::vector<std::string>& mandatory_config_files();

// Locate packages/base_game by searching "", "../", "../../", "../../../"
// relative to the working directory — the search the CLI has always used.
// Returns "" when not found.
std::string find_base_game_root();

// Resolve each content path under `package_root`, keeping only those that
// exist. An empty root yields all-empty paths.
BaseGamePaths resolve_base_game_paths(const std::string& package_root);

// What a session is asked to be. Only seed, sizes and the explicit hooks below
// may differ from the game; everything else comes from the package.
struct GameSetup {
    uint64_t seed = 42;
    uint32_t npc_count = 2000;
    uint32_t province_count = 6;
    uint8_t max_good_tier = 1;

    // Package location. Empty = find_base_game_root().
    std::string package_root;
    // Per-directory overrides (CLI --goods-dir / --config-dir). Empty = from root.
    std::string goods_dir_override;
    std::string config_dir_override;

    ContentPolicy policy = ContentPolicy::lenient;

    // DECLARED scenario overrides. A test that needs a world the game would not
    // generate (a forced regime, a higher criminal baseline) says so here, and
    // the hook names are recorded in the report. Nothing else may change.
    std::function<void(WorldGeneratorConfig&)> override_world_config;
    std::function<void(WorldState&)> override_generated_world;
    std::vector<std::string> declared_overrides;  // human-readable, for the report
};

// One line per PackageConfig field whose shipped JSON value differs from the
// compiled header default. Surfaced, never corrected (EL-SLICE-01 §3.C).
struct ConfigDivergence {
    std::string key;
    double header_default = 0.0;
    double effective = 0.0;
};

// What initialisation actually produced. Counts come from the generated world,
// not from the request.
struct BootstrapReport {
    uint64_t seed = 0;
    uint32_t requested_npcs = 0;
    uint32_t requested_provinces = 0;
    ContentPolicy policy = ContentPolicy::lenient;
    BaseGamePaths paths;

    // Configuration provenance.
    bool config_from_package = false;                // every mandatory JSON present
    std::vector<std::string> config_files_missing;   // header defaults used for these
    std::vector<std::string> content_substitutions;  // what lenient mode fell back on
    std::vector<std::string> declared_overrides;
    float inaction_threshold_effective = 0.0f;
    float inaction_threshold_header_default = 0.0f;
    std::vector<ConfigDivergence> config_divergences;

    // Catalogues as loaded.
    std::size_t goods_loaded = 0;
    std::size_t recipes_loaded = 0;
    std::size_t facility_types_loaded = 0;
    std::size_t technology_nodes_loaded = 0;
    std::size_t eras_loaded = 0;
    std::size_t occupations_loaded = 0;
    std::size_t scene_cards_loaded = 0;

    // World as instantiated.
    std::size_t provinces = 0;
    std::size_t npcs = 0;
    std::size_t businesses = 0;
    std::size_t facilities = 0;
    std::size_t markets = 0;
    std::size_t modules_registered = 0;
};

// A ready-to-tick session. Heap-allocated and non-movable because the
// orchestrator keeps a pointer to `config` (TickOrchestrator::set_config).
struct GameSession {
    PackageConfig config;
    WorldState world;
    TickOrchestrator orchestrator;
    BootstrapReport report;

    GameSession() = default;
    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;
};

// Generate the world, load the package config, register the base-game modules
// with it, finalize. Under ContentPolicy::strict throws BootstrapError (or the
// JSON library's parse/type error, rethrown as BootstrapError with the file name)
// on any missing mandatory input or a world with zero production facilities.
std::unique_ptr<GameSession> create_game_session(const GameSetup& setup);

// Multi-line, human-readable rendering of a report (for CLI diagnostics and test
// logs).
std::string format_bootstrap_report(const BootstrapReport& report);

}  // namespace econlife
