#pragma once

// scene_cards module — public header for SceneCardsModule.
// See docs/interfaces/scene_cards/INTERFACE.md for canonical spec.

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "scene_card_catalog.h"  // SceneCardCatalog
#include "scene_card_types.h"    // SceneSetting, SceneCard (complete types)

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
    // Loads the card catalog from cfg.card_catalog_directory if one is set.
    // A host that does not set it gets an empty catalog, which is a working
    // state for everything that does not seed cards.
    explicit SceneCardsModule(const SceneCardsConfig& cfg = {});

    std::string_view name() const noexcept override;
    std::string_view package_id() const noexcept override;
    ModuleScope scope() const noexcept override;

    std::vector<std::string_view> runs_after() const override;

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // The authored card copy. Loaded once from packages/base_game/scene_cards;
    // empty until then, which is a working state — producers that pass their
    // own text still get a card, they just get an unauthored one.
    void load_catalog(const std::string& directory);
    const SceneCardCatalog& catalog() const { return catalog_; }

   private:
    SceneCardsConfig cfg_;
    SceneCardCatalog catalog_;

    // Turn this tick's seed requests into real cards.
    void drain_card_seeds(const WorldState& state, DeltaBuffer& delta) const;

    // Builds a card from an authored template. Returns false when the catalog
    // holds no such template, in which case nothing is written: a card whose
    // copy was never written is a content bug that should be visible, not a
    // sentence invented at runtime.
    bool compose_from_template(const std::string& card_key, uint32_t card_id, uint32_t npc_id,
                               const std::vector<std::pair<std::string, std::string>>& params,
                               SceneCard& out) const;

    void resolve_player_choices(const WorldState& state, DeltaBuffer& delta) const;

    // Queue lifecycle: discards dead-NPC cards, retires resolved ones, fires
    // default outcomes for expired timed-optional cards, and holds the ambient
    // queue at its cap. This is what keeps pending_scene_cards bounded.
    void run_queue_lifecycle(const WorldState& state, DeltaBuffer& delta) const;

    void trigger_calendar_cards(const WorldState& state, DeltaBuffer& delta, uint32_t player_id,
                                uint32_t player_province) const;

    void apply_authored_priority(const WorldState& state, DeltaBuffer& delta) const;

    void finalize_new_cards(const WorldState& state, DeltaBuffer& delta, uint32_t player_id,
                            uint32_t player_province) const;
};

}  // namespace econlife
