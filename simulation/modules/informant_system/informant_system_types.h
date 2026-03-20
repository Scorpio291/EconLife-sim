#pragma once

// informant_system module types.
// See docs/interfaces/informant_system/INTERFACE.md for the canonical specification.

#include <cstdint>
#include <string>
#include <vector>

namespace econlife {

// ---------------------------------------------------------------------------
// InformantStatus — NPC cooperation state
// ---------------------------------------------------------------------------
enum class InformantStatus : uint8_t {
    not_cooperating = 0,
    cooperating     = 1,  // actively providing information to LE
    silenced        = 2,  // player countermeasure applied
    eliminated      = 3,  // player chose eliminate (triggers violence escalation)
    relocated       = 4,  // moved to witness protection
};

// ---------------------------------------------------------------------------
// PlayerCountermeasure — actions player can take against potential informants
// ---------------------------------------------------------------------------
enum class PlayerCountermeasure : uint8_t {
    pay_silence     = 0,  // costs 50000; suppresses cooperation
    threaten_silence = 1, // fear-based; may fail based on risk_tolerance
    relocate_witness = 2, // removes NPC from province; cooperative but unreachable
    eliminate       = 3,  // kills NPC; triggers violence escalation + LE multiplier 3.0
};

// ---------------------------------------------------------------------------
// InformantRecord — internal tracking for potential/active informants
// ---------------------------------------------------------------------------
struct InformantRecord {
    uint32_t npc_id;
    InformantStatus status;
    float flip_probability;         // computed from NPC state
    float base_flip_rate;           // 0.10 default
    uint32_t arrest_tick;           // when NPC was arrested/pressured
    uint32_t cooperation_start_tick;
    uint32_t compartmentalization_level; // number of intermediaries between NPC and player
};

}  // namespace econlife
