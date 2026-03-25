#pragma once

// FacilityTypeCatalog — loads facility type definitions from CSV.
// Each row defines a facility type with construction costs, worker caps,
// and signal weights for the evidence system.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "modules/production/production_types.h"

namespace econlife {

class FacilityTypeCatalog {
   public:
    // Load facility types from a single CSV file.
    bool load_from_csv(const std::string& filepath);

    // Access
    const std::vector<FacilityType>& all() const { return types_; }
    size_t size() const { return types_.size(); }

    // Find by key. Returns nullptr if not found.
    const FacilityType* find(const std::string& key) const;

    // Find all facility types in a given category.
    std::vector<const FacilityType*> by_category(const std::string& category) const;

   private:
    std::vector<FacilityType> types_;
    std::unordered_map<std::string, size_t> key_index_;
};

}  // namespace econlife
