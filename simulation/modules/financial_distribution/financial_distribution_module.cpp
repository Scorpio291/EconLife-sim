// Financial Distribution Module — implementation.
// See financial_distribution_module.h for class declarations and
// docs/interfaces/financial_distribution/INTERFACE.md for the canonical specification.
//
// Processing order per business (sorted by business_id ascending):
//   Step 1: Salary payments (deferred salary recovery FIFO, then current salary)
//   Step 2: Owner's draw (micro businesses only)
//   Step 3: Quarterly bonus distribution (from net profit, board approval for medium/large)
//   Step 4: Quarterly dividend payout (from retained earnings, board approval required)
//   Step 5: Equity grant vesting (full_package only, large businesses)
//   Step 6: Update retained earnings

#include "modules/financial_distribution/financial_distribution_module.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"  // PlayerCharacter complete type
#include "core/world_state/world_state.h"

namespace econlife {

// ===========================================================================
// FinancialDistributionModule — tick execution
// ===========================================================================

void FinancialDistributionModule::execute_province(uint32_t province_idx, const WorldState& state,
                                                   DeltaBuffer& province_delta) {
    // Collect businesses in this province, sorted by business_id ascending
    // for deterministic processing order.
    std::vector<const NPCBusiness*> province_businesses;
    for (const auto& biz : state.npc_businesses) {
        if (biz.province_id == province_idx) {
            province_businesses.push_back(&biz);
        }
    }

    // Sort by business_id ascending (canonical order per CLAUDE.md).
    std::sort(province_businesses.begin(), province_businesses.end(),
              [](const NPCBusiness* a, const NPCBusiness* b) { return a->id < b->id; });

    // Process each business through the distribution pipeline.
    for (const NPCBusiness* biz : province_businesses) {
        // Find or auto-create compensation record for this business.
        BusinessCompensationRecord* record = find_compensation_record(biz->id);
        if (!record) {
            // Auto-create: determine scale from worker count proxy (revenue_per_tick).
            BusinessCompensationRecord new_rec{};
            new_rec.business_id = biz->id;
            // Scale heuristic: micro < 5 workers, small < 20, medium < 100, large >= 100.
            // Use revenue_per_tick as proxy: micro < 100, small < 500, medium < 2000.
            if (biz->revenue_per_tick < 100.0f) {
                new_rec.scale = BusinessScale::micro;
                new_rec.compensation.mechanism = CompensationMechanism::owners_draw;
            } else if (biz->revenue_per_tick < 500.0f) {
                new_rec.scale = BusinessScale::small;
                new_rec.compensation.mechanism = CompensationMechanism::salary_only;
                new_rec.compensation.salary_per_tick = biz->revenue_per_tick * 0.3f;
            } else if (biz->revenue_per_tick < 2000.0f) {
                new_rec.scale = BusinessScale::medium;
                new_rec.compensation.mechanism = CompensationMechanism::salary_bonus;
                new_rec.compensation.salary_per_tick = biz->revenue_per_tick * 0.25f;
                new_rec.compensation.bonus_rate = 0.10f;
            } else {
                new_rec.scale = BusinessScale::large;
                new_rec.compensation.mechanism = CompensationMechanism::salary_bonus;
                new_rec.compensation.salary_per_tick = biz->revenue_per_tick * 0.20f;
                new_rec.compensation.bonus_rate = 0.15f;
            }
            compensation_records_.push_back(new_rec);
            record = &compensation_records_.back();
        }

        // Validate compensation mechanism for scale; fall back to salary_only
        // if invalid (per interface spec failure mode).
        if (!is_mechanism_valid_for_scale(record->compensation.mechanism, record->scale)) {
            record->compensation.mechanism = CompensationMechanism::salary_only;
        }

        // Step 1: Salary payments (all mechanisms except owners_draw).
        if (record->compensation.mechanism != CompensationMechanism::owners_draw) {
            process_salary_payments(*biz, *record, state, province_delta);
        }

        // Step 2: Owner's draw (micro only).
        if (record->compensation.mechanism == CompensationMechanism::owners_draw) {
            process_owners_draw(*biz, *record, state, province_delta);
        }

        // Quarterly processing (bonus, dividend) — only on quarter boundaries.
        bool is_quarter_tick =
            (state.current_tick > 0) &&
            (state.current_tick % FinancialDistributionConstants::ticks_per_quarter == 0);

        if (is_quarter_tick) {
            // Reset quarterly approval flags at quarter boundary.
            record->bonus_approved_this_quarter = false;
            record->dividend_approved_this_quarter = false;

            // Check board approval for medium/large businesses at quarter boundary.
            if (record->scale == BusinessScale::medium || record->scale == BusinessScale::large) {
                bool approved = is_board_approved(record->board, state.current_tick);
                record->bonus_approved_this_quarter = approved;
                record->dividend_approved_this_quarter = approved;
            } else {
                // Small businesses auto-approve.
                record->bonus_approved_this_quarter = true;
                record->dividend_approved_this_quarter = true;
            }

            // Step 3: Quarterly bonus.
            if (record->compensation.mechanism == CompensationMechanism::salary_bonus ||
                record->compensation.mechanism == CompensationMechanism::full_package) {
                process_quarterly_bonus(*biz, *record, state, province_delta);
            }

            // Step 4: Quarterly dividend.
            if (record->compensation.mechanism == CompensationMechanism::salary_dividend ||
                record->compensation.mechanism == CompensationMechanism::full_package) {
                process_quarterly_dividend(*biz, *record, state, province_delta);
            }
        }

        // Step 5: Equity vesting (every tick, not just quarterly).
        if (record->compensation.mechanism == CompensationMechanism::full_package) {
            process_equity_vesting(*biz, *record, state, province_delta);
        }

        // Step 6: Update retained earnings from this tick's net income.
        update_retained_earnings(*biz, *record);
    }
}

void FinancialDistributionModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Province-parallel modules dispatch through execute_province().
    // This fallback processes all provinces sequentially if called directly.
    for (uint32_t p = 0; p < static_cast<uint32_t>(state.provinces.size()); ++p) {
        execute_province(p, state, delta);
    }
}

