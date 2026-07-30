#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/apply_deltas.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"
#include "modules/energy_base/energy_base_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// ===========================================================================
// GHOST ACRES — the escape from the organic economy, and its end.
//
// An organic economy is bounded by photosynthesis on finite acres: food, fodder,
// firewood and charcoal all compete for the same ground. Coal breaks that bound
// by substituting a stock for the flow. England and Wales drew 4.3M
// acre-equivalents from coal in 1750 and 48.1M by 1850 — more than the ~37M
// acres of their entire land surface.
//
// The escape is finite by construction: the coal is a located deposit, so a
// society that industrialises is spending something, and when the seam is worked
// out the ceiling falls back to what the sun puts on its fields.
// ===========================================================================

namespace {

EnergyBaseConfig cfg() { return EnergyBaseConfig{}; }

ResourceDeposit coal(uint32_t id, float quantity, float quality, float depth,
                     float accessibility) {
    ResourceDeposit d{};
    d.id = id;
    d.type = ResourceType::Coal;
    d.quantity = quantity;
    d.quantity_remaining = quantity;
    d.quality = quality;
    d.depth = depth;
    d.accessibility = accessibility;
    return d;
}

// A one-province industrial-era world with a seam under it.
WorldState coal_world(float knowledge, float surplus, uint32_t population, float quantity = 3000.0f,
                      float quality = 0.8f, float depth = 0.2f, float accessibility = 0.9f) {
    WorldState w{};
    w.current_tick = kTicksPerYear;  // an annual tick: coal is raised over a year
    w.world_seed = 1;
    w.era_catalog.load_builtin_default();
    w.technology.current_era = 6;  // industrial — a commons regime
    w.technology.knowledge_level = knowledge;  // the world's frontier
    Province p{};
    p.id = 0;
    p.region_id = 0;
    p.geography.area_km2 = 1800.0f;
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    p.cohort_stats->total_population = population;
    // R6: mining technique is what THIS province commands, not what the world knows.
    p.cohort_stats->knowledge_level = knowledge;
    p.cohort_stats->subsistence_surplus_ratio = surplus;
    p.cohort_stats->productive_capital = 1.0e6f;
    if (quantity > 0.0f)
        p.deposits.push_back(coal(1, quantity, quality, depth, accessibility));
    w.provinces.push_back(std::move(p));
    return w;
}

float run_year(WorldState& w, EnergyBaseModule& mod) {
    DeltaBuffer d{};
    mod.execute_province(0, w, d);
    apply_deltas(w, d);
    return w.provinces[0].cohort_stats->coal_burned_per_year;
}

}  // namespace

TEST_CASE("energy_base: rich shallow reachable coal is cheap, poor deep stranded coal is dear",
          "[energy_base][ghost_acres]") {
    // The fuel price in the induced-innovation ratio is a property of the rock, not a
    // dial: grade, depth and access are all seeded by world-gen from the geology.
    const auto easy = EnergyBaseModule::seam_workability(coal(1, 1000.0f, 0.9f, 0.1f, 0.9f));
    const auto hard = EnergyBaseModule::seam_workability(coal(2, 1000.0f, 0.3f, 0.9f, 0.2f));
    CHECK(easy > hard);
    CHECK(easy <= 1.0f);
    CHECK(hard > 0.0f);
}

TEST_CASE("energy_base: a Neolithic society cannot mine coal however much is under it",
          "[energy_base][ghost_acres]") {
    // Outcrop coal was picked off hillsides for centuries before anybody could ventilate
    // and drain a drowned seam. Technique is the gate, and it saturates rather than
    // switching, so there is no year in which mining is suddenly invented.
    const auto c = cfg();
    const float dawn = EnergyBaseModule::mining_technique(0.0f, c);
    const float classical = EnergyBaseModule::mining_technique(c.mining_technique_halfsat, c);
    const float victorian = EnergyBaseModule::mining_technique(50.0f * c.mining_technique_halfsat, c);

    CHECK_THAT(dawn, WithinAbs(0.0f, 1e-6f));
    CHECK_THAT(classical, WithinAbs(0.5f, 1e-3f));
    CHECK(victorian > 0.95f);
    CHECK(victorian < 1.0f);  // saturating: approached, never reached
}

TEST_CASE("energy_base: coal is adopted where labour is dear and fuel cheap",
          "[energy_base][ghost_acres][induced]") {
    // Allen's argument, and the answer to why the same knowledge industrialised Britain
    // and not China: the machines have to PAY. A society at bare subsistence with deep,
    // poor coal adopts almost none of it however well it understands the technique.
    const auto c = cfg();
    const float britain = EnergyBaseModule::coal_adoption(/*wage=*/2.0f, /*workability=*/0.7f, c);
    const float cheap_labour = EnergyBaseModule::coal_adoption(/*wage=*/0.8f, 0.7f, c);
    const float dear_fuel = EnergyBaseModule::coal_adoption(/*wage=*/2.0f, 0.05f, c);

    CHECK(britain > cheap_labour);
    CHECK(dear_fuel > britain);  // unworkable coal makes every alternative look dear...
    CHECK(britain > 0.5f);
    CHECK(cheap_labour < britain);
    // ...but adoption is only ever a share of demand, never more than all of it.
    CHECK(dear_fuel < 1.0f);
}

