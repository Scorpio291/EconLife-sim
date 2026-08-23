// TechnologyCatalog — CSV loading for technology nodes and maturation ceilings.

#include "core/world_gen/technology_catalog.h"
#include <functional>

#include <algorithm>
#include <cmath>
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
        // Optional trailing world-economy effect multipliers (default 1.0). Guard
        // > 0 so an empty/blank column keeps the neutral default rather than zeroing.
        auto parse_mult = [](const std::string& s, float fallback) {
            float v = std::strtof(s.c_str(), nullptr);
            return v > 0.0f ? v : fallback;
        };
        if (fields.size() > 12)
            node.knowledge_mult = parse_mult(fields[12], 1.0f);
        if (fields.size() > 13)
            node.food_mult = parse_mult(fields[13], 1.0f);
        if (fields.size() > 14)
            node.mortality_mult = parse_mult(fields[14], 1.0f);
        // Main path or side path (optional trailing column). Absent => side, so a tree
        // that predates the column cannot accidentally gate an era on every node in it.
        if (fields.size() > 15) {
            node.main_path = (fields[15] == "main" || fields[15] == "1");
            node.hub = (fields[15] == "hub");
        }
        if (fields.size() > 16)
            node.spoke = fields[16];

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
        // node_key + at least one era column. The number of era columns is
        // data-driven (eras.csv); read whatever the row provides, up to capacity.
        if (fields.size() < 2)
            continue;

        MaturationCeilingEntry entry{};  // zero-init: unspecified eras default to 0.0
        entry.node_key = fields[0];
        size_t cols = 0;
        for (size_t i = 0; i < MAX_ERA_CAPACITY && i + 1 < fields.size(); ++i) {
            entry.era_ceilings[i] = std::strtof(fields[i + 1].c_str(), nullptr);
            cols = i + 1;
        }
        if (cols > era_column_count_)
            era_column_count_ = static_cast<uint8_t>(cols);

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
    if (era < 1 || era > era_column_count_)
        return -1.0f;  // era beyond the defined ceiling table = not researchable
    return it->second.era_ceilings[era - 1];
}

float TechnologyCatalog::content_weight(uint8_t era_available, float difficulty,
                                        const TechnologyAdoptionConfig& cfg) {
    // How much learning this technique represents, relative to the simplest thing in the
    // tree. A node authored as costless within a later era still stands for that era's
    // accumulated learning — the modern baseline nodes carry difficulty 0 because they
    // need no RESEARCH at a 2000 CE start, not because they are easy — so the era floors
    // the difficulty.
    const float d = std::max(difficulty, static_cast<float>(std::max<uint8_t>(1, era_available)));
    const float span = std::max(0.0f, d - cfg.difficulty_free_below);
    return static_cast<float>(
        std::exp(static_cast<double>(std::max(0.0f, cfg.difficulty_knowledge_exponent)) *
                 static_cast<double>(span)));
}

std::vector<float> TechnologyCatalog::derive_era_thresholds(
    uint8_t max_era, const TechnologyAdoptionConfig& cfg) const {
    // THE KNOWLEDGE AXIS, DEFINED BY CONTENT. A society leaves an era when it has
    // accumulated as much learning as the techniques up to and including that era
    // represent — so the ladder is the running total of the tree's own weights, and it
    // is a pure function of the data. Mod the tree and the ladder follows.
    //
    // These used to be seven numbers FITTED to make earthlike hit seven historical dates.
    // That made the one thing the grounding doctrine allows as a pure pacing dial into
    // the definition of the knowledge unit itself, so the unit floated with the fit and
    // every constant expressed in it floated too — and there was nothing for a technique's
    // cost to be measured against. Now the ladder is content and the single fitted dial
    // is the RATE knowledge accumulates at (KnowledgeConfig::production_scalar), which is
    // a rate and can be fitted without deciding anything.
    std::vector<float> out;
    double cum = 0.0;
    for (uint8_t era = 1; era <= max_era; ++era) {
        for (const auto& n : nodes_)
            if (n.era_available == era)
                cum += static_cast<double>(content_weight(n.era_available, n.difficulty, cfg));
        out.push_back(static_cast<float>(cum));
    }
    return out;
}

