#pragma once

// drug_economy module types.
// Module-specific types for the drug_economy module (Tier 8).
//
// See docs/interfaces/drug_economy/INTERFACE.md for the canonical specification.

#include <cstdint>
#include <string>
#include <vector>

namespace econlife {

// ---------------------------------------------------------------------------
// DrugType — V1 drug categories
// ---------------------------------------------------------------------------
enum class DrugType : uint8_t {
    cannabis         = 0,
    methamphetamine  = 1,
    synthetic_opioid = 2,
    designer_drug    = 3,
    // EX reserved:
    // cocaine        = 4,
    // heroin         = 5,
};

// ---------------------------------------------------------------------------
// DrugMarketTier — wholesale vs retail distribution
// ---------------------------------------------------------------------------
enum class DrugMarketTier : uint8_t {
    wholesale = 0,  // transacts with other criminal businesses
    retail    = 1,  // transacts with background NPC population cohorts
};

// ---------------------------------------------------------------------------
// DrugLegalizationStatus — per-province per-drug legal status
// ---------------------------------------------------------------------------
struct DrugLegalizationStatus {
    bool cannabis_legal;          // if true, cannabis prices through formal market
    bool methamphetamine_legal;   // always false in V1
    bool synthetic_opioid_legal;  // always false in V1
    bool designer_drug_legal;     // true until scheduled (see designer_drug module)

    bool is_legal(DrugType type) const {
        switch (type) {
            case DrugType::cannabis:         return cannabis_legal;
            case DrugType::methamphetamine:  return methamphetamine_legal;
            case DrugType::synthetic_opioid: return synthetic_opioid_legal;
            case DrugType::designer_drug:    return designer_drug_legal;
            default: return false;
        }
    }
};

// ---------------------------------------------------------------------------
// DrugProductionRecord — internal tracking for drug facility production
// ---------------------------------------------------------------------------
struct DrugProductionRecord {
    uint32_t business_id;
    DrugType drug_type;
    DrugMarketTier market_tier;
    float output_quantity;
    float output_quality;          // 0.0-1.0; degrades through distribution
    float precursor_consumed;
    uint32_t province_id;
};

}  // namespace econlife
