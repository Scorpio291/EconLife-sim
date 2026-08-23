// Technology catalog and module unit tests.

#include "core/world_gen/technology_catalog.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/shared_types.h"
#include "core/world_state/world_state.h"
#include "modules/economy/economy_types.h"
#include "modules/technology/technology_module.h"
#include "modules/technology/technology_types.h"

using namespace econlife;

// Helper: locate the shipped technology directory (the drift guard must not self-skip
// on a path problem, so it reports rather than silently passing).
static std::string find_base_game_technology_dir() {
    namespace fs = std::filesystem;
    for (const std::string prefix :
         {"packages/base_game/", "../packages/base_game/", "../../packages/base_game/",
          "../../../packages/base_game/", "../../../../packages/base_game/",
          "../../../../../packages/base_game/"}) {
        const std::string p = prefix + "technology";
        if (fs::exists(p + "/technology_nodes.csv"))
            return fs::canonical(p).string();
    }
    return "";
}

// Helper: write a temp CSV and return the path.
static std::string write_temp_csv(const std::string& filename, const std::string& content) {
    namespace fs = std::filesystem;
    // Per-process directory. ctest runs each TEST_CASE as its own process and -j6 runs
    // several at once, so a shared fixed path is a race: two cases writing the same
    // fixture raced and one read a half-written file. It passed alone and failed in the
    // suite, which is the signature.
    auto dir = fs::temp_directory_path() /
               ("econlife_tech_test_" + std::to_string(static_cast<long>(::getpid())));
    fs::create_directories(dir);
    auto path = dir / filename;
    std::ofstream out(path);
    out << content;
    return path.string();
}

// ===========================================================================
// TechnologyCatalog — node loading
// ===========================================================================

TEST_CASE("TechnologyCatalog loads nodes from CSV", "[technology][catalog]") {
    std::string csv =
        R"(node_key,domain,display_name,era_available,difficulty,patentable,prerequisites,outcome_type,key_technology_node,unlocks_recipe,unlocks_facility_type,is_baseline
basic_extraction,materials_science,Basic Extraction Methods,1,0.0,0,,baseline,,,mine,1
hydraulic_fracturing,energy_systems,Hydraulic Fracturing,1,1.0,1,,product_unlock,hydraulic_fracturing,shale_oil_extraction,,0
electric_vehicle,mechanical_engineering,Electric Vehicle,2,3.0,1,ev_powertrain,product_unlock,electric_vehicle,electric_vehicle,,0
)";
    auto path = write_temp_csv("test_nodes.csv", csv);

    TechnologyCatalog catalog;
    REQUIRE(catalog.load_nodes_csv(path));
    REQUIRE(catalog.all().size() == 3);

    SECTION("Find by key") {
        auto* node = catalog.find("hydraulic_fracturing");
        REQUIRE(node != nullptr);
        CHECK(node->domain == "energy_systems");
        CHECK(node->display_name == "Hydraulic Fracturing");
        CHECK(node->era_available == 1);
        CHECK_THAT(node->difficulty, Catch::Matchers::WithinAbs(1.0f, 0.001f));
        CHECK(node->patentable == true);
        CHECK(node->outcome_type == "product_unlock");
        CHECK(node->unlocks_recipe == "shale_oil_extraction");
    }

    SECTION("Baseline flag") {
        auto* baseline = catalog.find("basic_extraction");
        REQUIRE(baseline != nullptr);
        CHECK(baseline->is_baseline == true);
        CHECK(baseline->unlocks_facility_type == "mine");

        auto* non_baseline = catalog.find("hydraulic_fracturing");
        REQUIRE(non_baseline != nullptr);
        CHECK(non_baseline->is_baseline == false);
    }

    SECTION("Prerequisites parsing") {
        auto* ev = catalog.find("electric_vehicle");
        REQUIRE(ev != nullptr);
        REQUIRE(ev->prerequisites.size() == 1);
        CHECK(ev->prerequisites[0] == "ev_powertrain");
    }

    SECTION("Nodes available at era") {
        auto era1 = catalog.nodes_available_at(1);
        CHECK(era1.size() == 2);  // basic_extraction + hydraulic_fracturing

        auto era2 = catalog.nodes_available_at(2);
        CHECK(era2.size() == 3);  // all three
    }

    SECTION("Baseline nodes") {
        auto baseline = catalog.baseline_nodes();
        REQUIRE(baseline.size() == 1);
        CHECK(baseline[0]->node_key == "basic_extraction");
    }

    SECTION("Nodes in domain") {
        auto energy = catalog.nodes_in_domain("energy_systems");
        REQUIRE(energy.size() == 1);
        CHECK(energy[0]->node_key == "hydraulic_fracturing");
    }

    SECTION("Unknown key returns nullptr") {
        CHECK(catalog.find("nonexistent") == nullptr);
    }
}

