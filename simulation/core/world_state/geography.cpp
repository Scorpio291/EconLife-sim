#include "geography.h"

#include "shared_types.h"

namespace econlife {

Province::Province(const Province& other)
    : h3_index(other.h3_index),
      is_pentagon(other.is_pentagon),
      neighbor_count(other.neighbor_count),
      id(other.id),
      fictional_name(other.fictional_name),
      real_world_reference(other.real_world_reference),
      geography(other.geography),
      climate(other.climate),
      deposits(other.deposits),
      demographics(other.demographics),
      infrastructure_rating(other.infrastructure_rating),
      agricultural_productivity(other.agricultural_productivity),
      energy_cost_baseline(other.energy_cost_baseline),
      trade_openness(other.trade_openness),
      lod_level(other.lod_level),
      community(other.community),
      political(other.political),
      conditions(other.conditions),
      significant_npc_ids(other.significant_npc_ids),
      cohort_stats(other.cohort_stats ? std::make_unique<RegionCohortStats>(*other.cohort_stats)
                                      : nullptr),
      tectonic_context(other.tectonic_context),
      rock_type(other.rock_type),
      geology_type(other.geology_type),
      tectonic_stress(other.tectonic_stress),
      plate_age(other.plate_age),
      is_mountain_pass(other.is_mountain_pass),
      island_isolation(other.island_isolation),
      has_permafrost(other.has_permafrost),
      has_estuary(other.has_estuary),
      has_ria_coast(other.has_ria_coast),
      has_fjord(other.has_fjord),
      is_atoll(other.is_atoll),
      has_badlands(other.has_badlands),
      facility_concealment_bonus(other.facility_concealment_bonus),
      has_impact_crater(other.has_impact_crater),
      impact_crater_diameter_km(other.impact_crater_diameter_km),
      impact_mineral_signal(other.impact_mineral_signal),
      has_loess(other.has_loess),
      is_glacial_scoured(other.is_glacial_scoured),
      is_salt_flat(other.is_salt_flat),
      fisheries(other.fisheries),
      border_change_count(other.border_change_count),
      infra_gap(other.infra_gap),
      has_colonial_development_event(other.has_colonial_development_event),
      nomadic_population_fraction(other.nomadic_population_fraction),
      pastoral_carrying_capacity(other.pastoral_carrying_capacity),
      is_nation_capital(other.is_nation_capital),
      history(other.history),
      province_lore(other.province_lore),
      soil_type(other.soil_type),
      irrigation_potential(other.irrigation_potential),
      irrigation_cost_index(other.irrigation_cost_index),
      salinisation_risk(other.salinisation_risk),
      water_availability(other.water_availability),
      settlement_attractiveness(other.settlement_attractiveness),
      disease_burden(other.disease_burden),
      province_archetype_index(other.province_archetype_index),
      has_karst(other.has_karst),
      historical_trauma_index(other.historical_trauma_index),
      region_id(other.region_id),
      nation_id(other.nation_id),
      links(other.links),
      market_ids(other.market_ids) {}

Province& Province::operator=(const Province& other) {
    if (this != &other) {
        Province tmp(other);
        *this = std::move(tmp);
    }
    return *this;
}

}  // namespace econlife
