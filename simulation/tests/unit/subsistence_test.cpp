#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <memory>

#include "core/rng/deterministic_rng.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"
#include "core/world_state/apply_deltas.h"
#include "modules/subsistence/subsistence_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

namespace {
// A province with the given natural capital and a working population. `arable` is the
// share of the province that is actually worked, and it is SEPARATE from fertility on
// purpose: fertility sets what an acre yields, arable sets how many acres the hands
// have to cover, and their RATIO is what a pair of hands is worth (see
// SubsistenceModule::subsistence_output).
//
// It defaulted to `ag` — a place exactly as extensive as it is good — which is not what
// the generator makes and not what people settle. World-gen puts arable at 0.15-0.75
// (mean ~0.35) largely independently of fertility, so the default here is a worked core
// of that size: fertile ground the hands can actually cover, which is the property that
// makes a valley worth farming in the first place.
Province make_province(uint32_t idx, float ag, uint32_t population, float arable = 0.30f) {
    Province p{};
    p.id = idx;
    p.region_id = idx;
    p.agricultural_productivity = ag;
    p.geography.arable_land_fraction = arable;
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
    CHECK(SubsistenceModule::subsistence_output(0.0f, 1.0f, 1000.0f, cfg) == 0.0f);  // no land
    CHECK(SubsistenceModule::subsistence_output(1.0f, 1.0f, 0.0f, cfg) == 0.0f);     // no labour

    // Monotonic in labour, and bounded by the natural-capital ceiling.
    float low = SubsistenceModule::subsistence_output(1.0f, 1.0f, 500.0f, cfg);
    float high = SubsistenceModule::subsistence_output(1.0f, 1.0f, 500000.0f, cfg);
    CHECK(high > low);
    CHECK(high <= cfg.ceiling_per_capital_unit * 1.0f + 0.01f);

    // More natural capital -> proportionally more food at the same labour.
    float poor = SubsistenceModule::subsistence_output(0.5f, 1.0f, 2000.0f, cfg);
    float rich = SubsistenceModule::subsistence_output(1.0f, 1.0f, 2000.0f, cfg);
    CHECK(rich > poor);
}

TEST_CASE("subsistence_output: hands are spent by the acre, harvest is taken by the quality",
          "[subsistence][tier1][no-rails]") {
    // The two arguments are not interchangeable and the difference is physical.
    SubsistenceConfig cfg{};
    constexpr float kBand = 400.0f;  // a thin population, far below saturation

    // SAME ground, better soil: the same hands walk the same fields and carry more
    // home. A band on a river valley eats better than one on scrubland.
    const float scrub = SubsistenceModule::subsistence_output(0.4f, 1.0f, kBand, cfg);
    const float valley = SubsistenceModule::subsistence_output(1.2f, 1.0f, kBand, cfg);
    CHECK(valley > scrub * 2.5f);

    // SAME total yield spread over more ground: more walking for the same harvest, so
    // a thin population brings in less. This is why extensive land is not free.
    const float compact = SubsistenceModule::subsistence_output(1.0f, 0.5f, kBand, cfg);
    const float spread = SubsistenceModule::subsistence_output(1.0f, 2.0f, kBand, cfg);
    CHECK(spread < compact);

    // And at saturation the ground stops mattering: enough hands work all of it, and
    // what is left is the ceiling, which is the yield.
    constexpr float kCrowd = 500000.0f;
    CHECK_THAT(SubsistenceModule::subsistence_output(1.0f, 0.5f, kCrowd, cfg),
               WithinAbs(SubsistenceModule::subsistence_output(1.0f, 2.0f, kCrowd, cfg), 1.0f));
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
        // A river valley: good soil on a worked core 600 pairs of hands can actually
        // cover. A thin population spread over a whole province of the same fertility
        // would NOT be above subsistence — it cannot reach most of its land — and that
        // is the production law working, not a fixture being generous.
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

    // Enough resident heads that a stratum of a few percent is expressible as whole
    // people: with the stratum now a slowly-forming stock rather than this year's food
    // balance, ten residents times a ~9% share truncates to zero specialists.
    w.npc_indices_by_home_province.resize(1);
    for (uint32_t i = 0; i < 40; ++i) {
        NPC n{};
        n.id = 100 + i;
        n.home_province_id = 0;
        w.significant_npcs.push_back(n);
        w.npc_indices_by_home_province[0].push_back(i);
    }

    // The non-farming stratum is a STOCK with generational inertia, not this year's
    // food balance, so it has to be given time to form — a society does not raise a
    // priesthood and a smithy in a single season. Run a century of ticks.
    for (uint32_t t = 1; t <= 100u * kTicksPerYear; ++t) {
        w.current_tick = t;
        DeltaBuffer d{};
        mod.execute_province(0, w, d);
        apply_deltas(w, d);
    }

    // Inspect the livelihoods the residents actually HOLD, not this tick's deltas: the
    // module only publishes an occupation when it CHANGES, so once the century has
    // settled a steady state it correctly emits nothing.
    INFO("specialist_fraction after a century: "
         << w.provinces[0].cohort_stats->specialist_fraction);
    int assigned = 0, specialists = 0;
    for (const auto& npc : w.significant_npcs) {
        if (npc.occupation == 0)
            continue;
        ++assigned;
        const OccupationDefinition* o = w.occupation_catalog.by_index(npc.occupation);
        REQUIRE(o != nullptr);
        if (o->layer == 2)
            ++specialists;
    }
    CHECK(assigned == 40);     // everyone has a livelihood
    CHECK(specialists >= 1);   // and the surplus supports a non-farming stratum
}

TEST_CASE("subsistence: the non-farming stratum forms and sheds on a generational clock",
          "[subsistence][tier2][inertia]") {
    // It used to be recomputed from each year's harvest, so it appeared and vanished in
    // a single tick — which made every collapse instantaneous and total (measured 17%
    // -> 0%) and made elite overproduction impossible to express. Scholars, priests and
    // townsmen persist through lean decades on stores, patronage and tribute.
    SubsistenceModule mod;
    WorldState w{};
    w.era_catalog.load_builtin_default();
    w.occupation_catalog.load_builtin_default();
    w.technology.current_era = 1;
    w.hazard_settings.seasonality = 0.0f;
    w.provinces.push_back(make_province(0, /*ag=*/0.9f, /*population=*/1000));
    rebuild_npc_indices(w);

    // The stratum moves per TICK, so years must be stepped as ticks.
    uint32_t tick = 0;
    auto run = [&](uint32_t years) {
        for (uint32_t t = 0; t < years * kTicksPerYear; ++t) {
            w.current_tick = ++tick;
            DeltaBuffer d{};
            mod.execute_province(0, w, d);
            apply_deltas(w, d);
        }
        return w.provinces[0].cohort_stats->specialist_fraction;
    };

    // It does not appear overnight.
    const float after_one_year = run(1);
    CHECK(after_one_year < 0.02f);
    // Over a century it builds toward what the harvest supports.
    const float after_century = run(99);
    CHECK(after_century > after_one_year * 5.0f);

    // Now starve the province: the land cannot feed this many. The stratum falls, but
    // over decades rather than instantly — which is exactly what lets a society
    // overshoot instead of self-correcting.
    w.provinces[0].cohort_stats->total_population = 400000;
    const float after_shock = run(1);
    CHECK(after_shock > after_century * 0.5f);  // still standing a year later
    const float after_decades = run(60);
    CHECK(after_decades < after_shock);          // but shedding
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
    const uint32_t lords = SubsistenceModule::lord_count(n, cfg);
    CHECK(lords == 1);  // round(0.1 x 10)
    const float total = 100.0f;

    // Commons (manorial=false): perfectly even.
    CHECK_THAT(SubsistenceModule::proto_share_for(false, 0, n, total, false, cfg),
               WithinAbs(10.0f, 1e-4f));

    // Manorial: the lord takes the tithe; peasants share the rest.
    const float lord = SubsistenceModule::proto_share_for(true, lords, n, total, true, cfg);
    const float peasant = SubsistenceModule::proto_share_for(false, lords, n, total, true, cfg);
    CHECK(lord > peasant);             // the lord/peasant divide
    CHECK(peasant < 10.0f);            // peasants get less than the even commons share
    CHECK_THAT(lord, WithinAbs(55.0f, 0.5f));   // 5 base + 50 tithe
    CHECK_THAT(peasant, WithinAbs(5.0f, 0.5f));

    // CONSERVED: lords x lord-share + peasants x peasant-share == the same total.
    const float sum = static_cast<float>(lords) * lord + static_cast<float>(n - lords) * peasant;
    CHECK_THAT(sum, WithinAbs(total, 1e-3f));
}

TEST_CASE("manorialism: lordship is EMERGENT — the wealthiest resident collects the tithe",
          "[subsistence][tier1]") {
    // Ten resident heads in a feudal province; one of them (id 107, mid-list) is far
    // richer than the rest. The tithe must flow to HIM — rank, not array position.
    SubsistenceModule mod;
    WorldState w{};
    w.era_catalog.load_builtin_default();
    w.occupation_catalog.load_builtin_default();
    w.technology.current_era = 5;  // feudal (manorial)
    w.hazard_settings.seasonality = 0.0f;  // no harvest-failure noise
    w.provinces.push_back(Province{});
    auto& p = w.provinces[0];
    p.id = 0;
    p.region_id = 0;
    p.agricultural_productivity = 0.9f;
    // Good soil on a worked core the hands can cover — see make_province. At arable 0.9
    // the same fertility is spread over three times the ground and 600 workers cannot
    // reach enough of it to raise a tithe at all.
    p.geography.arable_land_fraction = 0.3f;
    p.geography.forest_coverage = 0.2f;
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    p.cohort_stats->total_population = 1000;
    p.cohort_stats->working_age_fraction = 0.6f;

    w.npc_indices_by_home_province.resize(1);
    for (uint32_t i = 0; i < 10; ++i) {
        NPC n{};
        n.id = 100 + i;
        n.home_province_id = 0;
        n.capital = (n.id == 107) ? 500.0f : 10.0f;  // the rich head, mid-list
        w.significant_npcs.push_back(n);
        w.npc_indices_by_home_province[0].push_back(i);
    }

    DeltaBuffer d{};
    mod.execute_province(0, w, d);

    // Find each resident's proto-capital credit: the richest head got the largest.
    float credit_107 = 0.0f, max_other = 0.0f;
    for (const auto& nd : d.npc_deltas) {
        if (!nd.capital_delta.has_value())
            continue;
        if (nd.npc_id == 107)
            credit_107 = *nd.capital_delta;
        else
            max_other = std::max(max_other, *nd.capital_delta);
    }
    CHECK(credit_107 > 0.0f);
    CHECK(credit_107 > 5.0f * max_other);  // the tithe flows to wealth: aristocracy emerges
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

// ===========================================================================
// THE LAND WEARS OUT — the only channel that can lower the carrying ceiling.
//
// Knowledge only ever raises the ceiling, so without this a society can never
// overshoot its land and history is a one-way ramp: measured across all four
// spectrum worlds, zero civilisations ever fell. Continuous cropping strips
// nutrients faster than they return; fallow and lighter pressure rebuild them.
// Rome's grain provinces, the Maya lowlands and Easter Island are the cases.
// ===========================================================================

TEST_CASE("soil: worn-out land feeds fewer people", "[subsistence][tier2][soil]") {
    // The feedback that makes a fall possible at all: fertility multiplies the FARMED
    // part of natural capital, so degraded land lowers the ceiling. Forage and
    // fisheries are untouched by tillage, so a ruined field does not empty the woods.
    Province p = make_province(0, /*ag=*/0.8f, /*population=*/1000);
    SubsistenceConfig cfg{};
    const float pristine = SubsistenceModule::natural_capital_of(p, cfg, 1.0f);
    const float worn = SubsistenceModule::natural_capital_of(p, cfg, 0.4f);
    const float dead = SubsistenceModule::natural_capital_of(p, cfg, 0.0f);
    CHECK(worn < pristine);
    CHECK(dead < worn);
    // The land is ruined, not the world: forage and fisheries remain.
    CHECK(dead > 0.0f);
    CHECK_THAT(dead, WithinAbs(cfg.weight_forest_forage * p.geography.forest_coverage, 1e-4f));
}

TEST_CASE("soil: a society that mines its land loses fertility, one that lives within it recovers",
          "[subsistence][tier2][soil]") {
    // The balance itself, over a run of years. A province packed far beyond what its
    // land renews strips fertility; the same province left with a small population
    // rebuilds it. Both directions are the same law with the sign of one comparison.
    auto run_years = [](uint32_t population, float starting_soil, uint32_t years) {
        WorldState w{};
        w.world_seed = 5;
        w.era_catalog.load_builtin_default();
        w.technology.current_era = 1;
        w.hazard_settings.seasonality = 0.0f;  // isolate soil from harvest-failure noise
        w.provinces.push_back(make_province(0, /*ag=*/0.8f, population));
        w.provinces[0].cohort_stats->soil_health = starting_soil;
        rebuild_npc_indices(w);

        // Unmechanised, so this exercises the SOIL law alone. With capital accumulating,
        // machine leverage (R11) lets even a thinly settled province work its land at
        // full stretch and strip it — which is a real effect and exactly what industrial
        // agriculture does, but it is not what this test is about.
        SubsistenceConfig no_capital{};
        no_capital.capital_investment_share = 0.0f;
        SubsistenceModule mod(no_capital);
        for (uint32_t y = 1; y <= years; ++y) {
            w.current_tick = y * kTicksPerYear;
            DeltaBuffer d{};
            mod.execute_province(0, w, d);
            apply_deltas(w, d);
        }
        return w.provinces[0].cohort_stats->soil_health;
    };

    // Packed onto the land: fertility falls.
    const float mined = run_years(/*population=*/400000, /*starting_soil=*/1.0f, /*years=*/60);
    CHECK(mined < 1.0f);

    // Thinly settled on damaged land: it comes back.
    const float rested = run_years(/*population=*/200, /*starting_soil=*/0.5f, /*years=*/60);
    CHECK(rested > 0.5f);

    // And recovery is slower than ruin — collapses are quick, healing is not.
    const float ruin = 1.0f - mined;
    const float healing = rested - 0.5f;
    CHECK(ruin > healing);
}

// ===========================================================================
// A PROVINCE EATS WHAT ARRIVES, NOT ONLY WHAT IT GROWS (R3D).
//
// Sea and river transport decoupled cities from their own hinterland. Egypt
// shipped on the order of 130,000 tonnes of grain a year to Rome, and moving
// grain 70 miles by road cost more than sailing it 1,400. That is a bargain
// with a bill attached: a province fed from elsewhere prospers beyond its own
// land right up until the route fails, and then starves in proportion to how
// far beyond it had grown.
//
// The Late Bronze Age collapse is the case — severing Ugarit cut Cyprus off
// from tin and copper, and the whole eastern Mediterranean system came down
// within a generation. Nothing models a cascade directly; it is what happens
// when a province whose neighbours fed it loses the neighbours.
// ===========================================================================

TEST_CASE("subsistence: a province fed from elsewhere reads better fed than its own land",
          "[subsistence][tier2][trade]") {
    SubsistenceModule mod;
    auto surplus_with_import = [&](float import_rate) {
        WorldState w{};
        w.current_tick = kTicksPerYear;
        w.world_seed = 1;
        w.era_catalog.load_builtin_default();
        w.technology.current_era = 5;  // feudal — a commons regime
        w.hazard_settings.seasonality = 0.0f;  // isolate from harvest-failure draws
        w.provinces.push_back(make_province(0, /*ag=*/0.3f, /*population=*/20000));
        w.provinces[0].cohort_stats->grain_import_rate = import_rate;
        DeltaBuffer d{};
        mod.execute_province(0, w, d);
        REQUIRE(d.region_deltas.size() == 1);
        REQUIRE(d.region_deltas[0].subsistence_surplus_replacement.has_value());
        return *d.region_deltas[0].subsistence_surplus_replacement;
    };

    const float alone = surplus_with_import(0.0f);
    const float fed = surplus_with_import(5000.0f);
    const float draining = surplus_with_import(-2000.0f);

    CHECK(fed > alone);      // grain arriving feeds people
    CHECK(draining < alone);  // and grain leaving does not
}

TEST_CASE("subsistence: when the route fails, the dependence is what starves you",
          "[subsistence][tier2][trade]") {
    // The whole mechanism. Two provinces with identical land and identical
    // populations: one has been living on its own harvest, the other on imports. Cut
    // both off and only the second falls — by exactly the share it had been importing.
    SubsistenceModule mod;
    auto run = [&](float import_rate, float* dependence_out) {
        WorldState w{};
        w.current_tick = kTicksPerYear;
        w.world_seed = 1;
        w.era_catalog.load_builtin_default();
        w.technology.current_era = 5;
        w.hazard_settings.seasonality = 0.0f;
        w.provinces.push_back(make_province(0, /*ag=*/0.3f, /*population=*/20000));
        w.provinces[0].cohort_stats->grain_import_rate = import_rate;
        DeltaBuffer d{};
        mod.execute_province(0, w, d);
        REQUIRE(d.region_deltas.size() == 1);
        if (dependence_out != nullptr) {
            REQUIRE(d.region_deltas[0].import_dependence_replacement.has_value());
            *dependence_out = *d.region_deltas[0].import_dependence_replacement;
        }
        return *d.region_deltas[0].subsistence_surplus_replacement;
    };

    float dependence = 0.0f;
    const float while_supplied = run(5000.0f, &dependence);
    const float after_the_route_fails = run(0.0f, nullptr);

    CHECK(dependence > 0.0f);
    CHECK(dependence < 1.0f);
    CHECK(after_the_route_fails < while_supplied);
    // The fall is the dependence: what it loses is exactly the share that was arriving.
    CHECK_THAT(1.0f - after_the_route_fails / while_supplied,
               Catch::Matchers::WithinAbs(dependence, 0.01f));
}

TEST_CASE("subsistence: a province with no neighbours is unaffected by any of this",
          "[subsistence][tier2][trade]") {
    // The fallback that matters: no haulage, no grain_logistics, or simply nobody
    // within reach means a zero flow, and the province lives on its own harvest exactly
    // as before. Nothing about the trade law can quietly change an isolated society.
    SubsistenceModule mod;
    WorldState w{};
    w.current_tick = kTicksPerYear;
    w.world_seed = 1;
    w.era_catalog.load_builtin_default();
    w.technology.current_era = 5;
    w.hazard_settings.seasonality = 0.0f;
    w.provinces.push_back(make_province(0, /*ag=*/0.8f, /*population=*/1000));
    // grain_import_rate defaults to 0 — no route, no flow.
    DeltaBuffer d{};
    mod.execute_province(0, w, d);
    REQUIRE(d.region_deltas.size() == 1);
    REQUIRE(d.region_deltas[0].import_dependence_replacement.has_value());
    CHECK_THAT(*d.region_deltas[0].import_dependence_replacement,
               Catch::Matchers::WithinAbs(0.0f, 1e-9f));
    CHECK(*d.region_deltas[0].subsistence_surplus_replacement > 1.0f);
}

// ===========================================================================
// WHY ANYONE BOTHERS BUILDING (R4B).
//
// Nobody clears land, digs a canal or raises a mill that pays back over
// thirty years if a warlord, a faction or a tax-farmer will have it in five.
// Credible commitment against confiscation is what North and Weingast
// identified as the precondition for sustained investment, and it is one of
// the standard explanations for why societies that knew perfectly well how to
// build never did.
//
//     s_eff = s * exp(-expropriation_hazard * horizon)
// ===========================================================================

TEST_CASE("subsistence: a society that expects to keep what it builds, builds",
          "[subsistence][tier2][property]") {
    const SubsistenceConfig cfg{};
    const float secure = SubsistenceModule::expropriation_hazard(
        /*trust=*/1.0f, /*psi=*/0.0f, /*war=*/0.0f, cfg);
    CHECK_THAT(secure, Catch::Matchers::WithinAbs(0.0f, 1e-9f));
    // With nothing to fear, the whole intended share is committed.
    CHECK_THAT(SubsistenceModule::effective_investment_share(secure, cfg),
               Catch::Matchers::WithinRel(cfg.capital_investment_share, 1e-4f));
}

TEST_CASE("subsistence: distrust, faction and war each suppress building",
          "[subsistence][tier2][property]") {
    const SubsistenceConfig cfg{};
    const float base = SubsistenceModule::expropriation_hazard(1.0f, 0.0f, 0.0f, cfg);
    CHECK(SubsistenceModule::expropriation_hazard(0.0f, 0.0f, 0.0f, cfg) > base);  // nobody
                                                                                   // trusts
    CHECK(SubsistenceModule::expropriation_hazard(1.0f, 0.5f, 0.0f, cfg) > base);  // factions
    CHECK(SubsistenceModule::expropriation_hazard(1.0f, 0.0f, 0.02f, cfg) > base);  // armies

    // And each shows up as less actually built.
    const float calm = SubsistenceModule::effective_investment_share(base, cfg);
    const float lawless = SubsistenceModule::effective_investment_share(
        SubsistenceModule::expropriation_hazard(0.0f, 0.4f, 0.02f, cfg), cfg);
    CHECK(lawless < calm);
    CHECK(lawless > 0.0f);  // even a bandit kingdom builds something
}

TEST_CASE("subsistence: the difference between accumulating and merely surviving",
          "[subsistence][tier2][property]") {
    // The magnitudes that matter. At a 1%/yr chance of losing it, a society commits
    // about 72% of what it wanted to; at 5%/yr, under a fifth. Nothing about the harvest
    // has changed in either case — this is entirely about what people expect to keep.
    const SubsistenceConfig cfg{};
    const float mild = SubsistenceModule::effective_investment_share(0.01f, cfg);
    const float severe = SubsistenceModule::effective_investment_share(0.05f, cfg);
    CHECK_THAT(mild / cfg.capital_investment_share, Catch::Matchers::WithinAbs(0.72f, 0.02f));
    CHECK(severe < mild);
    // Under real threat the HORIZON shortens rather than the building stopping: people
    // build what pays back before the danger arrives, so the term saturates at exp(-1).
    // A society in permanent danger still manages about a third of what it wanted.
    // (Was a fixed 33-year horizon, which left 0.2% of intended investment at the peak
    // stress this model reaches and erased the capital stock across a crisis.)
    CHECK_THAT(severe / cfg.capital_investment_share, Catch::Matchers::WithinAbs(0.368f, 0.02f));
    CHECK_THAT(SubsistenceModule::effective_investment_share(1.0f, cfg) /
                   cfg.capital_investment_share,
               Catch::Matchers::WithinAbs(0.368f, 0.02f));  // saturated, not zero
    // Saturating toward zero, never negative: an arbitrarily lawless place still cannot
    // build LESS than nothing.
    CHECK(SubsistenceModule::effective_investment_share(10.0f, cfg) >= 0.0f);
}

// ===========================================================================
// KNOWING IS NOT HAVING (R9).
//
// In 1800 Qing China and Britain did not differ much in what they KNEW. China
// had the books, the embassies and the engineers; Russia sent students to
// Britain for decades. What differed was the capital stock to deploy any of
// it — the pits, the pumps, the furnaces, the rails, the drained fields.
// Peoples in the same period advance at wildly different speeds because
// applying a technique costs matter and labour, not just understanding.
//
// It is also the only way the carrying ceiling can fall without anybody
// forgetting: capital wears out and is rebuilt only out of a real surplus.
// ===========================================================================

TEST_CASE("subsistence: a society that knows everything and has built nothing",
          "[subsistence][tier2][applied]") {
    // The China-in-1800 case. Same knowledge, no capital: it farms like the dawn.
    SubsistenceModule mod;
    auto surplus_with = [&](float knowledge, float capital_per_head) {
        WorldState w{};
        w.current_tick = kTicksPerYear;
        w.world_seed = 1;
        w.era_catalog.load_builtin_default();
        w.technology.current_era = 5;
        w.hazard_settings.seasonality = 0.0f;
        w.provinces.push_back(make_province(0, /*ag=*/0.6f, /*population=*/50000));
        w.provinces[0].cohort_stats->knowledge_level = knowledge;
        w.provinces[0].cohort_stats->productive_capital = capital_per_head * 50000.0f;
        DeltaBuffer d{};
        mod.execute_province(0, w, d);
        REQUIRE(d.region_deltas.size() == 1);
        return *d.region_deltas[0].subsistence_surplus_replacement;
    };

    const float ignorant = surplus_with(/*knowledge=*/0.0f, /*capital=*/1000.0f);
    const float knows_but_has_not = surplus_with(500000.0f, 0.0f);
    const float knows_and_has = surplus_with(500000.0f, 1000.0f);

    CHECK(knows_and_has > knows_but_has_not);
    // Knowing without building is worth almost nothing above knowing nothing.
    CHECK_THAT(knows_but_has_not, Catch::Matchers::WithinRel(ignorant, 0.35f));
    CHECK(knows_and_has > 1.5f * knows_but_has_not);
}

TEST_CASE("subsistence: the ceiling can fall without anybody forgetting",
          "[subsistence][tier2][applied]") {
    // The mechanism a secular cycle needs. Capital wears out at ~3%/yr and is rebuilt
    // only out of a real surplus, so a population that outruns its capital stock loses
    // the USE of what it still knows. Knowledge is unchanged in both cases here.
    SubsistenceModule mod;
    auto surplus_at_capital = [&](float capital_per_head) {
        WorldState w{};
        w.current_tick = kTicksPerYear;
        w.world_seed = 1;
        w.era_catalog.load_builtin_default();
        w.technology.current_era = 5;
        w.hazard_settings.seasonality = 0.0f;
        w.provinces.push_back(make_province(0, /*ag=*/0.6f, /*population=*/50000));
        w.provinces[0].cohort_stats->knowledge_level = 500000.0f;  // unchanged throughout
        w.provinces[0].cohort_stats->productive_capital = capital_per_head * 50000.0f;
        DeltaBuffer d{};
        mod.execute_province(0, w, d);
        REQUIRE(d.region_deltas.size() == 1);
        return *d.region_deltas[0].subsistence_surplus_replacement;
    };

    const float built = surplus_at_capital(2000.0f);
    const float worn = surplus_at_capital(200.0f);
    CHECK(worn < built);
    CHECK(worn < 0.6f * built);  // losing the tools really does cost the harvest
}

// ===========================================================================
// THE NO-RAILS RULE, ENFORCED (docs/design/EconLife_No_Rails_Rule.md)
//
// The per-regime specialist ceiling was a rail: a per-era constant deciding the
// shape of the economy by fiat. It bound at EVERY era — the supported share sat
// exactly on 0.15, 0.18, 0.22 — and the model reached "era 8" with 92% of its
// people still farming. Nothing failed; the economy simply never industrialised.
//
// It is gone, replaced by the physical constraint it stood in for: a non-farmer
// has to be fed by somebody else's field and the grain has to reach him, which
// is the ox law grain_logistics already computes. These tests exist so it cannot
// come back, and so the replacement stays a mechanism rather than a number.
// ===========================================================================

TEST_CASE("no rails: what a province can spare is not decided by its era",
          "[subsistence][tier2][no-rails]") {
    // Two provinces identical in every physical respect, in different ERAS. The era
    // label alone must not change how many people the land can spare — only the things
    // an era is a proxy for (knowledge, capital, haulage) may do that, and those are
    // held equal here.
    SubsistenceModule mod;
    auto supported_in_era = [&](uint8_t era) {
        WorldState w{};
        w.current_tick = kTicksPerYear;
        w.world_seed = 1;
        w.era_catalog.load_builtin_default();
        w.technology.current_era = era;
        w.hazard_settings.seasonality = 0.0f;
        w.provinces.push_back(make_province(0, /*ag=*/0.8f, /*population=*/4000));
        auto& cs = *w.provinces[0].cohort_stats;
        cs.knowledge_level = 20000.0f;      // held equal
        cs.productive_capital = 4.0e6f;     // held equal (1,000/head)
        cs.urban_capacity = 2000.0f;        // held equal: haulage is the real limit
        cs.net_feedable_surplus = 2000.0f;
        DeltaBuffer d{};
        mod.execute_province(0, w, d);
        REQUIRE(d.region_deltas.size() == 1);
        REQUIRE(d.region_deltas[0].supported_specialist_fraction_replacement.has_value());
        return *d.region_deltas[0].supported_specialist_fraction_replacement;
    };

    // Barter (2), money (4) and industrial (7) all used to carry different hard
    // ceilings — 0.15, 0.22, 0.45. With the same physics they must now agree.
    const float barter = supported_in_era(2);
    const float money = supported_in_era(4);
    const float industrial = supported_in_era(7);
    CHECK_THAT(barter, Catch::Matchers::WithinRel(money, 1e-4f));
    CHECK_THAT(money, Catch::Matchers::WithinRel(industrial, 1e-4f));
}

TEST_CASE("no rails: the growth signal is not the specialist solve reported back",
          "[subsistence][tier2][no-rails]") {
    // THE CLOSED RING. `labor_needed` is solved so the harvest equals need + granary
    // upkeep, and the population-growth signal used to be measured on that same harvest
    // divided by that same need + upkeep — so it read ~1.0 by construction however rich
    // the land was. The demography could never see abundance, the population never grew
    // into its land, labour stayed spare, and the assignment freed still more
    // specialists. A signal whose numerator and denominator are both written by the
    // module reading it is a rail with no constant in it.
    //
    // So: make the land better and nothing else. The growth signal MUST move.
    SubsistenceModule mod;
    auto growth_signal_on = [&](float ag, float arable) {
        WorldState w{};
        w.current_tick = kTicksPerYear;
        w.world_seed = 1;
        w.era_catalog.load_builtin_default();
        w.technology.current_era = 1;
        w.hazard_settings.seasonality = 0.0f;
        w.provinces.push_back(make_province(0, ag, /*population=*/4000, arable));
        auto& cs = *w.provinces[0].cohort_stats;
        // Generous haulage, so the stratum is free to absorb every scrap of the extra
        // food. That is precisely the case the old form could not distinguish.
        cs.urban_capacity = 4000.0f;
        cs.net_feedable_surplus = 4000.0f;
        DeltaBuffer d{};
        mod.execute_province(0, w, d);
        REQUIRE(d.region_deltas.size() == 1);
        REQUIRE(d.region_deltas[0].subsistence_surplus_replacement.has_value());
        return *d.region_deltas[0].subsistence_surplus_replacement;
    };

    const float poor = growth_signal_on(0.35f, 0.25f);
    const float good = growth_signal_on(0.90f, 0.25f);
    // Nearly three times the fertility on the same worked ground. A population on the
    // better land can grow and one on the worse cannot, and the signal has to say so.
    CHECK(good > poor * 1.5f);
    // And the poor province must still be able to report hardship rather than being
    // held at the neutral reading by the arithmetic.
    CHECK(poor < 1.0f);
}

TEST_CASE("no rails: the stratum is limited by haulage, and haulage is a mechanism",
          "[subsistence][tier2][no-rails]") {
    // The replacement has to actually bind, and it has to respond to the world rather
    // than to a schedule: a province whose neighbours can send it more grain can spare
    // more of its own people, in the same era with the same land.
    SubsistenceModule mod;
    auto supported_with_haulage = [&](float capacity) {
        WorldState w{};
        w.current_tick = kTicksPerYear;
        w.world_seed = 1;
        w.era_catalog.load_builtin_default();
        w.technology.current_era = 5;
        w.hazard_settings.seasonality = 0.0f;
        w.provinces.push_back(make_province(0, /*ag=*/0.8f, /*population=*/4000));
        auto& cs = *w.provinces[0].cohort_stats;
        cs.knowledge_level = 20000.0f;
        cs.productive_capital = 4.0e6f;
        cs.urban_capacity = capacity;
        cs.net_feedable_surplus = capacity;
        DeltaBuffer d{};
        mod.execute_province(0, w, d);
        REQUIRE(d.region_deltas.size() == 1);
        return *d.region_deltas[0].supported_specialist_fraction_replacement;
    };

    const float poor_routes = supported_with_haulage(200.0f);   // 5% of the population
    const float good_routes = supported_with_haulage(1600.0f);  // 40%
    CHECK(good_routes > poor_routes);
    CHECK(poor_routes > 0.0f);
    // And it is the HAULAGE that binds, not a constant: the share tracks the capacity.
    CHECK_THAT(poor_routes, Catch::Matchers::WithinRel(0.05f, 0.02f));
}

TEST_CASE("no rails: stocks evolve on a stated cadence, not per tick",
          "[subsistence][tier2][no-rails]") {
    // A per-tick rate is only correct if every tick runs. The stratum inertia was
    // `rate / ticks_per_year` applied per tick, and the history harness fast-forwards at
    // one step per year — so it advanced 365x too slowly there, and every measurement of
    // it was taken under that regime. It looked exactly like a slow mechanism.
    //
    // The stratum now moves ONLY on the annual tick, like the granary, the soil and the
    // capital. This asserts that: a mid-year tick must not move it at all.
    SubsistenceModule mod;
    auto held_after = [&](uint32_t tick) {
        WorldState w{};
        w.current_tick = tick;
        w.world_seed = 1;
        w.era_catalog.load_builtin_default();
        w.technology.current_era = 5;
        w.hazard_settings.seasonality = 0.0f;
        w.provinces.push_back(make_province(0, /*ag=*/0.8f, /*population=*/4000));
        auto& cs = *w.provinces[0].cohort_stats;
        cs.knowledge_level = 20000.0f;
        cs.productive_capital = 4.0e6f;
        cs.urban_capacity = 2000.0f;
        cs.net_feedable_surplus = 2000.0f;
        cs.specialist_fraction = 0.05f;  // well below what this harvest supports
        DeltaBuffer d{};
        mod.execute_province(0, w, d);
        REQUIRE(d.region_deltas.size() == 1);
        return *d.region_deltas[0].specialist_fraction_replacement;
    };

    const float mid_year = held_after(kTicksPerYear + 7);  // not year-aligned
    const float year_end = held_after(kTicksPerYear);      // year-aligned
    CHECK_THAT(mid_year, Catch::Matchers::WithinAbs(0.05f, 1e-6f));  // unchanged off-cadence
    CHECK(year_end > 0.05f);                                         // moves once a year
}
