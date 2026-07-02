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

float GrainLogisticsModule::delivered_fraction(LinkType type, float terrain_cost, float infra_bonus,
                                               float gravity_g, const GrainLogisticsConfig& cfg) {
    float mode = cfg.land_mode;
    if (type == LinkType::River)
        mode = cfg.river_mode;
    else if (type == LinkType::Maritime)
        mode = cfg.maritime_mode;

    const float terrain_factor = 1.0f + cfg.terrain_weight * std::clamp(terrain_cost, 0.0f, 1.0f);
    const float infra_factor =
        std::max(0.0f, 1.0f - cfg.infra_relief * std::clamp(infra_bonus, 0.0f, 1.0f));
    const float gravity_factor = 1.0f + cfg.gravity_weight * std::max(0.0f, gravity_g - 1.0f);

    const float k = cfg.k_base * mode * terrain_factor * infra_factor * gravity_factor;
    return std::clamp(1.0f - k, 0.0f, 1.0f);
}

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

    // Publish per province (keyed by region_id, 1:1 with province in world-gen):
    // the catchment surplus, and the urban (non-farm) population it can feed —
    // net_feedable / per-capita food, capped at the province population (a town can't
    // hold more townsfolk than there are people; a pure import-fed city approaches it).
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
        delta.region_deltas.push_back(rd);
    }
}

}  // namespace econlife
