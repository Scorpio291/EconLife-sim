#pragma once

#include <cstdint>

namespace econlife {

// =============================================================================
// Healthcare Types — TDD §26
// =============================================================================

// --- §26 — HealthcareProfile ---
// Per-province snapshot of healthcare system quality.
// Set at world load from world.json; updated at runtime by facility investment.

struct HealthcareProfile {
    float access_level;          // 0.0-1.0; 0 = no formal healthcare; 1 = universal coverage
    float quality_level;         // 0.0-1.0; affects NPC health recovery rate and treatment success
    float cost_per_treatment;    // game currency per NPC treatment event
    float capacity_utilisation;  // 0.0-1.0; above overload_threshold (default 0.85), quality degrades

    // Invariants:
    //   access_level in [0.0, 1.0]
    //   quality_level in [0.0, 1.0]
    //   capacity_utilisation in [0.0, 1.0]
    //   cost_per_treatment >= 0.0

    // Added to Province struct: HealthcareProfile healthcare;
    //
    // Healthcare facility (FacilityType::hospital) on build completion:
    //   province.healthcare.access_level  = min(1.0,
    //       access_level + facility.capacity * config.healthcare.access_per_capacity_unit)
    //   province.healthcare.quality_level = weighted_average(existing, facility.quality_rating,
    //                                                        facility.capacity)
    //
    // Health tick update runs at tick step 8 (NPC motivation update).
    // Labour force participation effect runs at tick step 17 (cohort aggregation).
};

}  // namespace econlife
