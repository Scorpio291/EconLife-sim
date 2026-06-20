#include "modules/knowledge/knowledge_module.h"

#include "core/world_gen/era_catalog.h"
#include "core/world_gen/occupation_catalog.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"

namespace econlife {

namespace {
constexpr uint32_t kTicksPerYear = 365;
}

bool KnowledgeModule::regime_active(std::string_view regime) const {
    for (const auto& r : cfg_.active_regimes) {
        if (r == regime)
            return true;
    }
    return false;
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

    // Knowledge produced this year = sum over scholar/scribe livelihoods.
    double production = 0.0;
    for (const auto& npc : state.significant_npcs) {
        if (npc.occupation == 0)
            continue;
        const OccupationDefinition* o = state.occupation_catalog.by_index(npc.occupation);
        if (o && o->knowledge_output > 0.0f)
            production += static_cast<double>(o->knowledge_output);
    }
    production *= static_cast<double>(cfg_.production_scalar);

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
