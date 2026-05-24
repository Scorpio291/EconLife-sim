# Module: real_estate

## Purpose
Manages per-province property markets each tick: collects rental income for occupied properties, recomputes monthly market values from provincial conditions, converges asking prices toward market value, processes property sale/purchase transactions, assigns commercial tenants to unoccupied properties, and updates province-level avg_property_value. Province-parallel.

## Inputs (from WorldState)
- `property_listings[]` — id, type (residential/commercial/industrial), province_id, owner_id, asking_price, market_value, rental_yield_rate, rental_income_per_tick, rented, tenant_id, launder_eligible, purchased_tick, purchase_price, **listed_for_sale** (V1 storage: held in `RealEstateModule::properties_` rather than on WorldState; round-tripped via the v7 module-state hook; schema_tag bumped 1→2 to carry `listed_for_sale`)
- `pending_property_transactions[]` — cross-module trigger queue on WorldState carrying `PropertyTransactionRequest{kind, property_id, actor_id, price}`. Emitted by `player_actions` (Tier 0) when the player issues `ListPropertyForSaleAction`, `UnlistPropertyAction`, or `MakePropertyOfferAction`. Drained at the start of `execute()` (global post-pass). Validation (ownership for list/unlist; listed state + offer ≥ asking + sufficient running cash for buy) is performed during the drain; invalid requests are silently dropped.
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
- `EvidenceDelta.new_token` — financial EvidenceToken at VisibilityScope::institutional on every property buy/sell transaction whose price ≥ `config.realestate.transaction_evidence_threshold` (default 50000). `actionability = clamp(0.20 + (price/threshold − 1) × 0.10, 0.20, 0.50)` — larger transactions leave a louder paper trail. `decay_rate = 0` (public registry records do not fade). Phase 1: emitted for both player-initiated buys (drain path) and NPC opportunistic buys of player listings.
- Property listing state updates via delta: market_value recomputed monthly, asking_price converged monthly, owner_id/purchase_price/purchased_tick updated on transactions, rented/tenant_id updated on tenant assignment/departure
- Province state updates via delta: avg_property_value recomputed monthly as mean of all PropertyListing.market_value in province
- Business cost_per_tick reduction when NPCBusiness occupies owned commercial premises (reduction rate: commercial_cost_reduction_rate = 0.10)
- `RegionDelta.homeless_rate_delta` — additive convergence delta toward the per-tick sample fraction of active NPCs whose `capital` falls below `homeless_rent_buffer_months * mean_residential_rent_per_tick`. Provinces with no residential listings use `homeless_rent_floor` as the rent baseline. Convergence rate is `homeless_rate_convergence` (default 0.05). Routes into `cohort_stats.homeless_rate` via `apply_region_deltas`.

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
- Property transactions whose price ≥ `transaction_evidence_threshold` generate exactly one financial EvidenceToken at institutional visibility per transaction; sub-threshold transactions emit none.
- Criminal dominance suppresses property values (criminal_dominance_discount = 0.50); laundering pressure inflates them. Both effects can coexist in the same province.
- `listed_for_sale == false` ⇒ property is not a valid target for `MakePropertyOfferAction`.

## Phase 1+2 Market Mechanics
Phase 1 shipped a symmetric at-asking cash market; **Phase 2** replaces instant settlement with a multi-tick `PendingTransaction` lifecycle. Both directions (player-buys-from-NPC, NPC-buys-from-player) flow through the same lifecycle.

