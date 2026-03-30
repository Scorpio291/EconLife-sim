// Technology catalog and module unit tests.

#include "core/world_gen/technology_catalog.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "core/world_state/shared_types.h"
#include "modules/technology/technology_types.h"

using namespace econlife;

// Helper: write a temp CSV and return the path.
static std::string write_temp_csv(const std::string& filename, const std::string& content) {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "econlife_tech_test";
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
    // Existing code uses ActorTechnologyState{1.0f} — must still work.
    ActorTechnologyState state{1.0f};
    CHECK_THAT(state.effective_tech_tier, Catch::Matchers::WithinAbs(1.0f, 0.001f));
    CHECK(state.holdings.empty());
}

// ===========================================================================
// GlobalTechnologyState
// ===========================================================================

TEST_CASE("GlobalTechnologyState defaults", "[technology][state]") {
    GlobalTechnologyState gts;
    CHECK(gts.current_era == SimulationEra::era_1_turn_of_millennium);
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

    // All baseline nodes should have era_available == 1.
    for (const auto* node : baseline) {
        CHECK(node->era_available == 1);
        CHECK(node->is_baseline == true);
    }

    // Should have nodes across multiple domains.
    auto energy = catalog.nodes_in_domain("energy_systems");
    CHECK(energy.size() >= 5);

    auto semi = catalog.nodes_in_domain("semiconductor_physics");
    CHECK(semi.size() >= 5);
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

    // EV should not be researchable in era 1.
    CHECK(catalog.ceiling_for("electric_vehicle", 1) < 0.0f);

    // EV should be researchable in era 2.
    CHECK(catalog.ceiling_for("electric_vehicle", 2) > 0.0f);

    // Baseline extraction should have full ceiling in all eras.
    CHECK_THAT(catalog.ceiling_for("basic_extraction", 1),
               Catch::Matchers::WithinAbs(1.0f, 0.001f));
}

// ===========================================================================
// SimulationEra enum values
// ===========================================================================

TEST_CASE("SimulationEra enum values match spec", "[technology][era]") {
    CHECK(static_cast<uint8_t>(SimulationEra::era_1_turn_of_millennium) == 1);
    CHECK(static_cast<uint8_t>(SimulationEra::era_5_transition) == 5);
    CHECK(static_cast<uint8_t>(SimulationEra::era_10_divergence) == 10);
    CHECK(MAX_ERA == 10);
    CHECK(V1_MAX_ERA == 5);
}
