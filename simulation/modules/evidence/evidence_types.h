#pragma once

// Evidence module types — module-specific types for evidence lifecycle
// management (Tier 6). Core shared types (EvidenceToken, EvidenceType)
// are defined in core/world_state/shared_types.h.
//
// See docs/interfaces/evidence/INTERFACE.md for the canonical specification.

#include <cstdint>
#include <vector>

namespace econlife {

// ---------------------------------------------------------------------------
// EvidenceDecayBatch — describes a pending decay batch for one token.
// Scheduled via DeferredWorkQueue at WorkType::evidence_decay_batch.
// Fires every 7 ticks per token.
// ---------------------------------------------------------------------------
struct EvidenceDecayBatch {
    uint32_t token_id;         // evidence token to decay
    uint32_t due_tick;         // tick when this batch fires
    bool     holder_credible;  // cached credibility of primary holder at scheduling time
};

// ---------------------------------------------------------------------------
// EvidenceCreationEvent — describes an evidence token to be created.
// Produced when criminal_sector or regulatory_violation_severity conditions
// are detected on an NPC business.
// ---------------------------------------------------------------------------
struct EvidenceCreationEvent {
    uint32_t source_npc_id;          // NPC or business owner generating the evidence
    uint32_t target_npc_id;          // NPC this evidence is about (can be same as source)
    uint32_t province_id;            // province where the evidence was created
    uint8_t  evidence_type;          // EvidenceType cast to uint8_t
    float    initial_actionability;  // starting actionability (typically 1.0)
    float    decay_rate;             // per-tick base decay rate
    bool     player_aware;           // true if player created this directly
};

// ---------------------------------------------------------------------------
// CredibilityEvaluation — result of evaluating an NPC's credibility
// as an evidence holder.
// ---------------------------------------------------------------------------
struct CredibilityEvaluation {
    uint32_t npc_id;
    float    public_credibility;  // NPC's public credibility score
    bool     is_credible;         // true if credibility >= threshold
};

// ---------------------------------------------------------------------------
// EvidencePropagationEvent — describes evidence sharing between NPCs
// via knowledge graph propagation.
// ---------------------------------------------------------------------------
struct EvidencePropagationEvent {
    uint32_t token_id;               // evidence token being shared
    uint32_t sharer_npc_id;          // NPC sharing the evidence
    uint32_t receiver_npc_id;        // NPC receiving the evidence
    float    sharer_confidence;      // sharer's confidence in this evidence
    float    relationship_trust;     // trust between sharer and receiver
    float    received_confidence;    // computed: sharer_confidence * trust_factor
};

}  // namespace econlife
