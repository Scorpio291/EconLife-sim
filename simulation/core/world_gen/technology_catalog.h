#pragma once

// TechnologyCatalog — loads technology nodes and maturation ceilings from CSV.
// Immutable after loading. Indexed by node_key for fast lookup.

#include <string>
#include <unordered_map>
#include <vector>

#include "core/config/package_config.h"
#include "core/world_gen/era_catalog.h"
#include "modules/technology/technology_types.h"

namespace econlife {

class TechnologyCatalog {
   public:
    // Load technology nodes from a CSV file.
    // Format: node_key,domain,display_name,era_available,difficulty,patentable,
    //         prerequisites,outcome_type,key_technology_node,unlocks_recipe,
    //         unlocks_facility_type,is_baseline
    //         [,knowledge_mult,food_mult,mortality_mult,path]
    // `path` is "main" or "side"; absent means side.
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

    // Aggregate the world-economy effects of every node available at `era`
    // (era_available <= era), as the product of each node's multipliers.
    //
    // KEPT FOR CONTENT QUERIES AND WORLD-GEN SEEDING ONLY. Do NOT use it to decide what
    // a society can DO — see effects_for() below and the note on it.
    EraTechEffects aggregate_effects(uint8_t era) const;

    // WHAT A PLACE CAN ACTUALLY DO, given what it knows and what it has built.
    //
    // This replaces era-number gating, which was the largest remaining rail in the model:
    // `aggregate_effects` switches on every node with `era_available <= era`, so a
    // society was handed the plough, the aqueduct, inoculation and germ theory the moment
    // an integer ticked over — regardless of what it knew, what it had built, or whether
    // it had ever researched anything. Measured, the era 1 -> 2 boundary raised the food
    // surplus from 1.58 to 3.95 in a single step with nothing in the world having
    // changed. It is also why no two provinces could ever differ in technique: the era is
    // global, so Britain and Qing China were the same society by construction.
    //
    // A technique needs BOTH: knowing it and having the means. Each is a saturating share
    // rather than a switch, because techniques SPREAD — a province half-way to the plough
    // has some ploughs. A node's effect is therefore
    //
    //     1 + (mult - 1) x penetration
    //
    // which works in both directions (food multipliers above 1, medicine below it) and is
    // exactly neutral at zero penetration.
    //
    //   knows   K / (K + knowledge_required(difficulty))    — Jones: harder ideas cost more
    //   has     c / (c + capital_required(difficulty))      — a plough needs a plough
    //   prereqs bounded by the least-penetrated prerequisite — no vaccines before germs
    //
    // The era number stays what it always should have been — a label on the content, not
    // a gate on the mechanism. Cost comes from the node's own difficulty.
    EraTechEffects effects_for(float knowledge, float capital_per_head, const EraCatalog& eras,
                               const TechnologyAdoptionConfig& cfg) const;

    // How much learning one technique represents, relative to the simplest in the tree.
    // Pure/static.
    static float content_weight(uint8_t era_available, float difficulty,
                                const TechnologyAdoptionConfig& cfg);

    // THE KNOWLEDGE AXIS, DEFINED BY CONTENT: the running total of the tree's weights up
    // to each era. A society leaves an era when it has accumulated as much learning as
    // the techniques up to it represent. Index e-1 holds the threshold to leave era e.
    // This is what eras.csv's knowledge_to_advance column must equal, and a test asserts
    // it — the column is authored so nothing derives at runtime, and the test is what
    // keeps it honest when the tree changes.
    std::vector<float> derive_era_thresholds(uint8_t max_era,
                                             const TechnologyAdoptionConfig& cfg) const;

    // Whether this era has a main path at all. An era without one is not advanced by this
    // rule — the modern band keeps its own calendar-and-score transition, because it has
    // real historical dates rather than a spine of techniques to work out.
    bool has_main_path(uint8_t era) const;

    // HOW FAR THROUGH AN ERA'S MAIN PATH a society has got, in [0,1] — the mean adoption
    // of the era's spine techniques. This is what advances an era: not a knowledge number
    // and not a calendar, but whether the society has actually worked out the things the
    // era is made of. Side paths are excluded on purpose; they are depth, and a society
    // may take as many or as few of them as it likes without being held back or hurried.
    //
    // Because adoption already carries BOTH knowing and having, this subsumes the two
    // separate era gates it replaces: knowing how to make bronze is not the Bronze Age,
    // having the smelters is, and a node needs both before it counts.
    float main_path_progress(uint8_t era, float knowledge, float capital_per_head,
                             const EraCatalog& eras, const TechnologyAdoptionConfig& cfg) const;

    // What a technique costs a society to take up: the learning its era represents, so a
    // society entering an era is half-way into that era's techniques. Anchored on the era
    // ladder, which is CONTENT rather than a fitted dial. Pure/static.
    static float knowledge_required(uint8_t era_available, float difficulty,
                                    const EraCatalog& eras);

   private:
    std::vector<TechnologyNode> nodes_;
    std::unordered_map<std::string, size_t> node_index_;  // node_key -> index in nodes_
    std::unordered_map<std::string, MaturationCeilingEntry> ceilings_;
    // Number of era columns present in the loaded ceilings data. Eras beyond this
    // are "not defined" (ceiling_for returns -1.0). Data-driven: widens with the CSV.
    uint8_t era_column_count_ = 0;
};

}  // namespace econlife
