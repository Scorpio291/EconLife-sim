#pragma once

// Obligation Network Module — sequential tick module that processes obligation
// escalation, demand growth, trust erosion, and status transitions.
//
// See docs/interfaces/obligation_network/INTERFACE.md for the canonical specification.

#include "core/tick/tick_module.h"
#include "modules/obligation_network/obligation_network_types.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;
struct NPC;
struct ObligationNode;

// ---------------------------------------------------------------------------
// ObligationNetworkModule — ITickModule implementation for obligation processing
// ---------------------------------------------------------------------------
class ObligationNetworkModule : public ITickModule {
public:
    std::string_view name() const noexcept override { return "obligation_network"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    bool is_province_parallel() const noexcept override { return false; }

    std::vector<std::string_view> runs_after() const override {
        return {"npc_behavior"};
    }

    std::vector<std::string_view> runs_before() const override {
        return {"trust_updates"};
    }

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Module-internal obligation state tracking ---
    // WorldState ObligationNode is minimal; this module tracks extended state.
    struct ObligationState {
        uint32_t obligation_id;
        float current_demand;
        float original_value;
        uint32_t deadline_tick;
        ObligationStatus status;
        std::vector<EscalationStep> history;
        uint32_t creditor_npc_id;
    };

    std::vector<ObligationState>& obligation_states() { return obligation_states_; }
    const std::vector<ObligationState>& obligation_states() const { return obligation_states_; }

    // --- Static utility functions (exposed for testing) ---

    // Compute demand growth per tick.
    // growth = creditor_urgency * escalation_rate_base * (1.0 + player_wealth_factor)
    static float compute_demand_growth(float creditor_urgency, float escalation_rate_base,
                                        float player_wealth_factor);

    // Compute player wealth factor from visible net worth.
    // factor = min(visible_net_worth / wealth_reference_scale, max_wealth_factor)
    static float compute_player_wealth_factor(float visible_net_worth,
                                               float wealth_reference_scale,
                                               float max_wealth_factor);

    // Extract highest motivation weight as creditor urgency.
    static float compute_creditor_urgency(const float* motivation_weights, size_t count);

    // Determine escalation status from current_demand / original_value ratio.
    static ObligationStatus evaluate_escalation(float current_demand, float original_value,
                                                 float escalation_threshold,
                                                 float critical_threshold);

    // Check if hostile action should trigger.
    static bool should_trigger_hostile(ObligationStatus status, float risk_tolerance,
                                        float hostile_threshold);

    // Compute trust erosion for overdue ticks.
    // erosion = overdue_ticks * trust_erosion_per_tick
    static float compute_trust_erosion(uint32_t overdue_ticks, float trust_erosion_per_tick);

    // --- Constants ---
    struct Constants {
        static constexpr float escalation_rate_base = 0.001f;
        static constexpr float escalation_threshold = 1.5f;    // current_demand / original_value
        static constexpr float critical_threshold = 3.0f;
        static constexpr float hostile_action_threshold = 0.7f; // creditor risk_tolerance
        static constexpr float wealth_reference_scale = 1000000.0f;
        static constexpr float max_wealth_factor = 2.0f;
        static constexpr float trust_erosion_per_tick = -0.001f;
        static constexpr uint32_t orphan_obligation_timeout_ticks = 180;
    };

private:
    std::vector<ObligationState> obligation_states_;

    // Find creditor NPC in world state. Returns nullptr if not found or dead.
    const NPC* find_creditor(const WorldState& state, uint32_t creditor_npc_id) const;
};

}  // namespace econlife
