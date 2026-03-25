#pragma once

// Trade Infrastructure Module — sequential tick module that processes
// transit shipments: arrivals, perishable degradation, criminal interception
// checks, and transit time calculation for dispatching.
//
// See docs/interfaces/trade_infrastructure/INTERFACE.md for the canonical spec.

#include <cstdint>
#include <vector>

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
struct TradeInfrastructureConstants {
    // Mode speeds in km per tick
    static constexpr float mode_speed_road = 800.0f;
    static constexpr float mode_speed_rail = 700.0f;
    static constexpr float mode_speed_sea = 900.0f;
    static constexpr float mode_speed_river = 450.0f;
    static constexpr float mode_speed_air = 10000.0f;

    // Transit time delay coefficients
    static constexpr float terrain_delay_coeff = 0.4f;
    static constexpr float infra_delay_coeff = 0.6f;

    // Concealment cap
    static constexpr float max_concealment_modifier = 0.40f;

    // Perishable decay
    static constexpr float perishable_decay_base = 0.01f;

    // Returns the mode speed for a given TransportMode.
    static constexpr float speed_for_mode(TransportMode mode) {
        switch (mode) {
            case TransportMode::road:
                return mode_speed_road;
            case TransportMode::rail:
                return mode_speed_rail;
            case TransportMode::sea:
                return mode_speed_sea;
            case TransportMode::river:
                return mode_speed_river;
            case TransportMode::air:
                return mode_speed_air;
            default:
                return mode_speed_road;
        }
    }
};

// ---------------------------------------------------------------------------
// TradeInfrastructureModule — ITickModule implementation for transit
// ---------------------------------------------------------------------------
class TradeInfrastructureModule : public ITickModule {
   public:
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

    // --- Static utility: transit time calculation ---
    // Used by supply_chain at dispatch time to compute arrival_tick.
    static uint32_t calculate_transit_ticks(const RouteProfile& route, TransportMode mode);

    // --- Static utility: perishable degradation ---
    // Applies one tick of perishable decay to a shipment.
    // Returns true if the shipment is still viable (quantity > 0).
    static bool apply_perishable_decay(TransitShipment& shipment, float decay_rate);

    // --- Static utility: interception check ---
    // Rolls against the shipment's interception risk.
    // Returns true if intercepted.
    static bool check_interception(const TransitShipment& shipment, DeterministicRNG& rng);

   private:
    std::vector<TransitShipment> active_shipments_;

    void process_transit_arrivals(uint32_t current_tick, DeltaBuffer& delta);
    void process_perishable_decay(float decay_rate);
    void process_interception_checks(uint32_t current_tick, DeltaBuffer& delta,
                                     DeterministicRNG& rng);
    void remove_completed_shipments();
};

}  // namespace econlife
