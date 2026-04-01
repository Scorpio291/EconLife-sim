#pragma once

// Media System Module — sequential tick module that simulates independent media,
// journalist behavior, story creation from evidence, story propagation, and
// exposure conversion from damaging coverage.
//
// See docs/interfaces/media_system/INTERFACE.md for the canonical specification.

#include <cstdint>
#include <string_view>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "modules/media_system/media_system_types.h"

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;
struct NPC;
struct EvidenceToken;

// ---------------------------------------------------------------------------
// MediaSystemModule — ITickModule implementation
// ---------------------------------------------------------------------------
class MediaSystemModule : public ITickModule {
   public:
    explicit MediaSystemModule(const MediaSystemConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "media_system"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    bool is_province_parallel() const noexcept override { return false; }

    std::vector<std::string_view> runs_after() const override {
        return {"evidence", "facility_signals"};
    }

    std::vector<std::string_view> runs_before() const override {
        return {"trust_updates", "political_cycle"};
    }

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Module-internal storage ---
    std::vector<MediaOutlet>& outlets() { return outlets_; }
    const std::vector<MediaOutlet>& outlets() const { return outlets_; }

    std::vector<Story>& active_stories() { return active_stories_; }
    const std::vector<Story>& active_stories() const { return active_stories_; }

    // --- Static utility functions (exposed for testing) ---

    // Compute evidence weight from evidence tokens (mean actionability).
    static float compute_evidence_weight(const std::vector<float>& actionabilities);

    // Evaluate editorial filter: returns true if story passes (published).
    // publish_decision = editorial_independence * (1.0 - owner_suppression_rate)
    static bool evaluate_editorial_filter(float editorial_independence,
                                          float owner_suppression_rate, float roll);

    // Compute cross-outlet pickup probability.
    static float compute_pickup_probability(float evidence_weight, float other_outlet_credibility,
                                            float cross_outlet_pickup_rate);

    // Compute social media amplification contribution.
    static float compute_social_amplification(float current_amplification, float social_reach,
                                              float social_multiplier, float evidence_weight);

    // Compute exposure delta from a damaging story.
    static float compute_exposure_delta(float amplification, float exposure_per_unit);

    // Check if story is within propagation window.
    static bool is_within_propagation_window(uint32_t published_tick, uint32_t current_tick,
                                             uint32_t window_ticks);

   private:
    MediaSystemConfig cfg_;
    std::vector<MediaOutlet> outlets_;
    std::vector<Story> active_stories_;
    uint32_t next_story_id_ = 1000;

    // Create stories from journalist evidence awareness.
    void create_stories_from_journalists(const WorldState& state, DeltaBuffer& delta);

    // Propagate active stories (cross-outlet pickup, social amplification).
    void propagate_stories(const WorldState& state, DeltaBuffer& delta);

    // Convert exposure from damaging stories.
    void convert_exposure(const WorldState& state, DeltaBuffer& delta);

    // Expire stories outside the propagation window.
    void expire_old_stories(uint32_t current_tick);
};

}  // namespace econlife
