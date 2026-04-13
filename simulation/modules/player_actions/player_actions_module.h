#pragma once

// player_actions module — drains the PlayerActionQueue each tick, validates
// actions against current WorldState, and translates them into DeltaBuffer
// writes and DeferredWorkQueue items.
//
// This module runs before calendar and scene_cards so those downstream
// modules see the effects of player input (e.g., chosen_choice_id set,
// calendar entries committed, travel initiated).
//
// See docs/interfaces/player_actions/INTERFACE.md for canonical spec.

#include "core/tick/tick_module.h"

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class PlayerActionsModule : public ITickModule {
   public:
    std::string_view name() const noexcept override;
    std::string_view package_id() const noexcept override;
    ModuleScope scope() const noexcept override;

    std::vector<std::string_view> runs_after() const override;
    std::vector<std::string_view> runs_before() const override;

    void execute(const WorldState& state, DeltaBuffer& delta) override;
};

}  // namespace econlife
