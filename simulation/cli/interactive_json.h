#pragma once

#include <nlohmann/json.hpp>
#include <string>

#include "core/world_state/world_state.h"

namespace econlife {

// Serialize the UI-relevant subset of WorldState to JSON.
// This is deliberately separate from persistence serialization —
// it only includes fields the UI needs, optimized for size and speed.
nlohmann::json serialize_ui_state(const WorldState& world);

// Parse an incoming JSON command and, if it's an action, enqueue it.
// Returns true if the command was a valid action that was enqueued.
bool parse_and_enqueue_action(const nlohmann::json& cmd, WorldState& world);

// Convert a tick number to an in-game date string (Jan 1 2000 + tick days).
std::string tick_to_date(uint32_t tick);

}  // namespace econlife
