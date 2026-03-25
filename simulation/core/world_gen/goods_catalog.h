#pragma once

// GoodsCatalog — loads good definitions from CSV files in packages/base_game/goods/.
// Each CSV row defines a tradeable good with its base price, tier, category, etc.
// The catalog is the single source of truth for what goods exist in the simulation.

#include <cstdint>
#include <string>
#include <vector>

namespace econlife {

// ---------------------------------------------------------------------------
// GoodDefinition — one tradeable good loaded from CSV
// ---------------------------------------------------------------------------
struct GoodDefinition {
    uint32_t numeric_id;       // assigned at load time, sequential from 0
    std::string good_id;       // string key from CSV (e.g., "iron_ore", "wheat")
    std::string display_name;  // human-readable name
    uint8_t tier;              // 0-4; determines supply chain depth
    std::string unit;          // "tonne", "litre", "unit"
    std::string category;      // "geological", "biological", "metals", etc.
    float base_price;          // starting equilibrium price
    bool perishable;           // true = decays during transit/storage
    bool illegal;              // true = contraband (coca_leaf, poppy)
    uint8_t era_available;     // era when this good becomes available (1-5)
};

// ---------------------------------------------------------------------------
// GoodsCatalog — loaded from CSV directory
// ---------------------------------------------------------------------------
class GoodsCatalog {
   public:
    // Load all goods_tier*.csv files from the given directory.
    // Returns false if no files found or parse errors occurred.
    bool load_from_directory(const std::string& goods_dir);

    // Load a single CSV file. Appends to existing goods.
    bool load_csv(const std::string& filepath);

    // Access
    const std::vector<GoodDefinition>& goods() const { return goods_; }
    size_t size() const { return goods_.size(); }

    // Filter goods available at a given era and up to a given tier.
    std::vector<const GoodDefinition*> goods_available_at(uint8_t era, uint8_t max_tier) const;

    // Find by string ID. Returns nullptr if not found.
    const GoodDefinition* find(const std::string& good_id) const;

   private:
    std::vector<GoodDefinition> goods_;
    uint32_t next_numeric_id_ = 0;
};

}  // namespace econlife
