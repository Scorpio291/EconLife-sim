#pragma once

#include "core/tick/tick_module.h"
#include "influence_network_types.h"
#include "core/world_state/shared_types.h"
#include <vector>

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class InfluenceNetworkModule : public ITickModule {
public:
    std::string_view name() const noexcept override { return "influence_network"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    std::vector<std::string_view> runs_after() const override { return {"community_response"}; }
    bool is_province_parallel() const noexcept override { return false; }
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Static utilities for testing ---
    static bool is_trust_based(float trust);
    static bool is_fear_based(float fear, float trust);
    static InfluenceType classify_relationship(float trust, float fear, bool is_movement_ally);
    static float compute_composite_health(uint32_t trust_count, uint32_t fear_count,
                                           uint32_t obligation_count, uint32_t movement_count);
    static float compute_obligation_erosion();
    static bool is_catastrophic_loss(float trust_delta, float resulting_trust);
    static float compute_recovery_ceiling(float trust_before, float trust_delta);

    // Constants
    static constexpr float TRUST_CLASSIFICATION_THRESHOLD   = 0.40f;
    static constexpr float FEAR_CLASSIFICATION_THRESHOLD    = 0.35f;
    static constexpr float FEAR_TRUST_CEILING               = 0.20f;
    static constexpr float CATASTROPHIC_TRUST_LOSS_THRESHOLD = -0.55f;
    static constexpr float CATASTROPHIC_TRUST_FLOOR         = 0.10f;
    static constexpr float RECOVERY_CEILING_FACTOR          = 0.60f;
    static constexpr float RECOVERY_CEILING_MINIMUM         = 0.15f;
    static constexpr uint32_t HEALTH_TARGET_COUNT           = 10;
    static constexpr float OBLIGATION_EROSION_RATE          = 0.001f;

    // Health weights
    static constexpr float TRUST_WEIGHT      = 0.35f;
    static constexpr float OBLIGATION_WEIGHT = 0.25f;
    static constexpr float FEAR_WEIGHT       = 0.20f;
    static constexpr float MOVEMENT_WEIGHT   = 0.20f;
    static constexpr float DIVERSITY_BONUS   = 0.05f;

private:
    InfluenceNetworkHealth cached_health_;
};

}  // namespace econlife
