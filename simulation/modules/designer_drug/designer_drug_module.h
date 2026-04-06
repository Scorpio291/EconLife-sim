#pragma once

#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "designer_drug_types.h"

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class DesignerDrugModule : public ITickModule {
   public:
    explicit DesignerDrugModule(const DesignerDrugConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "designer_drug"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    std::vector<std::string_view> runs_after() const override {
        return {"investigator_engine", "drug_economy"};
    }
    bool is_province_parallel() const noexcept override { return false; }
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Static utilities ---
    static bool is_detection_triggered(float cumulative_evidence, float threshold);
    static uint32_t compute_review_duration(uint32_t base_duration, float political_delay);
    float compute_market_margin(SchedulingStage stage, bool has_successor) const;
    static bool should_check_detection(uint32_t current_tick, uint32_t monthly_interval);
    static float accumulate_evidence_weight(float current, float new_weight);

   private:
    DesignerDrugConfig cfg_;
    std::vector<DesignerDrugCompound> compounds_;
};

}  // namespace econlife