float TechnologyCatalog::knowledge_required(uint8_t era_available, float difficulty,
                                            const EraCatalog& eras) {
    // WHAT A TECHNIQUE COSTS TO TAKE UP: the learning its era represents. A society that
    // has just entered an era is half-way into that era's techniques and works through
    // them as it accumulates the next era's worth — which is what an era boundary means
    // once it is a label rather than a gate.
    //
    // Anchored on the era ladder, which is CONTENT (the running total of the tree's own
    // weights — see derive_era_thresholds), not a fitted dial. That distinction is the
    // whole point: anchoring on a ladder that was fitted to historical dates made the
    // pacing dial load-bearing and the calibration stopped converging.
    const auto era = static_cast<uint8_t>(std::max<uint8_t>(1, era_available));
    auto threshold_of = [&eras](uint8_t e) -> float {
        if (e == 0)
            return 0.0f;  // nothing is needed to be where a world begins
        const EraDefinition* def = eras.by_index(e);
        return def ? std::max(0.0f, def->knowledge_to_advance) : 0.0f;
    };
    const float lo = threshold_of(static_cast<uint8_t>(era - 1));
    const float hi = std::max(lo, threshold_of(era));
    // Difficulty beyond the era number orders the techniques WITHIN it.
    const float within = std::clamp(difficulty - static_cast<float>(era), 0.0f, 1.0f);
    return lo + (hi - lo) * within;
}

bool TechnologyCatalog::has_main_path(uint8_t era) const {
    for (const auto& n : nodes_)
        if (n.era_available == era && n.main_path && !n.hub)
            return true;
    return false;
}

std::vector<std::string> TechnologyCatalog::spokes_in(uint8_t era) const {
    std::vector<std::string> out;
    for (const auto& n : nodes_) {
        if (n.era_available != era || !n.main_path || n.hub || n.spoke.empty())
            continue;
        if (std::find(out.begin(), out.end(), n.spoke) == out.end())
            out.push_back(n.spoke);
    }
    std::sort(out.begin(), out.end());  // fixed order: this feeds a count, not a display
    return out;
}

uint32_t TechnologyCatalog::spokes_worked(uint8_t era, float knowledge, float capital_per_head,
                                          const EraCatalog& eras,
                                          const TechnologyAdoptionConfig& cfg, float share) const {
    uint32_t worked = 0;
    for (const auto& spoke : spokes_in(era))
        if (main_path_progress(era, knowledge, capital_per_head, eras, cfg, spoke) >= share)
            ++worked;
    return worked;
}

float TechnologyCatalog::main_path_progress(uint8_t era, float knowledge,
                                            float capital_per_head, const EraCatalog& eras,
                                            const TechnologyAdoptionConfig& cfg,
                                            const std::string& spoke) const {
    const double K = static_cast<double>(std::max(0.0f, knowledge));
    const double c = static_cast<double>(std::max(0.0f, capital_per_head));
    double sum = 0.0;
    uint32_t count = 0;
    for (const auto& n : nodes_) {
        if (n.era_available != era || !n.main_path || n.hub)
            continue;
        if (!spoke.empty() && n.spoke != spoke)
            continue;
        const double k_req = static_cast<double>(knowledge_required(n.era_available, n.difficulty,
                                                                    eras));
        const double knows = k_req > 0.0 ? K / (K + k_req) : 1.0;
        const double c_req = static_cast<double>(std::max(0.0f, cfg.capital_per_difficulty)) *
                             static_cast<double>(std::max(0.0f, n.difficulty -
                                                                    cfg.difficulty_free_below));
        const double has = c_req > 0.0 ? c / (c + c_req) : 1.0;
        sum += knows * has;
        ++count;
    }
    if (count == 0)
        return 1.0f;  // a spoke this era authors nothing for does not hold anybody back
    return static_cast<float>(sum / static_cast<double>(count));
}

