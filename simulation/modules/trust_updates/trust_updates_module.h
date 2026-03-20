#pragma once

#include "core/tick/tick_module.h"
#include "trust_updates_types.h"
#include <vector>
#include <algorithm>

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class TrustUpdatesModule : public ITickModule {
public:
    std::string_view name() const noexcept override { return "trust_updates"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    std::vector<std::string_view> runs_after() const override { return {"community_response"}; }
    bool is_province_parallel() const noexcept override { return true; }
    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override;
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Static utilities for testing ---

    // Apply trust delta with clamping and ceiling enforcement
    static float apply_trust_delta(float current_trust, float delta, float recovery_ceiling);

    // Check if catastrophic loss triggers ceiling
    static bool is_catastrophic_loss(float trust_delta, float resulting_trust);

    // Compute recovery ceiling from pre-loss trust
    static float compute_recovery_ceiling(float trust_before_loss);

    // Compute new ceiling considering existing ceiling
    static float update_recovery_ceiling(float existing_ceiling, float new_ceiling);

    // Check if trust change is significant enough for memory
    static bool is_significant_change(float trust_delta);

    // Constants
    static constexpr float CATASTROPHIC_TRUST_LOSS_THRESHOLD = -0.55f;
    static constexpr float CATASTROPHIC_TRUST_FLOOR          = 0.10f;
    static constexpr float RECOVERY_CEILING_FACTOR           = 0.60f;
    static constexpr float RECOVERY_CEILING_MINIMUM          = 0.15f;
    static constexpr float SIGNIFICANT_CHANGE_THRESHOLD      = 0.10f;
    static constexpr float TRUST_MIN                         = -1.0f;
    static constexpr float TRUST_MAX                         = 1.0f;
    static constexpr float DEFAULT_RECOVERY_CEILING          = 1.0f;
};

}  // namespace econlife
