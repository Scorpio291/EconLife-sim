#pragma once

// Trade Infrastructure Module — sequential tick module that processes
// transit shipments: arrivals, perishable degradation, criminal interception
// checks, and transit time calculation for dispatching.
//
// See docs/interfaces/trade_infrastructure/INTERFACE.md for the canonical spec.

#include <cstdint>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "modules/trade_infrastructure/trade_types.h"

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;
class DeterministicRNG;

// ---------------------------------------------------------------------------
// Configuration constants for transit calculations (§18)
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// TradeInfrastructureModule — ITickModule implementation for transit
// ---------------------------------------------------------------------------
class TradeInfrastructureModule : public ITickModule {
   public:
    explicit TradeInfrastructureModule(const TradeInfrastructureConfig& cfg = {}) : cfg_(cfg) {}

    // Returns the mode speed for a given TransportMode from config.
    float speed_for_mode(TransportMode mode) const {
        switch (mode) {
            case TransportMode::road:
                return cfg_.mode_speed_road;
            case TransportMode::rail:
                return cfg_.mode_speed_rail;
            case TransportMode::sea:
                return cfg_.mode_speed_sea;
            case TransportMode::river:
                return cfg_.mode_speed_river;
            case TransportMode::air:
                return cfg_.mode_speed_air;
            default:
                return cfg_.mode_speed_road;
        }
    }

    const TradeInfrastructureConfig& config() const { return cfg_; }

    std::string_view name() const noexcept override { return "trade_infrastructure"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    std::vector<std::string_view> runs_after() const override { return {"supply_chain"}; }

    std::vector<std::string_view> runs_before() const override {
        return {"financial_distribution"};
    }

    bool is_province_parallel() const noexcept override { return false; }

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // Sequential module — execute_province is not used but must exist.
    void execute_province(uint32_t /*province_idx*/, const WorldState& /*state*/,
                          DeltaBuffer& /*province_delta*/) override {}

    // --- Active shipment management ---
    // The module owns in-transit shipments internally since WorldState
    // does not have a transit_shipments vector yet.
    void add_shipment(TransitShipment shipment);
    const std::vector<TransitShipment>& active_shipments() const { return active_shipments_; }
    std::vector<TransitShipment>& active_shipments() { return active_shipments_; }

    // --- Transit time calculation ---
    // Used by supply_chain at dispatch time to compute arrival_tick.
    uint32_t calculate_transit_ticks(const RouteProfile& route, TransportMode mode) const;

    // --- Static utility: perishable degradation ---
    // Applies one tick of perishable decay to a shipment.
    // Returns true if the shipment is still viable (quantity > 0).
    static bool apply_perishable_decay(TransitShipment& shipment, float decay_rate);

    // --- Interception check ---
    // Rolls against the shipment's interception risk.
    // Returns true if intercepted.
    bool check_interception(const TransitShipment& shipment, DeterministicRNG& rng) const;

   private:
    TradeInfrastructureConfig cfg_;
    std::vector<TransitShipment> active_shipments_;

    void process_transit_arrivals(uint32_t current_tick, DeltaBuffer& delta);
    void process_perishable_decay(float decay_rate);
    void process_interception_checks(uint32_t current_tick, DeltaBuffer& delta,
                                     DeterministicRNG& rng);
    void remove_completed_shipments();
};

}  // namespace econlife
