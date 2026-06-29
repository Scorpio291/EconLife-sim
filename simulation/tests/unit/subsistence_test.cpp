#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <memory>

#include "core/rng/deterministic_rng.h"
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
        w.hazard_settings.seasonality = 0.0f;  // isolate from episodic harvest failures (M6a)
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
    w.hazard_settings.seasonality = 0.0f;  // isolate from episodic harvest failures (M6a)
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
    w.hazard_settings.seasonality = 0.0f;  // isolate from episodic harvest failures (M6a)
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

TEST_CASE("manorialism: stratified regimes only", "[subsistence][tier1]") {
    SubsistenceModule mod;
    // The egalitarian commons stays even; the stratified regimes concentrate.
    CHECK_FALSE(mod.regime_manorial("subsistence"));
    CHECK_FALSE(mod.regime_manorial("barter"));
    CHECK_FALSE(mod.regime_manorial("coinage"));
    CHECK_FALSE(mod.regime_manorial("money"));
    CHECK(mod.regime_manorial("feudal"));
    CHECK(mod.regime_manorial("mercantile"));
    CHECK(mod.regime_manorial("industrial"));
}

TEST_CASE("manorialism: tithe concentrates proto-capital to lords, conserved",
          "[subsistence][tier1]") {
    SubsistenceConfig cfg{};  // tithe 0.5, lord_fraction 0.1
    const uint32_t n = 10;
    const float total = 100.0f;

    // Commons (manorial=false): perfectly even.
    for (uint32_t i = 0; i < n; ++i)
        CHECK_THAT(SubsistenceModule::proto_share_for(i, n, total, false, cfg),
                   WithinAbs(10.0f, 1e-4f));

    // Manorial: 1 lord (round(0.1*10)) takes the tithe; peasants share the rest.
    const float lord = SubsistenceModule::proto_share_for(0, n, total, true, cfg);
    const float peasant = SubsistenceModule::proto_share_for(5, n, total, true, cfg);
    CHECK(lord > peasant);             // the lord/peasant divide
    CHECK(peasant < 10.0f);            // peasants get less than the even commons share
    CHECK_THAT(lord, WithinAbs(55.0f, 0.5f));   // 5 base + 50 tithe
    CHECK_THAT(peasant, WithinAbs(5.0f, 0.5f));

    // CONSERVED: the skewed distribution sums to the same total proto-capital.
    float sum = 0.0f;
    for (uint32_t i = 0; i < n; ++i)
        sum += SubsistenceModule::proto_share_for(i, n, total, true, cfg);
    CHECK_THAT(sum, WithinAbs(total, 1e-3f));
}

TEST_CASE("harvest failures: episodic, scaled by the seasonality dial", "[subsistence][tier1]") {
    SubsistenceConfig cfg{};

    // A world with no seasonal swing never has a failed harvest.
    for (uint32_t s = 0; s < 200; ++s) {
        DeterministicRNG rng(s);
        CHECK(SubsistenceModule::harvest_failure_factor(0.0f, rng, cfg) == 1.0f);
    }

    // Failure probability rises with the seasonality dial.
    auto fail_rate = [&](float seasonality) {
        int hits = 0;
        const int N = 5000;
        for (int s = 0; s < N; ++s) {
            DeterministicRNG rng(static_cast<uint64_t>(s) * 2246822519ull + 3u);
            if (SubsistenceModule::harvest_failure_factor(seasonality, rng, cfg) < 1.0f)
                ++hits;
        }
        return static_cast<double>(hits) / N;
    };
    CHECK(fail_rate(1.0f) > fail_rate(0.3f));
    CHECK(fail_rate(0.3f) > 0.0);

    // A bad harvest cuts output; the cut is the deterministic formula and never negative.
    const float expected = 1.0f - cfg.seasonality_failure_severity * 1.0f;  // seasonality=1
    bool saw = false;
    for (uint32_t s = 0; s < 500; ++s) {
        DeterministicRNG rng(s);
        const float f = SubsistenceModule::harvest_failure_factor(1.0f, rng, cfg);
        CHECK(f <= 1.0f);
        CHECK(f >= 0.0f);
        if (f < 1.0f) {
            CHECK_THAT(f, WithinAbs(expected, 1e-4f));
            saw = true;
        }
    }
    CHECK(saw);
}

TEST_CASE("chronic hazards: predators (waning) and atmosphere (planetary) cut food",
          "[subsistence][tier1]") {
    SubsistenceConfig cfg{};
    // A predator-free / atmosphere-pristine world has no penalty.
    CHECK(SubsistenceModule::predator_food_factor(0.0f, 0.0f, cfg) == 1.0f);
    CHECK(SubsistenceModule::atmosphere_ceiling_factor(0.0f, cfg) == 1.0f);

    // Predators cut food, and the cut WANES as accumulated knowledge clears them.
    const float dawn = SubsistenceModule::predator_food_factor(1.0f, 0.0f, cfg);      // no knowledge
    const float advanced = SubsistenceModule::predator_food_factor(1.0f, 1e6f, cfg);  // high knowledge
    CHECK(dawn < 1.0f);
    CHECK(advanced > dawn);    // technique clears predators
    CHECK(advanced > 0.99f);   // ~fully cleared
    // At the half-saturation knowledge, predator pressure is ~half.
    const float half =
        SubsistenceModule::predator_food_factor(1.0f, cfg.predator_clearance_halfsat, cfg);
    CHECK_THAT(half, WithinAbs(1.0f - cfg.predator_food_penalty * 0.5f, 1e-3f));

    // A hostile atmosphere caps the ceiling (planetary; no knowledge term).
    CHECK_THAT(SubsistenceModule::atmosphere_ceiling_factor(1.0f, cfg),
               WithinAbs(1.0f - cfg.atmosphere_cap_penalty, 1e-4f));
    CHECK(SubsistenceModule::atmosphere_ceiling_factor(1.0f, cfg) <
          SubsistenceModule::atmosphere_ceiling_factor(0.3f, cfg));
}
