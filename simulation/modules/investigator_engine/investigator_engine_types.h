#pragma once

// investigator_engine module types.
// Module-specific types for the investigator_engine module (Tier 8).
//
// See docs/interfaces/investigator_engine/INTERFACE.md for the canonical specification.
//
// InvestigatorMeter and InvestigatorMeterStatus are defined in
// facility_signals_types.h (Tier 7) since they're shared infrastructure.
// This header adds investigation-case types specific to the engine itself.

#include <cstdint>
#include <string>
#include <vector>

namespace econlife {

// ---------------------------------------------------------------------------
// InvestigatorType — role classification for investigator NPCs
// ---------------------------------------------------------------------------
enum class InvestigatorType : uint8_t {
    law_enforcement  = 0,  // Drives InvestigatorMeter; reads criminal_sector facilities
    regulator        = 1,  // Reads all facilities; chemical + traffic signals only
    journalist       = 2,  // Reads public_info scope evidence only
    ngo_investigator = 3,  // Not affected by regional_corruption_coverage
};

// ---------------------------------------------------------------------------
// InvestigationCase — internal per-investigator case tracking
// Maintained by InvestigatorEngineModule internally.
// ---------------------------------------------------------------------------
struct InvestigationCase {
    uint32_t investigator_npc_id;
    InvestigatorType investigator_type;
    uint32_t target_id;             // argmax known criminal actor; 0 = no target
    float current_level;            // 0.0-1.0; accumulated meter level
    float fill_rate;                // per-tick increment
    uint8_t status;                 // maps to InvestigatorMeterStatus values
    uint32_t opened_tick;           // tick when investigation formally opened (status >= formal_inquiry)
    bool formally_opened;           // once true, does not close on signal drop alone
    uint32_t province_id;           // province this investigator operates in
};

}  // namespace econlife
