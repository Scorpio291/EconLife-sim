#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "modules/political_cycle/political_cycle_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

TEST_CASE("PoliticalCycle: raw vote share weighted calculation", "[political_cycle][tier10]") {
    std::unordered_map<std::string, float> approval = {
        {"working_class", 0.7f},
        {"corporate", 0.3f}
    };
    std::vector<DemographicWeight> demographics = {
        {"working_class", 0.6f, 1.0f},
        {"corporate", 0.4f, 1.0f}
    };
    // (0.7*0.6 + 0.3*0.4) / (0.6 + 0.4) = (0.42 + 0.12) / 1.0 = 0.54
    float share = PoliticalCycleModule::compute_raw_vote_share(approval, demographics);
    REQUIRE_THAT(share, WithinAbs(0.54f, 0.01f));
}

TEST_CASE("PoliticalCycle: vote share default approval for missing demographic", "[political_cycle][tier10]") {
    std::unordered_map<std::string, float> approval = {
        {"working_class", 0.8f}
    };
    std::vector<DemographicWeight> demographics = {
        {"working_class", 0.5f, 1.0f},
        {"corporate", 0.5f, 1.0f}
    };
    // working_class: 0.8*0.5 = 0.40; corporate uses default 0.5: 0.5*0.5 = 0.25
    // total = 0.65 / 1.0 = 0.65
    float share = PoliticalCycleModule::compute_raw_vote_share(approval, demographics);
    REQUIRE_THAT(share, WithinAbs(0.65f, 0.01f));
}

TEST_CASE("PoliticalCycle: zero weight demographics return 0.5", "[political_cycle][tier10]") {
    std::unordered_map<std::string, float> approval;
    std::vector<DemographicWeight> demographics = {
        {"empty", 0.0f, 0.0f}
    };
    float share = PoliticalCycleModule::compute_raw_vote_share(approval, demographics);
    REQUIRE_THAT(share, WithinAbs(0.5f, 0.01f));
}

TEST_CASE("PoliticalCycle: resource modifier diminishing returns", "[political_cycle][tier10]") {
    float low = PoliticalCycleModule::compute_resource_modifier(0.5f);
    float high = PoliticalCycleModule::compute_resource_modifier(5.0f);

    // Both within bounds
    REQUIRE(low >= -PoliticalCycleModule::RESOURCE_MAX_EFFECT);
    REQUIRE(low <= PoliticalCycleModule::RESOURCE_MAX_EFFECT);
    REQUIRE(high >= -PoliticalCycleModule::RESOURCE_MAX_EFFECT);
    REQUIRE(high <= PoliticalCycleModule::RESOURCE_MAX_EFFECT);

    // Diminishing returns: 5.0 produces less than 10x the effect of 0.5
    REQUIRE(high < low * 10.0f);
    REQUIRE(high > low);
}

TEST_CASE("PoliticalCycle: event modifiers capped at 20%", "[political_cycle][tier10]") {
    std::vector<float> mods = {0.10f, 0.10f, 0.10f, 0.10f, 0.10f};
    float total = PoliticalCycleModule::compute_event_modifier_total(mods);
    REQUIRE_THAT(total, WithinAbs(0.20f, 0.01f));
}

TEST_CASE("PoliticalCycle: negative event modifiers capped", "[political_cycle][tier10]") {
    std::vector<float> mods = {-0.15f, -0.15f};
    float total = PoliticalCycleModule::compute_event_modifier_total(mods);
    REQUIRE_THAT(total, WithinAbs(-0.20f, 0.01f));
}

TEST_CASE("PoliticalCycle: final vote share clamped to [0,1]", "[political_cycle][tier10]") {
    float high = PoliticalCycleModule::compute_final_vote_share(0.90f, 0.15f, 0.20f);
    REQUIRE_THAT(high, WithinAbs(1.0f, 0.01f));

    float low = PoliticalCycleModule::compute_final_vote_share(0.05f, -0.15f, -0.20f);
    REQUIRE_THAT(low, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("PoliticalCycle: legislator support computation", "[political_cycle][tier10]") {
    float support = PoliticalCycleModule::compute_legislator_support(0.3f, 0.1f, 0.15f);
    REQUIRE_THAT(support, WithinAbs(0.55f, 0.01f));
}

TEST_CASE("PoliticalCycle: legislative vote resolution passes", "[political_cycle][tier10]") {
    REQUIRE(PoliticalCycleModule::compute_vote_passed(60.0f, 40.0f, 0.50f) == true);
    REQUIRE(PoliticalCycleModule::compute_vote_passed(50.0f, 50.0f, 0.50f) == false);
    REQUIRE(PoliticalCycleModule::compute_vote_passed(0.0f, 0.0f, 0.50f) == false);
}

TEST_CASE("PoliticalCycle: constants match spec", "[political_cycle][tier10]") {
    REQUIRE_THAT(PoliticalCycleModule::SUPPORT_THRESHOLD, WithinAbs(0.55f, 0.001f));
    REQUIRE_THAT(PoliticalCycleModule::OPPOSE_THRESHOLD, WithinAbs(0.35f, 0.001f));
    REQUIRE_THAT(PoliticalCycleModule::MAJORITY_THRESHOLD, WithinAbs(0.50f, 0.001f));
    REQUIRE_THAT(PoliticalCycleModule::RESOURCE_SCALE, WithinAbs(2.0f, 0.001f));
    REQUIRE_THAT(PoliticalCycleModule::RESOURCE_MAX_EFFECT, WithinAbs(0.15f, 0.001f));
}

TEST_CASE("PoliticalCycle: resource modifier at zero deployment", "[political_cycle][tier10]") {
    float mod = PoliticalCycleModule::compute_resource_modifier(0.0f);
    REQUIRE_THAT(mod, WithinAbs(0.0f, 0.001f));
}
