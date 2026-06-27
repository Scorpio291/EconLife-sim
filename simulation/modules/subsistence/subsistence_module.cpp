#include "modules/subsistence/subsistence_module.h"

#include <algorithm>
#include <cmath>

#include "core/world_gen/era_catalog.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"

namespace econlife {

float SubsistenceModule::subsistence_output(float natural_capital, float labor,
                                            const SubsistenceConfig& cfg) {
    if (natural_capital <= 0.0f || labor <= 0.0f)
        return 0.0f;
    const float ceiling = cfg.ceiling_per_capital_unit * natural_capital;
    // Diminishing returns on labour: output -> ceiling as labour grows. At
    // labor == labor_half_saturation, output is ~63% (1 - 1/e) of the ceiling.
    const float half = cfg.labor_half_saturation > 0.0f ? cfg.labor_half_saturation : 1.0f;
    const float saturation = 1.0f - std::exp(-labor / half);
    return ceiling * saturation;
}

float SubsistenceModule::surplus_ratio(float output, uint32_t population,
                                       const SubsistenceConfig& cfg) {
    const float need = static_cast<float>(population) * cfg.per_capita_food_per_tick;
    if (need <= 0.0f)
        return 1.0f;  // no mouths to feed -> trivially "fed"
    return output / need;
}

uint32_t SubsistenceModule::specialist_count(uint32_t residents, float surplus,
                                             const SubsistenceConfig& cfg) {
    if (residents == 0 || surplus <= 1.0f)
        return 0;
    float freed = std::min(surplus - 1.0f, std::max(0.0f, cfg.max_specialist_fraction));
    return static_cast<uint32_t>(static_cast<float>(residents) * freed);
}

bool SubsistenceModule::regime_active(std::string_view regime) const {
    for (const auto& r : cfg_.active_regimes) {
        if (r == regime)
            return true;
    }
    return false;
}

bool SubsistenceModule::regime_manorial(std::string_view regime) const {
    for (const auto& r : cfg_.manorial_regimes) {
        if (r == regime)
            return true;
    }
    return false;
}

float SubsistenceModule::proto_share_for(uint32_t resident_index, uint32_t residents_count,
                                         float total_proto, bool manorial,
                                         const SubsistenceConfig& cfg) {
    if (residents_count == 0 || total_proto <= 0.0f)
        return 0.0f;
    const float n = static_cast<float>(residents_count);
    const float even = total_proto / n;
    if (!manorial)
        return even;
    // Lords: the first `lord_fraction` of residents (deterministic by index; >= 1).
    const float tithe = std::clamp(cfg.manorial_tithe_rate, 0.0f, 1.0f);
    uint32_t lords = static_cast<uint32_t>(std::lround(cfg.manorial_lord_fraction * n));
    if (lords < 1)
        lords = 1;
    if (lords > residents_count)
        lords = residents_count;
    const float peasant_base = total_proto * (1.0f - tithe) / n;
    const float lord_bonus = (total_proto * tithe) / static_cast<float>(lords);
    return (resident_index < lords) ? peasant_base + lord_bonus : peasant_base;
}

float SubsistenceModule::specialist_ceiling(std::string_view regime) const {
    // The non-farming share a regime can sustain rises as the economy monetizes
    // (coinage/money) and then institutionalizes production (feudal guilds/towns ->
    // mercantile trade & finance -> industrial factory/wage labour).
    if (regime == "subsistence")
        return cfg_.specialist_ceiling_subsistence;
    if (regime == "barter")
        return cfg_.specialist_ceiling_barter;
    if (regime == "coinage")
        return cfg_.specialist_ceiling_coinage;
    if (regime == "money")
        return cfg_.specialist_ceiling_money;
    if (regime == "feudal")
        return cfg_.specialist_ceiling_feudal;
    if (regime == "mercantile")
        return cfg_.specialist_ceiling_mercantile;
    if (regime == "industrial")
        return cfg_.specialist_ceiling_industrial;
    return cfg_.max_specialist_fraction;  // fallback for any other active regime
}

void SubsistenceModule::execute_province(uint32_t province_idx, const WorldState& state,
                                         DeltaBuffer& province_delta) {
    if (province_idx >= state.provinces.size())
        return;

    // Regime gate: only the pre-market eras run the commons food path. In market
    // eras this module is inert and the field keeps its default ("fed").
    const EraDefinition* era = state.era_catalog.by_index(state.technology.current_era);
    if (era == nullptr || !regime_active(era->economic_regime))
        return;

    const Province& prov = state.provinces[province_idx];
    if (!prov.cohort_stats)
        return;
    const RegionCohortStats& cs = *prov.cohort_stats;
    const uint32_t population = cs.total_population;
    if (population == 0)
        return;

    // Working-age fraction of the population is available to labour on the land.
    const float working_fraction = cs.working_age_fraction > 0.0f ? cs.working_age_fraction : 0.6f;

    // Natural capital the population can draw food from this tick.
    const float natural_capital =
        cfg_.weight_agricultural_productivity * prov.agricultural_productivity +
        cfg_.weight_arable_land * prov.geography.arable_land_fraction +
        cfg_.weight_forest_forage * prov.geography.forest_coverage +
        cfg_.weight_fisheries * prov.fisheries.current_stock;

    // Knowledge raises the land's carrying capacity (better technique) — the escape
    // from the Malthusian trap. knowledge_level is accumulated by the knowledge module.
    // Saturating (diminishing returns) so the ceiling tends toward a realistic limit
    // rather than exploding at high knowledge (which would crash the surplus).
    const float K = state.technology.knowledge_level;
    const float knowledge_factor =
        1.0f + cfg_.knowledge_productivity_max * K /
                   (K + std::max(1.0f, cfg_.knowledge_productivity_halfsat));
    // Food techs (plough/irrigation/heavy-plough/watermill) raise the carrying ceiling.
    const float tech_food_factor =
        state.tech_effects_for_era(state.technology.current_era).food_mult;
    // Seasonality (relative to Earth) cuts food reliability via lean seasons; gravity
    // does NOT affect the harvest. Earthlike seasonality is neutral.
    const float seasonality_factor = std::clamp(
        1.0f - cfg_.seasonality_food_penalty *
                   (state.hazard_settings.seasonality - earth_hazard().seasonality),
        0.3f, 1.3f);
    // Carrying ceiling: the maximum food this land can yield given technique. Output
    // saturates toward it with labour (diminishing returns). Specialists do not farm,
    // so only the food-producers' labour counts toward output.
    const float base_ceiling = cfg_.ceiling_per_capital_unit * natural_capital * knowledge_factor *
                               seasonality_factor * tech_food_factor;
    const float half = cfg_.labor_half_saturation > 0.0f ? cfg_.labor_half_saturation : 1.0f;
    const float need = static_cast<float>(population) * cfg_.per_capita_food_per_tick;  // per tick
    const float ticks_per_year =
        cfg_.ticks_per_year > 0 ? static_cast<float>(cfg_.ticks_per_year) : 365.0f;

    // The granary demands a permanent production surplus: stored grain spoils, so a
    // society must keep producing extra just to hold its reserves, and more still while
    // building them up. That standing demand (NOT a margin) is what frees a standing
    // specialist class. Work per tick.
    const float target_store = cfg_.granary_reserve_years * need * ticks_per_year;
    const float spoilage = cfg_.granary_spoilage_rate * cs.food_store / ticks_per_year;  // /tick
    const float build = cs.food_store < target_store
                            ? cfg_.granary_build_rate * (target_store - cs.food_store) / ticks_per_year
                            : 0.0f;
    const float granary_demand = spoilage + build;  // extra food/tick the reserves require

    // GROUNDED specialization: how many people must farm to feed everyone AND keep the
    // granary whole. Whoever is left is free to specialize. As knowledge raises the
    // ceiling, fewer farmers are needed, so more are freed — specialization rises with
    // technique, with no heuristic cap doing the work.
    const float desired_output = need + granary_demand;
    const float total_labor = static_cast<float>(population) * working_fraction;
    float labor_needed;
    if (base_ceiling <= 0.0f || desired_output >= base_ceiling) {
        labor_needed = total_labor;  // can't reach the target even with everyone farming
    } else {
        labor_needed = -half * std::log(1.0f - desired_output / base_ceiling);
    }
    const float farmers_needed = labor_needed / std::max(working_fraction, 0.01f);
    float specialists_people = static_cast<float>(population) - farmers_needed;
    // Ceiling rises along the pre-market arc (commons -> barter -> coinage -> money ->
    // feudal -> mercantile -> industrial): money/markets, then guilds/trade/finance and
    // the factory system, let a larger non-farming share be sustained.
    const float specialist_ceiling_frac = specialist_ceiling(era->economic_regime);
    specialists_people = std::clamp(specialists_people, 0.0f,
                                    static_cast<float>(population) * specialist_ceiling_frac);
    const float specialist_fraction =
        population > 0 ? specialists_people / static_cast<float>(population) : 0.0f;

    // Actual harvest from the farmers who remain on the land.
    const float farm_labor = (static_cast<float>(population) - specialists_people) * working_fraction;
    const float output = base_ceiling * (1.0f - std::exp(-std::max(0.0f, farm_labor) / half));

    // Granary: bank the year's net food (after feeding everyone and losing spoilage),
    // or draw it down, once per year. A conserved, capped per-province stock.
    const float net_per_tick = output - need - spoilage;
    float new_store = cs.food_store;
    const bool annual = state.current_tick > 0 && state.current_tick % cfg_.ticks_per_year == 0;
    if (annual)
        new_store = std::clamp(cs.food_store + net_per_tick * ticks_per_year, 0.0f, target_store);

    // The long-run food signal that paces population growth: output relative to what a
    // sustainable society must produce — feed everyone AND replace the grain that spoils
    // out of a full reserve. At output == need + full upkeep this reads 1.0, so the
    // population settles at its sustainable ceiling, LEAVING the upkeep as a permanent
    // surplus that frees the specialist class. Above it the population can grow; below,
    // it eases off. (Starvation is handled separately, gated on the granary running dry.)
    const float full_upkeep =
        cfg_.granary_spoilage_rate * cfg_.granary_reserve_years * need;  // per tick, at full store
    const float growth_surplus =
        need > 0.0f ? output / (need + full_upkeep) : 1.0f;

    RegionDelta rd{};
    rd.region_id = prov.region_id;
    rd.subsistence_surplus_replacement = growth_surplus;
    rd.food_store_replacement = new_store;
    // Absolute haulable grain surplus (output beyond bare need) — the grain available
    // to move/feed non-farmers, consumed by grain_logistics (the ox-cart, §3.5).
    rd.grain_surplus_replacement = std::max(0.0f, output - need);
    province_delta.region_deltas.push_back(rd);

    if (province_idx >= state.npc_indices_by_home_province.size())
        return;
    const auto& residents = state.npc_indices_by_home_province[province_idx];
    if (residents.empty())
        return;

    // Livelihoods: assign each resident an occupation. Everyone is a food producer
    // (Layer 1) by default; the food-balance share above is freed into Layer-2
    // specialists (knowledge-keepers first). Self-employed livelihoods, NOT firms.
    const auto layer1 = state.occupation_catalog.in_layer(1);
    // Layer-2 specialists available at this era: knowledge-keepers unlock over time
    // (elder at the dawn -> scribe once writing exists -> scholar with formal
    // scholarship), so the knowledge trickle starts tiny and accelerates.
    const auto layer2 = state.occupation_catalog.in_layer_for_era(2, state.technology.current_era);
    const uint32_t specialists = std::min(
        static_cast<uint32_t>(static_cast<float>(residents.size()) * specialist_fraction),
        static_cast<uint32_t>(residents.size()));

    // Proto-capital: food beyond need is stored (grain/herds/tools), controlled by
    // the resident heads/founders — the origin of capital. In the egalitarian commons
    // it splits evenly; under MANORIALISM (feudal+ regimes) a tithe concentrates it
    // toward a lord stratum (the lord/peasant divide). It is the wealth that later
    // funds the first firms (genesis is founder-capital-gated).
    const float surplus_food = output - need;
    const float total_proto = (cfg_.proto_capital_rate > 0.0f && surplus_food > 0.0f)
                                  ? cfg_.proto_capital_rate * surplus_food
                                  : 0.0f;
    const bool manorial = regime_manorial(era->economic_regime);

    for (uint32_t i = 0; i < residents.size(); ++i) {
        const uint32_t idx = residents[i];
        if (idx >= state.significant_npcs.size())
            continue;
        const NPC& npc = state.significant_npcs[idx];

        // Choose this resident's livelihood. The food balance already decided HOW MANY
        // can be freed from farming; here we just spread them across the era's available
        // Layer-2 roles, knowledge-keepers first. Everyone else farms (Layer 1).
        uint16_t occ = npc.occupation;
        const OccupationDefinition* chosen = nullptr;
        if (i < specialists && !layer2.empty())
            chosen = layer2[i % layer2.size()];
        if (chosen == nullptr && !layer1.empty())
            chosen = layer1[i % layer1.size()];
        if (chosen != nullptr)
            occ = chosen->index;

        const bool occupation_changed = (occ != npc.occupation);
        const float proto_share = proto_share_for(i, static_cast<uint32_t>(residents.size()),
                                                  total_proto, manorial, cfg_);
        if (!occupation_changed && proto_share <= 0.0f)
            continue;  // nothing to write for this resident

        NPCDelta nd{};
        nd.npc_id = npc.id;
        if (occupation_changed)
            nd.new_occupation = occ;
        if (proto_share > 0.0f)
            nd.capital_delta = proto_share;
        province_delta.npc_deltas.push_back(nd);
    }
}

void SubsistenceModule::execute(const WorldState&, DeltaBuffer&) {
    // Province-parallel: all work happens in execute_province().
}

}  // namespace econlife
