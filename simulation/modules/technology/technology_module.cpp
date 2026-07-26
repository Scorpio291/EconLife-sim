// Technology Module — implementation.
// See technology_module.h for class declarations and
// docs/design/EconLife_RnD_and_Technology_v22.md for the canonical specification.

#include "modules/technology/technology_module.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"

namespace econlife {

// ===========================================================================
// TechnologyModule — initialization
// ===========================================================================

void TechnologyModule::init_from_world_state(const WorldState& /*state*/) {
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

float TechnologyModule::compute_calendar_year(uint32_t tick, int32_t base_year) const {
    return static_cast<float>(base_year) + static_cast<float>(tick) / 365.0f;
}

float TechnologyModule::compute_era_transition_score(const WorldState& state,
                                                     uint8_t target_era) const {
    float score = 0.0f;
    float calendar_year = compute_calendar_year(state.current_tick, state.technology.base_year);

    // Calendar-year gate: an era cannot open before the calendar year the era
    // catalog says it opens. DATA-DRIVEN — the threshold is the target era's
    // start_year from eras.csv, not a table here. A hardcoded copy of the
    // timeline silently rots whenever the era indices are renumbered (it did:
    // a stale table pinned era 9 to year 2100 and dead-ended the timeline at
    // era 10), so the catalog is the single source of truth.
    // Semantics: the catalog's start_year is the era's SCHEDULED date, so reaching
    // it is sufficient on its own (weight == the transition threshold). The
    // technology conditions below then let a precocious world pull an era in
    // early; they no longer have to make up a shortfall. The old 0.40-of-0.70
    // split could never advance the modern timeline at all, because the tech
    // maturation it depended on is seeded per-era and read 0.0 for every
    // modern node (see FacilityGenerator::seed_technology).
    const EraDefinition* target_def = state.era_catalog.by_index(target_era);
    if (target_def != nullptr && calendar_year >= static_cast<float>(target_def->start_year)) {
        score += config_.era_transition_threshold;
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
                    "materials_science",      "semiconductor_physics",   "chemical_synthesis",
                    "energy_systems",         "mechanical_engineering",  "software_systems",
                    "biotechnology",          "climate_and_environment", "information_security",
                    "social_media_platforms", "financial_instruments",   "illicit_chemistry",
                    "nuclear_engineering",    "advanced_transportation", "space_systems",
                    "cognitive_science",      "synthetic_biology",       "geoengineering",
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

    // Additional technology-driven scoring for the modern V1 era transitions.
    // These supplement calendar_year and are based on the spec's era trigger
    // table. Keyed by the target era's STRING key, not its numeric index: the
    // index moved when the pre-modern eras were added (these bonuses used to be
    // cases 2-5 and, left numeric, ended up scoring Neolithic->Bronze Age with
    // smartphone maturation). The key is stable across renumbering.
    const std::string target_key = target_def != nullptr ? target_def->key : std::string();
    auto max_maturation_of = [&state](const char* node_key) {
        float best = 0.0f;
        for (const auto& biz : state.npc_businesses)
            best = std::max(best, biz.actor_tech_state.maturation_of(node_key));
        return best;
    };

    if (target_key == "disruption") {
        // turn_of_millennium -> disruption: smartphone maturation, broadband, social media
        if (max_maturation_of("smartphone_os") > 0.5f)
            score += 0.20f;
        // Software domain knowledge as proxy for broadband/digital adoption.
        if (state.technology.domain_knowledge[5] > 0.40f)
            score += 0.15f;  // software_systems
    } else if (target_key == "acceleration") {
        // disruption -> acceleration: cloud computing, renewable cost parity, EV early adoption
        if (max_maturation_of("cloud_computing") > 0.3f)
            score += 0.15f;
        if (max_maturation_of("cost_competitive_solar") > 0.3f)
            score += 0.15f;
    } else if (target_key == "fracture") {
        // acceleration -> fracture: EV mainstream, renewable dominant, GenAI
        if (max_maturation_of("electric_vehicle") > 0.3f)
            score += 0.15f;
        if (max_maturation_of("generative_ai") > 0.2f)
            score += 0.15f;
    } else if (target_key == "transition") {
        // fracture -> transition: renewables parity, EV penetration, AI integration
        if (max_maturation_of("ai_integration") > 0.3f)
            score += 0.15f;
        if (state.technology.domain_knowledge[3] > 0.70f)
            score += 0.15f;  // energy_systems
    }

    return score;
}

void TechnologyModule::check_era_transition(const WorldState& state, DeltaBuffer& delta) {
    uint8_t current_era = state.technology.current_era;
    // The timeline length is data-driven: cap advancement at the catalog's last era.
    uint8_t max_era = state.era_catalog.max_era();
    if (max_era == 0 || current_era >= max_era)
        return;

    // Ownership split between the two advancement paths, decided by the data:
    // an era with knowledge_to_advance > 0 is KNOWLEDGE-gated and belongs to
    // knowledge_module (the pre-modern climb, where each world advances at the
    // pace its own food/knowledge economy earns — that is what makes a garden
    // world stall in the Bronze Age and a fertile one race ahead). Only eras
    // with no knowledge gate (the modern band, which has real historical dates)
    // advance on the calendar + technology score computed here. Without this
    // split a calendar-sufficient gate would drag every world through the
    // pre-modern eras on a fixed schedule regardless of what it had earned.
    const EraDefinition* current_def = state.era_catalog.by_index(current_era);
    if (current_def != nullptr && current_def->knowledge_to_advance > 0.0f)
        return;

    uint8_t target_era = current_era + 1;
    float score = compute_era_transition_score(state, target_era);

    if (score >= config_.era_transition_threshold) {
        TechnologyDelta td{};
        td.new_era = target_era;
        delta.technology_deltas.push_back(td);
    }
}

// ===========================================================================
// TechnologyModule — maturation advancement
// ===========================================================================

void TechnologyModule::advance_maturation(const WorldState& state, DeltaBuffer& delta) {
    // R&D committed per business so far in this pass. Projects are visited in the
    // fixed order of active_maturation_projects, and this map is only ever probed
    // by business id, so it does not affect determinism.
    std::map<uint32_t, float> committed_by_business;

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
        if (!biz)
            continue;

        // Find the tech holding.
        auto holding_it = biz->actor_tech_state.holdings.find(project.node_key);
        if (holding_it == biz->actor_tech_state.holdings.end())
            continue;

        const TechHolding& holding = holding_it->second;

        // Already at ceiling? Skip.
        if (holding.maturation_level >= holding.maturation_ceiling)
            continue;

        // Researcher quality: documented baseline competence (a per-actor researcher
        // skill model is future work, see TechnologyConfig).
        float researcher_quality_avg = config_.researcher_quality_default;

        // Facility quality: scales with the actor's best lab tier
        // (effective_tech_tier, which tracks the era) — a more advanced lab matures
        // technology faster.
        float facility_quality_modifier = std::clamp(
            config_.facility_quality_base +
                config_.facility_quality_per_tier * biz->actor_tech_state.effective_tech_tier,
            config_.facility_quality_min, config_.facility_quality_max);

        // Domain knowledge bonus.
        float domain_knowledge_bonus = 1.0f;
        // Look up domain from catalog.
        const TechnologyNode* node = catalog_.find(project.node_key);
        if (node) {
            static const char* domain_names[] = {
                "materials_science",      "semiconductor_physics",   "chemical_synthesis",
                "energy_systems",         "mechanical_engineering",  "software_systems",
                "biotechnology",          "climate_and_environment", "information_security",
                "social_media_platforms", "financial_instruments",   "illicit_chemistry",
                "nuclear_engineering",    "advanced_transportation", "space_systems",
                "cognitive_science",      "synthetic_biology",       "geoengineering",
                "quantum_systems",
            };
            for (uint8_t i = 0; i < RESEARCH_DOMAIN_COUNT; ++i) {
                if (node->domain == domain_names[i]) {
                    domain_knowledge_bonus = 1.0f + state.technology.domain_knowledge[i] *
                                                        config_.domain_knowledge_bonus_coeff;
                    break;
                }
            }
        }

        // Funding adequacy: R&D is paid from the actor's cash. The ideal spend scales
        // with the assigned research effort; a cash-poor actor funds only what it can
        // afford and matures proportionally slower. The cash spent (rd_spend) is
        // deducted below once progress is confirmed — no free R&D.
        // A firm's cash is ONE purse: each project must be funded from what is left
        // after the projects already funded in this pass, not from the same pre-tick
        // snapshot. WorldState is const mid-tick, so measuring affordability against
        // biz->cash alone let a firm with three projects charge each one its full
        // cash and go negative (apply_business_deltas sums the deltas and has no
        // zero floor) — R&D funded from money that never existed.
        float funding_adequacy = 1.0f;
        float rd_spend = 0.0f;
        const float ideal_funding =
            config_.rd_funding_per_researcher * static_cast<float>(project.researchers_assigned);
        if (ideal_funding > 0.0f) {
            const float already_committed = committed_by_business[project.business_id];
            const float cash_left = std::max(0.0f, biz->cash - already_committed);
            const float affordable = std::min(ideal_funding, cash_left);
            funding_adequacy = affordable / ideal_funding;
            rd_spend = affordable;
        }

        // Compute maturation progress.
        float maturation_progress = static_cast<float>(project.researchers_assigned) *
                                    researcher_quality_avg * facility_quality_modifier *
                                    domain_knowledge_bonus * funding_adequacy *
                                    config_.maturation_rate_coeff;

        float maturation_delta =
            maturation_progress / (config_.maturation_difficulty_per_level * 10.0f);

        // Clamp to ceiling.
        float new_level =
            std::min(holding.maturation_ceiling, holding.maturation_level + maturation_delta);

        if (new_level > holding.maturation_level) {
            TechnologyDelta td{};
            td.business_id = project.business_id;
            td.node_key = project.node_key;
            td.maturation_level_update = new_level;
            delta.technology_deltas.push_back(td);

            // Conservation: charge the R&D spend to the funding business. Progress
            // implies funding_adequacy > 0, i.e. the actor could afford rd_spend.
            if (rd_spend > 0.0f) {
                BusinessDelta bd{};
                bd.business_id = project.business_id;
                bd.cash_delta = -rd_spend;
                delta.business_deltas.push_back(bd);
                // Reserve it against this firm's purse for the rest of the pass.
                committed_by_business[project.business_id] += rd_spend;
            }
        }
    }
}

// ===========================================================================
// TechnologyModule — domain knowledge decay
// ===========================================================================

void TechnologyModule::decay_domain_knowledge(const WorldState& state, DeltaBuffer& delta) {
    // Domain knowledge decays slowly per tick (obsolescence).
    // V1: very small rate, primarily cosmetic in early eras.
    for (uint8_t i = 0; i < RESEARCH_DOMAIN_COUNT; ++i) {
        float current = state.technology.domain_knowledge[i];
        if (current > 0.0f) {
            float decay = -current * config_.knowledge_decay_rate;
            if (decay < -0.0001f) {  // Skip negligible decay.
                TechnologyDelta td{};
                td.domain_index = i;
                td.domain_knowledge_delta = decay;
                delta.technology_deltas.push_back(td);
            }
        }
    }
}

// ===========================================================================
// TechnologyModule — maturation ceiling updates
// ===========================================================================

void TechnologyModule::update_maturation_ceilings(const WorldState& /*state*/,
                                                  DeltaBuffer& /*delta*/) {
    // Maturation ceilings are era-gated. When the era changes, all actors'
    // TechHolding.maturation_ceiling values need updating.
    // For V1, this is handled at the WorldGenerator level (seeding correct
    // ceilings for era 1) and will be properly wired when TechnologyDelta
    // is added to DeltaBuffer.
    // The ceiling values are available via catalog_.ceiling_for(node_key, era).
}

}  // namespace econlife
