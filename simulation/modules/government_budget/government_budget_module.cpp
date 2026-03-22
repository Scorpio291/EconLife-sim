// Government Budget Module — implementation.
// See government_budget_module.h for class declarations and
// docs/interfaces/government_budget/INTERFACE.md for the canonical specification.
//
// Processing order per quarterly tick:
//   Step 1: Quarterly tax collection (corporate, income, property)
//   Step 2: Intergovernmental transfers (national -> provincial -> city)
//   Step 3: Spending execution (pro-rate if cash constrained)
//   Step 4: Infrastructure update (decay + investment benefit)
//   Step 5: Fiscal health checks (debt ratio warnings/crises, insolvency)
//   Step 6: Spending effects on region conditions (stability, crime, inequality)

#include "modules/government_budget/government_budget_module.h"
#include "core/world_state/world_state.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"
#include "modules/economy/economy_types.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace econlife {

// ===========================================================================
// GovernmentBudgetModule — tick execution
// ===========================================================================

void GovernmentBudgetModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Non-quarterly ticks: only infrastructure decay and spending effects.
    // Quarterly ticks: full processing pipeline.
    if (is_quarterly_tick(state.current_tick)) {
        process_quarterly_taxes(state, delta);
        process_intergovernmental_transfers();
        execute_spending(delta);
        check_fiscal_health(delta);
        apply_spending_effects(delta);
        // Infrastructure runs last so its delta merges after spending effects.
        update_infrastructure(state, delta);
    }
    // Non-quarterly ticks: apply passive per-tick infrastructure decay as a
    // small negative stability signal.  The decay constant (0.0001) is chosen
    // so that ~90 ticks of neglect (one quarter without investment) produces
    // a cumulative stability drag comparable to one quarter of infrastructure
    // decay (0.01), keeping the two code paths in proportion.
    else {
        for (const auto& budget : budgets_) {
            if (budget.level != GovernmentLevel::province) continue;

            RegionDelta decay_delta{};
            decay_delta.region_id    = budget.jurisdiction_id;
            decay_delta.stability_delta = -0.0001f;
            delta.region_deltas.push_back(decay_delta);
        }
    }
}

// ===========================================================================
// Step 1: Quarterly Tax Collection
// ===========================================================================

void GovernmentBudgetModule::process_quarterly_taxes(const WorldState& state,
                                                      DeltaBuffer& /* delta */) {
    // Collect taxes for the national budget.
    GovernmentBudget* national = find_budget(GovernmentLevel::national, 0);
    if (!national) return;

    float national_corporate_tax = 0.0f;
    float national_income_tax = 0.0f;
    float national_property_tax = 0.0f;

    // Nation-level tax rates. Use first nation if available; fallback to 0.
    float corporate_tax_rate = 0.0f;
    float income_tax_rate = 0.0f;
    if (!state.nations.empty()) {
        corporate_tax_rate = state.nations[0].corporate_tax_rate;
        income_tax_rate = state.nations[0].income_tax_rate_top_bracket;
    }

    // Iterate provinces in ascending id order for determinism.
    // Collect sorted province indices.
    std::vector<uint32_t> province_indices;
    province_indices.reserve(state.provinces.size());
    for (uint32_t i = 0; i < static_cast<uint32_t>(state.provinces.size()); ++i) {
        province_indices.push_back(i);
    }
    std::sort(province_indices.begin(), province_indices.end());

    for (uint32_t prov_idx : province_indices) {
        const Province& prov = state.provinces[prov_idx];

        // --- Corporate tax ---
        // Sum revenue_per_tick * ticks_per_quarter * corporate_tax_rate
        // for all non-criminal businesses in this province.
        float prov_corporate = compute_corporate_tax(
            state.npc_businesses, corporate_tax_rate, prov.id);
        national_corporate_tax += prov_corporate;

        // --- Income tax ---
        // For V1 bootstrap: use a simplified cohort-based estimate.
        // Income tax = sum over income cohort fractions:
        //   median_income_estimate * population * income_tax_rate * cohort_modifier
        // Since we don't have per-province cohort data yet, use demographic fractions
        // from Province.demographics as a proxy.
        if (prov.cohort_stats) {
            float median_income_estimate = 1000.0f;  // placeholder per-tick income
            float pop = static_cast<float>(prov.cohort_stats->total_population);
            float working_fraction = prov.cohort_stats->working_age_fraction;

            // Working class: 60% of working population, modifier 0.40
            float working_class_income = median_income_estimate * pop * working_fraction
                                         * 0.60f * income_tax_rate
                                         * Constants::cohort_mod_working_class
                                         * static_cast<float>(Constants::ticks_per_quarter);

            // Professional: 30% of working population, modifier 0.85
            float professional_income = median_income_estimate * pop * working_fraction
                                        * 0.30f * income_tax_rate
                                        * Constants::cohort_mod_professional
                                        * static_cast<float>(Constants::ticks_per_quarter);

            // Corporate: 10% of working population, modifier 1.00
            float corporate_income = median_income_estimate * pop * working_fraction
                                     * 0.10f * income_tax_rate
                                     * Constants::cohort_mod_corporate
                                     * static_cast<float>(Constants::ticks_per_quarter);

            national_income_tax += working_class_income + professional_income + corporate_income;
        }

        // --- Property tax ---
        // Province.property_tax_revenue_estimate is noted as a FIELD ADDITION
        // in budget_types.h but not yet on the Province struct.
        // For V1 bootstrap: use a fixed per-province estimate based on population.
        if (prov.cohort_stats) {
            float property_estimate = static_cast<float>(prov.cohort_stats->total_population)
                                      * 10.0f;  // placeholder: $10 per capita per quarter
            national_property_tax += property_estimate;
        }
    }

    float total_tax_revenue = national_corporate_tax + national_income_tax + national_property_tax;
    national->revenue_own_taxes = total_tax_revenue;
    national->total_revenue = national->revenue_own_taxes
                            + national->revenue_transfers_in
                            + national->revenue_other;
    national->cash += total_tax_revenue;

    // Provincial budgets receive their fraction via intergovernmental transfers (Step 2),
    // not via direct tax collection in V1 bootstrap.
}

