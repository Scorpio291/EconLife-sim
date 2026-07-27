#pragma once

// EraCatalog — loads the simulation's era timeline from data
// (packages/base_game/eras/eras.csv) instead of a hardcoded C++ enum.
//
// Eras are the spine of the whole design: the timeline runs from the dawn
// (settlement formation / subsistence) forward through agrarian, industrial,
// modern, and future epochs. Making the timeline data-driven means eras (and
// the per-era economic regime, scope, and default entry point) can be added or
// reordered by editing the CSV — no recompile — exactly as goods/recipes are.
//
// An era is identified by a 1-based index (era_index). Content gates on the
// index via `era_available <= current_era`, so the catalog carries DEFINITIONS
// (name, year, regime, scope, default entry) while the index itself stays the
// lightweight runtime key the rest of the engine already uses.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace econlife {

// Maximum number of eras the engine will hold. Fixed-capacity per-actor arrays
// (e.g. ActorTechnologyState::era_ceilings) are sized to this, so the data-driven
// era count may grow up to — but not beyond — this capacity without a recompile.
constexpr uint8_t MAX_ERA_CAPACITY = 32;

// ---------------------------------------------------------------------------
// EraDefinition — one era loaded from CSV
// ---------------------------------------------------------------------------
// Shared regime gate for era-gated modules: is `regime` one of the module's
// active economic regimes? (One helper instead of a per-module copy — a change to
// regime matching must not have to be patched in four modules.)
inline bool regime_in(const std::vector<std::string>& active_regimes, std::string_view regime) {
    for (const auto& r : active_regimes) {
        if (r == regime)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Canonical regime classes.
//
// regime_in() answers "is this regime in MY configured list", which is the right
// question for a module whose active set is a tunable. These answer the different,
// structural question "what KIND of economy is this regime" — and that answer must
// be the same everywhere, because modules that disagree about it silently
// desynchronize. That is not hypothetical: the same list was hand-expanded in
// population_aging, world_generator, premarket_genesis and the society-evolution
// harness, and a drift between two of those copies produced a real bug (a stale
// war-mortality signal) recorded in the session log.
//
// Adding a regime means editing these lists, once.
// ---------------------------------------------------------------------------

// The pre-market arc: the whole climb from the dawn to the eve of the modern
// economy. Commons food, no markets to speak of, era-paced by knowledge.
inline bool is_premarket_regime(std::string_view regime) {
    return regime == "subsistence" || regime == "barter" || regime == "coinage" ||
           regime == "money" || regime == "feudal" || regime == "mercantile" ||
           regime == "industrial";
}

// Stratified regimes: wealth concentrates into a lord/owner stratum that takes a
// tithe, and estates (manors) exist as ownable holdings.
inline bool is_manorial_regime(std::string_view regime) {
    return regime == "feudal" || regime == "mercantile" || regime == "industrial";
}

// The modern market economy and beyond: firms, prices and wages do the allocating.
inline bool is_market_regime(std::string_view regime) {
    return regime == "modern" || regime == "near_future" || regime == "space_age";
}

struct EraDefinition {
    uint8_t index;                 // 1-based timeline position; the runtime era key
    std::string key;               // string id (e.g., "subsistence", "turn_of_millennium")
    std::string display_name;      // human-readable
    int32_t start_year;            // calendar year the era opens (negative = BCE)
    std::string economic_regime;   // behavioral regime hint: "subsistence", "barter",
                                   //   "mercantile", "industrial", "modern", ...
    bool is_default_entry;         // true for the era a freshly generated world enters
                                   //   by default when no starting era is specified
    bool v1_in_scope;              // true if this era is within V1 scope
    float knowledge_to_advance;    // accumulated knowledge needed to advance OUT of this era
                                   //   (0 = not knowledge-gated; advances by other triggers)
};

// ---------------------------------------------------------------------------
// EraCatalog — loaded from the eras CSV
// ---------------------------------------------------------------------------
class EraCatalog {
   public:
    // Load eras.csv from the given directory (expects "<dir>/eras.csv").
    // Returns false if the file is missing or no rows parsed.
    bool load_from_directory(const std::string& eras_dir);

    // Load a single CSV file. Replaces any existing eras.
    bool load_csv(const std::string& filepath);

    // Populate from the built-in default timeline (used when no data dir is
    // provided, mirroring the goods catalog's fallback behaviour). Keeps the
    // engine runnable in unit tests that don't load packages.
    void load_builtin_default();

    const std::vector<EraDefinition>& eras() const { return eras_; }
    size_t size() const { return eras_.size(); }
    bool empty() const { return eras_.empty(); }

    // Highest era index present (== size when indices are contiguous from 1).
    uint8_t max_era() const;
    // Highest index flagged v1_in_scope (0 if none).
    uint8_t v1_max_era() const;
    // The default entry era index (the is_default_entry row; falls back to 1).
    uint8_t default_entry_index() const;

    // Lookups (nullptr if absent).
    const EraDefinition* by_index(uint8_t index) const;
    const EraDefinition* find(const std::string& key) const;

   private:
    std::vector<EraDefinition> eras_;  // sorted by index ascending after load
};

}  // namespace econlife
