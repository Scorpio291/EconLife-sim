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

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <utility>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/economy/economy_types.h"

namespace econlife {

namespace {
// Criminal-role NPCs hold illicit wealth: hidden from the taxman (so the wealth
// tax never reaches it) but exposed to enforcement seizure where rule of law exists.
bool is_criminal_role(NPCRole r) {
    return r == NPCRole::criminal_operator || r == NPCRole::criminal_enforcer ||
           r == NPCRole::fixer;
}
}  // namespace

// ===========================================================================
// GovernmentBudgetModule — tick execution
// ===========================================================================

void GovernmentBudgetModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Non-quarterly ticks: only infrastructure decay and spending effects.
    // Quarterly ticks: full processing pipeline.
    if (is_quarterly_tick(state.current_tick, cfg_.ticks_per_quarter)) {
        process_quarterly_taxes(state, delta);
        process_intergovernmental_transfers();
        execute_spending(state, delta);
        check_fiscal_health(delta, state.current_tick);
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
            if (budget.level != GovernmentLevel::province)
                continue;

            RegionDelta decay_delta{};
            decay_delta.region_id = budget.jurisdiction_id;
            decay_delta.stability_delta = -0.0001f;
            delta.region_deltas.push_back(decay_delta);
        }
    }
}

// ===========================================================================
// Step 1: Quarterly Tax Collection
// ===========================================================================

