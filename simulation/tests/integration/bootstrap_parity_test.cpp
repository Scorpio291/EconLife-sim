// EL-SLICE-01 — one honest simulation baseline.
//
// Proves that the behavioural-test bootstrap builds the world the game builds,
// that it refuses to build anything else silently, and that the module config
// it hands the modules is the shipped package config.
//
//   AC-01 parity        the emergence harness setup and the game (CLI) setup
//                       produce bit-identical initial state, and stay identical
//                       through the first ticks (same module parameters).
//   AC-02 completeness  a production-economy world has recipes, facility types
//                       and instantiated facilities.
//   AC-03 fail-fast     a missing recipe directory, a missing facility-type file,
//                       a missing/unparsable/mistyped config file or a world that
//                       generates zero facilities throws BootstrapError.
//   AC-04 fidelity      inaction_threshold is the package value; editing the
//                       package changes the module's behaviour with no test edit.
//   AC-05 determinism   same seed + config -> same initial state on repeat.
//
// These run in the fast gate (label "integration"). They assert the INFRASTRUCTURE,
// not the economy's health: whether the game's world behaves well is the
// emergence suite's question, and it may legitimately answer "no".

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

#include "core/tick/thread_pool.h"
#include "modules/game_bootstrap.h"
#include "tests/integration/emergence_harness.h"
#include "tests/integration/world_fingerprint.h"

using namespace econlife;
namespace fs = std::filesystem;

