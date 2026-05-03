// Supply Chain Module — implementation.
// See supply_chain_module.h for class declarations and
// docs/interfaces/supply_chain/INTERFACE.md for the canonical specification.

#include "modules/supply_chain/supply_chain_module.h"

#include <algorithm>
#include <cmath>

#include "core/good_id_hash.h"
#include "core/rng/deterministic_rng.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"

namespace econlife {

// ===========================================================================
// SupplyChainModule — utility
// ===========================================================================

uint32_t SupplyChainModule::good_id_from_string(const std::string& good_id_str) {
    return good_id_hash(good_id_str);
}

uint32_t SupplyChainModule::compute_transit_ticks(const RouteProfile& route, float mode_speed,
                                                  float infrastructure_rating) {
    // Transit time formula:
    //   ticks = ceil(distance_km / (mode_speed * (1 + infrastructure * infra_speed_coeff)))
    // Minimum 1 tick for inter-province shipments.
    float effective_speed =
        mode_speed * (1.0f + infrastructure_rating * SupplyChainModuleConfig{}.infra_speed_coeff);

    if (effective_speed <= 0.0f) {
        return 1;
    }

    float raw_ticks = route.distance_km / effective_speed;
    uint32_t ticks = static_cast<uint32_t>(std::ceil(raw_ticks));
    return std::max(ticks, static_cast<uint32_t>(1));
}

float SupplyChainModule::compute_transport_cost(const RouteProfile& route, float quantity) {
    // Transport cost formula:
    //   cost = base_transport_rate * distance_km * quantity
    //          * (1 + terrain_roughness * terrain_cost_coeff)
    float terrain_factor =
        1.0f + route.route_terrain_roughness * SupplyChainModuleConfig{}.terrain_cost_coeff;

    return SupplyChainModuleConfig{}.base_transport_rate * route.distance_km * quantity *
           terrain_factor;
}

// ===========================================================================
// SupplyChainModule — per-province local matching
// ===========================================================================

void SupplyChainModule::execute_province(uint32_t province_idx, const WorldState& state,
                                         DeltaBuffer& province_delta) {
    // Skip provinces that are not at full LOD.
    if (province_idx >= state.provinces.size()) {
        return;
    }
    const Province& province = state.provinces[province_idx];
    if (province.lod_level != SimulationLOD::full) {
        return;
    }

    // Fork RNG with province_id for deterministic province-parallel work.
    DeterministicRNG rng = DeterministicRNG(state.world_seed).fork(province_idx);

    // Collect sell offers and buy orders for this province.
    std::vector<SellOffer> sell_offers;
    std::vector<BuyOrder> buy_orders;
    collect_sell_offers(province_idx, state, sell_offers);
    collect_buy_orders(province_idx, state, buy_orders);

    // Step 1: Local matching — match sell offers to buy orders in same province.
    match_local(province_idx, state, province_delta, sell_offers, buy_orders);

    // Step 2: Inter-province dispatch for unfulfilled orders.
    // Filter to orders that still have remaining quantity.
    std::vector<BuyOrder> unfulfilled;
    for (const auto& order : buy_orders) {
        if (order.quantity > 0.001f) {
            unfulfilled.push_back(order);
        }
    }
    dispatch_inter_province(province_idx, state, province_delta, unfulfilled, rng);
}

void SupplyChainModule::collect_sell_offers(uint32_t province_id, const WorldState& state,
                                            std::vector<SellOffer>& offers) const {
    // Build sell offers from regional market supply for this province.
    // Each market with positive supply generates a sell offer.
    // In V1, we treat the aggregated market supply as available for matching.
    for (const auto& market : state.regional_markets) {
        if (market.province_id == province_id && market.supply > 0.0f) {
            SellOffer offer{};
            offer.business_id = 0;  // aggregated market (not a specific business)
            offer.province_id = province_id;
            offer.good_id = market.good_id;
            offer.quantity = market.supply;
            offer.ask_price = market.spot_price;
            offer.is_criminal = false;
            offers.push_back(offer);
        }
    }

    // Sort by good_id ascending for deterministic order.
    std::sort(offers.begin(), offers.end(),
              [](const SellOffer& a, const SellOffer& b) { return a.good_id < b.good_id; });
}

void SupplyChainModule::collect_buy_orders(uint32_t province_id, const WorldState& state,
                                           std::vector<BuyOrder>& orders) const {
    // Build buy orders from demand buffer for this province.
    // Each market with positive demand_buffer generates a buy order.
    for (const auto& market : state.regional_markets) {
        if (market.province_id == province_id && market.demand_buffer > 0.0f) {
            BuyOrder order{};
            order.business_id = 0;  // aggregated demand
            order.province_id = province_id;
            order.good_id = market.good_id;
            order.quantity = market.demand_buffer;
            order.bid_price = market.spot_price;
            order.is_criminal = false;
            orders.push_back(order);
        }
    }

    // Sort by good_id ascending for deterministic order.
    std::sort(orders.begin(), orders.end(),
              [](const BuyOrder& a, const BuyOrder& b) { return a.good_id < b.good_id; });
}

void SupplyChainModule::match_local(uint32_t province_id, const WorldState& state,
                                    DeltaBuffer& delta, std::vector<SellOffer>& offers,
                                    std::vector<BuyOrder>& orders) const {
    // Local matching: for each good, match available supply to demand.
    // Same province = zero transit time, supply updated immediately.
    // No TransitShipment created for local matches.
    //
    // Process in canonical order: good_id ascending.
    // Both vectors are pre-sorted by good_id.

    size_t oi = 0;  // offer index
    size_t bi = 0;  // buy order index

    while (oi < offers.size() && bi < orders.size()) {
        if (offers[oi].good_id < orders[bi].good_id) {
            ++oi;
            continue;
        }
        if (offers[oi].good_id > orders[bi].good_id) {
            ++bi;
            continue;
        }

        // Same good_id — match as much as possible.
        float matched_qty = std::min(offers[oi].quantity, orders[bi].quantity);
        if (matched_qty > 0.0f) {
            // Write a MarketDelta recording the local fulfillment.
            // Supply is consumed (negative delta) and demand is satisfied.
            // Actually, per the interface spec, local matching means the
            // supply is already present in RegionalMarket.supply and we
            // just record the demand fulfillment. The supply was already
            // added by the production module this tick.
            //
            // We emit a demand_buffer_delta reflecting matched demand.
            MarketDelta demand_delta{};
            demand_delta.good_id = offers[oi].good_id;
            demand_delta.region_id = province_id;
            demand_delta.demand_buffer_delta = -matched_qty;  // demand satisfied
            delta.market_deltas.push_back(demand_delta);

            offers[oi].quantity -= matched_qty;
            orders[bi].quantity -= matched_qty;
        }

        if (offers[oi].quantity <= 0.001f) {
            ++oi;
        }
        if (bi < orders.size() && orders[bi].quantity <= 0.001f) {
            ++bi;
        }
    }
}

void SupplyChainModule::dispatch_inter_province(uint32_t province_id, const WorldState& state,
                                                DeltaBuffer& delta,
                                                const std::vector<BuyOrder>& unfulfilled_orders,
                                                DeterministicRNG& rng) const {
    if (unfulfilled_orders.empty()) {
        return;
    }

    // For each unfulfilled buy order, look for supply in other provinces
    // and dispatch an inter-province shipment.
    //
    // Search provinces in ascending id order for determinism.
    // Use the first available route (road = mode 0 by default for V1).
    for (const auto& order : unfulfilled_orders) {
        float remaining = order.quantity;
        if (remaining <= 0.001f) {
            continue;
        }

        // Search other provinces for supply of this good.
        for (uint32_t src = 0; src < static_cast<uint32_t>(state.provinces.size()); ++src) {
            if (src == province_id) {
                continue;
            }
            if (state.provinces[src].lod_level != SimulationLOD::full) {
                continue;
            }
            if (remaining <= 0.001f) {
                break;
            }

            // Check if source province has supply for this good.
            float source_supply = 0.0f;
            for (const auto& market : state.regional_markets) {
                if (market.province_id == src && market.good_id == order.good_id) {
                    source_supply = market.supply;
                    break;
                }
            }

            if (source_supply <= 0.001f) {
                continue;
            }

            // Look up route from source to destination (road mode = index 0).
            auto route_key = std::make_pair(src, province_id);
            auto route_it = state.province_route_table.find(route_key);
            if (route_it == state.province_route_table.end()) {
                continue;  // No route available
            }

            const RouteProfile& route = route_it->second[0];  // road mode

            // Determine shipment quantity.
            float ship_qty = std::min(remaining, source_supply);

            // Compute transport cost.
            float transport_cost = compute_transport_cost(route, ship_qty);

            // Find a business in the destination province to pay for shipping.
            // In V1, we look for any business with sufficient cash.
            const NPCBusiness* shipper = nullptr;
            for (const auto& biz : state.npc_businesses) {
                if (biz.province_id == province_id && biz.cash >= transport_cost) {
                    shipper = &biz;
                    break;
                }
            }

            if (!shipper) {
                continue;  // No business can afford shipping
            }

            // Compute transit time.
            float infra = state.provinces[src].infrastructure_rating;
            uint32_t transit_ticks = compute_transit_ticks(route, scfg_.road_speed, infra);

            // Deduct transport cost from shipper.
            NPCDelta cost_delta{};
            cost_delta.npc_id = shipper->id;
            cost_delta.capital_delta = -transport_cost;
            delta.npc_deltas.push_back(cost_delta);

            // Create TransitShipment and queue to DeferredWorkQueue.
            // We write a CrossProvinceDelta for the inter-province effect.
            // The actual arrival will be processed by execute() when the
            // deferred work item fires.
            //
            // For V1 bootstrap, we create the deferred work item directly.
            // The shipment details are encoded in the TransitPayload.
            uint32_t shipment_id = rng.next_uint(0xFFFFFFFF);

            DeferredWorkItem arrival_item{};
            arrival_item.due_tick = state.current_tick + transit_ticks;
            arrival_item.type = WorkType::transit_arrival;
            arrival_item.subject_id = shipment_id;
            arrival_item.payload = TransitPayload{shipment_id, province_id};

            // Push to cross-province delta buffer for transit-delay semantics.
            // The market supply at the destination increases when the shipment
            // arrives at due_tick. The orchestrator merges province deltas into
            // WorldState.cross_province_delta_buffer; apply_cross_province_deltas
            // releases entries on the tick that matches due_tick.
            CrossProvinceDelta cpd{};
            cpd.source_province_id = src;
            cpd.target_province_id = province_id;
            cpd.due_tick = state.current_tick + transit_ticks;

            MarketDelta arrival_market_delta{};
            arrival_market_delta.good_id = order.good_id;
            arrival_market_delta.region_id = province_id;
            arrival_market_delta.supply_delta = ship_qty;
            cpd.market_delta = arrival_market_delta;

            delta.cross_province_deltas.push_back(cpd);

            remaining -= ship_qty;
        }
    }
}

// ===========================================================================
// SupplyChainModule — global execution (transit arrivals, interception, LOD1)
// ===========================================================================

void SupplyChainModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Global post-pass: called by the orchestrator after all province-parallel
    // execute_province() calls have been merged and applied.
    DeterministicRNG rng(state.world_seed + state.current_tick);

