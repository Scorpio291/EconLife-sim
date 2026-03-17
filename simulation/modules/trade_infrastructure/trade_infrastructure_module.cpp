// Trade Infrastructure Module — implementation.
//
// Processes transit shipments each tick:
//   1. Transit arrivals: deposit goods into destination RegionalMarket
//   2. Perishable degradation: decay quantity and quality for in-transit goods
//   3. Criminal interception: risk-based checks for criminal shipments
//   4. Cleanup: remove completed/lost/intercepted shipments
//
// Sequential (not province-parallel) because shipments cross province boundaries.

#include <algorithm>
#include <cmath>

#include "modules/trade_infrastructure/trade_infrastructure_module.h"
#include "core/rng/deterministic_rng.h"
#include "core/world_state/world_state.h"
#include "core/world_state/delta_buffer.h"

namespace econlife {

// ---------------------------------------------------------------------------
// execute — main tick entry point
// ---------------------------------------------------------------------------
void TradeInfrastructureModule::execute(const WorldState& state, DeltaBuffer& delta) {
    const uint32_t current_tick = state.current_tick;

    // Fork RNG for deterministic interception checks.
    // Context id uses a fixed salt for this module.
    DeterministicRNG rng = DeterministicRNG(state.world_seed).fork(current_tick * 7919u + 18u);

    // Step 1: Process transit arrivals — shipments that have reached their destination.
    process_transit_arrivals(current_tick, delta);

    // Step 2: Apply perishable degradation to all still-in-transit shipments.
    process_perishable_decay(TradeInfrastructureConstants::perishable_decay_base);

    // Step 3: Criminal interception checks for in-transit criminal shipments.
    process_interception_checks(current_tick, delta, rng);

    // Step 4: Remove shipments that are no longer active (arrived, intercepted, lost).
    remove_completed_shipments();
}

// ---------------------------------------------------------------------------
// process_transit_arrivals
// ---------------------------------------------------------------------------
void TradeInfrastructureModule::process_transit_arrivals(uint32_t current_tick,
                                                         DeltaBuffer& delta) {
    for (auto& shipment : active_shipments_) {
        if (shipment.status != ShipmentStatus::in_transit) {
            continue;
        }
        if (shipment.arrival_tick <= current_tick) {
            // Shipment has arrived. Deposit goods into destination market.
            shipment.status = ShipmentStatus::arrived;

            MarketDelta md{};
            md.good_id = shipment.good_id;
            md.region_id = shipment.destination_province_id;
            md.supply_delta = shipment.quantity_remaining;
            delta.market_deltas.push_back(md);
        }
    }
}

// ---------------------------------------------------------------------------
// process_perishable_decay
// ---------------------------------------------------------------------------
void TradeInfrastructureModule::process_perishable_decay(float decay_rate) {
    for (auto& shipment : active_shipments_) {
        if (shipment.status != ShipmentStatus::in_transit) {
            continue;
        }
        apply_perishable_decay(shipment, decay_rate);
    }
}

// ---------------------------------------------------------------------------
// process_interception_checks
// ---------------------------------------------------------------------------
void TradeInfrastructureModule::process_interception_checks(uint32_t current_tick,
                                                             DeltaBuffer& delta,
                                                             DeterministicRNG& rng) {
    for (auto& shipment : active_shipments_) {
        if (shipment.status != ShipmentStatus::in_transit) {
            continue;
        }
        if (!shipment.is_criminal) {
            continue;
        }
        if (check_interception(shipment, rng)) {
            shipment.status = ShipmentStatus::intercepted;

            // Generate an evidence token for the interception.
            EvidenceDelta ed{};
            EvidenceToken token{};
            token.id = shipment.id + 100000u;  // deterministic id derivation
            token.type = EvidenceType::physical;
            token.source_npc_id = 0;  // system-generated
            token.target_npc_id = shipment.owner_id;
            token.actionability = 0.7f;
            token.decay_rate = 0.01f;
            token.created_tick = current_tick;
            token.province_id = shipment.destination_province_id;
            token.is_active = true;
            ed.new_token = token;
            delta.evidence_deltas.push_back(ed);

            // Queue a consequence for the interception.
            ConsequenceDelta cd{};
            cd.new_entry_id = shipment.id;
            delta.consequence_deltas.push_back(cd);
        }
    }
}

// ---------------------------------------------------------------------------
// remove_completed_shipments
// ---------------------------------------------------------------------------
void TradeInfrastructureModule::remove_completed_shipments() {
    active_shipments_.erase(
        std::remove_if(active_shipments_.begin(), active_shipments_.end(),
                        [](const TransitShipment& s) {
                            return s.status != ShipmentStatus::in_transit;
                        }),
        active_shipments_.end());
}

// ---------------------------------------------------------------------------
// add_shipment
// ---------------------------------------------------------------------------
void TradeInfrastructureModule::add_shipment(TransitShipment shipment) {
    active_shipments_.push_back(shipment);
}

// ---------------------------------------------------------------------------
// calculate_transit_ticks (static)
// ---------------------------------------------------------------------------
uint32_t TradeInfrastructureModule::calculate_transit_ticks(const RouteProfile& route,
                                                             TransportMode mode) {
    const float mode_speed = TradeInfrastructureConstants::speed_for_mode(mode);

    // Base transit time in ticks (fractional).
    const float base_transit = route.distance_km / mode_speed;

    // Terrain delay multiplier: 1.0 + terrain_roughness * coeff.
    const float terrain_delay = 1.0f
        + route.route_terrain_roughness * TradeInfrastructureConstants::terrain_delay_coeff;

    // Infrastructure delay multiplier: 1.0 + (1 - min_infrastructure) * coeff.
    const float infra_delay = 1.0f
        + (1.0f - route.min_infrastructure) * TradeInfrastructureConstants::infra_delay_coeff;

    // Final transit ticks: at least 1.
    const float raw = base_transit * terrain_delay * infra_delay;
    const int32_t rounded = static_cast<int32_t>(std::round(raw));
    return static_cast<uint32_t>(std::max(1, rounded));
}

// ---------------------------------------------------------------------------
// apply_perishable_decay (static)
// ---------------------------------------------------------------------------
bool TradeInfrastructureModule::apply_perishable_decay(TransitShipment& shipment,
                                                        float decay_rate) {
    shipment.quantity_remaining *= (1.0f - decay_rate);
    shipment.quality_current *= (1.0f - decay_rate * 0.5f);

    if (shipment.quantity_remaining <= 0.0f) {
        shipment.status = ShipmentStatus::lost;
        shipment.quantity_remaining = 0.0f;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// check_interception (static)
// ---------------------------------------------------------------------------
bool TradeInfrastructureModule::check_interception(const TransitShipment& shipment,
                                                    DeterministicRNG& rng) {
    // Cap concealment modifier at the configured maximum.
    const float capped_concealment = std::min(
        shipment.route_concealment_modifier,
        TradeInfrastructureConstants::max_concealment_modifier);

    // Effective risk per tick after concealment reduction.
    const float effective_risk = shipment.interception_risk_per_tick
        * (1.0f - capped_concealment);

    if (effective_risk <= 0.0f) {
        return false;
    }

    // Roll: intercepted if random draw < effective_risk.
    const float roll = rng.next_float();
    return roll < effective_risk;
}

}  // namespace econlife
