#pragma once

#include "core/tick/tick_module.h"
#include "political_cycle_types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <cmath>

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class PoliticalCycleModule : public ITickModule {
public:
    std::string_view name() const noexcept override { return "political_cycle"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    std::vector<std::string_view> runs_after() const override { return {"community_response"}; }
    bool is_province_parallel() const noexcept override { return false; }
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Static utilities for testing ---
    static float compute_raw_vote_share(
        const std::unordered_map<std::string, float>& approval_by_demographic,
        const std::vector<DemographicWeight>& demographics);
    static float compute_resource_modifier(float resource_deployment);
    static float compute_event_modifier_total(const std::vector<float>& event_modifiers);
    static float compute_final_vote_share(float raw_share, float resource_modifier, float event_total);
    static float compute_legislator_support(float motivation_alignment, float obligation_bonus,
                                             float constituency_pressure);
    static bool compute_vote_passed(float votes_for, float votes_against, float majority_threshold);

    // Constants
    static constexpr float SUPPORT_THRESHOLD     = 0.55f;
    static constexpr float OPPOSE_THRESHOLD      = 0.35f;
    static constexpr float MAJORITY_THRESHOLD    = 0.50f;
    static constexpr float RESOURCE_SCALE        = 2.0f;
    static constexpr float RESOURCE_MAX_EFFECT   = 0.15f;
    static constexpr float EVENT_MODIFIER_CAP    = 0.20f;

private:
    PoliticalCycleModuleState political_state_;
};

}  // namespace econlife
