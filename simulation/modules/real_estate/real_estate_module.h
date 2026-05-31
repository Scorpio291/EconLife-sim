#pragma once

// Real Estate Module — province-parallel tick module that manages
// per-province property markets: rent collection, market value recomputation,
// asking price convergence, buy/sell transactions, commercial tenant
// assignment, and province avg_property_value updates.
//
// See docs/interfaces/real_estate/INTERFACE.md for the canonical specification.

#include <cstdint>
#include <string_view>
#include <unordered_map>
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
    // The global post-pass runs after province-parallel work merges.
    // Used by Phase 1 to drain pending_property_transactions (player-
    // initiated list/unlist/buy) and to scan player listings for
    // opportunistic NPC buyers — both touch the module-private
    // properties_ vector and must run single-threaded.
    bool has_global_post_pass() const noexcept override { return true; }

    std::vector<std::string_view> runs_after() const override { return {"price_engine"}; }

    std::vector<std::string_view> runs_before() const override { return {"npc_behavior"}; }

    void init_for_tick(const WorldState& state) override;

    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override;

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // Persistence: properties_ holds genuine world state — every PropertyListing
    // with its owner, tenant, asking price, market value, and rental yield.
    // See ITickModule. Note: on deserialize the per-province index is invalidated;
    // it gets rebuilt by the next init_for_tick or by execute_province itself.
    void serialize_state(std::vector<uint8_t>& out) const override;
    bool deserialize_state(const uint8_t* data, size_t size) override;

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

    // Per-province indices into properties_, built by init_for_tick() on the
    // main thread before parallel dispatch.  Each vector contains indices into
    // properties_ for that province, in ascending id order.  This allows
    // execute_province() to touch only its own province's data, eliminating
    // cross-province data races.
    std::unordered_map<uint32_t, std::vector<size_t>> province_property_indices_;

    // Phase 3 — in-flight below-asking offers awaiting player decision via
    // SceneCard. Resolved in execute()'s global post-pass: accept creates a
    // PendingTransaction, decline drops the context, deadline expires it.
    // Round-tripped via the v7 module-state hook (schema_tag = 3).
    std::vector<NegotiationContext> active_negotiations_;

   public:
    // Exposed for testing.
    const std::vector<NegotiationContext>& active_negotiations() const {
        return active_negotiations_;
    }
    std::vector<NegotiationContext>& active_negotiations_mut() { return active_negotiations_; }
};

}  // namespace econlife
