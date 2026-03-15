# Module: weapons_trafficking

## Purpose
Processes the weapons trafficking supply chain each tick: legal market diversion from manufacturing businesses, corrupt official procurement, informal market pricing driven by territorial conflict demand, distribution through the RegionalMarket informal layer, and downstream chain-of-custody evidence generation when weapons are used in crimes. Weapons trafficking mirrors the drug economy architecture (same RegionalMarket infrastructure, same informal market layer, same supply chain pattern) but differs in the downstream effect system — weapons sold appear in subsequent crimes, generating a traceable chain of custody evidence trail that investigators can follow.

V1 supports four weapon types: small_arms, ammunition, arms_cache (bundled wholesale unit), and heavy_weapons (import only, no domestic production; arms embargo item). Heavy weapons generate intelligence service attention that cannot be suppressed by local corruption.

## Inputs (from WorldState)
- `npc_businesses` — manufacturing businesses producing small_arms or ammunition (formal sector); filtered for `diversion_fraction > 0.0` indicating active diversion to informal market. Also criminal_sector businesses distributing weapons.
- `regional_markets` — informal layer spot prices per (weapon_good_id x province_id); formal layer supply for legal weapons production
- `provinces` — province list; TerritorialConflictStage per province drives demand modifier
- `significant_npcs` — NPCs with role == law_enforcement (for chain_custody_actionability evidence delivery); NPCs with role == military_officer or law_enforcement (for corrupt procurement source)
- `obligation_network` — ObligationNode records for corrupt official procurement (player as creditor required)
- `evidence_pool` — existing chain-of-custody evidence tokens for investigator integration
- `current_tick` — for transit timing, diversion scheduling
- `config.weapons.*` — base_price per weapon type, price_floor_supply, max_diversion_fraction, chain_custody_actionability, embargo_meter_spike
- `deferred_work_queue` — pending TransitShipment entries for weapon goods in transit

## Outputs (to DeltaBuffer)
- `MarketDelta.supply_delta` — additive supply to informal market per (weapon_good_id, province_id) from diverted legal production and corrupt procurement this tick
- `MarketDelta.demand_buffer_delta` — weapons demand from criminal org territorial conflict; demand increases with conflict stage escalation
- `BusinessDelta.cash_delta` — revenue credited to weapons distributors at informal spot_price
- `BusinessDelta.diversion_fraction_delta` — updated diversion fraction for manufacturing businesses under player command or NPC coercion
- `EvidenceTokenDelta` — documentary token per diverted shipment (inventory discrepancy); physical token on corrupt procurement (movement of goods); testimonial token from corrupt official (witness); physical chain-of-custody token when weapon used in crime
- `InvestigatorMeterDelta.current_level` — immediate spike of config.weapons.embargo_meter_spike (0.25) for all LE NPCs in province on heavy_weapons import/transfer
- `ConsequenceDelta(embargo_investigation)` — queued on heavy_weapons transfer; cannot be suppressed by local corruption

## Preconditions
- Production module has completed; manufacturing output for small_arms and ammunition is finalized.
- Criminal operations module has completed; TerritorialConflictStage per province is current.
- Obligation network is current for corrupt procurement validation (player must be creditor on ObligationNode with the corrupt official).

## Postconditions
- All manufacturing businesses with diversion_fraction > 0.0 have diverted the specified fraction of output to the informal market, generating documentary evidence tokens per shipment.
- Informal market supply for weapon goods reflects diversions and procurement this tick.
- Weapons pricing reflects territorial conflict demand modifier: higher conflict stages increase demand and price.
- Chain-of-custody evidence is generated when any weapon unit with a known source is used in a crime event this tick.
- Heavy weapons transfers trigger embargo_investigation consequence and InvestigatorMeter spike for all LE NPCs in province; this spike is not reducible by corruption.