TEST_CASE("TechnologyCatalog handles semicolon-separated prerequisites", "[technology][catalog]") {
    std::string csv =
        R"(node_key,domain,display_name,era_available,difficulty,patentable,prerequisites,outcome_type,key_technology_node,unlocks_recipe,unlocks_facility_type,is_baseline
autonomous_vehicle,mechanical_engineering,Autonomous Vehicle,4,5.0,1,machine_learning_commercial;electric_vehicle,product_unlock,autonomous_vehicle,,,0
)";
    auto path = write_temp_csv("test_prereqs.csv", csv);

    TechnologyCatalog catalog;
    REQUIRE(catalog.load_nodes_csv(path));
    auto* node = catalog.find("autonomous_vehicle");
    REQUIRE(node != nullptr);
    REQUIRE(node->prerequisites.size() == 2);
    CHECK(node->prerequisites[0] == "machine_learning_commercial");
    CHECK(node->prerequisites[1] == "electric_vehicle");
}

TEST_CASE("TechnologyCatalog handles empty/malformed CSV gracefully", "[technology][catalog]") {
    SECTION("Empty file") {
        auto path = write_temp_csv("empty_nodes.csv", "");
        TechnologyCatalog catalog;
        CHECK_FALSE(catalog.load_nodes_csv(path));
        CHECK(catalog.all().empty());
    }

    SECTION("Header only") {
        auto path =
            write_temp_csv("header_only.csv",
                           "node_key,domain,display_name,era_available,difficulty,patentable,"
                           "prerequisites,outcome_type,key_technology_node,unlocks_recipe,"
                           "unlocks_facility_type,is_baseline\n");
        TechnologyCatalog catalog;
        CHECK_FALSE(catalog.load_nodes_csv(path));
    }

    SECTION("Comments only") {
        auto path = write_temp_csv("comments_only.csv",
                                   "# This is a comment\n"
                                   "# Another comment\n");
        TechnologyCatalog catalog;
        CHECK_FALSE(catalog.load_nodes_csv(path));
    }
}

// ===========================================================================
// TechnologyCatalog — ceiling loading
// ===========================================================================

TEST_CASE("TechnologyCatalog loads maturation ceilings", "[technology][catalog]") {
    std::string csv = R"(node_key,era_1,era_2,era_3,era_4,era_5,era_6,era_7,era_8,era_9,era_10
electric_vehicle,-1.0,0.10,0.35,0.55,0.75,0.90,1.0,1.0,1.0,1.0
basic_extraction,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0
)";
    auto path = write_temp_csv("test_ceilings.csv", csv);

    TechnologyCatalog catalog;
    REQUIRE(catalog.load_ceilings_csv(path));

    SECTION("Ceiling for existing node") {
        CHECK_THAT(catalog.ceiling_for("electric_vehicle", 1),
                   Catch::Matchers::WithinAbs(-1.0f, 0.001f));
        CHECK_THAT(catalog.ceiling_for("electric_vehicle", 2),
                   Catch::Matchers::WithinAbs(0.10f, 0.001f));
        CHECK_THAT(catalog.ceiling_for("electric_vehicle", 5),
                   Catch::Matchers::WithinAbs(0.75f, 0.001f));
    }

    SECTION("Baseline tech has full ceiling") {
        CHECK_THAT(catalog.ceiling_for("basic_extraction", 1),
                   Catch::Matchers::WithinAbs(1.0f, 0.001f));
    }

    SECTION("Unknown node returns 1.0 (no restriction)") {
        CHECK_THAT(catalog.ceiling_for("unknown_tech", 1),
                   Catch::Matchers::WithinAbs(1.0f, 0.001f));
    }

    SECTION("Invalid era returns -1.0") {
        CHECK_THAT(catalog.ceiling_for("electric_vehicle", 0),
                   Catch::Matchers::WithinAbs(-1.0f, 0.001f));
        CHECK_THAT(catalog.ceiling_for("electric_vehicle", 11),
                   Catch::Matchers::WithinAbs(-1.0f, 0.001f));
    }
}

// ===========================================================================
// ActorTechnologyState — query methods
// ===========================================================================

