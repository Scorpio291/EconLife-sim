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
#include "delta_buffer.h"           // DeltaBuffer + npc.h + shared_types.h
#include "player_action_types.h"    // PlayerAction

// Complete type definitions needed for std::vector/std::map value members and unique_ptr members
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

    // --- Economy ---
    std::vector<RegionalMarket> regional_markets;  // one per (good_id x province_id)
    std::vector<NPCBusiness> npc_businesses;
    std::vector<Facility> facilities;    // all production facilities; indexed by facility.id
    std::vector<Recipe> loaded_recipes;  // recipes loaded from CSV; immutable reference data

    // --- Evidence ---
    std::vector<EvidenceToken> evidence_pool;  // all active tokens in the world

    // --- Deferred Work Queue (unified) ---
    DeferredWorkQueue deferred_work_queue;

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

    // --- Player Action Queue ---
    // External code enqueues actions between ticks via enqueue_player_action().
    // The player_actions module drains this queue each tick.
    std::vector<PlayerAction> player_action_queue;
    uint32_t next_action_sequence = 0;  // monotonic; deterministic ordering
};

}  // namespace econlife
