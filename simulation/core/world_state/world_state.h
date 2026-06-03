#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

// Core headers (provide DeltaBuffer, DeferredWorkQueue, and transitive types)
#include "../tick/deferred_work.h"  // DeferredWorkQueue
#include "active_auction.h"         // ActiveAuction (Phase 6)
#include "construction_contract.h"  // ConstructionContract (Phase 11)
#include "delta_buffer.h"           // DeltaBuffer + npc.h + shared_types.h
#include "pending_transaction.h"    // PendingTransaction
#include "player_action_types.h"    // PlayerAction

// Complete type definitions needed for std::vector/std::map value members and unique_ptr members
#include "core/world_gen/goods_catalog.h"              // GoodsCatalog (unique_ptr member)
#include "geography.h"                                 // Nation, Province, Region
#include "modules/economy/economy_types.h"             // RegionalMarket, NPCBusiness
#include "modules/production/production_types.h"       // Facility, Recipe
#include "modules/trade_infrastructure/trade_types.h"  // TariffSchedule, NationalTradeOffer,
#include "player.h"  // PlayerCharacter (complete type for unique_ptr)
                     //   Lod1NationStats, RouteProfile
#include "modules/currency_exchange/currency_exchange_types.h"  // CurrencyRecord
#include "modules/technology/technology_types.h"                // GlobalTechnologyState

namespace econlife {

// GlobalCommodityPriceIndex is complete from trade_types.h include above;
// PlayerCharacter is complete from player.h include above.
// Both are needed as complete types for std::unique_ptr members.

enum class GameMode : uint8_t {
    ironman = 0,   // Timeline restoration locked. Achievement-eligible.
    standard = 1,  // Timeline restoration available with disruption consequences.
};

// Master simulation state container.
// Modules receive a const reference and write to DeltaBuffer.
// WorldState is never modified mid-tick.
//
// At full V1 scale (~2,000 significant NPCs, 6 provinces, ~50 goods per province),
// WorldState occupies ~10-15MB. Always pass by reference; never copy.
struct WorldState {
    uint32_t current_tick;  // absolute tick counter; monotonically increasing
    uint64_t world_seed;    // determinism anchor; used by all RNG calls

    // --- Geography ---
    std::vector<Nation> nations;        // V1: exactly 1 nation
    std::vector<Province> provinces;    // see §12
    std::vector<Region> region_groups;  // thin grouping layer
    std::vector<NamedFeature>
        named_features;  // Stage 10.1; geographic features (UI/encyclopedia only)
    std::vector<PreGameEvent> pre_game_events;  // Stage 10.3; living-memory events (NPC seeding)
    LoadingCommentary loading_commentary;       // Stage 10.4; world-specific loading screen text
    std::unordered_map<H3Index, uint32_t> h3_province_map;  // H3Index → province array index

    // --- NPC Population ---
    std::vector<NPC> significant_npcs;       // full model; see §4
    std::vector<NPC> named_background_npcs;  // simplified model; same struct, LOD flag set
    // Background population is aggregated in Region.cohort_stats, not per-individual

    // --- Player ---
    std::unique_ptr<PlayerCharacter> player;  // see §11; unique ownership

    // --- Goods Catalog ---
    // Loaded from CSV at world generation; transferred to WorldState so
    // modules can resolve string good_ids to the same numeric_id that
    // RegionalMarket.good_id was assigned at create_markets() time.
    // Persisted across save/load (schema v3+) so deserialised worlds
    // hold the same id mapping as when they were saved.
    // May be nullptr in unit tests that build WorldState piecemeal — the
    // lookup helpers fall back to the FNV-1a hash in that case.
    std::unique_ptr<GoodsCatalog> goods_catalog;

    // --- Economy ---
    std::vector<RegionalMarket> regional_markets;  // one per (good_id x province_id)
    std::vector<NPCBusiness> npc_businesses;
    std::vector<Facility> facilities;    // all production facilities; indexed by facility.id
    std::vector<Recipe> loaded_recipes;  // recipes loaded from CSV; immutable reference data