// ===========================================================================
// Step 1: Salary Payments
// ===========================================================================

void FinancialDistributionModule::process_salary_payments(const NPCBusiness& business,
                                                          BusinessCompensationRecord& record,
                                                          const WorldState& state,
                                                          DeltaBuffer& delta) {
    float salary = record.compensation.salary_per_tick;
    if (salary <= 0.0f)
        return;

    float available_cash = business.cash;

    // FIFO: Pay deferred salary first, then current salary.
    float deferred = business.deferred_salary_liability;
    float total_owed = deferred + salary;

    if (available_cash >= total_owed) {
        // Can pay everything: deferred + current salary.
        float total_payment = total_owed;

        // Apply tax withholding.
        float tax = total_payment * FinancialDistributionConstants::default_tax_withholding_rate;
        float net_payment = total_payment - tax;

        // Emit payment to owner.
        if (state.player && business.owner_id == state.player->id) {
            if (delta.player_delta.wealth_delta.has_value()) {
                delta.player_delta.wealth_delta =
                    delta.player_delta.wealth_delta.value() + net_payment;
            } else {
                delta.player_delta.wealth_delta = net_payment;
            }
        } else if (business.owner_id != 0) {
            NPCDelta npc_delta{};
            npc_delta.npc_id = business.owner_id;
            npc_delta.capital_delta = net_payment;
            delta.npc_deltas.push_back(npc_delta);
        }

        // Deduct from business cash.
        BusinessDelta biz_delta{};
        biz_delta.business_id = business.id;
        biz_delta.cash_delta = -total_payment;
        delta.business_deltas.push_back(biz_delta);

        // Reset deferred state.
        record.deferred_salary_ticks = 0;

    } else if (available_cash > 0.0f) {
        // Partial payment: pay what we can. Deferred salary paid first (FIFO).
        float payment = available_cash;

        float tax = payment * FinancialDistributionConstants::default_tax_withholding_rate;
        float net_payment = payment - tax;

        if (state.player && business.owner_id == state.player->id) {
            if (delta.player_delta.wealth_delta.has_value()) {
                delta.player_delta.wealth_delta =
                    delta.player_delta.wealth_delta.value() + net_payment;
            } else {
                delta.player_delta.wealth_delta = net_payment;
            }
        } else if (business.owner_id != 0) {
            NPCDelta npc_delta{};
            npc_delta.npc_id = business.owner_id;
            npc_delta.capital_delta = net_payment;
            delta.npc_deltas.push_back(npc_delta);
        }

        // Deduct from business cash.
        BusinessDelta biz_delta{};
        biz_delta.business_id = business.id;
        biz_delta.cash_delta = -payment;
        delta.business_deltas.push_back(biz_delta);

        // Remaining amount becomes new deferred liability.
        record.deferred_salary_ticks++;

    } else {
        // No cash at all. Full deferral.
        record.deferred_salary_ticks++;
    }

    // Check for sustained deferral exceeding threshold — generate wage theft memory.
    if (record.deferred_salary_ticks > FinancialDistributionConstants::deferred_salary_max_ticks) {
        if (business.owner_id != 0) {
            NPCDelta wage_theft_delta{};
            wage_theft_delta.npc_id = business.owner_id;

            MemoryEntry mem{};
            mem.tick_timestamp = state.current_tick;
            mem.type = MemoryType::witnessed_wage_theft;
            mem.subject_id = business.id;
            mem.emotional_weight = FinancialDistributionConstants::wage_theft_emotional_weight;
            mem.decay = 1.0f;
            mem.is_actionable = true;

            wage_theft_delta.new_memory_entry = mem;
            delta.npc_deltas.push_back(wage_theft_delta);
        }
    }
}

