// Unit tests for RecipeCatalog - CSV loading, indexing, and lookup.

#include "core/world_gen/recipe_catalog.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>

using namespace econlife;
namespace fs = std::filesystem;

// Helper: write a temp CSV and return its path.
static std::string write_temp_csv(const std::string& filename, const std::string& content) {
    auto dir = fs::temp_directory_path() / "econlife_test_recipes";
    fs::create_directories(dir);
    auto path = dir / filename;
    std::ofstream f(path);
    f << content;
    f.close();
    return path.string();
}

static const char* VALID_CSV =
    "recipe_key,facility_type_key,display_name,"
    "input_1_key,input_1_qty,input_2_key,input_2_qty,"
    "input_3_key,input_3_qty,input_4_key,input_4_qty,"
    "output_1_key,output_1_qty,output_1_is_byproduct,"
    "output_2_key,output_2_qty,output_2_is_byproduct,"
    "labor_per_tick,energy_per_tick,min_tech_tier,key_technology_node,era_available\n"
    "iron_smelting,smelter,Iron Smelting,"
    "iron_ore,5,coking_coal,2,,,,,"
    "steel,3,0,,,0,"
    "80,5.0,2,,1\n"
    "copper_smelting,smelter,Copper Smelting,"
    "copper_ore,4,,,,,,,"
    "copper_wire_rod,2,0,copper_pipe,1,1,"
    "40,3.0,2,,1\n"
    "silicon_wafer_fab,electronics_factory,Silicon Wafer Fabrication,"
    "silica_sand,3,industrial_gas,1,,,,,"
    "silicon_wafer,1,0,,,0,"
    "30,5.0,4,semiconductor_fabrication,2\n";

TEST_CASE("RecipeCatalog - load single CSV", "[module][production]") {
    RecipeCatalog catalog;
    std::string path = write_temp_csv("test_recipes.csv", VALID_CSV);
    REQUIRE(catalog.load_csv(path));
    REQUIRE(catalog.size() == 3);
}

TEST_CASE("RecipeCatalog - find by key", "[module][production]") {
    RecipeCatalog catalog;
    catalog.load_csv(write_temp_csv("test_recipes2.csv", VALID_CSV));

    const Recipe* r = catalog.find("iron_smelting");
    REQUIRE(r != nullptr);
    CHECK(r->name == "Iron Smelting");
    CHECK(r->facility_type_key == "smelter");
    CHECK(r->inputs.size() == 2);
    CHECK(r->inputs[0].good_id == "iron_ore");
    CHECK(r->inputs[0].quantity_per_tick == 5.0f);
    CHECK(r->inputs[1].good_id == "coking_coal");
    CHECK(r->outputs.size() == 1);
    CHECK(r->outputs[0].good_id == "steel");
    CHECK(r->outputs[0].quantity_per_tick == 3.0f);
    CHECK_FALSE(r->outputs[0].is_byproduct);
    CHECK(r->min_tech_tier == 2);
    CHECK(r->era_available == 1);
    CHECK_FALSE(r->is_technology_intensive);

    REQUIRE(catalog.find("nonexistent") == nullptr);
}

TEST_CASE("RecipeCatalog - byproduct flag", "[module][production]") {
    RecipeCatalog catalog;
    catalog.load_csv(write_temp_csv("test_recipes3.csv", VALID_CSV));

    const Recipe* r = catalog.find("copper_smelting");
    REQUIRE(r != nullptr);
    REQUIRE(r->outputs.size() == 2);
    CHECK_FALSE(r->outputs[0].is_byproduct);
    CHECK(r->outputs[1].is_byproduct);
    CHECK(r->outputs[1].good_id == "copper_pipe");
}

TEST_CASE("RecipeCatalog - technology intensive recipe", "[module][production]") {
    RecipeCatalog catalog;
    catalog.load_csv(write_temp_csv("test_recipes4.csv", VALID_CSV));

    const Recipe* r = catalog.find("silicon_wafer_fab");
    REQUIRE(r != nullptr);
    CHECK(r->is_technology_intensive);
    CHECK(r->key_technology_node == "semiconductor_fabrication");
    CHECK(r->min_tech_tier == 4);
    CHECK(r->era_available == 2);
}

TEST_CASE("RecipeCatalog - recipes for facility type index", "[module][production]") {
    RecipeCatalog catalog;
    catalog.load_csv(write_temp_csv("test_recipes5.csv", VALID_CSV));

    auto smelter_recipes = catalog.recipes_for_facility_type("smelter");
    CHECK(smelter_recipes.size() == 2);

    auto electronics_recipes = catalog.recipes_for_facility_type("electronics_factory");
    CHECK(electronics_recipes.size() == 1);

    auto none = catalog.recipes_for_facility_type("nonexistent");
    CHECK(none.empty());
}

TEST_CASE("RecipeCatalog - recipes by output index", "[module][production]") {
    RecipeCatalog catalog;
    catalog.load_csv(write_temp_csv("test_recipes6.csv", VALID_CSV));

    auto steel_recipes = catalog.recipes_by_output("steel");
    CHECK(steel_recipes.size() == 1);
    CHECK(steel_recipes[0]->id == "iron_smelting");

    auto copper_pipe_recipes = catalog.recipes_by_output("copper_pipe");
    CHECK(copper_pipe_recipes.size() == 1);
}

TEST_CASE("RecipeCatalog - recipes available at era", "[module][production]") {
    RecipeCatalog catalog;
    catalog.load_csv(write_temp_csv("test_recipes7.csv", VALID_CSV));

    auto era1 = catalog.recipes_available_at(1);
    CHECK(era1.size() == 2);

    auto era2 = catalog.recipes_available_at(2);
    CHECK(era2.size() == 3);
}

TEST_CASE("RecipeCatalog - empty/malformed CSV", "[module][production]") {
    RecipeCatalog catalog;
    CHECK_FALSE(catalog.load_csv("nonexistent_file.csv"));
    CHECK(catalog.size() == 0);
}

TEST_CASE("RecipeCatalog - load from directory", "[module][production]") {
    auto dir = fs::temp_directory_path() / "econlife_test_recipe_dir";
    fs::create_directories(dir);

    // Write test CSV into the directory.
    {
        std::ofstream f(dir / "recipes_test.csv");
        f << VALID_CSV;
    }

    RecipeCatalog catalog;
    REQUIRE(catalog.load_from_directory(dir.string()));
    CHECK(catalog.size() >= 3);

    // Cleanup.
    fs::remove_all(dir);
}
