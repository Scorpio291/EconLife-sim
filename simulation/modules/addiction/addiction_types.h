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
};

}  // namespace econlife
