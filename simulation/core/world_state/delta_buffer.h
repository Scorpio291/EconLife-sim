#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Complete type definitions needed for std::optional and std::vector members.
#include "consequence.h"                               // ConsequenceEntry, ConsequenceCategory
#include "modules/calendar/calendar_types.h"           // CalendarEntry
#include "modules/economy/economy_types.h"             // NPCBusiness (for NewBusinessDelta)
#include "modules/production/production_types.h"       // Facility (for NewFacilityDelta)
#include "modules/scene_cards/scene_card_types.h"      // SceneCard
#include "modules/trade_infrastructure/trade_types.h"  // NPCTravelStatus (complete type for optional)
#include "npc.h"                                       // MemoryEntry, Relationship, NPCStatus
#include "shared_types.h"                              // EvidenceToken, ObligationNode

namespace econlife {

// --- Per-entity delta structs ---
// Additive deltas are summed and clamped to domain range before application.
// Replacement fields overwrite in write order (last write wins within a tick step).
// No two modules in the same step should write replacement overrides to the same field.

struct SkillDelta {
    uint32_t skill_id;
    float value;  // additive
};

struct RelationshipDelta {
    uint32_t target_npc_id;
    float trust_delta;    // additive
    float respect_delta;  // additive
};

struct NPCDelta {
    uint32_t npc_id;
    std::optional<float> capital_delta;                // additive
    std::optional<uint16_t> new_occupation;            // replacement; NPC.occupation (livelihood)
    std::optional<NPCStatus> new_status;               // replacement
    std::optional<NPCTravelStatus> new_travel_status;  // replacement
    std::optional<MemoryEntry> new_memory_entry;       // appended to memory_log
    std::optional<Relationship> updated_relationship;  // upsert by target_npc_id
    std::optional<float> motivation_delta;  // additive to financial_gain slot (weights[0]); prefer
                                            // motivation_replacement for full vector
    std::optional<MotivationVector> motivation_replacement;  // replacement; full vector override
    std::optional<AddictionState> set_addiction_state;  // replacement; full AddictionState override
                                                        // (drug_economy seeds; AddictionModule
                                                        // writes back each tick)
    std::optional<float> age_delta;             // additive; population_aging advances age annually
    std::optional<float> risk_tolerance_delta;  // additive; clamped [0,1] (e.g. intimidation)
    std::optional<float>
        investigator_meter_fill_delta;  // additive; investigator_meter.current_level [0,1]
    std::optional<uint32_t>
        investigator_meter_target;  // replacement; investigator_meter.target_npc_id (sets opened)
};

struct PlayerDelta {
    std::optional<float> health_delta;                    // additive
    std::optional<float> wealth_delta;                    // additive; liquid cash only
    std::optional<SkillDelta> skill_delta;                // replacement; latest skill update wins
    std::optional<uint32_t> new_evidence_awareness;       // replacement; latest evidence token wins
    std::optional<float> exhaustion_delta;                // additive
    std::optional<RelationshipDelta> relationship_delta;  // replacement; latest update wins
    std::optional<uint32_t> new_province_id;              // replacement; player location
    std::optional<NPCTravelStatus> new_travel_status;     // replacement; travel state
    std::optional<float> reputation_business_delta;  // additive; reputation.public_business [-1,1]
    std::optional<float>
        reputation_political_delta;                // additive; reputation.public_political [-1,1]
    std::optional<float> reputation_social_delta;  // additive; reputation.public_social [-1,1]

