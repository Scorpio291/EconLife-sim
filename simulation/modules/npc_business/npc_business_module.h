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

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "modules/economy/economy_types.h"  // BoardComposition (complete type for unordered_map value)
#include "modules/npc_business/npc_business_types.h"

namespace econlife {

// Forward declarations -- complete types not needed for references/pointers
struct WorldState;
struct DeltaBuffer;
class DeterministicRNG;

// ---------------------------------------------------------------------------
// Configuration constants for NPC business decisions (from INTERFACE.md)
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// NpcBusinessModule -- ITickModule implementation for quarterly decisions
// ---------------------------------------------------------------------------
class NpcBusinessModule : public ITickModule {
   public:
    explicit NpcBusinessModule(const NpcBusinessConfig& cfg = {}) : cfg_(cfg) {}

    const NpcBusinessConfig& config() const { return cfg_; }

    std::string_view name() const noexcept override { return "npc_business"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    std::vector<std::string_view> runs_after() const override { return {"price_engine"}; }

    std::vector<std::string_view> runs_before() const override { return {"npc_behavior"}; }

    bool is_province_parallel() const noexcept override { return true; }

    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override;

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Board composition management ---
    // Module-internal state: board compositions keyed by business_id.
    void set_board_composition(uint32_t business_id, const BoardComposition& board);
    const BoardComposition* get_board_composition(uint32_t business_id) const;

    // --- Decision evaluation (exposed for testing) ---

    // Compute the working capital floor for a business.
    float compute_working_capital_floor(const NPCBusiness& biz) const;

    // Compute available investment cash (cash minus working capital floor).
    float compute_available_cash(const NPCBusiness& biz) const;

    // Compute monthly operating costs approximation.
    float compute_monthly_operating_costs(const NPCBusiness& biz) const;

    // Compute profit margin (revenue - cost) / revenue.
    static float compute_profit_margin(const NPCBusiness& biz);

    // Evaluate a quarterly decision for a single business.
    // Returns the decision result. Uses RNG for probabilistic decisions.
    BusinessDecisionResult evaluate_decision(const NPCBusiness& biz, const BoardComposition* board,
                                             DeterministicRNG& rng) const;

    // Check whether the board approves a decision.
    // Captured boards (independence < 0.25) always approve.
    // Independent boards (independence > 0.70) may block risky expansion.
    bool check_board_approval(const BoardComposition* board, const BusinessDecisionResult& decision,
                              DeterministicRNG& rng) const;

    // Check if this tick is a decision tick for the given business.
    static bool is_decision_tick(const NPCBusiness& biz, uint32_t current_tick);

    // Check if a business is player-owned (should be skipped).
    static bool is_player_owned(const NPCBusiness& biz, const WorldState& state);

   private:
    NpcBusinessConfig cfg_;
    // Internal board composition storage (keyed by business_id)
    std::unordered_map<uint32_t, BoardComposition> board_compositions_;

    // Apply a decision result to the delta buffer.
    static void apply_decision_to_deltas(const NPCBusiness& biz,
                                         const BusinessDecisionResult& result, DeltaBuffer& delta);
};

}  // namespace econlife
