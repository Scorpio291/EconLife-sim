#pragma once

// Society-evolution harness — boots a world at the DAWN (era 1, founding seed) and
// runs it forward through the real tick orchestrator, capturing an annual time
// series of *society-level* aggregates so we can watch whether — and how — a
// society evolves: survive, stagnate, develop (surplus -> specialists -> capital
// -> firms -> era advance), or overshoot and crash.
//
// Companion to emergence_harness.h (which observes a modern, era-5 world). This one
// is the lab for the world-spectrum / World-Class work; see
// docs/design/EconLife_World_Spectrum_and_Evolution_Plan.md.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/thread_pool.h"
#include "core/world_gen/era_catalog.h"
#include "core/tick/tick_orchestrator.h"
#include "core/world_gen/world_class.h"
#include "core/world_gen/world_generator.h"
#include "core/world_state/world_state.h"
#include "modules/register_base_game_modules.h"

namespace econlife::society {

inline std::string find_base_game_subdir(const char* sub) {
    namespace fs = std::filesystem;
    for (const std::string prefix : {"packages/base_game/", "../packages/base_game/",
                                     "../../packages/base_game/", "../../../packages/base_game/"}) {
        std::string p = prefix + sub;
        if (fs::exists(p) && fs::is_directory(p))
            return fs::canonical(p).string();
    }
    return "";
}

inline std::string find_goods_dir_society() { return find_base_game_subdir("goods"); }

// One annual observation of the whole society (observable WorldState only).
struct SocietySnapshot {
    uint32_t year = 0;
    double total_population = 0.0;     // sum of cohort populations
    double urban_population = 0.0;     // sum of cohort urban_population (catchment town economy, M3)
    double mean_surplus = 0.0;         // mean subsistence_surplus_ratio over provinces
    double specialist_fraction = 0.0;  // Layer-2 livelihoods / livelihoods assigned
    double total_capital = 0.0;        // sum of significant-NPC capital (proto-capital)
    double productive_capital_per_head = 0.0;  // BUILT capacity per person (tools, kilns,
                                               // cleared land) — the material era gate
    double soil_health = 0.0;  // mean fertility of the worked land [0,1] — the only
                               // channel that can lower the carrying ceiling
    double ghost_land = 0.0;   // mean ghost-acre fraction: the land coal is standing in
                               // for — the only channel that can RAISE it without limit
    double coal_burned = 0.0;  // tonnes/yr drawn from the province seams
    double coal_remaining = 0.0;  // tonnes still in the ground (the finite escape)
    // R6: knowledge is held per province now, so the SPREAD across them is the thing to
    // watch. A world where every province tracks the frontier is one civilisation with
    // six provinces; a world where they diverge has regions that can fall independently,
    // which is what the record actually contains.
    double knowledge_leader = 0.0;    // the frontier — what the best-informed place knows
    double knowledge_laggard = 0.0;   // what the worst-informed place knows
    double knowledge_mean = 0.0;
    double political_stress = 0.0;  // mean PSI: how close the society is to coming apart
                                    // for reasons that are nothing to do with the weather
    double faction_deaths = 0.0;    // mean annual death fraction from factional conflict
    double capital_gini = 0.0;         // inequality of that capital [0,1]
    uint32_t businesses = 0;           // emergent firms
    int era = 0;
    float knowledge = 0.0f;            // accumulated knowledge_level
    bool reached_market = false;       // left the commons for a market era (climb complete)
    bool extinct = false;              // population collapsed to ~0
};

// Gini over a list of non-negative values (0 = perfect equality).
inline double gini(std::vector<double> v) {
    if (v.size() < 2)
        return 0.0;
    std::sort(v.begin(), v.end());
    double sum = 0.0, weighted = 0.0;
    for (size_t i = 0; i < v.size(); ++i) {
        sum += v[i];
        weighted += static_cast<double>(i + 1) * v[i];
    }
    if (sum <= 0.0)
        return 0.0;
    const double n = static_cast<double>(v.size());
    return (2.0 * weighted) / (n * sum) - (n + 1.0) / n;
}

inline SocietySnapshot capture_society(const WorldState& w, uint32_t year) {
    SocietySnapshot s;
    s.year = year;
    s.era = static_cast<int>(w.technology.current_era);
    s.knowledge = w.technology.knowledge_level;
    const EraDefinition* edef = w.era_catalog.by_index(w.technology.current_era);
    // The dawn (commons) arc spans subsistence -> barter -> coinage -> money -> feudal
    // -> mercantile -> industrial; the climb is "complete" only when it reaches a true
    // market economy (modern) beyond those.
    const std::string regime = edef ? edef->economic_regime : std::string();
    s.reached_market = edef && !is_premarket_regime(regime);

    double surplus_sum = 0.0;
    int prov_with_cohorts = 0;
    for (const auto& p : w.provinces) {
        if (!p.cohort_stats)
            continue;
        s.total_population += static_cast<double>(p.cohort_stats->total_population);
        s.productive_capital_per_head += static_cast<double>(p.cohort_stats->productive_capital);
        s.soil_health += static_cast<double>(p.cohort_stats->soil_health);
        s.urban_population += static_cast<double>(p.cohort_stats->urban_population);
        s.ghost_land += static_cast<double>(p.cohort_stats->ghost_land_fraction);
        const double k = static_cast<double>(p.cohort_stats->knowledge_level);
        s.knowledge_leader = std::max(s.knowledge_leader, k);
        s.knowledge_laggard = prov_with_cohorts == 0 ? k : std::min(s.knowledge_laggard, k);
        s.knowledge_mean += k;
        s.political_stress += static_cast<double>(p.cohort_stats->political_stress);
        s.faction_deaths += static_cast<double>(p.cohort_stats->faction_death_fraction);
        s.coal_burned += static_cast<double>(p.cohort_stats->coal_burned_per_year);
        for (const auto& dep : p.deposits)
            if (dep.type == ResourceType::Coal)
                s.coal_remaining += static_cast<double>(dep.quantity_remaining);
        surplus_sum += p.cohort_stats->subsistence_surplus_ratio;
        ++prov_with_cohorts;
    }
    s.mean_surplus = prov_with_cohorts > 0 ? surplus_sum / prov_with_cohorts : 0.0;
    if (prov_with_cohorts > 0) {
        s.knowledge_mean /= prov_with_cohorts;
        s.ghost_land /= prov_with_cohorts;
        s.political_stress /= prov_with_cohorts;
        s.faction_deaths /= prov_with_cohorts;
    }
    s.extinct = s.total_population <= 0.0;

    int assigned = 0, specialists = 0;
    std::vector<double> capitals;
    capitals.reserve(w.significant_npcs.size());
    for (const auto& npc : w.significant_npcs) {
        capitals.push_back(static_cast<double>(npc.capital));
        s.total_capital += static_cast<double>(npc.capital);
        if (npc.occupation != 0) {
            ++assigned;
            const OccupationDefinition* o = w.occupation_catalog.by_index(npc.occupation);
            if (o && o->layer == 2)
                ++specialists;
        }
    }
    s.specialist_fraction = assigned > 0 ? static_cast<double>(specialists) / assigned : 0.0;
    s.capital_gini = gini(std::move(capitals));
    // Convert the accumulated stock into per-head capacity — the form the era gate
    // reads, so a larger society needs proportionally more built.
    if (s.total_population > 0.0)
        s.productive_capital_per_head /= s.total_population;
    if (!w.provinces.empty())
        s.soil_health /= static_cast<double>(w.provinces.size());
    s.businesses = static_cast<uint32_t>(w.npc_businesses.size());
    return s;
}

// Boot a dawn (era 1, founding-seed) world shaped by `arch` (the world-spectrum
// dial: Bounty + World-Class hazard) and run it `years` in-game years,
// capturing one SocietySnapshot per year (plus the t=0 snapshot).
inline std::vector<SocietySnapshot> run_society_years(uint64_t seed, uint32_t npc_count,
                                                      uint32_t years, const WorldArchetype& arch,
                                                      float founding_hardiness = 0.0f,
                                                      bool fast_forward = false) {
    WorldGeneratorConfig config{};
    config.seed = seed;
    config.province_count = 6;
    config.npc_count = npc_count;
    config.starting_era = 1;           // the dawn (subsistence regime)
    config.founding_seed_mode = true;  // no hand-seeded economy; it must emerge
    config.goods_directory = find_goods_dir_society();
    config.technology_directory = find_base_game_subdir("technology");  // tech tree + effects
    config.bounty_scale = arch.bounty;       // Bounty dial -> natural capital
    config.hazard_settings = arch.hazard;    // Hazard settings -> per-module effects
    config.founding_hardiness = founding_hardiness;  // 0 = native (adapted); >0 = transplant
    // eras/occupations dirs left empty -> builtin catalogs (match the CSVs).

    WorldState world = WorldGenerator::generate(config);

    TickOrchestrator orch;
    register_base_game_modules(orch);
    orch.finalize_registration();
    ThreadPool pool(1);

    // The commons climb is "done" once the world leaves the pre-market regimes for a
    // true market era — that is the natural terminus of dawn history-gen (the modern
    // economy modules take over from there and are not meant to run in this
    // fast-forward dawn lab). The dawn arc spans subsistence -> barter -> coinage ->
    // money -> feudal -> mercantile -> industrial; stop only beyond those (modern).
    auto is_commons_era = [&](uint8_t era) {
        const EraDefinition* e = world.era_catalog.by_index(era);
        if (!e)
            return false;
        return is_premarket_regime(e->economic_regime);
    };

    std::vector<SocietySnapshot> series;
    series.reserve(years + 1);
    series.push_back(capture_society(world, 0));
    for (uint32_t y = 0; y < years; ++y) {
        if (fast_forward) {
            // Coarse history stride: one orchestrator step per year, landed on a
            // year-aligned tick so the annual dynamics (knowledge, births/deaths,
            // era advance) fire. ~365x fewer ticks. Per-day effects under-count —
            // fine for pre-market history-gen (no deferred work at the dawn).
            world.current_tick = (y + 1) * 365u;
            orch.execute_tick(world, pool);
        } else {
            for (uint32_t t = 0; t < 365; ++t)
                orch.execute_tick(world, pool);
        }
        series.push_back(capture_society(world, y + 1));
        if (!is_commons_era(world.technology.current_era))
            break;  // reached a market era — the dawn climb is complete
    }
    return series;
}

// Convenience: a default earthlike dawn (Class 12, bounty 1.0).
inline std::vector<SocietySnapshot> run_society_years(uint64_t seed, uint32_t npc_count,
                                                      uint32_t years) {
    return run_society_years(seed, npc_count, years, archetype_earthlike());
}

// How a society's run turned out.
enum class Trajectory {
    Extinct,          // population collapsed to ~0
    OvershootCrash,   // grew then crashed hard (Malthusian overshoot)
    BareSubsistence,  // survived, but flat: no surplus to spend, no specialization
    Stalled,          // developed (climbed eras) then plateaued at the Malthusian wall —
                      // population at carrying capacity, surplus gone, advancement frozen
    Developing,       // sustained surplus + emerging specialization/capital
    Thriving,         // growing population AND real specialization/wealth/advancement
};

inline const char* trajectory_name(Trajectory t) {
    switch (t) {
        case Trajectory::Extinct:
            return "Extinct";
        case Trajectory::OvershootCrash:
            return "OvershootCrash";
        case Trajectory::BareSubsistence:
            return "BareSubsistence";
        case Trajectory::Stalled:
            return "Stalled";
        case Trajectory::Developing:
            return "Developing";
        case Trajectory::Thriving:
            return "Thriving";
    }
    return "Unknown";
}

// Classify a run from its time series. Heuristic thresholds (tunable); the point is
// a stable, observable label, not precision.
inline Trajectory classify_trajectory(const std::vector<SocietySnapshot>& s) {
    if (s.empty())
        return Trajectory::Extinct;
    const SocietySnapshot& last = s.back();
    const SocietySnapshot& first = s.front();

    if (last.extinct || last.total_population <= 0.0)
        return Trajectory::Extinct;

    // Left the commons for a market era: the dawn climb completed all the way to a
    // money economy — the success terminus (the modern game takes over from here).
    if (last.reached_market)
        return Trajectory::Thriving;

    double peak = 0.0;
    for (const auto& snap : s)
        peak = std::max(peak, snap.total_population);
    // Grew meaningfully then lost most of it back -> overshoot/crash.
    if (peak > first.total_population * 1.2 && last.total_population < peak * 0.5)
        return Trajectory::OvershootCrash;

    const bool grew = last.total_population > first.total_population;
    const bool specialized = last.specialist_fraction >= 0.05;
    const bool wealth = last.total_capital > 0.0;
    const bool advanced = last.era > first.era || last.businesses > 0;

    // Still in the commons but genuinely developing (climbing eras, specialists, a
    // surplus) — on its way, but it has not completed the climb to a market economy.
    if (advanced && specialized && (wealth || last.mean_surplus > 1.0))
        return Trajectory::Developing;
    if (last.mean_surplus > 1.0 && (specialized || wealth))
        return Trajectory::Developing;
    // Climbed real eras but ended flat (specialists gone, no surplus) — developed, then
    // hit the Malthusian wall and plateaued, rather than never developing at all.
    if (advanced && !specialized && last.mean_surplus <= 1.0)
        return Trajectory::Stalled;
    return Trajectory::BareSubsistence;
}

}  // namespace econlife::society