    // Merge another PlayerDelta into this one.
    // Additive fields sum; replacement fields take the incoming value when set.
    // Used by DeltaBuffer::merge_from when collapsing per-province deltas in
    // ascending province order, so "last write wins" means highest province_id.
    void merge_from(PlayerDelta&& other);
};

struct MarketDelta {
    uint32_t good_id;
    uint32_t region_id;
    std::optional<float> supply_delta;                // additive
    std::optional<float> demand_buffer_delta;         // additive; written by Step 17
    std::optional<float> spot_price_override;         // replacement; set by Step 5
    std::optional<float> equilibrium_price_override;  // replacement; set by Step 5
};

struct EvidenceDelta {
    std::optional<EvidenceToken> new_token;    // appended to evidence_pool
    std::optional<uint32_t> retired_token_id;  // set is_active = false
    std::optional<uint32_t> updated_token_id;  // token whose actionability to replace
    std::optional<float>
        updated_actionability;  // replacement actionability (with updated_token_id)
};

struct ConsequenceDelta {
    // Schedule a fully-formed consequence into WorldState.consequence_queue
    // (GDD §21). The emitter computes scheduled_tick via compute_consequence_delay.
    std::optional<ConsequenceEntry> new_consequence;
    // Cancel a pending consequence by id (marks it cancelled; it will not fire).
    std::optional<uint32_t> cancelled_entry_id;
    // Legacy placeholder id (pre-consequence-system). Modules still emitting this
    // are not yet migrated to new_consequence; it is a no-op until then.
    std::optional<uint32_t> new_entry_id;
};

struct BusinessDelta {
    uint32_t business_id;
    std::optional<float> cash_delta;  // additive; operating cost deduction or revenue credit
    std::optional<float> revenue_per_tick_update;  // replacement; latest revenue figure
    std::optional<float> cost_per_tick_update;     // replacement; latest cost figure
    std::optional<float> output_quality_update;    // replacement; latest production quality [0,1]
    std::optional<uint32_t> owner_id_update;       // replacement; ownership transfer (Phase 10
                                                   // business acquisition)
    std::optional<uint32_t> next_decision_tick_update;  // replacement; npc_business advances the
                                                        // quarterly strategic-decision cadence
    std::optional<float> net_signal_update;  // replacement; per-tick facility detection signal
                                             // published by facility_signals (Tier 7), clamped to
                                             // [0,1] on apply. Consumed by investigator_engine.
};

// Depletion of a finite geological/biological deposit from extraction this tick.
// Emitted by production when an extraction-bound recipe runs against a province
// deposit. Applied additively (quantity_remaining -= quantity_extracted, clamped
// at 0) in deterministic (province_id, deposit_id) order. This is the mechanism
// that makes located resources finite and exhaustible.
struct DepositDelta {
    uint32_t province_id;
    uint32_t deposit_id;
    float quantity_extracted;  // >= 0; subtracted from ResourceDeposit.quantity_remaining
};

// Update to a province's fish stock from this tick's Schaefer dynamics (logistic
// growth minus harvest). Applied additively to FisheriesProfile.current_stock,
// clamped to [0, carrying_capacity]. Lets a renewable stock be fished sustainably
// — or overfished to collapse (the shared-access problem).
struct FisheriesDelta {
    uint32_t province_id;
    float stock_delta;  // signed; added to current_stock
};

// Business dissolved (market exit): removes entity from world.npc_businesses.
struct DissolvedBusinessDelta {
    uint32_t business_id;
};

// New business spawned (era entrant): appends entity to world.npc_businesses.
struct NewBusinessDelta {
    NPCBusiness new_business;
};

// New facility delivered (Phase 11 construction): appends to world.facilities.
struct NewFacilityDelta {
    Facility new_facility;
};

// Phase 11 — open a construction contract for bids. Emitted by
// player_actions (RequestConstructionBidsAction). Routed into
// WorldState.pending_construction_requests; real_estate drains same-tick,
// validates the parcel, collects contractor bids, and opens a
// ConstructionContract in the bidding stage.
struct ConstructionBidsRequest {
    uint32_t client_id;
    uint32_t property_id;
    std::string facility_type_key;
    std::string recipe_id;
    uint32_t bidding_window_ticks;
};

// Phase 11 — award a bid on an open contract. Emitted by player_actions
// (AwardConstructionBidAction). Routed into
// WorldState.pending_construction_awards; real_estate drains same-tick.
struct ConstructionAwardRequest {
    uint32_t client_id;
    uint32_t contract_id;
    uint32_t bid_index;
};

struct CurrencyDelta {
    uint32_t nation_id;
    std::optional<float> usd_rate_update;         // replacement; new exchange rate
    std::optional<bool> pegged_update;            // replacement; peg status change
    std::optional<float> foreign_reserves_delta;  // additive; reserve depletion
};

// LOD 2 global commodity price index update. Emitted annually by
// lod_system_module aggregating supply/demand across LOD 2 nations.
// lod_system is the sole writer; apply_lod2_price_deltas applies lerp
// smoothing using LodSystemConfig::lod2_smoothing_rate. Reading modules
// (price_engine, commodity_trading) read the smoothed value from
// `WorldState::lod2_price_index->lod2_price_modifier[good_id]`.
struct Lod2PriceIndexDelta {
    uint32_t good_id;
    float raw_modifier;  // unsmoothed ratio: consumption / max(production, supply_floor)
};

struct TechnologyDelta {
    // Era transition: replacement. Only one per tick (from TechnologyModule).
    std::optional<uint8_t> new_era;       // 1-based era index (see EraCatalog)
    std::optional<float> knowledge_delta;  // additive; GlobalTechnologyState.knowledge_level

