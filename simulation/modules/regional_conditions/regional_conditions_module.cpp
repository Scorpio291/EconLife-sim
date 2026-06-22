#include "regional_conditions_module.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include "core/world_state/apply_deltas.h"  // markets_in_province, lookup_good_id
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"
#include "modules/addiction/addiction_types.h"  // AddictionStage

namespace econlife {

namespace {
bool is_criminal_role(NPCRole r) {
    return r == NPCRole::criminal_operator || r == NPCRole::criminal_enforcer ||
           r == NPCRole::fixer;
}
bool is_addicted_stage(AddictionStage s) {
    return s == AddictionStage::dependent || s == AddictionStage::active ||
           s == AddictionStage::terminal;
}
}  // namespace

float RegionalConditionsModule::compute_stability_target(float employment, float infrastructure,
                                                         float trust, float crime,
                                                         float criminal_dominance, float inequality,
                                                         float grievance,
                                                         const RegionalConditionsConfig& cfg) {
    auto unit = [](float v) { return std::clamp(v, 0.0f, 1.0f); };
    float target = cfg.stability_target_base + cfg.stability_w_employment * unit(employment) +
                   cfg.stability_w_infrastructure * unit(infrastructure) +
                   cfg.stability_w_trust * unit(trust) - cfg.stability_w_crime * unit(crime) -
                   cfg.stability_w_criminal_dominance * unit(criminal_dominance) -
                   cfg.stability_w_inequality * unit(inequality) -
                   cfg.stability_w_grievance * unit(grievance);
    return std::clamp(target, 0.0f, 1.0f);
}

float RegionalConditionsModule::compute_stability_step(float current_stability, float target,
                                                       uint32_t instability_events,
                                                       const RegionalConditionsConfig& cfg) {
    // Move toward the grounded target (can be downward when conditions are poor),
    // then subtract the hit from active unrest. Not a blind climb to 1.0.
    float recovery = cfg.stability_recovery_rate * (target - current_stability);
    float degradation = static_cast<float>(instability_events) * cfg.event_stability_impact;
    return std::clamp(current_stability + recovery - degradation, 0.0f, 1.0f);
}

float RegionalConditionsModule::compute_criminal_dominance(float criminal_revenue,
                                                           float total_revenue) {
    if (total_revenue <= 0.0f)
        return 0.0f;
    return std::clamp(criminal_revenue / total_revenue, 0.0f, 1.0f);
}

float RegionalConditionsModule::compute_drought_recovery(float current_modifier,
                                                         float recovery_rate) {
    float result = current_modifier + recovery_rate;
    return std::clamp(result, 0.0f, 1.0f);
}

float RegionalConditionsModule::compute_inequality_from_gini(float gini_coefficient) {
    return std::clamp(gini_coefficient, 0.0f, 1.0f);
}

float RegionalConditionsModule::compute_wealth_concentration(std::vector<float>& capitals) {
    const std::size_t n = capitals.size();
    if (n < 2)
        return 0.0f;
    double total = 0.0;
    for (float c : capitals)
        total += static_cast<double>(std::max(0.0f, c));
    if (total <= 0.0)
        return 0.0f;
    // Descending sort so the top decile is the prefix. Determinism: capitals come
    // from a province-ordered NPC scan; equal values sort stably either way.
    std::sort(capitals.begin(), capitals.end(), std::greater<float>());
    const std::size_t k = std::max<std::size_t>(1, n / 10);
    double top = 0.0;
    for (std::size_t i = 0; i < k; ++i)
        top += static_cast<double>(std::max(0.0f, capitals[i]));
    const float share = static_cast<float>(top / total);
    const float fair = static_cast<float>(k) / static_cast<float>(n);  // equal-share baseline
    return std::clamp((share - fair) / (1.0f - fair), 0.0f, 1.0f);
}

float RegionalConditionsModule::compute_population_rate(uint32_t count, uint32_t population) {
    if (population == 0)
        return 0.0f;
    return std::clamp(static_cast<float>(count) / static_cast<float>(population), 0.0f, 1.0f);
}

float RegionalConditionsModule::compute_formal_employment_rate(float weighted_employment,
                                                               uint32_t total_size) {
    if (total_size == 0)
        return 0.0f;
    return std::clamp(weighted_employment / static_cast<float>(total_size), 0.0f, 1.0f);
}

float RegionalConditionsModule::compute_regulatory_compliance(float compliance_sum,
                                                              uint32_t facility_count) {
    if (facility_count == 0)
        return 1.0f;  // vacuously clean — no facilities to violate compliance
    return std::clamp(compliance_sum / static_cast<float>(facility_count), 0.0f, 1.0f);
}

float RegionalConditionsModule::compute_dominance_ema(float prev, float ratio, float alpha) {
    return std::clamp((1.0f - alpha) * prev + alpha * ratio, 0.0f, 1.0f);
}

void RegionalConditionsModule::execute_province(uint32_t province_idx, const WorldState& state,
                                                DeltaBuffer& province_delta) {
    if (province_idx >= state.provinces.size())
        return;

    const auto& province = state.provinces[province_idx];
    const auto& conditions = province.conditions;
    const auto& community = province.community;

    // Population-fraction monitors now live on cohort_stats (post schema-v5
    // consolidation). Read through this local pointer alias; the rest of
    // the module body uses `cohort_stats->X`. The invariant from
    // world_generator / test_world_factory is that cohort_stats is
    // non-null after construction.
    if (!province.cohort_stats)
        return;
    const RegionCohortStats* cohort_stats = province.cohort_stats.get();

    RegionDelta rdelta;
    rdelta.region_id = province_idx;

    // --- Stability (grounded restoring force) ---
    // Stability gravitates toward the level the province's conditions actually
    // SUPPORT — employment, infrastructure and institutional trust raise it; crime,
    // criminal capture, inequality and popular grievance lower it — rather than
    // drifting blindly toward 1.0. Active community-response unrest degrades it on
    // top. Signals are the province's authoritative current values (one-tick lag).
    uint32_t instability_events = 0;
    if (community.response_stage >= 3)
        instability_events++;
    if (community.response_stage >= 5)
        instability_events++;

    const float stability_target = compute_stability_target(
        cohort_stats->formal_employment_rate, province.infrastructure_rating,
        community.institutional_trust, cohort_stats->crime_rate,
        cohort_stats->criminal_dominance_index, conditions.inequality_index,
        community.grievance_level, cfg_);
    float new_stability = compute_stability_step(conditions.stability_score, stability_target,
                                                 instability_events, cfg_);
    rdelta.stability_delta = new_stability - conditions.stability_score;

    // --- Aggregation pass 1: NPCs physically in this province ---
    const uint32_t population = cohort_stats->total_population;
    uint32_t criminal_npc_count = 0;
    uint32_t addicted_count = 0;
    uint32_t active_npc_count = 0;    // tracked-actor sample size in this province
    std::vector<float> npc_capitals;  // for significant-NPC wealth concentration
    for (const auto& npc : state.significant_npcs) {
        if (npc.status != NPCStatus::active || npc.current_province_id != province.id)
            continue;
        ++active_npc_count;
        npc_capitals.push_back(npc.capital);
        if (is_criminal_role(npc.role))
            criminal_npc_count++;
        if (is_addicted_stage(npc.addiction_state.stage))
            addicted_count++;
    }

    // --- Aggregation pass 2: businesses in this province ---
    float criminal_revenue = 0.0f;
    float total_revenue = 0.0f;
    float compliance_sum = 0.0f;
    uint32_t facility_count = 0;
    for (const auto& biz : state.npc_businesses) {
        if (biz.province_id != province.id)
            continue;
        total_revenue += biz.revenue_per_tick;
        if (biz.criminal_sector) {
            criminal_revenue += biz.revenue_per_tick;
        } else {
            compliance_sum += 1.0f - std::clamp(biz.regulatory_violation_severity, 0.0f, 1.0f);
            facility_count++;
        }
    }

    // --- Cohort employment (size-weighted) ---
    float weighted_employment = 0.0f;
    uint32_t total_cohort_size = 0;
    for (const auto& [group, c] : cohort_stats->cohorts) {
        (void)group;
        weighted_employment += static_cast<float>(c.size) * c.employment_rate;
        total_cohort_size += c.size;
    }

    // Zero-population province: neutral stability, zero per-capita metrics.
    if (population == 0) {
        rdelta.stability_delta = 0.5f - conditions.stability_score;
        rdelta.crime_rate_delta = -cohort_stats->crime_rate;
        rdelta.addiction_rate_delta = -cohort_stats->addiction_rate;
        rdelta.formal_employment_rate_delta = -cohort_stats->formal_employment_rate;
        province_delta.region_deltas.push_back(rdelta);
        return;
    }

    // --- Inequality: cohort income gini, raised by significant-NPC wealth concentration ---
    // The cohort gini measures income spread across the demographic masses but is
    // blind to wealth concentration among the tracked actors: when a boom funnels
    // capital to a handful of owners, income gini barely moves while the top-decile
    // wealth share spikes. Take the worse of the two signals so a "booming but
    // unequal" economy registers as high inequality (which feeds community_response
    // grievance — i.e. a boom that concentrates wealth breeds resentment). The
    // wealth term is weighted below 1.0 so a single rich owner alone cannot peg
    // inequality; broad concentration is required to dominate the income signal.
    const float wealth_concentration = compute_wealth_concentration(npc_capitals);
    const float inequality_target = std::max(cohort_stats->gini_coefficient,
                                             cfg_.wealth_inequality_weight * wealth_concentration);
    rdelta.inequality_delta = inequality_target - conditions.inequality_index;

    // --- Crime rate: criminal fraction of the tracked-actor sample ---
    // Significant NPCs are a SAMPLE of the population, not its full count; the
    // background masses (cohorts) are not modeled per-person. Dividing the
    // sample's criminal count by the full demographic population produced a
    // crime_rate ~1e-5 (off by the sampling ratio, ~300 NPCs vs ~500k people),
    // disconnected from the actual ~10% criminal presence. Use the sample as
    // representative: crime_rate = criminal NPCs / active NPCs in the province.
    // (A composite that also weights criminal business revenue / dominance per
    // INTERFACE is a future refinement.)
    float crime_sample = active_npc_count > 0 ? static_cast<float>(criminal_npc_count) /
                                                    static_cast<float>(active_npc_count)
                                              : 0.0f;
    rdelta.crime_rate_delta = crime_sample - cohort_stats->crime_rate;

    // --- Addiction rate: dependent+ NPCs / population (authoritative) ---
    rdelta.addiction_rate_delta =
        compute_population_rate(addicted_count, population) - cohort_stats->addiction_rate;

    // --- Formal employment: size-weighted mean of cohort employment_rate ---
    rdelta.formal_employment_rate_delta =
        compute_formal_employment_rate(weighted_employment, total_cohort_size) -
        cohort_stats->formal_employment_rate;

    // --- Regulatory compliance: mean(1 - violation) over non-criminal facilities ---
    rdelta.regulatory_compliance_delta =
        compute_regulatory_compliance(compliance_sum, facility_count) -
        conditions.regulatory_compliance_index;

    // --- Criminal dominance: criminal revenue share with quarterly EMA (alpha 0.1) ---
    // No economy this tick -> carry the previous value forward (no delta).
    if (total_revenue > 0.0f) {
        float ratio = compute_criminal_dominance(criminal_revenue, total_revenue);
        float ema = compute_dominance_ema(cohort_stats->criminal_dominance_index, ratio, 0.1f);
        rdelta.criminal_dominance_delta = ema - cohort_stats->criminal_dominance_index;
    }

    // --- Agricultural modifiers: recover monotonically toward 1.0 ---
    // (An active-weather-event signal is not exposed on WorldState; an event
    // producer would lower these faster than recovery raises them.)
    {
        float new_drought =
            compute_drought_recovery(conditions.drought_modifier, cfg_.drought_recovery_rate);
        if (new_drought != conditions.drought_modifier)
            rdelta.drought_modifier_delta = new_drought - conditions.drought_modifier;
        float new_flood =
            compute_drought_recovery(conditions.flood_modifier, cfg_.flood_recovery_rate);
        if (new_flood != conditions.flood_modifier)
            rdelta.flood_modifier_delta = new_flood - conditions.flood_modifier;
    }

    // --- Social cohesion ---
    // Cohesion is eroded by high grievance and boosted by low crime and high stability.
    {
        constexpr float COHESION_GRIEVANCE_WEIGHT = -0.001f;
        constexpr float COHESION_STABILITY_WEIGHT = 0.001f;
        rdelta.cohesion_delta = COHESION_GRIEVANCE_WEIGHT * community.grievance_level +
                                COHESION_STABILITY_WEIGHT * conditions.stability_score;
    }

    // --- Grievance ---
    // Owned by community_response (material-deprivation + actor-wrong model with
    // a built-in restoring force). This module previously ALSO wrote grievance
    // (inequality + crime + a stage→grievance feedback term); that uncoordinated
    // second writer — together with population_aging's — stacked additively and
    // pinned grievance at the ceiling. Inequality and crime are already inputs to
    // community_response's grievance target, and the stage term was a
    // self-reinforcing pump. Removed; grievance has a single owner.

    // --- Institutional trust ---
    // Eroded by corruption and high crime; boosted by high stability.
    {
        constexpr float TRUST_CORRUPTION_WEIGHT = -0.001f;
        constexpr float TRUST_CRIME_WEIGHT = -0.001f;
        constexpr float TRUST_STABILITY_WEIGHT = 0.0005f;
        rdelta.institutional_trust_delta =
            TRUST_CORRUPTION_WEIGHT * province.political.corruption_index +
            TRUST_CRIME_WEIGHT * cohort_stats->crime_rate +
            TRUST_STABILITY_WEIGHT * conditions.stability_score;
    }

    // --- Waste handling + pollution ---
    // Read the province's accumulated waste, handle (remove) a fraction of it, and
    // let the hazardous-weighted residual raise sick_rate (pollution illness). This
    // is what makes waste "must be handled" bite: provinces that out-generate their
    // handling capacity accumulate waste and pay in public health.
    {
        const uint32_t ind_id = lookup_good_id(state, "industrial_waste");
        const uint32_t haz_id = lookup_good_id(state, "hazardous_waste");
        const uint32_t muni_id = lookup_good_id(state, "municipal_waste");
        float ind = 0.0f, haz = 0.0f, muni = 0.0f;
        for (uint32_t mi : markets_in_province(state, province_idx)) {
            const auto& mk = state.regional_markets[mi];
            if (mk.good_id == ind_id)
                ind = mk.supply;
            else if (mk.good_id == haz_id)
                haz = mk.supply;
            else if (mk.good_id == muni_id)
                muni = mk.supply;
        }

        const float handle_frac =
            std::min(0.95f, cfg_.waste_handling_base +
                                cfg_.waste_handling_infra * province.infrastructure_rating);
        const float haz_handle = handle_frac * cfg_.waste_handling_hazardous_scale;
        auto handle = [&](uint32_t gid, float stock, float frac) {
            if (gid == 0 || stock <= 0.0f || frac <= 0.0f)
                return;
            MarketDelta md{};
            md.good_id = gid;
            md.region_id = province_idx;
            md.supply_delta = -stock * frac;
            province_delta.market_deltas.push_back(md);
        };
        handle(ind_id, ind, handle_frac);
        handle(muni_id, muni, handle_frac);
        handle(haz_id, haz, haz_handle);

        // Pollution from the hazardous-weighted residual. Saturating in [0,1) so the
        // effect is robust to the absolute scale of waste in the world.
        const float weighted = haz * cfg_.hazardous_pollution_weight + ind + muni;
        if (weighted > 0.0f) {
            const float pollution = weighted / (weighted + cfg_.waste_pollution_halfsat);
            const float sick_contribution = pollution * cfg_.waste_pollution_sick_scale;
            rdelta.sick_rate_delta = rdelta.sick_rate_delta.value_or(0.0f) + sick_contribution;
        }
    }

    province_delta.region_deltas.push_back(rdelta);
}

void RegionalConditionsModule::execute(const WorldState& state, DeltaBuffer& delta) {
    for (uint32_t i = 0; i < state.provinces.size(); ++i) {
        execute_province(i, state, delta);
    }
}

}  // namespace econlife
