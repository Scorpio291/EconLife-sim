#pragma once

// facility_signals module types.
// Module-specific types for the facility_signals module (Tier 7).
//
// See docs/interfaces/facility_signals/INTERFACE.md for the canonical specification.

#include <cstdint>
#include <string>
#include <vector>

// InvestigatorMeter / InvestigatorMeterStatus are promoted to core (NPC owns
// one) so this module shares the canonical definition rather than duplicating.
#include "core/world_state/npc.h"

namespace econlife {

// ---------------------------------------------------------------------------
// FacilitySignals — four-dimensional signal profile for a facility
// ---------------------------------------------------------------------------
struct FacilitySignals {
    uint32_t facility_id;
    uint32_t business_id;
    float power_consumption_anomaly;  // 0.0-1.0
    float chemical_waste_signature;   // 0.0-1.0
    float foot_traffic_visibility;    // 0.0-1.0
    float olfactory_signature;        // 0.0-1.0
    float scrutiny_mitigation;        // 0.0-1.0; from opsec investment or corruption
    float base_signal_composite;      // computed: weighted sum of four dimensions
    float net_signal;                 // computed: max(0, composite - mitigation)
};

// ---------------------------------------------------------------------------
// FacilityTypeSignalWeights — per-facility-type signal dimension weights
// Loaded from facility_types.csv; must sum to 1.0.
// ---------------------------------------------------------------------------
struct FacilityTypeSignalWeights {
    float w_power_consumption;
    float w_chemical_waste;
    float w_foot_traffic;
    float w_olfactory;
};

// ---------------------------------------------------------------------------
// RegulatorMeterStatus — regulator scrutiny meter status
// ---------------------------------------------------------------------------
enum class RegulatorMeterStatus : uint8_t {
    inactive = 0,
    notice_filed = 1,        // meter >= 0.25
    formal_audit = 2,        // meter >= 0.50
    enforcement_action = 3,  // meter >= 0.75
};

// ---------------------------------------------------------------------------
// RegulatorScrutinyMeter — per-regulator-NPC meter
// ---------------------------------------------------------------------------
struct RegulatorScrutinyMeter {
    float current_level;
    float fill_rate;
    RegulatorMeterStatus status;
    uint32_t opened_tick;
};

}  // namespace econlife
