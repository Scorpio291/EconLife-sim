// Legitimacy causal trace (R1 follow-up). Observation, not a ratchet.
//
// Reproduces the 3-year emergence baseline for several seeds and attributes every
// change in each province's stability, infrastructure, grievance and institutional
// trust (the inputs of national legitimacy) to the module whose deltas caused it.
// Each year it also prints the population-weighted legitimacy target over ALL
// provinces (so a nation-composition effect shows against the world as a whole)
// and the random events each province suffered.
// Attribution uses probe modules inserted between every pair of consecutive base
// modules: a probe records stability when it runs, so the difference between two
// probes is exactly what the module between them (its deltas, applied by the
// orchestrator) did. The probes write nothing, and the test asserts the base
// modules keep their original order.
//
//   ./econlife_emergence_tests "[.legitimacy-trace]"

#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/tick/thread_pool.h"
#include "core/tick/tick_orchestrator.h"
#include "core/world_gen/world_generator.h"
#include "modules/random_events/random_events_module.h"
#include "modules/register_base_game_modules.h"
#include "tests/integration/emergence_harness.h"

using namespace econlife;

namespace {

// Fields attributed per module: the legitimacy inputs and what drives them.
constexpr int kFields = 4;
const char* const kFieldName[kFields] = {"stability", "infrastructure", "grievance", "trust"};
float field(const Province& p, int f) {
    switch (f) {
        case 0:
            return p.conditions.stability_score;
        case 1:
            return p.infrastructure_rating;
        case 2:
            return p.community.grievance_level;
        default:
            return p.community.institutional_trust;
    }
}

struct Ledger {
    // [field][module][province] accumulated change this year.
    std::map<std::string, std::vector<double>> year[kFields];
    std::vector<float> last[kFields];  // value at the previous probe
};

class StabilityProbe final : public ITickModule {
   public:
    StabilityProbe(std::string name, std::string after, std::string before, std::string credited,
                   Ledger& ledger)
        : name_(std::move(name)),
          after_(std::move(after)),
          before_(std::move(before)),
          credited_(std::move(credited)),
          ledger_(ledger) {}
    std::string_view name() const noexcept override { return name_; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    std::vector<std::string_view> runs_after() const override {
        if (after_.empty())
            return {};
        return {after_};
    }
    std::vector<std::string_view> runs_before() const override {
        if (before_.empty())
            return {};
        return {before_};
    }
    void execute(const WorldState& state, DeltaBuffer&) override {
        const size_t n = state.provinces.size();
        for (int f = 0; f < kFields; ++f) {
            auto& last = ledger_.last[f];
            if (last.size() != n) {
                last.assign(n, 0.0f);
                for (size_t p = 0; p < n; ++p)
                    last[p] = field(state.provinces[p], f);
            }
            auto& y = ledger_.year[f][credited_];
            y.resize(n, 0.0);
            for (size_t p = 0; p < n; ++p) {
                const float now = field(state.provinces[p], f);
                y[p] += static_cast<double>(now) - static_cast<double>(last[p]);
                last[p] = now;
            }
        }
    }

   private:
    std::string name_, after_, before_, credited_;
    Ledger& ledger_;
};

std::vector<std::string> base_order() {
    TickOrchestrator o;
    register_base_game_modules(o);
    o.finalize_registration();
    std::vector<std::string> names;
    for (const auto& m : o.modules())
        names.emplace_back(m->name());
    return names;
}

}  // namespace

TEST_CASE("legitimacy trace: who moves stability, by module and province", "[.legitimacy-trace]") {
    const std::vector<std::string> order = base_order();
    for (uint64_t seed : {42ULL, 43ULL, 44ULL, 45ULL}) {
        WorldGeneratorConfig config{};
        config.seed = seed;
        config.province_count = 6;
        config.npc_count = 200;
        config.criminal_baseline = 0.10f;
        config.goods_directory = emergence::find_goods_dir();
        auto [world, player] = WorldGenerator::generate_with_player(config);
        world.player = std::make_unique<PlayerCharacter>(std::move(player));

        Ledger ledger;
        TickOrchestrator orch;
        register_base_game_modules(orch);
        // "!" sorts before every module name, so a probe runs the moment it is ready
        // and the base order cannot shift. Probe 0 runs first and credits whatever
        // happened between ticks (cross-province deltas, deferred work, the
        // previous tick's tail) to "(between ticks)".
        char buf[32];
        std::snprintf(buf, sizeof buf, "!probe_%03zu", size_t{0});
        orch.register_module(
            std::make_unique<StabilityProbe>(buf, "", order.front(), "(between ticks)", ledger));
        for (size_t i = 0; i < order.size(); ++i) {
            std::snprintf(buf, sizeof buf, "!probe_%03zu", i + 1);
            const std::string before = i + 1 < order.size() ? order[i + 1] : "";
            orch.register_module(
                std::make_unique<StabilityProbe>(buf, order[i], before, order[i], ledger));
        }
        orch.finalize_registration();
        std::vector<std::string> seen;
        for (const auto& m : orch.modules())
            if (m->name().substr(0, 7) != "!probe_")
                seen.emplace_back(m->name());
        REQUIRE(seen == order);

        const RandomEventsModule* events = nullptr;
        for (const auto& m : orch.modules())
            if (auto* r = dynamic_cast<const RandomEventsModule*>(m.get()))
                events = r;
        REQUIRE(events != nullptr);

        ThreadPool pool(1);
        uint32_t seen_event_id = 0;
        for (uint32_t y = 1; y <= 3; ++y) {
            for (auto& fl : ledger.year)
                for (auto& [_, v] : fl)
                    std::fill(v.begin(), v.end(), 0.0);
            // Per province: events started this year by category, and the stability
            // each started event will cost over its life (immediate + per-tick x duration).
            const size_t np = world.provinces.size();
            std::vector<std::array<int, 4>> count(np, {0, 0, 0, 0});
            std::vector<double> cost(np, 0.0), infra_hit(np, 0.0);
            std::vector<std::string> log;
            for (uint32_t t = 0; t < 365; ++t) {
                orch.execute_tick(world, pool);
                for (const auto& e : events->active_events()) {
                    if (e.id <= seen_event_id || e.province_id >= np)
                        continue;
                    seen_event_id = std::max(seen_event_id, e.id);
                    const auto c = static_cast<size_t>(e.category);
                    if (c < 4)
                        ++count[e.province_id][c];
                    const double dur = static_cast<double>(e.end_tick - e.started_tick);
                    if (e.category == EventCategory::natural) {
                        cost[e.province_id] += 0.02 * e.severity + 0.01 * e.severity * dur;
                        if (e.template_id != "drought_mild" && e.template_id != "drought_severe")
                            infra_hit[e.province_id] += 0.01 + 0.14 * e.severity;
                    } else if (e.category == EventCategory::accident) {
                        cost[e.province_id] += 0.01 * e.severity + 0.005 * e.severity * dur;
                    }
                    if (y == 3 && e.province_id <= 1) {
                        char line[160];
                        std::snprintf(line, sizeof line, "    p%u tick %u %-18s sev %.3f dur %u",
                                      e.province_id, e.started_tick, e.template_id.c_str(),
                                      e.severity, e.end_tick - e.started_tick);
                        log.emplace_back(line);
                    }
                }
            }

            const auto& nat = world.nations[0];
            std::printf("\n=== seed %llu year %u: nations %zu, nations[0] legitimacy %.3f, gov %d\n",
                        static_cast<unsigned long long>(seed), y, world.nations.size(),
                        static_cast<double>(nat.political_cycle.national_legitimacy),
                        static_cast<int>(nat.government_type));
            std::printf(
                "prov nat   pop(k) stab  trust griev unemp formal infra crime cdom  ineq  "
                "income  surplus sick  homeless resp\n");
            for (const auto& p : world.provinces) {
                const auto* c = p.cohort_stats.get();
                std::printf(
                    "%4u %3u %8.0f %.3f %.3f %.3f %.3f %.3f  %.3f %.3f %.3f %.3f %7.1f %.3f   "
                    "%.3f %.3f    %u\n",
                    p.id, p.nation_id, c ? c->total_population / 1000.0 : 0.0,
                    p.conditions.stability_score, p.community.institutional_trust,
                    p.community.grievance_level, c ? c->unemployment_rate : 0.f,
                    c ? c->formal_employment_rate : 0.f, p.infrastructure_rating,
                    c ? c->crime_rate : 0.f, c ? c->criminal_dominance_index : 0.f,
                    p.conditions.inequality_index, c ? c->mean_income : 0.f,
                    c ? c->subsistence_surplus_ratio : 0.f, c ? c->sick_rate : 0.f,
                    c ? c->homeless_rate : 0.f, static_cast<unsigned>(p.community.response_stage));
            }
            // Population-weighted legitimacy target, as political_cycle computes it.
            double w = 0, tr = 0, st = 0, gr = 0, un = 0;
            for (const auto& p : world.provinces) {
                const double pop = p.cohort_stats ? p.cohort_stats->total_population : 0.0;
                w += pop;
                tr += pop * p.community.institutional_trust;
                st += pop * p.conditions.stability_score;
                gr += pop * p.community.grievance_level;
                un += pop * (p.cohort_stats ? p.cohort_stats->unemployment_rate : 0.f);
            }
            if (w > 0) {
                tr /= w, st /= w, gr /= w, un /= w;
                std::printf(
                    "legitimacy target: 0.35*trust %.3f + 0.45*stab %.3f - 0.50*griev %.3f - "
                    "0.30*unemp %.3f = %.3f (+%.3f +%.3f -%.3f -%.3f)\n",
                    tr, st, gr, un, 0.35 * tr + 0.45 * st - 0.50 * gr - 0.30 * un, 0.35 * tr,
                    0.45 * st, 0.50 * gr, 0.30 * un);
            }
            std::printf("events started this year (natural/accident/economic/human), "
                        "lifetime stability cost, natural infra hit:\n");
            for (size_t p = 0; p < np; ++p)
                std::printf("  p%zu  %2d %2d %2d %2d   cost %.3f   infra %.3f\n", p, count[p][0],
                            count[p][1], count[p][2], count[p][3], cost[p], infra_hit[p]);
            for (const auto& l : log)
                std::printf("%s\n", l.c_str());
            for (int f = 0; f < kFields; ++f) {
            std::printf("%s change this year by module (per province, then mean):\n",
                        kFieldName[f]);
            for (const auto& [mod, v] : ledger.year[f]) {
                double sum = 0.0, mag = 0.0;
                for (double d : v) {
                    sum += d;
                    mag += std::abs(d);
                }
                if (mag < 1e-6)
                    continue;
                std::printf("  %-28s", mod.c_str());
                for (double d : v)
                    std::printf(" %+.4f", d);
                std::printf("  | %+.4f\n", sum / static_cast<double>(v.size()));
            }
            }
        }
    }
}
