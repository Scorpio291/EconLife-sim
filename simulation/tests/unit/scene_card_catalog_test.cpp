// Unit tests for SceneCardCatalog — the authored card copy, as data.
//
// Two things are tested here. The parser, on synthetic CSV: what it accepts,
// what it refuses, and how it injects parameters. And the SHIPPED content,
// because the catalog introduces an indirection with a silent failure mode —
// a producer naming a template nobody wrote raises no card at all, and the
// player is talked to by a world that has gone quiet.

#include "modules/scene_cards/scene_card_catalog.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace econlife;
namespace fs = std::filesystem;

namespace {

// Locate packages/base_game/scene_cards from any plausible working directory.
// ctest runs the unit binary from build/simulation/tests/unit.
std::string find_scene_cards_dir() {
    const char* candidates[] = {
        "packages/base_game/scene_cards",          "../packages/base_game/scene_cards",
        "../../packages/base_game/scene_cards",    "../../../packages/base_game/scene_cards",
        "../../../../packages/base_game/scene_cards",
        "../../../../../packages/base_game/scene_cards",
    };
    for (const auto* c : candidates) {
        if (fs::is_directory(c))
            return fs::canonical(c).string();
    }
    return "";
}

std::string write_temp_csv(const std::string& filename, const std::string& content) {
    const auto dir = fs::temp_directory_path() / "econlife_test_scene_cards";
    fs::create_directories(dir);
    const auto path = dir / filename;
    std::ofstream f(path);
    f << content;
    return path.string();
}

const char* HEADER =
    "card_key,card_class,type,setting,dialogue,"
    "choice1_id,choice1_label,choice1_description,"
    "choice2_id,choice2_label,choice2_description,"
    "choice3_id,choice3_label,choice3_description,default_choice_id\n";

}  // namespace

TEST_CASE("SceneCardCatalog: loads a template with its class setting and choices",
          "[scene_cards][catalog]") {
    const std::string csv =
        std::string(HEADER) +
        "a_meeting,timed_optional,meeting,private_office,They want to see you.,"
        "1,Attend,Keep it.,2,Skip,Do not show.,0,,,2\n";
    SceneCardCatalog catalog;
    REQUIRE(catalog.load_from_file(write_temp_csv("basic.csv", csv)) == 1);

    const SceneCardTemplate* t = catalog.find("a_meeting");
    REQUIRE(t != nullptr);
    CHECK(t->card_class == CardClass::timed_optional);
    CHECK(t->type == SceneCardType::meeting);
    CHECK(t->setting == SceneSetting::private_office);
    CHECK(t->dialogue == "They want to see you.");
    REQUIRE(t->choices.size() == 2);
    CHECK(t->choices[0].id == 1);
    CHECK(t->choices[0].label == "Attend");
    CHECK(t->choices[1].id == 2);
    CHECK(t->default_choice_id == 2);
}

TEST_CASE("SceneCardCatalog: a template with no choices is refused", "[scene_cards][catalog]") {
    // A card the player cannot answer wedges the queue. scene_cards drops it
    // on admission; refusing it here makes the content error a MISSING card
    // rather than a silent one.
    const std::string csv =
        std::string(HEADER) + "mute,ambient,news_notification,phone_call,Nothing to say.,"
                              "0,,,0,,,0,,,0\n";
    SceneCardCatalog catalog;
    CHECK(catalog.load_from_file(write_temp_csv("mute.csv", csv)) == 0);
    CHECK(catalog.find("mute") == nullptr);
}

TEST_CASE("SceneCardCatalog: a malformed row is skipped not guessed at",
          "[scene_cards][catalog]") {
    const std::string csv = std::string(HEADER) + "truncated,ambient,news_notification\n" +
                            "good,ambient,news_notification,phone_call,Something happened.,"
                            "1,Noted,,0,,,0,,,0\n";
    SceneCardCatalog catalog;
    CHECK(catalog.load_from_file(write_temp_csv("malformed.csv", csv)) == 1);
    CHECK(catalog.find("truncated") == nullptr);
    CHECK(catalog.find("good") != nullptr);
}

TEST_CASE("SceneCardCatalog: a missing directory is empty not an error",
          "[scene_cards][catalog]") {
    SceneCardCatalog catalog;
    CHECK(catalog.load_from_directory("") == 0);
    CHECK(catalog.load_from_directory("/nonexistent/econlife/scene_cards") == 0);
    CHECK(catalog.size() == 0);
}