void GovernmentBudgetModule::process_quarterly_taxes(const WorldState& state, DeltaBuffer& delta) {
    // Collect taxes for the national budget.
    GovernmentBudget* national = find_budget(GovernmentLevel::national, 0);
    if (!national)
        return;

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
        float prov_corporate = compute_corporate_tax(state.npc_businesses, corporate_tax_rate,
                                                     prov.id, cfg_.ticks_per_quarter);
        national_corporate_tax += prov_corporate;

        // --- Income tax ---
        // For V1 bootstrap: use a simplified cohort-based estimate.
        // Income tax = sum over income cohort fractions:
        //   median_income_estimate * population * income_tax_rate * cohort_modifier
        // Since we don't have per-province cohort data yet, use demographic fractions
        // from Province.demographics as a proxy.
        if (prov.cohort_stats) {
            // Per-tick income from the population_aging cohort aggregate.
            float median_income_estimate = prov.cohort_stats->mean_income;
            float pop = static_cast<float>(prov.cohort_stats->total_population);
            float working_fraction = prov.cohort_stats->working_age_fraction;

            // Working class: 60% of working population, modifier 0.40
            float working_class_income = median_income_estimate * pop * working_fraction * 0.60f *
                                         income_tax_rate * cfg_.cohort_mod_working_class *
                                         static_cast<float>(cfg_.ticks_per_quarter);

            // Professional: 30% of working population, modifier 0.85
            float professional_income = median_income_estimate * pop * working_fraction * 0.30f *
                                        income_tax_rate * cfg_.cohort_mod_professional *
                                        static_cast<float>(cfg_.ticks_per_quarter);

            // Corporate: 10% of working population, modifier 1.00
            float corporate_income = median_income_estimate * pop * working_fraction * 0.10f *
                                     income_tax_rate * cfg_.cohort_mod_corporate *
                                     static_cast<float>(cfg_.ticks_per_quarter);

            national_income_tax += working_class_income + professional_income + corporate_income;
        }

        // --- Property tax ---
        // Province.property_tax_revenue_estimate is noted as a FIELD ADDITION
        // in budget_types.h but not yet on the Province struct.
        // For V1 bootstrap: use a fixed per-province estimate based on population.
        if (prov.cohort_stats) {
            // Real estate base: real_estate publishes avg_property_value (mean
            // PropertyListing.market_value); estimate the count of properties as
            // population / household_size and tax the total at a quarterly rate.
            float pop = static_cast<float>(prov.cohort_stats->total_population);
            float estimated_properties =
                (cfg_.household_size > 0.0f) ? pop / cfg_.household_size : 0.0f;
            float total_property_value = prov.avg_property_value * estimated_properties;
            float property_estimate = total_property_value * (cfg_.property_tax_annual_rate / 4.0f);
            national_property_tax += property_estimate;
        }
    }

    // --- Regime-dependent restoring force on concentrated wealth ---
    // Capital concentrates by default (profit + criminal proceeds flow in with no
    // wealth-proportional outflow). Nothing equalizes it automatically; the only
    // forces pushing back are STATE institutions, and they act on legit and illicit
    // wealth through different channels — both scaled per nation by government type:
    //   * legit wealth  -> progressive REDISTRIBUTION tax (fiscal capacity + will),
    //   * illicit wealth -> rule-of-law SEIZURE (enforcement against proceeds of crime).
    // Criminal proceeds are hidden from the revenue service, so the tax never reaches
    // them — only enforcement does; criminal NPCs therefore pay the seizure INSTEAD
    // of the tax. Both factors are looked up per NPC via their province's nation, so
    // an accountable state bounds concentration while a kleptocracy (shielding its
    // elite) or a failed state (no apparatus) lets it run. NPCs iterate in
    // significant_npcs order (id-ascending), so accumulation is deterministic.
    float national_wealth_tax = 0.0f;  // legit wealth (redistribution)
    float national_seizure = 0.0f;     // illicit wealth (rule of law)
    const float exemption = cfg_.wealth_tax_exemption;
    const float base_rate = cfg_.wealth_tax_base_rate;
    const float max_rate = cfg_.wealth_tax_max_rate;
    const float prog_scale =
        cfg_.wealth_tax_progressivity_scale > 0.0f ? cfg_.wealth_tax_progressivity_scale : 1.0f;
    const float seize_exemption = cfg_.criminal_seizure_exemption;
    const float seize_rate = cfg_.criminal_seizure_annual_rate;
    {
        // Map province_id -> {redistribution, rule_of_law} via its nation's government
        // type, so BOTH the restoring force on legit wealth and the seizure of illicit
        // wealth vary by place: a province in a social democracy redistributes AND
        // enforces; a kleptocracy does neither for its elite; a failed state has no
        // fiscal or enforcement apparatus at all.
        std::map<uint32_t, std::pair<float, float>> province_factors;
        for (const Province& prov : state.provinces) {
            float redistribution = cfg_.wealth_tax_redistribution_democracy;
            float rule_of_law = cfg_.rule_of_law_democracy;
            for (const Nation& nation : state.nations) {
                if (nation.id == prov.nation_id) {
                    redistribution = regime_redistribution_factor(nation.government_type, cfg_);
                    rule_of_law = regime_rule_of_law_factor(nation.government_type, cfg_);
                    break;
                }
            }
            province_factors[prov.id] = {redistribution, rule_of_law};
        }

        for (const auto& npc : state.significant_npcs) {
            if (npc.status != NPCStatus::active)
                continue;
            auto fit = province_factors.find(npc.current_province_id);
            const float redistribution = fit != province_factors.end() ? fit->second.first : 1.0f;
            const float rule_of_law = fit != province_factors.end() ? fit->second.second : 1.0f;

            if (is_criminal_role(npc.role)) {
                // Illicit wealth: stripped by enforcement, not the revenue service.
                if (rule_of_law <= 0.0f || seize_rate <= 0.0f)
                    continue;  // no enforcement — illicit fortune concentrates unchecked
                const float seizable = npc.capital - seize_exemption;
                if (seizable <= 0.0f)
                    continue;
                // The kingpin's launderers/fixers shield part of it — the law does
                // not bind equally. Individual, per NPC.
                const float shielded = 1.0f - levy_avoidance_fraction(npc, cfg_);
                float quarterly_seizure = seizable * seize_rate * rule_of_law * 0.25f * shielded;
                quarterly_seizure = std::min(quarterly_seizure, npc.capital);
                if (quarterly_seizure <= 0.0f)
                    continue;
                national_seizure += quarterly_seizure;
                NPCDelta nd{};
                nd.npc_id = npc.id;
                nd.capital_delta = -quarterly_seizure;
                delta.npc_deltas.push_back(nd);
                continue;
            }

            // Legit wealth: progressive redistribution tax.
            if (max_rate <= 0.0f || redistribution <= 0.0f)
                continue;  // no functioning fiscal state — wealth concentrates unchecked
            const float taxable = npc.capital - exemption;
            if (taxable <= 0.0f)
                continue;
            // Marginal rate rises linearly from base_rate at the exemption to max_rate
            // once taxable wealth reaches prog_scale, then is capped, then scaled by
            // the regime's redistribution strength.
            const float annual_rate =
                std::clamp(base_rate + (max_rate - base_rate) * (taxable / prog_scale), base_rate,
                           max_rate) *
                redistribution;
            // The wealthy and well-connected shield part of what they nominally owe
            // through their own teams — the law does not bind equally. Individual.
            const float shielded = 1.0f - levy_avoidance_fraction(npc, cfg_);
            float quarterly_tax = taxable * annual_rate * 0.25f * shielded;
            quarterly_tax = std::min(quarterly_tax, npc.capital);  // never drive capital below 0
            if (quarterly_tax <= 0.0f)
                continue;
            national_wealth_tax += quarterly_tax;
            NPCDelta nd{};
            nd.npc_id = npc.id;
            nd.capital_delta = -quarterly_tax;
            delta.npc_deltas.push_back(nd);
        }
    }

    float total_tax_revenue = national_corporate_tax + national_income_tax + national_property_tax +
                              national_wealth_tax + national_seizure;
    national->revenue_own_taxes = total_tax_revenue;
    national->total_revenue =
        national->revenue_own_taxes + national->revenue_transfers_in + national->revenue_other;
    national->cash += total_tax_revenue;

    // Provincial budgets receive their fraction via intergovernmental transfers (Step 2),
    // not via direct tax collection in V1 bootstrap.
}

