#pragma once

// Energy Base Module — GHOST ACRES (R1B), and why one province industrialises (R1C).
//
// An organic economy is bounded by photosynthesis on finite acres. Food, fodder for
// the draft animals, firewood, charcoal for the forges, wool and timber all compete
// for the same ground, so more of any one costs some of another. That bound is why
// every pre-industrial society, however clever, eventually stopped rising: the
// carrying ceiling saturates, and with a fixed ceiling the peak recurs at the same
// height however many times the cycle turns.
//
// Coal breaks the bound by substituting a STOCK for the flow. Burning a tonne does
// the work that would otherwise have taken woodland to grow, so it releases acres
// that never existed — Wrigley's "ghost acres". England and Wales drew 4.3M
// acre-equivalents from coal in 1750, 11.2M in 1800 and 48.1M by 1850, more than the
// ~37M acres of their entire land surface. That is the escape.
//
// It is also, by construction, temporary. The coal is a finite located deposit: a
// society that industrialises is spending something it cannot replace, and when the
// seam under it is worked out the ceiling falls back to what the sun puts on its
// fields. The rise is real and so is the fall.
//
// R1C — INDUCED INNOVATION. Knowing how to burn coal is not enough. Britain adopted
// coal-burning, labour-saving machines because its wage/fuel price ratio made them
// pay; the same knowledge sat unused where labour was cheap and fuel dear. So
// adoption here answers to the ratio of the real wage (consumption over subsistence —
// the same w the Malthusian valve runs on) to what the seam under the province
// actually costs to work. It saturates rather than switching: there is no moment when
// a society decides to industrialise, only techniques that gradually begin to pay.
//
// Conserved and located: every tonne burned comes out of a named deposit in a named
// province, emitted as a DepositDelta. Nothing appears from nowhere.
//
// See docs/design/EconLife_Realism_Roadmap_v01.md (1B, 1C).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "core/world_state/geography.h"  // ResourceDeposit, ResourceType

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class EnergyBaseModule : public ITickModule {
   public:
    explicit EnergyBaseModule(const EnergyBaseConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "energy_base"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    // Needs the era resolved (technology) and publishes the ghost acres that the
    // commons carrying ceiling is computed from, so it must land before subsistence.
    std::vector<std::string_view> runs_after() const override { return {"technology"}; }
    std::vector<std::string_view> runs_before() const override { return {"subsistence"}; }

    bool is_province_parallel() const noexcept override { return true; }

    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override;
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Pure, testable laws ---

    // Is the energy base active in `regime`? (regime ∈ cfg.active_regimes)
    bool regime_active(std::string_view regime) const;

    // How well a seam can be worked, in [0, 1]: rich, shallow, reachable coal is cheap
    // at the pithead and poor, deep, stranded coal is dear. This is the fuel price side
    // of the induced-innovation ratio, and it is a property of the rock, not a dial.
    static float seam_workability(const ResourceDeposit& deposit) {
        return std::max(0.0f, deposit.quality) * std::max(0.0f, deposit.accessibility) *
               std::max(0.0f, 1.0f - deposit.depth);
    }

    // INDUCED INNOVATION (R1C). The share of its fuel demand a province actually tries
    // to meet from coal, given how dear labour is (`wage` = consumption/subsistence,
    // the real wage) against how cheap the seam is. Saturating in the ratio: no
    // threshold, no moment of decision — techniques simply begin to pay.
    //
    // A society at bare subsistence with deep, poor coal adopts almost none of it, which
    // is the historical fact this reproduces: China knew coal and had seams, and the
    // machines never paid there.
    static float coal_adoption(float wage, float workability, const EnergyBaseConfig& cfg) {
        const float w = std::max(0.0f, wage);
        const float cheapness = std::max(cfg.min_seam_workability, workability);
        const float ratio = w / cheapness;
        const float half = std::max(1e-3f, cfg.adoption_ratio_halfsat);
        return ratio / (ratio + half);
    }

    // How much of the mining art a society commands, in [0, 1): saturating in
    // accumulated knowledge. Outcrop coal was dug for centuries before anybody could
    // ventilate a drowned seam, so this rises slowly and never quite reaches 1.
    static float mining_technique(float knowledge_level, const EnergyBaseConfig& cfg) {
        const float K = std::max(0.0f, knowledge_level);
        return K / (K + std::max(1.0f, cfg.mining_technique_halfsat));
    }

    // The ghost acres `tonnes` of coal stand in for, as a fraction of the land a
    // pre-industrial economy of `population` people would have needed. Pure unit
    // arithmetic: tonnes -> woodland acres -> share of the organic land base.
    //
    // 0.30 means coal is doing the work of another 30% of the country's acres, which is
    // roughly where England stood in 1800; above 1.0 it is doing more work than the
    // entire land surface, which is where England stood by 1850.
    static float ghost_land_fraction(float tonnes, uint32_t population,
                                     const EnergyBaseConfig& cfg) {
        if (population == 0)
            return 0.0f;
        const float organic_acres =
            static_cast<float>(population) * std::max(0.01f, cfg.preindustrial_acres_per_head);
        return std::max(0.0f, tonnes) * cfg.woodland_acres_per_tonne_coal / organic_acres;
    }

   private:
    EnergyBaseConfig cfg_;
    // True once the module has published in an active regime, so the one-time
    // regime-exit reset knows there is a stale value to clear. Mirrors subsistence's
    // commons_state_dirty_.
    bool energy_state_dirty_ = false;
};

}  // namespace econlife
