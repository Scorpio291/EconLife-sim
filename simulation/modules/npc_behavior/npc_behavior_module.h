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
// ActionOutcome is defined in core/world_state/npc.h (included above).

// ---------------------------------------------------------------------------
// NpcBehaviorModule — ITickModule implementation for NPC daily behavior
// ---------------------------------------------------------------------------
class NpcBehaviorModule : public ITickModule {
   public:
    explicit NpcBehaviorModule(const NpcBehaviorModuleConfig& mcfg = {},
                               const NpcBehaviorConfig& cfg = {})
        : mcfg_(mcfg), cfg_(cfg) {}

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
    float compute_risk_discount(float exposure_risk, float risk_tolerance) const;

    // Apply relationship modifier for cooperative actions.
    // result = base_ev * (1.0 + trust * trust_ev_bonus)
    static float apply_relationship_modifier(float base_ev, float trust, float trust_ev_bonus);

    // Evaluate a complete action: sum outcome EVs, apply risk discount and
    // optional trust bonus. Returns ActionEvaluation with net_utility.
    ActionEvaluation evaluate_action(const NPC& npc, DailyAction action,
                                     const std::vector<ActionOutcome>& outcomes,
                                     float exposure_risk, float trust_bonus_target,
                                     float trust_ev_bonus) const;

    // Decay all memory entries in an NPC's memory_log.
    // decay = decay * (1.0 - decay_rate). Entries below decay_floor are archived (removed).
    static void decay_memories(NPC& npc_copy, float decay_rate, float decay_floor);

    // Archive the memory entry with the lowest decay value.
    // Used when memory_log is at MAX_MEMORY_ENTRIES capacity.
    static void archive_lowest_decay_memory(std::vector<MemoryEntry>& memory_log);

    // Renormalize motivation weights to sum to 1.0.
    // If all weights are zero, reset to uniform distribution.
    static void renormalize_motivation_weights(MotivationVector& motivations);

    // Clamp relationship trust to [-1.0, 1.0], fear to [0.0, 1.0].
    // Trust cannot exceed recovery_ceiling.
    void clamp_relationship(Relationship& rel) const;

    // Compute worker satisfaction from memory log.
    // Returns 0.0-1.0; based on ratio of positive to negative employment memories.
    static float worker_satisfaction(const NPC& npc);

    // Map MemoryType to the closest OutcomeType for motivation shift.
    static size_t memory_type_to_outcome_index(MemoryType type);

    // --- Daily decision, exposed for tests and observers ---

    // Province conditions the candidate generator reads.
    struct ProvinceContext {
        float crime_rate = 0.0f;
        float stability = 0.0f;
        float employment_rate = 0.0f;       // formal employment rate
        float effective_employment = 0.0f;  // max(formal, informal_wage_floor)
    };

    // One discretionary action the NPC could take today.
    struct ActionCandidate {
        DailyAction action;
        std::vector<ActionOutcome> outcomes;
        float exposure_risk;
        float trust_target;  // -2.0f means no trust modifier
        float capital_cost;  // capital spent if chosen
    };

    // The best affordable candidate. best.net_utility is -1 when none is affordable.
    struct ActionChoice {
        ActionEvaluation best;
        float cost = 0.0f;
    };

    ProvinceContext province_context(const WorldState& state, uint32_t province_idx) const;
    std::vector<ActionCandidate> generate_candidates(const NPC& npc, const WorldState& state,
                                                     const ProvinceContext& ctx) const;
    ActionChoice choose_action(const NPC& npc,
                               const std::vector<ActionCandidate>& candidates) const;
    // The inaction decision (EL-SLICE-N2). An action's net utility is
    //   sum(motivation_weight x probability x magnitude) x risk_discount
    // and the motivation weights are normalised to sum to 1 across the 8
    // OutcomeTypes. Its absolute size therefore carries a factor that comes from
    // that normalisation (a balanced NPC has 1/8 on each motive), not from the
    // action: the same NPC and the same action would score half as much if the
    // enum had 16 entries. A fixed absolute threshold compared against it is not
    // scale-free — at 0.10 it sat above ~97% of every NPC's best option in the
    // game world, before anything had happened.
    //
    // The gate therefore compares like with like: the best action's net utility
    // as a fraction of the NPC's own largest motivation weight, i.e. of what a
    // certain (p = 1), full (m = 1), unrisky payoff on the thing it cares most
    // about would be worth to it. `inaction_threshold` is that fraction. An NPC
    // waits when nothing it can do today is worth that share of what it most
    // wants — typically because no available action serves its dominant motives.
    static float relative_utility(const NPC& npc, float net_utility);
    bool chooses_to_act(const NPC& npc, const ActionChoice& choice) const;
    float inaction_threshold() const { return mcfg_.inaction_threshold; }

   private:
    NpcBehaviorModuleConfig mcfg_;
    NpcBehaviorConfig cfg_;
};

}  // namespace econlife
