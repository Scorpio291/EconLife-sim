#pragma once

// Financial distribution module types.
// Core financial types (StockListing, EquityGrant) are in economy/financial_types.h.
// This header defines distribution-specific types.

#include <cstdint>

namespace econlife {

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
