#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace econlife {

// =============================================================================
// Government Budget Types — TDD §31
// =============================================================================

// --- §31.2 — GovernmentLevel ---

enum class GovernmentLevel : uint8_t {
    city     = 0,  // Mayor, City Council
    province = 1,  // Regional Governor, Provincial Legislature
    national = 2,  // Head of State, National Legislature / Cabinet
};

// --- §31.2 — SpendingCategory ---

enum class SpendingCategory : uint8_t {
    public_services      = 0,  // health, education, sanitation, emergency services
    infrastructure       = 1,  // roads, bridges, utilities, transit
    law_enforcement      = 2,  // police, courts, corrections
    social_programs      = 3,  // welfare, housing assistance, unemployment
    public_sector_wages  = 4,  // salaries for all government employees this level
    debt_servicing       = 5,  // interest on outstanding bonds/debt
    intergovernmental    = 6,  // transfers to lower levels (nation -> province -> city)
    discretionary        = 7,  // unallocated; player-directed special projects,
                                // grants, subsidies; highest corruption surface
};

// --- §31.2 — GovernmentBudget ---

struct GovernmentBudget {
    GovernmentLevel level;
    uint32_t        jurisdiction_id;  // province_id for provincial; 0 for national;
                                       // city_id for city-level

    // --- Revenue (per simulated quarter = 90 ticks) ---
    float revenue_own_taxes;          // collected from tax base this level controls
                                       // (see tax collection rules §31.3)
    float revenue_transfers_in;       // received from higher level this quarter
    float revenue_other;              // fees, fines, asset sales, bond proceeds

    float total_revenue;              // sum of above; recomputed at quarter end

    // --- Expenditure (per quarter) ---
    std::map<SpendingCategory, float> spending_allocations;
                                       // player/NPC sets target allocations;
                                       // actual spending may be less if revenue insufficient
    std::map<SpendingCategory, float> spending_actual;
                                       // what was actually paid; may differ from
                                       // allocation if cash constrained

    float total_expenditure;          // sum of spending_actual; recomputed at quarter end

    // --- Balance ---
    float surplus_deficit;            // total_revenue - total_expenditure; positive = surplus
    float accumulated_debt;           // cumulative deficit carried forward; serviced via
                                       // debt_servicing spending category

    // --- Cash position ---
    float cash;                        // liquid balance; revenue credited here;
                                       // expenditures debited here

    // --- Fiscal health indicators (derived; updated quarterly) ---
    float debt_to_revenue_ratio;      // accumulated_debt / total_revenue;
                                       // > config.fiscal.debt_warning_ratio (default 2.0)
                                       //   -> fiscal_pressure_warning
                                       // > config.fiscal.debt_crisis_ratio (default 4.0)
                                       //   -> fiscal_crisis consequence
    float deficit_to_revenue_ratio;   // surplus_deficit / total_revenue (negative = deficit);
                                       // feeds bond market credit_rating at national level (§17.2)

    // Invariants:
    //   total_revenue == revenue_own_taxes + revenue_transfers_in + revenue_other
    //   total_expenditure == sum(spending_actual)
    //   surplus_deficit == total_revenue - total_expenditure
    //   debt_to_revenue_ratio == accumulated_debt / total_revenue (when total_revenue > 0)
};

// --- §31.2 — CityBudget ---

struct CityBudget {
    uint32_t city_id;
    std::string city_name;
    GovernmentBudget budget;
    uint32_t governing_office_id;     // references PoliticalOffice (mayor or city_council)
};

// FIELD ADDITION to Nation:
//   GovernmentBudget national_budget;
//
// FIELD ADDITION to Province:
//   GovernmentBudget provincial_budget;
//   std::vector<CityBudget> city_budgets;   // one per city/district; V1: 1-3 cities per province
//
// FIELD ADDITION to Province (§31.3):
//   float provincial_business_tax_rate;  // set by scenario file; modifiable by provincial legislation
//   float property_tax_revenue_estimate; // seeded at world gen; placeholder until real estate specced

}  // namespace econlife
