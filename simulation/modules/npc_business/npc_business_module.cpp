// NPC Business Module -- implementation.
// See npc_business_module.h for class declarations and
// docs/interfaces/npc_business/INTERFACE.md for the canonical specification.
//
// Quarterly strategic decision matrix per BusinessProfile:
//   cost_cutter:          Reduce costs aggressively (layoffs, cheaper inputs)
//   quality_player:       Invest in R&D and quality improvements
//   fast_expander:        Grow market share, enter adjacent markets
//   defensive_incumbent:  Maintain stability, increase dividends, lobby

#include "modules/npc_business/npc_business_module.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/rng/deterministic_rng.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"  // PlayerCharacter complete type
#include "core/world_state/world_state.h"

namespace econlife {

// ===========================================================================
// Per-profile decision helpers (anonymous namespace, defined before use)
// ===========================================================================

namespace {

void apply_cost_cutter_strategy(const NPCBusiness& biz, BusinessDecisionResult& result,
                                float margin, float cash_months, float /*available_cash*/,
                                DeterministicRNG& rng, const NpcBusinessConfig& cfg) {
    // If cash is critical (< 2 months of operating costs), reduce workforce by 10%.
    if (cash_months < cfg.cash_critical_months) {
        result.contract = true;
        // Negative hiring_target_change = layoffs.
        result.hiring_target_change = -static_cast<int32_t>(std::max(
            1.0f, biz.cost_per_tick * cfg.cost_cutter_layoff_fraction * 10.0f));
        // Cost reduction from layoffs.
        result.cost_per_tick_delta =
            biz.cost_per_tick * -cfg.cost_cutter_layoff_fraction;
    }

    // If market share is below exit threshold, consider market exit.
    if (biz.market_share < cfg.exit_market_threshold) {
        float roll = rng.next_float();
        if (roll < cfg.exit_probability) {
            result.contract = true;
            result.enter_new_market = false;
            result.expand = false;
            // Signal market exit via large negative hiring change.
            result.hiring_target_change = -1000;
            return;
        }
    }

    // Low margin (< 10%) but not critical -- tighten costs modestly.
    if (margin < 0.10f && !result.contract) {
        result.contract = true;
        result.hiring_target_change =
            -static_cast<int32_t>(std::max(1.0f, biz.cost_per_tick * 0.05f * 10.0f));
        result.cost_per_tick_delta = biz.cost_per_tick * -0.05f;
    }

    // Minimal R&D investment for cost cutters.
    result.rd_investment_rate = 0.01f;
}

void apply_quality_player_strategy(const NPCBusiness& biz, BusinessDecisionResult& result,
                                   float margin, float cash_months, float available_cash,
                                   DeterministicRNG& /*rng*/, const NpcBusinessConfig& cfg) {
    // Contraction check first -- if cash is critical, override everything.
    if (cash_months < cfg.cash_critical_months) {
        result.contract = true;
        result.expand = false;
        result.hiring_target_change =
            -static_cast<int32_t>(std::max(1.0f, biz.cost_per_tick * 0.05f * 10.0f));
        result.cash_spent = 0.0f;
        result.rd_investment_rate = 0.0f;
        return;
    }

    // Quality players invest in R&D when they have sufficient cash.
    if (cash_months >= cfg.cash_comfortable_months && available_cash > 0.0f) {
        result.rd_investment_rate = cfg.quality_player_rd_rate;
        float rd_spend = available_cash * result.rd_investment_rate;
        result.cash_spent = rd_spend;
    }

    // If profitable, consider modest expansion.
    if (margin > cfg.expansion_return_threshold &&
        cash_months >= cfg.cash_comfortable_months) {
        result.expand = true;
        float expansion_spend = available_cash * 0.10f;
        result.cash_spent += expansion_spend;
        result.hiring_target_change = static_cast<int32_t>(std::max(1.0f, expansion_spend * 0.01f));
    }

    // Cap total spending at available cash.
    if (result.cash_spent > available_cash) {
        result.cash_spent = available_cash;
    }
}

void apply_fast_expander_strategy(const NPCBusiness& biz, BusinessDecisionResult& result,
                                  float /*margin*/, float cash_months, float available_cash,
                                  DeterministicRNG& /*rng*/, const NpcBusinessConfig& cfg) {
    // Contraction only if truly desperate.
    if (cash_months < cfg.cash_critical_months) {
        result.contract = true;
        result.expand = false;
        result.enter_new_market = false;
        result.hiring_target_change =
            -static_cast<int32_t>(std::max(1.0f, biz.cost_per_tick * 0.05f * 10.0f));
        result.cash_spent = 0.0f;
        result.rd_investment_rate = 0.0f;
        return;
    }

    // Fast expanders invest aggressively when cash is available.
    if (cash_months >= cfg.cash_comfortable_months && available_cash > 0.0f) {
        result.expand = true;

        // Invest up to 40% of available cash in expansion.
        float expansion_spend = available_cash * 0.40f;
        result.cash_spent = expansion_spend;

        // Hire aggressively.
        result.hiring_target_change = static_cast<int32_t>(std::max(1.0f, expansion_spend * 0.02f));

        // Some R&D too.
        result.rd_investment_rate = cfg.fast_expander_rd_rate;
        result.cash_spent += available_cash * result.rd_investment_rate;
    }

    // If market share is high and cash allows, enter adjacent market.
    if (biz.market_share > 0.30f && available_cash > 0.0f &&
        cash_months >= cfg.cash_comfortable_months) {
        result.enter_new_market = true;
        float entry_cost = available_cash * 0.15f;
        result.cash_spent += entry_cost;
    }

    // Cap spending at available cash.
    if (result.cash_spent > available_cash) {
        result.cash_spent = available_cash;
    }
}

void apply_defensive_incumbent_strategy(const NPCBusiness& biz, BusinessDecisionResult& result,
                                        float margin, float cash_months, float available_cash,
                                        DeterministicRNG& /*rng*/, const NpcBusinessConfig& cfg) {
    // Contraction if cash is critical.
    if (cash_months < cfg.cash_critical_months) {
        result.contract = true;
        result.hiring_target_change =
            -static_cast<int32_t>(std::max(1.0f, biz.cost_per_tick * 0.05f * 10.0f));
        result.cash_spent = 0.0f;
        result.rd_investment_rate = 0.0f;
        return;
    }

    // Defensive incumbents focus on stability and dividends.
    if (margin > 0.0f && cash_months >= cfg.cash_comfortable_months) {
        // Minimal R&D to maintain position.
        result.rd_investment_rate = 0.02f;
        float rd_spend = available_cash * result.rd_investment_rate;
        result.cash_spent = rd_spend;
    }

    // If surplus cash, invest in regulatory lobbying.
    if (cash_months >= cfg.cash_surplus_months && available_cash > 0.0f) {
        float lobby_spend = available_cash * 0.05f;
        result.cash_spent += lobby_spend;
    }

    // Cap spending at available cash.
    if (result.cash_spent > available_cash) {
        result.cash_spent = available_cash;
    }
}

}  // anonymous namespace

// ===========================================================================
// NpcBusinessModule -- tick execution
// ===========================================================================

void NpcBusinessModule::execute_province(uint32_t province_idx, const WorldState& state,
                                         DeltaBuffer& province_delta) {
    // Collect NPC businesses in this province, sorted by id ascending
    // for deterministic processing order.
    std::vector<const NPCBusiness*> province_businesses;
    for (const auto& biz : state.npc_businesses) {
        if (biz.province_id == province_idx) {
            province_businesses.push_back(&biz);
        }
    }

    // Sort by id ascending (canonical order per CLAUDE.md).
    std::sort(province_businesses.begin(), province_businesses.end(),
              [](const NPCBusiness* a, const NPCBusiness* b) { return a->id < b->id; });

    // Fork RNG for this province to ensure deterministic province-parallel output.
    DeterministicRNG rng = DeterministicRNG(state.world_seed).fork(province_idx);

    // Process each business.
    for (const NPCBusiness* biz : province_businesses) {
        // Skip player-owned businesses entirely.
        if (is_player_owned(*biz, state)) {
            continue;
        }

        // Skip businesses not on their decision tick.
        if (!is_decision_tick(*biz, state.current_tick)) {
            continue;
        }

        // Look up board composition for this business.
        const BoardComposition* board = get_board_composition(biz->id);

        // Fork RNG per business for deterministic per-business decisions.
        DeterministicRNG biz_rng = rng.fork(biz->id);

        // Evaluate quarterly decision.
        BusinessDecisionResult result = evaluate_decision(*biz, board, biz_rng);

        // Check board approval (may override the decision).
        if (!check_board_approval(board, result, biz_rng)) {
            result.board_approved = false;
            // Board rejected -- reset all actions but still advance decision tick.
            result.expand = false;
            result.contract = false;
            result.enter_new_market = false;
            result.hiring_target_change = 0;
            result.rd_investment_rate = 0.0f;
            result.cash_spent = 0.0f;
            result.cost_per_tick_delta = 0.0f;
        }

        // Apply the decision to deltas.
        apply_decision_to_deltas(*biz, result, province_delta);
    }
}

void NpcBusinessModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Province-parallel modules dispatch through execute_province().
    // This fallback processes all provinces sequentially if called directly.
    for (uint32_t p = 0; p < static_cast<uint32_t>(state.provinces.size()); ++p) {
        execute_province(p, state, delta);
    }
}