// ===========================================================================
// Step 2: Owner's Draw (micro businesses only)
// ===========================================================================

void FinancialDistributionModule::process_owners_draw(const NPCBusiness& business,
                                                      BusinessCompensationRecord& record,
                                                      const WorldState& state, DeltaBuffer& delta) {
    if (record.scale != BusinessScale::micro)
        return;

    // Compute draw amount: fraction of available profit.
    float profit_this_tick = business.revenue_per_tick - business.cost_per_tick;
    if (profit_this_tick <= 0.0f)
        return;

    float draw_amount = profit_this_tick * FinancialDistributionConstants::owners_draw_fraction;

    // Cannot draw more than available cash.
    if (draw_amount > business.cash) {
        draw_amount = business.cash;
    }
    if (draw_amount <= 0.0f)
        return;

    // Reset monthly draw accumulator if we've crossed a month boundary.
    if (state.current_tick >=
        record.draw_accumulator_reset_tick + FinancialDistributionConstants::ticks_per_month) {
        record.monthly_draw_accumulator = 0.0f;
        record.draw_accumulator_reset_tick = state.current_tick;
    }

    // Accumulate draw for suspicious transaction tracking.
    record.monthly_draw_accumulator += draw_amount;

    // Pay the draw to the owner.
    if (state.player && business.owner_id == state.player->id) {
        if (delta.player_delta.wealth_delta.has_value()) {
            delta.player_delta.wealth_delta = delta.player_delta.wealth_delta.value() + draw_amount;
        } else {
            delta.player_delta.wealth_delta = draw_amount;
        }
    } else if (business.owner_id != 0) {
        NPCDelta npc_delta{};
        npc_delta.npc_id = business.owner_id;
        npc_delta.capital_delta = draw_amount;
        delta.npc_deltas.push_back(npc_delta);
    }

    // Deduct from business cash.
    {
        BusinessDelta biz_delta{};
        biz_delta.business_id = business.id;
        biz_delta.cash_delta = -draw_amount;
        delta.business_deltas.push_back(biz_delta);
    }

    // Check if monthly draw exceeds reporting threshold — generate evidence.
    if (record.monthly_draw_accumulator >
        FinancialDistributionConstants::draw_reporting_threshold) {
        EvidenceDelta ev_delta{};
        EvidenceToken token{};
        token.id = business.id * 1000 + state.current_tick % 1000;  // deterministic ID
        token.type = EvidenceType::financial;
        token.source_npc_id = business.owner_id;
        token.target_npc_id = business.owner_id;
        token.actionability = 0.4f;  // moderate actionability
        token.decay_rate = 0.01f;
        token.created_tick = state.current_tick;
        token.province_id = business.province_id;
        token.is_active = true;

        ev_delta.new_token = token;
        delta.evidence_deltas.push_back(ev_delta);
    }
}

// ===========================================================================
// Step 3: Quarterly Bonus Distribution
// ===========================================================================

