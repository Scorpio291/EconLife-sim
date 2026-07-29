#include "modules/energy_base/energy_base_module.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "core/world_gen/era_catalog.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"

namespace econlife {

bool EnergyBaseModule::regime_active(std::string_view regime) const {
    return regime_in(cfg_.active_regimes, regime);
}

void EnergyBaseModule::execute_province(uint32_t province_idx, const WorldState& state,
                                        DeltaBuffer& province_delta) {
    if (province_idx >= state.provinces.size())
        return;
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

    energy_state_dirty_ = true;

    // Coal is raised and burned over a year, like the harvest. Between annual ticks the
    // published flow simply stands.
    const uint32_t tpy = kTicksPerYear;
    const bool annual = state.current_tick > 0 && state.current_tick % tpy == 0;
    if (!annual)
        return;

    // --- Which seam, and how workable is it -----------------------------------------
    // The best coal in the province: richest, shallowest, most reachable. A province
    // with no coal under it has no escape from its acres, which is most of them.
    const ResourceDeposit* seam = nullptr;
    float best_workability = 0.0f;
    for (const ResourceDeposit& dep : prov.deposits) {
        if (dep.type != ResourceType::Coal || dep.quantity_remaining <= 0.0f)
            continue;
        const float w = seam_workability(dep);
        // Ties broken by the lower id so the choice is deterministic.
        if (seam == nullptr || w > best_workability ||
            (w == best_workability && dep.id < seam->id)) {
            seam = &dep;
            best_workability = w;
        }
    }
    if (seam == nullptr) {
        RegionDelta rd{};
        rd.region_id = prov.region_id;
        rd.ghost_land_fraction_replacement = 0.0f;
        rd.coal_burned_replacement = 0.0f;
        province_delta.region_deltas.push_back(rd);
        return;
    }

    // --- What the province can RAISE ------------------------------------------------
    // Knowing how to sink, drain, ventilate and fire a deep pit is technique, and a poor
    // stranded seam yields less for the same effort than a rich shallow one.
    const float technique = mining_technique(state.technology.knowledge_level, cfg_);

    // --- What the province WANTS (R1C: induced innovation) --------------------------
    // Fuel demand per head, times the share of it that coal actually pays to supply.
    // The wage is the real one — consumption over subsistence, the same w the Malthusian
    // valve runs on. Cheap labour and dear fuel means the machines do not pay and the
    // knowledge sits unused, which is exactly what happened everywhere coal was known
    // and never adopted.
    const float adoption = coal_adoption(cs.subsistence_surplus_ratio, best_workability, cfg_);
    const float wanted_tonnes = static_cast<float>(population) * cfg_.tonnes_per_head_per_year *
                                adoption * technique;

    // --- What is actually there -----------------------------------------------------
    // The seam is finite and located. This is the whole point: the escape from the
    // organic economy is a stock being spent, so it has an end.
    const float in_the_ground = std::max(0.0f, seam->quantity_remaining) *
                                std::max(1.0f, cfg_.tonnes_per_deposit_unit);
    float burned = std::min(wanted_tonnes, in_the_ground);
    if (!(burned > 0.0f))
        burned = 0.0f;

    RegionDelta rd{};
    rd.region_id = prov.region_id;
    rd.coal_burned_replacement = burned;
    rd.ghost_land_fraction_replacement = ghost_land_fraction(burned, population, cfg_);
    province_delta.region_deltas.push_back(rd);

    // Conserved and located: every tonne burned comes out of this named seam in this
    // named province, converted back into the deposit's own units. When it is worked out
    // the ghost acres go with it and the ceiling falls back to what the sun puts on the
    // fields.
    if (burned > 0.0f) {
        DepositDelta dd{};
        dd.province_id = province_idx;
        dd.deposit_id = seam->id;
        dd.quantity_extracted = burned / std::max(1.0f, cfg_.tonnes_per_deposit_unit);
        province_delta.deposit_deltas.push_back(dd);
    }
}

void EnergyBaseModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Regime exit: once the market economy takes over, this module stops publishing and
    // the last commons value would otherwise stand forever. Clear it exactly once.
    const EraDefinition* era = state.era_catalog.by_index(state.technology.current_era);
    if (era != nullptr && regime_active(era->economic_regime))
        return;
    if (!energy_state_dirty_)
        return;
    energy_state_dirty_ = false;
    for (const auto& prov : state.provinces) {
        if (!prov.cohort_stats)
            continue;
        RegionDelta rd{};
        rd.region_id = prov.region_id;
        rd.ghost_land_fraction_replacement = 0.0f;
        rd.coal_burned_replacement = 0.0f;
        delta.region_deltas.push_back(rd);
    }
}

}  // namespace econlife