TEST_CASE("ActorTechnologyState query methods", "[technology][state]") {
    ActorTechnologyState state;
    state.effective_tech_tier = 2.0f;

    SECTION("Empty state returns defaults") {
        CHECK_FALSE(state.has_researched("electric_vehicle"));
        CHECK_FALSE(state.has_commercialized("electric_vehicle"));
        CHECK_THAT(state.maturation_of("electric_vehicle"),
                   Catch::Matchers::WithinAbs(0.0f, 0.001f));
    }

    SECTION("Researched holding") {
        TechHolding holding;
        holding.node_key = "electric_vehicle";
        holding.holder_id = 1;
        holding.stage = TechStage::researched;
        holding.maturation_level = 0.35f;
        state.holdings["electric_vehicle"] = holding;

        CHECK(state.has_researched("electric_vehicle"));
        CHECK_FALSE(state.has_commercialized("electric_vehicle"));
        CHECK_THAT(state.maturation_of("electric_vehicle"),
                   Catch::Matchers::WithinAbs(0.35f, 0.001f));
    }

    SECTION("Commercialized holding") {
        TechHolding holding;
        holding.node_key = "basic_extraction";
        holding.holder_id = 1;
        holding.stage = TechStage::commercialized;
        holding.maturation_level = 0.90f;
        state.holdings["basic_extraction"] = holding;

        CHECK(state.has_researched("basic_extraction"));
        CHECK(state.has_commercialized("basic_extraction"));
        CHECK_THAT(state.maturation_of("basic_extraction"),
                   Catch::Matchers::WithinAbs(0.90f, 0.001f));
    }
}

// ===========================================================================
// ActorTechnologyState — backward compatibility
// ===========================================================================

TEST_CASE("ActorTechnologyState aggregate init backward compat", "[technology][state]") {
    // Aggregate init must let callers set effective_tech_tier without
    // also specifying every holding. Use {tier, {}} to make the empty-map
    // intent explicit (silences -Wmissing-field-initializers).
    ActorTechnologyState state{1.0f, {}};
    CHECK_THAT(state.effective_tech_tier, Catch::Matchers::WithinAbs(1.0f, 0.001f));
    CHECK(state.holdings.empty());
}

// ===========================================================================
// GlobalTechnologyState
// ===========================================================================

TEST_CASE("GlobalTechnologyState defaults", "[technology][state]") {
    GlobalTechnologyState gts;
    // The piecemeal-construction fallback must be the MODERN anchor, i.e. the
    // is_default_entry row of eras.csv (era 8, turn_of_millennium). It read 5
    // after the era re-base, which is feudal Medieval — a piecemeal WorldState
    // silently came up in a pre-market regime.
    CHECK(gts.current_era == kDefaultEntryEra);
    CHECK(kDefaultEntryEra == 8);
    CHECK(gts.era_started_tick == 0);
    CHECK(gts.base_year == 2000);
    CHECK(gts.active_research_projects.empty());
    CHECK(gts.active_maturation_projects.empty());
}

// ===========================================================================
// TechnologyConfig defaults
// ===========================================================================

TEST_CASE("TechnologyConfig defaults match spec", "[technology][config]") {
    TechnologyConfig config;
    CHECK_THAT(config.maturation_rate_coeff, Catch::Matchers::WithinAbs(0.40f, 0.001f));
    CHECK_THAT(config.maturation_difficulty_per_level, Catch::Matchers::WithinAbs(2.0f, 0.001f));
    CHECK_THAT(config.base_research_success_rate, Catch::Matchers::WithinAbs(0.75f, 0.001f));
    CHECK_THAT(config.era_transition_threshold, Catch::Matchers::WithinAbs(0.70f, 0.001f));
    CHECK_THAT(config.maturation_transfer_license, Catch::Matchers::WithinAbs(0.80f, 0.001f));
    CHECK_THAT(config.maturation_transfer_reverse_eng, Catch::Matchers::WithinAbs(0.50f, 0.001f));
    CHECK(config.patent_duration_ticks == 7300);
}

// ===========================================================================
// Base game data file loading
// ===========================================================================

TEST_CASE("Load base game technology_nodes.csv", "[technology][catalog][data]") {
    // Find base_game path by searching upward.
    namespace fs = std::filesystem;
    std::string nodes_path;
    const char* prefixes[] = {
        "packages/base_game/technology/technology_nodes.csv",
        "../packages/base_game/technology/technology_nodes.csv",
        "../../packages/base_game/technology/technology_nodes.csv",
        "../../../packages/base_game/technology/technology_nodes.csv",
    };
    for (const auto* prefix : prefixes) {
        if (fs::exists(prefix)) {
            nodes_path = fs::canonical(prefix).string();
            break;
        }
    }
    if (nodes_path.empty()) {
        WARN("Could not find technology_nodes.csv — skipping data test");
        return;
    }

    TechnologyCatalog catalog;
    REQUIRE(catalog.load_nodes_csv(nodes_path));

    // Should have a reasonable number of nodes.
    CHECK(catalog.all().size() >= 50);

    // Should have baseline nodes.
    auto baseline = catalog.baseline_nodes();
    CHECK(baseline.size() >= 10);

    // Baseline nodes are available from the modern anchor (era 5) under the re-based
    // timeline. (Placing primitive baseline tech at the dawn is later content work.)
    for (const auto* node : baseline) {
        CHECK(node->era_available == 8);
        CHECK(node->is_baseline == true);
    }

    // Should have nodes across multiple domains.
    auto energy = catalog.nodes_in_domain("energy_systems");
    CHECK(energy.size() >= 5);

    auto semi = catalog.nodes_in_domain("semiconductor_physics");
    CHECK(semi.size() >= 5);
}

