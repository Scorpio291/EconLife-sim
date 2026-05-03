#pragma once

#include <cstdint>

namespace econlife {

// =============================================================================
// Real Estate Types — TDD §33
// =============================================================================

// --- §33.1 — PropertyType ---
// Resolves audit ambiguity S-4: residential and commercial properties use the same
// PropertyListing struct distinguished by this enum.

enum class PropertyType : uint8_t {
    residential = 0,  // Apartment, house, multi-family building.
                      // Primary use: player residence (PlayerCharacter.residence_id),
                      // rental income, laundering vehicle.
                      // Price drivers: province population density, income level,
                      // criminal_dominance_index (suppresses price).

    commercial = 1,  // Office, retail unit, warehouse.
                     // Primary use: business premises for NPCBusiness (reduces
                     // cost_per_tick by config.realestate.commercial_cost_reduction_rate
                     // when owner_id matches NPCBusiness.owner_id).
                     // Price drivers: infrastructure_rating, formal_employment_rate.

    industrial = 2,  // Factory floor, port facility, storage yard.
                     // Primary use: facility housing for Facility records.
                     // Price drivers: energy_cost_baseline, infrastructure_rating.
};

// --- §33.1 — PropertyListing ---

struct PropertyListing {
    uint32_t id;
    PropertyType type;
    uint32_t province_id;
    uint32_t owner_id;             // player_id, npc_id, or 0 (province-owned / market stock)
    float asking_price;            // current list price; converges toward market_value over time
    float market_value;            // equilibrium price derived from province conditions (§33.2)
    float rental_yield_rate;       // fraction of market_value paid as rent per tick
                                   // residential default: config.realestate.residential_yield_rate
                                   // commercial:          config.realestate.commercial_yield_rate
    float rental_income_per_tick;  // derived: market_value * rental_yield_rate
    bool rented;                   // true if a tenant NPC or business is currently occupying
    uint32_t tenant_id;            // NPC or NPCBusiness id; 0 if not rented
    bool launder_eligible;         // true if owned under a nominee or shell structure;
                                   // enables real_estate LaunderingMethod (§32.4)
    uint32_t purchased_tick;       // tick of last ownership transfer
    float purchase_price;          // price paid at last transfer; used for capital gain calc

    // Invariants:
    //   asking_price >= 0.0
    //   market_value >= 0.0
    //   rental_yield_rate >= 0.0
    //   rental_income_per_tick == market_value * rental_yield_rate (derived)
    //   If rented == false: tenant_id == 0
    //
    // Monthly asking price convergence:
    //   asking_price += (market_value - asking_price) * config.realestate.price_convergence_rate
    //
    // WorldState field: std::vector<PropertyListing> property_listings
    //   (all properties in the world; province-partitioned at load)
    //
    // Province field: float avg_property_value
    //   (mean market_value across all PropertyListing in province; recomputed monthly)
};

// --- §33.2 — RealEstateConstants ---
// Configuration constants for real estate market computations.
// These are compile-time defaults; runtime config overrides may be loaded
// from simulation_config.json -> realestate.

struct RealEstateConstants {};

}  // namespace econlife
