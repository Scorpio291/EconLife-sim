# Module: trade_infrastructure

## Purpose
Manages physical transport routes between provinces, processes transit shipment arrivals and perishable degradation, computes transport costs, handles route capacity and infrastructure effects on transit time, and runs per-tick interception checks for criminal shipments. NOT province-parallel (handles cross-province movement via the global DeferredWorkQueue).

## Inputs (from WorldState)
- `deferred_work_queue` — min-heap of `DeferredWorkItem`; this module processes items where `due_tick <= current_tick` and `type` is `transit_arrival` or `interception_check`.
- `transit_shipments[]` — active `TransitShipment` records with fields: `id`, `good_id`, `quantity_dispatched`, `quantity_remaining`, `quality_at_departure`, `quality_current`, `origin_province_id`, `destination_province_id`, `owner_id`, `dispatch_tick`, `arrival_tick`, `mode`, `cost_paid`, `is_criminal`, `interception_risk_per_tick`, `is_concealed`, `route_concealment_modifier`, `status`.
- `province_route_table` — precomputed at world load via Dijkstra; 6x6x5 = 180 `RouteProfile` entries. Each profile contains: `distance_km`, `route_terrain_roughness`, `min_infrastructure`, `hop_count`, `requires_sea_leg`, `requires_rail`, `concealment_bonus`, `province_path`.
- `config.transport` — `terrain_delay_coeff` (0.4), `infra_delay_coeff` (0.6), `base_transport_rate`, `transit.max_concealment_modifier` (0.40), `routes.karst_concealment_bonus` (0.08).
- `config.transport.mode_speeds` — road: 800 km/tick, rail: 700, sea: 900, river: 450, air: 10,000.
- `config.transport.route_mode_modifier` — road: 1.0, rail: 0.45, sea: 0.30, river: 0.60, air: 50.0.
- `goods_data[]` — per-good: `physical_size_factor` (0.0 to 2.0), `perishable_decay_rate` (0.0 for non-perishables).
- `provinces[]` — `infrastructure_rating`, `port_capacity`, `rail_connectivity`, `river_access`, `has_karst` per province.
- `corruption_coverage_along_route` — derived from NPC corruption relationships along the route path.
- `law_enforcement_heat_along_route` — derived from `InvestigatorMeter.current_level` across route provinces.
- `world_seed`, `current_tick` — for deterministic interception RNG: `DeterministicRNG(world_seed, current_tick, shipment_id)`.

## Outputs (to DeltaBuffer)
- `MarketDelta.supply_delta` — additive; adds `quantity_remaining` to destination `RegionalMarket.supply` when a shipment arrives.
- `ShipmentStatusDelta` — sets `TransitShipment.status` to `arrived`, `intercepted`, or `lost`.
- `EvidenceDelta.new_token` — on interception: `EvidenceToken { type: physical_seizure }` pushed to origin and intercepting province evidence pools.
- `ConsequenceDelta.new_entry` — on interception: `{ type: raid_executed }` queued for destination province; `{ type: investigation_opens }` queued for intercepting LE NPC.
- Perishable degradation: updates `quantity_remaining` and `quality_current` on in-transit shipments each tick.
- Cross-province effects written to `CrossProvinceDeltaBuffer` (applied at start of following tick with one-tick delay).

## Preconditions
- `supply_chain` module has executed: new shipments have been dispatched and registered in the deferred work queue.
- `province_route_table` is fully built at world load (or rebuilt after infrastructure change).
- All `TransitShipment` records have `arrival_tick` precomputed and immutable from dispatch.
- Criminal shipments have `interception_check` work items queued at dispatch time (one per in-transit tick).
- `DeterministicRNG` is available with `world_seed` set.

## Postconditions
- All `DeferredWorkItem` entries with `due_tick <= current_tick` and type `transit_arrival` have been processed: goods deposited into destination `RegionalMarket.supply`, shipment status set to `arrived`.
- All `interception_check` items for this tick have been evaluated: criminal shipments either continue in transit or are intercepted with evidence and consequence entries generated.
- Perishable goods in transit have had `quantity_remaining` and `quality_current` degraded for this tick.
- Transit arrivals are partitioned by `destination_province_id` for parallel processing by province workers.
- No shipment with `arrival_tick < current_tick` remains in `in_transit` status.

