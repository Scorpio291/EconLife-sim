#include "modules/game_bootstrap.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <utility>

#include "core/world_gen/era_catalog.h"
#include "core/world_gen/facility_type_catalog.h"
#include "core/world_gen/goods_catalog.h"
#include "core/world_gen/occupation_catalog.h"
#include "core/world_gen/recipe_catalog.h"
#include "core/world_gen/technology_catalog.h"
#include "core/world_state/player.h"
#include "modules/register_base_game_modules.h"

namespace econlife {

namespace fs = std::filesystem;

namespace {

std::string existing_or_empty(const fs::path& p) {
    std::error_code ec;
    if (fs::exists(p, ec))
        return fs::canonical(p, ec).string();
    return "";
}

bool directory_has_csv(const std::string& dir) {
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec))
        return false;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (e.is_regular_file() && e.path().extension() == ".csv")
            return true;
    }
    return false;
}

[[noreturn]] void fail(const std::string& what) {
    throw BootstrapError("EL-SLICE-01 bootstrap contract violated: " + what);
}

// Strict pre-flight. Every check runs the SAME loader world generation runs, so
// a failure here is exactly the input world generation would have silently
// replaced. Nothing is cached from these loads: world generation reloads.
void validate_strict_inputs(const BaseGamePaths& p) {
    if (p.package_root.empty())
        fail("package root packages/base_game not found from the working directory");

    struct Required {
        const char* name;
        const std::string* path;
    };
    const Required required[] = {
        {"goods/", &p.goods_dir},
        {"recipes/", &p.recipes_dir},
        {"facility_types/facility_types.csv", &p.facility_types_csv},
        {"technology/", &p.technology_dir},
        {"eras/", &p.eras_dir},
        {"occupations/", &p.occupations_dir},
        {"config/", &p.config_dir},
        {"scene_cards/", &p.scene_cards_dir},
    };
    for (const auto& r : required) {
        if (r.path->empty())
            fail(std::string("missing required package input ") + r.name + " under " +
                 p.package_root);
    }

    {
        GoodsCatalog goods;
        if (!directory_has_csv(p.goods_dir) || !goods.load_from_directory(p.goods_dir) ||
            goods.size() == 0)
            fail("goods catalogue at " + p.goods_dir + " loaded 0 goods");
    }
    {
        RecipeCatalog recipes;
        if (!directory_has_csv(p.recipes_dir) || !recipes.load_from_directory(p.recipes_dir) ||
            recipes.size() == 0)
            fail("recipe catalogue at " + p.recipes_dir + " loaded 0 recipes");
    }
    {
        FacilityTypeCatalog types;
        if (!types.load_from_csv(p.facility_types_csv) || types.size() == 0)
            fail("facility-type catalogue " + p.facility_types_csv + " loaded 0 types");
    }
    {
        TechnologyCatalog tech;
        const std::string nodes = p.technology_dir + "/technology_nodes.csv";
        const std::string ceilings = p.technology_dir + "/maturation_ceilings.csv";
        if (!tech.load_nodes_csv(nodes) || tech.all().empty())
            fail("technology nodes " + nodes + " loaded 0 nodes");
        if (!tech.load_ceilings_csv(ceilings))
            fail("maturation ceilings " + ceilings + " could not be loaded");
    }
    {
        EraCatalog eras;
        if (!eras.load_from_directory(p.eras_dir) || eras.size() == 0)
            fail("era catalogue at " + p.eras_dir + " loaded 0 eras (built-in default refused)");
    }
    {
        OccupationCatalog occ;
        if (!occ.load_from_directory(p.occupations_dir) || occ.size() == 0)
            fail("occupation catalogue at " + p.occupations_dir +
                 " loaded 0 occupations (built-in default refused)");
    }
    if (!directory_has_csv(p.scene_cards_dir))
        fail("scene card catalogue at " + p.scene_cards_dir + " has no CSV");

    // Config: every file load_package_config reads must exist and parse.
    // load_package_config itself substitutes {} for a missing file, and a parse
    // error from it does not name the file — so check both here, with the name.
    for (const auto& file : mandatory_config_files()) {
        const fs::path path = fs::path(p.config_dir) / file;
        std::ifstream in(path);
        if (!in.is_open())
            fail("missing required config file " + path.string() +
                 " (header defaults would be substituted)");
        try {
            const nlohmann::json parsed = nlohmann::json::parse(in);
            (void)parsed;
        } catch (const std::exception& e) {
            fail("config file " + path.string() + " does not parse: " + e.what());
        }
    }
}

