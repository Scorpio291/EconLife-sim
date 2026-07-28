#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <limits>

#include "core/rng/deterministic_rng.h"
#include "core/world_state/apply_deltas.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"
#include "modules/population_aging/population_aging_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

TEST_CASE("PopulationAging: income convergence", "[population_aging][tier11]") {
    // current=100, target=150, rate=0.05: 100 + 0.05*(150-100) = 102.5
    float result = PopulationAgingModule::compute_income_convergence(100.0f, 150.0f, 0.05f);
    REQUIRE_THAT(result, WithinAbs(102.5f, 0.1f));
}

TEST_CASE("PopulationAging: income convergence at target", "[population_aging][tier11]") {
    float result = PopulationAgingModule::compute_income_convergence(150.0f, 150.0f, 0.05f);
    REQUIRE_THAT(result, WithinAbs(150.0f, 0.01f));
}

TEST_CASE("PopulationAging: employment convergence", "[population_aging][tier11]") {
    // 0.60 + 0.02*(0.80-0.60) = 0.60 + 0.004 = 0.604
    float result = PopulationAgingModule::compute_employment_convergence(0.60f, 0.80f, 0.02f);
    REQUIRE_THAT(result, WithinAbs(0.604f, 0.001f));
}

TEST_CASE("PopulationAging: employment clamped", "[population_aging][tier11]") {
    float result = PopulationAgingModule::compute_employment_convergence(0.99f, 1.10f, 0.05f);
    REQUIRE(result <= 1.0f);
    REQUIRE(result >= 0.0f);
}

TEST_CASE("PopulationAging: education drift capped", "[population_aging][tier11]") {
    float result = PopulationAgingModule::compute_education_drift(0.50f, 0.80f, 0.01f);
    // Diff=0.30, capped to 0.01 -> 0.51
    REQUIRE_THAT(result, WithinAbs(0.51f, 0.001f));
}

TEST_CASE("PopulationAging: education drift negative", "[population_aging][tier11]") {
    float result = PopulationAgingModule::compute_education_drift(0.50f, 0.30f, 0.01f);
    // Diff=-0.20, capped to -0.01 -> 0.49
    REQUIRE_THAT(result, WithinAbs(0.49f, 0.001f));
}

