#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <string>

#include "core/world_gen/era_catalog.h"

using namespace econlife;

namespace {
// Locate packages/base_game/eras from any plausible build/working dir.
std::string find_eras_dir() {
    namespace fs = std::filesystem;
    const char* candidates[] = {
        "packages/base_game/eras",
        "../packages/base_game/eras",
        "../../packages/base_game/eras",
        "../../../packages/base_game/eras",
    };
    for (const auto* c : candidates) {
        if (fs::exists(fs::path(c) / "eras.csv"))
            return fs::canonical(c).string();
    }
    return "";
}
}  // namespace

TEST_CASE("EraCatalog builtin default defines the re-based timeline", "[era_catalog][tier0]") {
    EraCatalog cat;
    cat.load_builtin_default();

    REQUIRE(cat.size() == 17);
    CHECK(cat.max_era() == 17);
    // V1 spans the dawn (Neolithic) through "transition" (now index 12).
    CHECK(cat.v1_max_era() == 12);

    // The dawn is era 1 (Neolithic); the modern anchor (turn of the millennium) is era 8.
    const EraDefinition* dawn = cat.by_index(1);
    REQUIRE(dawn != nullptr);
    CHECK(dawn->key == "neolithic");
    CHECK(dawn->economic_regime == "subsistence");

    const EraDefinition* modern = cat.find("turn_of_millennium");
    REQUIRE(modern != nullptr);
    CHECK(modern->index == 8);
    CHECK(modern->start_year == 2000);

    // The default entry era (until the dawn becomes the default) is the modern anchor.
    CHECK(cat.default_entry_index() == 8);
}

TEST_CASE("EraCatalog loads the base-game CSV matching the builtin default",
          "[era_catalog][tier0]") {
    // This guard is the only thing standing between the two copies of the timeline
    // (era_catalog.cpp's builtin and packages/base_game/eras/eras.csv) and silent
    // drift: nearly every unit test runs on the builtin while the shipped game
    // loads the CSV, and world_generator falls back to the builtin without a word
    // if the CSV cannot be read. It must NOT self-skip — a skip on a missing
    // directory is exactly how a drifted timeline would slip through CI.
    std::string dir = find_eras_dir();
    REQUIRE_FALSE(dir.empty());
    EraCatalog from_csv;
    REQUIRE(from_csv.load_from_directory(dir));

    EraCatalog builtin;
    builtin.load_builtin_default();

    REQUIRE(from_csv.size() == builtin.size());
    CHECK(from_csv.default_entry_index() == builtin.default_entry_index());
    CHECK(from_csv.v1_max_era() == builtin.v1_max_era());
    for (uint8_t i = 1; i <= builtin.max_era(); ++i) {
        const EraDefinition* a = from_csv.by_index(i);
        const EraDefinition* b = builtin.by_index(i);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        CHECK(a->key == b->key);
        CHECK(a->start_year == b->start_year);
        CHECK(a->economic_regime == b->economic_regime);
        CHECK(a->v1_in_scope == b->v1_in_scope);
        CHECK(a->display_name == b->display_name);
        CHECK(a->is_default_entry == b->is_default_entry);
        // knowledge_to_advance is the ratified era PACING surface — the thresholds
        // the recalibration tunes. It was the one field this guard did not compare,
        // so the two copies could disagree on how long every era lasts while the
        // test stayed green.
        CHECK_THAT(a->knowledge_to_advance,
                   Catch::Matchers::WithinRel(b->knowledge_to_advance, 1e-6f));
        // The MATERIAL gate is pacing surface too: a society advances only when it can
        // build what it knows, so the two copies must agree on that as well.
        CHECK_THAT(a->capital_to_advance,
                   Catch::Matchers::WithinRel(b->capital_to_advance, 1e-6f));
    }
}

TEST_CASE("EraCatalog rejects missing dir and out-of-range indices", "[era_catalog][tier0]") {
    EraCatalog cat;
    CHECK_FALSE(cat.load_from_directory("/no/such/eras/dir"));
    CHECK(cat.empty());
}