// ===========================================================================
// Board composition management
// ===========================================================================

void NpcBusinessModule::set_board_composition(uint32_t business_id, const BoardComposition& board) {
    board_compositions_[business_id] = board;
}

const BoardComposition* NpcBusinessModule::get_board_composition(uint32_t business_id) const {
    auto it = board_compositions_.find(business_id);
    if (it != board_compositions_.end()) {
        return &it->second;
    }
    return nullptr;
}

// ===========================================================================
// Static utility functions
// ===========================================================================

float NpcBusinessModule::compute_working_capital_floor(const NPCBusiness& biz) const {
    // Working capital floor = cost_per_tick * dispatch_period * cash_surplus_months
    // Ensures the business retains enough cash for ongoing operations.
    return biz.cost_per_tick * static_cast<float>(cfg_.dispatch_period) *
           cfg_.cash_surplus_months;
}

float NpcBusinessModule::compute_available_cash(const NPCBusiness& biz) const {
    float floor = compute_working_capital_floor(biz);
    float available = biz.cash - floor;
    return std::max(available, 0.0f);
}

float NpcBusinessModule::compute_monthly_operating_costs(const NPCBusiness& biz) const {
    return biz.cost_per_tick * static_cast<float>(cfg_.dispatch_period);
}

float NpcBusinessModule::compute_profit_margin(const NPCBusiness& biz) {
    if (biz.revenue_per_tick <= 0.0f) {
        return -1.0f;  // No revenue means deeply unprofitable.
    }
    return (biz.revenue_per_tick - biz.cost_per_tick) / biz.revenue_per_tick;
}