TEST_CASE("SceneCardCatalog: inject substitutes parameters and shows what is missing",
          "[scene_cards][catalog]") {
    CHECK(SceneCardCatalog::inject("Unrest in {place}.", {{"place", "Ardestan"}}) ==
          "Unrest in Ardestan.");
    // Every occurrence, not just the first.
    CHECK(SceneCardCatalog::inject("{x} and {x}", {{"x", "one"}}) == "one and one");
    // A placeholder with no parameter is LEFT VISIBLE. Blanking it produces a
    // sentence with a hole in it that reads as finished prose and so is never
    // noticed; leaving it makes the missing value a thing someone can see.
    CHECK(SceneCardCatalog::inject("The sale closed. {subject} is yours.", {}) ==
          "The sale closed. {subject} is yours.");
}

// ---------------------------------------------------------------------------
// The shipped content.
// ---------------------------------------------------------------------------

TEST_CASE("SceneCardCatalog: base game card copy loads", "[scene_cards][catalog]") {
    const std::string dir = find_scene_cards_dir();
    REQUIRE_FALSE(dir.empty());
    SceneCardCatalog catalog;
    REQUIRE(catalog.load_from_directory(dir) > 0);
}

TEST_CASE("SceneCardCatalog: every key a producer names is authored",
          "[scene_cards][catalog]") {
    // The catalog is an indirection with a silent failure mode: a seed naming
    // a template that does not exist raises NO card, so a typo or a renamed
    // row takes the world's voice away without failing anything. This list is
    // the ratchet. A producer added without its copy fails here.
    const char* producer_keys[] = {
        // player_actions — why a founding attempt could not proceed.
        "not_here", "in_transit", "no_capital", "no_premises",
        // real_estate — how a business acquisition went.
        "offer_declined", "offer_accepted", "sale_closed", "sale_lapsed", "sale_lost",
        // media_system — a story that names the player ran.
        "story_ran",
        // random_events — something happened where the player is.
        "news_unrest",
        // scene_cards — a commitment on the calendar falls today.
        "calendar_meeting", "calendar_event", "calendar_operation", "calendar_deadline",
        "calendar_personal", "calendar_commitment", "calendar_summons",
    };

    const std::string dir = find_scene_cards_dir();
    REQUIRE_FALSE(dir.empty());
    SceneCardCatalog catalog;
    REQUIRE(catalog.load_from_directory(dir) > 0);

    for (const char* key : producer_keys) {
        INFO("card_key: " << key);
        REQUIRE(catalog.find(key) != nullptr);
    }
}

TEST_CASE("SceneCardCatalog: every authored card can be answered and expires honestly",
          "[scene_cards][catalog]") {
    const std::string dir = find_scene_cards_dir();
    REQUIRE_FALSE(dir.empty());
    SceneCardCatalog catalog;
    REQUIRE(catalog.load_from_directory(dir) > 0);

    const char* keys[] = {
        "not_here",         "in_transit",       "no_capital",          "no_premises",
        "offer_declined",   "offer_accepted",   "sale_closed",         "sale_lapsed",
        "sale_lost",        "story_ran",        "news_unrest",         "calendar_meeting",
        "calendar_event",   "calendar_operation", "calendar_deadline", "calendar_personal",
        "calendar_commitment", "calendar_summons",
    };

    for (const char* key : keys) {
        INFO("card_key: " << key);
        const SceneCardTemplate* t = catalog.find(key);
        REQUIRE(t != nullptr);

        // Answerable: the queue is bounded by cards leaving it.
        REQUIRE_FALSE(t->choices.empty());
        REQUIRE_FALSE(t->dialogue.empty());

        // A timed_optional card expires into its default outcome, so that
        // outcome has to be one the card actually offers. A default naming a
        // choice that is not on the card is an expiry that resolves to
        // nothing, which is the wedge this whole class of card was built to
        // avoid.
        if (t->card_class == CardClass::timed_optional) {
            REQUIRE(t->default_choice_id != 0);
            bool found = false;
            for (const auto& c : t->choices)
                found = found || (c.id == t->default_choice_id);
            REQUIRE(found);
        }

        // Ambient cards never expire and mandatory cards must be engaged, so
        // neither carries a default outcome.
        if (t->card_class != CardClass::timed_optional)
            REQUIRE(t->default_choice_id == 0);
    }
}