    // --- Evidence ---
    std::vector<EvidenceToken> evidence_pool;  // all active tokens in the world

    // --- Deferred Work Queue (unified) ---
    DeferredWorkQueue deferred_work_queue;

    // --- Consequence queue (GDD §21 delayed-consequence system) ---
    // Scheduled future outcomes; fired by drain_deferred_work when due.
    // Persisted (schema v17+); not cleared by player-character death.
    std::vector<ConsequenceEntry> consequence_queue;

    // --- Obligation Network ---
    std::vector<ObligationNode> obligation_network;

    // --- Scheduling ---
    std::vector<CalendarEntry> calendar;  // merged: player + NPC commitments

    // --- Scene Cards ---
    std::vector<SceneCard> pending_scene_cards;  // generated this tick, awaiting UI delivery

    // --- Global Tick Metadata ---
    uint32_t ticks_this_session;  // monotonic; reset on load; for WAL
    GameMode game_mode;           // set at game creation; immutable

    // --- Currency Exchange ---
    std::vector<CurrencyRecord> currencies;  // one per nation; keyed by nation_id

    // --- Trade and Transport Infrastructure ---
    std::vector<TariffSchedule> tariff_schedules;
    std::vector<NationalTradeOffer> lod1_trade_offers;            // regenerated monthly
    std::unique_ptr<GlobalCommodityPriceIndex> lod2_price_index;  // updated annually
    std::map<uint32_t, Lod1NationStats> lod1_national_stats;
    std::map<std::pair<uint32_t, uint32_t>, std::array<RouteProfile, 5>>
        province_route_table;  // precomputed at load

    // --- Technology & R&D ---
    GlobalTechnologyState
        technology;  // era tracking, domain knowledge, research/maturation projects

    // --- Schema ---
    uint32_t current_schema_version;

    // --- Dirty Flags ---
    bool network_health_dirty;  // set by deltas touching relationships/obligations/movement

    // --- Cross-Province Delta Buffer ---
    CrossProvinceDeltaBuffer cross_province_delta_buffer;  // scratch; cleared each tick

    // --- Cross-Module Trigger Queues ---
    // Scratch queues populated by apply_deltas from DeltaBuffer's new_*_seeds
    // vectors and drained by their consumer module within the same tick.
    // Must be empty at save time (consumer always runs within tick of emit;
    // any leftover entry would indicate a missing consumer or broken
    // topological ordering). Not persisted.
    //
    // pending_legal_case_seeds: written by any module observing an event
    // worth opening a legal investigation (e.g. investigator_engine when
    // its meter reaches raid_imminent). Drained by legal_process at the
    // start of its execute() — see legal_process_module.cpp for the
    // const_cast carve-out paralleling how player_actions writes to the
    // deferred_work_queue.
    std::vector<LegalCaseSeedDelta> pending_legal_case_seeds;

    // pending_racket_seeds: written by criminal_operations (Tier 7) when an
    // organization establishes a protection racket. Drained by
    // protection_rackets (Tier 8) in init_for_tick within the same tick.
    // Same-tick consumer contract (must be empty at save time); defensively
    // cleared on load. Not persisted.
    std::vector<RacketSeedDelta> pending_racket_seeds;

    // pending_laundering_seeds: written by criminal_operations (Tier 7) when an
    // organization launders illicit cash. Drained by money_laundering (Tier 9)
    // at the start of its execute() within the same tick. Same-tick contract;
    // defensively cleared on load. Not persisted.
    std::vector<LaunderingSeedDelta> pending_laundering_seeds;

