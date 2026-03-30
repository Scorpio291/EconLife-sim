// TechnologyCatalog — CSV loading for technology nodes and maturation ceilings.

#include "core/world_gen/technology_catalog.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

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
    std::istringstream stream(line);
    std::string field;
    while (std::getline(stream, field, ',')) {
        fields.push_back(trim(field));
    }
    return fields;
}

static std::vector<std::string> split_semicolons(const std::string& s) {
    std::vector<std::string> result;
    if (s.empty())
        return result;
    std::istringstream stream(s);
    std::string token;
    while (std::getline(stream, token, ';')) {
        std::string trimmed = trim(token);
        if (!trimmed.empty()) {
            result.push_back(trimmed);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// TechnologyCatalog — node loading
// ---------------------------------------------------------------------------

bool TechnologyCatalog::load_nodes_csv(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open())
        return false;

    std::string line;
    bool header_skipped = false;

    while (std::getline(file, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#')
            continue;

        // Skip header row.
        if (!header_skipped) {
            if (trimmed.find("node_key") != std::string::npos) {
                header_skipped = true;
                continue;
            }
        }

        auto fields = split_csv_line(trimmed);
        if (fields.size() < 12)
            continue;

        TechnologyNode node;
        node.node_key = fields[0];
        node.domain = fields[1];
        node.display_name = fields[2];
        node.era_available = static_cast<uint8_t>(std::strtoul(fields[3].c_str(), nullptr, 10));
        node.difficulty = std::strtof(fields[4].c_str(), nullptr);
        node.patentable = (fields[5] == "1" || fields[5] == "true");
        node.prerequisites = split_semicolons(fields[6]);
        node.outcome_type = fields[7];
        node.key_technology_node = fields[8];
        node.unlocks_recipe = fields[9];
        node.unlocks_facility_type = fields[10];
        node.is_baseline = (fields[11] == "1" || fields[11] == "true");

        node_index_[node.node_key] = nodes_.size();
        nodes_.push_back(std::move(node));
    }

    return !nodes_.empty();
}

// ---------------------------------------------------------------------------
// TechnologyCatalog — ceiling loading
// ---------------------------------------------------------------------------

bool TechnologyCatalog::load_ceilings_csv(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open())
        return false;

    std::string line;
    bool header_skipped = false;

    while (std::getline(file, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#')
            continue;

        if (!header_skipped) {
            if (trimmed.find("node_key") != std::string::npos) {
                header_skipped = true;
                continue;
            }
        }

        auto fields = split_csv_line(trimmed);
        // node_key + 10 era columns = 11 minimum
        if (fields.size() < 11)
            continue;

        MaturationCeilingEntry entry;
        entry.node_key = fields[0];
        for (int i = 0; i < 10 && i + 1 < static_cast<int>(fields.size()); ++i) {
            entry.era_ceilings[i] = std::strtof(fields[i + 1].c_str(), nullptr);
        }

        ceilings_[entry.node_key] = entry;
    }

    return !ceilings_.empty();
}

// ---------------------------------------------------------------------------
// TechnologyCatalog — lookups
// ---------------------------------------------------------------------------

const TechnologyNode* TechnologyCatalog::find(const std::string& node_key) const {
    auto it = node_index_.find(node_key);
    if (it != node_index_.end()) {
        return &nodes_[it->second];
    }
    return nullptr;
}

const MaturationCeilingEntry* TechnologyCatalog::find_ceiling(const std::string& node_key) const {
    auto it = ceilings_.find(node_key);
    if (it != ceilings_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<const TechnologyNode*> TechnologyCatalog::nodes_available_at(uint8_t era) const {
    std::vector<const TechnologyNode*> result;
    for (const auto& node : nodes_) {
        if (node.era_available <= era) {
            result.push_back(&node);
        }
    }
    return result;
}

std::vector<const TechnologyNode*> TechnologyCatalog::baseline_nodes() const {
    std::vector<const TechnologyNode*> result;
    for (const auto& node : nodes_) {
        if (node.is_baseline) {
            result.push_back(&node);
        }
    }
    return result;
}

std::vector<const TechnologyNode*> TechnologyCatalog::nodes_in_domain(
    const std::string& domain) const {
    std::vector<const TechnologyNode*> result;
    for (const auto& node : nodes_) {
        if (node.domain == domain) {
            result.push_back(&node);
        }
    }
    return result;
}

float TechnologyCatalog::ceiling_for(const std::string& node_key, uint8_t era) const {
    auto it = ceilings_.find(node_key);
    if (it == ceilings_.end())
        return 1.0f;  // no ceiling data = no restriction
    if (era < 1 || era > MAX_ERA)
        return -1.0f;
    return it->second.era_ceilings[era - 1];
}

}  // namespace econlife
