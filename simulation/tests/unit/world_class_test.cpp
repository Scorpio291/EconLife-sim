#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_gen/world_class.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

TEST_CASE("Deathworld Class is calculated from the settings; Earth anchors at ~12",
          "[world_class][tier0]") {
    // The Class is derived from the chosen settings, not hand-set.
    CHECK_THAT(deathworld_class(earth_hazard()), WithinAbs(12.0f, 0.2f));
    CHECK(class_band(deathworld_class(earth_hazard())) == WorldClassBand::deathworld);
}

TEST_CASE("Deathworld Class responds to individual settings", "[world_class][tier0]") {
    // Crank one dial at a time; the Class rises.
    const float base = deathworld_class(earth_hazard());

    WorldHazardSettings heavy = earth_hazard();
    heavy.gravity_g = 2.0f;  // +1g -> +2 class points
    CHECK_THAT(deathworld_class(heavy), WithinAbs(base + 2.0f, 0.01f));

    WorldHazardSettings plagued = earth_hazard();
    plagued.disease = earth_hazard().disease + 0.2f;  // +0.2 * 3.0 = +0.6
    CHECK(deathworld_class(plagued) > base);

    WorldHazardSettings irradiated = earth_hazard();
    irradiated.radiation = 1.0f;
    CHECK(deathworld_class(irradiated) > base);
}

TEST_CASE("Deathworld Class: bands map per the Jenkinsverse scale", "[world_class][tier0]") {
    CHECK(class_band(2.0f) == WorldClassBand::garden);       // 1-3
    CHECK(class_band(5.0f) == WorldClassBand::typical);      // 4-7
    CHECK(class_band(9.0f) == WorldClassBand::harsh);        // 8-10
    CHECK(class_band(12.0f) == WorldClassBand::deathworld);  // 11-13
    CHECK(class_band(15.0f) == WorldClassBand::extreme);     // 14+
    CHECK(class_band_name(WorldClassBand::deathworld) == "Deathworld");
}

TEST_CASE("Deathworld Class: presets fall in their bands", "[world_class][tier0]") {
    CHECK(class_band(deathworld_class(garden_hazard())) == WorldClassBand::garden);
    CHECK(class_band(deathworld_class(deathworld_hazard())) == WorldClassBand::deathworld);

    // Ordering: garden gentler than Earth, deathworld harsher.
    CHECK(deathworld_class(garden_hazard()) < deathworld_class(earth_hazard()));
    CHECK(deathworld_class(earth_hazard()) < deathworld_class(deathworld_hazard()));

    // Custom weights are honoured (calculation is not hardcoded).
    HazardScoringWeights w{};
    w.gravity = 4.0f;  // double gravity's weight
    CHECK(deathworld_class(earth_hazard(), w) > deathworld_class(earth_hazard()));
}