    // pending_random_event_triggers: written by any module observing a
    // condition that should start a specific ActiveRandomEvent (e.g.
    // currency_exchange when a currency's foreign reserves drop below
    // the peg-break threshold). Drained by random_events at the start
    // of its execute(). This queue MAY hold entries across tick
    // boundaries because typical producers (currency_exchange Tier 11,
    // government_budget Tier 7) run after random_events (Tier 1) and
    // their emits do not see consumption until the next tick. Persisted
    // by persistence schema v8+ so a save mid-cycle does not drop the
    // pending trigger.
    std::vector<RandomEventTriggerDelta> pending_random_event_triggers;

    // pending_property_transactions: written by player_actions (Tier 0)
    // and future NPC seller-intent logic. Drained by real_estate at the
    // start of its execute() (Tier 4) within the same tick. Validation
    // (ownership for list/unlist, listed state + offer >= asking +
    // sufficient cash for buy) is performed during the drain — invalid
    // requests are silently dropped. The queue is expected to be empty
    // at save time (same-tick consumer contract); defensively cleared on
    // load to match the legal_case_seeds invariant.
    std::vector<PropertyTransactionRequest> pending_property_transactions;

    // Active (multi-tick) property transactions awaiting settlement at
    // their close_tick. Created by real_estate when a player-initiated
    // buy is accepted at-or-above asking, and by real_estate's NPC
    // opportunistic-buy scan. Settled / cancelled / expired entries are
    // pruned the same tick they reach a terminal state. Persisted
    // (schema v9+) so that mid-cycle saves do not drop in-flight
    // transactions.
    std::vector<PendingTransaction> pending_transactions;

    // pending_loan_requests: written by real_estate at settlement time
    // for mortgage / mixed-payment transactions. Drained by banking at
    // the start of its execute() (Tier 5 follows real_estate Tier 4).
    // Empty at save time (same-tick consumer contract); defensively
    // cleared on load.
    std::vector<NewLoanRequest> pending_loan_requests;

    // pending_property_foreclosures: written by banking when a
    // collateralised loan transitions to in_default; drained by
    // real_estate at the start of its execute() on the *next* tick
    // (banking Tier 5 emits, real_estate Tier 4 consumes — cross-tick).
    // Persisted by schema v11+ so a save mid-cycle does not drop the
    // pending foreclosure.
    std::vector<PropertyForeclosureRequest> pending_property_foreclosures;

    // Open / settled / cancelled property auctions (Phase 6). Created
    // by real_estate when bank-foreclosed properties enter the auction
    // pipeline (lender_id = 0 → bank's auction policy). Bidders
    // (player + NPCs) submit AuctionBidRequests; real_estate places
    // them on the auction and settles at close_tick. Persisted (schema
    // v12+) — auctions outlive a single tick and cross save boundaries.
    // Settled / closed_no_reserve / cancelled entries are pruned the
    // tick they reach a terminal state.
    std::vector<ActiveAuction> active_auctions;

    // pending_auction_bid_requests: written by player_actions
    // (PlaceAuctionBidAction). Drained by real_estate at the start of
    // its execute() (same-tick consumer contract). Defensively cleared
    // on persistence load.
    std::vector<AuctionBidRequest> pending_auction_bid_requests;

    // pending_zoning_requests: written by player_actions
    // (RequestZoningChangeAction). Drained by real_estate at the start
    // of its execute() (same-tick). Defensively cleared on load.
    std::vector<ZoningChangeRequest> pending_zoning_requests;

    // pending_subdivision_requests: written by player_actions
    // (Subdivide/MergeUnits). Drained by real_estate same-tick.
    // Defensively cleared on load.
    std::vector<PropertySubdivisionRequest> pending_subdivision_requests;

    // pending_business_acquisition_requests: written by player_actions
    // (AcquireBusinessAction). Drained by real_estate same-tick (offer
    // accept-roll). Defensively cleared on load.
    std::vector<BusinessAcquisitionRequest> pending_business_acquisition_requests;

