#pragma once

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "political_cycle_types.h"

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class PoliticalCycleModule : public ITickModule {
   public:
    explicit PoliticalCycleModule(const PoliticalCycleConfig& cfg = {});

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
    static float compute_resource_modifier(float resource_deployment,
                                           float resource_scale, float resource_max_effect);
    static float compute_event_modifier_total(const std::vector<float>& event_modifiers,
                                              float event_modifier_cap);
    static float compute_final_vote_share(float raw_share, float resource_modifier,
                                          float event_total);
    static float compute_legislator_support(float motivation_alignment, float obligation_bonus,
                                            float constituency_pressure);
    static bool compute_vote_passed(float votes_for, float votes_against, float majority_threshold);

   private:
    PoliticalCycleConfig cfg_;
    PoliticalCycleModuleState political_state_;
};

}  // namespace econlife
