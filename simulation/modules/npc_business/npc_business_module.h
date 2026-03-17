#pragma once

// NPC Business Module -- province-parallel tick module that executes
// quarterly strategic decisions for NPC-owned businesses.
//
// Decisions include: expansion, contraction, new product line entry,
// hiring target adjustments, R&D allocation changes, pricing strategy
// updates, and market exit (bankruptcy/dissolution).
//
// Uses BusinessProfile (cost_cutter, quality_player, fast_expander,
// defensive_incumbent) and BoardComposition to determine decision quality.
//
// See docs/interfaces/npc_business/INTERFACE.md for the canonical specification.

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/tick/tick_module.h"
#include "modules/npc_business/npc_business_types.h"
#include "modules/economy/economy_types.h"  // BoardComposition (complete type for unordered_map value)

namespace econlife {

// Forward declarations -- complete types not needed for references/pointers
struct WorldState;
struct DeltaBuffer;
class DeterministicRNG;

// ---------------------------------------------------------------------------
// Configuration constants for NPC business decisions (from INTERFACE.md)
// ---------------------------------------------------------------------------
struct NpcBusinessConstants {
    // Cash thresholds (multiples of monthly operating costs)
    static constexpr float cash_critical_months    = 2.0f;
    static constexpr float cash_comfortable_months = 3.0f;
    static constexpr float cash_surplus_months     = 5.0f;

    // Market exit threshold (market share below which exit is considered)
    static constexpr float exit_market_threshold = 0.05f;

    // Exit probability per quarter when below exit threshold (cost_cutter)
    static constexpr float exit_probability = 0.30f;

    // Expansion return threshold
    static constexpr float expansion_return_threshold = 0.15f;

    // Quarterly decision interval (ticks)
    static constexpr uint32_t ticks_per_quarter = 90;

    // Dispatch offset period (ticks per month for load spreading)
    static constexpr uint32_t dispatch_period = 30;

    // R&D investment rates by profile
    static constexpr float quality_player_rd_rate   = 0.08f;  // 5-10% of cash
    static constexpr float fast_expander_rd_rate    = 0.05f;

    // cost_cutter workforce reduction fraction when cash critical
    static constexpr float cost_cutter_layoff_fraction = 0.10f;

    // Board independence thresholds
    static constexpr float board_captured_threshold    = 0.25f;  // below this = rubber stamp
    static constexpr float board_risky_block_threshold = 0.70f;  // above this = may block risky moves

    // Working capital floor = cost_per_tick * cash_surplus_months * dispatch_period
    // (approximation of monthly operating costs times surplus months)
};

// ---------------------------------------------------------------------------
// NpcBusinessModule -- ITickModule implementation for quarterly decisions
// ---------------------------------------------------------------------------
class NpcBusinessModule : public ITickModule {
public:
    std::string_view name() const noexcept override { return "npc_business"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    std::vector<std::string_view> runs_after() const override {
        return {"price_engine"};
    }

    std::vector<std::string_view> runs_before() const override {
        return {"npc_behavior"};
    }

    bool is_province_parallel() const noexcept override { return true; }

    void execute_province(uint32_t province_idx,
                          const WorldState& state,
                          DeltaBuffer& province_delta) override;

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Board composition management ---
    // Module-internal state: board compositions keyed by business_id.
    void set_board_composition(uint32_t business_id, const BoardComposition& board);
    const BoardComposition* get_board_composition(uint32_t business_id) const;

    // --- Decision evaluation (exposed for testing) ---

    // Compute the working capital floor for a business.
    static float compute_working_capital_floor(const NPCBusiness& biz);

    // Compute available investment cash (cash minus working capital floor).
    static float compute_available_cash(const NPCBusiness& biz);

    // Compute monthly operating costs approximation.
    static float compute_monthly_operating_costs(const NPCBusiness& biz);

    // Compute profit margin (revenue - cost) / revenue.
    static float compute_profit_margin(const NPCBusiness& biz);

    // Evaluate a quarterly decision for a single business.
    // Returns the decision result. Uses RNG for probabilistic decisions.
    static BusinessDecisionResult evaluate_decision(
        const NPCBusiness& biz,
        const BoardComposition* board,
        DeterministicRNG& rng);

    // Check whether the board approves a decision.
    // Captured boards (independence < 0.25) always approve.
    // Independent boards (independence > 0.70) may block risky expansion.
    static bool check_board_approval(const BoardComposition* board,
                                     const BusinessDecisionResult& decision,
                                     DeterministicRNG& rng);

    // Check if this tick is a decision tick for the given business.
    static bool is_decision_tick(const NPCBusiness& biz, uint32_t current_tick);

    // Check if a business is player-owned (should be skipped).
    static bool is_player_owned(const NPCBusiness& biz, const WorldState& state);

private:
    // Internal board composition storage (keyed by business_id)
    std::unordered_map<uint32_t, BoardComposition> board_compositions_;

    // Apply a decision result to the delta buffer.
    static void apply_decision_to_deltas(const NPCBusiness& biz,
                                         const BusinessDecisionResult& result,
                                         DeltaBuffer& delta);
};

}  // namespace econlife
