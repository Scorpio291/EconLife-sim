#pragma once

// weapons_trafficking module types.
// Module-specific types for the weapons_trafficking module (Tier 8).
//
// See docs/interfaces/weapons_trafficking/INTERFACE.md for the canonical specification.

#include <cstdint>
#include <string>
#include <vector>

namespace econlife {

// ---------------------------------------------------------------------------
// WeaponType — V1 weapon categories
// ---------------------------------------------------------------------------
enum class WeaponType : uint8_t {
    small_arms       = 0,
    ammunition       = 1,
    heavy_weapons    = 2,  // import only; no domestic production; arms embargo item
    converted_legal  = 3,  // converted from legal firearms
};

// ---------------------------------------------------------------------------
// WeaponDiversionRecord — tracking a diversion from legal manufacturing
// ---------------------------------------------------------------------------
struct WeaponDiversionRecord {
    uint32_t business_id;
    WeaponType weapon_type;
    float diversion_fraction;       // 0.0 to max_diversion_fraction (default 0.30)
    float output_diverted;          // units sent to informal market this tick
    float output_formal;            // units remaining in formal market
    uint32_t province_id;
};

// ---------------------------------------------------------------------------
// WeaponProcurementRecord — corrupt official procurement
// ---------------------------------------------------------------------------
struct WeaponProcurementRecord {
    uint32_t corrupt_npc_id;        // military_officer or law_enforcement
    uint32_t obligation_node_id;    // ObligationNode with player as creditor
    WeaponType weapon_type;
    float quantity;
    uint32_t province_id;
};

// ---------------------------------------------------------------------------
// TerritorialConflictDemandModifier — maps conflict stage to demand modifier
// ---------------------------------------------------------------------------
struct TerritorialConflictDemandModifier {
    // none=0.0, economic/intelligence_harassment=0.2,
    // property_violence=0.5, personnel_violence=0.8, open_warfare=1.5
    static float get_modifier(uint8_t conflict_stage) {
        switch (conflict_stage) {
            case 0: return 0.0f;   // none
            case 1: return 0.2f;   // economic
            case 2: return 0.2f;   // intelligence_harassment
            case 3: return 0.5f;   // property_violence
            case 4: return 0.8f;   // personnel_violence
            case 5: return 1.5f;   // open_warfare
            case 6: return 0.0f;   // resolution
            default: return 0.0f;
        }
    }
};

}  // namespace econlife
