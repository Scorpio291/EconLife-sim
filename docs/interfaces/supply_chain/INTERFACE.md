# Module: supply_chain

## Purpose
Matches buy orders to sell offers across the regional market system, dispatches transit shipments along precomputed trade routes for inter-province fulfillment, and delivers arrived shipments into destination market supply. Province-parallel for local (same-province) matching; cross-province trade uses CrossProvinceDeltaBuffer with one-tick delay.

## Inputs (from WorldState)
- `provinces[].regional_markets[]` — per-good per-province RegionalMarket: spot_price, equilibrium_price, supply, demand_buffer, import_price_ceiling, export_price_floor
- `provinces[].facilities[]` — facility output_good_id and production quantities (written by production module this tick)
- `businesses[]` — NPCBusiness records: province_id, sector, criminal_sector, cash for transport cost deduction
- `province_route_table[origin][dest][mode]` — precomputed RouteProfile: distance_km, route_terrain_roughness, min_infrastructure, hop_count, concealment_bonus, province_path
- `transit_shipments[]` — active TransitShipment records: status, arrival_tick, quantity_remaining, quality_current, destination_province_id
- `deferred_work_queue` — transit_arrival and interception_check items with due_tick <= current_tick
- `tariff_schedules[]` — TariffSchedule per nation: good_tariff_rates, trade_agreements, default_tariff_rate
- `lod1_national_trade_offers[]` — NationalTradeOffer: exports (GoodOffer), imports (GoodOffer) for LOD 1 nations
- `current_tick` — current simulation tick
- `config.transport` — base_transport_rate, terrain_delay_coeff, infra_delay_coeff, mode speeds
- `config.transit` — max_concealment_modifier

## Outputs (to DeltaBuffer)
- `MarketDelta` per (good_id, province_id): additive supply delta from transit arrivals this tick; supply does NOT include goods still in transit
- `MarketDelta` per (good_id, province_id): additive supply delta from local production matched to local buyers (same-province, zero transit time)
- `NPCDelta.capital_delta` (additive): transport cost deducted from shipper business cash at dispatch
- New `TransitShipment` entries created for inter-province fulfillment, queued in DeferredWorkQueue with type transit_arrival and due_tick = current_tick + transit_ticks
- New `TransitShipment` entries for LOD 1 imports accepted this tick, with arrival_tick computed from LOD 1 transit time formula
- `MarketDelta.import_price_ceiling` (replacement): set on LOD 1 trade offer acceptance; applies immediately even while goods are in transit
- `MarketDelta.export_price_floor` (replacement): set when LOD 1 nation publishes an import bid for a province good
- Criminal shipment interception_check items queued per tick of transit duration for criminal TransitShipments
- `CrossProvinceDeltaBuffer` entries for inter-province trade effects (take hold next tick)

## Preconditions
- Production module has completed: facility outputs for this tick are finalized in RegionalMarket.supply.
- province_route_table is populated (computed at world load; rebuilt if province infrastructure changes).
- All TransitShipment records have valid origin_province_id and destination_province_id referencing existing provinces.
- TransportMode speed constants loaded from transport.json.
- RegionalMarket.supply reflects only local production this tick plus transit arrivals this tick (not in-transit goods).

## Postconditions
- All TransitShipments with arrival_tick == current_tick have status set to arrived and their quantity_remaining added to destination RegionalMarket.supply.
- All interception_check items for current_tick processed; intercepted criminal shipments have status set to intercepted (goods destroyed, evidence token generated).
- All perishable goods in transit have quantity_remaining and quality_current decremented by their decay rates for this tick.
- Same-province buy/sell matches resolved with transit_ticks = 0 (instantaneous intra-province distribution).
- Inter-province shipments dispatched with transport_cost deducted from shipper at dispatch, arrival_tick precomputed and immutable.
- LOD 1 import TransitShipments created for accepted trade offers with correct sea-speed transit time.
- No TransitShipment has negative quantity_remaining.