TEST_CASE("Historical tech tree: nodes per era 1-7 and every prerequisite resolves",
          "[technology][catalog][data]") {
    namespace fs = std::filesystem;
    std::string nodes_path;
    for (const char* p : {"packages/base_game/technology/technology_nodes.csv",
                          "../packages/base_game/technology/technology_nodes.csv",
                          "../../packages/base_game/technology/technology_nodes.csv",
                          "../../../packages/base_game/technology/technology_nodes.csv"}) {
        if (fs::exists(p)) {
            nodes_path = fs::canonical(p).string();
            break;
        }
    }
    if (nodes_path.empty()) {
        WARN("technology_nodes.csv not found; skipping");
        return;
    }
    TechnologyCatalog catalog;
    REQUIRE(catalog.load_nodes_csv(nodes_path));

    // The pre-modern arc: each historical era (1-7) has at least one tech node.
    for (uint8_t era = 1; era <= 7; ++era) {
        int count = 0;
        for (const auto& n : catalog.all())
            if (n.era_available == era)
                ++count;
        INFO("era " << static_cast<int>(era));
        CHECK(count >= 1);
    }

    // The tree is well-formed: every prerequisite exists AND comes no later than the
    // node that needs it (no forward references in time).
    for (const auto& n : catalog.all()) {
        for (const auto& pre : n.prerequisites) {
            INFO(n.node_key << " requires " << pre);
            const auto* p = catalog.find(pre);
            REQUIRE(p != nullptr);
            CHECK(p->era_available <= n.era_available);
        }
    }

    // Spot-check the arc: writing (Bronze) -> iron smelting (Iron) -> steam (Industrial).
    REQUIRE(catalog.find("writing") != nullptr);
    CHECK(catalog.find("writing")->era_available == 2);
    REQUIRE(catalog.find("steam_engine") != nullptr);
    CHECK(catalog.find("steam_engine")->era_available == 7);
}

TEST_CASE("Tech effects: parsed from CSV and aggregate (compound) by era",
          "[technology][catalog]") {
    // Two knowledge techs in different eras; effects compound as the era rises.
    std::string csv =
        R"(node_key,domain,display_name,era_available,difficulty,patentable,prerequisites,outcome_type,key_technology_node,unlocks_recipe,unlocks_facility_type,is_baseline,knowledge_mult,food_mult,mortality_mult
writing,software_systems,Writing,2,2.0,0,,product_unlock,writing,,,0,1.5,1,1
plough,mechanical_engineering,Plough,2,2.0,0,,product_unlock,plough,,,0,1,2.0,1
printing,mechanical_engineering,Printing,4,4.0,0,,product_unlock,printing,,,0,2.0,1,1
medicine,biotechnology,Medicine,4,4.0,0,,product_unlock,medicine,,,0,1,1,0.5
)";
    auto path = write_temp_csv("test_effects.csv", csv);
    TechnologyCatalog catalog;
    REQUIRE(catalog.load_nodes_csv(path));

    CHECK_THAT(catalog.find("writing")->knowledge_mult, Catch::Matchers::WithinAbs(1.5f, 0.001f));

    // Era 1: no nodes available -> neutral.
    auto e1 = catalog.aggregate_effects(1);
    CHECK_THAT(e1.knowledge_mult, Catch::Matchers::WithinAbs(1.0f, 0.001f));

    // Era 2: writing (1.5 knowledge) + plough (2.0 food).
    auto e2 = catalog.aggregate_effects(2);
    CHECK_THAT(e2.knowledge_mult, Catch::Matchers::WithinAbs(1.5f, 0.001f));
    CHECK_THAT(e2.food_mult, Catch::Matchers::WithinAbs(2.0f, 0.001f));

    // Era 4: writing*printing knowledge (1.5*2.0=3.0), medicine mortality 0.5.
    auto e4 = catalog.aggregate_effects(4);
    CHECK_THAT(e4.knowledge_mult, Catch::Matchers::WithinAbs(3.0f, 0.001f));
    CHECK_THAT(e4.mortality_mult, Catch::Matchers::WithinAbs(0.5f, 0.001f));
}

