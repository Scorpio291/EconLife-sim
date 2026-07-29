#pragma once

// Structural Demography Module — THE ENDOGENOUS FALL (R2D).
//
// Societies do not only come apart because the harvest failed. Turchin's
// structural-demographic theory: as a population grows it depresses the real wage while
// inflating the incomes of those at the top, so the number of people raised to expect a
// place above the plough grows faster than the number of such places. Surplus claimants
// turn on one another, the retinues they raise have to be fed by somebody, and the
// fiscal base erodes underneath. The cycles run 200-300 years and are largely
// independent of the weather — Rome, the Han, late Ming, the Ottomans, France before
// 1789, and by the same measure the United States since about 1980.
//
// The model already had every ingredient and no mechanism joining them: a stratum with
// generational inertia that persists through lean decades (R2C), a real wage (R2A), a
// granary, and institutional trust. What was missing was the observation that the GAP
// between the stratum a society holds and the one its harvest can support is itself a
// destabilising force — the people in that gap have been raised to expect something the
// land no longer provides, and they do not quietly return to the plough.
//
// The Political Stress Index is MULTIPLICATIVE in three terms, which is the theory's
// sharpest and most falsifiable claim: all three must be elevated at once. A miserable
// population under a united elite and a solvent state does not bring the state down,
// and nor does a fractured elite over a contented one. That is why most bad years are
// merely bad years and the ones that are not are catastrophic.
//
// Everything it does is a real located flow, not a mood:
//   - factional conflict KILLS PEOPLE, published as an independent competing risk and
//     applied through the same cohort mortality path as war;
//   - retinues EAT, drawn from the province granary at the campaign ration, over and
//     above what those same people were already consuming;
//   - stress ERODES STABILITY, which feeds back into births and deaths.
//
// See docs/design/EconLife_Realism_Roadmap_v01.md (2D).

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

class StructuralDemographyModule : public ITickModule {
   public:
    explicit StructuralDemographyModule(const StructuralDemographyConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "structural_demography"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    // Reads the stratum and the surplus subsistence publishes this tick, and publishes a
    // death fraction population_aging consumes in the same tick — so it must sit between
    // them, exactly as warfare does.
    std::vector<std::string_view> runs_after() const override { return {"subsistence"}; }
    std::vector<std::string_view> runs_before() const override { return {"population_aging"}; }

    bool is_province_parallel() const noexcept override { return true; }

    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override;
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Pure, testable laws ---

    bool regime_active(std::string_view regime) const;

    // MASS MOBILISATION POTENTIAL. People who are hungry and young are the ones who
    // march. `wage` is consumption over subsistence (1.0 = exactly fed), `youth_share`
    // the fraction of the population not yet of working age. Zero for a well-fed
    // population however young, and zero for a starving one with no young men.
    static float mass_mobilisation(float wage, float youth_share) {
        const float immiseration = std::max(0.0f, 1.0f - std::max(0.0f, wage));
        return immiseration * std::clamp(youth_share, 0.0f, 1.0f);
    }

    // ELITE MOBILISATION POTENTIAL — the overproduction itself. `held` is the
    // non-farming stratum the society is actually carrying (it has generational inertia,
    // so it lags), `supported` what this year's harvest can pay for. The result is
    // claimants per remaining place: zero when the two agree, and rising without bound
    // as the places disappear from under a stratum that does not shrink to match.
    //
    // Uncapped on purpose. A society holding a scholar class its land can no longer feed
    // at all is in an extreme state and the model should say so; the bound belongs at
    // the far end, where the death rate becomes a probability via 1 - exp(-rate).
    static float elite_overproduction(float held, float supported,
                                      const StructuralDemographyConfig& cfg) {
        const float h = std::max(0.0f, held);
        const float s = std::max(0.0f, supported);
        const float surplus = std::max(0.0f, h - s);
        const float places = std::max(cfg.min_positions_fraction, s);  // divide-by-zero sentinel
        return surplus / places;
    }

    // STATE FISCAL DISTRESS. A state that cannot pay its servants and is not believed
    // when it promises to. The granary is the pre-modern treasury — an empty one is a
    // polity with nothing to distribute — and trust is what lets it borrow against next
    // year instead.
    static float fiscal_distress(float granary_cover, float institutional_trust) {
        const float empty = 1.0f - std::clamp(granary_cover, 0.0f, 1.0f);
        const float disbelief = 1.0f - std::clamp(institutional_trust, 0.0f, 1.0f);
        return empty * disbelief;
    }

    // THE POLITICAL STRESS INDEX. Multiplicative: all three at once, or nothing.
    static float political_stress(float mmp, float emp, float sfd) {
        return std::max(0.0f, mmp) * std::max(0.0f, emp) * std::max(0.0f, sfd);
    }

    // Annual death fraction from factional conflict at a given stress. A Poisson
    // first-arrival on the conflict hazard rate, so it approaches (and never exceeds)
    // total loss however extreme the stress becomes — the bound is physical, not a cap.
    static float conflict_death_fraction(float stress, const StructuralDemographyConfig& cfg) {
        const float rate = std::max(0.0f, stress) * std::max(0.0f, cfg.conflict_death_rate_at_unit_stress);
        if (std::isnan(rate))
            return 0.0f;  // crash sentinel only
        return 1.0f - std::exp(-rate);
    }

   private:
    StructuralDemographyConfig cfg_;
    // True once the module has published in an active regime, so the one-time
    // regime-exit reset knows there is a stale value to clear.
    bool stress_state_dirty_ = false;
};

}  // namespace econlife
