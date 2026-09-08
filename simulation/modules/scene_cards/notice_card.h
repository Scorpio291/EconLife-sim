#pragma once

#include <string>
#include <utility>

#include "scene_card_types.h"

namespace econlife {

// ---------------------------------------------------------------------------
// make_notice_card — the game telling the player something it has already
// decided: an offer was declined, a deal closed, an action could not be taken.
//
// Without this the player acts into silence. An acquisition offer that the
// seller refuses simply vanishes; a founding attempt that cannot produce an
// operating business used to take the money and say nothing. A notice is how
// the outcome reaches the player through the same channel as everything else
// they see, rather than through a stat they have to notice moving.
//
// Ambient by class (Scene Card Rulebook §1.3): it never interrupts and never
// expires, so it carries no default outcome — but it always carries the one
// choice that clears it, because a card the player cannot dismiss wedges the
// queue. The id is left at 0 so apply_deltas allocates it from
// WorldState::next_scene_card_id.
// ---------------------------------------------------------------------------
inline SceneCard make_notice_card(std::string text,
                                  SceneSetting setting = SceneSetting::phone_call) {
    SceneCard card{};
    card.id = 0;
    card.type = SceneCardType::news_notification;
    card.setting = setting;
    card.npc_id = 0;
    card.card_class = CardClass::ambient;
    card.npc_presentation_state = 0.0f;
    card.is_authored = false;
    card.chosen_choice_id = 0;

    DialogueLine line{};
    line.speaker_npc_id = 0;
    line.text = std::move(text);
    line.emotional_tone = 0.0f;
    card.dialogue.push_back(std::move(line));

    card.choices.push_back(PlayerChoice{1, "Noted", "", 0});
    return card;
}

}  // namespace econlife