TEST_CASE("Load base game maturation_ceilings.csv", "[technology][catalog][data]") {
    namespace fs = std::filesystem;
    std::string ceilings_path;
    const char* prefixes[] = {
        "packages/base_game/technology/maturation_ceilings.csv",
        "../packages/base_game/technology/maturation_ceilings.csv",
        "../../packages/base_game/technology/maturation_ceilings.csv",
        "../../../packages/base_game/technology/maturation_ceilings.csv",
    };
    for (const auto* prefix : prefixes) {
        if (fs::exists(prefix)) {
            ceilings_path = fs::canonical(prefix).string();
            break;
        }
    }
    if (ceilings_path.empty()) {
        WARN("Could not find maturation_ceilings.csv — skipping data test");
        return;
    }

    TechnologyCatalog catalog;
    REQUIRE(catalog.load_ceilings_csv(ceilings_path));

    // Re-based timeline: EV's old era_1 (-1.0) now sits at era 5; it is not
    // researchable through the modern anchor and opens at era 6 (old era 2).
    CHECK(catalog.ceiling_for("electric_vehicle", 8) < 0.0f);
    CHECK(catalog.ceiling_for("electric_vehicle", 9) > 0.0f);

    // Baseline extraction should have full ceiling in all eras.
    CHECK_THAT(catalog.ceiling_for("basic_extraction", 1),
               Catch::Matchers::WithinAbs(1.0f, 0.001f));
}

// ===========================================================================
// Data-driven era timeline (re-based: dawn = era 1, modern = era 5)
// ===========================================================================

TEST_CASE("Era timeline matches the re-based spec", "[technology][era]") {
    EraCatalog cat;
    cat.load_builtin_default();
    CHECK(cat.find("turn_of_millennium")->index == 8);  // modern anchor
    CHECK(cat.find("divergence")->index == 17);          // last era
    CHECK(cat.max_era() == 17);
    CHECK(cat.v1_max_era() == 12);  // V1 spans the dawn through "transition"
    CHECK(cat.by_index(1)->key == "neolithic");  // the dawn
}

// ---------------------------------------------------------------------------
// R&D maturation grounding (Phase 3): facility quality from the actor's lab
// tier, funding paid from cash, researcher quality a documented baseline.
// ---------------------------------------------------------------------------
namespace {
struct MaturationOutcome {
    float maturation_delta = 0.0f;  // increase in the node's maturation_level
    float cash_delta = 0.0f;        // R&D cost charged to the business (<= 0)
};

MaturationOutcome run_maturation(float effective_tier, float cash) {
    WorldState state{};
    state.current_tick = 1;

    NPCBusiness biz{};
    biz.id = 9;
    biz.cash = cash;
    biz.actor_tech_state.effective_tech_tier = effective_tier;
    TechHolding holding{};
    holding.node_key = "ai_optimization";
    holding.maturation_level = 0.20f;
    holding.maturation_ceiling = 1.0f;  // headroom to grow
    biz.actor_tech_state.holdings["ai_optimization"] = holding;
    state.npc_businesses.push_back(biz);

    MaturationProject mp{};
    mp.node_key = "ai_optimization";
    mp.business_id = 9;
    mp.researchers_assigned = 2;
    state.technology.active_maturation_projects.push_back(mp);

    TechnologyModule module;
    DeltaBuffer delta{};
    module.execute(state, delta);

    MaturationOutcome out{};
    for (const auto& td : delta.technology_deltas) {
        if (td.business_id == 9 && td.maturation_level_update.has_value())
            out.maturation_delta = *td.maturation_level_update - 0.20f;
    }
    for (const auto& bd : delta.business_deltas) {
        if (bd.business_id == 9 && bd.cash_delta.has_value())
            out.cash_delta = *bd.cash_delta;
    }
    return out;
}
}  // namespace

TEST_CASE("Technology: maturation is funded from cash and costs money",
          "[technology][rnd]") {
    // A well-funded actor matures the node AND is charged for the R&D.
    MaturationOutcome funded = run_maturation(/*effective_tier=*/5.0f, /*cash=*/100000.0f);
    CHECK(funded.maturation_delta > 0.0f);
    CHECK(funded.cash_delta < 0.0f);  // conservation: R&D is not free
}

TEST_CASE("Technology: a cash-starved actor cannot advance R&D", "[technology][rnd]") {
    // No cash -> funding_adequacy 0 -> no progress and no charge.
    MaturationOutcome broke = run_maturation(/*effective_tier=*/5.0f, /*cash=*/0.0f);
    CHECK(broke.maturation_delta == 0.0f);
    CHECK(broke.cash_delta == 0.0f);
}

TEST_CASE("Technology: a higher-tier lab matures technology faster", "[technology][rnd]") {
    // Same funding; the better-equipped lab (higher effective_tech_tier) makes more
    // progress per tick (facility quality is grounded, not a flat 1.0).
    MaturationOutcome low = run_maturation(/*effective_tier=*/2.0f, /*cash=*/100000.0f);
    MaturationOutcome high = run_maturation(/*effective_tier=*/9.0f, /*cash=*/100000.0f);
    CHECK(high.maturation_delta > low.maturation_delta);
}