    // Domain knowledge decay: additive per domain index.
    std::optional<uint8_t> domain_index;          // index into domain_knowledge[]
    std::optional<float> domain_knowledge_delta;  // additive (usually negative for decay)

    // Per-business maturation update: identifies a TechHolding to update.
    std::optional<uint32_t> business_id;
    std::optional<std::string> node_key;           // string key into holdings map
    std::optional<float> maturation_level_update;  // replacement
};

// Full replacement of a province's background-population cohorts plus the
// derived aggregates. Emitted by population_aging (the sole owner of the cohort
// lifecycle) at monthly/annual cadence; apply_deltas overwrites
// province.cohort_stats cohorts/total_population/mean_income/gini_coefficient.
struct CohortStatsDelta {
    uint32_t region_id;
    std::map<DemographicGroup, PopulationCohort> cohorts;  // replacement
    uint32_t total_population = 0;                         // sum(cohort.size)
    float mean_income = 0.0f;
    float gini_coefficient = 0.0f;
    float hardiness = 1.0f;  // replacement; population adaptation (drifts over generations)
};

struct RegionDelta {
    uint32_t region_id;
    std::optional<float> stability_delta;   // additive; conditions.stability_score
    std::optional<float> inequality_delta;  // additive; conditions.inequality_index

    // Population-fraction monitors — all live on RegionCohortStats since
    // the schema-v5 demographics consolidation. apply_region_deltas routes
    // these into province.cohort_stats->*.
    std::optional<float> crime_rate_delta;      // additive; cohort_stats->crime_rate
    std::optional<float> addiction_rate_delta;  // additive; cohort_stats->addiction_rate
    std::optional<float>
        criminal_dominance_delta;  // additive; cohort_stats->criminal_dominance_index
    std::optional<float>
        formal_employment_rate_delta;              // additive; cohort_stats->formal_employment_rate
    std::optional<float> sick_rate_delta;          // additive; cohort_stats->sick_rate
    std::optional<float> homeless_rate_delta;      // additive; cohort_stats->homeless_rate
    std::optional<float> unemployment_rate_delta;  // additive; cohort_stats->unemployment_rate

