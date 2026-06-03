#pragma once

// population_aging module types.
// DemographicGroup, PopulationCohort, and RegionCohortStats are defined in
// core/world_state/shared_types.h (the canonical home, so RegionCohortStats can
// embed the cohorts map without a core->module dependency). This header re-uses
// them and keeps the module-local count alias for backwards compatibility.
// RegionDemographics is in geography.h.

#include "core/world_state/shared_types.h"

namespace econlife {

static constexpr uint8_t DEMOGRAPHIC_GROUP_COUNT = kDemographicGroupCount;

}  // namespace econlife
