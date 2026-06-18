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

    const float output = subsistence_output(natural_capital, labor, cfg_);
    const float ratio = surplus_ratio(output, population, cfg_);

    RegionDelta rd{};
    rd.region_id = prov.region_id;
    rd.subsistence_surplus_replacement = ratio;
    province_delta.region_deltas.push_back(rd);
}

void SubsistenceModule::execute(const WorldState&, DeltaBuffer&) {
    // Province-parallel: all work happens in execute_province().
}

}  // namespace econlife
