# Module: real_estate

## Purpose
Manages per-province property markets each tick: collects rental income for occupied properties, recomputes monthly market values from provincial conditions, converges asking prices toward market value, processes property sale/purchase transactions, assigns commercial tenants to unoccupied properties, and updates province-level avg_property_value. Province-parallel.

## Inputs (from WorldState)
- `property_listings[]` — id, type (residential/commercial/industrial), province_id, owner_id, asking_price, market_value, rental_yield_rate, rental_income_per_tick, rented, tenant_id, launder_eligible, purchased_tick, purchase_price
- `provinces[]` — demographics.total_population, demographics.income_high_fraction, infrastructure_rating, conditions.criminal_dominance_index, conditions.inequality_index, provincial_business_tax_rate, avg_property_value
- `npc_businesses[]` — province_id, owner_id, cost_per_tick (for commercial tenant assignment: businesses without owned premises benefit from cost reduction)
- `player_character` — wealth (for rental income deposits and transaction settlements)
- `npcs[]` — capital, current_province_id (for residential tenant matching)
- `laundering_operations[]` — active real_estate method operations in province (for laundering pressure on market_value)
- `current_tick` — for monthly recompute cadence and transaction timestamping
- `config.realestate` — base_value_by_type (residential: 100,000; commercial: 250,000; industrial: 400,000), population_value_divisor (10,000), high_income_premium (0.40), infrastructure_premium (0.30), criminal_dominance_discount (0.50), inequality_discount (0.15), launder_pressure_divisor (100,000), price_convergence_rate (0.05), residential_yield_rate (0.0003), commercial_yield_rate (0.0005), commercial_cost_reduction_rate (0.10)

## Outputs (to DeltaBuffer)
- `PlayerDelta.wealth_delta` — additive; rental_income_per_tick for each rented property owned by player; sale proceeds on player sell; debited on player buy
- `NPCDelta.capital_delta` — additive; rental income for NPC-owned rented properties
- `EvidenceDelta.new_token` — financial EvidenceToken at VisibilityScope::institutional on every property buy/sell transaction (property registry record)
- Property listing state updates via delta: market_value recomputed monthly, asking_price converged monthly, owner_id/purchase_price/purchased_tick updated on transactions, rented/tenant_id updated on tenant assignment/departure
- Province state updates via delta: avg_property_value recomputed monthly as mean of all PropertyListing.market_value in province
- Business cost_per_tick reduction when NPCBusiness occupies owned commercial premises (reduction rate: commercial_cost_reduction_rate = 0.10)

## Preconditions
- price_engine has completed for this tick (economic conditions used in market_value formula are current).
- All PropertyListing records have valid province_id references.
- If rented == false, then tenant_id == 0 (structural invariant maintained by prior ticks).
- rental_income_per_tick == market_value * rental_yield_rate (derived field consistency).

## Postconditions
- Every rented property has generated rental_income_per_tick credited to its owner (player or NPC).
- On monthly ticks: market_value recomputed for every property using compute_market_value formula; asking_price moved toward market_value by price_convergence_rate fraction of the gap.
- On monthly ticks: province.avg_property_value updated to the mean market_value of all properties in province.
- Commercial properties without tenants have been matched to businesses in the same province that lack owned premises and would benefit from cost_per_tick reduction.
- All queued buy/sell transactions have been settled: owner_id transferred, wealth debited/credited, purchase_price recorded, financial EvidenceToken created.
- Laundering pressure from active real_estate LaunderingOperations has been reflected in market_value via the launder_pressure term.

## Invariants
- asking_price >= 0.0; market_value >= 0.0; rental_yield_rate >= 0.0.
- rental_income_per_tick is always derived: market_value * rental_yield_rate (never set independently).
- If rented == false, then tenant_id == 0.
- prosperity_multiplier in market_value formula is clamped to [0.1, 5.0] (prevents negative or extreme values).
- Monthly asking price convergence: asking_price += (market_value - asking_price) * price_convergence_rate. Prices never jump discontinuously.
- Property transactions generate exactly one financial EvidenceToken at institutional visibility per transaction.
- Criminal dominance suppresses property values (criminal_dominance_discount = 0.50); laundering pressure inflates them. Both effects can coexist in the same province.

## Failure Modes
- PropertyListing references invalid province_id: log warning, skip that property, continue.
- Owner_id references non-existent actor: log warning, skip rental income for that property.
- Tenant_id references non-existent NPC/business: clear tenant assignment (set rented=false, tenant_id=0), log warning.
- Division by zero in market_value formula (population_value_divisor): use config default (10,000); config loader rejects 0.

## Performance Contract
- Province-parallel execution: < 2ms per province at ~50-100 properties per province.
- Monthly recompute tick (market_value + asking_price + avg_property_value): < 5ms per province.
- Total module budget: < 15ms at full V1 scale (6 provinces, ~500 total properties).

## Dependencies
- runs_after: ["price_engine"]
- runs_before: ["npc_behavior"]

## Test Scenarios
- `test_rental_income_credited_each_tick`: Player owns residential property with market_value=200,000 and residential_yield_rate=0.0003. Property is rented. Verify player.wealth_delta = 200,000 * 0.0003 = 60.0 per tick.
- `test_market_value_reflects_criminal_dominance`: Province with criminal_dominance_index=0.7. Compute market_value for residential property. Verify criminal_dominance_discount applies: prosperity_multiplier reduced by 0.7 * 0.50 = 0.35 compared to a province with criminal_dominance_index=0.0.
- `test_asking_price_converges_toward_market_value`: Property with asking_price=100,000 and market_value=150,000. After one monthly convergence: asking_price = 100,000 + (150,000 - 100,000) * 0.05 = 102,500. Verify convergence is gradual, not instantaneous.
- `test_commercial_tenant_auto_assignment`: Commercial property (rented=false) in province with NPCBusiness that has no owned premises. Verify tenant_id set to business id, rented=true, and business cost_per_tick reduced by commercial_cost_reduction_rate (10%).
- `test_property_sale_generates_evidence`: Player sells property for 300,000 (purchase_price was 200,000). Verify: player.wealth_delta = +300,000, capital gain = 100,000 flagged for tax, financial EvidenceToken created at VisibilityScope::institutional.
- `test_laundering_pressure_inflates_values`: Province with active real_estate LaunderingOperation (launder_rate_per_tick=5,000). Verify market_value includes launder_pressure term: 5,000 / launder_pressure_divisor added to prosperity_multiplier.
- `test_avg_property_value_recomputed_monthly`: Province with 3 properties (market_values: 100k, 200k, 300k). On monthly tick, verify province.avg_property_value = 200,000 (mean).
