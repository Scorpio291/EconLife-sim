#pragma once

// NPC Behavior Module — province-parallel tick module that manages
// NPC daily decision-making via expected-value engine, memory formation
// and decay, relationship updates, motivation shifts, and knowledge
// confidence decay.
//
// See docs/interfaces/npc_behavior/INTERFACE.md for the canonical specification.

#include <cstdint>
#include <string_view>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "core/world_state/npc.h"  // NPC, MemoryType, MemoryEntry, Relationship, MotivationVector
#include "modules/npc_behavior/npc_behavior_types.h"

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;
struct ActionOutcome;

// ---------------------------------------------------------------------------
// NpcBehaviorModule — ITickModule implementation for NPC daily behavior
// ---------------------------------------------------------------------------
class NpcBehaviorModule : public ITickModule {
   public:
    explicit NpcBehaviorModule(const NpcBehaviorConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "npc_behavior"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    bool is_province_parallel() const noexcept override { return true; }

    std::vector<std::string_view> runs_after() const override {
        return {"financial_distribution", "npc_business", "commodity_trading", "real_estate"};
    }

    std::vector<std::string_view> runs_before() const override {
        return {};  // end of Pass 1 chain
    }

    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override;

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Static utility functions (exposed for testing) ---

    // Compute the raw expected value contribution for a single outcome,
    // weighted by the NPC's motivation vector.
    // EV = motivation_weight[outcome.type] * outcome.probability * outcome.magnitude
    static float compute_expected_value(const NPC& npc, const ActionOutcome& outcome);

    // Compute risk discount factor from exposure_risk and risk_tolerance.
    // risk_discount = max(min_risk_discount, 1.0 - (exposure_risk - risk_tolerance) *
    // risk_sensitivity_coeff)
    static float compute_risk_discount(float exposure_risk, float risk_tolerance);

    // Apply relationship modifier for cooperative actions.
    // result = base_ev * (1.0 + trust * trust_ev_bonus)
    static float apply_relationship_modifier(float base_ev, float trust, float trust_ev_bonus);

    // Evaluate a complete action: sum outcome EVs, apply risk discount and
    // optional trust bonus. Returns ActionEvaluation with net_utility.
    static ActionEvaluation evaluate_action(const NPC& npc, DailyAction action,
                                            const std::vector<ActionOutcome>& outcomes,
                                            float exposure_risk, float trust_bonus_target,
                                            float trust_ev_bonus);

    // Decay all memory entries in an NPC's memory_log.
    // decay = decay * (1.0 - decay_rate). Entries below decay_floor are archived (removed).
    static void decay_memories(NPC& npc_copy, float decay_rate);

    // Archive the memory entry with the lowest decay value.
    // Used when memory_log is at MAX_MEMORY_ENTRIES capacity.
    static void archive_lowest_decay_memory(std::vector<MemoryEntry>& memory_log);

    // Renormalize motivation weights to sum to 1.0.
    // If all weights are zero, reset to uniform distribution.
    static void renormalize_motivation_weights(MotivationVector& motivations);

    // Clamp relationship trust to [-1.0, 1.0], fear to [0.0, 1.0].
    // Trust cannot exceed recovery_ceiling.
    static void clamp_relationship(Relationship& rel);

    // Compute worker satisfaction from memory log.
    // Returns 0.0-1.0; based on ratio of positive to negative employment memories.
    static float worker_satisfaction(const NPC& npc);

    // Map MemoryType to the closest OutcomeType for motivation shift.
    static size_t memory_type_to_outcome_index(MemoryType type);

    // --- Constants ---
    struct Constants {
        static constexpr float inaction_threshold = 0.10f;
        static constexpr float min_risk_discount = 0.05f;
        static constexpr float risk_sensitivity_coeff = 2.0f;
        static constexpr float trust_ev_bonus = 0.3f;
        static constexpr float memory_decay_rate = 0.002f;
        static constexpr float memory_decay_floor = 0.01f;
        static constexpr float knowledge_confidence_decay_rate = 0.001f;
        static constexpr float motivation_shift_rate = 0.001f;
        static constexpr float recovery_ceiling_minimum = 0.15f;
    };

   private:
    NpcBehaviorConfig cfg_;
};

}  // namespace econlife
