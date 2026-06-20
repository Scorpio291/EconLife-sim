#pragma once

// Knowledge Module — the engine that lets a society move *forward*.
//
// Surplus frees a few livelihoods into scholars/scribes (occupations with
// knowledge_output > 0). They accumulate practical knowledge (geometry, the
// calendar, writing, technique), which: (a) raises the subsistence carrying
// ceiling — the escape from the Malthusian trap (read by the subsistence module
// via GlobalTechnologyState.knowledge_level), and (b) once it crosses an era's
// data-driven knowledge_to_advance, advances the era. Progress is contingent: a
// society with no scholars never accumulates knowledge and stays put.
//
// Pre-market only (modern eras use the technology module). Sequential — it
// aggregates one global knowledge figure. See
// docs/design/EconLife_World_Spectrum_and_Evolution_Plan.md (the knowledge engine).

#include <cstdint>
#include <string_view>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class KnowledgeModule : public ITickModule {
   public:
    explicit KnowledgeModule(const KnowledgeConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "knowledge"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    // After subsistence (which assigns the scholar livelihoods) and technology
    // (which owns current_era).
    std::vector<std::string_view> runs_after() const override {
        return {"subsistence", "technology"};
    }

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    bool regime_active(std::string_view regime) const;

   private:
    KnowledgeConfig cfg_;
};

}  // namespace econlife