// ===========================================================================
// WHAT A PLACE CAN DO, NOT WHAT ITS ERA ENTITLES IT TO (2026-08-22).
//
// `aggregate_effects` switches on every node with era_available <= era, so a
// society was handed the plough, the aqueduct, inoculation and germ theory the
// moment an integer ticked over — the "it rises with the era" form the no-rails
// rule names as the most dangerous, and the reason no two provinces could ever
// differ in technique. These tests exist so it cannot come back.
// ===========================================================================

namespace {
// A three-node tree spanning the eras: a starting-package technique, a mid one, and a
// hard late one that depends on it.
TechnologyCatalog tiny_tree() {
    const std::string csv =
        "node_key,domain,display_name,era_available,difficulty,patentable,prerequisites,"
        "outcome_type,key_technology_node,unlocks_recipe,unlocks_facility_type,is_baseline,"
        "knowledge_mult,food_mult,mortality_mult,path\n"
        "farming,agri,Farming,1,1.0,0,,product_unlock,,,,0,1,1.5,1,main\n"
        "sewers,civil,Sewers,4,4.0,0,,product_unlock,,,,0,1,1,0.9,main\n"
        "germs,bio,Germ Theory,7,7.5,0,sewers,product_unlock,,,,0,1,1,0.6,main\n";
    TechnologyCatalog cat;
    cat.load_nodes_csv(write_temp_csv("adoption_tree.csv", csv));
    return cat;
}
EraCatalog default_eras() {
    EraCatalog e;
    e.load_builtin_default();
    return e;
}
}  // namespace

TEST_CASE("technique: the era ladder IS the tree's content, not a fit to historical dates",
          "[technology][no-rails][adoption]") {
    // THE DRIFT GUARD ON THE KNOWLEDGE AXIS. `knowledge_to_advance` in eras.csv is
    // authored so nothing derives at runtime, and this is what keeps it honest: it must
    // equal the running total of the tree's own content weights. Change the tree and this
    // fails until the ladder is re-authored to match.
    //
    // They used to be seven numbers FITTED so that earthlike hit seven historical dates.
    // That made the model's one permitted pure pacing dial into the definition of the
    // knowledge unit, so the unit floated with the fit — and there was nothing for a
    // technique's cost to be measured against. The single fitted dial is now the RATE
    // knowledge accumulates at, which is a rate and decides nothing.
    TechnologyCatalog cat;
    const std::string dir = find_base_game_technology_dir();
    if (dir.empty())
        SKIP("base_game technology directory not found");
    REQUIRE(cat.load_nodes_csv(dir + "/technology_nodes.csv"));

    EraCatalog eras;
    eras.load_builtin_default();
    const TechnologyAdoptionConfig cfg{};
    const std::vector<float> derived = cat.derive_era_thresholds(7, cfg);
    REQUIRE(derived.size() == 7u);
    for (uint8_t era = 1; era <= 7; ++era) {
        const EraDefinition* def = eras.by_index(era);
        REQUIRE(def != nullptr);
        INFO("era " << static_cast<int>(era) << ": authored " << def->knowledge_to_advance
                    << ", tree says " << derived[era - 1]);
        CHECK_THAT(def->knowledge_to_advance,
                   Catch::Matchers::WithinAbs(derived[era - 1], 1.0f));
    }
    // And it is a ladder: strictly rising, and compounding rather than creeping, because
    // each era's tree stands for more learning than the one before.
    for (size_t i = 1; i < derived.size(); ++i)
        CHECK(derived[i] > derived[i - 1]);
    CHECK(derived[6] / derived[0] > 50.0f);
}

TEST_CASE("technique: a technique costs the learning its era stands for",
          "[technology][no-rails][adoption]") {
    EraCatalog eras;
    eras.load_builtin_default();

    // Era 1 is the Neolithic and the model STARTS there, so its package is free.
    // Anchoring it at the END of the Neolithic instead starved the founding band before
    // it could invent farming: 21,127 people fell to 1,900 and stayed for 13,000 years.
    CHECK_THAT(TechnologyCatalog::knowledge_required(1, 1.0f, eras),
               Catch::Matchers::WithinAbs(0.0f, 1e-6f));

    // A later technique costs what it takes to REACH its era — so a society entering an
    // era is half-way into that era's techniques and works through them as it goes.
    const EraDefinition* e3 = eras.by_index(3);
    REQUIRE(e3 != nullptr);
    CHECK_THAT(TechnologyCatalog::knowledge_required(4, 4.0f, eras),
               Catch::Matchers::WithinRel(e3->knowledge_to_advance, 1e-4f));

    // Difficulty beyond the era number orders techniques WITHIN it, never past it.
    CHECK(TechnologyCatalog::knowledge_required(4, 4.5f, eras) >
          TechnologyCatalog::knowledge_required(4, 4.0f, eras));
    CHECK(TechnologyCatalog::knowledge_required(4, 9.0f, eras) <=
          TechnologyCatalog::knowledge_required(5, 5.0f, eras));
}

