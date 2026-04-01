#pragma once

#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "core/world_state/shared_types.h"
#include "influence_network_types.h"

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class InfluenceNetworkModule : public ITickModule {
   public:
    explicit InfluenceNetworkModule(const InfluenceNetworkConfig& cfg = {});

    std::string_view name() const noexcept override { return "influence_network"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    std::vector<std::string_view> runs_after() const override { return {"community_response"}; }
    bool is_province_parallel() const noexcept override { return false; }
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Static utilities for testing ---
    static bool is_trust_based(float trust, float threshold);
    static bool is_fear_based(float fear, float trust, float fear_threshold,
                              float fear_trust_ceiling);
    static InfluenceType classify_relationship(float trust, float fear, bool is_movement_ally,
                                               float trust_threshold, float fear_threshold,
                                               float fear_trust_ceiling);
    static float compute_composite_health(uint32_t trust_count, uint32_t fear_count,
                                          uint32_t obligation_count, uint32_t movement_count,
                                          uint32_t health_target_count, float trust_weight,
                                          float obligation_weight, float fear_weight,
                                          float movement_weight, float diversity_bonus);
    static float compute_obligation_erosion(float obligation_erosion_rate);
    static bool is_catastrophic_loss(float trust_delta, float resulting_trust,
                                     float catastrophic_trust_loss_threshold,
                                     float catastrophic_trust_floor);
    static float compute_recovery_ceiling(float trust_before, float trust_delta,
                                          float catastrophic_trust_loss_threshold,
                                          float catastrophic_trust_floor,
                                          float recovery_ceiling_factor,
                                          float recovery_ceiling_minimum);

   private:
    InfluenceNetworkConfig cfg_;
    InfluenceNetworkHealth cached_health_;
};

}  // namespace econlife