**Player-initiated transactions** flow through `pending_property_transactions`:
1. `player_actions` validates the action shape and emits a `PropertyTransactionRequest` into the DeltaBuffer.
2. `apply_deltas` routes it into `state.pending_property_transactions`.
3. `real_estate.execute()` (global post-pass) drains the queue:
   - `list`: requires `actor_id == property.owner_id` and `price > 0`. Sets `listed_for_sale = true`, `asking_price = price`. **Immediate.**
   - `unlist`: requires `actor_id == property.owner_id`. Sets `listed_for_sale = false`. **Immediate.**
   - `buy`: requires `property.listed_for_sale`, `offer_price >= asking_price`, `actor_id != property.owner_id`, no active pending tx on the property, and running player wealth ≥ offer_price. **Creates a `PendingTransaction{stage = pending, close_tick = current_tick + close_delay}`** and reserves the offer in running wealth. Below-asking offers are dropped silently (Phase 3 wires negotiation).
   - `cancel`: requires an active pending tx on the property and `actor_id ∈ {buyer_id, seller_id}`. Marks the tx `cancelled`. Refunds the buyer's running-wealth reservation.

**Settlement pass** (same global post-pass, after the drain):
1. Iterate `pending_transactions`; for each entry with `stage == pending` and `close_tick <= current_tick`:
   - Re-check buyer's current cash (`running_player_wealth` for the player, `npc.capital` for NPCs). Reservations do not carry across tick boundaries — the queue itself is the authoritative record of in-flight commitments.
   - If sufficient: transfer ownership, debit buyer, credit seller, emit evidence, mark `settled`.
   - If insufficient: mark `expired`. No transfer occurs.
2. Settled, cancelled, and expired entries are pruned at the end of `execute()`.

**Close delays per type** (`RealEstateConfig`):
- residential: 7 ticks
- commercial: 30 ticks
- industrial: 30 ticks

**NPC opportunistic buys** of player-listed properties run after settlement:
1. Skip properties with an active pending tx (under contract).
2. Scan for listings where `asking_price / market_value < npc_buyer_deal_max_ratio` (default 0.90).
3. `deal_strength = clamp(1 - asking/market, 0, 1)`; per-NPC accept probability `p = deal_strength × npc_opportunistic_buy_rate` (base rate 0.02).
4. Iterate NPCs in same province in id-ascending order. The first NPC with sufficient running capital whose deterministic roll passes creates a **`PendingTransaction{stage = pending, close_tick = current_tick + close_delay}`** at the property's asking price. Per-tick RNG is forked from `world_seed ^ (tick × salt)`, then per-property from `tick_rng.next ^ (property_id × salt)`.
5. Settlement at `close_tick` follows the same path as player-initiated buys.

**Persistence**: `pending_transactions` round-trips via schema v9 (u32 count + per-entry record of id, property_id, buyer_id, seller_id, offer_price, offered_tick, close_tick, stage). v7/v8 saves load with the queue empty.

## Phase 3 Negotiation
**Phase 3** unlocks below-asking offers in both directions. Two paths:

**Below-asking buy → NPC accept-roll (drain path).**
When `req.price < prop.asking_price`, real_estate looks up the seller NPC. If the seller is not in `significant_npcs` (state-owned / player-owned that re-emitted via this drain) the request is dropped. Otherwise:

```
price_ratio       = offer_price / market_value
trust             = find_trust_toward(seller, buyer_id)
fear              = find_fear_toward(seller, buyer_id)
capital_pressure  = clamp(1 - seller.capital / distress_capital_threshold, 0, 1)
accept_score      = price_ratio
                  + trust_accept_weight  * trust
                  + fear_accept_weight   * fear
                  + capital_pressure_weight * capital_pressure
p_accept          = sigmoid((accept_score - 1.0) * sigmoid_steepness)
```

Deterministic RNG fork from `(world_seed ⊕ tick × salt₁ ⊕ property_id × salt₂ ⊕ buyer_id × salt₃)`. On accept, a `PendingTransaction` is created at the offered price; on reject, the request is silently dropped.

