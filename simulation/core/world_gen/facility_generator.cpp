// FacilityGenerator — facility assignment and technology seeding.
// Split from WorldGenerator to keep world_gen focused on physical geography.

#include "core/world_gen/facility_generator.h"

#include <algorithm>
#include <string>
#include <vector>

namespace econlife {

// ===========================================================================
// Facility assignment — gives each business 1-3 facilities with recipes
// ===========================================================================

// Maps business sector to preferred facility type categories.
static std::vector<std::string> facility_categories_for_sector(BusinessSector sector) {
    switch (sector) {
        case BusinessSector::manufacturing:
            return {"manufacturing", "processing"};
        case BusinessSector::food_beverage:
            return {"manufacturing", "agriculture"};
        case BusinessSector::agriculture:
            return {"agriculture"};
        case BusinessSector::energy:
            return {"extraction", "processing"};
        case BusinessSector::technology:
            return {"manufacturing"};
        case BusinessSector::research:
            return {"manufacturing"};
        case BusinessSector::transport_logistics:
            return {"processing", "manufacturing"};
        case BusinessSector::criminal:
            return {"processing", "manufacturing", "agriculture"};
        default:
            return {"manufacturing", "processing"};
    }
}

void FacilityGenerator::create_facilities(WorldState& world, DeterministicRNG& rng,
                                          const RecipeCatalog& recipes,
                                          const FacilityTypeCatalog& facility_types,
                                          const WorldGeneratorConfig& config) {
    uint32_t facility_id_counter = 5000;

    for (auto& biz : world.npc_businesses) {
        // Determine how many facilities this business gets (1-3).
        uint32_t facility_count = 1 + rng.next_uint(3);

        // Get preferred categories for this business sector.
        auto preferred_cats = facility_categories_for_sector(biz.sector);

        // Collect candidate facility types from preferred categories.
        std::vector<const FacilityType*> candidate_types;
        for (const auto& cat : preferred_cats) {
            auto cat_types = facility_types.by_category(cat);
            for (const auto* ft : cat_types) {
                candidate_types.push_back(ft);
            }
        }

        // Fallback: all facility types if no match.
        if (candidate_types.empty()) {
            for (const auto& ft : facility_types.all()) {
                candidate_types.push_back(&ft);
            }
        }
        if (candidate_types.empty())
            continue;

        for (uint32_t f = 0; f < facility_count; ++f) {
            // Pick a facility type.
            uint32_t ft_idx = rng.next_uint(static_cast<uint32_t>(candidate_types.size()));
            const FacilityType* ft = candidate_types[ft_idx];

            // Pick a recipe that runs on this facility type.
            auto ft_recipes = recipes.recipes_for_facility_type(ft->key);

            // Filter by era.
            std::vector<const Recipe*> available;
            for (const auto* r : ft_recipes) {
                if (r->era_available <= config.starting_era) {
                    available.push_back(r);
                }
            }
            if (available.empty())
                continue;

            uint32_t r_idx = rng.next_uint(static_cast<uint32_t>(available.size()));
            const Recipe* recipe = available[r_idx];

            Facility facility{};
            facility.id = facility_id_counter++;
            facility.business_id = biz.id;
            facility.province_id = biz.province_id;
            facility.recipe_id = recipe->id;
            facility.tech_tier = recipe->min_tech_tier;
            facility.output_rate_modifier = 1.0f;
            facility.soil_health =
                (ft->category == "agriculture") ? 0.8f + rng.next_float() * 0.2f : 1.0f;
            facility.worker_count = std::min(
                ft->max_workers,
                static_cast<uint32_t>(3 + rng.next_uint(std::max(1u, ft->max_workers / 4))));
            facility.is_operational = true;

            world.facilities.push_back(std::move(facility));
        }
    }
}

// ===========================================================================
// Technology seeding — assign baseline tech holdings to businesses
// ===========================================================================

void FacilityGenerator::seed_technology(WorldState& world, DeterministicRNG& rng,
                                        const TechnologyCatalog& tech_catalog,
                                        const WorldGeneratorConfig& config) {
    // Initialize GlobalTechnologyState.
    world.technology.current_era = SimulationEra::era_1_turn_of_millennium;
    world.technology.era_started_tick = 0;
    world.technology.base_year = 2000;

    // Seed initial domain knowledge from config defaults.
    TechnologyConfig tech_config;
    for (uint8_t i = 0; i < RESEARCH_DOMAIN_COUNT; ++i) {
        world.technology.domain_knowledge[i] = tech_config.era1_domain_knowledge[i];
    }

    // Get baseline technology nodes (available at game start).
    auto baseline = tech_catalog.baseline_nodes();
    if (baseline.empty())
        return;

    // Sector-to-relevant-domain mapping for seeding.
    // Businesses get baseline tech holdings in their relevant domains.
    auto domains_for_sector = [](BusinessSector sector) -> std::vector<std::string> {
        switch (sector) {
            case BusinessSector::manufacturing:
                return {"mechanical_engineering", "materials_science"};
            case BusinessSector::food_beverage:
                return {"biotechnology", "chemical_synthesis"};
            case BusinessSector::agriculture:
                return {"biotechnology"};
            case BusinessSector::energy:
                return {"energy_systems"};
            case BusinessSector::technology:
                return {"semiconductor_physics", "software_systems"};
            case BusinessSector::research:
                return {"semiconductor_physics", "software_systems", "biotechnology"};
            case BusinessSector::transport_logistics:
                return {"mechanical_engineering"};
            case BusinessSector::criminal:
                return {"illicit_chemistry", "chemical_synthesis"};
            default:
                return {"materials_science"};
        }
    };

    for (auto& biz : world.npc_businesses) {
        auto relevant_domains = domains_for_sector(biz.sector);

        // Give each business holdings in baseline nodes matching their sector domains.
        for (const TechnologyNode* node : baseline) {
            bool relevant = false;
            for (const auto& domain : relevant_domains) {
                if (node->domain == domain) {
                    relevant = true;
                    break;
                }
            }
            if (!relevant)
                continue;

            TechHolding holding;
            holding.node_key = node->node_key;
            holding.holder_id = biz.id;
            holding.stage = TechStage::commercialized;  // baseline tech is fully commercialized
            holding.researched_tick = 0;
            holding.commercialized_tick = 0;
            holding.has_patent = false;
            holding.internal_use_only = false;

            // Maturation: baseline tech is mature (0.7-0.95 range, randomized).
            float ceiling = tech_catalog.ceiling_for(node->node_key, config.starting_era);
            if (ceiling < 0.0f)
                ceiling = 1.0f;  // fallback if no ceiling data
            holding.maturation_ceiling = ceiling;
            holding.maturation_level = std::min(ceiling, 0.7f + rng.next_float() * 0.25f);

            biz.actor_tech_state.holdings[node->node_key] = std::move(holding);
        }

        // Also give a few non-baseline Era 1 techs to a fraction of businesses
        // (simulating early R&D investment that existed before year 2000).
        auto era1_nodes = tech_catalog.nodes_available_at(1);
        for (const TechnologyNode* node : era1_nodes) {
            if (node->is_baseline)
                continue;

            // Check domain relevance.
            bool relevant = false;
            for (const auto& domain : relevant_domains) {
                if (node->domain == domain) {
                    relevant = true;
                    break;
                }
            }
            if (!relevant)
                continue;

            // ~20% chance of having started early research.
            if (rng.next_float() > 0.20f)
                continue;

            TechHolding holding;
            holding.node_key = node->node_key;
            holding.holder_id = biz.id;
            holding.stage = TechStage::researched;  // early research, not yet commercialized
            holding.researched_tick = 0;
            holding.commercialized_tick = 0;
            holding.has_patent = (node->patentable && rng.next_float() < 0.3f);
            holding.internal_use_only = false;

            float ceiling = tech_catalog.ceiling_for(node->node_key, config.starting_era);
            if (ceiling < 0.0f)
                ceiling = 0.3f;  // low ceiling for early-era tech
            holding.maturation_ceiling = ceiling;
            // Early research: low maturation (0.05-0.25).
            holding.maturation_level = std::min(ceiling, 0.05f + rng.next_float() * 0.20f);

            biz.actor_tech_state.holdings[node->node_key] = std::move(holding);
        }
    }
}

// ===========================================================================
// Stage 1 — Tectonic plate generation and context classification
// ===========================================================================

}  // namespace econlife
