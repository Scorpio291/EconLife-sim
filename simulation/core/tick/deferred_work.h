#pragma once

#include <cstdint>
#include <functional>
#include <queue>
#include <variant>
#include <vector>

namespace econlife {

// Forward declaration — in econlife namespace to match type definition
struct WorldState;

enum class WorkType : uint8_t {
    consequence,                 // ConsequenceEntry execution
    transit_arrival,             // TransitShipment arriving at destination
    interception_check,          // per-tick criminal shipment exposure check
    npc_relationship_decay,      // batch decay for one NPC's relationships
    evidence_decay_batch,        // batch decay for one evidence token
    npc_business_decision,       // quarterly decision for one NPCBusiness
    market_recompute,            // price recompute for one RegionalMarket
    investigator_meter_update,   // InvestigatorMeter recalc for one LE NPC
    climate_downstream_batch,    // agricultural + community stress update
    background_work,             // non-urgent: route table rebuild, log compaction
    npc_travel_arrival,          // NPC physically arrives at destination province
    player_travel_arrival,       // Player character physically arrives at destination
    community_stage_check,       // community response stage threshold evaluation
    maturation_project_advance,  // advance one MaturationProject per active project
    commercialize_technology,    // player command: bring researched tech to market
};

// Type-specific payload for deferred work items.
// Each variant member corresponds to a WorkType.
// Expanded as new work types require structured payloads.
struct ConsequencePayload {
    uint32_t consequence_id;
};

struct TransitPayload {
    uint32_t shipment_id;
    uint32_t destination_province_id;
};

struct NPCRelationshipDecayPayload {
    uint32_t npc_id;
};

struct EvidenceDecayPayload {
    uint32_t evidence_token_id;
};

struct NPCBusinessDecisionPayload {
    uint32_t business_id;
};

struct MarketRecomputePayload {
    uint32_t good_id;
    uint32_t region_id;
};

struct InvestigatorMeterPayload {
    uint32_t npc_id;
};

struct MaturationPayload {
    uint32_t business_id;
    uint32_t node_key;
};

struct CommercializePayload {
    uint32_t business_id;
    uint32_t node_key;
    uint8_t decision;  // CommercializationDecision enum value
};

struct PlayerTravelPayload {
    uint32_t destination_province_id;
};

struct EmptyPayload {};

using WorkPayload =
    std::variant<EmptyPayload, ConsequencePayload, TransitPayload, NPCRelationshipDecayPayload,
                 EvidenceDecayPayload, NPCBusinessDecisionPayload, MarketRecomputePayload,
                 InvestigatorMeterPayload, MaturationPayload, CommercializePayload,
                 PlayerTravelPayload>;

struct DeferredWorkItem {
    uint32_t due_tick;  // min-heap sort key
    WorkType type;
    uint32_t subject_id;  // NPC id, shipment id, market id, etc.
    WorkPayload payload;  // type-specific
};

// A sort key for the item's payload, so two items alike in due_tick, type and
// subject_id still have a defined order. Encodes the variant index alongside
// the payload's own identifying field — the consequence, shipment, NPC, market
// or business the work is about — which is what distinguishes them.
inline uint64_t deferred_payload_key(const WorkPayload& payload) {
    const uint64_t index = static_cast<uint64_t>(payload.index()) << 32;
    return index + std::visit(
                       [](const auto& p) -> uint64_t {
                           using T = std::decay_t<decltype(p)>;
                           if constexpr (std::is_same_v<T, ConsequencePayload>)
                               return p.consequence_id;
                           else if constexpr (std::is_same_v<T, TransitPayload>)
                               return p.shipment_id;
                           else if constexpr (std::is_same_v<T, NPCRelationshipDecayPayload>)
                               return p.npc_id;
                           else if constexpr (std::is_same_v<T, EvidenceDecayPayload>)
                               return p.evidence_token_id;
                           else if constexpr (std::is_same_v<T, NPCBusinessDecisionPayload>)
                               return p.business_id;
                           else if constexpr (std::is_same_v<T, MarketRecomputePayload>)
                               return p.good_id;
                           else if constexpr (std::is_same_v<T, InvestigatorMeterPayload>)
                               return p.npc_id;
                           else if constexpr (std::is_same_v<T, MaturationPayload>)
                               return p.business_id;
                           else if constexpr (std::is_same_v<T, CommercializePayload>)
                               return p.business_id;
                           else if constexpr (std::is_same_v<T, PlayerTravelPayload>)
                               return p.destination_province_id;
                           else
                               return 0u;
                       },
                       payload);
}

// Min-heap comparator: lowest due_tick has highest priority, and ties are
// broken by (type, subject_id, payload) so the order is a TOTAL one.
//
// Ordering on due_tick alone left every tie to the heap's internal array
// layout, which depends on the exact history of pushes and pops that produced
// it — not on the queue's contents. Two runs holding the same items could drain
// them in different orders, and a save made that visible: the serializer writes
// items in canonical (due_tick, type, subject_id) order, so a loaded queue
// drained ties canonically while the uninterrupted run drained them in
// insertion-history order. Same work, different sequence, and from there the
// RNG draws and floating-point accumulations parted company — a resumed game
// quietly diverged from the one that was saved.
//
// With the tie-break, drain order is a function of the queue's contents alone,
// which is what determinism requires of it and what the save format already
// assumed.
struct DeferredWorkComparator {
    bool operator()(const DeferredWorkItem& a, const DeferredWorkItem& b) const noexcept {
        if (a.due_tick != b.due_tick)
            return a.due_tick > b.due_tick;
        if (a.type != b.type)
            return static_cast<uint8_t>(a.type) > static_cast<uint8_t>(b.type);
        if (a.subject_id != b.subject_id)
            return a.subject_id > b.subject_id;
        return deferred_payload_key(a.payload) > deferred_payload_key(b.payload);
    }
};

using DeferredWorkQueue =
    std::priority_queue<DeferredWorkItem, std::vector<DeferredWorkItem>, DeferredWorkComparator>;

}  // namespace econlife
