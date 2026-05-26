#pragma once

#include <cstdint>
#include <string>

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

    raw_land = 3,  // Undeveloped parcel (Phase 7). No rental yield. Carries a
                   // `subtype_key` (farmland / coastal / wilderness / island /
                   // urban_*) and a `zoned_use` describing what it may be
                   // developed into without a zoning-change application.
                   // Construction onto raw_land lands in Phase 11.
};

// Phase 9 — LocationFlags bitfield values (stored in
// PropertyListing.location_flags).
inline constexpr uint8_t LocationFlag_None = 0u;
inline constexpr uint8_t LocationFlag_Offshore = 1u << 0;       // no tax, concealed transfers
inline constexpr uint8_t LocationFlag_International = 1u << 1;  // multi-flag legal status
inline constexpr uint8_t LocationFlag_Remote = 1u << 2;         // raises construction cost

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
    bool listed_for_sale = false;  // owner has flagged the property as on the market;
                                   // set by ListPropertyForSaleAction (player) or by NPC
                                   // sale-intent logic (future). Buyers can only target
                                   // properties where listed_for_sale == true.

    // Phase 7 — land taxonomy + zoning (meaningful chiefly for raw_land,
    // but stored uniformly).
    std::string subtype_key;            // "farmland" | "coastal" | "wilderness" |
                                        // "island" | "urban_residential" |
                                        // "urban_commercial" | "urban_industrial" | ""
    float parcel_area_hectares = 0.0f;  // distinguishes a 0.05 ha house lot from
                                        // a 500 ha island. 0 = unspecified.
    PropertyType zoned_use =
        PropertyType::residential;  // permitted development
                                    // class. For built properties equals `type`.
                                    // For raw_land, the class it may be developed
                                    // into without a zoning-change application; a
                                    // RequestZoningChangeAction can change it
                                    // subject to local-government approval.

    // Phase 8 — composite property subdivision. A subdivisible building
    // (subtype_key in {apartment_block, office_tower, mixed_use_building,
    // warehouse_complex, retail_center, industrial_park}) can be split
    // into `unit_count` child PropertyListings, each individually
    // ownable/sellable. While subdivided, the parent is a dormant shell
    // (not buyable, not rentable). Re-merge (MergeUnitsAction) is
    // possible when one owner holds the parent and all live children.
    uint32_t parent_property_id = 0;  // 0 = standalone/top-level; else child unit's parent
    uint32_t unit_count = 1;          // units a subdivided parent was split into; 1 otherwise
    bool subdivided = false;          // true on a parent whose children are live

    // Phase 9 — location flags bitfield (LocationFlag_* constants). An
    // offshore parcel conceals its ownership transfers (no institutional
    // evidence token on buy/sell) and is exempt from province property
    // tax (Phase 12). `remote` raises construction cost (Phase 11).
    // `international` carries a multi-flag legal status (forward-looking).
    // "Islands" are simply raw_land with subtype_key="island" + the
    // offshore flag set — that combination is what makes them
    // mechanically distinct.
    uint8_t location_flags = 0u;

    // Phase 12 — property tax + delinquency. Assessed quarterly against
    // the owner. Offshore parcels are exempt. Unpaid assessments
    // accumulate in unpaid_tax_balance and bump
    // consecutive_delinquent_quarters, which Phase 13 escalates to a
    // lien and then a tax-sale auction.
    float unpaid_tax_balance = 0.0f;
    uint32_t last_tax_assessment_tick = 0u;
    uint8_t consecutive_delinquent_quarters = 0u;
    bool tax_lien = false;  // Phase 13: blocks voluntary sale until cleared

    // Invariants:
    //   asking_price >= 0.0
    //   market_value >= 0.0
    //   rental_yield_rate >= 0.0
    //   rental_income_per_tick == market_value * rental_yield_rate (derived)
    //   If rented == false: tenant_id == 0
    //   If listed_for_sale == false: property is not a valid buy target
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

// Phase 3 — NegotiationContext
//
// In-flight below-asking offer that was emitted as a SceneCard to the
// player. The NegotiationContext links the SceneCard.id to the
// underlying real-estate intent (which property, who's buying, at what
// price). real_estate's global post-pass resolves the context by
// reading state.pending_scene_cards for chosen_choice_id; on accept the
// context produces a PendingTransaction at offer_price, on decline the
// context is dropped, on deadline the context expires.
//
// V1 storage: module-private in RealEstateModule::active_negotiations_;
// round-tripped via the v7 module-state hook with schema_tag bumped 2->3.
//
// Choice ID convention: 1 = accept, 2 = decline.
struct NegotiationContext {
    uint32_t scene_card_id;
    uint32_t property_id;
    uint32_t buyer_id;   // the NPC (always for now; future: any actor)
    uint32_t seller_id;  // the player (always for now)
    float offer_price;
    uint32_t offered_tick;
    uint32_t deadline_tick;
};

// --- §33.2 — RealEstateConstants ---
// Configuration constants for real estate market computations.
// These are compile-time defaults; runtime config overrides may be loaded
// from simulation_config.json -> realestate.

struct RealEstateConstants {};

}  // namespace econlife