// Curated list of PackageConfig fields known to differ between the compiled
// header and the shipped JSON (review 2026-11-14, N2/N10). Not exhaustive:
// PackageConfig has no reflection, so a full diff needs the key map that N10
// asks for. Reported, never corrected.
std::vector<ConfigDivergence> diff_known_fields(const PackageConfig& header,
                                                const PackageConfig& eff) {
    std::vector<ConfigDivergence> out;
    auto add = [&](const char* key, double h, double e) {
        if (h != e)
            out.push_back({key, h, e});
    };
    add("modules.json:npc_behavior_module.inaction_threshold",
        header.npc_behavior_module.inaction_threshold, eff.npc_behavior_module.inaction_threshold);
    add("modules.json:npc_spending.default_base_demand_units",
        header.npc_spending.default_base_demand_units, eff.npc_spending.default_base_demand_units);
    add("modules.json:npc_spending.default_base_price", header.npc_spending.default_base_price,
        eff.npc_spending.default_base_price);
    add("economy.json:supply_chain.surplus_decay_rate_per_tick",
        header.supply_chain.surplus_decay_rate, eff.supply_chain.surplus_decay_rate);
    return out;
}

}  // namespace

const std::vector<std::string>& mandatory_config_files() {
    // Mirrors the load_json() calls in core/config/package_config.cpp.
    static const std::vector<std::string> files = {
        "economy.json",
        "npc.json",
        "production_config.json",
        "rnd_config.json",
        "consequence_delays.json",
        "modules.json",
        "business_lifecycle.json",
    };
    return files;
}

std::string find_base_game_root() {
    static const char* prefixes[] = {
        "packages/base_game",
        "../packages/base_game",
        "../../packages/base_game",
        "../../../packages/base_game",
    };
    for (const auto* prefix : prefixes) {
        std::error_code ec;
        if (fs::is_directory(prefix, ec))
            return fs::canonical(prefix, ec).string();
    }
    return "";
}

BaseGamePaths resolve_base_game_paths(const std::string& package_root) {
    BaseGamePaths p;
    if (package_root.empty())
        return p;
    const fs::path root(package_root);
    p.package_root = existing_or_empty(root);
    if (p.package_root.empty())
        return p;
    p.goods_dir = existing_or_empty(root / "goods");
    p.recipes_dir = existing_or_empty(root / "recipes");
    p.facility_types_csv = existing_or_empty(root / "facility_types" / "facility_types.csv");
    p.technology_dir = existing_or_empty(root / "technology");
    p.eras_dir = existing_or_empty(root / "eras");
    p.occupations_dir = existing_or_empty(root / "occupations");
    p.config_dir = existing_or_empty(root / "config");
    p.scene_cards_dir = existing_or_empty(root / "scene_cards");
    return p;
}

