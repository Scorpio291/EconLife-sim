#include "scene_card_catalog.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace econlife {

namespace {

// Split a CSV line on commas. The card copy is authored without embedded
// commas for exactly this reason — a full CSV parser is not worth carrying for
// one content file, and a sentence that needs a comma can use a semicolon or a
// dash, which is also better prose.
std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> out;
    std::string field;
    std::istringstream in(line);
    while (std::getline(in, field, ','))
        out.push_back(field);
    return out;
}

CardClass parse_class(const std::string& s) {
    if (s == "mandatory")
        return CardClass::mandatory;
    if (s == "timed_optional")
        return CardClass::timed_optional;
    return CardClass::ambient;
}

SceneCardType parse_type(const std::string& s) {
    if (s == "meeting")
        return SceneCardType::meeting;
    if (s == "call")
        return SceneCardType::call;
    if (s == "personal_event")
        return SceneCardType::personal_event;
    return SceneCardType::news_notification;
}

// Only the settings the authored cards use. An unknown name falls back to a
// phone call, which is the least presumptuous setting there is.
SceneSetting parse_setting(const std::string& s) {
    if (s == "boardroom")
        return SceneSetting::boardroom;
    if (s == "private_office")
        return SceneSetting::private_office;
    if (s == "open_plan_office")
        return SceneSetting::open_plan_office;
    if (s == "factory_floor")
        return SceneSetting::factory_floor;
    if (s == "warehouse")
        return SceneSetting::warehouse;
    if (s == "construction_site")
        return SceneSetting::construction_site;
    if (s == "restaurant")
        return SceneSetting::restaurant;
    if (s == "cafe")
        return SceneSetting::cafe;
    if (s == "government_office")
        return SceneSetting::government_office;
    if (s == "courthouse")
        return SceneSetting::courthouse;
    if (s == "street_corner")
        return SceneSetting::street_corner;
    if (s == "home_dining")
        return SceneSetting::home_dining;
    if (s == "home_office")
        return SceneSetting::home_office;
    if (s == "moving_vehicle")
        return SceneSetting::moving_vehicle;
    if (s == "video_call")
        return SceneSetting::video_call;
    return SceneSetting::phone_call;
}

uint32_t to_u32(const std::string& s) {
    if (s.empty())
        return 0;
    try {
        return static_cast<uint32_t>(std::stoul(s));
    } catch (...) {
        return 0;
    }
}

}  // namespace

std::size_t SceneCardCatalog::load_from_file(const std::string& path) {
    std::ifstream in(path);
    if (!in)
        return 0;

    std::string line;
    if (!std::getline(in, line))
        return 0;  // header

    std::size_t loaded = 0;
    while (std::getline(in, line)) {
        if (line.empty())
            continue;
        const std::vector<std::string> f = split_csv(line);
        if (f.size() < 15)
            continue;  // malformed row: skip rather than guess

        SceneCardTemplate t{};
        t.card_key = f[0];
        if (t.card_key.empty())
            continue;
        t.card_class = parse_class(f[1]);
        t.type = parse_type(f[2]);
        t.setting = parse_setting(f[3]);
        t.dialogue = f[4];

        for (int c = 0; c < 3; ++c) {
            const std::size_t base = 5 + static_cast<std::size_t>(c) * 3;
            const uint32_t id = to_u32(f[base]);
            if (id == 0 || f[base + 1].empty())
                continue;
            t.choices.push_back(PlayerChoice{id, f[base + 1], f[base + 2], 0});
        }
        t.default_choice_id = to_u32(f[14]);

        // A card the player cannot answer wedges the queue, and scene_cards
        // drops it on admission. Refusing it here means the content error is
        // a missing card rather than a silent one.
        if (t.choices.empty())
            continue;

        templates_[t.card_key] = std::move(t);
        ++loaded;
    }
    return loaded;
}

std::size_t SceneCardCatalog::load_from_directory(const std::string& directory) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (directory.empty() || !fs::is_directory(directory, ec))
        return 0;

    // Sorted, so two files defining the same key resolve the same way on every
    // machine.
    std::vector<std::string> files;
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".csv")
            files.push_back(entry.path().string());
    }
    std::sort(files.begin(), files.end());

    std::size_t loaded = 0;
    for (const auto& file : files)
        loaded += load_from_file(file);
    return loaded;
}

const SceneCardTemplate* SceneCardCatalog::find(const std::string& card_key) const {
    const auto it = templates_.find(card_key);
    return (it == templates_.end()) ? nullptr : &it->second;
}

std::string SceneCardCatalog::inject(
    const std::string& text, const std::vector<std::pair<std::string, std::string>>& params) {
    std::string out = text;
    for (const auto& [name, value] : params) {
        const std::string needle = "{" + name + "}";
        std::size_t pos = out.find(needle);
        while (pos != std::string::npos) {
            out.replace(pos, needle.size(), value);
            pos = out.find(needle, pos + value.size());
        }
    }
    return out;
}

}  // namespace econlife
