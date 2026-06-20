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

    // Labour = working-age heads (the founding population works the land).
    const float working_fraction = cs.working_age_fraction > 0.0f ? cs.working_age_fraction : 0.6f;
    const float labor = static_cast<float>(population) * working_fraction;

    // Natural capital the population can draw food from this tick.
    const float natural_capital =
        cfg_.weight_agricultural_productivity * prov.agricultural_productivity +
        cfg_.weight_arable_land * prov.geography.arable_land_fraction +
        cfg_.weight_forest_forage * prov.geography.forest_coverage +
        cfg_.weight_fisheries * prov.fisheries.current_stock;

    // Knowledge raises the land's carrying capacity (better technique) — the escape
    // from the Malthusian trap. knowledge_level is accumulated by the knowledge module.
    const float knowledge_factor =
        1.0f + cfg_.knowledge_productivity_coupling * state.technology.knowledge_level;
    // Food techs (plough/irrigation/heavy-plough/watermill) raise the carrying ceiling.
    const float tech_food_factor =
        state.tech_effects_for_era(state.technology.current_era).food_mult;
    // Seasonality (relative to Earth) cuts food reliability via lean seasons; gravity
    // does NOT affect the harvest. Earthlike seasonality is neutral.
    const float seasonality_factor = std::clamp(
        1.0f - cfg_.seasonality_food_penalty *
                   (state.hazard_settings.seasonality - earth_hazard().seasonality),
        0.3f, 1.3f);
    const float output = subsistence_output(natural_capital, labor, cfg_) * knowledge_factor *
                         seasonality_factor * tech_food_factor;
    const float ratio = surplus_ratio(output, population, cfg_);

    RegionDelta rd{};
    rd.region_id = prov.region_id;
    rd.subsistence_surplus_replacement = ratio;
    province_delta.region_deltas.push_back(rd);

    if (province_idx >= state.npc_indices_by_home_province.size())
        return;
    const auto& residents = state.npc_indices_by_home_province[province_idx];
    if (residents.empty())
        return;

    // Livelihoods: assign each resident an occupation. Everyone is a food producer
    // (Layer 1) by default; a surplus frees a share into Layer-2 specialists
    // (artisan/healer/trader/...). These are self-employed livelihoods, NOT firms —
    // a business only crystallizes later when a livelihood employs others.
    const auto layer1 = state.occupation_catalog.in_layer(1);
    const auto layer2 = state.occupation_catalog.in_layer(2);
    const uint32_t specialists =
        specialist_count(static_cast<uint32_t>(residents.size()), ratio, cfg_);

    // Proto-capital: food beyond need is stored (grain/herds/tools), controlled by
    // the resident heads/founders — the origin of capital. Split evenly; it is the
    // wealth that later funds the first firms (genesis is founder-capital-gated).
    const float surplus_food = output - static_cast<float>(population) * cfg_.per_capita_food_per_tick;
    const float proto_share =
        (cfg_.proto_capital_rate > 0.0f && ratio > 1.0f && surplus_food > 0.0f)
            ? (cfg_.proto_capital_rate * surplus_food) / static_cast<float>(residents.size())
            : 0.0f;

    for (uint32_t i = 0; i < residents.size(); ++i) {
        const uint32_t idx = residents[i];
        if (idx >= state.significant_npcs.size())
            continue;
        const NPC& npc = state.significant_npcs[idx];

        // Choose this resident's livelihood.
        uint16_t occ = npc.occupation;
        const OccupationDefinition* chosen = nullptr;
        if (i < specialists && !layer2.empty()) {
            const OccupationDefinition* cand = layer2[i % layer2.size()];
            if (ratio >= cand->min_surplus)
                chosen = cand;
        }
        if (chosen == nullptr && !layer1.empty())
            chosen = layer1[i % layer1.size()];
        if (chosen != nullptr)
            occ = chosen->index;

        const bool occupation_changed = (occ != npc.occupation);
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
