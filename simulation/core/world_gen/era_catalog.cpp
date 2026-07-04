#include "core/world_gen/era_catalog.h"

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
    std::string field;
    while (std::getline(ss, field, ',')) {
        fields.push_back(trim(field));
    }
    // A trailing comma yields a final empty field that getline drops; the era CSV
    // has no trailing-empty columns, so this is fine.
    return fields;
}

int32_t parse_i32(const std::string& s, int32_t fallback = 0) {
    if (s.empty())
        return fallback;
    try {
        return static_cast<int32_t>(std::stol(s));
    } catch (...) {
        return fallback;
    }
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

bool parse_bool(const std::string& s) {
    return s == "1" || s == "true" || s == "TRUE";
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

bool EraCatalog::load_from_directory(const std::string& eras_dir) {
    namespace fs = std::filesystem;
    fs::path p = fs::path(eras_dir) / "eras.csv";
    if (!fs::exists(p))
        return false;
    return load_csv(p.string());
}

bool EraCatalog::load_csv(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open())
        return false;

    std::vector<EraDefinition> loaded;
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
        auto f = split_csv(line);
        // era_index,era_key,display_name,start_year,economic_regime,is_default_entry,
        //   v1_in_scope[,knowledge_to_advance]
        if (f.size() < 7)
            continue;
        EraDefinition e{};
        e.index = parse_u8(f[0]);
        e.key = f[1];
        e.display_name = f[2];
        e.start_year = parse_i32(f[3]);
        e.economic_regime = f[4];
        e.is_default_entry = parse_bool(f[5]);
        e.v1_in_scope = parse_bool(f[6]);
        e.knowledge_to_advance = f.size() > 7 ? parse_f(f[7], 0.0f) : 0.0f;  // optional column
        if (e.index == 0 || e.index > MAX_ERA_CAPACITY)
            continue;  // out-of-range index: skip rather than corrupt the timeline
        loaded.push_back(std::move(e));
    }
    if (loaded.empty())
        return false;
    std::sort(loaded.begin(), loaded.end(),
              [](const EraDefinition& a, const EraDefinition& b) { return a.index < b.index; });
    eras_ = std::move(loaded);
    return true;
}

void EraCatalog::load_builtin_default() {
    // Mirrors packages/base_game/eras/eras.csv. Keeps the engine runnable when no
    // package data directory is supplied (unit tests, fallback paths).
    struct Row {
        uint8_t idx;
        const char* key;
        const char* name;
        int32_t year;
        const char* regime;
        bool def;
        bool v1;
        float knowledge;
    };
    static const Row rows[] = {
        {1, "neolithic", "Neolithic", -10000, "subsistence", false, true, 3830.0f},
        {2, "bronze_age", "Bronze Age", -3300, "barter", false, true, 12740.0f},
        {3, "iron_age", "Iron Age", -1200, "coinage", false, true, 19650.0f},
        {4, "classical", "Classical", -550, "money", false, true, 49250.0f},
        {5, "medieval", "Medieval", 500, "feudal", false, true, 253000.0f},
        {6, "early_modern", "Early Modern", 1450, "mercantile", false, true, 892000.0f},
        {7, "industrial", "Industrial", 1750, "industrial", false, true, 2117000.0f},
        {8, "turn_of_millennium", "Turn of the Millennium", 2000, "modern", true, true, 0.0f},
        {9, "disruption", "Disruption", 2007, "modern", false, true, 0.0f},
        {10, "acceleration", "Acceleration", 2013, "modern", false, true, 0.0f},
        {11, "fracture", "Fracture", 2019, "modern", false, true, 0.0f},
        {12, "transition", "Transition", 2024, "modern", false, true, 0.0f},
        {13, "convergence", "Convergence", 2035, "near_future", false, false, 0.0f},
        {14, "reckoning", "Reckoning", 2050, "near_future", false, false, 0.0f},
        {15, "synthesis", "Synthesis", 2075, "near_future", false, false, 0.0f},
        {16, "expansion", "Expansion", 2100, "space_age", false, false, 0.0f},
        {17, "divergence", "Divergence", 2150, "space_age", false, false, 0.0f},
    };
    eras_.clear();
    for (const auto& r : rows) {
        EraDefinition e{};
        e.index = r.idx;
        e.key = r.key;
        e.display_name = r.name;
        e.start_year = r.year;
        e.economic_regime = r.regime;
        e.is_default_entry = r.def;
        e.v1_in_scope = r.v1;
        e.knowledge_to_advance = r.knowledge;
        eras_.push_back(std::move(e));
    }
}

uint8_t EraCatalog::max_era() const {
    uint8_t m = 0;
    for (const auto& e : eras_)
        m = std::max(m, e.index);
    return m;
}

uint8_t EraCatalog::v1_max_era() const {
    uint8_t m = 0;
    for (const auto& e : eras_)
        if (e.v1_in_scope)
            m = std::max(m, e.index);
    return m;
}

uint8_t EraCatalog::default_entry_index() const {
    for (const auto& e : eras_)
        if (e.is_default_entry)
            return e.index;
    return 1;  // sane fallback: the first/earliest era
}

const EraDefinition* EraCatalog::by_index(uint8_t index) const {
    for (const auto& e : eras_)
        if (e.index == index)
            return &e;
    return nullptr;
}

const EraDefinition* EraCatalog::find(const std::string& key) const {
    for (const auto& e : eras_)
        if (e.key == key)
            return &e;
    return nullptr;
}

}  // namespace econlife
