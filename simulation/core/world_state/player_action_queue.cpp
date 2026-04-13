#include "player_action_queue.h"

#include "world_state.h"

namespace econlife {

uint32_t enqueue_player_action(WorldState& world, PlayerActionType type,
                               PlayerActionPayload payload) {
    uint32_t seq = world.next_action_sequence++;

    PlayerAction action{};
    action.type = type;
    action.payload = std::move(payload);
    action.submitted_tick = world.current_tick;
    action.sequence_number = seq;

    world.player_action_queue.push_back(std::move(action));
    return seq;
}

void clear_player_action_queue(WorldState& world) {
    world.player_action_queue.clear();
}

bool has_pending_player_actions(const WorldState& world) {
    return !world.player_action_queue.empty();
}

}  // namespace econlife
