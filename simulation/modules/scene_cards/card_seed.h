#pragma once

#include <string>
#include <utility>
#include <vector>

#include "core/world_state/delta_buffer.h"

namespace econlife {

// ---------------------------------------------------------------------------
// seed_card — how a module asks the world to say something to the player.
//
// The producer names an authored template and supplies its parameters. It does
// NOT write the sentence, choose the choices, allocate the id, or know the
// queue caps: scene_cards owns all of that when it drains the seed. Before
// this, every card the player read was a string literal at the site that
// raised it, so the writing lived in the C++ and a change of wording needed a
// recompile — while goods, recipes, eras and the technology tree were all
// authored as data.
//
// The seed lands in WorldState::pending_scene_card_seeds and is drained by
// scene_cards, this tick or the next depending on which side of it the
// producer runs. A seed naming a template the catalog does not hold is
// dropped: a card whose copy was never written is a content bug that should be
// visible, not a sentence invented at runtime.
// ---------------------------------------------------------------------------
inline void seed_card(DeltaBuffer& delta, std::string card_key, uint32_t npc_id = 0,
                      std::vector<std::pair<std::string, std::string>> params = {}) {
    SceneCardSeedDelta seed{};
    seed.card_key = std::move(card_key);
    seed.npc_id = npc_id;
    seed.params = std::move(params);
    delta.scene_card_seeds.push_back(std::move(seed));
}

}  // namespace econlife
