// RecipeCatalog — CSV parser for recipe definitions.
// See recipe_catalog.h for class documentation.

#include "core/world_gen/recipe_catalog.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/world_gen/goods_catalog.h"

namespace econlife {

// ---------------------------------------------------------------------------
// CSV parsing helpers (shared with goods_catalog.cpp)
// ---------------------------------------------------------------------------

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, ',')) {
        fields.push_back(trim(field));
    }
    return fields;
}

static float parse_float(const std::string& s, float fallback = 0.0f) {
    if (s.empty())
        return fallback;
    try {
        return std::stof(s);
    } catch (...) {
        return fallback;
    }
}

static uint32_t parse_uint(const std::string& s, uint32_t fallback = 0) {
    if (s.empty())
        return fallback;
    try {
        return static_cast<uint32_t>(std::stoul(s));
    } catch (...) {
        return fallback;
    }
}

// ---------------------------------------------------------------------------
// RecipeCatalog — load from directory
// ---------------------------------------------------------------------------

bool RecipeCatalog::load_from_directory(const std::string& recipes_dir) {
    namespace fs = std::filesystem;

    if (!fs::exists(recipes_dir) || !fs::is_directory(recipes_dir)) {
        return false;
    }

    std::vector<std::string> csv_files;
    for (const auto& entry : fs::directory_iterator(recipes_dir)) {
        if (entry.path().extension() == ".csv") {
            csv_files.push_back(entry.path().string());
        }
    }
    std::sort(csv_files.begin(), csv_files.end());

    if (csv_files.empty())
        return false;

    bool any_loaded = false;
    for (const auto& file : csv_files) {
        if (load_csv(file)) {
            any_loaded = true;
        }
    }
    return any_loaded;
}

// ---------------------------------------------------------------------------
// RecipeCatalog — load single CSV
// ---------------------------------------------------------------------------
// Expected columns (22):
//  0: recipe_key
//  1: facility_type_key
//  2: display_name
//  3-4: input_1_key, input_1_qty
//  5-6: input_2_key, input_2_qty
//  7-8: input_3_key, input_3_qty
//  9-10: input_4_key, input_4_qty
//  11-13: output_1_key, output_1_qty, output_1_is_byproduct
//  14-16: output_2_key, output_2_qty, output_2_is_byproduct
//  17: labor_per_tick
//  18: energy_per_tick
//  19: min_tech_tier
//  20: key_technology_node
//  21: era_available

bool RecipeCatalog::load_csv(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open())
        return false;

    std::string line;
    bool header_skipped = false;

    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty())
            continue;

        if (!header_skipped) {
            header_skipped = true;
            continue;
        }

        auto fields = split_csv_line(line);
        if (fields.size() < 22)
            continue;

        Recipe recipe{};
        recipe.id = fields[0];
        recipe.facility_type_key = fields[1];
        recipe.name = fields[2];

        // Parse up to 4 inputs.
        for (int i = 0; i < 4; ++i) {
            int base = 3 + i * 2;
            if (!fields[base].empty()) {
                RecipeInput input{};
                input.good_id = fields[base];
                input.quantity_per_tick = parse_float(fields[base + 1]);
                if (input.quantity_per_tick > 0.0f) {
                    recipe.inputs.push_back(std::move(input));
                }
            }
        }

        // Parse up to 2 outputs.
        for (int i = 0; i < 2; ++i) {
            int base = 11 + i * 3;
            if (!fields[base].empty()) {
                RecipeOutput output{};
                output.good_id = fields[base];
                output.quantity_per_tick = parse_float(fields[base + 1]);
                output.is_byproduct = (fields[base + 2] == "1");
                output.quality_base = 0.5f;  // default; recipe quality comes from tech tier
                if (output.quantity_per_tick > 0.0f) {
                    recipe.outputs.push_back(std::move(output));
                }
            }
        }

        recipe.labor_per_tick = parse_float(fields[17]);
        recipe.energy_per_tick = parse_float(fields[18]);
        recipe.min_tech_tier = parse_uint(fields[19]);
        recipe.key_technology_node = fields[20];
        recipe.is_technology_intensive = !recipe.key_technology_node.empty();
        recipe.era_available = static_cast<uint8_t>(parse_uint(fields[21], 1));

        // Derive base_cost_per_tick from energy cost (placeholder formula).
        recipe.base_cost_per_tick = recipe.energy_per_tick * 10.0f + recipe.labor_per_tick * 5.0f;

        // Build indexes before moving.
        size_t idx = recipes_.size();
        key_index_[recipe.id] = idx;
        facility_type_index_[recipe.facility_type_key].push_back(idx);
        for (const auto& output : recipe.outputs) {
            output_index_[output.good_id].push_back(idx);
        }

        recipes_.push_back(std::move(recipe));
    }

    return true;
}

// ---------------------------------------------------------------------------
// RecipeCatalog — lookups
// ---------------------------------------------------------------------------

const Recipe* RecipeCatalog::find(const std::string& recipe_key) const {
    auto it = key_index_.find(recipe_key);
    if (it != key_index_.end()) {
        return &recipes_[it->second];
    }
    return nullptr;
}

std::vector<const Recipe*> RecipeCatalog::recipes_for_facility_type(
    const std::string& facility_type_key) const {
    std::vector<const Recipe*> result;
    auto it = facility_type_index_.find(facility_type_key);
    if (it != facility_type_index_.end()) {
        result.reserve(it->second.size());
        for (size_t idx : it->second) {
            result.push_back(&recipes_[idx]);
        }
    }
    return result;
}

std::vector<const Recipe*> RecipeCatalog::recipes_by_output(const std::string& good_id) const {
    std::vector<const Recipe*> result;
    auto it = output_index_.find(good_id);
    if (it != output_index_.end()) {
        result.reserve(it->second.size());
        for (size_t idx : it->second) {
            result.push_back(&recipes_[idx]);
        }
    }
    return result;
}

std::vector<const Recipe*> RecipeCatalog::recipes_available_at(uint8_t era) const {
    std::vector<const Recipe*> result;
    for (const auto& r : recipes_) {
        if (r.era_available <= era) {
            result.push_back(&r);
        }
    }
    return result;
}

std::vector<std::string> RecipeCatalog::validate_against_goods(const GoodsCatalog& goods) const {
    std::vector<std::string> errors;
    for (const auto& recipe : recipes_) {
        for (const auto& input : recipe.inputs) {
            if (goods.find(input.good_id) == nullptr) {
                errors.push_back("recipe '" + recipe.id + "' references unknown input good_id '" +
                                 input.good_id + "'");
            }
        }
        for (const auto& output : recipe.outputs) {
            if (goods.find(output.good_id) == nullptr) {
                errors.push_back("recipe '" + recipe.id + "' references unknown output good_id '" +
                                 output.good_id + "'");
            }
        }
    }
    return errors;
}

}  // namespace econlife
