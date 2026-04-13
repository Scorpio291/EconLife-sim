#pragma once

// Player action queue — public API for enqueuing player actions.
//
// External code (UI IPC, CLI) calls enqueue_player_action() between ticks.
// The player_actions module drains the queue at the start of each tick.
//
// Thread safety: V1 assumes single-threaded enqueue between ticks.
// For future IPC: wrap enqueue calls with a mutex before calling.

#include <cstdint>

#include "player_action_types.h"

namespace econlife {

struct WorldState;

// Enqueue a player action for processing on the next tick.
// Assigns submitted_tick = world.current_tick and an incrementing
// sequence_number for deterministic ordering.
// Returns the assigned sequence_number.
uint32_t enqueue_player_action(WorldState& world, PlayerActionType type,
                               PlayerActionPayload payload);

// Clear all pending actions. Called by player_actions module after processing.
void clear_player_action_queue(WorldState& world);

// Query: are there pending actions?
bool has_pending_player_actions(const WorldState& world);

}  // namespace econlife
