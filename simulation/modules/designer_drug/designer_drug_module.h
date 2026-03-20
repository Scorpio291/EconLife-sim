#pragma once

#include "core/tick/tick_module.h"
#include "designer_drug_types.h"
#include <vector>

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class DesignerDrugModule : public ITickModule {
public:
    std::string_view name() const noexcept override { return "designer_drug"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    std::vector<std::string_view> runs_after() const override { return {"investigator_engine", "drug_economy"}; }
    bool is_province_parallel() const noexcept override { return false; }
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Static utilities ---
    static bool is_detection_triggered(float cumulative_evidence, float threshold);
    static uint32_t compute_review_duration(uint32_t base_duration, float political_delay);
    static float compute_market_margin(SchedulingStage stage, bool has_successor);
    static bool should_check_detection(uint32_t current_tick, uint32_t monthly_interval);
    static float accumulate_evidence_weight(float current, float new_weight);

    static constexpr float DETECTION_THRESHOLD           = 2.5f;
    static constexpr uint32_t BASE_REVIEW_DURATION       = 180;
    static constexpr float UNSCHEDULED_MARGIN            = 2.5f;
    static constexpr float SCHEDULED_MARGIN              = 1.0f;
    static constexpr float NO_SUCCESSOR_MARGIN           = 0.80f;
    static constexpr uint32_t MONTHLY_INTERVAL           = 30;

private:
    std::vector<DesignerDrugCompound> compounds_;
};

}  // namespace econlife
