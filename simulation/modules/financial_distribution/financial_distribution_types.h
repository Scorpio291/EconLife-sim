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
    // Migrated to FinancialDistributionConfig in core/config/package_config.h.
    // This struct is kept empty for backward compatibility.
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
