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

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/tick/thread_pool.h"
#include "core/tick/tick_orchestrator.h"
#include "core/world_gen/world_generator.h"
#include "core/world_state/apply_deltas.h"
#include "modules/random_events/random_events_module.h"
#include "modules/register_base_game_modules.h"
#include "tests/integration/emergence_harness.h"

using namespace econlife;

namespace {

double D(float x) {
    return static_cast<double>(x);
}

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
                        cost[e.province_id] += 0.02 * D(e.severity) + 0.01 * D(e.severity) * dur;
                        if (e.template_id != "drought_mild" && e.template_id != "drought_severe")
                            infra_hit[e.province_id] += 0.01 + 0.14 * D(e.severity);
                    } else if (e.category == EventCategory::accident) {
                        cost[e.province_id] += 0.01 * D(e.severity) + 0.005 * D(e.severity) * dur;
                    }
                    if (y == 3 && e.province_id <= 1) {
                        char line[160];
                        std::snprintf(line, sizeof line, "    p%u tick %u %-18s sev %.3f dur %u",
                                      e.province_id, e.started_tick, e.template_id.c_str(),
                                      D(e.severity), e.end_tick - e.started_tick);
                        log.emplace_back(line);
                    }
                }
            }

            const auto& nat = world.nations[0];
            std::printf(
                "\n=== seed %llu year %u: nations %zu, nations[0] legitimacy %.3f, gov %d\n",
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
                    D(p.conditions.stability_score), D(p.community.institutional_trust),
                    D(p.community.grievance_level), D(c ? c->unemployment_rate : 0.f),
                    D(c ? c->formal_employment_rate : 0.f), D(p.infrastructure_rating),
                    D(c ? c->crime_rate : 0.f), D(c ? c->criminal_dominance_index : 0.f),
                    D(p.conditions.inequality_index), D(c ? c->mean_income : 0.f),
                    D(c ? c->subsistence_surplus_ratio : 0.f), D(c ? c->sick_rate : 0.f),
                    D(c ? c->homeless_rate : 0.f),
                    static_cast<unsigned>(D(p.community.response_stage)));
            }
            // Population-weighted legitimacy target, as political_cycle computes it.
            double w = 0, tr = 0, st = 0, gr = 0, un = 0;
            for (const auto& p : world.provinces) {
                const double pop = p.cohort_stats ? p.cohort_stats->total_population : 0.0;
                w += pop;
                tr += pop * D(p.community.institutional_trust);
                st += pop * D(p.conditions.stability_score);
                gr += pop * D(p.community.grievance_level);
                un += pop * D(p.cohort_stats ? p.cohort_stats->unemployment_rate : 0.f);
            }
            if (w > 0) {
                tr /= w, st /= w, gr /= w, un /= w;
                std::printf(
                    "legitimacy target: 0.35*trust %.3f + 0.45*stab %.3f - 0.50*griev %.3f - "
                    "0.30*unemp %.3f = %.3f (+%.3f +%.3f -%.3f -%.3f)\n",
                    tr, st, gr, un, 0.35 * tr + 0.45 * st - 0.50 * gr - 0.30 * un, 0.35 * tr,
                    0.45 * st, 0.50 * gr, 0.30 * un);
            }
            // The event roll's inputs, as random_events reads them: rate multiplier
            // (1 + 1.5 climate)(1 + instability)(1 + mean |spot - eq| / eq).
            for (const auto& p : world.provinces) {
                double dev = 0, worst = 0;
                int n = 0;
                for (uint32_t i : markets_in_province(world, p.id)) {
                    const auto& m = world.regional_markets[i];
                    if (m.equilibrium_price > 0.0f) {
                        const double d =
                            std::abs(m.spot_price - m.equilibrium_price) / m.equilibrium_price;
                        dev += d;
                        worst = std::max(worst, d);
                        ++n;
                    }
                }
                const double vol = 1.0 + (n ? dev / n : 0.0);
                const double cs = p.climate.climate_stress_current;
                const double inst = 1.0 - D(p.conditions.stability_score);
                std::printf(
                    "  roll p%u: climate %.3f instab %.3f volatility %.3f (%d markets, "
                    "worst dev %.2f) -> rate x%.2f = %.2f events/yr\n",
                    p.id, cs, inst, vol, n, worst, (1 + 1.5 * cs) * (1 + inst) * vol,
                    0.15 * 12 * (1 + 1.5 * cs) * (1 + inst) * vol);
            }
            {
                // spot / equilibrium across province 0's markets, bucketed.
                int b[6] = {0, 0, 0, 0, 0, 0};  // <0.5, <0.9, <1.1, <2, <2.9, >=2.9
                int zero_supply = 0, zero_demand = 0;
                for (uint32_t i : markets_in_province(world, 0)) {
                    const auto& m = world.regional_markets[i];
                    if (!(m.equilibrium_price > 0.0f))
                        continue;
                    const double r = m.spot_price / m.equilibrium_price;
                    ++b[r < 0.5 ? 0 : r < 0.9 ? 1 : r < 1.1 ? 2 : r < 2 ? 3 : r < 2.9 ? 4 : 5];
                    zero_supply += m.supply <= 0.0f;
                    zero_demand += m.demand_buffer <= 0.0f;
                }
                std::printf(
                    "  p0 spot/eq: <0.5 %d  0.5-0.9 %d  0.9-1.1 %d  1.1-2 %d  2-2.9 %d  "
                    ">=2.9 %d   supply<=0 %d  demand<=0 %d\n",
                    b[0], b[1], b[2], b[3], b[4], b[5], zero_supply, zero_demand);
            }
            std::printf(
                "events started this year (natural/accident/economic/human), "
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

TEST_CASE("legitimacy trace: counterfactual event rates", "[.legitimacy-trace]") {
    // The same worlds with the random-event roll switched off (base rate 0; no
    // cross-module triggers fire in this baseline): what legitimacy is left is what
    // the material and institutional conditions alone sustain. Then at base rate
    // 0.15 / 2.972: the rate the roll would have if markets cleared, since 148 of
    // 151 markets sit at spot = 3 x equilibrium and the volatility multiplier reads
    // 2.972 in every province of every seed.
    for (float rate : {0.0f, 0.15f / 2.972f})
        for (uint64_t seed : {42ULL, 43ULL, 44ULL, 45ULL}) {
            WorldGeneratorConfig config{};
            config.seed = seed;
            config.province_count = 6;
            config.npc_count = 200;
            config.criminal_baseline = 0.10f;
            config.goods_directory = emergence::find_goods_dir();
            auto [world, player] = WorldGenerator::generate_with_player(config);
            world.player = std::make_unique<PlayerCharacter>(std::move(player));
            TickOrchestrator orch;
            register_base_game_modules(orch);
            orch.finalize_registration();
            RandomEventsModule* events = nullptr;
            for (const auto& m : orch.modules())
                if (auto* r = dynamic_cast<RandomEventsModule*>(m.get()))
                    events = r;
            REQUIRE(events != nullptr);
            events->set_base_rate(rate);
            ThreadPool pool(1);
            for (uint32_t y = 1; y <= 3; ++y) {
                for (uint32_t t = 0; t < 365; ++t)
                    orch.execute_tick(world, pool);
                double w = 0, st = 0, gr = 0, tr = 0;
                for (const auto& p : world.provinces) {
                    const double pop = p.cohort_stats ? p.cohort_stats->total_population : 0.0;
                    w += pop;
                    st += pop * D(p.conditions.stability_score);
                    gr += pop * D(p.community.grievance_level);
                    tr += pop * D(p.community.institutional_trust);
                }
                std::printf(
                    "base rate %.4f seed %llu year %u: legitimacy %.3f  stab %.3f griev %.3f "
                    "trust %.3f  events %zu\n",
                    static_cast<double>(rate), static_cast<unsigned long long>(seed), y,
                    static_cast<double>(world.nations[0].political_cycle.national_legitimacy),
                    st / w, gr / w, tr / w, events->active_events().size());
            }
        }
}
