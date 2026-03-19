#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "modules/drug_economy/drug_economy_module.h"
#include "core/world_state/world_state.h"
#include "core/world_state/player.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

// ============================================================================
// Static utility tests
// ============================================================================

TEST_CASE("DrugEconomy: wholesale_price_fraction applied",
          "[drug_economy][tier8]") {
    // Retail spot_price 100, wholesale fraction 0.45 -> 45
    float price = DrugEconomyModule::compute_wholesale_price(100.0f, 0.45f);
    REQUIRE_THAT(price, WithinAbs(45.0f, 0.01f));
}

TEST_CASE("DrugEconomy: wholesale price zero when spot price zero",
          "[drug_economy][tier8]") {
    float price = DrugEconomyModule::compute_wholesale_price(0.0f, 0.45f);
    REQUIRE_THAT(price, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("DrugEconomy: quality degrades through distribution",
          "[drug_economy][tier8]") {
    // Production quality 0.95
    float wholesale_quality = DrugEconomyModule::degrade_quality(0.95f, 0.95f);
    REQUIRE_THAT(wholesale_quality, WithinAbs(0.9025f, 0.001f));

    float retail_quality = DrugEconomyModule::degrade_quality(wholesale_quality, 0.90f);
    REQUIRE_THAT(retail_quality, WithinAbs(0.81225f, 0.001f));
}

TEST_CASE("DrugEconomy: quality clamped to [0,1]",
          "[drug_economy][tier8]") {
    float result = DrugEconomyModule::degrade_quality(1.5f, 0.95f);
    REQUIRE(result <= 1.0f);

    float result2 = DrugEconomyModule::degrade_quality(0.0f, 0.95f);
    REQUIRE(result2 >= 0.0f);
}

TEST_CASE("DrugEconomy: addiction demand computation",
          "[drug_economy][tier8]") {
    // addiction_rate 0.05, population 100000, demand_per_addict 1.0
    float demand = DrugEconomyModule::compute_addiction_demand(0.05f, 100000, 1.0f);
    REQUIRE_THAT(demand, WithinAbs(5000.0f, 1.0f));
}

TEST_CASE("DrugEconomy: precursor consumption for meth",
          "[drug_economy][tier8]") {
    // 100 units meth output, ratio 2.0 -> 200 units precursor
    float precursor = DrugEconomyModule::compute_precursor_consumption(100.0f, 2.0f);
    REQUIRE_THAT(precursor, WithinAbs(200.0f, 0.01f));
}

TEST_CASE("DrugEconomy: drug legalization status check",
          "[drug_economy][tier8]") {
    DrugLegalizationStatus status{true, false, false, true};

    REQUIRE(DrugEconomyModule::is_drug_legal(status, DrugType::cannabis) == true);
    REQUIRE(DrugEconomyModule::is_drug_legal(status, DrugType::methamphetamine) == false);
    REQUIRE(DrugEconomyModule::is_drug_legal(status, DrugType::synthetic_opioid) == false);
    REQUIRE(DrugEconomyModule::is_drug_legal(status, DrugType::designer_drug) == true);
}

TEST_CASE("DrugEconomy: legal cannabis uses formal market",
          "[drug_economy][tier8]") {
    DrugLegalizationStatus status{true, false, false, true};
    REQUIRE(status.is_legal(DrugType::cannabis) == true);
}

TEST_CASE("DrugEconomy: illegal cannabis uses informal market",
          "[drug_economy][tier8]") {
    DrugLegalizationStatus status{false, false, false, true};
    REQUIRE(status.is_legal(DrugType::cannabis) == false);
}

TEST_CASE("DrugEconomy: meth chemical waste signature",
          "[drug_economy][tier8]") {
    // 50 units output, 0.15 waste per unit -> 7.5 -> clamped to 1.0 since max
    float waste = DrugEconomyModule::compute_meth_waste_signature(50.0f, 0.15f);
    REQUIRE(waste <= 1.0f);
    REQUIRE(waste > 0.0f);

    // Small output
    float waste_small = DrugEconomyModule::compute_meth_waste_signature(1.0f, 0.15f);
    REQUIRE_THAT(waste_small, WithinAbs(0.15f, 0.01f));
}

TEST_CASE("DrugEconomy: precursor shortage clamps production",
          "[drug_economy][tier8]") {
    // Lab needs 10 units but only 3 available
    // Output should be clamped proportionally: 3 / 2.0 ratio = 1.5 units drug
    float available_precursor = 3.0f;
    float ratio = 2.0f;
    float clamped_output = available_precursor / ratio;
    REQUIRE_THAT(clamped_output, WithinAbs(1.5f, 0.01f));
}

TEST_CASE("DrugEconomy: zero addiction rate produces zero demand",
          "[drug_economy][tier8]") {
    float demand = DrugEconomyModule::compute_addiction_demand(0.0f, 100000, 1.0f);
    REQUIRE_THAT(demand, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("DrugEconomy: wholesale price fraction constant is 0.45",
          "[drug_economy][tier8]") {
    REQUIRE_THAT(DrugEconomyModule::WHOLESALE_PRICE_FRACTION, WithinAbs(0.45f, 0.001f));
}

TEST_CASE("DrugEconomy: quality degradation factors match spec",
          "[drug_economy][tier8]") {
    // Starting quality 1.0 through wholesale (0.95) then retail (0.90)
    float after_wholesale = DrugEconomyModule::degrade_quality(1.0f, 0.95f);
    REQUIRE_THAT(after_wholesale, WithinAbs(0.95f, 0.001f));

    float after_retail = DrugEconomyModule::degrade_quality(after_wholesale, 0.90f);
    REQUIRE_THAT(after_retail, WithinAbs(0.855f, 0.001f));
}

TEST_CASE("DrugEconomy: designer drug initially legal",
          "[drug_economy][tier8]") {
    DrugLegalizationStatus status{false, false, false, true};
    REQUIRE(DrugEconomyModule::is_drug_legal(status, DrugType::designer_drug) == true);
}

TEST_CASE("DrugEconomy: all drugs illegal in strict province",
          "[drug_economy][tier8]") {
    DrugLegalizationStatus status{false, false, false, false};
    REQUIRE(DrugEconomyModule::is_drug_legal(status, DrugType::cannabis) == false);
    REQUIRE(DrugEconomyModule::is_drug_legal(status, DrugType::methamphetamine) == false);
    REQUIRE(DrugEconomyModule::is_drug_legal(status, DrugType::synthetic_opioid) == false);
    REQUIRE(DrugEconomyModule::is_drug_legal(status, DrugType::designer_drug) == false);
}