**NPC below-asking offer → SceneCard (opportunistic-buy scan path).**
The opportunistic scan now has two branches:
- **Path A (deal listing, `asking/market < npc_buyer_deal_max_ratio`):** at-asking opportunistic buy, unchanged from Phase 1/2.
- **Path B (non-deal listing):** NPCs in the same province with capital ≥ `market_value × npc_offer_min_ratio` each roll `p_offer = npc_offer_base_rate` (default 0.005/tick). On pass, a uniform offer price is drawn in `[market_value × npc_offer_min_ratio, asking_price × npc_offer_max_ratio]`. A `SceneCard{type = meeting, setting = private_office, npc_id = buyer}` is emitted to the player with two choices: `accept` (id = 1) and `decline` (id = 2). A `NegotiationContext{scene_card_id, property_id, buyer_id, seller_id, offer_price, offered_tick, deadline_tick}` is recorded in `RealEstateModule::active_negotiations_`. Only one offer per property per tick.

**Negotiation resolution** runs at the top of `execute()`'s global post-pass, before the drain:
- For each active negotiation, locate its scene card in `pending_scene_cards`:
  - `chosen_choice_id == 1` (accept): validate ownership still matches, no active pending tx on the property, buyer can still afford → create `PendingTransaction` at `offer_price`. Drop the context.
  - `chosen_choice_id == 2` (decline): drop the context.
  - `chosen_choice_id == 0` (no decision) AND `current_tick > deadline_tick`: drop the context (expired).
  - Otherwise: keep the context for next tick.

Properties with an active negotiation are skipped by both Path A and Path B in subsequent ticks until the negotiation resolves.

**Persistence**: `active_negotiations_` round-trips via the v7 module-state hook with `schema_tag = 3` (trailing block per record: scene_card_id, property_id, buyer_id, seller_id, offer_price, offered_tick, deadline_tick). v2 saves load with the queue empty; v1 saves predate `listed_for_sale` and load with that flag false.

## Phase 4 Mortgage Financing
**Phase 4** lets the player finance a property buy through a banking-managed `LoanRecord` with the property as collateral. A new `PaymentMethod` field on `PropertyTransactionRequest` selects cash / mortgage / mixed:

- **cash**: down_payment_fraction = 1.0; entire offer paid at close (Phase 1-3 behavior).
- **mortgage**: maximum-financing; down_payment_fraction is forced to the type minimum (residential 0.10, commercial 0.25, industrial 0.35).
- **mixed**: caller-supplied down_payment_fraction (must be >= type minimum; sub-minimum requests are rejected at the drain so callers know their structure was non-conforming).

**Underwriting at offer-time** (drain pass):
- down_payment_fraction must meet the type minimum.
- principal (= offer_price × (1 - dpf)) must be <= player.wealth × `player_max_loan_multiplier_of_wealth` (default 10x).
- `BankingModule::evaluate_loan_application(credit=0.5 baseline, dti=0, LoanPurpose::property_purchase, denial_dti=0.40)` must pass — the player's effective credit score is the banking default (0.5) until they take a loan.
- Failure → request silently dropped (same shape as below-asking rejection).

**Settlement** (close_tick reached):
- Buyer is debited only `offer_price × down_payment_fraction` (the cash portion).
- Seller is credited the full `offer_price` — the bank funds the remainder.
- Evidence token emitted for the full transaction (institutional visibility).
- A `NewLoanRequest` is pushed into `delta.new_loan_requests` with:
  - `borrower_id = buyer_id`, `purpose = property_purchase`, `principal = price × (1 - dpf)`
  - `interest_rate = config.realestate.mortgage_interest_rate`
  - `repayment_per_tick = BankingModule::compute_repayment_per_tick(principal, rate, term_ticks)`
  - `maturity_tick = current_tick + config.realestate.mortgage_term_ticks` (default 10950 ≈ 30y)
  - `collateral_id = property_id`
- `apply_deltas` routes new_loan_requests into `state.pending_loan_requests`.