## Invariants
- WeaponType enum: small_arms=0, ammunition=1, heavy_weapons=2, converted_legal=3.
- Informal spot price formula: `informal_spot_price(weapon_type, province) = config.weapons.base_price[weapon_type] * (1.0 + territorial_conflict_demand_modifier(province)) / max(supply_this_tick, config.weapons.price_floor_supply)`.
- Territorial conflict demand modifier: none=0.0, economic/intelligence_harassment=0.2, property_violence=0.5, personnel_violence=0.8, open_warfare=1.5.
- Diversion fraction: 0.0 to config.weapons.max_diversion_fraction (default 0.30). Cannot exceed maximum.
- Legal market diversion requires: owner_id == player_id (own business) OR trust > 0.6 obligation with controlling NPC.
- Corrupt official procurement requires: NPC with role == military_officer or law_enforcement AND ObligationNode with player as creditor.
- Heavy weapons: import only in V1, no domestic production recipe. Arms embargo goods that generate intelligence service response.
- Embargo investigation consequence cannot be suppressed by local corruption (intelligence services operate at national level).
- Chain of custody: when a weapon used in a crime has a known weapon_source_id, a physical EvidenceToken is generated linking the weapon to its last known owner with actionability = config.weapons.chain_custody_actionability (0.60).
- Floating-point accumulations use canonical sort order (good_id ascending, then province_id ascending) for deterministic summation.
- Same seed + same inputs = identical weapons trafficking output regardless of core count.
- All random draws (interception checks, procurement availability) through `DeterministicRNG`.

## Failure Modes
- No manufacturing business producing weapons in a province: diversion supply = 0; informal supply depends on cross-province transit arrivals and corrupt procurement only. Log diagnostic.
- Corrupt official NPC dead, fled, or imprisoned: procurement channel unavailable. Player must find alternative source. Log warning.
- Diversion fraction set above max_diversion_fraction: clamp to maximum. Log warning.
- NaN or negative price from floating-point edge case (division by zero prevented by price_floor_supply): clamp to 0.0, log diagnostic.
- Heavy weapons import attempted but no import route available: operation fails; no supply added. Player notified via scene card.

## Performance Contract
- Province-parallel execution across up to 6 provinces (supply chain is province-local; cross-province effects go through CrossProvinceDeltaBuffer).
- Target: < 10ms total for weapons trafficking processing across 6 provinces on 6 cores.
- Must not exceed tick budget share that would push total tick above 200ms target.

## Dependencies
- runs_after: ["criminal_operations", "production"]
- runs_before: ["investigator_engine"]

## Test Scenarios
- `test_diversion_sends_output_to_informal_market`: Manufacturing business producing small_arms with diversion_fraction = 0.20, output = 100. Verify 20 units added to informal market supply and 80 remain in formal market supply.
- `test_diversion_generates_documentary_evidence`: Each diverted shipment generates one documentary EvidenceToken (inventory discrepancy). Verify token created with appropriate actionability.
- `test_corrupt_procurement_generates_evidence`: Player procures weapons from corrupt LE NPC via ObligationNode. Verify one physical EvidenceToken (movement of goods) and one testimonial EvidenceToken (corrupt official as witness) generated.
- `test_price_increases_with_territorial_conflict`: Province at personnel_violence stage (modifier 0.8). Verify informal_spot_price is 1.8x base_price (normalized by supply).
- `test_price_floor_prevents_division_by_zero`: Province with zero weapon supply. Verify price uses config.weapons.price_floor_supply (1.0) as denominator, not zero.
- `test_chain_custody_evidence_on_crime`: Weapon with known source used in crime event. Verify physical EvidenceToken generated linking weapon to last known owner with actionability 0.60.
- `test_embargo_meter_spike_on_heavy_weapons`: Heavy weapons transferred in province. Verify all LE NPCs in province receive InvestigatorMeter.current_level += 0.25.
- `test_embargo_investigation_ignores_corruption`: Heavy weapons transfer consequence. Verify embargo_investigation is not suppressed by regional_corruption_coverage.
- `test_diversion_fraction_clamped`: Attempt to set diversion_fraction = 0.50 (above max 0.30). Verify clamped to 0.30.
- `test_no_domestic_heavy_weapons_production`: Attempt to assign heavy_weapons recipe to domestic facility. Verify no production occurs; heavy_weapons are import-only.
- `test_province_parallel_determinism`: Run 50 ticks of weapons trafficking with 6 provinces on 1 core and 6 cores. Verify bit-identical supply, demand, and evidence outputs.
- `test_transit_interception_removes_shipment`: Weapon shipment intercepted during inter-province transit. Verify supply not delivered, physical EvidenceToken generated, and goods removed from transit pool.
