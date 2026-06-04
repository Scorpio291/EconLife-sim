#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/weapons_trafficking/weapons_trafficking_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

// ============================================================================
// Static utility tests
// ============================================================================

TEST_CASE("WeaponsTrafficking: informal spot price basic computation",
          "[weapons_trafficking][tier8]") {
    // base 500, no conflict, supply 10, floor 1.0
    // price = 500 * (1 + 0) / 10 = 50
    float price = WeaponsTraffickingModule::compute_informal_spot_price(500.0f, 0.0f, 10.0f, 1.0f);
    REQUIRE_THAT(price, WithinAbs(50.0f, 0.01f));
}

TEST_CASE("WeaponsTrafficking: price increases with territorial conflict",
          "[weapons_trafficking][tier8]") {
    // personnel_violence modifier = 0.8
    // price = 500 * (1 + 0.8) / 10 = 500 * 1.8 / 10 = 90
    float price = WeaponsTraffickingModule::compute_informal_spot_price(500.0f, 0.8f, 10.0f, 1.0f);
    REQUIRE_THAT(price, WithinAbs(90.0f, 0.01f));
}

TEST_CASE("WeaponsTrafficking: price floor prevents division by zero",
          "[weapons_trafficking][tier8]") {
    // supply = 0.0, floor = 1.0 -> uses floor
    float price = WeaponsTraffickingModule::compute_informal_spot_price(500.0f, 0.0f, 0.0f, 1.0f);
    REQUIRE_THAT(price, WithinAbs(500.0f, 0.01f));
}

TEST_CASE("WeaponsTrafficking: open_warfare gives 1.5x demand modifier",
          "[weapons_trafficking][tier8]") {
    float mod = WeaponsTraffickingModule::get_conflict_demand_modifier(5);  // open_warfare
    REQUIRE_THAT(mod, WithinAbs(1.5f, 0.01f));
}

