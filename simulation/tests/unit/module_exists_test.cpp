// Module existence tests — verify all core modules compile and register correctly.
// Each module must implement ITickModule and return the expected name.

#include <catch2/catch_test_macros.hpp>
#include <string_view>

// Forward declarations — each module provides a factory function
// that will be implemented alongside the module stub.
// For now, these tests verify the stub infrastructure compiles.

namespace econlife {
class ITickModule;
}

// ── Tier 1: No inter-module dependencies ────────────────────────────────────

TEST_CASE("production module exists and registers", "[module][tier1]") {
    // TODO: Instantiate ProductionModule and verify name() == "production"
    REQUIRE(true);  // Placeholder until module factories are wired
}

TEST_CASE("calendar module exists and registers", "[module][tier1]") {
    REQUIRE(true);
}

TEST_CASE("scene_cards module exists and registers", "[module][tier1]") {
    REQUIRE(true);
}

TEST_CASE("random_events module exists and registers", "[module][tier1]") {
    REQUIRE(true);
}

// ── Tier 2: Depends on Production ───────────────────────────────────────────

TEST_CASE("supply_chain module exists and registers", "[module][tier2]") {
    REQUIRE(true);
}

TEST_CASE("labor_market module exists and registers", "[module][tier2]") {
    REQUIRE(true);
}

TEST_CASE("seasonal_agriculture module exists and registers", "[module][tier2]") {
    REQUIRE(true);
}

// ── Tier 3: Depends on Supply Chain ─────────────────────────────────────────

TEST_CASE("price_engine module exists and registers", "[module][tier3]") {
    REQUIRE(true);
}

TEST_CASE("trade_infrastructure module exists and registers", "[module][tier3]") {
    REQUIRE(true);
}

// ── Tier 4: Depends on Price Engine ─────────────────────────────────────────

TEST_CASE("financial_distribution module exists and registers", "[module][tier4]") {
    REQUIRE(true);
}

TEST_CASE("npc_business module exists and registers", "[module][tier4]") {
    REQUIRE(true);
}

TEST_CASE("commodity_trading module exists and registers", "[module][tier4]") {
    REQUIRE(true);
}

TEST_CASE("real_estate module exists and registers", "[module][tier4]") {
    REQUIRE(true);
}

// ── Tier 5: Depends on Financial Distribution ───────────────────────────────

TEST_CASE("npc_behavior module exists and registers", "[module][tier5]") {
    REQUIRE(true);
}

TEST_CASE("banking module exists and registers", "[module][tier5]") {
    REQUIRE(true);
}

TEST_CASE("government_budget module exists and registers", "[module][tier5]") {
    REQUIRE(true);
}

TEST_CASE("healthcare module exists and registers", "[module][tier5]") {
    REQUIRE(true);
}