// ===========================================================================
// Step 2: Intergovernmental Transfers
// ===========================================================================

void GovernmentBudgetModule::process_intergovernmental_transfers() {
    GovernmentBudget* national = find_budget(GovernmentLevel::national, 0);
    if (!national) return;

    // National -> Provincial transfers.
    auto it = national->spending_allocations.find(SpendingCategory::intergovernmental);
    float intergovernmental_pool = 0.0f;
    if (it != national->spending_allocations.end()) {
        intergovernmental_pool = it->second;
    }

    if (intergovernmental_pool <= 0.0f) return;

    // Count provincial budgets.
    uint32_t province_count = 0;
    for (const auto& b : budgets_) {
        if (b.level == GovernmentLevel::province) {
            ++province_count;
        }
    }
    if (province_count == 0) return;

    // Equal split (distribution_modifier = 1.0 for V1).
    float per_province = intergovernmental_pool / static_cast<float>(province_count);

    // Process provinces in ascending jurisdiction_id order for determinism.
    std::vector<GovernmentBudget*> prov_budgets;
    for (auto& b : budgets_) {
        if (b.level == GovernmentLevel::province) {
            prov_budgets.push_back(&b);
        }
    }
    std::sort(prov_budgets.begin(), prov_budgets.end(),
              [](const GovernmentBudget* a, const GovernmentBudget* b) {
                  return a->jurisdiction_id < b->jurisdiction_id;
              });

    for (GovernmentBudget* prov : prov_budgets) {
        prov->revenue_transfers_in += per_province;
        prov->cash += per_province;
        prov->total_revenue = prov->revenue_own_taxes
                            + prov->revenue_transfers_in
                            + prov->revenue_other;
    }

    // Debit national cash by total transfers.
    national->cash -= intergovernmental_pool;

    // Provincial -> City transfers (similarly).
    for (GovernmentBudget* prov : prov_budgets) {
        auto prov_ig = prov->spending_allocations.find(SpendingCategory::intergovernmental);
        float prov_transfer_pool = 0.0f;
        if (prov_ig != prov->spending_allocations.end()) {
            prov_transfer_pool = prov_ig->second;
        }
        if (prov_transfer_pool <= 0.0f) continue;

        // Count city budgets for this province.
        uint32_t city_count = 0;
        for (const auto& b : budgets_) {
            if (b.level == GovernmentLevel::city
                && b.jurisdiction_id / 1000 == prov->jurisdiction_id) {
                // Convention: city jurisdiction_id encodes province as id/1000.
                // This is a simplified V1 mapping.
                ++city_count;
            }
        }
        if (city_count == 0) continue;

        float per_city = prov_transfer_pool / static_cast<float>(city_count);
        for (auto& b : budgets_) {
            if (b.level == GovernmentLevel::city
                && b.jurisdiction_id / 1000 == prov->jurisdiction_id) {
                b.revenue_transfers_in += per_city;
                b.cash += per_city;
                b.total_revenue = b.revenue_own_taxes
                                + b.revenue_transfers_in
                                + b.revenue_other;
            }
        }
        prov->cash -= prov_transfer_pool;
    }
}