TEST_CASE("technique: two places in the same era can do different things",
          "[technology][no-rails][adoption]") {
    // The whole point. Under era gating this was impossible by construction — the era is
    // global, so Britain and Qing China were the same society.
    const TechnologyCatalog cat = tiny_tree();
    EraCatalog eras;
    eras.load_builtin_default();
    const TechnologyAdoptionConfig cfg{};
    const float late_knowledge = TechnologyCatalog::knowledge_required(7, 7.5f, eras) * 4.0f;

    // KNOWING IS NOT HAVING. Same knowledge, different built capital.
    const float knows_and_has = cat.effects_for(late_knowledge, 5000.0f, eras, cfg).mortality_mult;
    const float knows_only = cat.effects_for(late_knowledge, 1.0f, eras, cfg).mortality_mult;
    CHECK(knows_and_has < knows_only);  // medicine cuts mortality; only the equipped get it

    // And having is not knowing: capital without the learning does nothing for a
    // technique nobody here understands.
    const float has_only = cat.effects_for(0.0f, 5000.0f, eras, cfg).mortality_mult;
    CHECK(has_only > knows_and_has);
}

TEST_CASE("technique: nothing switches on at an era boundary", "[technology][no-rails][adoption]") {
    // A technique SPREADS. Crossing the knowledge a technique demands must not step the
    // world's capability; measured under era gating, the era 1 -> 2 boundary raised the
    // food surplus from 1.58 to 3.95 in a single tick with nothing in the world changed.
    const TechnologyCatalog cat = tiny_tree();
    EraCatalog eras;
    eras.load_builtin_default();
    const TechnologyAdoptionConfig cfg{};
    const float t = TechnologyCatalog::knowledge_required(4, 4.0f, eras);  // `sewers`

    const float just_below = cat.effects_for(t * 0.999f, 400.0f, eras, cfg).mortality_mult;
    const float just_above = cat.effects_for(t * 1.001f, 400.0f, eras, cfg).mortality_mult;
    // Continuous: a 0.2% change in knowledge moves capability by well under a percent.
    CHECK(std::abs(just_above - just_below) / just_below < 0.01f);
    // And monotone across the whole range, with no plateau at the boundary.
    CHECK(cat.effects_for(t * 4.0f, 400.0f, eras, cfg).mortality_mult < just_above);
}

TEST_CASE("technique: a chain cannot outrun its weakest link", "[technology][no-rails][adoption]") {
    // No vaccination in a society that has not got germ theory. `germs` requires `sewers`,
    // so a place with the knowledge for both but the capital for neither gets neither.
    const TechnologyCatalog cat = tiny_tree();
    EraCatalog eras;
    eras.load_builtin_default();
    const TechnologyAdoptionConfig cfg{};
    const float k = TechnologyCatalog::knowledge_required(7, 7.5f, eras) * 8.0f;

    const float unbuilt = cat.effects_for(k, 0.5f, eras, cfg).mortality_mult;
    const float built = cat.effects_for(k, 20000.0f, eras, cfg).mortality_mult;
    CHECK_THAT(unbuilt, Catch::Matchers::WithinAbs(1.0f, 0.05f));  // knows it, cannot do it
    CHECK(built < 0.7f);                                          // both, and it works
}

TEST_CASE("technique: the dawn is not a blank slate but it is not the Bronze Age either",
          "[technology][no-rails][adoption]") {
    // A founding band has its era's package and nothing beyond it, so its food technique
    // is the era-1 multiplier alone and its medicine is neutral.
    const TechnologyCatalog cat = tiny_tree();
    const EraCatalog eras = default_eras();
    const TechnologyAdoptionConfig cfg{};
    const EraTechEffects dawn = cat.effects_for(0.0f, 0.0f, eras, cfg);
    // Its one free technique's full gain, less the sliver the saturating composition
    // takes even at the first node — techniques overlap from the very first one.
    CHECK_THAT(dawn.food_mult, Catch::Matchers::WithinRel(1.5f, 0.02f));
    CHECK_THAT(dawn.mortality_mult, Catch::Matchers::WithinAbs(1.0f, 1e-4f));
}

// ===========================================================================
// AN ERA IS A SET OF TECHNIQUES (2026-08-23).
//
// A society moves past an era when it has worked out enough of that era's MAIN
// PATH — the spine the era is made of — and it falls back when it can no longer
// carry the one it came from. Side paths are depth: a society may take as many
// or as few as it likes without being held back or hurried.
//
// This replaced two fitted numbers per era whose values existed to make one
// world hit seven historical dates. These tests inherit the behaviours those
// numbers were protecting, and they are about the SHAPE of advancement — never
// about how long a society takes over it.
// ===========================================================================

