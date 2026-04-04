// Production Module — implementation.
// See production_module.h for class declarations and
// docs/interfaces/production/INTERFACE.md for the canonical specification.

#include "modules/production/production_module.h"

#include <algorithm>
#include <cmath>

#include "core/rng/deterministic_rng.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"

namespace econlife {

// ===========================================================================
// RecipeRegistry
// ===========================================================================

void RecipeRegistry::register_recipe(Recipe recipe) {
    const std::string id = recipe.id;
    recipes_[id] = std::move(recipe);
}

const Recipe* RecipeRegistry::find(const std::string& recipe_id) const {
    auto it = recipes_.find(recipe_id);
    if (it != recipes_.end()) {
        return &it->second;
    }
    return nullptr;
}

// ===========================================================================
// FacilityRegistry
// ===========================================================================

void FacilityRegistry::register_facility(Facility facility) {
    facilities_by_business_[facility.business_id].push_back(std::move(facility));
}

const std::vector<Facility>* FacilityRegistry::find_by_business(uint32_t business_id) const {
    auto it = facilities_by_business_.find(business_id);
    if (it != facilities_by_business_.end()) {
        return &it->second;
    }
    return nullptr;
}

// ===========================================================================
// ProductionModule — init from WorldState
// ===========================================================================

void ProductionModule::init_from_world_state(const WorldState& state) {
    // Populate recipe registry from WorldState.loaded_recipes.
    for (const auto& recipe : state.loaded_recipes) {
        recipe_registry_.register_recipe(recipe);
    }
    // Populate facility registry from WorldState.facilities.
    for (const auto& facility : state.facilities) {
        facility_registry_.register_facility(facility);
    }
}

// ===========================================================================
// ProductionModule — utility
// ===========================================================================

uint32_t ProductionModule::good_id_from_string(const std::string& good_id_str) {
    // Deterministic hash for V1 prototype.
    // The goods registry will provide the canonical mapping in full implementation.
    uint32_t hash = 0;
    for (char c : good_id_str) {
        hash = hash * 31 + static_cast<uint32_t>(c);
    }
    return hash;
}

// ===========================================================================
// ProductionModule — tick execution
// ===========================================================================

void ProductionModule::execute_province(uint32_t province_idx, const WorldState& state,
                                        DeltaBuffer& province_delta) {
    // Lazy-init: populate registries from WorldState on first execution.
    // std::call_once is thread-safe for province-parallel dispatch.
    std::call_once(init_flag_, [this, &state]() { init_from_world_state(state); });

    // Fork RNG with province_id for deterministic province-parallel work.
    DeterministicRNG rng = DeterministicRNG(state.world_seed).fork(province_idx);

    // Collect businesses for this province, sorted by business id (ascending)
    // for deterministic processing order.
    std::vector<const NPCBusiness*> province_businesses;
    for (const auto& biz : state.npc_businesses) {
        if (biz.province_id == province_idx) {
            province_businesses.push_back(&biz);
        }
    }

    // Sort by business_id ascending for deterministic order.
    std::sort(province_businesses.begin(), province_businesses.end(),
              [](const NPCBusiness* a, const NPCBusiness* b) { return a->id < b->id; });

    // Track supply consumed per good in this province to prevent over-consumption.
    // Maps good_id (string) -> remaining available supply.
    std::unordered_map<std::string, float> available_supply;

    // Pre-populate available supply from regional markets for this province.
    // We need to map from uint32_t market.good_id back to string keys.
    // Build reverse lookup from registered recipes' input/output good_id strings.
    std::unordered_map<uint32_t, std::string> id_to_string;
    for (const auto& [recipe_id, recipe] : recipe_registry_.all()) {
        for (const auto& input : recipe.inputs) {
            id_to_string[good_id_from_string(input.good_id)] = input.good_id;
        }
        for (const auto& output : recipe.outputs) {
            id_to_string[good_id_from_string(output.good_id)] = output.good_id;
        }
    }
    for (const auto& market : state.regional_markets) {
        if (market.province_id == province_idx) {
            auto it = id_to_string.find(market.good_id);
            if (it != id_to_string.end()) {
                available_supply[it->second] = market.supply;
            }
        }
    }

    // Process each business.
    for (const NPCBusiness* biz : province_businesses) {
        process_business(*biz, state, province_delta, available_supply, rng);
    }
}

void ProductionModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Province-parallel modules dispatch through execute_province().
    // This fallback processes all provinces sequentially if called directly.
    for (uint32_t p = 0; p < static_cast<uint32_t>(state.provinces.size()); ++p) {
        execute_province(p, state, delta);
    }
}

