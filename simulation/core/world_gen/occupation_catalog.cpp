#include "core/world_gen/occupation_catalog.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace econlife {

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string f;
    while (std::getline(ss, f, ','))
        fields.push_back(trim(f));
    return fields;
}

uint8_t parse_u8(const std::string& s, uint8_t fallback = 0) {
    if (s.empty())
        return fallback;
    try {
        return static_cast<uint8_t>(std::stoul(s));
    } catch (...) {
        return fallback;
    }
}

float parse_f(const std::string& s, float fallback = 0.0f) {
    if (s.empty())
        return fallback;
    try {
        return std::stof(s);
    } catch (...) {
        return fallback;
    }
}

}  // namespace

bool OccupationCatalog::load_from_directory(const std::string& dir) {
    namespace fs = std::filesystem;
    fs::path p = fs::path(dir) / "occupations.csv";
    if (!fs::exists(p))
        return false;
    return load_csv(p.string());
}

bool OccupationCatalog::load_csv(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open())
        return false;

    std::vector<OccupationDefinition> loaded;
    std::string line;
    bool header_skipped = false;
    uint16_t next_index = 1;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        if (!header_skipped) {
            header_skipped = true;
            continue;
        }
        auto f = split_csv(line);
        // occupation_key,display_name,layer,min_surplus
        if (f.size() < 4)
            continue;
        OccupationDefinition o{};
        o.index = next_index++;
        o.key = f[0];
        o.display_name = f[1];
        o.layer = parse_u8(f[2], 1);
        o.min_surplus = parse_f(f[3], 1.0f);
        loaded.push_back(std::move(o));
    }
    if (loaded.empty())
        return false;
    occupations_ = std::move(loaded);
    return true;
}

void OccupationCatalog::load_builtin_default() {
    struct Row {
        const char* key;
        const char* name;
        uint8_t layer;
        float min_surplus;
    };
    static const Row rows[] = {
        {"forager", "Forager", 1, 1.0f},          {"hunter", "Hunter", 1, 1.0f},
        {"fisher", "Fisher", 1, 1.0f},            {"farmer", "Subsistence Farmer", 1, 1.0f},
        {"herder", "Herder", 1, 1.0f},            {"artisan", "Artisan", 2, 1.10f},
        {"builder", "Builder", 2, 1.20f},         {"healer", "Healer", 2, 1.20f},
        {"trader", "Trader", 2, 1.15f},           {"elder", "Elder", 2, 1.10f},
    };
    occupations_.clear();
    uint16_t idx = 1;
    for (const auto& r : rows) {
        OccupationDefinition o{};
        o.index = idx++;
        o.key = r.key;
        o.display_name = r.name;
        o.layer = r.layer;
        o.min_surplus = r.min_surplus;
        occupations_.push_back(std::move(o));
    }
}

const OccupationDefinition* OccupationCatalog::by_index(uint16_t index) const {
    for (const auto& o : occupations_)
        if (o.index == index)
            return &o;
    return nullptr;
}

const OccupationDefinition* OccupationCatalog::find(const std::string& key) const {
    for (const auto& o : occupations_)
        if (o.key == key)
            return &o;
    return nullptr;
}

std::vector<const OccupationDefinition*> OccupationCatalog::in_layer(uint8_t layer) const {
    std::vector<const OccupationDefinition*> out;
    for (const auto& o : occupations_)
        if (o.layer == layer)
            out.push_back(&o);
    return out;
}

}  // namespace econlife
