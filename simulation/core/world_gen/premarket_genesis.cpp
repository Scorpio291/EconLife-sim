#include "core/world_gen/premarket_genesis.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "core/rng/deterministic_rng.h"
#include "core/world_gen/era_catalog.h"
#include "core/world_gen/facility_type_catalog.h"
#include "core/world_gen/recipe_catalog.h"
#include "core/world_gen/world_generator.h"
#include "core/world_state/geography.h"
#include "core/world_state/world_state.h"
#include "modules/grain_logistics/grain_logistics_module.h"
#include "modules/subsistence/subsistence_module.h"

namespace econlife {

uint32_t PremarketGenesis::materialize(WorldState& world, const RecipeCatalog& recipes,
                                       const FacilityTypeCatalog& facility_types,
                                       const WorldGeneratorConfig& config, uint8_t era,
                                       DeterministicRNG& rng) {
    const uint32_t n = static_cast<uint32_t>(world.provinces.size());
    if (n == 0)
        return 0;

    // The SAME laws the climb runs on (runtime defaults — one law, two resolutions).
    const SubsistenceConfig sub{};
    const GrainLogisticsConfig grain{};
    const float food_mult = world.tech_effects_for_era(era).food_mult;

    // 1. Each province's earned grain surplus (subsistence law).
    std::vector<float> surplus(n, 0.0f);
    std::vector<double> pop(n, 0.0);
    for (uint32_t i = 0; i < n; ++i) {
        const Province& p = world.provinces[i];
        if (!p.cohort_stats)
            continue;
        pop[i] = static_cast<double>(p.cohort_stats->total_population);
        const float wf =
            p.cohort_stats->working_age_fraction > 0.0f ? p.cohort_stats->working_age_fraction : 0.6f;
        const float nc = SubsistenceModule::natural_capital_of(p, sub);
        // The SAME chronic ceiling the runtime law applies: technique, climate,
        // era food tech, predators, atmosphere. Applying only food_mult (as this
        // did) overestimated what the land can feed — ~8-9% on a default Earth
        // world, more on a hazard world — and founded workshops for a town the
        // first real harvest could not support. Episodic harvest failure is left
        // out on purpose: it is a per-year draw and genesis has no year.
        const float chronic = SubsistenceModule::chronic_ceiling_factors(
            world.technology.knowledge_level, food_mult, world.hazard_settings, sub);
        const float output =
            SubsistenceModule::subsistence_output(nc, static_cast<float>(pop[i]) * wf, sub) *
            chronic;
        const float need = static_cast<float>(pop[i]) * sub.per_capita_food_per_tick;
        surplus[i] = std::max(0.0f, output - need);
    }

    // 2. The catchment each province can feed a town from (the ox-cart law, M2):
    // each source allocates its surplus across {self + neighbours} weighted by the
    // delivered fraction; the teams eat the difference.
    const auto h3_to_idx = build_h3_to_province_index(world.provinces);
    const float gravity_g = world.hazard_settings.gravity_g;
    std::vector<double> net_feedable(n, 0.0);
    for (uint32_t s = 0; s < n; ++s) {
        if (surplus[s] <= 0.0f)
            continue;
        std::vector<std::pair<uint32_t, float>> dests;
        dests.emplace_back(s, 1.0f);
        for (const auto& link : world.provinces[s].links) {
            auto it = h3_to_idx.find(link.neighbor_h3);
            if (it == h3_to_idx.end())
                continue;
            const float df = GrainLogisticsModule::delivered_fraction(
                link.type, link.transit_terrain_cost, link.infrastructure_bonus, gravity_g, grain);
            if (df > 0.0f)
                dests.emplace_back(it->second, df);
        }
        std::sort(dests.begin(), dests.end());
        double total_w = 0.0;
        for (const auto& d : dests)
            total_w += d.second;
        if (total_w <= 0.0)
            continue;
        for (const auto& d : dests)
            net_feedable[d.first] +=
                static_cast<double>(surplus[s]) * (d.second / total_w) * d.second;
    }

    // 3. Era content: facility types that have an era-available recipe (the M4
    // medieval chains). Deterministic: lowest-era recipe per type, keys sorted.
    std::map<std::string, std::string> recipe_for_type;  // facility_type_key -> recipe_key
    for (const auto& r : recipes.all()) {
        if (r.era_available > era)
            continue;
        if (facility_types.find(r.facility_type_key) == nullptr)
            continue;
        auto it = recipe_for_type.find(r.facility_type_key);
        if (it == recipe_for_type.end() || r.id < it->second)
            recipe_for_type[r.facility_type_key] = r.id;
    }
    // Workshops rotate over the non-manor crafts; the manor is the lords' estate.
    std::vector<std::string> workshop_types;
    for (const auto& [ftype, rkey] : recipe_for_type) {
        if (ftype != "manor_farm")
            workshop_types.push_back(ftype);
    }
    const bool manor_available = recipe_for_type.count("manor_farm") > 0;
    if (workshop_types.empty() && !manor_available)
        return 0;

    const EraDefinition* era_def = world.era_catalog.by_index(era);
    const std::string regime = era_def ? era_def->economic_regime : std::string();
    const bool manorial = is_manorial_regime(regime);

    // Fresh id counters continue from whatever already exists.
    uint32_t next_biz_id = 1;
    for (const auto& b : world.npc_businesses)
        next_biz_id = std::max(next_biz_id, b.id + 1);
    uint32_t next_fac_id = 1;
    for (const auto& f : world.facilities)
        next_fac_id = std::max(next_fac_id, f.id + 1);

    const BusinessProfile profiles[4] = {
        BusinessProfile::cost_cutter, BusinessProfile::quality_player,
        BusinessProfile::fast_expander, BusinessProfile::defensive_incumbent};
    auto sector_for = [&](const std::string& ftype) {
        const FacilityType* ft = facility_types.find(ftype);
        if (ft == nullptr)
            return BusinessSector::manufacturing;
        if (ft->category == "agriculture")
            return BusinessSector::agriculture;
        if (ft->category == "processing")
            return BusinessSector::food_beverage;
        return BusinessSector::manufacturing;
    };

    auto found_firm = [&](uint32_t province_idx, NPC& founder, const std::string& ftype,
                          const std::string& recipe_key, float workers) {
        // The founder ENDOWS the firm from their own proto-capital — a conserved
        // transfer (the wealth that funds the first firms; genesis is founder-gated).
        const float endow = founder.capital * config.premarket_endowment_fraction;
        founder.capital -= endow;

        NPCBusiness biz{};
        biz.id = next_biz_id++;
        biz.owner_id = founder.id;
        biz.province_id = province_idx;
        biz.sector = sector_for(ftype);
        biz.profile = profiles[biz.id % 4];
        biz.cash = endow;
        biz.revenue_per_tick = 0.0f;  // production earns it; nothing conjured
        biz.cost_per_tick = 0.0f;
        biz.market_share = 0.05f;
        biz.strategic_decision_tick = rng.next_uint(90);
        biz.dispatch_day_offset = static_cast<uint8_t>(biz.id % 30);
        biz.actor_tech_state.effective_tech_tier = 1.0f;
        biz.default_activity_scope = VisibilityScope::institutional;
        world.npc_businesses.push_back(std::move(biz));

        Facility fac{};
        fac.id = next_fac_id++;
        fac.business_id = next_biz_id - 1;
        fac.province_id = province_idx;
        fac.recipe_id = recipe_key;
        fac.tech_tier = 1;
        fac.output_rate_modifier = 1.0f;
        fac.soil_health = 1.0f;
        fac.worker_count = static_cast<uint32_t>(workers);
        fac.is_operational = true;
        world.facilities.push_back(std::move(fac));
    };

    uint32_t created = 0;
    for (uint32_t i = 0; i < n; ++i) {
        // Residents of this province, wealth-ranked (capital desc, id asc) — the
        // same emergent ordering that makes lords (G3).
        std::vector<uint32_t> res;
        for (uint32_t k = 0; k < world.significant_npcs.size(); ++k) {
            if (world.significant_npcs[k].home_province_id == i)
                res.push_back(k);
        }
        if (res.empty())
            continue;
        std::sort(res.begin(), res.end(), [&](uint32_t x, uint32_t y) {
            const NPC& a = world.significant_npcs[x];
            const NPC& b = world.significant_npcs[y];
            if (a.capital != b.capital)
                return a.capital > b.capital;
            return a.id < b.id;
        });
        size_t cursor = 0;

        // MANORS: the lord stratum (wealth-ranked, G3) each hold an estate.
        if (manorial && manor_available) {
            const uint32_t lords =
                SubsistenceModule::lord_count(static_cast<uint32_t>(res.size()), sub);
            for (uint32_t l = 0; l < lords && cursor < res.size(); ++l, ++cursor) {
                found_firm(i, world.significant_npcs[res[cursor]], "manor_farm",
                           recipe_for_type.at("manor_farm"), 30.0f);
                ++created;
            }
        }

        // WORKSHOPS: the town the catchment can feed organizes into shops — the
        // urban population at real workshop headcounts. EARNED: a subsistence-locked
        // province (no catchment surplus) gets none.
        if (workshop_types.empty())
            continue;
        const double urban =
            std::min(net_feedable[i] / std::max(grain.urban_per_capita_food, 1e-3f), pop[i]);
        uint32_t shops = static_cast<uint32_t>(
            urban / std::max(config.premarket_workers_per_workshop, 1.0f));
        for (uint32_t s = 0; s < shops && cursor < res.size(); ++cursor) {
            NPC& founder = world.significant_npcs[res[cursor]];
            // Founder-gated: only someone whose wealth could have raised the
            // building owns one (the qualification the proto-capital climb earns).
            // Each founder takes the CHEAPEST craft still owed a shop that they can
            // actually afford, walking the rotation forward from where it stands.
            // Rotating blindly and skipping on failure could never work: residents
            // are ranked by descending wealth, so the moment the rotation landed on
            // a craft the wealthiest remaining resident could not afford, every
            // poorer resident failed the identical test and the province's workshop
            // genesis halted for good — far short of what the catchment can feed.
            const size_t type_count = workshop_types.size();
            const FacilityType* chosen_ft = nullptr;
            size_t chosen_offset = 0;
            for (size_t k = 0; k < type_count; ++k) {
                const size_t idx = (created + k) % type_count;
                const FacilityType* ft = facility_types.find(workshop_types[idx]);
                if (ft == nullptr || founder.capital < ft->base_construction_cost)
                    continue;
                if (chosen_ft == nullptr || ft->base_construction_cost < chosen_ft->base_construction_cost) {
                    chosen_ft = ft;
                    chosen_offset = idx;
                }
            }
            if (chosen_ft == nullptr)
                continue;  // this resident cannot raise any of the era's workshops
            const std::string& ftype = workshop_types[chosen_offset];
            found_firm(i, founder, ftype, recipe_for_type.at(ftype),
                       config.premarket_workers_per_workshop);
            ++created;
            ++s;
        }
    }
    return created;
}

}  // namespace econlife