// ===========================================================================
// Step 3: Spending Execution
// ===========================================================================

void GovernmentBudgetModule::execute_spending(DeltaBuffer& /* delta */) {
    // Process budgets sorted by level (national first, then province, then city)
    // and within level by jurisdiction_id ascending.
    std::vector<GovernmentBudget*> sorted_budgets;
    for (auto& b : budgets_) {
        sorted_budgets.push_back(&b);
    }
    std::sort(sorted_budgets.begin(), sorted_budgets.end(),
              [](const GovernmentBudget* a, const GovernmentBudget* b) {
                  if (static_cast<uint8_t>(a->level) != static_cast<uint8_t>(b->level)) {
                      // national (2) > province (1) > city (0); process national first.
                      return static_cast<uint8_t>(a->level) > static_cast<uint8_t>(b->level);
                  }
                  return a->jurisdiction_id < b->jurisdiction_id;
              });

    for (GovernmentBudget* budget : sorted_budgets) {
        // Pro-rate spending if cash is insufficient.
        budget->spending_actual = prorate_spending(budget->spending_allocations, budget->cash);

        // Compute total expenditure.
        float total_exp = 0.0f;
        for (const auto& [cat, amount] : budget->spending_actual) {
            total_exp += amount;
        }
        budget->total_expenditure = total_exp;

        // Debit cash.
        budget->cash -= total_exp;

        // Compute surplus/deficit.
        budget->surplus_deficit = budget->total_revenue - budget->total_expenditure;

        // Accumulate debt from deficit.
        if (budget->surplus_deficit < 0.0f) {
            budget->accumulated_debt += (-budget->surplus_deficit);
        }
    }
}

// ===========================================================================
// Step 4: Infrastructure Update
// ===========================================================================

void GovernmentBudgetModule::update_infrastructure(const WorldState& state,
                                                   DeltaBuffer& delta) {
    // Process provincial budgets in ascending jurisdiction_id order (determinism).
    std::vector<const GovernmentBudget*> prov_budgets;
    for (const auto& b : budgets_) {
        if (b.level == GovernmentLevel::province) {
            prov_budgets.push_back(&b);
        }
    }
    std::sort(prov_budgets.begin(), prov_budgets.end(),
              [](const GovernmentBudget* a, const GovernmentBudget* b) {
                  return a->jurisdiction_id < b->jurisdiction_id;
              });

    for (const GovernmentBudget* budget : prov_budgets) {
        // Look up the matching Province to get current infrastructure_rating
        // and area_km2.  jurisdiction_id for province-level budgets equals
        // province.id (see budget_types.h).
        const Province* province = nullptr;
        for (const auto& prov : state.provinces) {
            if (prov.id == budget->jurisdiction_id) {
                province = &prov;
                break;
            }
        }
        if (!province) continue;

        // Derive spending_fraction: infrastructure spend as a fraction of
        // total expenditure.  When total_expenditure is 0 (no spending at
        // all), fraction is 0 and the quarter contributes no investment.
        float infra_spend = 0.0f;
        auto it = budget->spending_actual.find(SpendingCategory::infrastructure);
        if (it != budget->spending_actual.end()) {
            infra_spend = it->second;
        }

        // Scale the actual infrastructure spend by province area so that
        // larger provinces require proportionally more investment to achieve
        // the same rating improvement.  area_km2 is used directly as the
        // investment_scale denominator (1 km2 ~ 1 unit of monetary effort).
        // For very small or zero areas fall back to the module constant.
        float area_km2 = province->geography.area_km2;
        float investment_scale = (area_km2 > 0.0f)
            ? area_km2
            : Constants::infrastructure_investment_scale;

        float new_rating = compute_infrastructure_change(
            province->infrastructure_rating,
            infra_spend,
            Constants::infrastructure_decay_per_quarter,
            investment_scale);

        // Province.infrastructure_rating is const on WorldState; write the
        // net change as a stability_delta on the region that owns this
        // province.  Positive infrastructure change -> positive stability;
        // negative (decay with no investment) -> negative stability.
        // The proportionality constant keeps the effect small relative to
        // other stability sources: each full point of infrastructure
        // improvement contributes +0.05 stability (tunable).
        float infra_change = new_rating - province->infrastructure_rating;
        if (infra_change != 0.0f) {
            RegionDelta region_delta{};
            region_delta.region_id     = province->region_id;
            region_delta.stability_delta = infra_change * 0.05f;
            region_delta.infrastructure_rating_delta = infra_change;
            delta.region_deltas.push_back(region_delta);
        }
    }
}

