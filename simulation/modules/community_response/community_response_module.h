#pragma once

// Community Response Module — sequential tick module that aggregates NPC state
// into province-level community metrics via EMA smoothing and evaluates the
// 7-stage community response escalation state machine.
//
// See docs/interfaces/community_response/INTERFACE.md for the canonical specification.

#include <cstdint>
#include <string_view>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "modules/community_response/community_response_types.h"

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;
struct NPC;
struct MemoryEntry;
enum class MemoryType : uint8_t;

// ---------------------------------------------------------------------------
// CommunityResponseModule — ITickModule implementation for community metrics
// ---------------------------------------------------------------------------
class CommunityResponseModule : public ITickModule {
   public:
    explicit CommunityResponseModule(const ConsequenceDelayConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "community_response"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    bool is_province_parallel() const noexcept override { return false; }

    std::vector<std::string_view> runs_after() const override { return {"npc_behavior"}; }

    std::vector<std::string_view> runs_before() const override {
        return {"trust_updates", "political_cycle"};
    }

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Static utility functions (exposed for testing) ---

    // Compute cohesion sample for a single NPC.
    // sample = clamp(social_capital / social_capital_max, 0, 1)
    //        * clamp(stability_weight, 0, 1)
    static float compute_cohesion_sample(float social_capital, float social_capital_max,
                                         float stability_weight);

    // Compute grievance contribution from an NPC's memory log.
    // Sum of abs(emotional_weight) * memory_type_grievance_weight(type) for negative entries
    // with decay > memory_decay_floor.
    static float compute_grievance_contribution(const std::vector<MemoryEntry>& memory_log,
                                                float memory_decay_floor);

    // Get grievance weight for a memory type.
    // Direct harm types = 1.0, economic harm = 0.5, others = 0.0
    static float memory_type_grievance_weight(MemoryType type);

    // Compute resource access sample for a single NPC.
    // sample = clamp(capital/capital_normalizer + social_capital/social_normalizer, 0, 1)
    static float compute_resource_access_sample(float capital, float capital_normalizer,
                                                float social_capital, float social_normalizer);

    // EMA update: new_value = current * (1 - alpha) + sample * alpha
    static float ema_update(float current_value, float new_sample, float alpha);

    // Determine the highest stage whose thresholds are satisfied.
    static CommunityResponseStage evaluate_stage(float grievance, float cohesion,
                                                 float institutional_trust, float resource_access,
                                                 bool has_leadership);

    // Apply stage transition rules: at most one step, cannot skip.
    // If opposition_org_exists and current >= sustained_opposition, no regression.
    static CommunityResponseStage apply_stage_transition(CommunityResponseStage current,
                                                         CommunityResponseStage target,
                                                         bool can_regress,
                                                         bool opposition_org_exists);

    // --- Internal province tracking ---
    struct ProvinceOppositionState {
        bool opposition_org_exists = false;
        uint32_t last_stage_change_tick = 0;
    };
    std::vector<ProvinceOppositionState>& province_states() { return province_states_; }
    const std::vector<ProvinceOppositionState>& province_states() const { return province_states_; }

    // --- Constants ---
    struct Constants {
        static constexpr float ema_alpha = 0.05f;
        static constexpr float social_capital_max = 100.0f;
        static constexpr float capital_normalizer = 10000.0f;
        static constexpr float social_normalizer = 50.0f;
        static constexpr float memory_decay_floor = 0.01f;
        static constexpr float grievance_normalizer = 10.0f;
        static constexpr float grievance_shock_threshold = 0.15f;
        static constexpr float resistance_revenue_penalty = -0.15f;
        static constexpr float trauma_grievance_floor_scale = 0.25f;
        static constexpr float trauma_trust_ceiling_scale = 0.30f;
        static constexpr uint32_t regression_cooldown_ticks = 7;
    };

   private:
    ConsequenceDelayConfig cfg_;
    std::vector<ProvinceOppositionState> province_states_;
};

}  // namespace econlife
