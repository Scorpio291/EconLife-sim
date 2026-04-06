#pragma once

#include <algorithm>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "trust_updates_types.h"

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class TrustUpdatesModule : public ITickModule {
   public:
    explicit TrustUpdatesModule(const TrustUpdatesConfig& cfg = {}) : cfg_(cfg) {}

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

   private:
    TrustUpdatesConfig cfg_;
};

}  // namespace econlife
