#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_gen/world_class.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

TEST_CASE("Deathworld Class: Earth anchors at Class 12", "[world_class][tier0]") {
    // The Earth profile is the calibration anchor.
    CHECK_THAT(deathworld_class(earth_profile()), WithinAbs(12.0f, 0.01f));
    CHECK(class_band(deathworld_class(earth_profile())) == WorldClassBand::deathworld);
}

TEST_CASE("Deathworld Class: bands map per the Jenkinsverse scale", "[world_class][tier0]") {
    CHECK(class_band(2.0f) == WorldClassBand::garden);       // 1-3
    CHECK(class_band(5.0f) == WorldClassBand::typical);      // 4-7
    CHECK(class_band(9.0f) == WorldClassBand::harsh);        // 8-10
    CHECK(class_band(12.0f) == WorldClassBand::deathworld);  // 11-13
    CHECK(class_band(15.0f) == WorldClassBand::extreme);     // 14+

    CHECK(class_band_name(WorldClassBand::garden) == "Garden");
    CHECK(class_band_name(WorldClassBand::deathworld) == "Deathworld");
}

TEST_CASE("Deathworld Class: presets fall in their bands; class never below 1",
          "[world_class][tier0]") {
    CHECK(class_band(deathworld_class(garden_profile())) == WorldClassBand::garden);
    CHECK(class_band(deathworld_class(deathworld_profile())) == WorldClassBand::deathworld);

    // A near-zero profile still floors at Class 1 (no sub-Class-1 world).
    WorldHazardProfile nil{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    CHECK(deathworld_class(nil) == 1.0f);

    // Earth is harsher than a garden, gentler than a deathworld.
    CHECK(deathworld_class(garden_profile()) < deathworld_class(earth_profile()));
    CHECK(deathworld_class(earth_profile()) < deathworld_class(deathworld_profile()));
}
