#pragma once

// legal_process module types.
// See docs/interfaces/legal_process/INTERFACE.md for the canonical specification.

#include <cstdint>
#include <string>
#include <vector>

namespace econlife {

// ---------------------------------------------------------------------------
// LegalCaseStage — multi-stage court proceedings state machine
// ---------------------------------------------------------------------------
enum class LegalCaseStage : uint8_t {
    investigation   = 0,
    arrested        = 1,
    charged         = 2,
    trial           = 3,
    convicted       = 4,
    acquitted       = 5,
    imprisoned      = 6,
    fined           = 7,
    paroled         = 8,
    pardoned        = 9,
};

// ---------------------------------------------------------------------------
// CaseSeverity — severity levels affecting sentencing
// ---------------------------------------------------------------------------
enum class CaseSeverity : uint8_t {
    minor           = 0,  // severity 1
    moderate        = 1,  // severity 2
    serious         = 2,  // severity 3
    major           = 3,  // severity 4
    severe          = 4,  // severity 5
    capital         = 5,  // severity 6
};

// ---------------------------------------------------------------------------
// LegalCase — a single court proceeding
// ---------------------------------------------------------------------------
struct LegalCase {
    uint32_t id;
    uint32_t defendant_npc_id;      // 0 = player is defendant
    uint32_t prosecutor_npc_id;
    uint32_t judge_npc_id;
    LegalCaseStage stage;
    CaseSeverity severity;
    float evidence_weight;          // aggregated from evidence tokens
    float defense_quality;          // 0.0-1.0; from defense counsel skill
    float conviction_probability;   // computed each tick
    uint32_t opened_tick;
    uint32_t sentence_ticks;        // severity * 365
    uint32_t release_tick;          // opened_tick + sentence_ticks
    uint32_t double_jeopardy_until; // tick after which re-filing is allowed (1825 ticks cooldown)
    bool is_player_case;
};

}  // namespace econlife