// ===========================================================================
// Step 2: Intergovernmental Transfers
// ===========================================================================

void GovernmentBudgetModule::process_intergovernmental_transfers() {
    GovernmentBudget* national = find_budget(GovernmentLevel::national, 0);
    if (!national)
        return;

    // National -> Provincial transfers.
    auto it = national->spending_allocations.find(SpendingCategory::intergovernmental);
    float intergovernmental_pool = 0.0f;
    if (it != national->spending_allocations.end()) {
        intergovernmental_pool = it->second;
    }

    if (intergovernmental_pool <= 0.0f)
        return;

    // Count provincial budgets.
    uint32_t province_count = 0;
    for (const auto& b : budgets_) {
        if (b.level == GovernmentLevel::province) {
            ++province_count;
        }
    }
    if (province_count == 0)
        return;

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
        prov->total_revenue =
            prov->revenue_own_taxes + prov->revenue_transfers_in + prov->revenue_other;
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
        if (prov_transfer_pool <= 0.0f)
            continue;

        // Count city budgets for this province.
        uint32_t city_count = 0;
        for (const auto& b : budgets_) {
            if (b.level == GovernmentLevel::city &&
                b.jurisdiction_id / 1000 == prov->jurisdiction_id) {
                // Convention: city jurisdiction_id encodes province as id/1000.
                // This is a simplified V1 mapping.
                ++city_count;
            }
        }
        if (city_count == 0)
            continue;

        float per_city = prov_transfer_pool / static_cast<float>(city_count);
        for (auto& b : budgets_) {
            if (b.level == GovernmentLevel::city &&
                b.jurisdiction_id / 1000 == prov->jurisdiction_id) {
                b.revenue_transfers_in += per_city;
                b.cash += per_city;
                b.total_revenue = b.revenue_own_taxes + b.revenue_transfers_in + b.revenue_other;
            }
        }
        prov->cash -= prov_transfer_pool;
    }
}

// ===========================================================================
// Step 3: Spending Execution
// ===========================================================================

