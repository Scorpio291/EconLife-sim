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
    double mean_surplus = 0.0;         // mean subsistence_surplus_ratio over provinces
    double specialist_fraction = 0.0;  // Layer-2 livelihoods / livelihoods assigned
    double total_capital = 0.0;        // sum of significant-NPC capital (proto-capital)
    double capital_gini = 0.0;         // inequality of that capital [0,1]
    uint32_t businesses = 0;           // emergent firms
    int era = 0;
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

    double surplus_sum = 0.0;
    int prov_with_cohorts = 0;
    for (const auto& p : w.provinces) {
        if (!p.cohort_stats)
            continue;
        s.total_population += static_cast<double>(p.cohort_stats->total_population);
        surplus_sum += p.cohort_stats->subsistence_surplus_ratio;
        ++prov_with_cohorts;
    }
    s.mean_surplus = prov_with_cohorts > 0 ? surplus_sum / prov_with_cohorts : 0.0;
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
    s.businesses = static_cast<uint32_t>(w.npc_businesses.size());
    return s;
}

// Boot a dawn (era 1, founding-seed) world shaped by `arch` (the world-spectrum
// dial: Bounty + World-Class hazard) and run it `years` in-game years,
// capturing one SocietySnapshot per year (plus the t=0 snapshot).
inline std::vector<SocietySnapshot> run_society_years(uint64_t seed, uint32_t npc_count,
                                                      uint32_t years, const WorldArchetype& arch,
                                                      float founding_hardiness = 0.0f) {
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

    std::vector<SocietySnapshot> series;
    series.reserve(years + 1);
    series.push_back(capture_society(world, 0));
    for (uint32_t y = 0; y < years; ++y) {
        for (uint32_t t = 0; t < 365; ++t)
            orch.execute_tick(world, pool);
        series.push_back(capture_society(world, y + 1));
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

    if (grew && specialized && (wealth || advanced) &&
        (last.specialist_fraction >= 0.2 || advanced))
        return Trajectory::Thriving;
    if (last.mean_surplus > 1.0 && (specialized || wealth))
        return Trajectory::Developing;
    return Trajectory::BareSubsistence;
}

}  // namespace econlife::society
