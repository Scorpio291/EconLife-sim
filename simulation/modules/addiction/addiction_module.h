#pragma once

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "addiction_types.h"
#include "core/tick/tick_module.h"

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class AddictionModule : public ITickModule {
   public:
    std::string_view name() const noexcept override { return "addiction"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    std::vector<std::string_view> runs_after() const override { return {"community_response"}; }
    bool is_province_parallel() const noexcept override { return true; }
    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override;
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Static utilities for testing ---
    static AddictionStage compute_next_stage(const AddictionState& state);
    static float craving_increment(AddictionStage stage);
    static float compute_withdrawal_damage(AddictionStage stage, uint32_t supply_gap_ticks);
    static float compute_work_efficiency(AddictionStage stage);
    static float compute_addiction_rate_delta(AddictionStage old_stage, AddictionStage new_stage);
    static bool is_recovery_complete(uint32_t clean_ticks, float relapse_probability);

    // Constants
    static constexpr float TOLERANCE_PER_USE_CASUAL = 0.05f;
    static constexpr uint32_t REGULAR_USE_THRESHOLD = 30;
    static constexpr uint32_t DEPENDENCY_THRESHOLD = 90;
    static constexpr float DEPENDENCY_TOLERANCE_FLOOR = 0.30f;
    static constexpr float ACTIVE_CRAVING_THRESHOLD = 0.70f;
    static constexpr uint32_t ACTIVE_DURATION_TICKS = 60;
    static constexpr float WITHDRAWAL_HEALTH_HIT = 0.005f;
    static constexpr float DEPENDENT_WORK_EFFICIENCY = 0.70f;
    static constexpr float ACTIVE_WORK_EFFICIENCY = 0.50f;
    static constexpr float TERMINAL_WORK_EFFICIENCY = 0.20f;
    static constexpr uint32_t RECOVERY_ATTEMPT_THRESHOLD = 14;
    static constexpr float CRAVING_DECAY_RATE_RECOVERY = 0.003f;
    static constexpr uint32_t FULL_RECOVERY_TICKS = 365;
    static constexpr float RECOVERY_SUCCESS_THRESHOLD = 0.05f;
    static constexpr float TERMINAL_HEALTH_THRESHOLD = 0.15f;
    static constexpr uint32_t TERMINAL_PERSISTENCE_TICKS = 90;
    static constexpr float RATE_DELTA_PER_ACTIVE_NPC = 0.001f;
    static constexpr float LABOUR_IMPACT_PER_ADDICT = 0.80f;
    static constexpr float HEALTHCARE_LOAD_PER_ADDICT = 0.50f;
    static constexpr float GRIEVANCE_PER_ADDICT_FRACTION = 0.30f;
    static constexpr float CASUAL_CRAVING_INC = 0.01f;
    static constexpr float REGULAR_CRAVING_INC = 0.02f;
    static constexpr float DEPENDENT_CRAVING_INC = 0.03f;
    static constexpr float ACTIVE_CRAVING_INC = 0.05f;
    static constexpr float CASUAL_TO_REGULAR_CRAVING = 0.30f;
    static constexpr float REGULAR_TO_DEPENDENT_CRAVING = 0.70f;

   private:
    // Module-internal addiction state per NPC (not in WorldState)
    std::unordered_map<uint32_t, AddictionState> addiction_states_;
};

}  // namespace econlife