    // Accepted, in-flight business acquisitions awaiting their
    // due-diligence close_tick. Created by real_estate when an owner
    // accepts an offer; settled at close (owner transfer + payment +
    // optional mortgage). Persisted (schema v13+).
    std::vector<PendingBusinessAcquisition> pending_business_acquisitions;

    // pending_construction_requests / _awards: written by player_actions
    // (RequestConstructionBids / AwardConstructionBid). Drained by
    // real_estate same-tick. Defensively cleared on load.
    std::vector<ConstructionBidsRequest> pending_construction_requests;
    std::vector<ConstructionAwardRequest> pending_construction_awards;

    // Live construction contracts (Phase 11): bidding → awarded →
    // in_progress → completed. Created/advanced by real_estate. Persisted
    // (schema v14+). Terminal-state (completed/cancelled) entries are
    // pruned the tick they resolve.
    std::vector<ConstructionContract> construction_contracts;

    // --- Player Action Queue ---
    // External code enqueues actions between ticks via enqueue_player_action().
    // The player_actions module drains this queue each tick.
    std::vector<PlayerAction> player_action_queue;
    uint32_t next_action_sequence = 0;  // monotonic; deterministic ordering

    // --- Computed: NPC province bucket index ---
    // Indices into significant_npcs grouped by current_province_id. Outer
    // vector is sized to provinces.size(); inner vectors hold significant_npcs
    // indices in ascending order (matches the order of significant_npcs, which
    // is id-ascending after world generation). Modules iterating "NPCs in
    // province p" should read npc_indices_by_province[p] instead of scanning
    // significant_npcs.
    //
    // Maintained by rebuild_npc_indices() in apply_deltas.cpp:
    //  - Built at world generation and persistence load.
    //  - Rebuilt by tick_orchestrator after every apply_deltas() call so the
    //    index always reflects the WorldState a module is about to read.
    //
    // Not serialized (rebuilt on load). Safe to read concurrently from
    // province-parallel workers because the rebuild runs single-threaded
    // between modules.
    std::vector<std::vector<uint32_t>> npc_indices_by_province;

    // --- Computed: NPC id → significant_npcs index ---
    // O(1) lookup for "give me the NPC with id X". Many modules previously
    // walked significant_npcs linearly to resolve a single id (find_npc
    // helpers in calendar, banking, healthcare, informant_system, scene_cards,
    // player_actions, media_system, evidence, obligation_network, ...).
    // Maintained by the same rebuild_npc_indices() call as the province
    // bucket; the value is an index into significant_npcs.
    std::unordered_map<uint32_t, uint32_t> npc_index_by_id;

    // --- Computed: NPC bucket index by home_province_id ---
    // Distinct from npc_indices_by_province (which is keyed by
    // current_province_id). Used by modules whose semantics are "residents
    // of this province" rather than "people physically in this province" —
    // e.g. community_response (grievance is felt by residents wherever they
    // are), government_budget (tax base is residents), business_lifecycle
    // (founders draw from local population). NPCs with home_province_id
    // out of range are skipped.
    std::vector<std::vector<uint32_t>> npc_indices_by_home_province;

    // --- Computed: regional_markets bucket index by province_id ---
    // Indices into regional_markets grouped by province_id. Replaces the
    // "for (auto& m : state.regional_markets) if (m.province_id == p)"
    // pattern in price_engine, supply_chain, production, npc_spending,
    // antitrust, commodity_trading, random_events, lod_system.
    std::vector<std::vector<uint32_t>> market_indices_by_province;

    // --- Computed: regional_markets composite-key index ---
    // (good_id, province_id) → regional_markets index, packed as
    // (good_id << 32) | province_id. O(1) "find the market for this good in
    // this province" lookup. Mirrors the per-call map that apply_market_deltas
    // already builds; modules that read markets by composite key route
    // through this index instead of building their own.
    std::unordered_map<uint64_t, uint32_t> market_index_by_good_province;
};

}  // namespace econlife
