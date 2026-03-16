#pragma once

// Supply Chain Module — province-parallel tick module that matches buy
// orders to sell offers within and across provinces, dispatches transit
// shipments, processes arrivals, and handles criminal interception.
//
// See docs/interfaces/supply_chain/INTERFACE.md for the canonical specification.

#include "core/tick/tick_module.h"
#include "modules/supply_chain/supply_chain_types.h"

#include <vector>

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;
struct NPCBusiness;
struct RegionalMarket;
struct TransitShipment;
struct RouteProfile;
class DeterministicRNG;

// ---------------------------------------------------------------------------
// SupplyChainModule — ITickModule implementation for supply chain
//
// Province-parallel for local matching (execute_province).
// Global execute() handles transit arrival processing, interception
// checks, and LOD 1 import processing.
// ---------------------------------------------------------------------------
class SupplyChainModule : public ITickModule {
public:
    std::string_view name() const noexcept override { return "supply_chain"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    std::vector<std::string_view> runs_after() const override {
        return {"production"};
    }

    std::vector<std::string_view> runs_before() const override {
        return {"price_engine"};
    }

    bool is_province_parallel() const noexcept override { return true; }

    // Per-province: local matching of sell offers to buy orders.
    void execute_province(uint32_t province_idx,
                          const WorldState& state,
                          DeltaBuffer& province_delta) override;

    // Global: transit arrival processing, interception checks, LOD 1 imports.
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Utility: good_id string to uint32_t hash ---
    // Uses same algorithm as ProductionModule for consistency.
    static uint32_t good_id_from_string(const std::string& good_id_str);

    // --- Transit time calculation ---
    // Returns the number of ticks for a shipment to travel the given route
    // at the given transport mode speed. Always >= 1 for inter-province.
    static uint32_t compute_transit_ticks(const RouteProfile& route,
                                          float mode_speed,
                                          float infrastructure_rating);

    // --- Transport cost calculation ---
    // Returns the total cost for shipping quantity units along a route.
    static float compute_transport_cost(const RouteProfile& route,
                                        float quantity);

private:
    // Build sell offers from market supply for a given province.
    void collect_sell_offers(uint32_t province_id,
                             const WorldState& state,
                             std::vector<SellOffer>& offers) const;

    // Build buy orders from demand buffer for a given province.
    void collect_buy_orders(uint32_t province_id,
                            const WorldState& state,
                            std::vector<BuyOrder>& orders) const;

    // Match local sell offers to buy orders within the same province.
    // Writes MarketDelta entries for fulfilled supply.
    void match_local(uint32_t province_id,
                     const WorldState& state,
                     DeltaBuffer& delta,
                     std::vector<SellOffer>& offers,
                     std::vector<BuyOrder>& orders) const;

    // Dispatch inter-province shipments for unfulfilled buy orders.
    void dispatch_inter_province(uint32_t province_id,
                                 const WorldState& state,
                                 DeltaBuffer& delta,
                                 const std::vector<BuyOrder>& unfulfilled_orders,
                                 DeterministicRNG& rng) const;

    // Process transit arrivals from deferred work queue.
    void process_transit_arrivals(const WorldState& state,
                                  DeltaBuffer& delta) const;

    // Process criminal shipment interception checks.
    void process_interception_checks(const WorldState& state,
                                     DeltaBuffer& delta,
                                     DeterministicRNG& rng) const;

    // Process LOD 1 import offers.
    void process_lod1_imports(const WorldState& state,
                              DeltaBuffer& delta) const;
};

}  // namespace econlife
