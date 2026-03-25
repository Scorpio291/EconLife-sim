#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

// Core headers (provide DeltaBuffer, DeferredWorkQueue, and transitive types)
#include "../tick/deferred_work.h"  // DeferredWorkQueue
#include "delta_buffer.h"           // DeltaBuffer + npc.h + shared_types.h

// Complete type definitions needed for std::vector/std::map value members
#include "geography.h"                                 // Nation, Province, Region
#include "modules/economy/economy_types.h"             // RegionalMarket, NPCBusiness
#include "modules/production/production_types.h"       // Facility, Recipe
#include "modules/trade_infrastructure/trade_types.h"  // TariffSchedule, NationalTradeOffer,
                                                       //   Lod1NationStats, RouteProfile
#include "modules/currency_exchange/currency_exchange_types.h"  // CurrencyRecord

namespace econlife {

// Forward declarations for types used only as pointers (complete type not needed)
struct PlayerCharacter;            // defined in player.h; used as PlayerCharacter*
struct GlobalCommodityPriceIndex;  // defined in trade_types.h; used as pointer
                                   // (already complete from trade_types.h include above,
                                   //  but kept as documentation of the pointer contract)

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

    // --- NPC Population ---
    std::vector<NPC> significant_npcs;       // full model; see §4
    std::vector<NPC> named_background_npcs;  // simplified model; same struct, LOD flag set
    // Background population is aggregated in Region.cohort_stats, not per-individual

    // --- Player ---
    PlayerCharacter* player;  // see §11; pointer to allow forward declaration

    // --- Economy ---
    std::vector<RegionalMarket> regional_markets;  // one per (good_id x province_id)
    std::vector<NPCBusiness> npc_businesses;
    std::vector<Facility> facilities;  // all production facilities; indexed by facility.id

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
    std::vector<NationalTradeOffer> lod1_trade_offers;  // regenerated monthly
    GlobalCommodityPriceIndex* lod2_price_index;        // updated annually
    std::map<uint32_t, Lod1NationStats> lod1_national_stats;
    std::map<std::pair<uint32_t, uint32_t>, std::array<RouteProfile, 5>>
        province_route_table;  // precomputed at load

    // --- Schema ---
    uint32_t current_schema_version;

    // --- Dirty Flags ---
    bool network_health_dirty;  // set by deltas touching relationships/obligations/movement

    // --- Cross-Province Delta Buffer ---
    CrossProvinceDeltaBuffer cross_province_delta_buffer;  // scratch; cleared each tick
};

}  // namespace econlife
