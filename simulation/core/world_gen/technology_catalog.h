#pragma once

// TechnologyCatalog — loads technology nodes and maturation ceilings from CSV.
// Immutable after loading. Indexed by node_key for fast lookup.

#include <string>
#include <unordered_map>
#include <vector>

#include "modules/technology/technology_types.h"

namespace econlife {

class TechnologyCatalog {
   public:
    // Load technology nodes from a CSV file.
    // Format: node_key,domain,display_name,era_available,difficulty,patentable,
    //         prerequisites,outcome_type,key_technology_node,unlocks_recipe,
    //         unlocks_facility_type,is_baseline
    // Lines starting with # are comments.
    bool load_nodes_csv(const std::string& filepath);

    // Load maturation ceiling data from CSV.
    // Format: node_key,era_1,era_2,...,era_10
    // -1.0 means "not researchable in this era."
    bool load_ceilings_csv(const std::string& filepath);

    // --- Lookup ---
    const TechnologyNode* find(const std::string& node_key) const;
    const MaturationCeilingEntry* find_ceiling(const std::string& node_key) const;

    // All nodes.
    const std::vector<TechnologyNode>& all() const { return nodes_; }

    // Nodes available at a given era (era_available <= era).
    std::vector<const TechnologyNode*> nodes_available_at(uint8_t era) const;

    // Baseline nodes (available at game start without research).
    std::vector<const TechnologyNode*> baseline_nodes() const;

    // Nodes in a given domain.
    std::vector<const TechnologyNode*> nodes_in_domain(const std::string& domain) const;

    // Get maturation ceiling for a node at a given era.
    // Returns -1.0 if the node is not researchable in that era.
    float ceiling_for(const std::string& node_key, uint8_t era) const;

   private:
    std::vector<TechnologyNode> nodes_;
    std::unordered_map<std::string, size_t> node_index_;  // node_key -> index in nodes_
    std::unordered_map<std::string, MaturationCeilingEntry> ceilings_;
};

}  // namespace econlife
