#pragma once

// Financial distribution module types.
// Core financial types (StockListing, EquityGrant) are in economy/financial_types.h.
// This header defines distribution-specific types.

#include <cstdint>
#include <vector>

#include "modules/economy/economy_types.h"

namespace econlife {

// ---------------------------------------------------------------------------
// FinancialDistributionConstants — config-driven constants for the module
//
// In production these would be loaded from simulation_config.json -> business.
// For V1 bootstrap these are compile-time defaults matching the interface spec.
// ---------------------------------------------------------------------------
struct FinancialDistributionConstants {
    // Ticks per quarter (365 / 4 ~ 91 ticks).
    static constexpr uint32_t ticks_per_quarter = 91;

    // Maximum ticks of deferred salary before wage theft memory is generated.
    static constexpr uint32_t deferred_salary_max_ticks = 30;

    // Monthly reporting threshold for owner's draw. Draws above this per month
    // generate a suspicious_transaction evidence token.
    static constexpr float draw_reporting_threshold = 20000.0f;

    // Ticks per month (used for draw accumulation tracking).
    static constexpr uint32_t ticks_per_month = 30;

    // Working capital floor multiplier: business must retain
    // cost_per_tick * cash_surplus_months in cash after dividend payout.
    static constexpr float cash_surplus_months = 5.0f;

    // Board rubber-stamp threshold: boards with independence_score below this
    // auto-approve all compensation decisions.
    static constexpr float board_rubber_stamp_threshold = 0.3f;

    // Board approval bonus threshold: bonus_rate above this requires board
    // approval at medium/large scale.
    static constexpr float board_approval_bonus_threshold = 0.25f;

    // Tax withholding rate (simplified flat rate for V1).
    static constexpr float default_tax_withholding_rate = 0.20f;

    // Owner's draw default fraction of profit for micro businesses per tick.
    static constexpr float owners_draw_fraction = 0.5f;

    // Emotional weight for wage theft memory entry.
    static constexpr float wage_theft_emotional_weight = -0.6f;
};

// ---------------------------------------------------------------------------
// BusinessCompensationRecord — per-business compensation state stored
// internally in the module (not on WorldState).
//
// Since WorldState does not carry ExecutiveCompensation or BoardComposition
// per business, the module maintains its own records (analogous to how
// LaborMarketModule stores employment records).
// ---------------------------------------------------------------------------
struct BusinessCompensationRecord {
    uint32_t business_id = 0;
    BusinessScale scale = BusinessScale::micro;
    ExecutiveCompensation compensation{};
    BoardComposition board{};

    // Accumulated owner's draw this month (reset every ticks_per_month ticks).
    float monthly_draw_accumulator = 0.0f;
    uint32_t draw_accumulator_reset_tick = 0;

    // Deferred salary tracking: number of consecutive ticks salary was deferred.
    uint32_t deferred_salary_ticks = 0;

    // Retained earnings: accumulated net profit held in the business.
    float retained_earnings = 0.0f;

    // Whether board has approved current bonus rate this quarter.
    bool bonus_approved_this_quarter = false;

    // Whether board has approved dividend payout this quarter.
    bool dividend_approved_this_quarter = false;
};

// Summary of a single tick's financial distribution for a business.
struct DistributionSummary {
    uint32_t business_id = 0;
    float total_revenue = 0.0f;
    float total_wages_paid = 0.0f;
    float total_dividends_paid = 0.0f;
    float tax_withheld = 0.0f;
    float retained_earnings = 0.0f;
};

}  // namespace econlife
