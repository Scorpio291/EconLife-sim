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

    // THE PRESS (R3E). How many times faster a society copies than it did by hand.
    //
    // A scribe produces about one substantial work a year; a press runs off hundreds. All
    // of Europe held fewer than 30,000 manuscript books in 1450 and 8-12 million printed
    // ones by 1500. This is what ends the possibility of a dark age: roughly 90% of
    // classical Latin literature was lost between 500 and 900 CE because every copy was a
    // hand-made object in a named building that could burn, and a work in ten thousand
    // houses cannot be lost by anything short of the end of the civilisation.
    //
    // Gated on accumulated knowledge rather than on an era number, so a world that
    // develops differently still gets the press at the right point in ITS development;
    // and saturating rather than switching, because presses spread. Returns 1.0 for a
    // society that has not got there — a manuscript culture is unchanged. Pure/static.
    static double printing_copy_mult(float level, const KnowledgeConfig& cfg) {
        const double K = static_cast<double>(std::max(0.0f, level));
        const double half = static_cast<double>(std::max(1.0f, cfg.printing_knowledge_halfsat));
        const double adoption = K / (K + half);
        const double gain = static_cast<double>(std::max(1.0f, cfg.printing_copy_multiplier)) - 1.0;
        return 1.0 + gain * adoption;
    }

    // POLYCENTRISM (R3F). How much more slowly a written corpus is lost when it sits in
    // `shelters` independent jurisdictions rather than one.
    //
    // An idea suppressed or burned in one polity survives in the next. Tyndale printed in
    // Antwerp, Galileo circulated in the Netherlands, Descartes published in Amsterdam;
    // the reason Europe's scientific revolution could not be stopped is that nobody could
    // stop it everywhere at once. A unified empire offers no refuge — which is why
    // conquest, in this model, costs a civilisation the very stock that lets its next
    // cycle start above the last. Returns 1.0 for a single polity. Pure/static.
    static double shelter_loss_divisor(uint32_t shelters, const KnowledgeConfig& cfg) {
        if (shelters <= 1)
            return 1.0;
        return 1.0 + static_cast<double>(std::max(0.0f, cfg.record_loss_shelter_weight)) *
                         static_cast<double>(shelters - 1);
    }

    // How much of what a neighbour knows a province can actually take in, given the share
    // of its people free to be scholars. Zero for a region whose learned stratum has
    // scattered, approaching one for a society with a substantial literate class.
    //
    // Ideas travel with traders, envoys and scribes and arrive as texts and techniques
    // that need people trained to use them — so a dark age is not a shortage of knowledge
    // in the world, it is a shortage of anyone able to receive it. Greek mathematics sat
    // intact in Byzantium and Baghdad the entire time western Europe could not read it.
    // Pure/static.
    static double absorptive_capacity(float specialist_fraction, const KnowledgeConfig& cfg) {
        const double spec = static_cast<double>(std::max(0.0f, specialist_fraction));
        const double half = static_cast<double>(std::max(1e-4f, cfg.knowledge_absorption_halfsat));
        return spec / (spec + half);
    }

   private:
    KnowledgeConfig cfg_;
};

}  // namespace econlife