// ===========================================================================
// Decision tick and player-owned checks
// ===========================================================================

bool NpcBusinessModule::is_decision_tick(const NPCBusiness& biz, uint32_t current_tick) {
    return current_tick >= biz.strategic_decision_tick;
}

bool NpcBusinessModule::is_player_owned(const NPCBusiness& biz, const WorldState& state) {
    if (state.player == nullptr) {
        return false;
    }
    return biz.owner_id == state.player->id;
}

// ===========================================================================
// Decision evaluation -- dispatches to per-profile strategy helpers
// ===========================================================================

BusinessDecisionResult NpcBusinessModule::evaluate_decision(const NPCBusiness& biz,
                                                            const BoardComposition* /*board*/,
                                                            DeterministicRNG& rng) const {
    BusinessDecisionResult result{};
    result.business_id = biz.id;
    result.board_approved = true;  // default; may be overridden by board check later

    float monthly_costs = compute_monthly_operating_costs(biz);
    float cash_months = (monthly_costs > 0.0f) ? (biz.cash / monthly_costs) : 999.0f;
    float margin = compute_profit_margin(biz);
    float available_cash = compute_available_cash(biz);

    switch (biz.profile) {
        case BusinessProfile::cost_cutter:
            apply_cost_cutter_strategy(biz, result, margin, cash_months, available_cash, rng, cfg_);
            break;

        case BusinessProfile::quality_player:
            apply_quality_player_strategy(biz, result, margin, cash_months, available_cash, rng, cfg_);
            break;

        case BusinessProfile::fast_expander:
            apply_fast_expander_strategy(biz, result, margin, cash_months, available_cash, rng, cfg_);
            break;

        case BusinessProfile::defensive_incumbent:
            apply_defensive_incumbent_strategy(biz, result, margin, cash_months, available_cash,
                                               rng, cfg_);
            break;

        default:
            // Unknown profile -- maintain current operations (no-op).
            break;
    }

    return result;
}

// ===========================================================================
// Board approval check
// ===========================================================================

bool NpcBusinessModule::check_board_approval(const BoardComposition* board,
                                             const BusinessDecisionResult& decision,
                                             DeterministicRNG& rng) const {
    // No board = micro/small business. All decisions auto-approved.
    if (board == nullptr) {
        return true;
    }

    float independence = board->independence_score;

    // Captured board (independence < 0.25) rubber-stamps everything.
    if (independence < cfg_.board_captured_threshold) {
        return true;
    }

    // Only risky decisions (expansion or new market entry) can be blocked.
    bool is_risky = decision.expand || decision.enter_new_market;
    if (!is_risky) {
        return true;
    }

    // The more independent the board, the more likely to block risky moves.
    // Block probability scales with independence above the captured threshold.
    if (independence > cfg_.board_risky_block_threshold) {
        float block_prob = (independence - cfg_.board_captured_threshold) * 0.5f;
        float roll = rng.next_float();
        if (roll < block_prob) {
            return false;  // Board blocks the decision.
        }
    }

    return true;
}

