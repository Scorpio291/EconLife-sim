#pragma once

#include <vector>

#include "alternative_identity_types.h"
#include "core/tick/tick_module.h"

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class AlternativeIdentityModule : public ITickModule {
   public:
    std::string_view name() const noexcept override { return "alternative_identity"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    std::vector<std::string_view> runs_after() const override { return {"investigator_engine"}; }
    bool is_province_parallel() const noexcept override { return false; }
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Static utilities ---
    static float decay_documentation_quality(float current_quality, float decay_rate);
    static float build_documentation_quality(float current_quality, float build_rate);
    static bool should_burn_identity(float documentation_quality, float burn_threshold);
    static float compute_witness_discovery_confidence();
    static float compute_forensic_discovery_confidence();

    static constexpr float DOCUMENTATION_DECAY_RATE = 0.001f;
    static constexpr float DOCUMENTATION_BUILD_RATE = 0.005f;
    static constexpr float BURN_THRESHOLD = 0.10f;
    static constexpr float WITNESS_CONFIDENCE = 0.70f;
    static constexpr float FORENSIC_CONFIDENCE = 0.55f;

   private:
    std::vector<AlternativeIdentity> identities_;
};

}  // namespace econlife
