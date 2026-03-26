#pragma once

#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "informant_system_types.h"

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class InformantSystemModule : public ITickModule {
   public:
    explicit InformantSystemModule(const InformantConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "informant_system"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    std::vector<std::string_view> runs_after() const override {
        return {"investigator_engine", "legal_process"};
    }
    bool is_province_parallel() const noexcept override { return false; }
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Static utilities ---
    static float compute_flip_probability(float base_flip_rate, float risk_tolerance, float trust,
                                          uint32_t mutual_incrimination_count,
                                          uint32_t compartmentalization_level,
                                          float max_flip_probability,
                                          float risk_factor_scale,
                                          float trust_factor_scale,
                                          float incrimination_suppression,
                                          float compartment_bonus_per_level);
    static float compute_risk_factor(float risk_tolerance, float risk_factor_scale);
    static float compute_trust_factor(float trust, float trust_factor_scale);
    static float compute_incrimination_suppression(uint32_t obligation_count,
                                                   float incrimination_suppression);
    static float compute_compartmentalization_bonus(uint32_t level, float compartment_bonus);

   private:
    InformantConfig cfg_;
    std::vector<InformantRecord> records_;
};

}  // namespace econlife