// ===========================================================================
// ProductionModule — per-business processing
// ===========================================================================

void ProductionModule::process_business(const NPCBusiness& biz, const WorldState& state,
                                        DeltaBuffer& delta,
                                        std::unordered_map<std::string, float>& available_supply,
                                        DeterministicRNG& rng) {
    // Skip bankrupt businesses: cash <= 0 and no revenue.
    if (biz.cash <= 0.0f && biz.revenue_per_tick <= 0.0f) {
        return;
    }

    // Look up facilities for this business.
    const auto* facilities = facility_registry_.find_by_business(biz.id);
    if (!facilities || facilities->empty()) {
        return;
    }

    // Process each operational facility.
    for (const Facility& facility : *facilities) {
        if (!facility.is_operational) {
            continue;
        }

        // Only process facilities in the same province as the business.
        if (facility.province_id != biz.province_id) {
            continue;
        }

        process_facility(biz, facility, state, delta, available_supply, rng);
    }
}

// ===========================================================================
// ProductionModule — per-facility processing
// ===========================================================================

void ProductionModule::process_facility(const NPCBusiness& biz, const Facility& facility,
                                        const WorldState& state, DeltaBuffer& delta,
                                        std::unordered_map<std::string, float>& available_supply,
                                        DeterministicRNG& rng) {
    // Look up recipe.
    const Recipe* recipe = recipe_registry_.find(facility.recipe_id);
    if (!recipe) {
        // Missing recipe: skip facility. Business still incurs fixed operating
        // costs but produces nothing. (TODO: proper logging)
        return;
    }

    // Compute tech tier bonus.
    int32_t tier_diff =
        static_cast<int32_t>(facility.tech_tier) - static_cast<int32_t>(recipe->min_tech_tier);
    int32_t effective_tier_diff = std::max(0, tier_diff);

    float output_multiplier =
        1.0f + cfg_.tech_tier_output_bonus * static_cast<float>(effective_tier_diff);

    float cost_multiplier =
        1.0f - cfg_.tech_tier_cost_reduction * static_cast<float>(effective_tier_diff);

    // Determine input availability — compute bottleneck ratio.
    // The bottleneck ratio is the minimum ratio of (available / required)
    // across all inputs. If any input is insufficient, output is clamped
    // proportionally.
    float bottleneck_ratio = 1.0f;

    // Sort inputs by good_id ascending for deterministic floating-point accumulation.
    std::vector<const RecipeInput*> sorted_inputs;
    sorted_inputs.reserve(recipe->inputs.size());
    for (const auto& input : recipe->inputs) {
        sorted_inputs.push_back(&input);
    }
    std::sort(sorted_inputs.begin(), sorted_inputs.end(),
              [](const RecipeInput* a, const RecipeInput* b) { return a->good_id < b->good_id; });

    for (const RecipeInput* input : sorted_inputs) {
        float required = input->quantity_per_tick;
        if (required <= 0.0f) {
            continue;
        }
        float available = 0.0f;
        auto it = available_supply.find(input->good_id);
        if (it != available_supply.end()) {
            available = it->second;
        }
        float ratio = available / required;
        bottleneck_ratio = std::min(bottleneck_ratio, ratio);
    }

    // Clamp bottleneck to [0, 1].
    bottleneck_ratio = std::max(0.0f, std::min(1.0f, bottleneck_ratio));

    // Consume inputs (proportional to bottleneck ratio).
    // Record derived demand for each consumed input.
    for (const RecipeInput* input : sorted_inputs) {
        float consumed = input->quantity_per_tick * bottleneck_ratio;
        if (consumed <= 0.0f) {
            continue;
        }

        // Reduce available supply.
        auto it = available_supply.find(input->good_id);
        if (it != available_supply.end()) {
            it->second = std::max(0.0f, it->second - consumed);
        }

        // Write demand_buffer_delta for derived demand.
        MarketDelta demand_delta{};
        demand_delta.good_id = good_id_from_string(input->good_id);
        demand_delta.region_id = biz.province_id;
        demand_delta.demand_buffer_delta = consumed;
        delta.market_deltas.push_back(demand_delta);
    }

    // Compute outputs — sort by good_id ascending for deterministic accumulation.
    std::vector<const RecipeOutput*> sorted_outputs;
    sorted_outputs.reserve(recipe->outputs.size());
    for (const auto& output : recipe->outputs) {
        sorted_outputs.push_back(&output);
    }
    std::sort(sorted_outputs.begin(), sorted_outputs.end(),
              [](const RecipeOutput* a, const RecipeOutput* b) { return a->good_id < b->good_id; });

    // Compute quality ceiling.
    float quality_ceiling =
        cfg_.tech_quality_ceiling_base +
        cfg_.tech_quality_ceiling_step * static_cast<float>(effective_tier_diff);

    // For technology-intensive recipes, cap by maturation level.
    // If the recipe has a key_technology_node, the actor's maturation level
    // for that node caps the quality ceiling.
    if (!recipe->key_technology_node.empty()) {
        float maturation = biz.actor_tech_state.maturation_of(recipe->key_technology_node);
        if (maturation > 0.0f) {
            quality_ceiling = std::min(quality_ceiling, maturation);
        } else {
            // Actor has no maturation for required tech node.
            // Per spec: maturation_of() returns 0.0 if not held → quality is capped at 0.
            // However, for baseline recipes (no tech required), this path isn't reached.
            // For non-baseline recipes requiring tech the actor doesn't have,
            // quality is severely limited but production still occurs (low quality output).
            quality_ceiling = std::min(quality_ceiling, 0.1f);
        }
    }

    // Clamp quality ceiling to valid range.
    quality_ceiling = std::max(0.0f, std::min(1.0f, quality_ceiling));

    // Worker count throughput effect.
    // Baseline assumes 1 worker. Each additional worker adds diminishing returns.
    // Formula: worker_multiplier = min(worker_count, 1) + 0.15 * max(0, worker_count - 1)
    // Capped so that 10 workers = 1 + 0.15*9 = 2.35x throughput (not 10x).
    float worker_multiplier = 1.0f;
    if (facility.worker_count > 1) {
        worker_multiplier = 1.0f + 0.15f * static_cast<float>(facility.worker_count - 1);
    } else if (facility.worker_count == 0) {
        worker_multiplier = 0.0f;  // no workers = no production
    }

    float total_revenue = 0.0f;

    for (const RecipeOutput* output : sorted_outputs) {
        float actual_output = output->quantity_per_tick * output_multiplier * bottleneck_ratio *
                              worker_multiplier * facility.output_rate_modifier;

        // Clamp NaN or negative to 0.
        if (std::isnan(actual_output) || actual_output < 0.0f) {
            actual_output = 0.0f;
        }

        if (actual_output <= 0.0f) {
            continue;
        }

        // Write supply_delta.
        MarketDelta supply_delta{};
        supply_delta.good_id = good_id_from_string(output->good_id);
        supply_delta.region_id = biz.province_id;
        supply_delta.supply_delta = actual_output;
        delta.market_deltas.push_back(supply_delta);

        // Calculate revenue using appropriate price.
        float price = get_price_for_business(biz, supply_delta.good_id, state);
        total_revenue += actual_output * price;
    }

    // Operating cost computed for downstream modules.
    float actual_cost = recipe->base_cost_per_tick * cost_multiplier;
    if (std::isnan(actual_cost) || actual_cost < 0.0f) {
        actual_cost = 0.0f;
    }

    // Write BusinessDelta for revenue, cost, and quality.
    BusinessDelta biz_delta{};
    biz_delta.business_id = biz.id;
    biz_delta.cash_delta = total_revenue - actual_cost;
    biz_delta.revenue_per_tick_update = total_revenue;
    biz_delta.cost_per_tick_update = actual_cost;
    biz_delta.output_quality_update = quality_ceiling;
    delta.business_deltas.push_back(biz_delta);
}

// ===========================================================================
// ProductionModule — price lookup
// ===========================================================================

float ProductionModule::get_price_for_business(const NPCBusiness& biz, uint32_t good_id,
                                               const WorldState& state) const {
    // Find the regional market for this good in this province.
    for (const auto& market : state.regional_markets) {
        if (market.good_id == good_id && market.province_id == biz.province_id) {
            if (biz.criminal_sector) {
                // Criminal sector uses informal price.
                // In V1, informal price is approximated as a discount on
                // spot_price. The full informal market model is expansion scope.
                return market.spot_price * informal_price_discount;
            }
            return market.spot_price;
        }
    }
    // No market found; return 0 to avoid uninitialized access.
    return 0.0f;
}

}  // namespace econlife