namespace {

constexpr uint64_t kSeed = 42;
constexpr uint32_t kNpcs = 120;
constexpr uint32_t kProvinces = 6;

// The game's session exactly as econlife_cli builds it (lenient policy, CLI
// default max_good_tier), with only seed and sizes set.
std::unique_ptr<GameSession> game_session(uint64_t seed, uint32_t npcs, uint32_t provinces,
                                          const std::string& root = "") {
    GameSetup setup{};
    setup.seed = seed;
    setup.npc_count = npcs;
    setup.province_count = provinces;
    setup.package_root = root;
    setup.policy = ContentPolicy::lenient;
    return create_game_session(setup);
}

// The behavioural suite's session exactly as emergence_harness.h builds it.
std::unique_ptr<GameSession> harness_session(uint64_t seed, uint32_t npcs, uint32_t provinces,
                                             const std::string& root = "") {
    emergence::RunSpec spec;
    spec.seed = seed;
    spec.npc_count = npcs;
    spec.province_count = provinces;
    spec.package_root = root;
    return create_game_session(emergence::make_setup(spec));
}

void tick(GameSession& s, uint32_t n) {
    ThreadPool pool(1);
    for (uint32_t i = 0; i < n; ++i)
        s.orchestrator.execute_tick(s.world, pool);
}

// A disposable copy of packages/base_game that a test may break.
struct PackageCopy {
    fs::path root;
    explicit PackageCopy(const std::string& tag) {
        const std::string src = find_base_game_root();
        REQUIRE_FALSE(src.empty());
        root = fs::temp_directory_path() / ("econlife_el_slice_01_" + tag) / "base_game";
        fs::remove_all(root.parent_path());
        fs::create_directories(root.parent_path());
        fs::copy(src, root, fs::copy_options::recursive);
    }
    ~PackageCopy() {
        std::error_code ec;
        fs::remove_all(root.parent_path(), ec);
    }
    std::string path() const { return root.string(); }
    void write(const std::string& rel, const std::string& content) const {
        std::ofstream(root / rel, std::ios::trunc) << content;
    }
    void set_inaction_threshold(double v) const {
        const fs::path p = root / "config" / "modules.json";
        nlohmann::json j;
        {
            std::ifstream in(p);
            j = nlohmann::json::parse(in);
        }
        j["npc_behavior_module"]["inaction_threshold"] = v;
        std::ofstream(p, std::ios::trunc) << j.dump(2);
    }
};

// Strict bootstrap of `root` must throw BootstrapError whose message names `why`.
void expect_bootstrap_error(const std::string& root, const std::string& why) {
    try {
        harness_session(kSeed, kNpcs, kProvinces, root);
    } catch (const BootstrapError& e) {
        const std::string what = e.what();
        INFO("BootstrapError: " << what);
        CHECK(what.find(why) != std::string::npos);
        return;
    }
    FAIL("expected BootstrapError naming '" << why << "', bootstrap succeeded");
}

double shipped_inaction_threshold() {
    std::ifstream in(fs::path(find_base_game_root()) / "config" / "modules.json");
    const auto j = nlohmann::json::parse(in);
    return j.at("npc_behavior_module").at("inaction_threshold").get<double>();
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// AC-01 — production configuration parity
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("EL-SLICE-01 AC-01: harness bootstrap builds the game's initial world",
          "[integration][bootstrap][el_slice_01]") {
    auto game = game_session(kSeed, kNpcs, kProvinces);
    auto harness = harness_session(kSeed, kNpcs, kProvinces);

    const auto fg = fingerprint::take(game->world);
    const auto fh = fingerprint::take(harness->world);
    INFO("game:    " << fg.describe());
    INFO("harness: " << fh.describe());
    REQUIRE(fg == fh);

    // Same paths, same module set.
    CHECK(game->report.paths.recipes_dir == harness->report.paths.recipes_dir);
    CHECK(game->report.paths.config_dir == harness->report.paths.config_dir);
    CHECK(game->report.modules_registered == harness->report.modules_registered);
    CHECK(game->report.config_files_missing.empty());
    CHECK(harness->report.config_files_missing.empty());
    CHECK(harness->report.declared_overrides.empty());
}

TEST_CASE("EL-SLICE-01 AC-01: harness and game stay identical through the first ticks",
          "[integration][bootstrap][el_slice_01]") {
    // Equal initial state is necessary but not sufficient: if the modules were
    // registered with different parameters, the worlds diverge on tick 1.
    auto game = game_session(kSeed, kNpcs, kProvinces);
    auto harness = harness_session(kSeed, kNpcs, kProvinces);
    for (uint32_t t = 1; t <= 5; ++t) {
        tick(*game, 1);
        tick(*harness, 1);
        const auto fg = fingerprint::take(game->world);
        const auto fh = fingerprint::take(harness->world);
        INFO("tick " << t << "\n game:    " << fg.describe() << "\n harness: " << fh.describe());
        REQUIRE(fg == fh);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// AC-02 — content completeness
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("EL-SLICE-01 AC-02: an economy test world has recipes and facilities",
          "[integration][bootstrap][el_slice_01]") {
    auto s = harness_session(kSeed, 500, kProvinces);
    const BootstrapReport& r = s->report;
    INFO(format_bootstrap_report(r));
    CHECK(r.goods_loaded > 0);
    CHECK(r.recipes_loaded > 0);
    CHECK(r.facility_types_loaded > 0);
    CHECK(r.technology_nodes_loaded > 0);
    CHECK(r.eras_loaded > 0);
    CHECK(r.occupations_loaded > 0);
    CHECK(r.content_substitutions.empty());
    REQUIRE(r.facilities > 0);
    REQUIRE(s->world.facilities.size() == r.facilities);
    // Every facility runs a recipe that is in the loaded catalogue.
    for (const auto& f : s->world.facilities) {
        bool found = false;
        for (const auto& rc : s->world.loaded_recipes)
            if (rc.id == f.recipe_id) {
                found = true;
                break;
            }
        INFO("facility " << f.id << " recipe " << f.recipe_id);
        CHECK(found);
    }
    CHECK(!s->config.scene_cards.card_catalog_directory.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// AC-03 — fail-fast behaviour
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("EL-SLICE-01 AC-03: missing recipe directory fails the strict bootstrap",
          "[integration][bootstrap][el_slice_01]") {
    PackageCopy pkg("no_recipes");
    fs::remove_all(pkg.root / "recipes");
    expect_bootstrap_error(pkg.path(), "recipes/");

    // The game's lenient path does not throw — and says what it substituted.
    auto lenient = game_session(kSeed, kNpcs, kProvinces, pkg.path());
    CHECK(lenient->report.recipes_loaded == 0);
    CHECK(lenient->report.facilities == 0);
    CHECK_FALSE(lenient->report.content_substitutions.empty());
}

TEST_CASE("EL-SLICE-01 AC-03: empty recipe directory fails the strict bootstrap",
          "[integration][bootstrap][el_slice_01]") {
    PackageCopy pkg("empty_recipes");
    fs::remove_all(pkg.root / "recipes");
    fs::create_directories(pkg.root / "recipes");
    expect_bootstrap_error(pkg.path(), "loaded 0 recipes");
}

TEST_CASE("EL-SLICE-01 AC-03: missing facility-type file fails the strict bootstrap",
          "[integration][bootstrap][el_slice_01]") {
    PackageCopy pkg("no_facility_types");
    fs::remove(pkg.root / "facility_types" / "facility_types.csv");
    expect_bootstrap_error(pkg.path(), "facility_types.csv");
}

TEST_CASE("EL-SLICE-01 AC-03: missing config file fails instead of using header defaults",
          "[integration][bootstrap][el_slice_01]") {
    PackageCopy pkg("no_modules_json");
    fs::remove(pkg.root / "config" / "modules.json");
    expect_bootstrap_error(pkg.path(), "missing required config file");
}

TEST_CASE("EL-SLICE-01 AC-03: unparsable config fails visibly",
          "[integration][bootstrap][el_slice_01]") {
    PackageCopy pkg("bad_json");
    pkg.write("config/modules.json", "{ \"npc_behavior_module\": { \"inaction_threshold\": ");
    expect_bootstrap_error(pkg.path(), "does not parse");
}

TEST_CASE("EL-SLICE-01 AC-03: a mistyped config value fails visibly",
          "[integration][bootstrap][el_slice_01]") {
    PackageCopy pkg("bad_type");
    {
        const fs::path p = pkg.root / "config" / "modules.json";
        nlohmann::json j;
        {
            std::ifstream in(p);
            j = nlohmann::json::parse(in);
        }
        j["npc_behavior_module"]["inaction_threshold"] = "high";
        std::ofstream(p, std::ios::trunc) << j.dump(2);
    }
    expect_bootstrap_error(pkg.path(), "load_package_config");
}

TEST_CASE("EL-SLICE-01 AC-03: a world that generates zero facilities fails its contract",
          "[integration][bootstrap][el_slice_01]") {
    // Facility types that no recipe uses: every catalogue loads, but world
    // generation can place no facility. The contract must catch the empty world.
    PackageCopy pkg("zero_facilities");
    pkg.write("facility_types/facility_types.csv",
              "facility_type_key,display_name,category,base_construction_cost,"
              "base_operating_cost,max_workers,signal_weight_noise,signal_weight_waste,"
              "signal_weight_traffic,signal_weight_pollution,signal_weight_odor\n"
              "el_slice_01_unused,Unused,processing,1000,10,5,0,0,0,0,0\n");
    try {
        harness_session(kSeed, kNpcs, kProvinces, pkg.path());
        FAIL("expected BootstrapError for a zero-facility world");
    } catch (const BootstrapError& e) {
        const std::string what = e.what();
        INFO(what);
        CHECK(what.find("0 production facilities") != std::string::npos);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// AC-04 — configuration fidelity
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("EL-SLICE-01 AC-04: inaction_threshold is the package value, divergence surfaced",
          "[integration][bootstrap][el_slice_01]") {
    auto s = harness_session(kSeed, kNpcs, kProvinces);
    const double shipped = shipped_inaction_threshold();
    CHECK(static_cast<double>(s->config.npc_behavior_module.inaction_threshold) ==
          static_cast<double>(static_cast<float>(shipped)));
    CHECK(s->report.inaction_threshold_effective ==
          s->config.npc_behavior_module.inaction_threshold);

    // The header default is NOT corrected to match; the report must name the gap
    // whenever it exists.
    const float header = PackageConfig{}.npc_behavior_module.inaction_threshold;
    bool reported = false;
    for (const auto& d : s->report.config_divergences)
        if (d.key.find("inaction_threshold") != std::string::npos)
            reported = true;
    CHECK(reported == (header != s->config.npc_behavior_module.inaction_threshold));
    WARN("inaction_threshold: package " << shipped << " vs header " << header);
}

TEST_CASE("EL-SLICE-01 AC-04: editing the package changes the running module, no test edit",
          "[integration][bootstrap][el_slice_01]") {
    // Two edited packages, nothing else changed. A threshold no action can clear
    // must make every evaluated NPC choose inaction on tick 1; one every action
    // clears must make none do so. If the npc_behavior module did not consume the
    // package value, the two runs would be identical.
    PackageCopy high("threshold_high");
    high.set_inaction_threshold(1.0e9);
    PackageCopy low("threshold_low");
    low.set_inaction_threshold(-1.0e9);

    auto s_high = harness_session(kSeed, kNpcs, kProvinces, high.path());
    auto s_low = harness_session(kSeed, kNpcs, kProvinces, low.path());
    REQUIRE(s_high->config.npc_behavior_module.inaction_threshold == 1.0e9f);
    REQUIRE(s_low->config.npc_behavior_module.inaction_threshold == -1.0e9f);
    REQUIRE(s_high->report.inaction_threshold_effective == 1.0e9f);

    // Identical worlds before the first tick: only module behaviour differs.
    REQUIRE(fingerprint::take(s_high->world) == fingerprint::take(s_low->world));

    tick(*s_high, 1);
    tick(*s_low, 1);
    auto count = [](const WorldState& w, NPCStatus st) {
        int n = 0;
        for (const auto& npc : w.significant_npcs)
            n += (npc.status == st) ? 1 : 0;
        return n;
    };
    const int npcs = static_cast<int>(s_high->world.significant_npcs.size());
    const int waiting_high = count(s_high->world, NPCStatus::waiting);
    const int waiting_low = count(s_low->world, NPCStatus::waiting);
    INFO("waiting after tick 1: threshold 1e9 -> " << waiting_high << ", threshold -1e9 -> "
                                                   << waiting_low << " (of " << npcs << ")");
    CHECK(waiting_low == 0);
    CHECK(waiting_high * 10 >= npcs * 9);
}

// ─────────────────────────────────────────────────────────────────────────────
// AC-05 — determinism
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("EL-SLICE-01 AC-05: same seed and config give the same initial state",
          "[integration][bootstrap][el_slice_01]") {
    auto a = harness_session(kSeed, kNpcs, kProvinces);
    auto b = harness_session(kSeed, kNpcs, kProvinces);
    REQUIRE(fingerprint::take(a->world) == fingerprint::take(b->world));
    tick(*a, 3);
    tick(*b, 3);
    REQUIRE(fingerprint::take(a->world) == fingerprint::take(b->world));

    // And a different seed is a different world (the fingerprint can tell).
    auto c = harness_session(kSeed + 1, kNpcs, kProvinces);
    CHECK_FALSE(fingerprint::take(a->world) == fingerprint::take(c->world));
}