void FinancialDistributionModule::process_quarterly_bonus(const NPCBusiness& business,
                                                          BusinessCompensationRecord& record,
                                                          const WorldState& state,
                                                          DeltaBuffer& delta) {
    float bonus_rate = record.compensation.bonus_rate;
    if (bonus_rate <= 0.0f)
        return;

    // Board approval check for medium/large businesses.
    if (record.scale == BusinessScale::medium || record.scale == BusinessScale::large) {
        // If bonus_rate exceeds threshold, board approval is required.
        if (bonus_rate > FinancialDistributionConstants::board_approval_bonus_threshold) {
            if (!record.bonus_approved_this_quarter) {
                return;  // Board did not approve; skip bonus.
            }
        }
    }

    // Compute quarterly net profit.
    float net_profit = compute_quarterly_net_profit(
        business.revenue_per_tick, business.cost_per_tick, record.compensation.salary_per_tick);

    if (net_profit <= 0.0f)
        return;

    float bonus_amount = net_profit * bonus_rate;

    // Cannot pay more than available cash (after salary).
    if (bonus_amount > business.cash) {
        bonus_amount = business.cash;
    }
    if (bonus_amount <= 0.0f)
        return;

    // Apply tax withholding.
    float tax = bonus_amount * FinancialDistributionConstants::default_tax_withholding_rate;
    float net_bonus = bonus_amount - tax;

    // Pay bonus to owner.
    if (state.player && business.owner_id == state.player->id) {
        if (delta.player_delta.wealth_delta.has_value()) {
            delta.player_delta.wealth_delta = delta.player_delta.wealth_delta.value() + net_bonus;
        } else {
            delta.player_delta.wealth_delta = net_bonus;
        }
    } else if (business.owner_id != 0) {
        NPCDelta npc_delta{};
        npc_delta.npc_id = business.owner_id;
        npc_delta.capital_delta = net_bonus;
        delta.npc_deltas.push_back(npc_delta);
    }

    // Deduct from business cash.
    {
        BusinessDelta biz_delta{};
        biz_delta.business_id = business.id;
        biz_delta.cash_delta = -bonus_amount;
        delta.business_deltas.push_back(biz_delta);
    }
}

// ===========================================================================
// Step 4: Quarterly Dividend Payout
// ===========================================================================

void FinancialDistributionModule::process_quarterly_dividend(const NPCBusiness& business,
                                                             BusinessCompensationRecord& record,
                                                             const WorldState& state,
                                                             DeltaBuffer& delta) {
    float yield_target = record.compensation.dividend_yield_target;
    if (yield_target <= 0.0f)
        return;

    // Board approval required for medium/large businesses.
    if (record.scale == BusinessScale::medium || record.scale == BusinessScale::large) {
        if (!record.dividend_approved_this_quarter) {
            return;  // Board did not approve.
        }
    }

    if (record.retained_earnings <= 0.0f)
        return;

    // Compute target payout from retained earnings.
    float target_payout = record.retained_earnings * yield_target;

    // Working capital floor: dividend cannot reduce cash below this.
    float working_capital_floor = compute_working_capital_floor(business.cost_per_tick);
    float max_payout = business.cash - working_capital_floor;

    if (max_payout <= 0.0f)
        return;

    float actual_payout = std::min(target_payout, max_payout);
    if (actual_payout <= 0.0f)
        return;

    // Deduct from retained earnings.
    record.retained_earnings -= actual_payout;
    if (record.retained_earnings < 0.0f) {
        record.retained_earnings = 0.0f;
    }

    // Apply tax withholding.
    float tax = actual_payout * FinancialDistributionConstants::default_tax_withholding_rate;
    float net_dividend = actual_payout - tax;

    // Pay dividend to owner.
    if (state.player && business.owner_id == state.player->id) {
        if (delta.player_delta.wealth_delta.has_value()) {
            delta.player_delta.wealth_delta =
                delta.player_delta.wealth_delta.value() + net_dividend;
        } else {
            delta.player_delta.wealth_delta = net_dividend;
        }
    } else if (business.owner_id != 0) {
        NPCDelta npc_delta{};
        npc_delta.npc_id = business.owner_id;
        npc_delta.capital_delta = net_dividend;
        delta.npc_deltas.push_back(npc_delta);
    }

    // Deduct from business cash.
    {
        BusinessDelta biz_delta{};
        biz_delta.business_id = business.id;
        biz_delta.cash_delta = -actual_payout;
        delta.business_deltas.push_back(biz_delta);
    }
}