## Invariants
- `arrival_tick` on a `TransitShipment` is immutable after dispatch; the module does not modify it.
- Same-province shipments have `transit_ticks = 0`; intra-province distribution is abstracted.
- `route_concealment_modifier` is capped at `config.transit.max_concealment_modifier` (0.40) regardless of route profile.
- Interception checks are spread across the transit duration (one per tick), not concentrated at arrival.
- Legitimate shipments (`is_criminal = false`) have `interception_risk_per_tick = 0.0`; no interception checks are queued for them.
- LOD 1 import shipments (`origin_province_id = LOD1_SENTINEL_PROVINCE_ID`) bypass the interception model entirely.
- Transit time formula: `max(1, round(base_transit_ticks * terrain_delay * infra_delay))` where terrain_delay = `1.0 + (roughness * terrain_delay_coeff)` and infra_delay = `1.0 + ((1.0 - min_infrastructure) * infra_delay_coeff)`.
- Goods in transit are NOT counted in `RegionalMarket.supply` until `arrival_tick == current_tick`.
- Route table lookup is O(1); route table is rebuilt at most once per tick when a province's infrastructure changes.
- Same seed + same inputs = identical transit and interception outcomes (deterministic RNG keyed on world_seed, current_tick, shipment_id).

## Failure Modes
- If `province_route_table` is missing an entry for a (origin, destination, mode) triple: shipment dispatch fails; logged as error; shipment not created.
- If a required transport mode is unavailable (e.g., `rail_connectivity = 0` at one end): dispatch must fall back to an available mode or reject the shipment.
- If `quantity_remaining` degrades to zero during transit (extreme perishable on long route): shipment arrives with zero quantity; `supply_delta = 0.0`; no market effect.
- If `quality_current` degrades below a minimum threshold: goods arrive but may be unsellable at standard price (quality premium overlay applies negative modifier).
- If interception check fires on an already-intercepted shipment (race condition): status is already `intercepted`; remaining checks are no-ops.
- Weather events or accidents can set status to `lost`: goods destroyed, no supply effect, owner absorbs cost_paid as loss.

## Performance Contract
- Tick step 2 in the 27-step pipeline (supply chain/transit/consequences).
- Transit arrivals per tick: typically 0-20 (most shipments are multi-tick; only a fraction arrive each tick).
- Interception checks per tick: up to ~50 for criminal shipments currently in transit.
- Budget: < 1ms total at 30x fast-forward for all transit processing.
- Route table: 180 entries (6x6x5); trivial memory; O(1) lookup.
- Perishable degradation: linear scan of in-transit shipments; bounded by total active shipments (typically < 100).

## Dependencies
- runs_after: ["supply_chain"]
- runs_before: ["financial_distribution"]

## Test Scenarios
- `test_shipment_arrival_adds_to_supply`: Dispatch 100 units of good_id=1 from province A to province B with `arrival_tick = current_tick + 3`. Advance 3 ticks. Assert `RegionalMarket.supply` for (good_id=1, province_B) increases by 100 on tick 3. Assert supply is unchanged on ticks 1 and 2.
- `test_perishable_degradation_during_transit`: Dispatch a perishable good (`perishable_decay_rate = 0.05`) with `quantity_dispatched = 100`, transit time = 5 ticks. Assert `quantity_remaining < 100` on arrival. Assert `quality_current < quality_at_departure`. Verify degradation is applied each tick proportionally.
- `test_criminal_shipment_interception`: Dispatch a criminal shipment (`is_criminal = true`, `interception_risk_per_tick = 0.30`) over a 5-tick route. Use a seed that triggers interception on tick 2. Assert `status = intercepted` after tick 2. Assert `EvidenceToken { type: physical_seizure }` is generated for both origin and intercepting provinces. Assert remaining `interception_check` items for ticks 3-5 are cancelled or become no-ops.
- `test_concealed_route_reduces_interception`: Dispatch two identical criminal shipments: one direct (`route_concealment_modifier = 0.0`), one concealed (`route_concealment_modifier = 0.35`). Concealed shipment has longer transit time but lower `interception_risk_per_tick`. Over 1000 seeded runs, assert concealed shipment interception rate is measurably lower than direct route.
- `test_terrain_and_infrastructure_affect_transit_time`: Route with `route_terrain_roughness = 0.8` and `min_infrastructure = 0.3`. Compute transit time. Assert it is longer than the same route with `roughness = 0.0` and `min_infrastructure = 1.0`. Verify formula: `terrain_delay = 1.32`, `infra_delay = 1.42`, total multiplier approximately 1.87x base.
- `test_same_province_zero_transit`: Dispatch a shipment where `origin_province_id == destination_province_id`. Assert `transit_ticks = 0` and goods appear in supply immediately.
- `test_lod1_import_bypasses_interception`: Create a LOD 1 import `TransitShipment` with `origin_province_id = LOD1_SENTINEL_PROVINCE_ID` and `interception_risk_per_tick = 0.0`. Assert no `interception_check` items are queued. Assert shipment arrives at computed `arrival_tick` with full quantity.