void GovernmentBudgetModule::execute_spending(const WorldState& state, DeltaBuffer& delta) {
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

        // Welfare re-injection: distribute welfare spending to NPC residents of
        // this province so money collected as taxes returns to the economy.
        // Province-level budgets only (national and city budgets do not directly
        // distribute to individual NPCs in V1 bootstrap).
        if (budget->level != GovernmentLevel::province)
            continue;

        auto welfare_it = budget->spending_actual.find(SpendingCategory::social_programs);
        if (welfare_it == budget->spending_actual.end() || welfare_it->second <= 0.0f)
            continue;

        // Resident NPCs of this province via the home_province bucket.
        std::vector<const NPC*> resident_npcs;
        if (budget->jurisdiction_id < state.npc_indices_by_home_province.size()) {
            for (uint32_t idx : state.npc_indices_by_home_province[budget->jurisdiction_id]) {
                resident_npcs.push_back(&state.significant_npcs[idx]);
            }
        }
        if (resident_npcs.empty())
            continue;

        const float per_npc_payment = welfare_it->second / static_cast<float>(resident_npcs.size());

        // Emit NPCDelta.capital_delta for each resident NPC (ascending id order for determinism).
        std::sort(resident_npcs.begin(), resident_npcs.end(),
                  [](const NPC* a, const NPC* b) { return a->id < b->id; });

        for (const NPC* npc : resident_npcs) {
            NPCDelta nd{};
            nd.npc_id = npc->id;
            nd.capital_delta = per_npc_payment;
            delta.npc_deltas.push_back(nd);
        }
    }
}

// ===========================================================================
// Step 4: Infrastructure Update
// ===========================================================================

void GovernmentBudgetModule::update_infrastructure(const WorldState& state, DeltaBuffer& delta) {
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
        if (!province)
            continue;

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
        float investment_scale =
            (area_km2 > 0.0f) ? area_km2 : cfg_.infrastructure_investment_scale;

        float new_rating =
            compute_infrastructure_change(province->infrastructure_rating, infra_spend,
                                          cfg_.infrastructure_decay_per_quarter, investment_scale);

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
            region_delta.region_id = province->region_id;
            region_delta.stability_delta = infra_change * 0.05f;
            region_delta.infrastructure_rating_delta = infra_change;
            delta.region_deltas.push_back(region_delta);
        }
    }
}

// ===========================================================================
// Step 5: Fiscal Health Checks
// ===========================================================================