## Invariants
- RegionalMarket.supply never includes goods in transit; only local production + arrived TransitShipments with arrival_tick == current_tick.
- Same-province shipments always have transit_ticks = 0 (intra-province distribution is abstracted).
- Transit time is deterministic: same route + same mode + same infrastructure = same transit_ticks.
- TransitShipment.arrival_tick is immutable after dispatch.
- Transport cost is deducted exactly once at dispatch; no in-transit cost.
- Criminal shipments (is_criminal == true) have interception_risk_per_tick > 0; legitimate shipments have interception_risk_per_tick == 0.
- route_concealment_modifier is capped by config.transit.max_concealment_modifier (0.40).
- Floating-point accumulations use canonical sort order (good_id ascending, province_id ascending).
- LOD 1 transit does not use the full RouteProfile table; distance is computed directly from geographic centroid to destination port.
- Cross-province effects use CrossProvinceDeltaBuffer and take hold at the start of the following tick.

## Failure Modes
- RouteProfile missing for a province pair + mode: skip shipment dispatch, log warning, use fallback road route if available.
- Business cash insufficient for transport_cost: shipment not dispatched; buy order remains unfulfilled this tick.
- Circular supply chain dependency (energy needed to produce energy equipment): handled by single-pass propagation with one-tick lag; documented as [RISK] requiring careful initialization order.
- LOD 1 nation has no valid port for import transit: skip LOD 1 import for that province, log error.
- Perishable goods quantity_remaining reaches 0 during transit: shipment marked as lost, removed from queue.

## Performance Contract
- Tick step 2 budget: supply chain/transit/consequences combined < 100ms at 2,000 NPCs, 6 provinces, ~50 goods.
- Transit arrival processing parallelized by destination_province_id (one province per worker thread).
- Province route table lookup is O(1) per dispatch.
- DeferredWorkQueue pop is O(log n) per item.

## Dependencies
- runs_after: ["production"]
- runs_before: ["price_engine"]

## Test Scenarios
- `test_local_match_zero_transit`: Create a sell offer and buy order for the same good in the same province. Verify transit_ticks == 0 and supply is updated in the same tick without creating a TransitShipment.
- `test_inter_province_shipment_dispatch`: Create a buy order in Province A for a good only produced in Province B. Verify a TransitShipment is created with correct arrival_tick from the transit time formula, and transport_cost is deducted from the shipper business cash.
- `test_transit_arrival_adds_to_supply`: Create a TransitShipment with arrival_tick == current_tick. Verify RegionalMarket.supply for the destination province increases by quantity_remaining, and shipment status transitions to arrived.
- `test_in_transit_excluded_from_supply`: Create a TransitShipment with arrival_tick > current_tick. Verify its quantity is NOT reflected in RegionalMarket.supply for the destination province.
- `test_perishable_decay_during_transit`: Dispatch a perishable good (perishable_decay_rate > 0) on a 5-tick route. Verify quantity_remaining < quantity_dispatched on arrival and quality_current < quality_at_departure.
- `test_criminal_shipment_interception`: Dispatch a criminal TransitShipment (is_criminal == true) with interception_risk_per_tick = 1.0. Verify interception_check items are queued for each tick of transit, and the shipment is intercepted with status set to intercepted and an evidence token generated.
- `test_concealment_caps_at_max`: Dispatch a concealed criminal shipment along a route with RouteProfile.concealment_bonus exceeding config.transit.max_concealment_modifier. Verify route_concealment_modifier on the TransitShipment is clamped to max_concealment_modifier (0.40).
- `test_lod1_import_creates_transit_shipment`: Accept a LOD 1 trade offer for Province A. Verify a TransitShipment is created with origin as LOD 1 sentinel, arrival_tick computed from sea speed and centroid distance, and import_price_ceiling set immediately on acceptance.
- `test_insufficient_cash_blocks_dispatch`: Set shipper business cash to 0. Attempt to dispatch an inter-province shipment. Verify no TransitShipment is created and the buy order remains unfulfilled.
- `test_cross_province_delta_one_tick_delay`: Dispatch an inter-province trade effect. Verify it is written to CrossProvinceDeltaBuffer and does not affect the destination province until the following tick.
- `test_supply_chain_propagation_wave`: Set up a 3-stage chain (raw material -> intermediate -> finished good) across 2 provinces. Remove the raw material supply. Verify that the intermediate facility idles next tick and the finished good facility idles the tick after, producing a multi-tick propagation wave.
- `test_deterministic_across_core_counts`: Run 50 ticks of supply chain processing with identical seeds on 1 core and 6 cores. Verify bit-identical RegionalMarket.supply values for all (good_id, province_id) pairs.
