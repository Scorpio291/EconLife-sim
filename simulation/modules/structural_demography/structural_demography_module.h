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
    // them, exactly as warfare does. After warfare too, because the political map it
    // draws is the unit this module measures stress over.
    std::vector<std::string_view> runs_after() const override {
        return {"subsistence", "warfare"};
    }
    std::vector<std::string_view> runs_before() const override { return {"population_aging"}; }

    // GLOBAL, not province-parallel (R5). A polity is one political unit and its stress is
    // a property of the whole of it, not of each province separately; and refugees move
    // BETWEEN provinces, which a province-parallel pass cannot express.
    bool is_province_parallel() const noexcept override { return false; }

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Pure, testable laws ---

    bool regime_active(std::string_view regime) const;

    // MASS MOBILISATION POTENTIAL. People who are getting poorer and young are the ones
    // who march. `wage` is consumption over subsistence, `reference` what this people is
    // USED TO — a generation-scale average — and `youth_share` the fraction not yet of
    // working age.
    //
    // Measured against the REFERENCE rather than against subsistence, and that distinction
    // decides whether a society can fall at all. A population whose numbers track its food
    // supply is never absolutely starving: the wage valve sees to that, so against
    // subsistence this term was exactly zero for an entire 12,000-year climb and the
    // multiplicative index was zero with it. Turchin's variable is the real wage against
    // trend, and Davies' J-curve is the standard finding — revolutions follow REVERSALS
    // after improvement, not steady poverty. A people whose living standards have halved
    // in fifty years is in exactly the condition that brings a state down, however well
    // fed it would have looked to its own grandparents.
    //
    // Zero for a people getting no worse off however poor, and zero for a collapsing one
    // with no young men. Pure/static.
    static float mass_mobilisation(float wage, float reference, float youth_share) {
        const float ref = std::max(1e-3f, reference);  // divide-by-zero sentinel
        const float immiseration = std::clamp(1.0f - std::max(0.0f, wage) / ref, 0.0f, 1.0f);
        return immiseration * std::clamp(youth_share, 0.0f, 1.0f);
    }

    // One year of a people's sense of what is normal catching up with what it is actually
    // getting. About a generation — which is how long anyone remembers being better off.
    // Pure/static.
    static float wage_reference_year(float reference, float wage,
                                     const StructuralDemographyConfig& cfg) {
        const float years = std::max(1.0f, cfg.wage_memory_years);
        const float ref = std::max(1e-3f, reference);
        return ref + (std::max(0.0f, wage) - ref) / years;
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

    // REFUGEES (R5). The share of a province's people who leave in a year, given how far
    // its food has failed. When the harvest fails, those who can walk do — the Migration
    // Period, the Sea Peoples, the Irish famine, where roughly an eighth of the country
    // left over five years.
    //
    // Zero for a fed province, rising with the shortfall. The caller applies the further
    // and more important condition: people only go somewhere BETTER, so a province whose
    // neighbours are all worse off keeps its people and starves in place. That is the
    // historically correct reading — the Migration Period happened because there was
    // somewhere to go. Pure/static.
    static float flight_fraction(float surplus_ratio, const StructuralDemographyConfig& cfg) {
        const float shortfall = std::clamp(1.0f - std::max(0.0f, surplus_ratio), 0.0f, 1.0f);
        return std::max(0.0f, cfg.refugee_flight_rate_per_year) * shortfall;
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
