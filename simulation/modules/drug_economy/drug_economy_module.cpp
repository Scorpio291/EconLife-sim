#include "drug_economy_module.h"

#include <algorithm>
#include <cmath>

#include "core/world_state/player.h"
#include "core/world_state/world_state.h"

namespace econlife {

// ============================================================================
// Static utility functions
// ============================================================================

float DrugEconomyModule::compute_wholesale_price(float retail_spot_price,
                                                 float wholesale_price_fraction) {
    return retail_spot_price * wholesale_price_fraction;
}

float DrugEconomyModule::degrade_quality(float input_quality, float degradation_factor) {
    return std::clamp(input_quality * degradation_factor, 0.0f, 1.0f);
}

float DrugEconomyModule::compute_addiction_demand(float addiction_rate,
                                                  uint32_t province_population,
                                                  float demand_per_addict) {
    return addiction_rate * static_cast<float>(province_population) * demand_per_addict;
}

float DrugEconomyModule::compute_precursor_consumption(float drug_output, float precursor_ratio) {
    return drug_output * precursor_ratio;
}

bool DrugEconomyModule::is_drug_legal(const DrugLegalizationStatus& status, DrugType drug_type) {
    return status.is_legal(drug_type);
}

float DrugEconomyModule::compute_meth_waste_signature(float output_quantity, float waste_per_unit) {
    return std::clamp(output_quantity * waste_per_unit, 0.0f, 1.0f);
}

// ============================================================================
// Province-parallel execution
// ============================================================================

void DrugEconomyModule::execute_province(uint32_t province_idx, const WorldState& state,
                                         DeltaBuffer& province_delta) {
    if (province_idx >= state.provinces.size())
        return;
    const auto& province = state.provinces[province_idx];

    // Collect criminal drug businesses in this province, sorted by id ascending
    std::vector<const NPCBusiness*> drug_businesses;
    for (const auto& biz : state.npc_businesses) {
        if (biz.criminal_sector && biz.province_id == province.id) {
            drug_businesses.push_back(&biz);
        }
    }
    std::sort(drug_businesses.begin(), drug_businesses.end(),
              [](const NPCBusiness* a, const NPCBusiness* b) { return a->id < b->id; });

    // Default legalization status (all illegal except designer drugs)
    DrugLegalizationStatus leg_status{false, false, false, true};
    if (province_idx < legalization_status_.size()) {
        leg_status = legalization_status_[province_idx];
    }

    // Process each drug business
    for (const auto* biz : drug_businesses) {
        // Simplified: each criminal business produces a drug type based on sector
        // In full impl, this reads from the recipe registry
        float production_output = biz->revenue_per_tick * 0.1f;  // proxy for drug output
        if (production_output <= 0.0f)
            continue;

        // Assume cannabis type for province-based businesses; in full impl,
        // drug type comes from the business's assigned recipe
        DrugType drug_type = DrugType::cannabis;

        // Determine market tier (simplified: smaller businesses are retail)
        DrugMarketTier tier =
            (biz->market_share >= 0.1f) ? DrugMarketTier::wholesale : DrugMarketTier::retail;

        // Compute quality (starts at 0.85 for production)
        float base_quality = 0.85f;

        // Degrade quality through distribution tier
        float output_quality = base_quality;
        if (tier == DrugMarketTier::wholesale) {
            output_quality = degrade_quality(base_quality, WHOLESALE_QUALITY_DEGRADATION);
        } else {
            output_quality =
                degrade_quality(degrade_quality(base_quality, WHOLESALE_QUALITY_DEGRADATION),
                                RETAIL_QUALITY_DEGRADATION);
        }

        // Compute pricing
        // In full impl, this reads from RegionalMarket informal layer
        float spot_price = 100.0f;  // proxy
        float revenue = 0.0f;
        if (tier == DrugMarketTier::wholesale) {
            revenue =
                production_output * compute_wholesale_price(spot_price, WHOLESALE_PRICE_FRACTION);
        } else {
            revenue = production_output * spot_price;
        }

        // Add supply to informal market
        MarketDelta supply_delta;
        supply_delta.good_id = static_cast<uint32_t>(drug_type);
        supply_delta.region_id = province.id;
        supply_delta.supply_delta = production_output;
        province_delta.market_deltas.push_back(supply_delta);

        // BusinessDelta: credit drug revenue to the criminal business
        BusinessDelta biz_revenue;
        biz_revenue.business_id = biz->id;
        biz_revenue.cash_delta = revenue;
        biz_revenue.revenue_per_tick_update = revenue;
        province_delta.business_deltas.push_back(biz_revenue);

        // Compute precursor consumption (meth requires 2x precursor)
        if (drug_type == DrugType::methamphetamine) {
            float precursor =
                compute_precursor_consumption(production_output, PRECURSOR_RATIO_METH);
            MarketDelta precursor_demand;
            precursor_demand.good_id = 9999;  // proxy precursor good_id
            precursor_demand.region_id = province.id;
            precursor_demand.demand_buffer_delta = precursor;
            province_delta.market_deltas.push_back(precursor_demand);
        }

        // Generate evidence tokens for drug operations
        EvidenceDelta ev;
        ev.new_token =
            EvidenceToken{0,      EvidenceType::physical, biz->owner_id, biz->owner_id, 0.20f,
                          0.003f, state.current_tick,     province.id,   true};
        province_delta.evidence_deltas.push_back(ev);

        // Track production
        production_records_.push_back(DrugProductionRecord{
            biz->id, drug_type, tier, production_output, output_quality, 0.0f, province.id});
    }

    // Consumer demand from addiction rates
    float addiction_rate = province.conditions.addiction_rate;
    if (addiction_rate > 0.0f && province.cohort_stats) {
        float demand = compute_addiction_demand(
            addiction_rate, province.cohort_stats->total_population, DEMAND_PER_ADDICT);

        MarketDelta demand_delta;
        demand_delta.good_id = 0;  // aggregate drug demand
        demand_delta.region_id = province.id;
        demand_delta.demand_buffer_delta = demand;
        province_delta.market_deltas.push_back(demand_delta);

        // RegionDelta: addiction_rate_delta grows proportionally to consumption volume
        // Consumption approximated as min(demand, total supply available this tick).
        // Use demand as upper bound; scale factor keeps the per-tick delta small.
        constexpr float ADDICTION_GROWTH_PER_UNIT = 0.000001f;
        RegionDelta region;
        region.region_id = province.id;
        region.addiction_rate_delta = demand * ADDICTION_GROWTH_PER_UNIT;
        province_delta.region_deltas.push_back(region);
    }
}

void DrugEconomyModule::execute(const WorldState& state, DeltaBuffer& delta) {
    production_records_.clear();
    for (uint32_t i = 0; i < state.provinces.size(); ++i) {
        execute_province(i, state, delta);
    }
}

}  // namespace econlife