std::unique_ptr<GameSession> create_game_session(const GameSetup& setup) {
    const bool strict = setup.policy == ContentPolicy::strict;

    BaseGamePaths paths = resolve_base_game_paths(setup.package_root.empty() ? find_base_game_root()
                                                                             : setup.package_root);
    if (!setup.goods_dir_override.empty())
        paths.goods_dir = setup.goods_dir_override;
    if (!setup.config_dir_override.empty())
        paths.config_dir = setup.config_dir_override;

    if (strict)
        validate_strict_inputs(paths);

    auto session = std::make_unique<GameSession>();
    BootstrapReport& rep = session->report;
    rep.seed = setup.seed;
    rep.requested_npcs = setup.npc_count;
    rep.requested_provinces = setup.province_count;
    rep.policy = setup.policy;
    rep.paths = paths;
    rep.declared_overrides = setup.declared_overrides;

    // ── 1. World ────────────────────────────────────────────────────────────
    WorldGeneratorConfig gen{};
    gen.seed = setup.seed;
    gen.province_count = setup.province_count;
    gen.npc_count = setup.npc_count;
    gen.max_good_tier = setup.max_good_tier;
    gen.goods_directory = paths.goods_dir;
    gen.recipes_directory = paths.recipes_dir;
    gen.facility_types_filepath = paths.facility_types_csv;
    gen.technology_directory = paths.technology_dir;
    gen.eras_directory = paths.eras_dir;
    gen.occupations_directory = paths.occupations_dir;
    if (setup.override_world_config)
        setup.override_world_config(gen);

    {
        auto result = WorldGenerator::generate_with_player(gen);
        session->world = std::move(result.world);
        session->world.player = std::make_unique<PlayerCharacter>(std::move(result.player));
    }
    if (setup.override_generated_world)
        setup.override_generated_world(session->world);

    // ── 2. Package config ───────────────────────────────────────────────────
    // load_package_config("") reads "/economy.json" etc. and silently returns
    // header defaults — that is the CLI's existing "not found (using spec
    // defaults)" behaviour, kept in lenient mode and recorded below.
    try {
        session->config = load_package_config(paths.config_dir);
    } catch (const std::exception& e) {
        if (strict)
            fail(std::string("load_package_config(") + paths.config_dir + ") threw: " + e.what());
        throw;
    }
    session->config.scene_cards.card_catalog_directory = paths.scene_cards_dir;

    // ── 3. Modules, with that config ────────────────────────────────────────
    register_base_game_modules(session->orchestrator, session->config);
    session->orchestrator.set_config(session->config);
    session->orchestrator.finalize_registration();

    // ── 4. Report ───────────────────────────────────────────────────────────
    const WorldState& w = session->world;
    for (const auto& file : mandatory_config_files()) {
        std::error_code ec;
        if (paths.config_dir.empty() || !fs::exists(fs::path(paths.config_dir) / file, ec))
            rep.config_files_missing.push_back(file);
    }
    rep.config_from_package = rep.config_files_missing.empty();
    const PackageConfig header_defaults{};
    rep.inaction_threshold_header_default = header_defaults.npc_behavior_module.inaction_threshold;
    rep.inaction_threshold_effective = session->config.npc_behavior_module.inaction_threshold;
    rep.config_divergences = diff_known_fields(header_defaults, session->config);

    rep.goods_loaded = w.goods_catalog ? w.goods_catalog->size() : 0;
    rep.recipes_loaded = w.loaded_recipes.size();
    if (!paths.facility_types_csv.empty()) {
        FacilityTypeCatalog types;
        if (types.load_from_csv(paths.facility_types_csv))
            rep.facility_types_loaded = types.size();
    }
    rep.technology_nodes_loaded = w.technology_catalog ? w.technology_catalog->all().size() : 0;
    rep.eras_loaded = w.era_catalog.size();
    rep.occupations_loaded = w.occupation_catalog.size();
    rep.provinces = w.provinces.size();
    rep.npcs = w.significant_npcs.size();
    rep.businesses = w.npc_businesses.size();
    rep.facilities = w.facilities.size();
    rep.markets = w.regional_markets.size();
    rep.modules_registered = session->orchestrator.modules().size();

    auto note_missing = [&](const std::string& path, const char* what) {
        if (path.empty())
            rep.content_substitutions.push_back(what);
    };
    note_missing(paths.goods_dir, "goods: none (no markets for catalogue goods)");
    note_missing(paths.recipes_dir, "recipes: none (no production recipes)");
    note_missing(paths.facility_types_csv, "facility types: none (world-gen creates 0 facilities)");
    note_missing(paths.technology_dir, "technology: empty catalogue");
    note_missing(paths.eras_dir, "eras: built-in default");
    note_missing(paths.occupations_dir, "occupations: built-in default");
    note_missing(paths.scene_cards_dir, "scene cards: none (cards are not raised)");
    for (const auto& f : rep.config_files_missing)
        rep.content_substitutions.push_back("config " + f + ": header defaults");

    // ── 5. Post-generation contract ─────────────────────────────────────────
    if (strict) {
        if (rep.recipes_loaded == 0)
            fail("generated world holds 0 recipes");
        if (rep.facilities == 0)
            fail("generated world holds 0 production facilities (seed " +
                 std::to_string(setup.seed) + ", " + std::to_string(setup.province_count) +
                 " provinces) — report the world-generation defect, do not inject facilities");
    }
    return session;
}

std::string format_bootstrap_report(const BootstrapReport& r) {
    std::ostringstream o;
    o << "bootstrap: seed=" << r.seed
      << " policy=" << (r.policy == ContentPolicy::strict ? "strict" : "lenient")
      << " requested npcs=" << r.requested_npcs << " provinces=" << r.requested_provinces << "\n";
    o << "  package root: " << (r.paths.package_root.empty() ? "(not found)" : r.paths.package_root)
      << "\n";
    o << "  config: " << (r.paths.config_dir.empty() ? "(none)" : r.paths.config_dir)
      << (r.config_from_package ? " [all package JSON present]" : " [HEADER DEFAULTS USED]")
      << "\n";
    o << "  npc_behavior_module.inaction_threshold: effective=" << r.inaction_threshold_effective
      << " header_default=" << r.inaction_threshold_header_default << "\n";
    for (const auto& d : r.config_divergences)
        o << "  config divergence (header vs package): " << d.key << " header=" << d.header_default
          << " package=" << d.effective << "\n";
    for (const auto& s : r.content_substitutions)
        o << "  SUBSTITUTED: " << s << "\n";
    for (const auto& s : r.declared_overrides)
        o << "  declared scenario override: " << s << "\n";
    o << "  catalogues: goods=" << r.goods_loaded << " recipes=" << r.recipes_loaded
      << " facility_types=" << r.facility_types_loaded
      << " tech_nodes=" << r.technology_nodes_loaded << " eras=" << r.eras_loaded
      << " occupations=" << r.occupations_loaded << "\n";
    o << "  world: provinces=" << r.provinces << " npcs=" << r.npcs
      << " businesses=" << r.businesses << " facilities=" << r.facilities
      << " markets=" << r.markets << " modules=" << r.modules_registered << "\n";
    return o.str();
}

}  // namespace econlife
