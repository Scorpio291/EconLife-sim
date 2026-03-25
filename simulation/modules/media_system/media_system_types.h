#pragma once

// media_system module types.
// Module-specific types for the media_system module (Tier 7).
//
// See docs/interfaces/media_system/INTERFACE.md for the canonical specification.

#include <cstdint>
#include <string>
#include <vector>

namespace econlife {

// ---------------------------------------------------------------------------
// MediaOutletType — type of media outlet
// ---------------------------------------------------------------------------
enum class MediaOutletType : uint8_t {
    newspaper = 0,
    television = 1,
    digital_outlet = 2,
    social_platform = 3,
};

// ---------------------------------------------------------------------------
// StoryTone — editorial tone of a published story
// ---------------------------------------------------------------------------
enum class StoryTone : uint8_t {
    neutral = 0,
    positive = 1,
    damaging = 2,
};

// ---------------------------------------------------------------------------
// MediaOutlet — a media organization in the simulation
// ---------------------------------------------------------------------------
struct MediaOutlet {
    uint32_t id;
    uint32_t province_id;
    MediaOutletType type;
    float credibility;                     // 0.0-1.0
    float reach;                           // 0.0-1.0; fraction of province audience
    float editorial_independence;          // 0.0-1.0
    uint32_t owner_npc_id;                 // 0 = independent; player_id or npc_id
    std::vector<uint32_t> journalist_ids;  // NPC ids of journalists at this outlet
};

// ---------------------------------------------------------------------------
// Story — a published media story
// ---------------------------------------------------------------------------
struct Story {
    uint32_t id;
    uint32_t subject_id;     // NPC this story is about
    uint32_t journalist_id;  // who published
    uint32_t outlet_id;      // where published
    StoryTone tone;
    float evidence_weight;  // mean actionability of attached evidence
    float amplification;    // accumulated amplification (starts at 1.0)
    uint32_t published_tick;
    std::vector<uint32_t> evidence_token_ids;
    bool is_active;  // false after propagation window expires
};

}  // namespace econlife