TEST_CASE("PopulationAging: gini coefficient equal incomes", "[population_aging][tier11]") {
    std::vector<float> incomes = {100.0f, 100.0f, 100.0f, 100.0f};
    float gini = PopulationAgingModule::compute_gini_coefficient(incomes);
    REQUIRE_THAT(gini, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("PopulationAging: gini coefficient unequal incomes", "[population_aging][tier11]") {
    std::vector<float> incomes = {10.0f, 20.0f, 30.0f, 100.0f};
    float gini = PopulationAgingModule::compute_gini_coefficient(incomes);
    REQUIRE(gini > 0.0f);
    REQUIRE(gini <= 1.0f);
}

TEST_CASE("PopulationAging: gini empty returns zero", "[population_aging][tier11]") {
    std::vector<float> incomes;
    REQUIRE_THAT(PopulationAgingModule::compute_gini_coefficient(incomes), WithinAbs(0.0f, 0.001f));
}

TEST_CASE("PopulationAging: monthly tick check", "[population_aging][tier11]") {
    REQUIRE(PopulationAgingModule::is_monthly_tick(0) == true);
    REQUIRE(PopulationAgingModule::is_monthly_tick(30) == true);
    REQUIRE(PopulationAgingModule::is_monthly_tick(15) == false);
}

TEST_CASE("PopulationAging: config defaults match spec", "[population_aging][tier11]") {
    constexpr PopulationAgingConfig cfg{};
    REQUIRE_THAT(cfg.cohort_income_update_rate, WithinAbs(0.05f, 0.001f));
    REQUIRE_THAT(cfg.cohort_employment_update_rate, WithinAbs(0.02f, 0.001f));
    REQUIRE(PopulationAgingModule::TICKS_PER_MONTH == 30);
    REQUIRE(PopulationAgingModule::TICKS_PER_YEAR == 365);
}

// --- Execute-path smoke coverage ------------------------------------------
// Drives the tick entry point. population_aging runs on a monthly cadence,
// so these also lock in the gate: a delta on a monthly tick, none off-month.

namespace {
// A one-province world at an annual tick with a populated working cohort, set up
// to exercise births/deaths under a given subsistence food surplus.
WorldState make_annual_cohort_world(float surplus) {
    WorldState w{};
    w.current_tick = PopulationAgingModule::TICKS_PER_YEAR;  // annual births/deaths fire
    w.world_seed = 1;
    Province p{};
    p.id = 0;
    p.conditions.stability_score = 0.9f;  // high stability so births occur
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    p.cohort_stats->subsistence_surplus_ratio = surplus;
    p.cohort_stats->sick_rate = 0.0f;
    p.cohort_stats->addiction_rate = 0.0f;
    PopulationCohort workers{};
    workers.size = 100000;
    p.cohort_stats->cohorts[DemographicGroup::working_urban_mid] = workers;
    w.provinces.push_back(std::move(p));
    return w;
}

WorldState make_one_province_world(uint32_t tick) {
    WorldState w{};
    w.current_tick = tick;
    w.world_seed = 1;
    Province p{};
    p.id = 0;
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    p.demographics.income_low_fraction = 0.40f;
    p.demographics.income_high_fraction = 0.20f;
    p.demographics.education_level = 0.50f;
    p.conditions.inequality_index = 0.30f;
    w.provinces.push_back(std::move(p));
    return w;
}
}  // namespace

TEST_CASE("PopulationAging: generational hardiness — soft people on a harsh world",
          "[population_aging][hardiness][tier11]") {
    PopulationAgingModule module;
    auto run = [&](float hardiness) {
        WorldState w = make_annual_cohort_world(/*surplus=*/1.5f);  // fed: deaths are hazard-driven
        w.hazard_settings = deathworld_hazard();                   // a harsh world
        w.provinces[0].cohort_stats->hardiness = hardiness;
        DeltaBuffer d{};
        module.execute_province(0, w, d);
        REQUIRE(d.cohort_stats_deltas.size() == 1);
        return d.cohort_stats_deltas[0];
    };
    const float native = hazard_mortality_from_settings(deathworld_hazard());  // adapted (~1.07)
    auto soft_d = run(0.2f);       // garden-bred, unadapted
    auto native_d = run(native);   // native, adapted

    // The unadapted soft people suffer more mortality -> fewer survivors than natives.
    // (Unchanged intent; the GAP is now larger than it was, because the maladaptation
    // ratio of ~5.4 here is no longer pinned to the retired 3x cap.)
    CHECK(soft_d.total_population < native_d.total_population);
    // Hardiness drifts toward the world's hazard level (up from 0.2 toward ~1.07).
    CHECK(soft_d.hardiness > 0.2f);
    CHECK(soft_d.hardiness < native);  // but only partway in one year (generational)
}

TEST_CASE("PopulationAging: hazard mortality is a RATE that saturates at 1.0 (no 3x rail)",
          "[population_aging][hardiness][tier11]") {
    // UPDATED 2026-07-26 — this test used to pin cfg.hazard_mortality_min == 0.15 and
    // cfg.hazard_mortality_max == 3.0 and assert survivors remained a majority *because
    // of the 3x cap*. Those config fields are gone: the maladaptation term is now an
    // uncapped multiplier on the annual mortality RATE, and the bound is physical — the
    // annual probability arrives as p = 1 - exp(-rate), which rises toward 100% and can
    // never exceed it. Same intent (mortality is bounded, not arbitrary), grounded cause.
    constexpr PopulationAgingConfig cfg{};
    using PA = PopulationAgingModule;

    // 1) The multiplier is uncapped. Garden-bred hardiness 0.19 on a deathworld
    //    (world_hazard ~1.076) is exactly where the old band bound hardest: the true
    //    ratio is ~5.7, formerly pinned to 3.0 — a 1.9x softening of the culling.
    const float deathworld = hazard_mortality_from_settings(deathworld_hazard());
    const float soft_mult = PA::hazard_rate_multiplier(deathworld, 0.19f, cfg.hardiness_floor);
    CHECK(soft_mult > 5.0f);  // was pinned at 3.0
    CHECK_THAT(soft_mult, WithinAbs(deathworld / 0.19f, 1e-4f));
    // hardiness_floor is only the divide-by-zero sentinel.
    CHECK_THAT(PA::hazard_rate_multiplier(1.0f, 0.0f, cfg.hardiness_floor),
               WithinAbs(1.0f / cfg.hardiness_floor, 1e-4f));

    // 2) The probability is what saturates: monotone in the rate, never above certainty.
    CHECK(PA::annual_probability_from_rate(0.0f) == 0.0f);
    CHECK_THAT(PA::annual_probability_from_rate(0.0088f), WithinAbs(0.0087614f, 1e-6f));
    float prev = 0.0f;
    for (float rate : {0.05f, 0.5f, 2.0f, 10.0f}) {
        const float p = PA::annual_probability_from_rate(rate);
        CHECK(p > prev);   // strictly increasing in the rate
        CHECK(p <= 1.0f);  // never exceeds certainty
        prev = p;
    }
    CHECK_THAT(PA::annual_probability_from_rate(1.0e6f), WithinAbs(1.0f, 1e-6f));  // -> 100%
    // Non-finite input is a crash sentinel, not a massacre.
    CHECK(PA::annual_probability_from_rate(std::numeric_limits<float>::quiet_NaN()) == 0.0f);

    // 3) End to end. A pathologically unadapted people on a deathworld now takes the
    //    full rate: hardiness 0.01 sits below hardiness_floor, so the multiplier is
    //    world_hazard/0.10 = 10.76 -> rate = 0.008 * 1.1 * 10.76 = 0.0947/person-year
    //    -> p = 9.0%/yr, against 2.6%/yr under the retired 3x cap. Still bounded: the
    //    cohort is culled hard, not annihilated, and population never goes negative.
    PopulationAgingModule module;
    WorldState w = make_annual_cohort_world(/*surplus=*/1.5f);  // fed: deaths are hazard-driven
    w.hazard_settings = deathworld_hazard();
    w.provinces[0].cohort_stats->hardiness = 0.01f;  // pathologically unadapted
    uint32_t before = 0;
    for (const auto& [g, c] : w.provinces[0].cohort_stats->cohorts) {
        (void)g;
        before += c.size;  // the seeded stats.total_population field is not populated
    }
    REQUIRE(before == 100000u);
    DeltaBuffer d{};
    module.execute_province(0, w, d);
    REQUIRE(d.cohort_stats_deltas.size() == 1);
    const uint32_t after = d.cohort_stats_deltas[0].total_population;
    CHECK(after > before / 2);  // bounded: a majority survives one brutal year
    CHECK(after < 95000u);      // and the culling is no longer softened by the cap
                                // (the capped form left ~98,900 of 100,000)
}

TEST_CASE("PopulationAging: Earth-normal mortality survives the rate reform unchanged",
          "[population_aging][hardiness][tier11]") {
    // The rate->probability reform must not move the long-horizon baseline. An
    // Earth-normal world with an Earth-adapted people is the anchor: world_hazard is
    // Earth-normalized to exactly 1.0 and cohort hardiness defaults to 1.0, so the
    // maladaptation multiplier is exactly 1.0 and the composed rate is unchanged. Only
    // the rate -> probability conversion differs, and at Earth-normal rates that is a
    // sub-percent effect:
    //     rate = base_annual_death_rate 0.008 * (1 + (1 - stability 0.9)) = 0.0088/yr
    //     was (rate used directly AS a probability): p = 0.008800
    //     now  p = 1 - exp(-0.0088)                  = 0.0087614   (-0.45% relative)
    constexpr PopulationAgingConfig cfg{};
    const float rate = cfg.base_annual_death_rate * 1.1f;
    const float p = PopulationAgingModule::annual_probability_from_rate(rate);
    CHECK_THAT(p, WithinAbs(0.0087614f, 1e-6f));
    CHECK(p < rate);                    // the rate form is very slightly gentler
    CHECK((rate - p) / rate < 0.005f);  // by under 0.5% of deaths
    // Retirees take the same rate x4; still under 2%.
    const float retiree_rate = rate * cfg.retiree_mortality_multiplier;
    const float p_ret = PopulationAgingModule::annual_probability_from_rate(retiree_rate);
    CHECK((retiree_rate - p_ret) / retiree_rate < 0.02f);

    // End to end on an Earth-normal, exactly-fed province of 100,000:
    //   births = round(100000 * 0.012 * 0.9 * 1.0 * (1 - 0.18*0.20 radiation)) = 1041
    //   deaths = round(cohort * p) on each cohort (the newborn youth cohorts included)
    //   was: 100000 + 1041 - 880 - 5 - 5 = 100,151
    //   now: 100000 + 1041 - 876 - 5 - 5 = 100,155   (+0.004% population)
    PopulationAgingModule module;
    WorldState w = make_annual_cohort_world(/*surplus=*/1.0f);  // exactly fed: neutral food
    REQUIRE_THAT(hazard_mortality_from_settings(w.hazard_settings), WithinAbs(1.0f, 1e-6f));
    DeltaBuffer d{};
    module.execute_province(0, w, d);
    REQUIRE(d.cohort_stats_deltas.size() == 1);
    const uint32_t after = d.cohort_stats_deltas[0].total_population;
    CHECK(after >= 100150u);  // the pre-reform figure was 100,151 — the baseline is intact
    CHECK(after <= 100160u);
}

TEST_CASE("PopulationAging: subsistence surplus drives the Malthusian loop",
          "[population_aging][subsistence][tier11]") {
    PopulationAgingModule module;
    auto annual_population = [&](float surplus) {
        WorldState w = make_annual_cohort_world(surplus);
        DeltaBuffer d{};
        module.execute_province(0, w, d);
        REQUIRE(d.cohort_stats_deltas.size() == 1);
        return d.cohort_stats_deltas[0].total_population;
    };

    const uint32_t fed = annual_population(1.0f);      // exactly fed: neutral
    const uint32_t surplus = annual_population(1.5f);  // surplus: population grows
    const uint32_t famine = annual_population(0.5f);   // deficit: population is culled

    CHECK(surplus > fed);    // a food surplus lifts births
    CHECK(famine < fed);     // a deficit raises mortality
    CHECK(famine < surplus);
}

TEST_CASE("PopulationAging: execute_province emits a RegionDelta on a monthly tick",
          "[population_aging][tier11]") {
    auto world = make_one_province_world(/*tick=*/PopulationAgingModule::TICKS_PER_MONTH);
    PopulationAgingModule module;

    DeltaBuffer delta{};
    module.execute_province(0, world, delta);

    REQUIRE(delta.region_deltas.size() == 1);
    const RegionDelta& rd = delta.region_deltas[0];
    REQUIRE(rd.region_id == 0);
    REQUIRE(rd.stability_delta.has_value());
    REQUIRE(rd.inequality_delta.has_value());
    // Grievance is no longer written here — it has a single owner
    // (community_response). This module previously injected a constant
    // income_low_fraction grievance pump every tick.
    REQUIRE_FALSE(rd.grievance_delta.has_value());
}

TEST_CASE("PopulationAging: execute_province is a no-op off the monthly cadence",
          "[population_aging][tier11]") {
    // Tick 1 is not a multiple of TICKS_PER_MONTH (30): the module must skip.
    auto world = make_one_province_world(/*tick=*/1);
    PopulationAgingModule module;

    DeltaBuffer delta{};
    module.execute_province(0, world, delta);

    REQUIRE(delta.region_deltas.empty());
}

// ===========================================================================
// Background-population cohort lifecycle (built out from the former stub)
// ===========================================================================
namespace {
void seed_two_cohorts(RegionCohortStats& cs) {
    PopulationCohort a;
    a.group = DemographicGroup::working_urban_mid;
    a.size = 1000;
    a.median_income = 100.0f;
    a.education_level = 0.50f;
    a.employment_rate = 0.60f;
    PopulationCohort b;
    b.group = DemographicGroup::working_rural_low;
    b.size = 500;
    b.median_income = 50.0f;
    b.education_level = 0.40f;
    b.employment_rate = 0.50f;
    cs.cohorts[a.group] = a;
    cs.cohorts[b.group] = b;
    cs.regional_wage_anchor = 150.0f;
    cs.formal_employment_rate = 0.80f;
}
}  // namespace

TEST_CASE("PopulationAging: monthly cohort income + employment convergence",
          "[population_aging][tier11]") {
    auto world = make_one_province_world(/*tick=*/PopulationAgingModule::TICKS_PER_MONTH);
    seed_two_cohorts(*world.provinces[0].cohort_stats);

    PopulationAgingModule module;
    DeltaBuffer delta{};
    module.execute_province(0, world, delta);
    REQUIRE(delta.cohort_stats_deltas.size() == 1);
    apply_deltas(world, delta);

    const auto& cohorts = world.provinces[0].cohort_stats->cohorts;
    const auto& a = cohorts.at(DemographicGroup::working_urban_mid);
    // income: 100 + 0.05*(150-100) = 102.5 ; employment: 0.6 + 0.02*(0.8-0.6) = 0.604
    REQUIRE_THAT(a.median_income, WithinAbs(102.5f, 1e-3f));
    REQUIRE_THAT(a.employment_rate, WithinAbs(0.604f, 1e-4f));
    // total_population preserved on a monthly tick (no births/deaths), gini > 0.
    REQUIRE(world.provinces[0].cohort_stats->total_population == 1500u);
    REQUIRE(world.provinces[0].cohort_stats->gini_coefficient > 0.0f);
}

TEST_CASE("PopulationAging: annual education drift is capped", "[population_aging][tier11]") {
    auto world = make_one_province_world(/*tick=*/PopulationAgingModule::TICKS_PER_YEAR);
    world.provinces[0].demographics.education_level = 0.80f;  // target well above cohort 0.50
    seed_two_cohorts(*world.provinces[0].cohort_stats);

    PopulationAgingModule module;
    DeltaBuffer delta{};
    module.execute_province(0, world, delta);
    REQUIRE(delta.cohort_stats_deltas.size() == 1);
    apply_deltas(world, delta);

    // education moved by at most max_education_drift_per_year (0.01).
    float edu = world.provinces[0]
                    .cohort_stats->cohorts.at(DemographicGroup::working_urban_mid)
                    .education_level;
    REQUIRE(edu <= 0.50f + 0.01f + 1e-5f);
    REQUIRE(edu > 0.50f);  // drifted up toward 0.80
}

TEST_CASE("PopulationAging: empty cohorts produce no cohort delta", "[population_aging][tier11]") {
    auto world = make_one_province_world(/*tick=*/PopulationAgingModule::TICKS_PER_MONTH);
    // cohorts left empty.
    PopulationAgingModule module;
    DeltaBuffer delta{};
    module.execute_province(0, world, delta);
    REQUIRE(delta.cohort_stats_deltas.empty());
}

TEST_CASE("PopulationAging: better healthcare yields more net births",
          "[population_aging][tier11]") {
    PopulationAgingModule module;

    auto run = [&](float sick_rate) -> uint32_t {
        auto world = make_one_province_world(/*tick=*/PopulationAgingModule::TICKS_PER_YEAR);
        auto& cs = *world.provinces[0].cohort_stats;
        seed_two_cohorts(cs);
        cs.sick_rate = sick_rate;
        world.provinces[0].conditions.stability_score = 0.80f;
        DeltaBuffer delta{};
        module.execute_province(0, world, delta);
        apply_deltas(world, delta);
        return world.provinces[0].cohort_stats->total_population;
    };

    uint32_t healthy = run(0.10f);  // low sickness -> high birth survival
    uint32_t sick = run(0.90f);     // high sickness -> low birth survival
    REQUIRE(healthy > sick);
}

// ===========================================================================
// Significant-NPC aging (annual age advancement + natural death)
// ===========================================================================

TEST_CASE("PopulationAging: natural death probability gating", "[population_aging][tier11]") {
    // Below lifespan -> zero.
    REQUIRE_THAT(PopulationAgingModule::compute_natural_death_probability(40.0f, 80.0f, 0.10f),
                 WithinAbs(0.0f, 1e-6f));
    // At lifespan -> base prob.
    REQUIRE_THAT(PopulationAgingModule::compute_natural_death_probability(80.0f, 80.0f, 0.10f),
                 WithinAbs(0.10f, 1e-4f));
    // Well past lifespan -> clamps to 1.0.
    REQUIRE_THAT(PopulationAgingModule::compute_natural_death_probability(10000.0f, 80.0f, 0.10f),
                 WithinAbs(1.0f, 1e-4f));
}

TEST_CASE("PopulationAging: annual tick advances NPC age", "[population_aging][tier11]") {
    auto world = make_one_province_world(/*tick=*/PopulationAgingModule::TICKS_PER_YEAR);
    NPC npc{};
    npc.id = 5;
    npc.status = NPCStatus::active;
    npc.current_province_id = 0;
    npc.age_years = 40.0f;
    world.significant_npcs.push_back(npc);

    PopulationAgingModule module;
    DeltaBuffer delta{};
    module.execute_province(0, world, delta);

    bool found = false;
    for (const auto& nd : delta.npc_deltas) {
        if (nd.npc_id == 5) {
            REQUIRE(nd.age_delta.has_value());
            REQUIRE_THAT(*nd.age_delta, WithinAbs(1.0f, 1e-6f));
            REQUIRE_FALSE(nd.new_status.has_value());  // 40 << lifespan, no death
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("PopulationAging: NPC far past lifespan dies", "[population_aging][tier11]") {
    auto world = make_one_province_world(/*tick=*/PopulationAgingModule::TICKS_PER_YEAR);
    NPC npc{};
    npc.id = 9;
    npc.status = NPCStatus::active;
    npc.current_province_id = 0;
    npc.age_years = 10000.0f;  // death probability clamps to 1.0
    world.significant_npcs.push_back(npc);

    PopulationAgingModule module;
    DeltaBuffer delta{};
    module.execute_province(0, world, delta);
    apply_deltas(world, delta);

    REQUIRE(world.significant_npcs[0].status == NPCStatus::dead);
}

TEST_CASE("PopulationAging: NPC aging only fires on annual ticks", "[population_aging][tier11]") {
    auto world = make_one_province_world(/*tick=*/PopulationAgingModule::TICKS_PER_MONTH);  // 30
    NPC npc{};
    npc.id = 1;
    npc.status = NPCStatus::active;
    npc.current_province_id = 0;
    npc.age_years = 40.0f;
    world.significant_npcs.push_back(npc);

    PopulationAgingModule module;
    DeltaBuffer delta{};
    module.execute_province(0, world, delta);
    // Tick 30 is monthly but not annual: no NPC age delta.
    for (const auto& nd : delta.npc_deltas)
        REQUIRE(nd.npc_id != 1);
}

TEST_CASE("PopulationAging: disease epidemics — episodic, scaled by disease dial and crowding",
          "[population_aging][tier11]") {
    PopulationAgingConfig cfg{};

    // A disease-free world (dial 0) never has outbreaks.
    for (uint32_t s = 0; s < 200; ++s) {
        DeterministicRNG rng(s);
        CHECK(PopulationAgingModule::epidemic_mortality_factor(0.0f, 0.5f, rng, cfg) == 1.0f);
    }

    // Outbreak probability rises with the disease dial AND with urban crowding.
    auto outbreak_rate = [&](float disease, float density) {
        int hits = 0;
        const int N = 4000;
        for (int s = 0; s < N; ++s) {
            DeterministicRNG rng(static_cast<uint64_t>(s) * 2654435761ull + 1u);
            if (PopulationAgingModule::epidemic_mortality_factor(disease, density, rng, cfg) > 1.0f)
                ++hits;
        }
        return static_cast<double>(hits) / N;
    };
    const double low = outbreak_rate(0.3f, 0.0f);
    const double high_disease = outbreak_rate(1.0f, 0.0f);
    const double high_density = outbreak_rate(1.0f, 1.0f);
    CHECK(low > 0.0);
    CHECK(high_disease > low);           // a plaguier world outbreaks more
    CHECK(high_density > high_disease);  // crowded towns are disease vectors

    // When an outbreak strikes, the mortality spike is the deterministic formula
    // 1 + severity * disease * (1 + density).
    const float expected = 1.0f + cfg.epidemic_severity * 1.0f * (1.0f + 1.0f);  // disease=1, density=1
    bool saw_outbreak = false;
    for (uint32_t s = 0; s < 500 && !saw_outbreak; ++s) {
        DeterministicRNG rng(s);
        const float f = PopulationAgingModule::epidemic_mortality_factor(1.0f, 1.0f, rng, cfg);
        if (f > 1.0f) {
            CHECK_THAT(f, WithinAbs(expected, 1e-4f));
            saw_outbreak = true;
        }
    }
    CHECK(saw_outbreak);
}

TEST_CASE("PopulationAging: geology disasters — episodic, scaled by the geology dial",
          "[population_aging][tier11]") {
    PopulationAgingConfig cfg{};

    // A geologically calm world (dial 0) never has disasters.
    for (uint32_t s = 0; s < 200; ++s) {
        DeterministicRNG rng(s);
        CHECK(PopulationAgingModule::disaster_mortality_factor(0.0f, rng, cfg) == 1.0f);
    }

    // Disaster probability rises with the geology dial (NOT density — a quake hits all).
    auto rate = [&](float geology) {
        int hits = 0;
        const int N = 5000;
        for (int s = 0; s < N; ++s) {
            DeterministicRNG rng(static_cast<uint64_t>(s) * 40503ull + 7u);
            if (PopulationAgingModule::disaster_mortality_factor(geology, rng, cfg) > 1.0f)
                ++hits;
        }
        return static_cast<double>(hits) / N;
    };
    CHECK(rate(1.0f) > rate(0.3f));
    CHECK(rate(0.3f) > 0.0);

    // The disaster spike is the deterministic formula 1 + severity * geology.
    const float expected = 1.0f + cfg.geology_disaster_severity * 1.0f;
    bool saw = false;
    for (uint32_t s = 0; s < 500 && !saw; ++s) {
        DeterministicRNG rng(s);
        const float f = PopulationAgingModule::disaster_mortality_factor(1.0f, rng, cfg);
        if (f > 1.0f) {
            CHECK_THAT(f, WithinAbs(expected, 1e-4f));
            saw = true;
        }
    }
    CHECK(saw);
}

TEST_CASE("PopulationAging: radiation chronically depresses fertility (planetary)",
          "[population_aging][tier11]") {
    PopulationAgingConfig cfg{};
    CHECK(PopulationAgingModule::radiation_fertility_factor(0.0f, cfg) == 1.0f);
    const float earth = PopulationAgingModule::radiation_fertility_factor(0.2f, cfg);
    const float hot = PopulationAgingModule::radiation_fertility_factor(1.0f, cfg);
    CHECK(earth < 1.0f);
    CHECK(hot < earth);  // a more irradiated world is less fertile
    CHECK(hot >= 0.0f);
    CHECK_THAT(hot, WithinAbs(1.0f - cfg.radiation_fertility_penalty, 1e-4f));
}

// ===========================================================================
// THE WAGE VALVE — growth is an equilibrium, not a cap.
//
// Fixed land means more people cut the marginal product of labour: real wages
// fall, delaying marriage and depressing fertility while raising mortality.
// England shows no real-wage trend 1200-1800 despite population tripling —
// growth was pinned near zero by this valve. Measured here it ran ~0.18%/yr
// against a real ~0.04%/yr, and that speed let population outrun the
// institutions a society needs, so the learned stratum never formed.
// ===========================================================================

TEST_CASE("population: a society exactly at subsistence stops growing on its own",
          "[population_aging][tier11][wage]") {
    // w = 1 is the crossing point: both the fertility and mortality responses are
    // neutral there, so the population neither runs away nor dies out. Nothing
    // imposes this — it falls out of where the two power laws meet.
    PopulationAgingModule mod;
    WorldState w = make_annual_cohort_world(/*surplus=*/1.0f);
    const uint64_t before = w.provinces[0].cohort_stats->cohorts[
        DemographicGroup::working_urban_mid].size;

    DeltaBuffer d{};
    mod.execute_province(0, w, d);
    apply_deltas(w, d);

    uint64_t after = 0;
    for (const auto& [g, c] : w.provinces[0].cohort_stats->cohorts) {
        (void)g;
        after += c.size;
    }
    // Within a few percent of where it started after a year at exact subsistence.
    const double drift = std::abs(static_cast<double>(after) - static_cast<double>(before)) /
                         static_cast<double>(before);
    CHECK(drift < 0.05);
}

TEST_CASE("population: growth answers to how well fed people are, in both directions",
          "[population_aging][tier11][wage]") {
    // Slack in the food supply brings marriage forward and growth follows; pressing
    // on the land kills before outright famine does. The valve is symmetric — which
    // is what makes the growth RATE emergent rather than chosen.
    PopulationAgingModule mod;

    auto year_change = [&](float surplus) {
        WorldState w = make_annual_cohort_world(surplus);
        uint64_t before = 0;
        for (const auto& [g, c] : w.provinces[0].cohort_stats->cohorts) {
            (void)g;
            before += c.size;
        }
        DeltaBuffer d{};
        mod.execute_province(0, w, d);
        apply_deltas(w, d);
        uint64_t after = 0;
        for (const auto& [g, c] : w.provinces[0].cohort_stats->cohorts) {
            (void)g;
            after += c.size;
        }
        return static_cast<double>(after) - static_cast<double>(before);
    };

    const double fed = year_change(1.6f);      // room to spare
    const double subsistence = year_change(1.0f);
    const double hungry = year_change(0.7f);   // pressing on the land

    CHECK(fed > subsistence);
    CHECK(hungry < subsistence);
    // Chronic hunger FLATTENS growth rather than reversing it: at 0.7 the change is
    // ~0.02% of the population. Outright decline is the acute famine path, which fires
    // when the granary is actually empty — the two are deliberately separate, because
    // historically famine mortality was a weak long-run check (its demographic bite is
    // mostly lost births plus emigration, with fast rebound) while chronic
    // undernutrition is what held pre-industrial growth near zero for centuries.
    CHECK(std::abs(hungry) < 0.001 * 100000.0);
}
