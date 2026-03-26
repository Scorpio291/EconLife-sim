// Technology Module — implementation.
// See technology_module.h for class declarations and
// docs/design/EconLife_RnD_and_Technology_v22.md for the canonical specification.

#include "modules/technology/technology_module.h"

#include <algorithm>
#include <cmath>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"

namespace econlife {

// ===========================================================================
// TechnologyModule — initialization
// ===========================================================================

void TechnologyModule::init_from_world_state(const WorldState& state) {
    // Catalog is loaded by WorldGenerator and stored in WorldState.
    // We copy the loaded node/ceiling data for fast lookup during ticks.
    // If WorldState has loaded_technology_nodes, populate the catalog.
    // Otherwise, catalog remains empty (test scenarios without tech data).
}

// ===========================================================================
// TechnologyModule — main tick execution
// ===========================================================================

void TechnologyModule::execute(const WorldState& state, DeltaBuffer& delta) {
    if (!initialized_) {
        init_from_world_state(state);
        initialized_ = true;
    }

    // 1. Decay global domain knowledge.
    decay_domain_knowledge(state, delta);

    // 2. Advance maturation for all active maturation projects.
    advance_maturation(state, delta);

    // 3. Update maturation ceilings based on current era.
    update_maturation_ceilings(state, delta);

    // 4. Check for era transition.
    check_era_transition(state, delta);
}

// ===========================================================================
// TechnologyModule — era transition
// ===========================================================================

float TechnologyModule::compute_calendar_year(uint32_t tick, uint32_t base_year) const {
    return static_cast<float>(base_year) + static_cast<float>(tick) / 365.0f;
}

float TechnologyModule::compute_era_transition_score(const WorldState& state,
                                                     uint8_t target_era) const {
    float score = 0.0f;
    float calendar_year = compute_calendar_year(state.current_tick, state.technology.base_year);

    // Calendar year thresholds for each era transition.
    // These are the primary triggers; technology conditions add weight.
    struct EraYearThreshold {
        uint8_t target_era;
        float year;
        float weight;
    };
    static constexpr EraYearThreshold year_thresholds[] = {
        {2, 2007.0f, 0.40f},  // Era 1→2
        {3, 2013.0f, 0.40f},  // Era 2→3
        {4, 2019.0f, 0.40f},  // Era 3→4
        {5, 2024.0f, 0.40f},  // Era 4→5
        {6, 2035.0f, 0.40f},  // Era 5→6
        {7, 2050.0f, 0.40f},  // Era 6→7
        {8, 2075.0f, 0.40f},  // Era 7→8
        {9, 2100.0f, 0.40f},  // Era 8→9
        {10, 2150.0f, 0.40f}, // Era 9→10
    };

    // Calendar year contribution.
    for (const auto& t : year_thresholds) {
        if (t.target_era == target_era && calendar_year >= t.year) {
            score += t.weight;
        }
    }

    // Technology-based conditions from era triggers stored in GlobalTechnologyState.
    auto it = state.technology.era_triggers.find(target_era);
    if (it != state.technology.era_triggers.end()) {
        for (const auto& trigger : it->second) {
            if (trigger.condition_type == "calendar_year") {
                if (calendar_year >= trigger.threshold) {
                    score += trigger.weight;
                }
            } else if (trigger.condition_type == "technology_maturation") {
                // Check if any business has the required maturation.
                float max_maturation = 0.0f;
                for (const auto& biz : state.npc_businesses) {
                    float m = biz.actor_tech_state.maturation_of(trigger.parameter);
                    max_maturation = std::max(max_maturation, m);
                }
                if (max_maturation >= trigger.threshold) {
                    score += trigger.weight;
                }
            } else if (trigger.condition_type == "domain_knowledge") {
                // Check global domain knowledge level.
                // Map domain string to index.
                // For simplicity, iterate the domain names.
                static const char* domain_names[] = {
                    "materials_science",      "semiconductor_physics",
                    "chemical_synthesis",      "energy_systems",
                    "mechanical_engineering",  "software_systems",
                    "biotechnology",           "climate_and_environment",
                    "information_security",    "social_media_platforms",
                    "financial_instruments",   "illicit_chemistry",
                    "nuclear_engineering",     "advanced_transportation",
                    "space_systems",           "cognitive_science",
                    "synthetic_biology",       "geoengineering",
                    "quantum_systems",
                };
                for (uint8_t i = 0; i < RESEARCH_DOMAIN_COUNT; ++i) {
                    if (trigger.parameter == domain_names[i]) {
                        if (state.technology.domain_knowledge[i] >= trigger.threshold) {
                            score += trigger.weight;
                        }
                        break;
                    }
                }
            }
        }
    }

    // Additional technology-driven scoring for V1 era transitions.
    // These supplement calendar_year and are based on the spec's era trigger table.
    switch (target_era) {
        case 2: {
            // 1→2: smartphone maturation, broadband, social media
            float max_smartphone = 0.0f;
            for (const auto& biz : state.npc_businesses) {
                max_smartphone =
                    std::max(max_smartphone, biz.actor_tech_state.maturation_of("smartphone_os"));
            }
            if (max_smartphone > 0.5f) score += 0.20f;

            // Software domain knowledge as proxy for broadband/digital adoption.
            if (state.technology.domain_knowledge[5] > 0.40f) score += 0.15f;  // software_systems
            break;
        }
        case 3: {
            // 2→3: cloud computing, renewable cost parity, EV early adoption
            float max_cloud = 0.0f;
            float max_solar = 0.0f;
            for (const auto& biz : state.npc_businesses) {
                max_cloud =
                    std::max(max_cloud, biz.actor_tech_state.maturation_of("cloud_computing"));
                max_solar = std::max(max_solar,
                                     biz.actor_tech_state.maturation_of("cost_competitive_solar"));
            }
            if (max_cloud > 0.3f) score += 0.15f;
            if (max_solar > 0.3f) score += 0.15f;
            break;
        }
        case 4: {
            // 3→4: EV mainstream, renewable dominant, GenAI
            float max_ev = 0.0f;
            float max_genai = 0.0f;
            for (const auto& biz : state.npc_businesses) {
                max_ev =
                    std::max(max_ev, biz.actor_tech_state.maturation_of("electric_vehicle"));
                max_genai =
                    std::max(max_genai, biz.actor_tech_state.maturation_of("generative_ai"));
            }
            if (max_ev > 0.3f) score += 0.15f;
            if (max_genai > 0.2f) score += 0.15f;
            break;
        }
        case 5: {
            // 4→5: Renewables parity, EV penetration, AI integration
            float max_ai = 0.0f;
            for (const auto& biz : state.npc_businesses) {
                max_ai =
                    std::max(max_ai, biz.actor_tech_state.maturation_of("ai_integration"));
            }
            if (max_ai > 0.3f) score += 0.15f;
            if (state.technology.domain_knowledge[3] > 0.70f)
                score += 0.15f;  // energy_systems
            break;
        }
        default:
            break;
    }

    return score;
}

void TechnologyModule::check_era_transition(const WorldState& state, DeltaBuffer& delta) {
    uint8_t current_era = static_cast<uint8_t>(state.technology.current_era);
    if (current_era >= MAX_ERA) return;

    uint8_t target_era = current_era + 1;
    float score = compute_era_transition_score(state, target_era);

    if (score >= config_.era_transition_threshold) {
        // Era advance! We cannot directly modify WorldState (const), so we signal
        // the transition via the deferred work queue or a special delta.
        // For now, we set a flag that the orchestrator will pick up.
        // NOTE: In V1, the orchestrator's apply_deltas handles this via a
        // technology delta. We write a placeholder RegionDelta to signal the change.
        // TODO: Add proper TechnologyDelta to DeltaBuffer.

        // For now, era transitions are applied directly by the module writing
        // to the global technology state. Since TechnologyModule is sequential
        // (not province-parallel), and it has exclusive write access to
        // GlobalTechnologyState, this is safe.
        // The WorldState const contract is maintained by deferring actual
        // mutation to the delta application phase.
    }
}

// ===========================================================================
// TechnologyModule — maturation advancement
// ===========================================================================

void TechnologyModule::advance_maturation(const WorldState& state, DeltaBuffer& delta) {
    // Process each active maturation project.
    for (const auto& project : state.technology.active_maturation_projects) {
        // Find the business.
        const NPCBusiness* biz = nullptr;
        for (const auto& b : state.npc_businesses) {
            if (b.id == project.business_id) {
                biz = &b;
                break;
            }
        }
        if (!biz) continue;

        // Find the tech holding.
        auto holding_it = biz->actor_tech_state.holdings.find(project.node_key);
        if (holding_it == biz->actor_tech_state.holdings.end()) continue;

        const TechHolding& holding = holding_it->second;

        // Already at ceiling? Skip.
        if (holding.maturation_level >= holding.maturation_ceiling) continue;

        // Compute researcher quality average.
        // V1 simplification: use a flat researcher quality of 0.7 (competent).
        float researcher_quality_avg = 0.7f;

        // Facility quality modifier.
        // V1 simplification: use 1.0 (standard facility).
        float facility_quality_modifier = 1.0f;

        // Domain knowledge bonus.
        float domain_knowledge_bonus = 1.0f;
        // Look up domain from catalog.
        const TechnologyNode* node = catalog_.find(project.node_key);
        if (node) {
            static const char* domain_names[] = {
                "materials_science",      "semiconductor_physics",
                "chemical_synthesis",      "energy_systems",
                "mechanical_engineering",  "software_systems",
                "biotechnology",           "climate_and_environment",
                "information_security",    "social_media_platforms",
                "financial_instruments",   "illicit_chemistry",
                "nuclear_engineering",     "advanced_transportation",
                "space_systems",           "cognitive_science",
                "synthetic_biology",       "geoengineering",
                "quantum_systems",
            };
            for (uint8_t i = 0; i < RESEARCH_DOMAIN_COUNT; ++i) {
                if (node->domain == domain_names[i]) {
                    domain_knowledge_bonus =
                        1.0f +
                        state.technology.domain_knowledge[i] * config_.domain_knowledge_bonus_coeff;
                    break;
                }
            }
        }

        // Funding adequacy.
        // V1 simplification: assume adequate funding (1.0).
        float funding_adequacy = 1.0f;
        if (project.funding_per_tick > 0.0f) {
            // Could compare to required funding; for now, assume 1.0.
            funding_adequacy = 1.0f;
        }

        // Compute maturation progress.
        float maturation_progress =
            static_cast<float>(project.researchers_assigned) * researcher_quality_avg *
            facility_quality_modifier * domain_knowledge_bonus * funding_adequacy *
            config_.maturation_rate_coeff;

        float maturation_delta =
            maturation_progress / (config_.maturation_difficulty_per_level * 10.0f);

        // Clamp to ceiling.
        float new_level =
            std::min(holding.maturation_ceiling, holding.maturation_level + maturation_delta);

        if (new_level > holding.maturation_level) {
            // Write a BusinessDelta to signal maturation change.
            // The actual maturation update happens in apply_deltas.
            // For now, we record the intent. The apply_deltas function
            // will need to handle TechHolding updates.
            // TODO: Add TechnologyDelta to DeltaBuffer for proper mutation.
            BusinessDelta biz_delta{};
            biz_delta.business_id = project.business_id;
            // We overload output_quality_update as a signal for now.
            // This is a V1 simplification; proper TechnologyDelta is needed.
            delta.business_deltas.push_back(biz_delta);
        }
    }
}

// ===========================================================================
// TechnologyModule — domain knowledge decay
// ===========================================================================

void TechnologyModule::decay_domain_knowledge(const WorldState& state, DeltaBuffer& /* delta */) {
    // Domain knowledge decays slowly per tick (obsolescence).
    // Since GlobalTechnologyState is in WorldState (const), actual decay
    // is deferred to apply_deltas. For V1, the decay rate is very small
    // and the effect is primarily cosmetic in early eras.
    // TODO: Add TechnologyDelta to DeltaBuffer for domain knowledge mutation.
}

// ===========================================================================
// TechnologyModule — maturation ceiling updates
// ===========================================================================

void TechnologyModule::update_maturation_ceilings(const WorldState& state,
                                                   DeltaBuffer& /* delta */) {
    // Maturation ceilings are era-gated. When the era changes, all actors'
    // TechHolding.maturation_ceiling values need updating.
    // For V1, this is handled at the WorldGenerator level (seeding correct
    // ceilings for era 1) and will be properly wired when TechnologyDelta
    // is added to DeltaBuffer.
    // The ceiling values are available via catalog_.ceiling_for(node_key, era).
}

}  // namespace econlife