// ===========================================================================
// Step 5: Equity Grant Vesting
// ===========================================================================

void FinancialDistributionModule::process_equity_vesting(const NPCBusiness& business,
                                                         BusinessCompensationRecord& record,
                                                         const WorldState& state,
                                                         DeltaBuffer& /* delta */) {
    if (record.scale != BusinessScale::large)
        return;

    for (auto& grant : record.compensation.equity_grants) {
        if (grant.business_id != business.id)
            continue;

        // Past cliff tick?
        if (state.current_tick < grant.cliff_tick)
            continue;

        // Already fully vested?
        if (grant.shares_vested >= grant.shares_granted)
            continue;

        // Advance vesting.
        grant.shares_vested += grant.vesting_rate;
        if (grant.shares_vested > grant.shares_granted) {
            grant.shares_vested = grant.shares_granted;
        }
    }
}

// ===========================================================================
// Step 6: Update Retained Earnings
// ===========================================================================

void FinancialDistributionModule::update_retained_earnings(const NPCBusiness& business,
                                                           BusinessCompensationRecord& record) {
    float net_income = business.revenue_per_tick - business.cost_per_tick;
    // Negative revenue_per_tick treated as zero per interface spec.
    if (business.revenue_per_tick < 0.0f) {
        net_income = -business.cost_per_tick;
    }

    record.retained_earnings += net_income;
    // Retained earnings can go negative (accumulated losses).
}

// ===========================================================================
// Static Utility Functions
// ===========================================================================

bool FinancialDistributionModule::is_mechanism_valid_for_scale(CompensationMechanism mechanism,
                                                               BusinessScale scale) {
    switch (mechanism) {
        case CompensationMechanism::owners_draw:
            return scale == BusinessScale::micro;

        case CompensationMechanism::salary_only:
        case CompensationMechanism::salary_bonus:
            return scale == BusinessScale::small || scale == BusinessScale::medium ||
                   scale == BusinessScale::large;

        case CompensationMechanism::salary_dividend:
            return scale == BusinessScale::medium || scale == BusinessScale::large;

        case CompensationMechanism::full_package:
            return scale == BusinessScale::large;
    }
    return false;
}

float FinancialDistributionModule::compute_quarterly_net_profit(float revenue_per_tick,
                                                                float cost_per_tick,
                                                                float salary_per_tick) {
    // Net profit = (revenue - cost - salary) * ticks_per_quarter.
    float net_per_tick = revenue_per_tick - cost_per_tick - salary_per_tick;
    return net_per_tick * static_cast<float>(FinancialDistributionConstants::ticks_per_quarter);
}

float FinancialDistributionModule::compute_working_capital_floor(float cost_per_tick) {
    // Working capital floor = cost_per_tick * cash_surplus_months * ticks_per_month.
    return cost_per_tick * FinancialDistributionConstants::cash_surplus_months *
           static_cast<float>(FinancialDistributionConstants::ticks_per_month);
}

bool FinancialDistributionModule::is_board_approved(const BoardComposition& board,
                                                    uint32_t current_tick) {
    // Captured boards (independence_score below rubber stamp threshold) auto-approve.
    if (board.independence_score < FinancialDistributionConstants::board_rubber_stamp_threshold) {
        return true;
    }

    // Independent boards approve at scheduled meeting time.
    // If the current tick matches or has passed the next_approval_tick, approval is granted.
    return current_tick >= board.next_approval_tick;
}

// ===========================================================================
// Lookup Helpers
// ===========================================================================

BusinessCompensationRecord* FinancialDistributionModule::find_compensation_record(
    uint32_t business_id) {
    for (auto& rec : compensation_records_) {
        if (rec.business_id == business_id) {
            return &rec;
        }
    }
    return nullptr;
}

const BusinessCompensationRecord* FinancialDistributionModule::find_compensation_record(
    uint32_t business_id) const {
    for (const auto& rec : compensation_records_) {
        if (rec.business_id == business_id) {
            return &rec;
        }
    }
    return nullptr;
}

const NPCBusiness* FinancialDistributionModule::find_business(const WorldState& state,
                                                              uint32_t business_id) {
    for (const auto& biz : state.npc_businesses) {
        if (biz.id == business_id) {
            return &biz;
        }
    }
    return nullptr;
}

}  // namespace econlife
