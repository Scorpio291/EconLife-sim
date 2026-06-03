#pragma once

// Consequence queue — the delayed-consequence system (GDD §21).
// A timestamped registry of outcomes scheduled to fire in the future. The queue
// is the game's memory: it keeps processing regardless of player-character
// status (death does not clear it) and survives serialization. Entries are
// scheduled by domain modules via ConsequenceDelta and fired by
// drain_deferred_work when current_tick >= scheduled_tick.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace econlife {

// The eight consequence categories from GDD §21 (distinct from the calendar
// DeadlineConsequence ConsequenceType taxonomy in shared_types.h).
enum class ConsequenceCategory : uint8_t {
    financial_investigation = 0,
    criminal_investigation = 1,
    media_exposure = 2,
    political_consequence = 3,
    social_consequence = 4,
    legal_proceeding = 5,
    whistle_blower_contact = 6,
    rival_escalation = 7,
};

// A single scheduled consequence.
struct ConsequenceEntry {
    uint32_t id = 0;
    ConsequenceCategory category = ConsequenceCategory::social_consequence;
    uint32_t source_npc_id = 0;  // who set it in motion (may be dead by fire time)
    uint32_t target_id = 0;      // defendant/affected NPC; 0 = player
    uint32_t province_id = 0;    // where the effect lands
    uint32_t scheduled_tick = 0;
    bool fired = false;
    bool cancelled = false;
};

// GDD §21 base delay (ticks) by category.
inline uint32_t consequence_base_delay(ConsequenceCategory c) {
    switch (c) {
        case ConsequenceCategory::financial_investigation:
            return 90;
        case ConsequenceCategory::criminal_investigation:
            return 120;
        case ConsequenceCategory::media_exposure:
            return 45;
        case ConsequenceCategory::political_consequence:
            return 60;
        case ConsequenceCategory::social_consequence:
            return 30;
        case ConsequenceCategory::legal_proceeding:
            return 180;
        case ConsequenceCategory::whistle_blower_contact:
            return 75;
        case ConsequenceCategory::rival_escalation:
            return 45;
    }
    return 30;
}

// GDD §21 scheduling formula:
//   delay = BASE_DELAY[type] * (1 + variance[-0.2,0.2]) * awareness[0.5,2.0]
// `variance01` is a uniform draw in [0,1) (e.g. DeterministicRNG::next_float())
// mapped to [-0.2, 0.2]; `awareness` is the source NPC's visibility/capability.
inline uint32_t compute_consequence_delay(ConsequenceCategory c, float variance01,
                                          float awareness) {
    float variance = -0.2f + 0.4f * std::clamp(variance01, 0.0f, 1.0f);
    float aware = std::clamp(awareness, 0.5f, 2.0f);
    float d = static_cast<float>(consequence_base_delay(c)) * (1.0f + variance) * aware;
    return static_cast<uint32_t>(std::lround(static_cast<double>(d)));
}

// Build a scheduled ConsequenceEntry. The per-id variance de-synchronises event
// clustering deterministically (no RNG dependency): identical id+tick always
// schedule the same fire tick. Modules assign `new_consequence` from this and
// apply_deltas routes it into WorldState.consequence_queue.
inline ConsequenceEntry make_consequence(uint32_t id, ConsequenceCategory category,
                                         uint32_t source_npc_id, uint32_t target_id,
                                         uint32_t province_id, uint32_t current_tick,
                                         float awareness = 1.0f) {
    float variance01 = static_cast<float>((id * 2654435761u) % 100000u) / 100000.0f;
    ConsequenceEntry e{};
    e.id = id;
    e.category = category;
    e.source_npc_id = source_npc_id;
    e.target_id = target_id;
    e.province_id = province_id;
    e.scheduled_tick = current_tick + compute_consequence_delay(category, variance01, awareness);
    return e;
}

}  // namespace econlife
