#pragma once

#include <cstdint>
#include <optional>
#include <vector>

// Complete type definitions needed for std::optional and std::vector members.
#include "modules/calendar/calendar_types.h"       // CalendarEntry
#include "modules/scene_cards/scene_card_types.h"  // SceneCard
#include "npc.h"                                   // MemoryEntry, Relationship, NPCStatus
#include "shared_types.h"                          // EvidenceToken, ObligationNode

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
    std::optional<NPCStatus> new_status;               // replacement
    std::optional<MemoryEntry> new_memory_entry;       // appended to memory_log
    std::optional<Relationship> updated_relationship;  // upsert by target_npc_id
    std::optional<float> motivation_delta;             // additive to specific motivation slot
};

struct PlayerDelta {
    std::optional<float> health_delta;                    // additive
    std::optional<float> wealth_delta;                    // additive; liquid cash only
    std::optional<SkillDelta> skill_delta;                // skill_id + additive value
    std::optional<uint32_t> new_evidence_awareness;       // evidence token id
    std::optional<float> exhaustion_delta;                // additive
    std::optional<RelationshipDelta> relationship_delta;  // player <-> NPC update
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
    std::optional<EvidenceToken> new_token;         // appended to evidence_pool
    std::optional<uint32_t> retired_token_id;       // set is_active = false
    std::optional<uint32_t> updated_token_id;       // token whose actionability to replace
    std::optional<float>    updated_actionability;  // replacement actionability (with updated_token_id)
};

struct ConsequenceDelta {
    // Forward-declared; ConsequenceEntry defined in evidence module
    // std::optional<ConsequenceEntry> new_entry;
    std::optional<uint32_t> new_entry_id;  // placeholder until consequence types available
    std::optional<uint32_t> cancelled_entry_id;
};

struct BusinessDelta {
    uint32_t business_id;
    std::optional<float> cash_delta;  // additive; operating cost deduction or revenue credit
    std::optional<float> revenue_per_tick_update;  // replacement; latest revenue figure
    std::optional<float> cost_per_tick_update;     // replacement; latest cost figure
    std::optional<float> output_quality_update;    // replacement; latest production quality [0,1]
};

struct CurrencyDelta {
    uint32_t nation_id;
    std::optional<float> usd_rate_update;         // replacement; new exchange rate
    std::optional<bool> pegged_update;            // replacement; peg status change
    std::optional<float> foreign_reserves_delta;  // additive; reserve depletion
};

struct RegionDelta {
    uint32_t region_id;
    std::optional<float> stability_delta;            // additive
    std::optional<float> inequality_delta;           // additive
    std::optional<float> crime_rate_delta;           // additive
    std::optional<float> addiction_rate_delta;       // additive
    std::optional<float> criminal_dominance_delta;   // additive
    std::optional<float> cohesion_delta;             // additive
    std::optional<float> grievance_delta;            // additive
    std::optional<float> institutional_trust_delta;  // additive
    std::optional<float> resource_access_delta;  // additive; applied to community.resource_access
    std::optional<uint8_t>
        response_stage_replacement;  // replacement; new community response stage (0-6)
    std::optional<float>
        infrastructure_rating_delta;  // additive; applied to province infrastructure_rating
};

// Accumulated state changes for one tick step.
// Pre-reserve vectors at WorldState initialization using known NPC count.
struct DeltaBuffer {
    std::vector<NPCDelta> npc_deltas;
    PlayerDelta player_delta;
    std::vector<MarketDelta> market_deltas;
    std::vector<EvidenceDelta> evidence_deltas;
    std::vector<ConsequenceDelta> consequence_deltas;
    std::vector<BusinessDelta> business_deltas;
    std::vector<RegionDelta> region_deltas;
    std::vector<CurrencyDelta> currency_deltas;
    std::vector<CalendarEntry> new_calendar_entries;
    std::vector<SceneCard> new_scene_cards;
    std::vector<ObligationNode> new_obligation_nodes;
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

// Thread-safe via partitioning: each province worker appends to its own partition.
// Main thread merges after join. Cleared each tick; not persisted.
struct CrossProvinceDeltaBuffer {
    std::vector<CrossProvinceDelta> entries;

    void push(CrossProvinceDelta delta);
    void flush_to_deferred_queue(struct WorldState& world_state);
};

}  // namespace econlife