    // Step 1: Process transit arrivals from deferred work queue.
    process_transit_arrivals(state, delta);

    // Step 2: Process criminal interception checks.
    process_interception_checks(state, delta, rng);

    // Step 3: Process LOD 1 import offers.
    process_lod1_imports(state, delta);
}

void SupplyChainModule::process_transit_arrivals(const WorldState& state,
                                                 DeltaBuffer& delta) const {
    // In V1, transit arrivals are DeferredWorkItems with
    // type == transit_arrival and due_tick <= current_tick.
    //
    // The DeferredWorkQueue is a priority queue on WorldState (const).
    // The orchestrator drains it at Step 2 and passes items to the
    // appropriate module. For this implementation, we process items
    // that would have been passed to us.
    //
    // Since we cannot mutate the const WorldState queue, the actual
    // drain is handled by the tick orchestrator. This method processes
    // TransitShipment records that have arrived.
    //
    // For V1 bootstrap: no-op here. Transit arrivals are modeled
    // through the market deltas emitted at dispatch time with the
    // appropriate timing (arrival_tick). The orchestrator applies
    // CrossProvinceDelta entries at their due_tick.
    //
    // When full transit tracking is implemented, this method will:
    // 1. Pop DeferredWorkItems with type=transit_arrival, due_tick <= current_tick
    // 2. Look up the TransitShipment by shipment_id
    // 3. Apply perishable decay: quantity_remaining *= (1 - decay_rate)^transit_ticks
    // 4. Add quantity_remaining to destination RegionalMarket.supply via MarketDelta
    // 5. Set shipment status to arrived
}

