# Module: price_engine

## Purpose
Recalculates spot prices and equilibrium prices for all goods in all regional markets each tick using a 3-step supply-demand equilibrium formula with price stickiness, LOD 1 trade ceilings/floors, and LOD 2 global modifiers. Province-parallel (each province has independent markets).

## Inputs (from WorldState)
- `regional_markets[]` — vector of `RegionalMarket` structs; one per (good_id, province_id) pair. Reads `spot_price`, `equilibrium_price`, `supply`, `demand_buffer`, `import_price_ceiling`, `export_price_floor`, and `adjustment_rate`.
- `goods_data[]` — per-good static data including `base_price`, `quality_premium_coeff`, `price_adjustment_rate` (per-good override; default 1.0), and `perishable_decay_rate`.
- `config.economy` — `price_adjustment_rate` (global default: 0.10), `export_floor_coeff` (default: 0.40), `import_ceiling_coeff` (default: 3.0).
- `lod2_price_index.lod2_price_modifier[]` — per-good annual multiplier from LOD 2 batch; 1.0 for unmodified goods.
- `SUPPLY_FLOOR` — engine constant = 0.01; division-by-zero guard.

## Outputs (to DeltaBuffer)
- `MarketDelta.spot_price_override` — replacement value for `RegionalMarket.spot_price` after the 3-step computation.
- `MarketDelta.equilibrium_price_override` — replacement value for `RegionalMarket.equilibrium_price` computed in Step 1.
- Both are per (good_id, province_id) pair. These are replacement fields (not additive).

## Preconditions
- `supply_chain` module has executed: production outputs and transit arrivals have been applied to `RegionalMarket.supply`.
- `labor_market` module has executed: wage adjustments are reflected in cost structures.
- `seasonal_agriculture` module has executed: seasonal harvest supply bursts are included in `RegionalMarket.supply`.
- `demand_buffer` contains accumulated demand from the previous tick (one-tick lag).
- `RegionalMarket.supply` includes only local production this tick plus `TransitShipment` arrivals with `arrival_tick == current_tick`; goods in transit are excluded.

## Postconditions
- Every `RegionalMarket` in every LOD 0 province has an updated `spot_price` and `equilibrium_price`.
- `spot_price` reflects the 3-step formula: equilibrium computation, sticky adjustment, and LOD 2 modifier application.
- At steady state (demand/supply ratio = 1.0, no LOD 1 offers, LOD 2 modifier = 1.0), `spot_price` converges to `good.base_price`.
- Informal market layer (criminal goods) has been computed using the same equilibrium formula with separate price per layer.
- All price computations processed in canonical order (good_id ascending) within each province for determinism.

## Invariants
- Processing order within each province: goods iterated in ascending `good_id` order. Across provinces: each province processes independently in parallel; merge order is ascending `province_id`.
- `equilibrium_price` is always clamped between the effective floor (`export_price_floor` if set, else `base_price * export_floor_coeff`) and the effective ceiling (`import_price_ceiling` if set, else `base_price * import_ceiling_coeff`).
- `spot_price` never goes negative. Division by supply uses `max(supply, SUPPLY_FLOOR)` where `SUPPLY_FLOOR = 0.01`.
- `base_price` acts as drift anchor: prevents runaway price collapse for zero-demand goods and runaway inflation for zero-supply goods.
- LOD 1 offer prices override config coefficients when non-zero (`import_price_ceiling > 0.0` or `export_price_floor > 0.0`).
- LOD 2 modifier is applied multiplicatively after spot price adjustment; modifier of 1.0 is identity.
- Same seed + same inputs = identical price outputs on any number of cores.
- Quality premium overlay is transactional only (applied at point of sale); it does not alter `spot_price` stored in `RegionalMarket`.

## Failure Modes
- If `supply` is zero or near-zero: `SUPPLY_FLOOR` (0.01) prevents division by zero; equilibrium price rises to ceiling.
- If `demand_buffer` is zero: equilibrium price falls to floor; `spot_price` converges toward floor over multiple ticks at `adjustment_rate`.
- If `lod2_price_modifier` is missing for a good_id: treated as 1.0 (no modification).
- If `import_price_ceiling` and `export_price_floor` are both zero: fallback to config coefficients (`import_ceiling_coeff`, `export_floor_coeff`).
- If `import_price_ceiling < export_price_floor` (inverted clamp from conflicting LOD 1 offers): clamp resolves by standard `clamp()` semantics (min wins); this is a degenerate state that should be logged as a warning.

## Performance Contract
- Tick step 5 in the 27-step pipeline. Classified as "global aggregate first; then province-parallel recompute."
- Target: 6 provinces x ~50 goods = ~300 RegionalMarket updates per tick.
- Budget: < 0.5ms total at 30x fast-forward (tick budget: 33ms total for all 27 steps).
- Province-parallel execution: one province per worker thread; each province processes its ~50 markets independently.
- Market recomputation can be event-driven: triggered only when supply or demand changes (production step, shipment arrival); static markets skip recomputation.

## Dependencies
- runs_after: ["supply_chain", "labor_market", "seasonal_agriculture"]
- runs_before: ["financial_distribution"]

## Test Scenarios
- `test_equilibrium_at_unity_ratio`: Set `supply = 100`, `demand_buffer = 100`, `base_price = 10.0`, no LOD 1 offers, LOD 2 modifier = 1.0. After sufficient ticks, `spot_price` converges to `base_price` (10.0). Assert `|spot_price - 10.0| < 0.01` after 50 ticks.
- `test_demand_spike_raises_price`: Province has `supply = 50`, `demand_buffer = 150` for good with `base_price = 10.0`. Assert `equilibrium_price > base_price` on the first tick. Assert `spot_price` increases each tick toward equilibrium, closing 10% of the gap per tick at default `price_adjustment_rate = 0.10`.
- `test_supply_floor_prevents_division_by_zero`: Set `supply = 0.0`, `demand_buffer = 100.0`, `base_price = 10.0`. Assert no crash or NaN. Assert `equilibrium_price = import_ceiling_coeff * base_price` (clamped at ceiling). Assert `spot_price` moves toward ceiling.
- `test_lod1_import_ceiling_overrides_config`: Set `import_price_ceiling = 15.0` on a market with `base_price = 10.0` and `config.import_ceiling_coeff = 3.0`. Assert `equilibrium_price <= 15.0` (LOD 1 ceiling), not `<= 30.0` (config ceiling).
- `test_lod2_modifier_applied_multiplicatively`: After Step 2 yields `spot_price = 10.0`, set `lod2_price_modifier[good_id] = 1.25`. Assert final `spot_price = 12.5`.
- `test_canonical_order_determinism`: Run price engine twice with identical WorldState on different thread counts (1 thread vs. 6 threads). Assert all `spot_price` and `equilibrium_price` values are bit-identical across both runs.
- `test_seasonal_price_cycle`: Province with grain farm facility. Run 2 full annual cycles. Assert `spot_price` in pre-harvest quarter is at least 20% higher than in harvest quarter for each cycle, reflecting seasonal supply burst dynamics.
