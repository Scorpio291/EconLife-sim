#pragma once

#include "core/tick/tick_module.h"
#include "core/world_state/world_state.h"

namespace econlife {

// CalendarModule — Tier 1 sequential tick module.
//
// Advances in-game date, checks deadline expirations on calendar entries,
// and emits consequence deltas for missed deadlines including relationship
// penalties, NPC unilateral actions, and NPC memory entries.
//
// Not province-parallel; operates on global state.
// Runs before: scene_cards. Runs after: nothing (early-tick module).
//
// See docs/interfaces/calendar/INTERFACE.md for the canonical specification.
class CalendarModule : public ITickModule {
public:
    std::string_view name() const noexcept override { return "calendar"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    std::vector<std::string_view> runs_after() const override {
        return {};
    }

    std::vector<std::string_view> runs_before() const override {
        return {"scene_cards"};
    }

    bool is_province_parallel() const noexcept override { return false; }

    void execute(const WorldState& state, DeltaBuffer& delta) override;

private:
    // Returns true if the NPC with the given id is dead in the current world state.
    static bool is_npc_dead(const WorldState& state, uint32_t npc_id);

    // Finds the NPC index in significant_npcs or named_background_npcs.
    // Returns nullptr if not found.
    static const NPC* find_npc(const WorldState& state, uint32_t npc_id);

    // Executes the 4-step missed deadline procedure for a single calendar entry.
    // Steps:
    //   1. Apply relationship_penalty to npc.relationships[player].trust
    //   2. Queue ConsequenceEntry at deadline_tick + consequence_delay_ticks
    //   3. If npc_initiative == true, queue NPC unilateral action
    //   4. Add memory entry: type=event, emotional_weight = -(0.3 + relationship_penalty)
    //
    // Steps 1 and 4 are skipped for dead NPCs. Step 2 and 3 always execute.
    static void execute_missed_deadline(const WorldState& state,
                                        DeltaBuffer& delta,
                                        const CalendarEntry& entry,
                                        uint32_t deadline_tick);
};

}  // namespace econlife