    std::optional<float> cohesion_delta;             // additive
    std::optional<float> grievance_delta;            // additive
    std::optional<float> institutional_trust_delta;  // additive
    std::optional<float> resource_access_delta;  // additive; applied to community.resource_access
    std::optional<uint8_t>
        response_stage_replacement;  // replacement; new community response stage (0-6)
    std::optional<float>
        infrastructure_rating_delta;  // additive; applied to province infrastructure_rating
    std::optional<float>
        avg_property_value_update;  // replacement; latest mean PropertyListing.market_value
    std::optional<float>
        regulatory_compliance_delta;  // additive; conditions.regulatory_compliance_index
    std::optional<float> drought_modifier_delta;  // additive; conditions.drought_modifier (->1.0)
    std::optional<float> flood_modifier_delta;    // additive; conditions.flood_modifier (->1.0)
    std::optional<float>
        subsistence_surplus_replacement;  // replacement; cohort_stats->subsistence_surplus_ratio
    std::optional<float>
        specialist_fraction_replacement;  // replacement; cohort_stats->specialist_fraction
                                          // (share of population freed from farming)
    std::optional<float>
        productive_capital_delta;  // additive; cohort_stats->productive_capital
                                   // (investment out of surplus, minus wear)
    std::optional<float>
        soil_health_delta;  // additive; cohort_stats->soil_health (working the land out,
                            // or letting it recover)
    std::optional<float>
        codified_knowledge_delta;  // additive; cohort_stats->codified_knowledge
                                   // (scribes copying, records decaying or burning)
                                          // (commons food production / need this tick)
    std::optional<float>
        food_store_replacement;  // replacement; cohort_stats->food_store (granary stock,
                                 // recomputed by the subsistence module from the year's net)
    std::optional<uint8_t>
        territorial_conflict_stage_replacement;  // replacement; cohort_stats->
                                                 // territorial_conflict_stage (0-5), published by
                                                 // criminal_operations from org conflict_state
    std::optional<float>
        grain_surplus_replacement;  // replacement; cohort_stats->grain_surplus (haulable
                                    // surplus this tick), published by subsistence
    std::optional<float>
        net_feedable_surplus_replacement;  // replacement; cohort_stats->net_feedable_surplus,
                                           // published by grain_logistics (ox-cart catchment)
    std::optional<float>
        urban_capacity_replacement;  // replacement; cohort_stats->urban_capacity, published by
                                     // grain_logistics — how many townsfolk the catchment COULD
                                     // feed. The town's actual size is a real cohort headcount
                                     // (cohort_stats->urban_population), moved by migration.
    std::optional<float>
        grain_import_rate_replacement;  // replacement; cohort_stats->grain_import_rate.
                                        // SIGNED net grain flow per tick into this
                                        // province's granary, published by grain_logistics
                                        // — the same conserved diffusion as
                                        // food_store_delta, exposed so the food balance
                                        // can see what arrives from elsewhere.
    std::optional<float>
        import_dependence_replacement;  // replacement; cohort_stats->import_dependence,
                                        // published by subsistence — the share of what a
                                        // province eats that came from somewhere else
    std::optional<float>
        asabiya_replacement;  // replacement; cohort_stats->asabiya, published by warfare.
                              // Forged at frontiers, decaying in the interior — the
                              // second oscillator that de-synchronises the world.
    std::optional<float>
        plague_susceptible_replacement;  // replacement; cohort_stats->plague_susceptible_fraction,
                                         // published by population_aging. Drawn down by each
                                         // wave, refilled by population turnover — the stock
                                         // that makes plague RECUR instead of blip.
    std::optional<float>
        supported_specialist_fraction_replacement;  // replacement; published by subsistence —
                                                    // the stratum THIS harvest supports, before
                                                    // the generational inertia on the held one
    std::optional<float>
        political_stress_replacement;  // replacement; cohort_stats->political_stress,
                                       // published by structural_demography (the PSI)
    std::optional<float>
        faction_death_fraction_replacement;  // replacement; cohort_stats->faction_death_fraction
                                             // [0,1], published by structural_demography (extra
                                             // annual death fraction from factional conflict;
                                             // consumed by population_aging as an independent
                                             // competing risk, separate from war)
    std::optional<float>
        ghost_land_fraction_replacement;  // replacement; cohort_stats->ghost_land_fraction,
                                          // published by energy_base (coal as extra acres)
    std::optional<float>
        coal_burned_replacement;  // replacement; cohort_stats->coal_burned_per_year,
                                  // published by energy_base. The matching DEPOSIT
                                  // drawdown rides on deposit_deltas — this is the
                                  // observable, that is the conserved flow.
    std::optional<float>
        war_death_fraction_replacement;  // replacement; cohort_stats->war_death_fraction
                                         // [0,1], published by warfare (extra annual death
                                         // fraction; consumed by population_aging)
    std::optional<float>
        food_store_delta;  // ADDITIVE; cohort_stats->food_store. Conserved grain flows
                           // (war rations/plunder/burn, grain redistribution). Floor 0
                           // at apply; capacity re-enforced by subsistence banking.
};

// NationDelta — national-level governance state (one per nation per tick at most).
// Routed by apply_nation_deltas into WorldState.nations[].political_cycle.
struct NationDelta {
    uint32_t nation_id;
    std::optional<float> legitimacy_update;  // replacement; national_legitimacy [0,1]. Derived per
                                             // tick by political_cycle from aggregated provincial
                                             // conditions; transient (not serialized).
    std::optional<float> approval_delta;     // additive; national_approval [0,1]
    std::optional<uint8_t> government_type_update;  // replacement; regime change (e.g. Autocracy
                                                    // collapse -> FailedState)
};

// --- Cross-province communication ---
// Effects take hold at the start of the following tick (one-tick propagation delay).

struct CrossProvinceDelta {
    uint32_t source_province_id;
    uint32_t target_province_id;
    uint32_t due_tick;  // current_tick + 1

