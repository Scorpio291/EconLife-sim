// Unit tests for FacilityTypeCatalog - CSV loading and lookup.

#include "core/world_gen/facility_type_catalog.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>

using namespace econlife;
namespace fs = std::filesystem;

static std::string write_temp_csv(const std::string& filename, const std::string& content) {
    auto dir = fs::temp_directory_path() / "econlife_test_facility_types";
    fs::create_directories(dir);
    auto path = dir / filename;
    std::ofstream f(path);
    f << content;
    f.close();
    return path.string();
}

static const char* VALID_CSV =
    "facility_type_key,display_name,category,"
    "base_construction_cost,base_operating_cost,max_workers,"
    "signal_weight_noise,signal_weight_waste,signal_weight_traffic,"
    "signal_weight_pollution,signal_weight_odor\n"
    "mine,Mine,extraction,500000,3000,60,0.7,0.6,0.3,0.5,0.2\n"
    "farm,Farm,agriculture,150000,1000,25,0.2,0.3,0.2,0.2,0.4\n"
    "smelter,Smelter,processing,400000,3500,50,0.7,0.7,0.3,0.8,0.5\n"
    "chemical_plant,Chemical Plant,processing,600000,4000,40,0.5,0.9,0.3,0.8,0.6\n"
    "workshop,Workshop,manufacturing,100000,800,15,0.4,0.3,0.3,0.2,0.2\n";

TEST_CASE("FacilityTypeCatalog - load CSV", "[module][production]") {
    FacilityTypeCatalog catalog;
    std::string path = write_temp_csv("test_ft.csv", VALID_CSV);
    REQUIRE(catalog.load_from_csv(path));
    REQUIRE(catalog.size() == 5);
}

TEST_CASE("FacilityTypeCatalog - find by key", "[module][production]") {
    FacilityTypeCatalog catalog;
    catalog.load_from_csv(write_temp_csv("test_ft2.csv", VALID_CSV));

    const FacilityType* mine = catalog.find("mine");
    REQUIRE(mine != nullptr);
    CHECK(mine->display_name == "Mine");
    CHECK(mine->category == "extraction");
    CHECK(mine->base_construction_cost == 500000.0f);
    CHECK(mine->base_operating_cost == 3000.0f);
    CHECK(mine->max_workers == 60);
    CHECK(mine->signal_weight_noise == 0.7f);
    CHECK(mine->signal_weight_waste == 0.6f);
    CHECK(mine->signal_weight_pollution == 0.5f);

    REQUIRE(catalog.find("nonexistent") == nullptr);
}

TEST_CASE("FacilityTypeCatalog - by category", "[module][production]") {
    FacilityTypeCatalog catalog;
    catalog.load_from_csv(write_temp_csv("test_ft3.csv", VALID_CSV));

    auto extraction = catalog.by_category("extraction");
    CHECK(extraction.size() == 1);
    CHECK(extraction[0]->key == "mine");

    auto processing = catalog.by_category("processing");
    CHECK(processing.size() == 2);

    auto empty = catalog.by_category("nonexistent");
    CHECK(empty.empty());
}

TEST_CASE("FacilityTypeCatalog - empty file", "[module][production]") {
    FacilityTypeCatalog catalog;
    CHECK_FALSE(catalog.load_from_csv("nonexistent.csv"));
    CHECK(catalog.size() == 0);
}
