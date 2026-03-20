#pragma once

#include "core/tick/tick_module.h"
#include "informant_system_types.h"
#include <vector>

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class InformantSystemModule : public ITickModule {
public:
    std::string_view name() const noexcept override { return "informant_system"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    std::vector<std::string_view> runs_after() const override { return {"investigator_engine", "legal_process"}; }
    bool is_province_parallel() const noexcept override { return false; }
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Static utilities ---
    static float compute_flip_probability(float base_flip_rate, float risk_tolerance,
                                           float trust, uint32_t mutual_incrimination_count,
                                           uint32_t compartmentalization_level);
    static float compute_risk_factor(float risk_tolerance);
    static float compute_trust_factor(float trust);
    static float compute_incrimination_suppression(uint32_t obligation_count);
    static float compute_compartmentalization_bonus(uint32_t level);
    static constexpr float MAX_FLIP_PROBABILITY         = 0.20f;
    static constexpr float BASE_FLIP_RATE               = 0.10f;
    static constexpr float RISK_FACTOR_SCALE            = 0.30f;
    static constexpr float TRUST_FACTOR_SCALE           = 0.25f;
    static constexpr float INCRIMINATION_SUPPRESSION    = 0.08f;
    static constexpr float COMPARTMENT_BONUS_PER_LEVEL  = 0.05f;
    static constexpr float PAY_SILENCE_COST             = 50000.0f;
    static constexpr float VIOLENCE_LE_MULTIPLIER       = 3.0f;

private:
    std::vector<InformantRecord> records_;
};

}  // namespace econlife
