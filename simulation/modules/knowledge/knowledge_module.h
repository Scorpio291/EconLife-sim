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

#include <algorithm>
#include <cmath>
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

    // IDEAS GET HARDER TO FIND. How much more a discovery costs a society that already
    // knows `level` than it cost one that knew nothing. Always >= 1, rising without
    // bound: the easy discoveries are made first and every one made leaves the next
    // harder. American research productivity has fallen roughly 41-fold since the 1930s
    // while researcher numbers rose more than twenty-fold — sustaining Moore's law now
    // takes eighteen times the effort it took in 1971 — and the same holds for crop
    // yields and for medicine.
    //
    // Jones' semi-endogenous form, expressed against a reference stock so the dawn is
    // untouched: at `level` = halfsat the next discovery costs twice the first, and
    // beyond it production falls as level^-beta. Nothing is capped; the frontier simply
    // recedes. Pure/static.
    static double discovery_difficulty(float level, const KnowledgeConfig& cfg) {
        const double K = static_cast<double>(std::max(0.0f, level));
        const double half = static_cast<double>(std::max(1.0f, cfg.discovery_difficulty_halfsat));
        const double beta = static_cast<double>(std::max(0.0f, cfg.discovery_difficulty_exponent));
        if (beta <= 0.0)
            return 1.0;
        return std::pow(1.0 + K / half, beta);
    }

   private:
    KnowledgeConfig cfg_;
};

}  // namespace econlife