**Banking integration** (Tier 5, same tick):
- Banking drains `pending_loan_requests` at the start of `execute()` and creates `LoanRecord` entries with `next_loan_id_`. From the next tick onward, banking's existing repayment lifecycle handles per-tick deductions from the borrower's wealth. Default detection (`consecutive_misses` → `in_default`) and collateral-seizure `ConsequenceDelta` emission are already in place; Phase 5 wires the foreclosure → owner-transfer half.

**Persistence**: schema v9→v10 extends each `pending_transaction` record with `payment_method`, `down_payment_fraction`, `interest_rate`, `loan_maturity_ticks`. v9 saves load with cash defaults (payment_method=cash, dpf=1.0, others zero). `pending_loan_requests` is defensively cleared on load (same-tick contract).

## Phase 5 Foreclosure
**Phase 5** wires mortgage default to physical-asset transfer. When a `property_purchase` LoanRecord in `BankingModule::active_loans_` transitions to `in_default` (existing banking behavior: `consecutive_misses > default_grace_ticks`), banking's `process_loan_default` now also emits a `PropertyForeclosureRequest{loan_id, property_id, borrower_id, lender_id}` and immediately winds down the loan (`outstanding_balance = 0`, `maturity_tick = originated_tick`) so banking's existing `retire_matured_loans` removes it at the end of `execute()`.

`apply_deltas` routes the foreclosure request into `WorldState.pending_property_foreclosures` (cross-tick: banking is Tier 5, real_estate is Tier 4 — consumption lands the following tick).

real_estate drains the queue at the very top of `execute()` (before negotiation resolution and before any other pass). For each request:
- If `property.owner_id != borrower_id` → no-op (innocent new owner protected; player sold the property before default crystalised).
- Otherwise:
  - Any active `PendingTransaction` on the property is marked `cancelled`.
  - Any active `NegotiationContext` on the property is removed.
  - `property.owner_id` is transferred to `lender_id` (0 = anonymous bank — the property becomes state/bank-held).
  - `property.listed_for_sale` is set to false. Phase 6 will introduce the auction path that re-lists seized properties; for V1 the property simply sits in bank inventory.
  - `purchased_tick` is updated; `purchase_price` is set to 0 (involuntary transfer).

**Persistence**: schema v10→v11 adds the `pending_property_foreclosures` footer (u32 count + per-entry record of loan_id, property_id, borrower_id, lender_id). v7..v10 saves load with the queue empty.

## Phase 6 Auctions
**Phase 6** adds an open auction pipeline. Bank-foreclosed properties (lender_id = 0 → bank liquidation policy) auto-open an `ActiveAuction` immediately after the Phase 5 seizure transfer, with `reserve_price = market_value × auction_reserve_fraction` (default 0.70) and a window of `auction_duration_ticks` (default 30).

`ActiveAuction` (WorldState, persisted schema v12): `{id, asset_id, consigner_id, reserve_price, opened_tick, closes_tick, status, current_high_bidder_id, current_high_bid, bids[]}`. Status ∈ {open, closed_sold, closed_no_reserve, cancelled}; terminal-state auctions are pruned the tick they resolve.

real_estate's global post-pass runs four auction sub-passes (after the foreclosure drain, before negotiation resolution):
1. **Open** auctions for freshly-foreclosed bank-held properties.
2. **Drain `pending_auction_bid_requests`** (player bids via `PlaceAuctionBidAction`, routed through `new_auction_bid_requests`). A bid registers iff: auction open, not past close, bid > current high, bidder can afford, and — for the opening bid — bid ≥ reserve. Sub-reserve opening bids are dropped.
3. **NPC bid scan**: for each open auction, NPCs in the asset's province (excluding the current high bidder) roll `npc_auction_bid_rate` (default 0.05/tick). A winner raises the high by `npc_auction_bid_increment` (default 5%) over the current high, or opens at the reserve. NPCs never bid above `market_value × npc_auction_max_value_ratio` (default 1.10). Deterministic RNG fork per (tick, auction).
4. **Settle** auctions at `close_tick`: high bid ≥ reserve and winner can still pay → `settle_transfer` (ownership to winner, winner debited, consigner credited — bank consigner_id=0 receives nothing, i.e. funds recovered by the anonymous bank, evidence emitted). No qualifying bid or winner can't pay → `closed_no_reserve` (consigner keeps the asset).

