#pragma once

// Government Budget Module — sequential tick module that processes quarterly
// tax collection, spending allocation, intergovernmental transfers, and
// fiscal stress across national, provincial, and city levels.
//
// See docs/interfaces/government_budget/INTERFACE.md for the canonical specification.

#include <cmath>
#include <map>
#include <string_view>
#include <vector>

#include "core/tick/tick_module.h"
#include "modules/government_budget/budget_types.h"

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;
struct NPCBusiness;

// ---------------------------------------------------------------------------
// GovernmentBudgetModule — ITickModule implementation for fiscal operations
// ---------------------------------------------------------------------------
class GovernmentBudgetModule : public ITickModule {
   public:
    std::string_view name() const noexcept override { return "government_budget"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    std::vector<std::string_view> runs_after() const override { return {"financial_distribution"}; }

    // End of Pass 1 chain; no current-tick dependents.
    std::vector<std::string_view> runs_before() const override { return {}; }

    bool is_province_parallel() const noexcept override { return false; }

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Internal budget storage ---
    // Module owns GovernmentBudget records because WorldState does not yet
    // carry budget fields on Province/Nation (see budget_types.h FIELD ADDITION notes).

    std::vector<GovernmentBudget>& budgets() { return budgets_; }
    const std::vector<GovernmentBudget>& budgets() const { return budgets_; }
    void add_budget(const GovernmentBudget& budget) { budgets_.push_back(budget); }

    // --- Static utility functions exposed for testing ---

    // Compute corporate tax revenue for a province over a quarter.
    // Sums (business.revenue_per_tick * ticks_per_quarter * tax_rate) for all
    // non-criminal businesses in the given province.
    static float compute_corporate_tax(const std::vector<NPCBusiness>& businesses, float tax_rate,
                                       uint32_t province_id);

    // Compute infrastructure rating change for one quarter:
    //   new_rating = (current_rating - decay_rate) + (spending_actual / investment_scale)
    //   clamped to [0.0, 1.0]
    static float compute_infrastructure_change(float current_rating, float spending_actual,
                                               float decay_rate, float investment_scale);

    // Pro-rate spending allocations when cash is insufficient.
    // Each category gets: allocation * (available_cash / total_allocations).
    // If total_allocations == 0 or available_cash <= 0, all categories get 0.
    static std::map<SpendingCategory, float> prorate_spending(
        const std::map<SpendingCategory, float>& allocations, float available_cash);

    // Compute debt-to-revenue ratio.
    // Returns infinity if total_revenue == 0 and accumulated_debt > 0.
    // Returns 0.0 if both are 0.
    static float compute_debt_to_revenue_ratio(float accumulated_debt, float total_revenue);

    // Returns true if current_tick is a quarterly boundary (multiple of ticks_per_quarter).
    // Tick 0 is NOT treated as quarterly to avoid processing before the world is initialized.
    static bool is_quarterly_tick(uint32_t current_tick);

    // --- Compile-time constants ---

    struct Constants {
        static constexpr uint32_t ticks_per_quarter = 90;
        static constexpr float infrastructure_decay_per_quarter = 0.01f;
        static constexpr float infrastructure_investment_scale = 1000000.0f;
        static constexpr float debt_warning_ratio = 2.0f;
        static constexpr float debt_crisis_ratio = 4.0f;
        static constexpr float city_revenue_fraction = 0.25f;
        static constexpr float corruption_evidence_threshold = 500000.0f;

        // Spending effect scales: how much spending (per unit) affects region conditions.
        static constexpr float spending_stability_scale = 0.0001f;
        static constexpr float spending_crime_scale = 0.0001f;
        static constexpr float spending_inequality_scale = 0.0001f;

        // Cohort tax rate modifiers by income group.
        static constexpr float cohort_mod_working_class = 0.40f;
        static constexpr float cohort_mod_professional = 0.85f;
        static constexpr float cohort_mod_corporate = 1.00f;
        static constexpr float cohort_mod_criminal_adjacent = 0.10f;
    };

   private:
    std::vector<GovernmentBudget> budgets_;

    // --- Internal processing steps ---

    // Step 1: Quarterly tax collection (corporate, income, property).
    void process_quarterly_taxes(const WorldState& state, DeltaBuffer& delta);

    // Step 2: Intergovernmental transfers (national -> provincial -> city).
    void process_intergovernmental_transfers();

    // Step 3: Execute spending for all budgets (pro-rate if cash constrained).
    void execute_spending(DeltaBuffer& delta);

    // Step 4: Update province infrastructure ratings (decay + investment).
    // Writes RegionDelta.stability_delta proportional to the infrastructure change.
    void update_infrastructure(const WorldState& state, DeltaBuffer& delta);

    // Step 5: Check fiscal health and queue consequences.
    void check_fiscal_health(DeltaBuffer& delta);

    // Step 6: Apply spending effects on region conditions.
    void apply_spending_effects(DeltaBuffer& delta);

    // --- Lookup helpers ---

    // Find a budget by level and jurisdiction_id. Returns nullptr if not found.
    GovernmentBudget* find_budget(GovernmentLevel level, uint32_t jurisdiction_id);
    const GovernmentBudget* find_budget(GovernmentLevel level, uint32_t jurisdiction_id) const;
};

}  // namespace econlife