    // Exactly one of these is populated per entry:
    std::optional<NPCDelta> npc_delta;
    std::optional<MarketDelta> market_delta;
    // EvidenceToken and DeferredWorkItem payloads added in Pass 2
};

// Scene card choice — sets chosen_choice_id on a pending scene card.
// Written by player_actions module; applied before scene_cards module reads.
struct SceneCardChoiceDelta {
    uint32_t scene_card_id;
    uint32_t chosen_choice_id;
};

// Calendar commit — sets player_committed on a calendar entry.
// Written by player_actions module; applied before calendar module reads.
struct CalendarCommitDelta {
    uint32_t calendar_entry_id;
    bool committed;
};

// Legal case seed — request to open a new LegalCase at the investigation
// stage. Emitted by any module observing a triggering event (e.g.
// investigator_engine when its meter crosses raid_imminent). Routed by
// apply_deltas into WorldState.pending_legal_case_seeds; legal_process
// drains the queue at the start of its execute() within the same tick.
//
// severity is the underlying value of CaseSeverity (kept as u8 here so
// delta_buffer.h does not depend on legal_process_types.h).
struct LegalCaseSeedDelta {
    uint32_t defendant_npc_id;      // 0 = player defendant
    uint32_t lead_investigator_id;  // NPC opening the case; 0 if none
    uint8_t severity;               // CaseSeverity enum value
    uint32_t province_id;
    float initial_evidence_weight;  // starting evidence accumulated so far
};

// Random event trigger — request to start a specific ActiveRandomEvent
// from any module observing a triggering condition (e.g.
// currency_exchange when a peg breaks). Routed by apply_deltas into
// WorldState.pending_random_event_triggers; random_events drains the
// queue at the start of its execute() and instantiates the event using
// the named template.
//
// Unlike LegalCaseSeedDelta, this trigger may cross tick boundaries:
// producers later in the tick order (e.g. currency_exchange Tier 11)
// emit triggers that random_events (Tier 1) cannot consume until the
// next tick. The pending queue is therefore persisted (schema v8+).
struct RandomEventTriggerDelta {
    std::string template_key;  // matches RandomEventTemplate.id
    uint32_t province_id;
    float severity;  // 0.0–1.0; clamped to template's [min,max] range
};

// Protection-racket seed — request to start a racket on a target business.
// Emitted by criminal_operations (Tier 7) when an organization establishes
// extortion in its province. Routed by apply_deltas into
// WorldState.pending_racket_seeds; protection_rackets (Tier 8) drains the
// queue in init_for_tick within the same tick, looking the target business up
// in WorldState to derive its demand_per_tick. Same-tick contract: the queue
// must be empty at save time (defensively cleared on load).
struct RacketSeedDelta {
    uint32_t criminal_org_id;
    uint32_t target_business_id;
    uint32_t province_id;
};

// Money-laundering seed — request to open a laundering operation for an
// organization's illicit cash. Emitted by criminal_operations (Tier 7);
// routed into WorldState.pending_laundering_seeds and drained by
// money_laundering (Tier 9) at the start of its execute() the same tick,
// which fills in the per-method launder/loss rates from config. Same-tick
// contract: empty at save time (defensively cleared on load).
struct LaunderingSeedDelta {
    uint32_t actor_id;                 // org leadership NPC doing the laundering
    float dirty_amount;                // illicit cash to wash
    uint32_t destination_business_id;  // front the clean money lands in (0 = direct)
};

// Property transaction request — player_actions (and future NPC seller
// logic) emit these to drive real-estate market state changes. Routed
// by apply_deltas into WorldState.pending_property_transactions;
// real_estate drains the queue at the start of its execute() within
// the same tick (real_estate runs at Tier 4, after player_actions at
// Tier 0). Validation (ownership, listing state, sufficient cash) is
// performed by real_estate; player_actions emits requests without
// touching the module-private properties_ vector.
//
// Phase 1 (symmetric at-asking cash market, instant settle): supports
// list / unlist / buy. Below-asking buy requests are dropped during
// drain. Pending state, financing, negotiation, subdivision, raw_land,
// and business acquisition come in later phases.
enum class PropertyTransactionKind : uint8_t {
    list = 0,    // owner flags property listed_for_sale = true; updates asking_price
    unlist = 1,  // owner flags property listed_for_sale = false
    buy = 2,     // buyer offers; creates a PendingTransaction awaiting close_tick
    cancel = 3,  // buyer or seller aborts the active PendingTransaction on a
                 // property. property_id identifies the under-contract property
                 // (at most one active pending_transaction per property).
};

// Phase 4 — payment method for a buy request. Cash settles in full at
// close. Mortgage finances the full price; buyer pays only the down
// payment in cash at close, the remainder is a LoanRecord created in
// banking with collateral_id = property_id. Mixed splits the difference
// using a caller-supplied down_payment_fraction.
enum class PaymentMethod : uint8_t {
    cash = 0,
    mortgage = 1,
    mixed = 2,
};

// Phase 10 — business acquisition offer. Emitted by player_actions
// (AcquireBusinessAction). Routed by apply_deltas into
// WorldState.pending_business_acquisition_requests; real_estate's
// post-pass drains it same-tick, runs the owner accept-roll, and on
// acceptance creates a PendingBusinessAcquisition. collateral_is_target
// (default true per design) finances the buy against the target
// business itself when payment_method != cash.
struct BusinessAcquisitionRequest {
    uint32_t business_id;
    uint32_t buyer_id;
    float offer_multiple;  // price = revenue_per_tick × ticks_per_month × this
    uint8_t payment_method;
    float down_payment_fraction;
};

// Phase 8 — composite property subdivision / re-merge request. Emitted
// by player_actions (SubdividePropertyAction / MergeUnitsAction).
// Routed by apply_deltas into WorldState.pending_subdivision_requests;
// real_estate drains the queue at the start of its execute() same-tick.
enum class SubdivisionKind : uint8_t {
    subdivide = 0,  // split a subdivisible parent into n_units children
    merge = 1,      // recombine all live children back into the parent
};

struct PropertySubdivisionRequest {
    SubdivisionKind kind;
    uint32_t property_id;  // the parent property
    uint32_t actor_id;
    uint32_t n_units;  // subdivide: number of child units; ignored for merge
};

// Phase 7 — zoning-change application on an owned property. Emitted by
// player_actions on RequestZoningChangeAction. Routed by apply_deltas
// into WorldState.pending_zoning_requests; real_estate drains the queue
// at the start of its execute() within the same tick. The local
// government's approval roll (deterministic) decides whether the
// property's zoned_use changes. desired_use is the underlying
// PropertyType enum value encoded as u8 to keep delta_buffer.h free of
// per-module includes.
struct ZoningChangeRequest {
    uint32_t property_id;
    uint32_t actor_id;
    uint8_t desired_use;  // PropertyType enum value
};

// Phase 6 — bid submission to an open ActiveAuction. Emitted by
// player_actions on PlaceAuctionBidAction (and by NPC auction logic in
// real_estate's own scan). Routed by apply_deltas into
// WorldState.pending_auction_bid_requests; real_estate drains the
// queue at the start of its execute() within the same tick (Tier 4
// follows player_actions Tier 0). Validation (auction exists & open,
// bid above current high, bidder has cash) is performed during the
// drain — invalid bids are dropped.
struct AuctionBidRequest {
    uint32_t auction_id;
    uint32_t bidder_id;
    float bid_amount;
};

// Phase 5 — cross-module foreclosure trigger. Emitted by banking when a
// property_purchase loan transitions to in_default; routed by
// apply_deltas into WorldState.pending_property_foreclosures;
// real_estate drains at the start of its execute() (banking Tier 5 →
// real_estate Tier 4 next tick) and transfers the collateral
// PropertyListing's ownership to the lender (lender_id; 0 = anonymous
// bank → unowned). In-flight pending_transactions on the property are
// marked cancelled; active negotiations are dropped. The loan itself
// is wound down by banking (outstanding_balance set to 0 → next
// retire_matured_loans pass removes it).
struct PropertyForeclosureRequest {
    uint32_t loan_id;
    uint32_t property_id;
    uint32_t borrower_id;
    uint32_t lender_id;
};

// Phase 4 — cross-module request from real_estate at settlement time to
// banking to originate a LoanRecord with the supplied parameters. Routed
// via apply_deltas into WorldState.pending_loan_requests; banking
// drains the queue at the start of its execute() within the same tick
// (banking Tier 5 follows real_estate Tier 4). purpose is the underlying
// banking LoanPurpose enum encoded as u8 to keep delta_buffer.h free of
// per-module includes.
struct NewLoanRequest {
    uint32_t borrower_id;
    uint32_t lender_id;  // 0 = anonymous bank (V1 default)
    uint8_t purpose;     // LoanPurpose enum value (property_purchase = 1)
    float principal;
    float interest_rate;
    float repayment_per_tick;
    uint32_t maturity_tick;
    uint32_t collateral_id;  // PropertyListing.id for property_purchase loans
};

struct PropertyTransactionRequest {
    PropertyTransactionKind kind;
    uint32_t property_id;
    uint32_t actor_id;  // player.id for player-initiated; NPC.id for NPC-initiated
    float price;        // asking_price for list, offer_price for buy, ignored for unlist
    PaymentMethod payment_method = PaymentMethod::cash;
    float down_payment_fraction = 1.0f;  // cash → 1.0; mortgage → 0.0; mixed → in (0, 1)
};

// Accumulated state changes for one tick step.
// Pre-reserve vectors at WorldState initialization using known NPC count.
//
// Merge policy (see merge_from below):
// - Every vector field appends (move-extend) — entries from `other` are
//   moved onto the end of the corresponding vector here.
// - `player_delta` merges through PlayerDelta::merge_from (additive +
//   last-write-wins per field).
//
// IMPORTANT: When you add a new field to DeltaBuffer, you MUST update
// merge_from in delta_buffer.cpp, or per-province writes to the new
// field will be silently dropped during the orchestrator's province
// merge. The test_delta_buffer_merge_covers_every_field unit test will
// fail when a new field is added without corresponding merge support.
struct DeltaBuffer {
    std::vector<NPCDelta> npc_deltas;                            // merge: append
    PlayerDelta player_delta;                                    // merge: PlayerDelta::merge_from
    std::vector<MarketDelta> market_deltas;                      // merge: append
    std::vector<EvidenceDelta> evidence_deltas;                  // merge: append
    std::vector<ConsequenceDelta> consequence_deltas;            // merge: append
    std::vector<BusinessDelta> business_deltas;                  // merge: append
    std::vector<RegionDelta> region_deltas;                      // merge: append
    std::vector<CurrencyDelta> currency_deltas;                  // merge: append
    std::vector<TechnologyDelta> technology_deltas;              // merge: append
    std::vector<Lod2PriceIndexDelta> lod2_price_index_deltas;    // merge: append
    std::vector<CalendarEntry> new_calendar_entries;             // merge: append
    std::vector<SceneCard> new_scene_cards;                      // merge: append
    std::vector<ObligationNode> new_obligation_nodes;            // merge: append
    std::vector<CrossProvinceDelta> cross_province_deltas;       // merge: append
    std::vector<DissolvedBusinessDelta> dissolved_businesses;    // merge: append
    std::vector<NewBusinessDelta> new_businesses;                // merge: append
    std::vector<SceneCardChoiceDelta> scene_card_choice_deltas;  // merge: append
    std::vector<CalendarCommitDelta> calendar_commit_deltas;     // merge: append
    std::vector<LegalCaseSeedDelta> new_legal_case_seeds;        // merge: append
    std::vector<RandomEventTriggerDelta> new_random_event_triggers;     // merge: append
    std::vector<PropertyTransactionRequest> new_property_transactions;  // merge: append
    std::vector<NewLoanRequest> new_loan_requests;                      // merge: append
    std::vector<PropertyForeclosureRequest> new_property_foreclosures;  // merge: append
    std::vector<AuctionBidRequest> new_auction_bid_requests;            // merge: append
    std::vector<ZoningChangeRequest> new_zoning_requests;               // merge: append
    std::vector<PropertySubdivisionRequest> new_subdivision_requests;   // merge: append
    std::vector<BusinessAcquisitionRequest> new_business_acquisitions;  // merge: append
    std::vector<NewFacilityDelta> new_facilities;                       // merge: append
    std::vector<ConstructionBidsRequest> new_construction_requests;     // merge: append
    std::vector<ConstructionAwardRequest> new_construction_awards;      // merge: append
    std::vector<RacketSeedDelta> new_racket_seeds;                      // merge: append
    std::vector<LaunderingSeedDelta> new_laundering_seeds;              // merge: append
    std::vector<CohortStatsDelta> cohort_stats_deltas;                  // merge: append
    std::vector<NationDelta> nation_deltas;                             // merge: append
    std::vector<DepositDelta> deposit_deltas;                           // merge: append
    std::vector<FisheriesDelta> fisheries_deltas;                       // merge: append

    // Merge another DeltaBuffer into this one. Vectors are move-extended;
    // player_delta merges through PlayerDelta::merge_from. After the call
    // `other` is in a valid but unspecified (moved-from) state.
    void merge_from(DeltaBuffer&& other);
};

// Cross-province effects are accumulated here during apply_deltas (main thread only).
// Province-parallel modules write cross-province deltas to their per-province
// DeltaBuffer, which the orchestrator merges sequentially before apply_deltas.
// Cleared each tick; not persisted.
struct CrossProvinceDeltaBuffer {
    std::vector<CrossProvinceDelta> entries;

    void push(CrossProvinceDelta delta);
};

}  // namespace econlife
