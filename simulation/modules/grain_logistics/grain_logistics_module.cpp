#include "modules/grain_logistics/grain_logistics_module.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/world_gen/era_catalog.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"

namespace econlife {

bool GrainLogisticsModule::regime_active(std::string_view regime) const {
    return regime_in(cfg_.active_regimes, regime);
}

void GrainLogisticsModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Regime gate: only the pre-market (commons) eras have a commons grain surplus to
    // haul. In market eras this module is inert.
    const EraDefinition* era = state.era_catalog.by_index(state.technology.current_era);
    if (era == nullptr || !regime_active(era->economic_regime))
        return;

    const uint32_t n = static_cast<uint32_t>(state.provinces.size());
    if (n == 0)
        return;

    // h3_index -> province index, for O(1) link-neighbour resolution.
    const auto h3_to_idx = build_h3_to_province_index(state.provinces);

    const float gravity_g = state.hazard_settings.gravity_g;

    // Conserved one-pass allocation. Each source province distributes its haulable
    // surplus across {itself + resolvable neighbours}, weighted by the delivered
    // fraction (grain flows to the cheapest destinations and arrives most intact
    // there); the team eats (1 - df) of whatever is sent over each link. net_feedable
    // sums the delivered grain. Determinism: sources in province order, destinations
    // sorted by index; double accumulation in that fixed order.
    std::vector<double> net_feedable(n, 0.0);
    for (uint32_t s = 0; s < n; ++s) {
        const Province& src = state.provinces[s];
        if (!src.cohort_stats)
            continue;
        const float surplus = src.cohort_stats->grain_surplus;
        if (surplus <= 0.0f)
            continue;

        // Destinations: self (df = 1.0, no transport) + each resolvable neighbour link.
        std::vector<std::pair<uint32_t, float>> dests;
        dests.reserve(src.links.size() + 1);
        dests.emplace_back(s, 1.0f);
        for (const auto& link : src.links) {
            auto it = h3_to_idx.find(link.neighbor_h3);
            if (it == h3_to_idx.end())
                continue;
            const float df = delivered_fraction(link.type, link.transit_terrain_cost,
                                                link.infrastructure_bonus, gravity_g, cfg_);
            if (df <= 0.0f)
                continue;  // beyond the economic radius — all eaten in transit
            dests.emplace_back(it->second, df);
        }
        std::sort(dests.begin(), dests.end(),
                  [](const std::pair<uint32_t, float>& a, const std::pair<uint32_t, float>& b) {
                      return a.first < b.first;
                  });

        double total_w = 0.0;
        for (const auto& d : dests)
            total_w += static_cast<double>(d.second);
        if (total_w <= 0.0)
            continue;

        for (const auto& d : dests) {
            const double alloc = static_cast<double>(surplus) * (static_cast<double>(d.second) / total_w);
            const double delivered = alloc * static_cast<double>(d.second);  // ox eats (1 - df)
            net_feedable[d.first] += delivered;
        }
    }

    // REAL grain flows (G4): stored grain diffuses down the scarcity gradient along
    // links — kin networks, tribute, trade-in-kind — and every haul pays the ox law
    // (the transit loss is eaten by the draft teams: an explicit conserved sink).
    // Per tick: a per-year share of the granary-FULLNESS gap closes. This is what
    // makes the catchment a FLOW on the actual granaries, not parallel bookkeeping —
    // a river network is real famine insurance.
    const float tpy = static_cast<float>(
        subsistence_cfg_.ticks_per_year > 0 ? subsistence_cfg_.ticks_per_year : 365);
    const float rate_tick = cfg_.grain_trade_rate_per_year / tpy;
    std::vector<double> avail(n, 0.0), target(n, 0.0), store_delta(n, 0.0);
    for (uint32_t i = 0; i < n; ++i) {
        if (!state.provinces[i].cohort_stats)
            continue;
        const auto& cs = *state.provinces[i].cohort_stats;
        avail[i] = std::max(0.0f, cs.food_store);
        // The province's own reserve target: what a full granary means here.
        target[i] = static_cast<double>(cs.total_population) *
                    subsistence_cfg_.per_capita_food_per_tick * tpy *
                    subsistence_cfg_.granary_reserve_years;
    }
    if (rate_tick > 0.0f) {
        for (uint32_t src = 0; src < n; ++src) {
            if (target[src] <= 0.0 || avail[src] <= 0.0)
                continue;
            // Destinations in ascending index order (deterministic).
            std::vector<std::pair<uint32_t, float>> dests;
            for (const auto& link : state.provinces[src].links) {
                auto it = h3_to_idx.find(link.neighbor_h3);
                if (it == h3_to_idx.end())
                    continue;
                const float df = delivered_fraction(link.type, link.transit_terrain_cost,
                                                    link.infrastructure_bonus, gravity_g, cfg_);
                if (df > 0.0f)
                    dests.emplace_back(it->second, df);
            }
            std::sort(dests.begin(), dests.end());
            for (const auto& [dst, df] : dests) {
                if (target[dst] <= 0.0)
                    continue;
                const double f_src = avail[src] / target[src];
                const double f_dst = avail[dst] / target[dst];
                if (f_src <= f_dst)
                    continue;  // grain flows down the scarcity gradient only
                const double sent =
                    std::min(avail[src], rate_tick * (f_src - f_dst) * target[dst]);
                if (sent <= 0.0)
                    continue;
                const double delivered = sent * df;  // the oxen eat the rest en route
                avail[src] -= sent;
                store_delta[src] -= sent;
                avail[dst] += delivered;
                store_delta[dst] += delivered;
            }
        }
    }

    // Publish per province (keyed by region_id, 1:1 with province in world-gen):
    // the catchment surplus, the urban (non-farm) population it can feed —
    // net_feedable / per-capita food, capped at the province population (a town can't
    // hold more townsfolk than there are people; a pure import-fed city approaches it)
    // — and the conserved grain-flow deltas.
    const float per_capita = cfg_.urban_per_capita_food > 0.0f ? cfg_.urban_per_capita_food : 1.0f;
    for (uint32_t i = 0; i < n; ++i) {
        RegionDelta rd{};
        rd.region_id = state.provinces[i].region_id;
        rd.net_feedable_surplus_replacement = static_cast<float>(net_feedable[i]);
        float urban = static_cast<float>(net_feedable[i] / per_capita);
        if (state.provinces[i].cohort_stats) {
            const float pop = static_cast<float>(state.provinces[i].cohort_stats->total_population);
            if (urban > pop)
                urban = pop;
        }
        rd.urban_population_replacement = urban;
        if (store_delta[i] != 0.0)
            rd.food_store_delta = static_cast<float>(store_delta[i]);
        delta.region_deltas.push_back(rd);
    }
}

}  // namespace econlife