New player action: `PlaceAuctionBidAction{auction_id, bid_amount}` (PlayerActionType 13).

**Persistence**: schema v11→v12 adds the `active_auctions` footer (auctions + nested bids). `pending_auction_bid_requests` is defensively cleared on load (same-tick contract). v7..v11 saves load with no auctions.

## Phase 7 Raw Land + Zoning
**Phase 7** adds `PropertyType::raw_land` and a zoning-change mechanism honouring "no hard-locking — major changes need local-government approval." `PropertyListing` gains `subtype_key` (farmland / coastal / wilderness / island / urban_residential / urban_commercial / urban_industrial), `parcel_area_hectares`, and `zoned_use` (the development class currently permitted; equals `type` for built properties). raw_land carries no rental yield and is bought/sold/auctioned through the existing market generically.

`RequestZoningChangeAction{property_id, desired_use}` (PlayerActionType 14) emits a `ZoningChangeRequest` routed into `WorldState.pending_zoning_requests`. real_estate drains it same-tick:
- Owner check: only the property's owner may apply.
- Classification: a change is **major** if it involves industrial use (either side) or develops raw_land into a built class; otherwise **minor** (residential↔commercial).
- Approval probability = base (`zoning_minor_approval_prob` 0.60 / `zoning_major_approval_prob` 0.25) + `province.criminal_dominance_index × zoning_corruption_bonus` (0.30) — political capture eases approval — clamped to [0, 1]. Deterministic RNG fork per (tick, property, desired_use).
- On approval: `zoned_use` changes and `market_value` is nudged toward the target land baseline (`land_base_value_per_hectare(subtype_key) × parcel_area_hectares`) by `zoning_revaluation_rate` (0.20). On denial: no change (player may re-apply; no cooldown in V1).

Land subtype base values per hectare (authored defaults, designer-tunable): farmland 8k, urban_residential 250k, urban_commercial 400k, urban_industrial 180k, coastal 120k, wilderness 2k, island 50k.

**Persistence**: real_estate module-state `schema_tag` 3→4 adds `subtype_key` (length-prefixed string), `parcel_area_hectares` (f32), `zoned_use` (u8) per property. Pre-v4 records load with no subtype, zero area, and `zoned_use == type`. `pending_zoning_requests` is defensively cleared on load (same-tick contract).

## Phase 8 Subdivision + Re-merge
**Phase 8** lets a subdivisible building be split into individually-ownable units and recombined. `PropertyListing` gains `parent_property_id` (0 = standalone), `unit_count`, and `subdivided`. Subdivisible subtypes: apartment_block, office_tower, mixed_use_building, warehouse_complex, retail_center, industrial_park.

`SubdividePropertyAction{property_id, n_units}` (PlayerActionType 15) and `MergeUnitsAction{property_id}` (PlayerActionType 16) emit a `PropertySubdivisionRequest{kind, property_id, actor_id, n_units}` into `WorldState.pending_subdivision_requests`. real_estate drains it same-tick:

- **Subdivide**: owner-only; subtype must be subdivisible and not already subdivided; `n_units ∈ [subdivision_min_units (2), subdivision_max_units (100)]`; parent must not be under contract or in auction. Creates N child `PropertyListing`s each valued at `(parent_value / n) × subdivision_unit_premium` (1.10 — individually-sellable units carry a small premium), inheriting type/province/owner/yield/zoned_use, with `parent_property_id` set and a derived unit subtype (apartment_block → apartment_unit, etc). The parent becomes a dormant shell: `subdivided = true`, `unit_count = n`, unlisted, untenanted, and **not buyable** (the buy drain rejects subdivided parents).
- **Merge**: owner-only; the parent must be subdivided and the actor must own every live child, none of which may be under contract or in auction. Children are removed; the parent is restored (`subdivided = false`, `unit_count = 1`, `market_value = Σ child values`).

