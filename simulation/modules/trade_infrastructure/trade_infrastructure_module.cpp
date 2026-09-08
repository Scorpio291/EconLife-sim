// Trade Infrastructure Module — implementation.
//
// Processes transit shipments each tick:
//   1. Transit arrivals: deposit goods into destination RegionalMarket
//   2. Perishable degradation: decay quantity and quality for in-transit goods
//   3. Criminal interception: risk-based checks for criminal shipments
//   4. Cleanup: remove completed/lost/intercepted shipments
//
// Sequential (not province-parallel) because shipments cross province boundaries.

#include "modules/trade_infrastructure/trade_infrastructure_module.h"

#include <algorithm>
#include <cmath>

#include "core/rng/deterministic_rng.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"
#include "modules/persistence/module_state_io.h"

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
    process_perishable_decay(cfg_.perishable_decay_base);

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
            cd.new_consequence = make_consequence(
                shipment.id, ConsequenceCategory::social_consequence, 0, 0, 0, current_tick);
            delta.consequence_deltas.push_back(cd);
        }
    }
}

// ---------------------------------------------------------------------------
// remove_completed_shipments
// ---------------------------------------------------------------------------
void TradeInfrastructureModule::remove_completed_shipments() {
    active_shipments_.erase(std::remove_if(active_shipments_.begin(), active_shipments_.end(),
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
                                                            TransportMode mode) const {
    const float mode_speed = speed_for_mode(mode);

    // Base transit time in ticks (fractional).
    const float base_transit = route.distance_km / mode_speed;

    // Terrain delay multiplier: 1.0 + terrain_roughness * coeff.
    const float terrain_delay = 1.0f + route.route_terrain_roughness * cfg_.terrain_delay_coeff;

    // Infrastructure delay multiplier: 1.0 + (1 - min_infrastructure) * coeff.
    const float infra_delay = 1.0f + (1.0f - route.min_infrastructure) * cfg_.infra_delay_coeff;

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
                                                   DeterministicRNG& rng) const {
    // Cap concealment modifier at the configured maximum.
    const float capped_concealment =
        std::min(shipment.route_concealment_modifier, cfg_.max_concealment_modifier);

    // Effective risk per tick after concealment reduction.
    const float effective_risk = shipment.interception_risk_per_tick * (1.0f - capped_concealment);

    if (effective_risk <= 0.0f) {
        return false;
    }

    // Roll: intercepted if random draw < effective_risk.
    const float roll = rng.next_float();
    return roll < effective_risk;
}


// ---------------------------------------------------------------------------
// Module-private state — shipments in transit
// ---------------------------------------------------------------------------
// Format: u32 schema_tag (1), u32 count, then each shipment's fields in
// declaration order.

void TradeInfrastructureModule::serialize_state(std::vector<uint8_t>& out) const {
    using namespace state_io;
    put_u32(out, 1u);
    put_u32(out, static_cast<uint32_t>(active_shipments_.size()));
    for (const auto& s : active_shipments_) {
        put_u32(out, s.id);
        put_u32(out, s.good_id);
        put_f32(out, s.quantity_dispatched);
        put_f32(out, s.quantity_remaining);
        put_f32(out, s.quality_at_departure);
        put_f32(out, s.quality_current);
        put_u32(out, s.origin_province_id);
        put_u32(out, s.destination_province_id);
        put_u32(out, s.owner_id);
        put_u32(out, s.dispatch_tick);
        put_u32(out, s.arrival_tick);
        put_u8(out, static_cast<uint8_t>(s.mode));
        put_f32(out, s.cost_paid);
        put_u8(out, s.is_criminal ? 1u : 0u);
        put_f32(out, s.interception_risk_per_tick);
        put_u8(out, s.is_concealed ? 1u : 0u);
        put_f32(out, s.route_concealment_modifier);
        put_u8(out, static_cast<uint8_t>(s.status));
    }
}

bool TradeInfrastructureModule::deserialize_state(const uint8_t* data, size_t size) {
    using namespace state_io;
    active_shipments_.clear();
    if (data == nullptr || size == 0)
        return true;

    Reader r(data, size);
    if (r.u32() != 1u)
        return false;
    const uint32_t count = r.u32();
    if (r.error)
        return false;
    active_shipments_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        TransitShipment s{};
        s.id = r.u32();
        s.good_id = r.u32();
        s.quantity_dispatched = r.f32();
        s.quantity_remaining = r.f32();
        s.quality_at_departure = r.f32();
        s.quality_current = r.f32();
        s.origin_province_id = r.u32();
        s.destination_province_id = r.u32();
        s.owner_id = r.u32();
        s.dispatch_tick = r.u32();
        s.arrival_tick = r.u32();
        s.mode = static_cast<TransportMode>(r.u8());
        s.cost_paid = r.f32();
        s.is_criminal = r.u8() != 0u;
        s.interception_risk_per_tick = r.f32();
        s.is_concealed = r.u8() != 0u;
        s.route_concealment_modifier = r.f32();
        s.status = static_cast<ShipmentStatus>(r.u8());
        if (r.error)
            return false;
        active_shipments_.push_back(s);
    }
    return true;
}

}  // namespace econlife
