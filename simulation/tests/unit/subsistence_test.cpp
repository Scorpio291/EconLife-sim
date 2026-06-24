#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <memory>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"
#include "modules/subsistence/subsistence_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

namespace {
// A province with the given natural capital and a working population.
Province make_province(uint32_t idx, float ag, uint32_t population) {
    Province p{};
    p.id = idx;
    p.region_id = idx;
    p.agricultural_productivity = ag;
    p.geography.arable_land_fraction = ag;
    p.geography.forest_coverage = 0.2f;
    p.fisheries.current_stock = 0.0f;
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    p.cohort_stats->total_population = population;
    p.cohort_stats->working_age_fraction = 0.6f;
    return p;
}
}  // namespace

TEST_CASE("subsistence_output: zero without land or labour, rises toward a ceiling",
          "[subsistence][tier1]") {
    SubsistenceConfig cfg{};
    CHECK(SubsistenceModule::subsistence_output(0.0f, 1000.0f, cfg) == 0.0f);  // no land
    CHECK(SubsistenceModule::subsistence_output(1.0f, 0.0f, cfg) == 0.0f);     // no labour

    // Monotonic in labour, and bounded by the natural-capital ceiling.
    float low = SubsistenceModule::subsistence_output(1.0f, 500.0f, cfg);
    float high = SubsistenceModule::subsistence_output(1.0f, 5000.0f, cfg);
    CHECK(high > low);
    CHECK(high <= cfg.ceiling_per_capital_unit * 1.0f + 0.01f);

    // More natural capital -> proportionally more food at the same labour.
    float poor = SubsistenceModule::subsistence_output(0.5f, 2000.0f, cfg);
    float rich = SubsistenceModule::subsistence_output(1.0f, 2000.0f, cfg);
    CHECK(rich > poor);
}

TEST_CASE("surplus_ratio: produced over needed; trivially fed with no population",
          "[subsistence][tier1]") {
    SubsistenceConfig cfg{};
    cfg.per_capita_food_per_tick = 1.0f;
    CHECK_THAT(SubsistenceModule::surplus_ratio(150.0f, 100, cfg), WithinAbs(1.5f, 0.001f));
    CHECK_THAT(SubsistenceModule::surplus_ratio(60.0f, 100, cfg), WithinAbs(0.6f, 0.001f));
    CHECK(SubsistenceModule::surplus_ratio(0.0f, 0, cfg) == 1.0f);  // no mouths
}

TEST_CASE("regime gate: active across the dawn commons arc, inert in market eras",
          "[subsistence][tier1]") {
    SubsistenceModule mod;
    // The commons food path spans the whole pre-market arc (subsistence -> industrial).
    CHECK(mod.regime_active("subsistence"));
    CHECK(mod.regime_active("barter"));
    CHECK(mod.regime_active("coinage"));
    CHECK(mod.regime_active("money"));
    CHECK(mod.regime_active("feudal"));
    CHECK(mod.regime_active("mercantile"));
    CHECK(mod.regime_active("industrial"));
    CHECK_FALSE(mod.regime_active("modern"));
}

TEST_CASE("specialist ceiling rises across the pre-market arc", "[subsistence][tier1]") {
    SubsistenceModule mod;
    const float sub = mod.specialist_ceiling("subsistence");
    const float bar = mod.specialist_ceiling("barter");
    const float coin = mod.specialist_ceiling("coinage");
    const float money = mod.specialist_ceiling("money");
    const float feudal = mod.specialist_ceiling("feudal");
    const float mercantile = mod.specialist_ceiling("mercantile");
    const float industrial = mod.specialist_ceiling("industrial");
    // Money/markets, then guilds/trade/finance and the factory system, each sustain a
    // larger non-farming share: commons <= barter < coinage < money < feudal <
    // mercantile < industrial.
    CHECK(sub <= bar);
    CHECK(bar < coin);
    CHECK(coin < money);
    CHECK(money < feudal);
    CHECK(feudal < mercantile);
    CHECK(mercantile < industrial);
    // An unlisted regime falls back to the generic cap (no crash, sane default).
    SubsistenceConfig cfg{};
    CHECK_THAT(mod.specialist_ceiling("unknown_regime"),
               WithinAbs(cfg.max_specialist_fraction, 0.0001f));
}