Child units are ordinary `PropertyListing`s once created, so the full Phase 1-6 market (list/buy/negotiate/mortgage/auction) applies to them individually — satisfying "per-unit and whole blocks both viable to buy."

**Persistence**: real_estate module-state `schema_tag` 4→5 adds `parent_property_id` (u32), `unit_count` (u32), `subdivided` (u8) per property. Pre-v5 records load as standalone (parent 0, unit_count 1, not subdivided). `pending_subdivision_requests` is defensively cleared on load.

## Phase 9 Location Flags
**Phase 9** adds a `location_flags` bitfield to `PropertyListing` (`LocationFlag_Offshore | LocationFlag_International | LocationFlag_Remote`). The concrete behaviour shipped now: **offshore** transactions emit **no institutional evidence token** (the offshore registry conceals ownership transfers — the laundering appeal). `remote` and `international` are stored for Phase 11 (raises construction cost) and forward-looking legal status. An "island" is simply `raw_land` with `subtype_key="island"` and the offshore flag — that combination is what makes islands mechanically distinct (untaxed in Phase 12, concealed transfers now).

**Persistence**: real_estate module-state `schema_tag` 5→6 adds `location_flags` (u8) per property; pre-v6 records load flag-free.

## Phase 10 Business Acquisition
**Phase 10** lets the player buy an NPC-owned business (and, implicitly, all its facilities — they follow via the unchanged `Facility.business_id` link). `AcquireBusinessAction{business_id, offer_multiple, payment_method, down_payment_fraction}` (PlayerActionType 17) emits a `BusinessAcquisitionRequest` into `WorldState.pending_business_acquisition_requests`. real_estate's post-pass (it hosts the player-asset-market machinery) drains it same-tick:

- Price = `revenue_per_tick × acquisition_ticks_per_month (30) × offer_multiple`. A zero-revenue business has no price and the offer is dropped.
- Owner accept-roll: `p_accept = logistic(((offer_multiple / acquisition_fair_multiple) − 1 + trust × trust_accept_weight) × sigmoid_steepness)`, where `trust` is the owner NPC's relationship trust toward the buyer (0 for independent businesses, owner_id=0). Deterministic RNG fork per (tick, business, buyer).
- Mortgage/mixed underwriting: down-payment ≥ `acquisition_min_down_payment` (0.40); principal ≤ `player.wealth × player_max_loan_multiplier_of_wealth`.
- On acceptance a `PendingBusinessAcquisition` is created with `close_tick = current_tick + acquisition_due_diligence_ticks (60)` and the cash portion reserved.

Settlement at `close_tick`: the business must still exist and still be owned by the recorded seller (else the acquisition is cancelled — protects against the business changing hands mid-deal). Buyer is debited the cash portion; the seller NPC (if any) is credited the full price; ownership transfers via `BusinessDelta.owner_id_update` (new field). Financed buys emit a `NewLoanRequest` with `purpose = business_capital` and `collateral_id = business_id` — the target business itself secures the loan (design default for collateral).

**Persistence**: schema v12→v13 adds the `pending_business_acquisitions` footer. `pending_business_acquisition_requests` is defensively cleared on load. `BusinessDelta` gained `owner_id_update` (applied by `apply_business_deltas`).

## Phase 11 Construction
**Phase 11** adds `BusinessSector::construction` and a service-trade construction pipeline: the player hires a contractor to build a `Facility` on a developed parcel. `Facility` gains a `property_id` link.