TEST_CASE("WeaponsTrafficking: no conflict gives zero demand modifier",
          "[weapons_trafficking][tier8]") {
    float mod = WeaponsTraffickingModule::get_conflict_demand_modifier(0);  // none
    REQUIRE_THAT(mod, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("WeaponsTrafficking: conflict demand modifier mapping", "[weapons_trafficking][tier8]") {
    REQUIRE_THAT(WeaponsTraffickingModule::get_conflict_demand_modifier(0), WithinAbs(0.0f, 0.01f));
    REQUIRE_THAT(WeaponsTraffickingModule::get_conflict_demand_modifier(1), WithinAbs(0.2f, 0.01f));
    REQUIRE_THAT(WeaponsTraffickingModule::get_conflict_demand_modifier(2), WithinAbs(0.2f, 0.01f));
    REQUIRE_THAT(WeaponsTraffickingModule::get_conflict_demand_modifier(3), WithinAbs(0.5f, 0.01f));
    REQUIRE_THAT(WeaponsTraffickingModule::get_conflict_demand_modifier(4), WithinAbs(0.8f, 0.01f));
    REQUIRE_THAT(WeaponsTraffickingModule::get_conflict_demand_modifier(5), WithinAbs(1.5f, 0.01f));
    REQUIRE_THAT(WeaponsTraffickingModule::get_conflict_demand_modifier(6), WithinAbs(0.0f, 0.01f));
}

TEST_CASE("WeaponsTrafficking: diversion output computation", "[weapons_trafficking][tier8]") {
    // total 100, diversion 0.20, max 0.30 -> 20 diverted
    float diverted = WeaponsTraffickingModule::compute_diversion_output(100.0f, 0.20f, 0.30f);
    REQUIRE_THAT(diverted, WithinAbs(20.0f, 0.01f));
}

TEST_CASE("WeaponsTrafficking: diversion fraction clamped to maximum",
          "[weapons_trafficking][tier8]") {
    // requested 0.50, max 0.30 -> clamped to 0.30
    float clamped = WeaponsTraffickingModule::clamp_diversion_fraction(0.50f, 0.30f);
    REQUIRE_THAT(clamped, WithinAbs(0.30f, 0.01f));
}

TEST_CASE("WeaponsTrafficking: diversion fraction zero gives zero output",
          "[weapons_trafficking][tier8]") {
    float diverted = WeaponsTraffickingModule::compute_diversion_output(100.0f, 0.0f, 0.30f);
    REQUIRE_THAT(diverted, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("WeaponsTrafficking: heavy_weapons is embargo item", "[weapons_trafficking][tier8]") {
    REQUIRE(WeaponsTraffickingModule::is_embargo_item(WeaponType::heavy_weapons) == true);
    REQUIRE(WeaponsTraffickingModule::is_embargo_item(WeaponType::small_arms) == false);
    REQUIRE(WeaponsTraffickingModule::is_embargo_item(WeaponType::ammunition) == false);
    REQUIRE(WeaponsTraffickingModule::is_embargo_item(WeaponType::converted_legal) == false);
}

TEST_CASE("WeaponsTrafficking: chain custody actionability clamped",
          "[weapons_trafficking][tier8]") {
    float action = WeaponsTraffickingModule::compute_chain_custody_actionability(0.60f);
    REQUIRE_THAT(action, WithinAbs(0.60f, 0.01f));

    float clamped = WeaponsTraffickingModule::compute_chain_custody_actionability(1.5f);
    REQUIRE_THAT(clamped, WithinAbs(1.0f, 0.01f));
}

TEST_CASE("WeaponsTrafficking: embargo meter spike default is 0.25",
          "[weapons_trafficking][tier8]") {
    REQUIRE_THAT(WeaponsTraffickingConfig{}.embargo_meter_spike, WithinAbs(0.25f, 0.001f));
}

TEST_CASE("WeaponsTrafficking: max diversion fraction default is 0.30",
          "[weapons_trafficking][tier8]") {
    REQUIRE_THAT(WeaponsTraffickingConfig{}.max_diversion_fraction, WithinAbs(0.30f, 0.001f));
}

TEST_CASE("WeaponsTrafficking: price with maximum conflict and low supply",
          "[weapons_trafficking][tier8]") {
    // open_warfare (1.5), supply 1.0 (floor)
    // price = 500 * (1 + 1.5) / 1.0 = 1250
    float price = WeaponsTraffickingModule::compute_informal_spot_price(500.0f, 1.5f, 1.0f, 1.0f);
    REQUIRE_THAT(price, WithinAbs(1250.0f, 0.01f));
}

TEST_CASE("WeaponsTrafficking: no domestic heavy weapons production",
          "[weapons_trafficking][tier8]") {
    // Heavy weapons are import only in V1 — verify constant exists
    REQUIRE(WeaponsTraffickingModule::is_embargo_item(WeaponType::heavy_weapons) == true);
    // No production recipe validation — just verify the type flag
}

// --- Execute-path smoke coverage ------------------------------------------
// The cases above exercise only static price/diversion helpers. This drives
// execute_province on a minimal world: a non-compliant manufacturing business
// in a high-dominance province should divert weapons into the informal market
// (a MarketDelta for small_arms) rather than silently doing nothing.

TEST_CASE("WeaponsTrafficking: execute_province diverts weapons to informal supply",
          "[weapons_trafficking][tier8]") {
    WorldState w{};
    w.current_tick = 1;
    w.world_seed = 1;

    Province p{};
    p.id = 0;
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    p.cohort_stats->criminal_dominance_index = 0.85f;  // open-warfare demand
    w.provinces.push_back(std::move(p));

    NPCBusiness biz{};
    biz.id = 1;
    biz.sector = BusinessSector::manufacturing;
    biz.province_id = 0;
    biz.revenue_per_tick = 1000.0f;
    biz.regulatory_violation_severity = 0.5f;  // proxy for diversion
    w.npc_businesses.push_back(biz);

    WeaponsTraffickingModule module;
    DeltaBuffer delta{};
    module.execute_province(0, w, delta);

    REQUIRE_FALSE(delta.market_deltas.empty());
    const MarketDelta& md = delta.market_deltas[0];
    REQUIRE(md.good_id == static_cast<uint32_t>(WeaponType::small_arms));
    REQUIRE(md.region_id == 0);
    REQUIRE(md.supply_delta.has_value());
    REQUIRE(*md.supply_delta > 0.0f);
}

TEST_CASE("WeaponsTrafficking: compliant manufacturer diverts nothing",
          "[weapons_trafficking][tier8]") {
    WorldState w{};
    w.current_tick = 1;
    Province p{};
    p.id = 0;
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    p.cohort_stats->criminal_dominance_index = 0.85f;
    w.provinces.push_back(std::move(p));

    NPCBusiness biz{};
    biz.id = 1;
    biz.sector = BusinessSector::manufacturing;
    biz.province_id = 0;
    biz.revenue_per_tick = 1000.0f;
    biz.regulatory_violation_severity = 0.0f;  // compliant -> not a diverter
    w.npc_businesses.push_back(biz);

    WeaponsTraffickingModule module;
    DeltaBuffer delta{};
    module.execute_province(0, w, delta);

    // The conflict still drives a demand-side MarketDelta, but a compliant
    // manufacturer must divert no weapons: no supply_delta should be emitted,
    // and no inventory-discrepancy evidence generated.
    bool any_supply = false;
    for (const auto& md : delta.market_deltas) {
        if (md.supply_delta.has_value())
            any_supply = true;
    }
    REQUIRE_FALSE(any_supply);
    REQUIRE(delta.evidence_deltas.empty());
}

// =============================================================================
// Embargo investigation on heavy/embargo-level diversion (execute path)
// =============================================================================

TEST_CASE("WeaponsTrafficking: heavy diversion opens a monthly embargo investigation",
          "[weapons_trafficking][tier8]") {
    WorldState state{};
    state.world_seed = 1;
    Province prov{};
    prov.id = 0;
    prov.cohort_stats = std::make_unique<RegionCohortStats>();
    state.provinces.push_back(std::move(prov));

    NPCBusiness biz{};
    biz.id = 7;
    biz.owner_id = 42;
    biz.province_id = 0;
    biz.sector = BusinessSector::manufacturing;
    biz.regulatory_violation_severity = 0.80f;  // >= embargo_severity_threshold (0.70)
    biz.revenue_per_tick = 1000.0f;
    state.npc_businesses.push_back(biz);

    WeaponsTraffickingModule module;

    // Monthly tick -> embargo investigation consequence against the owner.
    state.current_tick = 30;
    DeltaBuffer d{};
    module.execute_province(0, state, d);
    bool embargo = false;
    for (const auto& c : d.consequence_deltas) {
        if (c.new_consequence.has_value() &&
            c.new_consequence->category == ConsequenceCategory::criminal_investigation &&
            c.new_consequence->target_id == 42) {
            embargo = true;
        }
    }
    REQUIRE(embargo);

    // Off the monthly cadence -> no embargo consequence (avoids flooding courts).
    state.current_tick = 5;
    DeltaBuffer d2{};
    module.execute_province(0, state, d2);
    bool embargo2 = false;
    for (const auto& c : d2.consequence_deltas)
        if (c.new_consequence.has_value())
            embargo2 = true;
    REQUIRE_FALSE(embargo2);
}