void GovernmentBudgetModule::check_fiscal_health(DeltaBuffer& delta, uint32_t current_tick) {
    for (auto& budget : budgets_) {
        // Compute debt-to-revenue ratio.
        budget.debt_to_revenue_ratio =
            compute_debt_to_revenue_ratio(budget.accumulated_debt, budget.total_revenue);

        // Compute deficit-to-revenue ratio.
        if (budget.total_revenue > 0.0f) {
            budget.deficit_to_revenue_ratio = budget.surplus_deficit / budget.total_revenue;
        } else {
            budget.deficit_to_revenue_ratio =
                (budget.surplus_deficit < 0.0f) ? -std::numeric_limits<float>::infinity() : 0.0f;
        }

        // Where a fiscal consequence lands: a province-level budget's crisis is felt
        // in its own province (jurisdiction_id IS the region id — apply_spending_effects
        // already routes that way); a national budget's crisis has no single province,
        // so it says so explicitly rather than defaulting onto province 0, which is a
        // real province that was silently absorbing every nation's fiscal shock.
        const uint32_t consequence_province = budget.level == GovernmentLevel::province
                                                  ? static_cast<uint32_t>(budget.jurisdiction_id)
                                                  : kConsequenceNoProvince;

        // Check fiscal stress thresholds.
        if (budget.debt_to_revenue_ratio > cfg_.debt_crisis_ratio) {
            // Fiscal crisis consequence.
            ConsequenceDelta consequence{};
            consequence.new_consequence =
                make_consequence(static_cast<uint32_t>(budget.jurisdiction_id * 100 + 2),
                                 ConsequenceCategory::political_consequence, 0, 0,
                                 consequence_province, current_tick);
            delta.consequence_deltas.push_back(consequence);
        } else if (budget.debt_to_revenue_ratio > cfg_.debt_warning_ratio) {
            // Fiscal pressure warning consequence.
            ConsequenceDelta consequence{};
            consequence.new_consequence =
                make_consequence(static_cast<uint32_t>(budget.jurisdiction_id * 100 + 1),
                                 ConsequenceCategory::political_consequence, 0, 0,
                                 consequence_province, current_tick);
            delta.consequence_deltas.push_back(consequence);
        }

        // Government insolvency: cash < 0.
        if (budget.cash < 0.0f) {
            ConsequenceDelta insolvency{};
            insolvency.new_consequence =
                make_consequence(static_cast<uint32_t>(budget.jurisdiction_id * 100 + 3),
                                 ConsequenceCategory::political_consequence, 0, 0,
                                 consequence_province, current_tick);
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
        if (budget.level != GovernmentLevel::province)
            continue;

        RegionDelta region_delta{};
        region_delta.region_id = budget.jurisdiction_id;

        bool has_effect = false;

        // Law enforcement spending reduces crime rate.
        auto le_it = budget.spending_actual.find(SpendingCategory::law_enforcement);
        if (le_it != budget.spending_actual.end() && le_it->second > 0.0f) {
            float crime_reduction = -(le_it->second * cfg_.spending_crime_scale);
            region_delta.crime_rate_delta = crime_reduction;
            has_effect = true;
        }

        // Social programs spending reduces inequality.
        auto sp_it = budget.spending_actual.find(SpendingCategory::social_programs);
        if (sp_it != budget.spending_actual.end() && sp_it->second > 0.0f) {
            float inequality_reduction = -(sp_it->second * cfg_.spending_inequality_scale);
            region_delta.inequality_delta = inequality_reduction;
            has_effect = true;
        }

        // Public services spending improves stability.
        auto ps_it = budget.spending_actual.find(SpendingCategory::public_services);
        if (ps_it != budget.spending_actual.end() && ps_it->second > 0.0f) {
            float stability_improvement = ps_it->second * cfg_.spending_stability_scale;
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

float GovernmentBudgetModule::compute_corporate_tax(const std::vector<NPCBusiness>& businesses,
                                                    float tax_rate, uint32_t province_id,
                                                    uint32_t ticks_per_quarter) {
    // Sort businesses by id ascending for deterministic accumulation order
    // (IEEE 754 non-associativity concern from CLAUDE.md).
    std::vector<const NPCBusiness*> sorted;
    for (const auto& biz : businesses) {
        if (biz.province_id == province_id && !biz.criminal_sector) {
            sorted.push_back(&biz);
        }
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const NPCBusiness* a, const NPCBusiness* b) { return a->id < b->id; });

    float total = 0.0f;
    for (const NPCBusiness* biz : sorted) {
        total += biz->revenue_per_tick * static_cast<float>(ticks_per_quarter) * tax_rate;
    }
    return total;
}

float GovernmentBudgetModule::compute_infrastructure_change(float current_rating,
                                                            float spending_actual, float decay_rate,
                                                            float investment_scale) {
    float new_rating = current_rating - decay_rate;

    if (investment_scale > 0.0f) {
        new_rating += spending_actual / investment_scale;
    }

    // Clamp to [0.0, 1.0].
    if (new_rating < 0.0f)
        new_rating = 0.0f;
    if (new_rating > 1.0f)
        new_rating = 1.0f;

    return new_rating;
}

std::map<SpendingCategory, float> GovernmentBudgetModule::prorate_spending(
    const std::map<SpendingCategory, float>& allocations, float available_cash) {
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

float GovernmentBudgetModule::compute_debt_to_revenue_ratio(float accumulated_debt,
                                                            float total_revenue) {
    if (total_revenue <= 0.0f) {
        if (accumulated_debt > 0.0f) {
            return std::numeric_limits<float>::infinity();
        }
        return 0.0f;
    }
    return accumulated_debt / total_revenue;
}

bool GovernmentBudgetModule::is_quarterly_tick(uint32_t current_tick, uint32_t ticks_per_quarter) {
    if (current_tick == 0)
        return false;
    return (current_tick % ticks_per_quarter) == 0;
}

float GovernmentBudgetModule::regime_redistribution_factor(GovernmentType type,
                                                           const GovernmentBudgetConfig& cfg) {
    switch (type) {
        case GovernmentType::Democracy:
            return cfg.wealth_tax_redistribution_democracy;
        case GovernmentType::Autocracy:
            return cfg.wealth_tax_redistribution_autocracy;
        case GovernmentType::Federation:
            return cfg.wealth_tax_redistribution_federation;
        case GovernmentType::FailedState:
            return cfg.wealth_tax_redistribution_failed_state;
    }
    return cfg.wealth_tax_redistribution_democracy;  // defensive default
}

float GovernmentBudgetModule::regime_rule_of_law_factor(GovernmentType type,
                                                        const GovernmentBudgetConfig& cfg) {
    switch (type) {
        case GovernmentType::Democracy:
            return cfg.rule_of_law_democracy;
        case GovernmentType::Autocracy:
            return cfg.rule_of_law_autocracy;
        case GovernmentType::Federation:
            return cfg.rule_of_law_federation;
        case GovernmentType::FailedState:
            return cfg.rule_of_law_failed_state;
    }
    return cfg.rule_of_law_democracy;  // defensive default
}

namespace {
// Structural avoidance advantage conferred by an NPC's role: the enablers
// (accountants, lawyers, bankers, fixers) and the politically protected dodge far
// more than an ordinary actor of the same wealth.
float avoidance_role_bonus(NPCRole r, const GovernmentBudgetConfig& cfg) {
    switch (r) {
        case NPCRole::accountant:
        case NPCRole::lawyer:
        case NPCRole::banker:
        case NPCRole::fixer:
            return cfg.avoidance_role_enabler;
        case NPCRole::corporate_executive:
            return cfg.avoidance_role_corporate;
        case NPCRole::criminal_operator:
        case NPCRole::criminal_enforcer:
            return cfg.avoidance_role_criminal;
        case NPCRole::politician:
        case NPCRole::judge:
        case NPCRole::appointed_official:
        case NPCRole::regulator:
            return cfg.avoidance_role_political;
        default:
            return 0.0f;
    }
}
}  // namespace

float GovernmentBudgetModule::levy_avoidance_fraction(const NPC& npc,
                                                      const GovernmentBudgetConfig& cfg) {
    float a = cfg.avoidance_base;
    // Connections / institutional access.
    a += cfg.avoidance_w_social *
         std::clamp(npc.social_capital / cfg.avoidance_social_norm, 0.0f, 1.0f);
    // Network reach: fixers, accountants, bankers a phone call away.
    a += cfg.avoidance_w_contacts *
         std::clamp(static_cast<float>(npc.contact_ids.size()) / cfg.avoidance_contacts_norm, 0.0f,
                    1.0f);
    // Appetite for aggressive schemes.
    a += cfg.avoidance_w_risk * std::clamp(npc.risk_tolerance, 0.0f, 1.0f);
    // Motivation to protect money (financial_gain weight; OutcomeType::financial_gain = 0).
    a += cfg.avoidance_w_money * std::clamp(npc.motivations.weights[0], 0.0f, 1.0f);
    // Role expertise: enablers and the politically protected shield structurally more.
    a += avoidance_role_bonus(npc.role, cfg);
    // Wealth affords better teams — a modest contribution, not the whole story.
    const float wealth_factor = std::clamp(
        (npc.capital - cfg.avoidance_wealth_threshold) / cfg.avoidance_wealth_scale, 0.0f, 1.0f);
    a += cfg.avoidance_w_wealth * wealth_factor;
    // Stable per-NPC innate aptitude (cunning, inherited advisors) not captured by
    // the fields above — deterministically derived from the NPC id so it is fixed
    // for an individual but varies across the population.
    const float innate = static_cast<float>((npc.id * 2654435761u) % 1000u) / 1000.0f;  // [0,1)
    a += cfg.avoidance_w_innate * innate;
    return std::clamp(a, 0.0f, cfg.avoidance_max);
}

// ===========================================================================
// Lookup Helpers
// ===========================================================================

GovernmentBudget* GovernmentBudgetModule::find_budget(GovernmentLevel level,
                                                      uint32_t jurisdiction_id) {
    for (auto& b : budgets_) {
        if (b.level == level && b.jurisdiction_id == jurisdiction_id) {
            return &b;
        }
    }
    return nullptr;
}

const GovernmentBudget* GovernmentBudgetModule::find_budget(GovernmentLevel level,
                                                            uint32_t jurisdiction_id) const {
    for (const auto& b : budgets_) {
        if (b.level == level && b.jurisdiction_id == jurisdiction_id) {
            return &b;
        }
    }
    return nullptr;
}

// ─── Persistence helpers (schema v7) ────────────────────────────────────────
//
// Format (little-endian):
//   u32 schema_tag (1)
//   u32 count
//   for each GovernmentBudget:
//     u8 level, u32 jurisdiction_id
//     f32 revenue_own_taxes, f32 revenue_transfers_in, f32 revenue_other, f32 total_revenue
//     u32 alloc_count, for each: u8 SpendingCategory, f32 amount
//     u32 actual_count, for each: u8 SpendingCategory, f32 amount
//     f32 total_expenditure
//     f32 surplus_deficit, f32 accumulated_debt
//     f32 cash
//     f32 debt_to_revenue_ratio, f32 deficit_to_revenue_ratio
//
// std::map iterates in key order so spending_allocations and
// spending_actual serialise deterministically.

namespace {

void put_u32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void put_f32(std::vector<uint8_t>& out, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    put_u32(out, bits);
}

struct Reader {
    const uint8_t* data;
    size_t size;
    size_t pos = 0;
    bool error = false;
    bool need(size_t n) {
        if (pos + n > size) {
            error = true;
            return false;
        }
        return true;
    }
    uint32_t u32() {
        if (!need(4))
            return 0;
        uint32_t v = data[pos] | (uint32_t(data[pos + 1]) << 8) | (uint32_t(data[pos + 2]) << 16) |
                     (uint32_t(data[pos + 3]) << 24);
        pos += 4;
        return v;
    }
    uint8_t u8() {
        if (!need(1))
            return 0;
        return data[pos++];
    }
    float f32() {
        uint32_t bits = u32();
        float v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }
};

}  // namespace

void GovernmentBudgetModule::serialize_state(std::vector<uint8_t>& out) const {
    put_u32(out, 1u);
    put_u32(out, static_cast<uint32_t>(budgets_.size()));
    for (const auto& b : budgets_) {
        out.push_back(static_cast<uint8_t>(b.level));
        put_u32(out, b.jurisdiction_id);
        put_f32(out, b.revenue_own_taxes);
        put_f32(out, b.revenue_transfers_in);
        put_f32(out, b.revenue_other);
        put_f32(out, b.total_revenue);
        put_u32(out, static_cast<uint32_t>(b.spending_allocations.size()));
        for (const auto& [cat, amt] : b.spending_allocations) {
            out.push_back(static_cast<uint8_t>(cat));
            put_f32(out, amt);
        }
        put_u32(out, static_cast<uint32_t>(b.spending_actual.size()));
        for (const auto& [cat, amt] : b.spending_actual) {
            out.push_back(static_cast<uint8_t>(cat));
            put_f32(out, amt);
        }
        put_f32(out, b.total_expenditure);
        put_f32(out, b.surplus_deficit);
        put_f32(out, b.accumulated_debt);
        put_f32(out, b.cash);
        put_f32(out, b.debt_to_revenue_ratio);
        put_f32(out, b.deficit_to_revenue_ratio);
    }
}

bool GovernmentBudgetModule::deserialize_state(const uint8_t* data, size_t size) {
    Reader r{data, size};
    if (r.u32() != 1u)
        return false;
    uint32_t count = r.u32();
    budgets_.clear();
    budgets_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        GovernmentBudget b{};
        b.level = static_cast<GovernmentLevel>(r.u8());
        b.jurisdiction_id = r.u32();
        b.revenue_own_taxes = r.f32();
        b.revenue_transfers_in = r.f32();
        b.revenue_other = r.f32();
        b.total_revenue = r.f32();
        uint32_t alloc_n = r.u32();
        if (r.error)
            return false;
        for (uint32_t j = 0; j < alloc_n; ++j) {
            SpendingCategory cat = static_cast<SpendingCategory>(r.u8());
            float amt = r.f32();
            b.spending_allocations[cat] = amt;
        }
        uint32_t act_n = r.u32();
        if (r.error)
            return false;
        for (uint32_t j = 0; j < act_n; ++j) {
            SpendingCategory cat = static_cast<SpendingCategory>(r.u8());
            float amt = r.f32();
            b.spending_actual[cat] = amt;
        }
        b.total_expenditure = r.f32();
        b.surplus_deficit = r.f32();
        b.accumulated_debt = r.f32();
        b.cash = r.f32();
        b.debt_to_revenue_ratio = r.f32();
        b.deficit_to_revenue_ratio = r.f32();
        if (r.error)
            return false;
        budgets_.push_back(std::move(b));
    }
    return !r.error;
}

}  // namespace econlife
