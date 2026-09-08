#pragma once

// scene_card_catalog — the card copy, as data.
//
// Every card the player reads used to be a string literal at the site that
// raised it, which put the writing inside the C++ and meant a change of wording
// needed a recompile. Goods, recipes, facility types, eras, occupations and the
// technology tree are all authored as CSV under packages/base_game; cards are
// the interface the player spends most of their time in and had no such layer.
//
// A template carries the card's shape (class, type, setting), its dialogue with
// {placeholders}, its choices, and which choice fires if it expires. Producers
// name a template and supply the parameters; scene_cards does the rest.

#include <string>
#include <unordered_map>
#include <vector>

#include "scene_card_types.h"

namespace econlife {

struct SceneCardTemplate {
    std::string card_key;
    CardClass card_class = CardClass::ambient;
    SceneCardType type = SceneCardType::news_notification;
    SceneSetting setting = SceneSetting::phone_call;
    std::string dialogue;  // may contain {placeholders}
    std::vector<PlayerChoice> choices;
    uint32_t default_choice_id = 0;
};

class SceneCardCatalog {
   public:
    // Loads every *.csv in `directory`. Returns the number of templates loaded.
    // A missing directory is not an error: the catalog is simply empty, and
    // producers fall back to the text they pass in.
    std::size_t load_from_directory(const std::string& directory);

    // Loads one CSV file. Exposed for tests.
    std::size_t load_from_file(const std::string& path);

    const SceneCardTemplate* find(const std::string& card_key) const;
    std::size_t size() const { return templates_.size(); }

    // Substitutes {name} placeholders from `params`. A placeholder with no
    // matching parameter is left as it is rather than blanked, so a missing
    // value is visible in play instead of producing a sentence with a hole in
    // it that nobody notices.
    static std::string inject(const std::string& text,
                              const std::vector<std::pair<std::string, std::string>>& params);

   private:
    std::unordered_map<std::string, SceneCardTemplate> templates_;
};

}  // namespace econlife
