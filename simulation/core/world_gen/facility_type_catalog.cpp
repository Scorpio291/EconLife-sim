// FacilityTypeCatalog — CSV parser for facility type definitions.
// See facility_type_catalog.h for class documentation.

#include "core/world_gen/facility_type_catalog.h"

#include <fstream>
#include <sstream>

namespace econlife {

// ---------------------------------------------------------------------------
// CSV parsing helpers
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
// FacilityTypeCatalog — load from CSV
// ---------------------------------------------------------------------------
// Expected columns (11):
//  0: facility_type_key
//  1: display_name
//  2: category
//  3: base_construction_cost
//  4: base_operating_cost
//  5: max_workers
//  6: signal_weight_noise
//  7: signal_weight_waste
//  8: signal_weight_traffic
//  9: signal_weight_pollution
// 10: signal_weight_odor

bool FacilityTypeCatalog::load_from_csv(const std::string& filepath) {
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
        if (fields.size() < 11)
            continue;

        FacilityType ft{};
        ft.key = fields[0];
        ft.display_name = fields[1];
        ft.category = fields[2];
        ft.base_construction_cost = parse_float(fields[3]);
        ft.base_operating_cost = parse_float(fields[4]);
        ft.max_workers = parse_uint(fields[5]);
        ft.signal_weight_noise = parse_float(fields[6]);
        ft.signal_weight_waste = parse_float(fields[7]);
        ft.signal_weight_traffic = parse_float(fields[8]);
        ft.signal_weight_pollution = parse_float(fields[9]);
        ft.signal_weight_odor = parse_float(fields[10]);

        size_t idx = types_.size();
        key_index_[ft.key] = idx;
        types_.push_back(std::move(ft));
    }

    return true;
}

// ---------------------------------------------------------------------------
// FacilityTypeCatalog — lookups
// ---------------------------------------------------------------------------

const FacilityType* FacilityTypeCatalog::find(const std::string& key) const {
    auto it = key_index_.find(key);
    if (it != key_index_.end()) {
        return &types_[it->second];
    }
    return nullptr;
}

std::vector<const FacilityType*> FacilityTypeCatalog::by_category(
    const std::string& category) const {
    std::vector<const FacilityType*> result;
    for (const auto& ft : types_) {
        if (ft.category == category) {
            result.push_back(&ft);
        }
    }
    return result;
}

}  // namespace econlife
