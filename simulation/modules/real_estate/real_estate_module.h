#pragma once

// Real Estate Module — province-parallel tick module that manages
// per-province property markets: rent collection, market value recomputation,
// asking price convergence, buy/sell transactions, commercial tenant
// assignment, and province avg_property_value updates.
//
// See docs/interfaces/real_estate/INTERFACE.md for the canonical specification.

#include <cstdint>
#include <string_view>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "modules/real_estate/real_estate_types.h"

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;
struct Province;

// ---------------------------------------------------------------------------
// RealEstateModule — ITickModule implementation for property markets
// ---------------------------------------------------------------------------
class RealEstateModule : public ITickModule {
   public:
    explicit RealEstateModule(const RealEstateConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "real_estate"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    bool is_province_parallel() const noexcept override { return true; }

    std::vector<std::string_view> runs_after() const override { return {"price_engine"}; }

    std::vector<std::string_view> runs_before() const override { return {"npc_behavior"}; }

    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override;

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Property management (exposed for testing) ---

    void add_property(PropertyListing listing);
    const std::vector<PropertyListing>& properties() const;
    std::vector<PropertyListing>& properties();

    // --- Utilities (exposed for testing) ---

    // Compute the market value for a property based on provincial conditions.
    // Base value is the property's current market_value; modifiers come from
    // the province's criminal_dominance_index and launder_eligible flag.
    float compute_market_value(const PropertyListing& prop, const Province& province) const;

    // Compute rental income: market_value * rental_yield_rate (derived invariant).
    float compute_rental_income(float market_value, float rental_yield_rate) const;

    // Converge asking_price toward market_value by the given rate.
    // asking_price += (market_value - asking_price) * rate
    void converge_asking_price(PropertyListing& prop, float rate) const;

    // Compute average property value for a province as mean of market_values.
    // Returns 0.0f if no properties exist in the given province.
    float compute_avg_property_value(const std::vector<PropertyListing>& props,
                                     uint32_t province_id) const;

   private:
    RealEstateConfig cfg_;

    // Internal property storage — WorldState does not hold property listings.
    // Sorted by id ascending for deterministic processing order.
    std::vector<PropertyListing> properties_;
};

}  // namespace econlife
