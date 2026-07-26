#include "modules/knowledge/knowledge_module.h"

#include <algorithm>
#include <cmath>

#include "core/world_gen/era_catalog.h"
#include "core/world_gen/occupation_catalog.h"
#include "core/world_gen/world_class.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"

namespace econlife {

namespace {
}

bool KnowledgeModule::regime_active(std::string_view regime) const {
    return regime_in(cfg_.active_regimes, regime);
}

void KnowledgeModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Annual cadence (knowledge accrues slowly); skip the t=0 snapshot tick.
    if (state.current_tick == 0 || state.current_tick % kTicksPerYear != 0)
        return;

    // Pre-market only: in market eras the technology module owns advancement, and
    // there are no scholar livelihoods anyway. Leaves knowledge_level untouched.
    const EraDefinition* era = state.era_catalog.by_index(state.technology.current_era);
    if (era == nullptr || !regime_active(era->economic_regime))
        return;

    // Dedicated knowledge work: sum over scholar/scribe/elder livelihoods.
    double specialist_term = 0.0;
    for (const auto& npc : state.significant_npcs) {
        // Only the LIVING produce knowledge. significant_npcs is append-only —
        // the dead are retained for forensic and memory references and their
        // occupation is never cleared — so an unfiltered sum ratcheted upward
        // with the cumulative death toll and paced the era clock off corpses
        // rather than off the living scholar corps.
        if (npc.status != NPCStatus::active)
            continue;
        if (npc.occupation == 0)
            continue;
        const OccupationDefinition* o = state.occupation_catalog.by_index(npc.occupation);
        if (o && o->knowledge_output > 0.0f)
            specialist_term += static_cast<double>(o->knowledge_output);
    }
    specialist_term *= static_cast<double>(cfg_.production_scalar);

    // Adversity drives invention (Boserup intensification + the Deathworlders premise):
    // under pressure the WHOLE population innovates, not just a thin elite. Pressure
    // rises with the world's hazard (a hard world forges capability) and with food
    // scarcity (a population pressing on its supply intensifies — necessity is the
    // mother of invention). This scales with total population (more minds) and gives a
    // grounded escape from the Malthusian wall, and makes harsher worlds out-innovate
    // comfortable gardens.
    double total_population = 0.0;
    double weighted_surplus = 0.0;
    for (const auto& p : state.provinces) {
        if (!p.cohort_stats)
            continue;
        const double pop = static_cast<double>(p.cohort_stats->total_population);
        total_population += pop;
        weighted_surplus += pop * static_cast<double>(p.cohort_stats->subsistence_surplus_ratio);
    }
    const double avg_surplus = total_population > 0.0 ? weighted_surplus / total_population : 1.0;
    const float world_hazard = hazard_mortality_from_settings(state.hazard_settings);
    const float scarcity = std::clamp(1.0f - static_cast<float>(avg_surplus), 0.0f, 1.0f);
    const float pressure = std::clamp(
        cfg_.adversity_base +
            cfg_.adversity_hazard_weight * std::max(0.0f, world_hazard - cfg_.adversity_garden_hazard) +
            cfg_.adversity_scarcity_weight * scarcity,
        0.0f, cfg_.adversity_pressure_cap);
    const double population_term =
        static_cast<double>(cfg_.population_innovation_rate) * total_population;

    // Total: dedicated work + diffuse population innovation, both lifted by pressure and
    // compounded by the tech tree (writing/printing/scientific-method "learning to learn").
    double production = (specialist_term + population_term) * static_cast<double>(pressure);
    production *= static_cast<double>(
        state.tech_effects_for_era(state.technology.current_era).knowledge_mult);

    const float level = state.technology.knowledge_level;
    const double decay = static_cast<double>(cfg_.decay_per_year) * static_cast<double>(level);
    const float net = static_cast<float>(production - decay);

    TechnologyDelta td{};
    if (net != 0.0f)
        td.knowledge_delta = net;

    // Era advancement: once accumulated knowledge clears this era's data-driven
    // threshold, advance (forward-only; capped at the catalog's last era).
    if (era->knowledge_to_advance > 0.0f && level >= era->knowledge_to_advance) {
        const uint8_t max_era = state.era_catalog.max_era();
        if (state.technology.current_era < max_era)
            td.new_era = static_cast<uint8_t>(state.technology.current_era + 1);
    }

    if (td.knowledge_delta.has_value() || td.new_era.has_value())
        delta.technology_deltas.push_back(td);
}

}  // namespace econlife