TEST_CASE("energy_base: burning coal adds land that was never there",
          "[energy_base][ghost_acres]") {
    // The historical anchors, straight from Wrigley's acre-equivalent series against
    // English coal output. Per head, England drew ~0.7 ghost acres in 1750, 1.24 in
    // 1800 and 2.64 by 1850 — against a pre-industrial standard of ~4.1 real acres a
    // head, an index running 0.17 -> 0.30 -> 0.64 over the century in which coal remade
    // the country. (Against the land England ACTUALLY had by 1850, which had halved per
    // head as the population doubled, the same 48.1M ghost acres exceeded the entire
    // 37M-acre surface.)
    const auto c = cfg();
    const uint32_t people = 1000000;
    auto index_at = [&](float tonnes_per_head) {
        return EnergyBaseModule::ghost_land_fraction(static_cast<float>(people) * tonnes_per_head,
                                                     people, c);
    };
    // Tonnages are the rough contemporary output series, so 10% is the honest
    // tolerance — the acre-equivalents are the primary figure and the tonnages are
    // what Wrigley converted from.
    const float england_1750 = index_at(5.0f / 6.0f);    // ~5M tonnes, ~6M people
    const float england_1800 = index_at(15.0f / 9.0f);   // ~15M tonnes, ~9M people
    const float england_1850 = index_at(60.0f / 18.0f);  // ~60M tonnes, ~18M people

    CHECK_THAT(england_1750, WithinRel(0.17f, 0.10f));
    CHECK_THAT(england_1800, WithinRel(0.30f, 0.10f));
    CHECK_THAT(england_1850, WithinRel(0.64f, 0.10f));
    CHECK_THAT(EnergyBaseModule::ghost_land_fraction(0.0f, people, c), WithinAbs(0.0f, 1e-9f));
}

TEST_CASE("energy_base: the seam is finite, and the escape ends with it",
          "[energy_base][ghost_acres]") {
    // The whole point. A society that industrialises is SPENDING something it cannot
    // replace: burn long enough and the ground is empty, the ghost acres vanish, and the
    // carrying ceiling falls back to what the sun puts on the fields.
    EnergyBaseModule mod;
    WorldState w = coal_world(/*knowledge=*/5.0e6f, /*surplus=*/1.6f, /*population=*/2000000,
                              /*quantity=*/2.0f);  // a nearly worked-out seam

    const float first = run_year(w, mod);
    CHECK(first > 0.0f);
    CHECK(w.provinces[0].cohort_stats->ghost_land_fraction > 0.0f);

    // Keep burning until the ground is empty.
    for (int year = 0; year < 20; ++year) {
        w.current_tick += kTicksPerYear;
        run_year(w, mod);
    }
    CHECK_THAT(w.provinces[0].deposits[0].quantity_remaining, WithinAbs(0.0f, 1e-3f));
    CHECK_THAT(w.provinces[0].cohort_stats->coal_burned_per_year, WithinAbs(0.0f, 1e-3f));
    CHECK_THAT(w.provinces[0].cohort_stats->ghost_land_fraction, WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("energy_base: every tonne burned comes out of the ground it came from",
          "[energy_base][ghost_acres][conservation]") {
    // Conserved and located. Nothing appears from nowhere: the drawdown on the named
    // deposit matches the published burn exactly, in the deposit's own units.
    EnergyBaseModule mod;
    WorldState w = coal_world(/*knowledge=*/5.0e6f, /*surplus=*/1.6f, /*population=*/500000);
    const float before = w.provinces[0].deposits[0].quantity_remaining;

    const float burned = run_year(w, mod);
    const float after = w.provinces[0].deposits[0].quantity_remaining;

    REQUIRE(burned > 0.0f);
    CHECK_THAT(before - after, WithinRel(burned / cfg().tonnes_per_deposit_unit, 1e-3f));
}

TEST_CASE("energy_base: a province with no coal has no escape from its acres",
          "[energy_base][ghost_acres]") {
    // Which is most of them. Coal is unevenly distributed, and a society sitting on
    // none of it stays in the organic economy however clever it becomes.
    EnergyBaseModule mod;
    WorldState w = coal_world(/*knowledge=*/5.0e6f, /*surplus=*/1.6f, /*population=*/2000000,
                              /*quantity=*/0.0f);
    CHECK_THAT(run_year(w, mod), WithinAbs(0.0f, 1e-9f));
    CHECK_THAT(w.provinces[0].cohort_stats->ghost_land_fraction, WithinAbs(0.0f, 1e-9f));
}

TEST_CASE("energy_base: inert in market eras", "[energy_base][ghost_acres]") {
    // The commons food economy is where the carrying ceiling lives; the modern market
    // economy is untouched by this module.
    EnergyBaseModule mod;
    WorldState w = coal_world(/*knowledge=*/5.0e6f, /*surplus=*/1.6f, /*population=*/2000000);
    w.technology.current_era = 8;  // modern (market regime)

    DeltaBuffer d{};
    mod.execute_province(0, w, d);
    CHECK(d.region_deltas.empty());
    CHECK(d.deposit_deltas.empty());
}
