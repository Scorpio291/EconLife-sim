#include "modules/npc_spending/npc_spending_module.h"

#include <algorithm>
#include <cmath>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"

namespace econlife {

// ---------------------------------------------------------------------------
// Static utility implementations
// ---------------------------------------------------------------------------

float NpcSpendingModule::compute_income_factor(float capital, float reference_income,
                                               float income_elasticity, float max_income_factor) {
    if (reference_income <= 0.0f)
        return 0.0f;
    float ratio = capital / reference_income;
    if (ratio <= 0.0f)
        return 0.0f;
    float factor = std::pow(ratio, income_elasticity);
    return std::min(std::max(factor, 0.0f), max_income_factor);
}

float NpcSpendingModule::compute_price_factor(float base_price, float spot_price,
                                              float price_elasticity, BuyerType buyer_type,
                                              float min_price_factor) {
    float effective_spot = std::max(spot_price, 0.01f);
    float modulator = buyer_type_elasticity_modulator(buyer_type);
    float adjusted_elasticity = std::abs(price_elasticity) * modulator;
    float ratio = base_price / effective_spot;
    float factor = std::pow(ratio, adjusted_elasticity);
    return std::max(factor, min_price_factor);
}

float NpcSpendingModule::compute_quality_factor(float batch_quality, float market_quality_avg,
                                                BuyerType buyer_type) {
    float qw = buyer_type_quality_weight(buyer_type);
    float factor = 1.0f + qw * (batch_quality - market_quality_avg);
    return std::max(factor, 0.0f);  // quality factor cannot be negative
}

float NpcSpendingModule::compute_demand_contribution(float base_demand_units, float income_factor,
                                                     float price_factor, float quality_factor) {
    float demand = base_demand_units * income_factor * price_factor * quality_factor;
    return std::max(demand, 0.0f);  // demand cannot be negative
}

float NpcSpendingModule::buyer_type_elasticity_modulator(BuyerType buyer_type) {
    switch (buyer_type) {
        case BuyerType::necessity_buyer:
            return 0.1f;
        case BuyerType::price_sensitive:
            return 1.5f;
        case BuyerType::quality_seeker:
            return 0.6f;
        case BuyerType::brand_loyal:
            return 0.8f;
        default:
            return 1.0f;
    }
}

float NpcSpendingModule::buyer_type_quality_weight(BuyerType buyer_type) {
    switch (buyer_type) {
        case BuyerType::price_sensitive:
            return 0.0f;
        case BuyerType::brand_loyal:
            return 0.3f;
        case BuyerType::quality_seeker:
            return 0.6f;
        case BuyerType::necessity_buyer:
            return 0.0f;
        default:
            return 0.0f;
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

BuyerType NpcSpendingModule::get_buyer_type(uint32_t npc_id) const {
    if (buyer_profile_index_.size() != buyer_profiles_.size()) {
        buyer_profile_index_.clear();
        buyer_profile_index_.reserve(buyer_profiles_.size());
        for (std::size_t i = 0; i < buyer_profiles_.size(); ++i) {
            buyer_profile_index_[buyer_profiles_[i].npc_id] = i;
        }
    }
    auto it = buyer_profile_index_.find(npc_id);
    if (it == buyer_profile_index_.end()) {
        return BuyerType::necessity_buyer;  // default
    }
    return buyer_profiles_[it->second].buyer_type;
}

// ---------------------------------------------------------------------------
// execute_province — province-parallel consumer demand computation
// ---------------------------------------------------------------------------

void NpcSpendingModule::execute_province(uint32_t province_idx, const WorldState& state,
                                         DeltaBuffer& province_delta) {
    if (province_idx >= state.provinces.size())
        return;
    const auto& province = state.provinces[province_idx];

    // Collect active NPCs in this province, sorted by id ascending for determinism.
    std::vector<const NPC*> province_npcs;
    for (const auto& npc : state.significant_npcs) {
        if (npc.current_province_id == province.id && npc.status == NPCStatus::active) {
            province_npcs.push_back(&npc);
        }
    }
    std::sort(province_npcs.begin(), province_npcs.end(),
              [](const NPC* a, const NPC* b) { return a->id < b->id; });

    if (province_npcs.empty())
        return;

    // For each regional market in this province, compute consumer demand.
    // Markets sorted by good_id ascending for deterministic accumulation.
    std::vector<const RegionalMarket*> province_markets;
    for (const auto& market : state.regional_markets) {
        if (market.province_id == province.id) {
            province_markets.push_back(&market);
        }
    }
    std::sort(
        province_markets.begin(), province_markets.end(),
        [](const RegionalMarket* a, const RegionalMarket* b) { return a->good_id < b->good_id; });

    // Track total spending per NPC across all markets for NPCDelta emission.
    // NPCs are charged only for the goods they actually receive (consumed share),
    // not for their full expressed demand. This prevents capital drain when supply is scarce.
    std::vector<float> npc_total_spending(province_npcs.size(), 0.0f);

    // Per-market demand buffer — pre-allocated outside market loop to avoid per-market allocation.
    std::vector<float> npc_demand_this_market(province_npcs.size(), 0.0f);

    for (const auto* market : province_markets) {
        float total_demand = 0.0f;
        std::fill(npc_demand_this_market.begin(), npc_demand_this_market.end(), 0.0f);

        // Pass 1: collect per-NPC demand (same formula as before).
        for (size_t ni = 0; ni < province_npcs.size(); ++ni) {
            const auto* npc = province_npcs[ni];
            BuyerType bt = get_buyer_type(npc->id);

            float income_f =
                compute_income_factor(npc->capital, cfg_.reference_income,
                                      cfg_.default_income_elasticity, cfg_.max_income_factor);

            float price_f =
                compute_price_factor(cfg_.default_base_price, market->spot_price,
                                     cfg_.default_price_elasticity, bt, cfg_.min_price_factor);

            // Use 0.5 as default batch_quality and market_quality_avg
            // (no per-good quality data in V1 bootstrap)
            float quality_f = compute_quality_factor(0.5f, 0.5f, bt);

            float demand = compute_demand_contribution(cfg_.default_base_demand_units, income_f,
                                                       price_f, quality_f);

            total_demand += demand;
            npc_demand_this_market[ni] = demand;
        }

        if (total_demand <= 0.0f)
            continue;

        float available = std::max(market->supply, 0.0f);
        float consumed = std::min(total_demand, available);
        // consumption_ratio: fraction of demand that is actually fulfilled by available supply.
        // Full demand signal still reaches price_engine (demand_buffer_delta = total_demand).
        float consumption_ratio = consumed / total_demand;  // in [0, 1]
        float spot = std::max(market->spot_price, 0.0f);

        // Pass 2: charge each NPC only for their consumed share.
        for (size_t ni = 0; ni < province_npcs.size(); ++ni) {
            npc_total_spending[ni] += npc_demand_this_market[ni] * consumption_ratio * spot;
        }

        // Write demand contribution and supply consumption to delta buffer.
        MarketDelta md;
        md.good_id = market->good_id;
        md.region_id = province.id;
        md.demand_buffer_delta = total_demand;  // full demand reaches price_engine
        md.supply_delta = -consumed;            // only consumed goods leave supply
        province_delta.market_deltas.push_back(md);
    }

    // Emit NPCDelta for each NPC that spent capital this tick.
    // capital_delta is negative (spending reduces capital).
    for (size_t ni = 0; ni < province_npcs.size(); ++ni) {
        float spent = npc_total_spending[ni];
        if (spent <= 0.0f)
            continue;

        NPCDelta npc_delta;
        npc_delta.npc_id = province_npcs[ni]->id;
        npc_delta.capital_delta = -spent;
        province_delta.npc_deltas.push_back(npc_delta);
    }
}

// ---------------------------------------------------------------------------
// execute — fallback for sequential execution (delegates to province-parallel)
// ---------------------------------------------------------------------------

void NpcSpendingModule::execute(const WorldState& state, DeltaBuffer& delta) {
    for (uint32_t i = 0; i < state.provinces.size(); ++i) {
        execute_province(i, state, delta);
    }
}

}  // namespace econlife
