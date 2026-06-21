#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "core/world_gen/occupation_catalog.h"

using namespace econlife;

namespace {
std::string find_occupations_dir() {
    namespace fs = std::filesystem;
    const char* candidates[] = {
        "packages/base_game/occupations",
        "../packages/base_game/occupations",
        "../../packages/base_game/occupations",
        "../../../packages/base_game/occupations",
    };
    for (const auto* c : candidates) {
        if (fs::exists(fs::path(c) / "occupations.csv"))
            return fs::canonical(c).string();
    }
    return "";
}
}  // namespace

TEST_CASE("OccupationCatalog builtin default defines the livelihood layers", "[occupation][tier0]") {
    OccupationCatalog cat;
    cat.load_builtin_default();

    REQUIRE(cat.size() == 12);
    CHECK(cat.by_index(kNoOccupation) == nullptr);  // 0 is "none"

    const OccupationDefinition* farmer = cat.find("farmer");
    REQUIRE(farmer != nullptr);
    CHECK(farmer->layer == 1);                 // subsistence/food livelihood
    CHECK(farmer->knowledge_output == 0.0f);   // food producers make no knowledge

    const OccupationDefinition* trader = cat.find("trader");
    REQUIRE(trader != nullptr);
    CHECK(trader->layer == 2);            // surplus-funded specialist
    CHECK(trader->min_surplus > 1.0f);    // needs a surplus to be supported

    // Knowledge-producers: the engine of progress.
    const OccupationDefinition* scholar = cat.find("scholar");
    REQUIRE(scholar != nullptr);
    CHECK(scholar->layer == 2);
    CHECK(scholar->knowledge_output > 0.0f);

    CHECK(cat.in_layer(1).size() == 5);  // forager/hunter/fisher/farmer/herder
    CHECK(cat.in_layer(2).size() == 7);  // artisan/builder/healer/trader/elder/scribe/scholar

    // Knowledge-keepers unlock over time: elder at the dawn, scribe with writing
    // (era 2), scholar with formal scholarship (era 4).
    CHECK(cat.find("elder")->min_era == 1);
    CHECK(cat.find("scribe")->min_era == 2);
    CHECK(cat.find("scholar")->min_era == 4);
    // The available layer-2 pool grows as those eras are reached.
    CHECK(cat.in_layer_for_era(2, 1).size() == 5);  // no scribe, no scholar yet
    CHECK(cat.in_layer_for_era(2, 2).size() == 6);  // +scribe
    CHECK(cat.in_layer_for_era(2, 3).size() == 6);  // still no scholar
    CHECK(cat.in_layer_for_era(2, 4).size() == 7);  // +scholar
}

TEST_CASE("OccupationCatalog loads the base-game CSV matching the builtin", "[occupation][tier0]") {
    std::string dir = find_occupations_dir();
    if (dir.empty()) {
        WARN("packages/base_game/occupations not found; skipping CSV load");
        return;
    }
    OccupationCatalog csv;
    REQUIRE(csv.load_from_directory(dir));
    OccupationCatalog builtin;
    builtin.load_builtin_default();

    REQUIRE(csv.size() == builtin.size());
    for (uint16_t i = 1; i <= builtin.size(); ++i) {
        const OccupationDefinition* a = csv.by_index(i);
        const OccupationDefinition* b = builtin.by_index(i);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        CHECK(a->key == b->key);
        CHECK(a->layer == b->layer);
        CHECK(a->min_era == b->min_era);
    }
}