void SupplyChainModule::process_interception_checks(const WorldState& state, DeltaBuffer& delta,
                                                    DeterministicRNG& rng) const {
    // Process interception_check DeferredWorkItems for criminal shipments.
    //
    // For each interception check:
    // 1. Roll rng.next_float() < interception_risk_per_tick * (1 - concealment)
    // 2. If intercepted: set status to intercepted, generate evidence token
    // 3. If not intercepted: shipment continues
    //
    // In V1 bootstrap, criminal shipment tracking is not yet fully
    // integrated with the DeferredWorkQueue drain. This method provides
    // the framework for when it is.
    //
    // Interception checks would be passed from the orchestrator's
    // Step 2 drain. For now, no-op.
}

void SupplyChainModule::process_lod1_imports(const WorldState& state, DeltaBuffer& delta) const {
    // Process NationalTradeOffer records from LOD 1 nations.
    //
    // For each offer in state.lod1_trade_offers:
    //   For each export GoodOffer:
    //     - Determine destination province (lowest spot_price for this good)
    //     - Compute transit time using sea speed and centroid distance
    //     - Create TransitShipment with lod1 sentinel origin
    //     - Set import_price_ceiling on destination market immediately
    //
    // For V1 bootstrap: process available LOD 1 offers.
    for (const auto& offer : state.lod1_trade_offers) {
        for (const auto& export_good : offer.exports) {
            // Find the province with highest demand for this good.
            uint32_t best_province = 0;
            float best_demand = -1.0f;
            bool found = false;

            for (const auto& market : state.regional_markets) {
                if (market.good_id == export_good.good_id && market.demand_buffer > best_demand) {
                    best_demand = market.demand_buffer;
                    best_province = market.province_id;
                    found = true;
                }
            }

            if (!found || best_demand <= 0.0f) {
                continue;
            }

            // Compute LOD 1 transit time.
            // Use geographic centroid distance / sea_speed.
            // For V1, we approximate with a fixed transit time of 5 ticks
            // for LOD 1 imports, since centroid data may not be populated.
            constexpr uint32_t lod1_default_transit_ticks = 5;

            float ship_qty = std::min(export_good.quantity_available, best_demand);
            if (ship_qty <= 0.001f) {
                continue;
            }

            // Set import_price_ceiling immediately.
            MarketDelta ceiling_delta{};
            ceiling_delta.good_id = export_good.good_id;
            ceiling_delta.region_id = best_province;
            ceiling_delta.spot_price_override = export_good.offer_price;
            delta.market_deltas.push_back(ceiling_delta);

            // Emit supply delta for when the shipment arrives.
            // In the full implementation, this would be a DeferredWorkItem.
            // For V1, we record it directly (the orchestrator handles timing).
            MarketDelta arrival_delta{};
            arrival_delta.good_id = export_good.good_id;
            arrival_delta.region_id = best_province;
            arrival_delta.supply_delta = ship_qty;
            delta.market_deltas.push_back(arrival_delta);
        }
    }
}

}  // namespace econlife