EraTechEffects TechnologyCatalog::effects_for(float knowledge, float capital_per_head,
                                              const EraCatalog& eras,
                                              const TechnologyAdoptionConfig& cfg) const {
    const double K = static_cast<double>(std::max(0.0f, knowledge));
    const double c = static_cast<double>(std::max(0.0f, capital_per_head));

    // Penetration of every node, in the order they are declared. Prerequisites are
    // bounded by their own penetration, so a chain cannot outrun its weakest link (no
    // vaccination in a society that has not got germ theory). Nodes are resolved by
    // recursive lookup with memoisation; the tree is a DAG in the data and a node already
    // in progress resolves to 0 rather than recursing forever, so a malformed cycle in a
    // mod degrades instead of hanging.
    std::vector<float> pen(nodes_.size(), -1.0f);
    std::vector<bool> resolving(nodes_.size(), false);

    std::function<float(size_t)> penetration_of = [&](size_t i) -> float {
        if (pen[i] >= 0.0f)
            return pen[i];
        if (resolving[i])
            return 0.0f;  // cycle guard (malformed data), not a design bound
        resolving[i] = true;

        const TechnologyNode& n = nodes_[i];
        // KNOWING IT. Jones again: a harder idea costs a society more to hold. A
        // requirement of zero — the starting package of the era a world begins in — is
        // known outright rather than dividing by nothing.
        const double k_req =
            static_cast<double>(knowledge_required(n.era_available, n.difficulty, eras));
        const double knows = k_req > 0.0 ? K / (K + k_req) : 1.0;
        // HAVING THE MEANS. A plough needs a plough; a sewer needs a city's savings. The
        // requirement is measured from the SIMPLEST thing in the tree, because a digging
        // stick is not capital — it is a morning's work — while a sewer system is a
        // city's annual output and a vaccine programme is a laboratory, a supply chain
        // and a state. This is the R9 separation, and it is what lets two provinces that
        // know the same things do different ones.
        const double c_req = static_cast<double>(std::max(0.0f, cfg.capital_per_difficulty)) *
                             static_cast<double>(std::max(0.0f, n.difficulty - 1.0f));
        const double has = c_req > 0.0 ? c / (c + c_req) : 1.0;

        double p = knows * has;
        for (const auto& key : n.prerequisites) {
            auto it = node_index_.find(key);
            if (it == node_index_.end())
                continue;  // unknown prerequisite in the data: ignore, do not zero the node
            p = std::min(p, static_cast<double>(penetration_of(it->second)));
        }
        resolving[i] = false;
        pen[i] = static_cast<float>(p);
        return pen[i];
    };

    // TECHNIQUES OVERLAP. Each adopted node contributes its raw gain to a budget and the
    // effect saturates in that budget — see TechnologyAdoptionConfig. Multiplying them
    // instead compounded 9,557x of food technique out of the authored tree and put 239
    // million people on six provinces.
    double food_budget = 0.0, knowledge_budget = 0.0, mortality_budget = 0.0;
    for (size_t i = 0; i < nodes_.size(); ++i) {
        const float p = penetration_of(i);
        if (p <= 0.0f)
            continue;
        const TechnologyNode& n = nodes_[i];
        knowledge_budget += static_cast<double>(std::max(0.0f, n.knowledge_mult - 1.0f)) * p;
        food_budget += static_cast<double>(std::max(0.0f, n.food_mult - 1.0f)) * p;
        mortality_budget += static_cast<double>(std::max(0.0f, 1.0f - n.mortality_mult)) * p;
    }
    auto saturate_gain = [](double budget, float cap) {
        const double m = static_cast<double>(std::max(0.0f, cap));
        if (m <= 0.0)
            return 1.0f;
        return static_cast<float>(1.0 + m * (1.0 - std::exp(-budget / m)));
    };
    EraTechEffects e{};
    e.knowledge_mult = saturate_gain(knowledge_budget, cfg.knowledge_gain_max);
    e.food_mult = saturate_gain(food_budget, cfg.food_gain_max);
    {
        const double m = static_cast<double>(std::clamp(cfg.mortality_reduction_max, 0.0f, 0.999f));
        e.mortality_mult =
            m > 0.0 ? static_cast<float>(1.0 - m * (1.0 - std::exp(-mortality_budget / m))) : 1.0f;
    }
    auto physical = [](float v) { return (std::isfinite(v) && v >= 0.0f) ? v : 1.0f; };
    e.knowledge_mult = physical(e.knowledge_mult);
    e.food_mult = physical(e.food_mult);
    e.mortality_mult = physical(e.mortality_mult);
    return e;
}

EraTechEffects TechnologyCatalog::aggregate_effects(uint8_t era) const {
    EraTechEffects e{};
    for (const auto& n : nodes_) {
        if (n.era_available <= era) {
            e.knowledge_mult *= n.knowledge_mult;
            e.food_mult *= n.food_mult;
            e.mortality_mult *= n.mortality_mult;
        }
    }
    // Grounding doctrine (root CLAUDE.md): NO behavior-shaping caps. The tech tree
    // CONTENT is the ceiling — the aggregate is exactly the product of the techs a
    // society has actually earned, and a modded tree means what it says. (A bare 6.0
    // food cap once bit at era 2 and flattened the whole agricultural arc; the 40/200
    // caps that replaced it were still silent flatteners for larger trees.) Only
    // non-physicality guards remain: a multiplier cannot be negative or non-finite.
    auto physical = [](float v) { return (std::isfinite(v) && v >= 0.0f) ? v : 1.0f; };
    e.knowledge_mult = physical(e.knowledge_mult);
    e.food_mult = physical(e.food_mult);
    e.mortality_mult = physical(e.mortality_mult);
    return e;
}

}  // namespace econlife
