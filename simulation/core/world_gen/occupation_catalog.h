#pragma once

// OccupationCatalog — the data-driven vocabulary of early *livelihoods*.
//
// Before "businesses" exist there are occupations: a person (or household) makes
// a living by doing a thing. Layer 1 are the subsistence/food livelihoods
// (forage, hunt, fish, farm, herd); Layer 2 are the surplus-funded specialists
// (artisan, healer, trader, elder, builder). A livelihood is NOT an NPCBusiness —
// it is a self-employed NPC. A business only crystallizes later, when a livelihood
// employs non-household labour and holds separable, persistent capital.
//
// Loaded from packages/base_game/occupations/occupations.csv so the vocabulary is
// data, not code — new occupations are a CSV edit. Occupations are referenced by a
// 1-based index (0 = "none / unassigned").

#include <cstdint>
#include <string>
#include <vector>

namespace econlife {

// 0 is reserved as "no occupation" (modern NPCs, or not-yet-assigned).
inline constexpr uint16_t kNoOccupation = 0;

struct OccupationDefinition {
    uint16_t index;            // 1-based; the runtime key (0 = none)
    std::string key;           // string id (e.g., "farmer", "trader")
    std::string display_name;  // human-readable
    uint8_t layer;             // 1 = subsistence/food livelihood; 2 = surplus-funded specialist
    float min_surplus;         // province food surplus needed to support this livelihood
    float knowledge_output;    // knowledge produced per worker (scholars/scribes > 0; 0 otherwise)
                               // — the engine that lets a society escape the Malthusian trap.
};

class OccupationCatalog {
   public:
    // Load occupations.csv from a directory (expects "<dir>/occupations.csv").
    bool load_from_directory(const std::string& dir);
    bool load_csv(const std::string& filepath);
    // Built-in fallback (mirrors the CSV) so the engine runs without package data.
    void load_builtin_default();

    const std::vector<OccupationDefinition>& occupations() const { return occupations_; }
    size_t size() const { return occupations_.size(); }
    bool empty() const { return occupations_.empty(); }

    const OccupationDefinition* by_index(uint16_t index) const;
    const OccupationDefinition* find(const std::string& key) const;

    // All occupations of a given layer, in load (index) order.
    std::vector<const OccupationDefinition*> in_layer(uint8_t layer) const;

   private:
    std::vector<OccupationDefinition> occupations_;  // sorted by index after load
};

}  // namespace econlife