// ===========================================================================
// Apply decision to delta buffer
// ===========================================================================

void NpcBusinessModule::apply_decision_to_deltas(const NPCBusiness& biz,
                                                 const BusinessDecisionResult& result,
                                                 DeltaBuffer& delta) {
    // --- BusinessDelta: deduct cash_spent from the business and update cost_per_tick ---
    // This is the primary financial outcome of every quarterly decision.
    if (result.cash_spent > 0.0f || result.cost_per_tick_delta != 0.0f) {
        BusinessDelta biz_delta{};
        biz_delta.business_id = biz.id;
        if (result.cash_spent > 0.0f) {
            biz_delta.cash_delta = -result.cash_spent;  // investment deducted from business cash
        }
        if (result.cost_per_tick_delta != 0.0f) {
            biz_delta.cost_per_tick_update = biz.cost_per_tick + result.cost_per_tick_delta;
        }
        delta.business_deltas.push_back(biz_delta);
    }

    // Workforce changes (hiring or layoffs).
    if (result.hiring_target_change != 0) {
        NPCDelta npc_delta{};
        npc_delta.npc_id = biz.owner_id;
        // Severance cost for layoffs: negative hiring_target_change * cost factor.
        npc_delta.capital_delta = (result.hiring_target_change < 0)
                                      ? static_cast<float>(-result.hiring_target_change) * -10.0f
                                      : 0.0f;
        delta.npc_deltas.push_back(npc_delta);
    }

    // Market supply adjustments from expansion.
    if (result.expand || result.enter_new_market) {
        MarketDelta market_delta{};
        market_delta.good_id = static_cast<uint32_t>(biz.sector);
        market_delta.region_id = biz.province_id;
        // Expansion increases future supply capacity.
        market_delta.supply_delta = result.cash_spent * 0.001f;
        delta.market_deltas.push_back(market_delta);

        // Significant expansion decision: emit ConsequenceDelta so the consequence
        // system can schedule downstream effects (new facility, competitor response, etc.).
        // new_entry_id encodes the business id; the consequence engine resolves the type.
        ConsequenceDelta expansion_cons{};
        expansion_cons.new_entry_id = biz.id;
        delta.consequence_deltas.push_back(expansion_cons);
    }

    // Market supply adjustments from contraction.
    if (result.contract) {
        MarketDelta market_delta{};
        market_delta.good_id = static_cast<uint32_t>(biz.sector);
        market_delta.region_id = biz.province_id;
        // Contraction decreases supply.
        market_delta.supply_delta = result.cost_per_tick_delta * 10.0f;
        delta.market_deltas.push_back(market_delta);
    }

    // Market exit (hiring_target_change <= -1000 signals full exit).
    // Emits DissolvedBusinessDelta to remove entity from world + ConsequenceDelta
    // for downstream scene card / NPC memory effects.
    if (result.hiring_target_change <= -1000) {
        DissolvedBusinessDelta dissolve{};
        dissolve.business_id = biz.id;
        delta.dissolved_businesses.push_back(dissolve);
        ConsequenceDelta cons_delta{};
        // Use id offset to distinguish exit consequences from expansion consequences.
        cons_delta.new_entry_id = biz.id + 1000000u;
        delta.consequence_deltas.push_back(cons_delta);
    }

    // Defensive incumbent lobbying generates evidence delta.
    if (biz.profile == BusinessProfile::defensive_incumbent && result.cash_spent > 0.0f &&
        !result.contract) {
        EvidenceDelta ev_delta{};
        EvidenceToken token{};
        token.id = biz.id * 1000 + 1;  // synthetic evidence id
        token.type = EvidenceType::documentary;
        token.source_npc_id = biz.owner_id;
        token.target_npc_id = 0;
        token.actionability = 0.2f;
        token.decay_rate = 0.001f;
        token.created_tick = 0;  // will be set by engine at application time
        token.province_id = biz.province_id;
        token.is_active = true;
        ev_delta.new_token = token;
        delta.evidence_deltas.push_back(ev_delta);
    }
}

}  // namespace econlife
