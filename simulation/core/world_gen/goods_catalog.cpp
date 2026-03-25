// GoodsCatalog — CSV parser for goods definitions.
// See goods_catalog.h for class documentation.

#include "core/world_gen/goods_catalog.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace econlife {

// ---------------------------------------------------------------------------
// CSV parsing helpers
// ---------------------------------------------------------------------------

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
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

// ---------------------------------------------------------------------------
// GoodsCatalog — load from directory
// ---------------------------------------------------------------------------

bool GoodsCatalog::load_from_directory(const std::string& goods_dir) {
    namespace fs = std::filesystem;

    if (!fs::exists(goods_dir) || !fs::is_directory(goods_dir)) {
        return false;
    }

    // Collect and sort CSV files for deterministic load order.
    std::vector<std::string> csv_files;
    for (const auto& entry : fs::directory_iterator(goods_dir)) {
        if (entry.path().extension() == ".csv") {
            csv_files.push_back(entry.path().string());
        }
    }
    std::sort(csv_files.begin(), csv_files.end());

    if (csv_files.empty()) return false;

    bool any_loaded = false;
    for (const auto& file : csv_files) {
        if (load_csv(file)) {
            any_loaded = true;
        }
    }
    return any_loaded;
}

// ---------------------------------------------------------------------------
// GoodsCatalog — load single CSV
// ---------------------------------------------------------------------------

bool GoodsCatalog::load_csv(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return false;

    std::string line;
    bool header_skipped = false;

    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty()) continue;

        // Skip header row.
        if (!header_skipped) {
            header_skipped = true;
            continue;
        }

        auto fields = split_csv_line(line);
        if (fields.size() < 9) continue;

        GoodDefinition good{};
        good.numeric_id   = next_numeric_id_++;
        good.good_id      = fields[0];
        good.display_name = fields[1];

        // Parse tier (uint8_t)
        try { good.tier = static_cast<uint8_t>(std::stoi(fields[2])); }
        catch (...) { continue; }

        good.unit     = fields[3];
        good.category = fields[4];

        // Parse base_price (float)
        try { good.base_price = std::stof(fields[5]); }
        catch (...) { continue; }

        good.perishable    = (fields[6] == "true");
        good.illegal       = (fields[7] == "true");

        // Parse era_available (uint8_t)
        try { good.era_available = static_cast<uint8_t>(std::stoi(fields[8])); }
        catch (...) { good.era_available = 1; }

        goods_.push_back(std::move(good));
    }

    return true;
}

// ---------------------------------------------------------------------------
// GoodsCatalog — filtering
// ---------------------------------------------------------------------------

std::vector<const GoodDefinition*> GoodsCatalog::goods_available_at(
        uint8_t era, uint8_t max_tier) const {
    std::vector<const GoodDefinition*> result;
    for (const auto& g : goods_) {
        if (g.era_available <= era && g.tier <= max_tier) {
            result.push_back(&g);
        }
    }
    return result;
}

const GoodDefinition* GoodsCatalog::find(const std::string& good_id) const {
    for (const auto& g : goods_) {
        if (g.good_id == good_id) return &g;
    }
    return nullptr;
}

}  // namespace econlife