TEST_CASE("era advance: knowing is not enough — an era needs the capacity to use it",
          "[technology][era-advance][no-rails]") {
    // The flying-car case: the knowledge exists, the capacity does not. And its mirror,
    // which is the one the model could never express while the era was global — a society
    // with the workshops and no idea what to build in them.
    const TechnologyCatalog cat = tiny_tree();
    EraCatalog eras;
    eras.load_builtin_default();
    const TechnologyAdoptionConfig cfg{};
    const float plenty = TechnologyCatalog::knowledge_required(7, 7.5f, eras) * 8.0f;

    const float knows_only = cat.main_path_progress(7, plenty, 0.0f, eras, cfg);
    const float has_only = cat.main_path_progress(7, 0.0f, 50000.0f, eras, cfg);
    const float both = cat.main_path_progress(7, plenty, 50000.0f, eras, cfg);
    CHECK(knows_only < cfg.era_advance_main_share);
    CHECK(has_only < cfg.era_advance_main_share);
    CHECK(both > knows_only);
    CHECK(both > has_only);
}

TEST_CASE("era advance: built capacity is counted per head, not as a heap",
          "[technology][era-advance][no-rails]") {
    // A bigger society needs proportionally more built, so population growth alone cannot
    // buy an era: the same absolute stock that carries a small society is spread too thin
    // by a large one. main_path_progress takes capital PER HEAD for exactly this reason.
    const TechnologyCatalog cat = tiny_tree();
    EraCatalog eras;
    eras.load_builtin_default();
    const TechnologyAdoptionConfig cfg{};
    const float k = TechnologyCatalog::knowledge_required(7, 7.5f, eras) * 8.0f;

    const float stock = 2.0e9f;
    const float small_society = cat.main_path_progress(7, k, stock / 20000.0f, eras, cfg);
    const float large_society = cat.main_path_progress(7, k, stock / 40000000.0f, eras, cfg);
    CHECK(small_society > large_society);
    CHECK(small_society > cfg.era_advance_main_share);
    CHECK(large_society < cfg.era_advance_main_share);
}

TEST_CASE("era advance: an era is held while its path holds and lost when it does not",
          "[technology][era-advance][no-rails]") {
    // THE FALL, and the ratchet back. The works stand but nobody can build or maintain
    // them any more — which is what makes the climb a sawtooth rather than a ramp, and
    // forward-only advancement could only ever model the first half of that.
    const TechnologyCatalog cat = tiny_tree();
    EraCatalog eras;
    eras.load_builtin_default();
    const TechnologyAdoptionConfig cfg{};
    const float carries = TechnologyCatalog::knowledge_required(4, 4.0f, eras) * 4.0f;

    // A society comfortably carrying the previous era's path keeps what it has.
    CHECK(cat.main_path_progress(4, carries, 20000.0f, eras, cfg) >
          cfg.era_advance_main_share * cfg.era_fall_hysteresis);
    // One whose learning has collapsed cannot, and the era goes.
    CHECK(cat.main_path_progress(4, carries * 0.02f, 20000.0f, eras, cfg) <
          cfg.era_advance_main_share * cfg.era_fall_hysteresis);
    // And when it recovers, it climbs again — falling back is not the end of history.
    CHECK(cat.main_path_progress(4, carries, 20000.0f, eras, cfg) > cfg.era_advance_main_share);
}

TEST_CASE("era advance: side paths are depth, and never a toll on the road",
          "[technology][era-advance][no-rails]") {
    // The distinction the whole tree turns on. A society may specialise as deeply as it
    // likes without that being required of it, and a society that ignores the side
    // branches entirely is not held back — it just never gets what they pay.
    TechnologyCatalog cat;
    const std::string dir = find_base_game_technology_dir();
    if (dir.empty())
        SKIP("base_game technology directory not found");
    REQUIRE(cat.load_nodes_csv(dir + "/technology_nodes.csv"));

    // Every pre-modern era has a spine and rather more depth around it.
    for (uint8_t era = 1; era <= 7; ++era) {
        uint32_t main_count = 0, side_count = 0;
        for (const auto& n : cat.all()) {
            if (n.era_available != era)
                continue;
            (n.main_path ? main_count : side_count)++;
        }
        INFO("era " << static_cast<int>(era) << ": " << main_count << " main, " << side_count
                    << " side");
        CHECK(cat.has_main_path(era));
        CHECK(main_count >= 5u);
        CHECK(side_count > main_count);  // depth outweighs the spine, as it should
    }
    // The modern band has no spine authored: it keeps its own calendar transition and
    // must not advance the instant it is entered on a vacuous mean over no nodes.
    CHECK_FALSE(cat.has_main_path(8));
}
