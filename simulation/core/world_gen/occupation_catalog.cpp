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
        // occupation_key,display_name,layer,min_surplus[,knowledge_output][,min_era]
        if (f.size() < 4)
            continue;
        OccupationDefinition o{};
        o.index = next_index++;
        o.key = f[0];
        o.display_name = f[1];
        o.layer = parse_u8(f[2], 1);
        o.min_surplus = parse_f(f[3], 1.0f);
        o.knowledge_output = f.size() > 4 ? parse_f(f[4], 0.0f) : 0.0f;  // optional column
        o.min_era = f.size() > 5 ? parse_u8(f[5], 1) : 1;               // optional column
        if (o.min_era == 0)
            o.min_era = 1;
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
        float knowledge_output;
        uint8_t min_era;
    };
    static const Row rows[] = {
        {"forager", "Forager", 1, 1.0f, 0.0f, 1},
        {"hunter", "Hunter", 1, 1.0f, 0.0f, 1},
        {"fisher", "Fisher", 1, 1.0f, 0.0f, 1},
        {"farmer", "Subsistence Farmer", 1, 1.0f, 0.0f, 1},
        {"herder", "Herder", 1, 1.0f, 0.0f, 1},
        // Knowledge-keepers lead the layer-2 list so the FIRST surplus-funded
        // specialists are always them — otherwise a thin surplus margin (few
        // specialists per province) never reaches them and the knowledge engine
        // stalls. Their min_surplus sits below the commons equilibrium so they stay
        // funded; era-gating (not surplus) is what staggers their arrival: elder
        // (oral tradition) at the dawn, scribe with writing (Bronze Age), scholar with
        // formal scholarship (Classical) — each a stronger knowledge source.
        {"elder", "Elder", 2, 0.0f, 0.2f, 1},
        {"scribe", "Scribe", 2, 0.0f, 0.6f, 2},
        {"scholar", "Scholar", 2, 0.0f, 1.0f, 4},
        {"artisan", "Artisan", 2, 1.10f, 0.0f, 1},
        {"builder", "Builder", 2, 1.20f, 0.0f, 1},
        {"healer", "Healer", 2, 1.20f, 0.0f, 1},
        {"trader", "Trader", 2, 1.15f, 0.0f, 1},
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
        o.knowledge_output = r.knowledge_output;
        o.min_era = r.min_era;
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

std::vector<const OccupationDefinition*> OccupationCatalog::in_layer_for_era(uint8_t layer,
                                                                            uint8_t era) const {
    std::vector<const OccupationDefinition*> out;
    for (const auto& o : occupations_)
        if (o.layer == layer && o.min_era <= era)
            out.push_back(&o);
    return out;
}

}  // namespace econlife
