#pragma once

// addiction module types.
// Module-specific types for the addiction module (Tier 10).

#include <cstdint>
#include <string>

namespace econlife {

enum class AddictionStage : uint8_t {
    none,
    casual,
    regular,
    dependent,
    active,
    recovery,
    terminal
};

struct AddictionState {
    AddictionStage stage = AddictionStage::none;
    std::string substance_key;
    float tolerance = 0.0f;  // 0.0-1.0
    float craving = 0.0f;    // 0.0-1.0
    uint32_t consecutive_use_ticks = 0;
    uint32_t clean_ticks = 0;
    uint32_t supply_gap_ticks = 0;
    float relapse_probability = 0.0f;

    // Addiction-local physical-health proxy (1.0 = healthy, 0.0 = fatal).
    // This is NOT the NPC's global health — the simulation has no such field
    // and the healthcare module tracks its own per-NPC health internally.
    // withdrawal_health is the addiction system's own severity-health: it
    // falls under withdrawal (dependent+ with a supply gap), recovers when
    // supplied, gates entry into the terminal stage, and triggers NPC death
    // when it reaches 0.0. Persisted in the per-NPC addiction footer.
    float withdrawal_health = 1.0f;
    // Consecutive ticks spent at terminal-low withdrawal_health while at
    // dependent/active. Drives the sustained-deprivation gate for terminal
    // stage entry; reset whenever health recovers above the threshold.
    uint32_t terminal_ticks = 0;
};

}  // namespace econlife
