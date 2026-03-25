#pragma once

// RecipeCatalog — loads recipe definitions from CSV files in packages/base_game/recipes/.
// Each CSV row defines a production recipe with inputs, outputs, and requirements.
// Mirrors the GoodsCatalog pattern: load from directory, immutable after init.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "modules/production/production_types.h"

namespace econlife {

class RecipeCatalog {
   public:
    // Load all recipes_*.csv files from the given directory.
    bool load_from_directory(const std::string& recipes_dir);

    // Load a single CSV file. Appends to existing recipes.
    bool load_csv(const std::string& filepath);

    // Access
    const std::vector<Recipe>& all() const { return recipes_; }
    size_t size() const { return recipes_.size(); }

    // Find by recipe key. Returns nullptr if not found.
    const Recipe* find(const std::string& recipe_key) const;

    // Find all recipes that run on a given facility type.
    std::vector<const Recipe*> recipes_for_facility_type(
        const std::string& facility_type_key) const;

    // Find all recipes that produce a given good (primary or byproduct).
    std::vector<const Recipe*> recipes_by_output(const std::string& good_id) const;

    // Find all recipes available at a given era.
    std::vector<const Recipe*> recipes_available_at(uint8_t era) const;

   private:
    std::vector<Recipe> recipes_;
    std::unordered_map<std::string, size_t> key_index_;
    std::unordered_map<std::string, std::vector<size_t>> facility_type_index_;
    std::unordered_map<std::string, std::vector<size_t>> output_index_;
};

}  // namespace econlife
