#pragma once

// scene_cards module — public header for SceneCardsModule.
// See docs/interfaces/scene_cards/INTERFACE.md for canonical spec.

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "scene_card_types.h"  // SceneSetting, SceneCard (complete types)

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;

// Computes npc_presentation_state from the NPC's trust toward the player
// and their risk_tolerance. Returns a value in [0.0, 1.0].
//
// trust is on [-1.0, 1.0]; normalized to [0.0, 1.0] before weighting.
// risk_tolerance is already on [0.0, 1.0].
//
// Result: 0.0 = hostile/closed, 1.0 = open/cooperative.
float compute_presentation_state(float trust, float risk_tolerance, float trust_weight = 0.7f,
                                 float risk_weight = 0.3f);

// Returns true if the given SceneSetting requires physical co-location
// (player and NPC in the same province).
bool is_in_person_setting(SceneSetting setting);

class SceneCardsModule : public ITickModule {
   public:
    explicit SceneCardsModule(const SceneCardsConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override;
    std::string_view package_id() const noexcept override;
    ModuleScope scope() const noexcept override;

    std::vector<std::string_view> runs_after() const override;

    void execute(const WorldState& state, DeltaBuffer& delta) override;

   private:
    SceneCardsConfig cfg_;

    void resolve_player_choices(const WorldState& state, DeltaBuffer& delta) const;

    void discard_dead_npc_cards(const WorldState& state, DeltaBuffer& delta) const;

    void trigger_calendar_cards(const WorldState& state, DeltaBuffer& delta, uint32_t player_id,
                                uint32_t player_province) const;

    void apply_authored_priority(const WorldState& state, DeltaBuffer& delta) const;

    void finalize_new_cards(const WorldState& state, DeltaBuffer& delta, uint32_t player_id,
                            uint32_t player_province) const;
};

}  // namespace econlife