// ===========================================================================
// Step 5: Fiscal Health Checks
// ===========================================================================

void GovernmentBudgetModule::check_fiscal_health(DeltaBuffer& delta) {
    for (auto& budget : budgets_) {
        // Compute debt-to-revenue ratio.
        budget.debt_to_revenue_ratio = compute_debt_to_revenue_ratio(
            budget.accumulated_debt, budget.total_revenue);

        // Compute deficit-to-revenue ratio.
        if (budget.total_revenue > 0.0f) {
            budget.deficit_to_revenue_ratio = budget.surplus_deficit / budget.total_revenue;
        } else {
            budget.deficit_to_revenue_ratio = (budget.surplus_deficit < 0.0f)
                ? -std::numeric_limits<float>::infinity()
                : 0.0f;
        }

        // Check fiscal stress thresholds.
        if (budget.debt_to_revenue_ratio > Constants::debt_crisis_ratio) {
            // Fiscal crisis consequence.
            ConsequenceDelta consequence{};
            consequence.new_entry_id = static_cast<uint32_t>(
                budget.jurisdiction_id * 100 + 2);  // deterministic ID
            delta.consequence_deltas.push_back(consequence);
        } else if (budget.debt_to_revenue_ratio > Constants::debt_warning_ratio) {
            // Fiscal pressure warning consequence.
            ConsequenceDelta consequence{};
            consequence.new_entry_id = static_cast<uint32_t>(
                budget.jurisdiction_id * 100 + 1);  // deterministic ID
            delta.consequence_deltas.push_back(consequence);
        }

        // Government insolvency: cash < 0.
        if (budget.cash < 0.0f) {
            ConsequenceDelta insolvency{};
            insolvency.new_entry_id = static_cast<uint32_t>(
                budget.jurisdiction_id * 100 + 3);  // deterministic ID
            delta.consequence_deltas.push_back(insolvency);
        }
    }
}

// ===========================================================================
// Step 6: Spending Effects on Region Conditions
// ===========================================================================

void GovernmentBudgetModule::apply_spending_effects(DeltaBuffer& delta) {
    // Provincial budgets drive region condition deltas.
    for (const auto& budget : budgets_) {
        if (budget.level != GovernmentLevel::province) continue;

        RegionDelta region_delta{};
        region_delta.region_id = budget.jurisdiction_id;

        bool has_effect = false;

        // Law enforcement spending reduces crime rate.
        auto le_it = budget.spending_actual.find(SpendingCategory::law_enforcement);
        if (le_it != budget.spending_actual.end() && le_it->second > 0.0f) {
            float crime_reduction = -(le_it->second * Constants::spending_crime_scale);
            region_delta.crime_rate_delta = crime_reduction;
            has_effect = true;
        }

        // Social programs spending reduces inequality.
        auto sp_it = budget.spending_actual.find(SpendingCategory::social_programs);
        if (sp_it != budget.spending_actual.end() && sp_it->second > 0.0f) {
            float inequality_reduction = -(sp_it->second * Constants::spending_inequality_scale);
            region_delta.inequality_delta = inequality_reduction;
            has_effect = true;
        }

        // Public services spending improves stability.
        auto ps_it = budget.spending_actual.find(SpendingCategory::public_services);
        if (ps_it != budget.spending_actual.end() && ps_it->second > 0.0f) {
            float stability_improvement = ps_it->second * Constants::spending_stability_scale;
            region_delta.stability_delta = stability_improvement;
            has_effect = true;
        }

        if (has_effect) {
            delta.region_deltas.push_back(region_delta);
        }
    }
}