TEST_CASE("execute_province produces a surplus at the dawn and is inert in the modern era",
          "[subsistence][tier1]") {
    SubsistenceModule mod;

    auto make_world = [](uint8_t era) {
        WorldState w{};
        w.era_catalog.load_builtin_default();
        w.technology.current_era = era;
        w.provinces.push_back(make_province(0, /*ag=*/0.8f, /*population=*/1000));
        return w;
    };

    SECTION("dawn (era 1, subsistence regime) emits a surplus signal") {
        WorldState w = make_world(1);
        DeltaBuffer delta{};
        mod.execute_province(0, w, delta);
        REQUIRE(delta.region_deltas.size() == 1);
        REQUIRE(delta.region_deltas[0].subsistence_surplus_replacement.has_value());
        // Fertile province, modest population -> comfortably above subsistence.
        CHECK(*delta.region_deltas[0].subsistence_surplus_replacement > 1.0f);
    }

    SECTION("modern anchor (era 8, market regime) is inert") {
        WorldState w = make_world(8);
        DeltaBuffer delta{};
        mod.execute_province(0, w, delta);
        CHECK(delta.region_deltas.empty());
    }
}

TEST_CASE("subsistence: surplus frees a share of livelihoods into specialists",
          "[subsistence][tier1]") {
    SubsistenceConfig cfg{};
    cfg.max_specialist_fraction = 0.5f;
    // No surplus -> everyone works the land.
    CHECK(SubsistenceModule::specialist_count(100, 1.0f, cfg) == 0);
    CHECK(SubsistenceModule::specialist_count(100, 0.7f, cfg) == 0);
    // Surplus frees a share, capped by max_specialist_fraction.
    CHECK(SubsistenceModule::specialist_count(100, 1.2f, cfg) == 20);
    CHECK(SubsistenceModule::specialist_count(100, 3.0f, cfg) == 50);  // capped at 50%
}

TEST_CASE("subsistence: a surplus dawn province assigns specialist occupations",
          "[subsistence][tier1]") {
    SubsistenceModule mod;
    WorldState w{};
    w.era_catalog.load_builtin_default();
    w.occupation_catalog.load_builtin_default();
    w.technology.current_era = 1;  // subsistence regime
    w.provinces.push_back(make_province(0, /*ag=*/0.9f, /*population=*/1000));

    // A handful of resident heads.
    w.npc_indices_by_home_province.resize(1);
    for (uint32_t i = 0; i < 10; ++i) {
        NPC n{};
        n.id = 100 + i;
        n.home_province_id = 0;
        w.significant_npcs.push_back(n);
        w.npc_indices_by_home_province[0].push_back(i);
    }

    DeltaBuffer delta{};
    mod.execute_province(0, w, delta);

    // Every resident gets a livelihood; with surplus, some are Layer-2 specialists.
    int assigned = 0, specialists = 0;
    for (const auto& nd : delta.npc_deltas) {
        if (!nd.new_occupation.has_value())
            continue;
        ++assigned;
        const OccupationDefinition* o = w.occupation_catalog.by_index(*nd.new_occupation);
        REQUIRE(o != nullptr);
        if (o->layer == 2)
            ++specialists;
    }
    CHECK(assigned == 10);
    CHECK(specialists >= 1);
}

TEST_CASE("subsistence: a food surplus accrues proto-capital to resident founders",
          "[subsistence][tier1]") {
    SubsistenceModule mod;

    // Dawn world, fertile province with a resident significant NPC (a "head").
    WorldState w{};
    w.era_catalog.load_builtin_default();
    w.technology.current_era = 1;  // subsistence regime
    w.provinces.push_back(make_province(0, /*ag=*/0.9f, /*population=*/1000));

    NPC head{};
    head.id = 42;
    head.home_province_id = 0;
    head.capital = 0.0f;
    w.significant_npcs.push_back(head);
    w.npc_indices_by_home_province.resize(1);
    w.npc_indices_by_home_province[0].push_back(0);  // index of the head in significant_npcs

    DeltaBuffer delta{};
    mod.execute_province(0, w, delta);

    // Surplus province -> the resident head accrues stored proto-capital.
    float credited = 0.0f;
    for (const auto& nd : delta.npc_deltas) {
        if (nd.npc_id == 42 && nd.capital_delta.has_value())
            credited += *nd.capital_delta;
    }
    CHECK(credited > 0.0f);

    SECTION("a deficit province accrues nothing") {
        WorldState poor{};
        poor.era_catalog.load_builtin_default();
        poor.technology.current_era = 1;
        // Tiny natural capital + large population -> deficit (surplus < 1).
        poor.provinces.push_back(make_province(0, /*ag=*/0.01f, /*population=*/50000));
        NPC h{};
        h.id = 7;
        h.home_province_id = 0;
        poor.significant_npcs.push_back(h);
        poor.npc_indices_by_home_province.resize(1);
        poor.npc_indices_by_home_province[0].push_back(0);
        DeltaBuffer d{};
        mod.execute_province(0, poor, d);
        for (const auto& nd : d.npc_deltas)
            CHECK_FALSE(nd.capital_delta.has_value());
    }
}
