#pragma once

// population_aging module types.
// RegionCohortStats is in shared_types.h. RegionDemographics is in geography.h.

#include <cstdint>
#include <string>

namespace econlife {

enum class DemographicGroup : uint8_t {
    youth_urban = 0,
    youth_rural,
    working_urban_low,
    working_urban_mid,
    working_urban_high,
    working_rural_low,
    working_rural_mid,
    working_rural_high,
    retiree_urban,
    retiree_rural,
    student,
    unemployed
};

static constexpr uint8_t DEMOGRAPHIC_GROUP_COUNT = 12;

struct PopulationCohort {
    DemographicGroup group = DemographicGroup::youth_urban;
    uint32_t size = 0;
    float median_income = 0.0f;
    float education_level = 0.0f;
    float employment_rate = 0.0f;
    float political_lean = 0.0f;
    float grievance_contribution = 0.0f;
    float addiction_prevalence = 0.0f;
};

}  // namespace econlife