// ===========================================================================
// Static Utility Functions
// ===========================================================================

float GovernmentBudgetModule::compute_corporate_tax(
        const std::vector<NPCBusiness>& businesses,
        float tax_rate,
        uint32_t province_id) {
    // Sort businesses by id ascending for deterministic accumulation order
    // (IEEE 754 non-associativity concern from CLAUDE.md).
    std::vector<const NPCBusiness*> sorted;
    for (const auto& biz : businesses) {
        if (biz.province_id == province_id && !biz.criminal_sector) {
            sorted.push_back(&biz);
        }
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const NPCBusiness* a, const NPCBusiness* b) {
                  return a->id < b->id;
              });

    float total = 0.0f;
    for (const NPCBusiness* biz : sorted) {
        total += biz->revenue_per_tick
                 * static_cast<float>(Constants::ticks_per_quarter)
                 * tax_rate;
    }
    return total;
}

float GovernmentBudgetModule::compute_infrastructure_change(
        float current_rating,
        float spending_actual,
        float decay_rate,
        float investment_scale) {
    float new_rating = current_rating - decay_rate;

    if (investment_scale > 0.0f) {
        new_rating += spending_actual / investment_scale;
    }

    // Clamp to [0.0, 1.0].
    if (new_rating < 0.0f) new_rating = 0.0f;
    if (new_rating > 1.0f) new_rating = 1.0f;

    return new_rating;
}

std::map<SpendingCategory, float> GovernmentBudgetModule::prorate_spending(
        const std::map<SpendingCategory, float>& allocations,
        float available_cash) {
    std::map<SpendingCategory, float> result;

    if (available_cash <= 0.0f) {
        // No cash: all categories get 0.
        for (const auto& [cat, _] : allocations) {
            result[cat] = 0.0f;
        }
        return result;
    }

    // Sum total allocations.
    float total_allocations = 0.0f;
    for (const auto& [cat, amount] : allocations) {
        total_allocations += amount;
    }

    if (total_allocations <= 0.0f) {
        // No allocations requested.
        for (const auto& [cat, _] : allocations) {
            result[cat] = 0.0f;
        }
        return result;
    }

    if (available_cash >= total_allocations) {
        // Enough cash: spend full allocations.
        return allocations;
    }

    // Pro-rate: each category gets allocation * (available_cash / total_allocations).
    float ratio = available_cash / total_allocations;
    for (const auto& [cat, amount] : allocations) {
        result[cat] = amount * ratio;
    }

    return result;
}

float GovernmentBudgetModule::compute_debt_to_revenue_ratio(
        float accumulated_debt,
        float total_revenue) {
    if (total_revenue <= 0.0f) {
        if (accumulated_debt > 0.0f) {
            return std::numeric_limits<float>::infinity();
        }
        return 0.0f;
    }
    return accumulated_debt / total_revenue;
}

bool GovernmentBudgetModule::is_quarterly_tick(uint32_t current_tick) {
    if (current_tick == 0) return false;
    return (current_tick % Constants::ticks_per_quarter) == 0;
}

// ===========================================================================
// Lookup Helpers
// ===========================================================================

GovernmentBudget* GovernmentBudgetModule::find_budget(
        GovernmentLevel level,
        uint32_t jurisdiction_id) {
    for (auto& b : budgets_) {
        if (b.level == level && b.jurisdiction_id == jurisdiction_id) {
            return &b;
        }
    }
    return nullptr;
}

const GovernmentBudget* GovernmentBudgetModule::find_budget(
        GovernmentLevel level,
        uint32_t jurisdiction_id) const {
    for (const auto& b : budgets_) {
        if (b.level == level && b.jurisdiction_id == jurisdiction_id) {
            return &b;
        }
    }
    return nullptr;
}

}  // namespace econlife