`RequestConstructionBidsAction{property_id, facility_type_key, recipe_id, bidding_window_ticks}` (PlayerActionType 18) opens a `ConstructionContract`. real_estate validates: client owns the parcel, parcel not subdivided, and `zoned_use != raw_land` (must be zoned for development first — ties to Phase 7). It then collects a `ConstructionBid` from every construction-sector firm in the parcel's province: `bid = construction_base_cost × remote_mult × (1 + margin)`, where margin is a deterministic draw in `[construction_margin_min, construction_margin_max]` (or **0 for a player-owned contractor** — internal cost), and `remote_mult = construction_remote_cost_multiplier` for `LocationFlag_Remote` parcels.

`AwardConstructionBidAction{contract_id, bid_index}` (PlayerActionType 19): real_estate validates ownership + affordability, escrows the bid amount (debits the client immediately), and moves the contract to `in_progress` with `expected_completion_tick = current_tick + bid.completion_ticks`.

At completion: the `Facility` is delivered onto the parcel (`is_operational = false` — labor_market staffs it), owned by a client-owned business in the province (auto-created if none exists), and the contractor's business is paid the escrowed amount via `BusinessDelta.cash_delta`. A player who owns the contractor recovers the cost internally. Bidding contracts with no award by `bidding_deadline_tick` are cancelled (no escrow was taken). Terminal contracts are pruned.

New `NewFacilityDelta` (applied by `apply_new_facilities`) is the runtime facility-creation path.

**Persistence**: schema v13→v14 — `Facility` gains a trailing `property_id` (pre-v14 facilities load with 0); new `construction_contracts` footer. `pending_construction_requests` / `_awards` are defensively cleared on load.

## Phase 12+13 Property Tax, Liens, Tax Sales
**Phase 12** assesses property tax every `property_tax_quarter_ticks` (90) against each parcel's owner. `PropertyListing` gains `unpaid_tax_balance`, `last_tax_assessment_tick`, `consecutive_delinquent_quarters`, `tax_lien`. Quarterly tax = `market_value × annual_rate / 4`, with per-type annual rates (residential 0.5%, commercial 1.0%, industrial 1.5%, raw_land 0.1%). State/bank-held parcels (owner 0), **offshore** parcels (Phase 9 exemption), and subdivided shells are skipped. A solvent owner is debited (player wealth / NPC capital) and their delinquency resets; an insolvent owner accrues `unpaid_tax_balance` and increments `consecutive_delinquent_quarters`.

**Phase 13** escalates delinquency:
- After `tax_lien_quarters` (2) delinquent quarters → `tax_lien = true`. A lien blocks voluntary sale (the buy drain rejects lien'd parcels) until cleared (paying current clears it).
- After `tax_sale_quarters` (4) → **tax sale**: any in-flight voluntary deal/negotiation on the parcel is cancelled, the parcel is seized to the state (`owner_id = 0`, lien/balance cleared), and a government-consigned `ActiveAuction` (consigner_id 0) opens with `reserve = min(unpaid_balance, market_value)` — reusing the Phase 6 auction pipeline end-to-end.

**Persistence**: real_estate module-state `schema_tag` 6→7 adds the four tax fields per property; pre-v7 records load tax-clean.

## Counter-offers
The negotiation loop is closed without free numeric input: when an NPC seller rejects a **serious** below-asking cash offer (`offer ≥ asking × negotiation_counter_min_ratio`, default 0.70) and no negotiation is already in flight on the parcel, it **counters** at `offer + (asking − offer) × negotiation_counter_split` (default 0.50) via a SceneCard. The player accepts (→ a `PendingTransaction` at the counter price, settling through the normal close path) or walks away. The resolution pass handles a player buyer (drawing on running player wealth) as well as the Phase 3/4 NPC-buyer case. Counters are cash-only in V1; financed below-asking offers that are rejected simply drop.

This completes the 13-phase player asset acquisition arc plus the counter-offer closure.

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
