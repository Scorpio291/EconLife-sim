// Production Module — implementation.
// See production_module.h for class declarations and
// docs/interfaces/production/INTERFACE.md for the canonical specification.

#include "modules/production/production_module.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/good_id_hash.h"
#include "core/rng/deterministic_rng.h"
#include "core/world_state/apply_deltas.h"  // lookup_market, markets_in_province
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
    last_synced_facility_count_ = state.facilities.size();
}

void ProductionModule::init_for_tick(const WorldState& state) {
    // First call seeds the registry via the same code path as the
    // execute_province std::call_once; subsequent calls register only
    // the tail (facilities delivered by Phase 11 construction since the
    // previous tick). state.facilities is append-only — apply_deltas.cpp
    // adds via apply_new_facilities() and there is no facility-deletion
    // delta — so a monotonically growing prefix is safe to skip.
    std::call_once(init_flag_, [this, &state]() { init_from_world_state(state); });
    for (std::size_t i = last_synced_facility_count_; i < state.facilities.size(); ++i) {
        facility_registry_.register_facility(state.facilities[i]);
    }
    last_synced_facility_count_ = state.facilities.size();
}

// ===========================================================================
// ProductionModule — utility
// ===========================================================================

uint32_t ProductionModule::good_id_from_string(const std::string& good_id_str) {
    return good_id_hash(good_id_str);
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
    // Build a numeric_id → string reverse lookup from registered recipes,
    // routing through lookup_good_id() so the ids match RegionalMarket.good_id
    // (catalog numeric_id when WorldState has a catalog; FNV-1a hash in tests
    // that build markets without one).
    std::unordered_map<uint32_t, std::string> id_to_string;
    for (const auto& [recipe_id, recipe] : recipe_registry_.all()) {
        for (const auto& input : recipe.inputs) {
            // Skip unknown goods so we don't stamp id 0 onto a real market.
            // lookup_good_id() returns 0 when WorldState has a catalog but
            // doesn't know the string — typically a recipe-CSV typo. The
            // recipe ↔ goods cross-validation in world_generator surfaces
            // these at load; this guard is defence-in-depth.
            const uint32_t gid = lookup_good_id(state, input.good_id);
            if (gid != 0)
                id_to_string[gid] = input.good_id;
        }
        for (const auto& output : recipe.outputs) {
            const uint32_t gid = lookup_good_id(state, output.good_id);
            if (gid != 0)
                id_to_string[gid] = output.good_id;
        }
    }
    // The motive-power pre-pass may burn fuels (biomass/fossil) that are not a local
    // recipe input/output, so make their province stock visible too. Mirrors the fuel
    // set in supply_province_power().
    for (const char* fuel : {"wood_chips", "softwood_logs", "hardwood_logs", "thermal_coal",
                             "natural_gas", "crude_oil"}) {
        const uint32_t gid = lookup_good_id(state, fuel);
        if (gid != 0)
            id_to_string[gid] = fuel;
    }
    for (uint32_t i : markets_in_province(state, province_idx)) {
        const auto& market = state.regional_markets[i];
        auto it = id_to_string.find(market.good_id);
        if (it != id_to_string.end()) {
            available_supply[it->second] = market.supply;
        }
    }

    // Motive-power pre-pass: supply the province's electricity, mechanical work, and
    // process heat from its endowment (burning biomass/fossil for the shortfalls,
    // conserving matter) before any facility runs, so the same-tick availability ratios
    // throttle production per power form when a form is scarce.
    const ProvincePower power = supply_province_power(
        province_idx, state, province_businesses, available_supply, province_delta);

    // Process each business.
    for (const NPCBusiness* biz : province_businesses) {
        process_business(*biz, state, province_delta, available_supply, power, rng);
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
                                        const ProvincePower& power, DeterministicRNG& rng) {
    // Skip bankrupt businesses: cash <= 0 and no revenue.
    if (biz.cash <= 0.0f && biz.revenue_per_tick <= 0.0f) {
        return;
    }

    // Look up facilities for this business.
    const auto* facilities = facility_registry_.find_by_business(biz.id);
    if (!facilities || facilities->empty()) {
        // Facility-less legit businesses still run real operations (services, trade,
        // light manufacturing modelled abstractly via revenue_per_tick rather than a
        // recipe). Without a cash inflow here they would only ever PAY OUT (wages,
        // owner draws) and never accumulate, so legit enterprise could not build
        // wealth — only crime could. Credit their operating profit so successful
        // legit firms enrich their owners like real businesses. Criminal businesses
        // are excluded: their cash is credited by the criminal-economy modules.
        if (!biz.criminal_sector && biz.revenue_per_tick > 0.0f) {
            BusinessDelta biz_delta{};
            biz_delta.business_id = biz.id;
            biz_delta.cash_delta = biz.revenue_per_tick - biz.cost_per_tick;
            delta.business_deltas.push_back(biz_delta);
        }
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

        process_facility(biz, facility, state, delta, available_supply, power, rng);
    }
}

// Find a usable deposit of the given resource type in a province for extraction.
// "Usable" = era-unlocked, accessible (accessibility > 0), and not exhausted.
// Among candidates, prefers the highest-grade deposit, tie-broken by lowest id
// for determinism. Returns nullptr if the province holds no usable deposit of
// the type — the existence precondition for exploitation.
static const ResourceDeposit* find_extractable_deposit(const WorldState& state,
                                                       uint32_t province_id, ResourceType type) {
    if (province_id >= state.provinces.size()) {
        return nullptr;
    }
    const auto current_era = static_cast<uint8_t>(state.technology.current_era);
    const ResourceDeposit* best = nullptr;
    for (const ResourceDeposit& dep : state.provinces[province_id].deposits) {
        if (dep.type != type)
            continue;
        if (dep.era_unlock > current_era)
            continue;
        if (dep.accessibility <= 0.0f)
            continue;
        if (dep.quantity_remaining <= 0.0f)
            continue;
        if (best == nullptr || dep.quality > best->quality ||
            (dep.quality == best->quality && dep.id < best->id)) {
            best = &dep;
        }
    }
    return best;
}

// Waste typing by output good category (conservation: matter not embodied in the
// product leaves the process as waste that must be handled). Returns the waste good
// and the per-unit-output rate; {nullptr, 0} for goods with no physical waste stream
// (services, financial, energy, and waste itself). Covers every product via category.
namespace {
struct WasteSpec {
    const char* good;
    float rate;
};
WasteSpec waste_for_category(const std::string& cat, const ProductionConfig& cfg) {
    if (cat == "petroleum" || cat == "chemicals" || cat == "pharmaceutical")
        return {"hazardous_waste", cfg.waste_rate_hazardous};
    if (cat == "electronics")
        return {"hazardous_waste", cfg.waste_rate_ewaste};
    if (cat == "heavy_industry" || cat == "metals" || cat == "geological" || cat == "vehicle" ||
        cat == "vehicles")
        return {"industrial_waste", cfg.waste_rate_heavy};
    if (cat == "construction" || cat == "structural")
        return {"industrial_waste", cfg.waste_rate_construction};
    if (cat == "food_beverage" || cat == "food_processing" || cat == "agricultural" ||
        cat == "biological" || cat == "textiles" || cat == "apparel" || cat == "timber")
        return {"industrial_waste", cfg.waste_rate_light};
    return {nullptr, 0.0f};
}
}  // namespace

float ProductionModule::burn_fuels(const std::vector<std::pair<std::string, float>>& fuels,
                                   float energy_needed, uint32_t province_idx,
                                   const WorldState& state,
                                   std::unordered_map<std::string, float>& available_supply,
                                   DeltaBuffer& delta) const {
    float supplied = 0.0f;
    for (const auto& [fuel, yield_per_unit] : fuels) {
        if (energy_needed - supplied <= 0.0f)
            break;
        if (yield_per_unit <= 0.0f)
            continue;
        auto it = available_supply.find(fuel);
        if (it == available_supply.end() || it->second <= 0.0f)
            continue;
        const float fuel_needed = (energy_needed - supplied) / yield_per_unit;
        const float burn = std::min(fuel_needed, it->second);
        if (burn <= 0.0f)
            continue;
        it->second -= burn;  // scratch: dwindling stock seen by later consumers (conservation)
        supplied += burn * yield_per_unit;
        const uint32_t fuel_gid = lookup_good_id(state, fuel);
        if (fuel_gid != 0) {
            MarketDelta md{};
            md.good_id = fuel_gid;
            md.region_id = province_idx;
            md.supply_delta = -burn;  // fuel matter consumed
            delta.market_deltas.push_back(md);
        }
    }
    return supplied;
}

ProvincePower ProductionModule::supply_province_power(
    uint32_t province_idx, const WorldState& state,
    const std::vector<const NPCBusiness*>& province_businesses,
    std::unordered_map<std::string, float>& available_supply, DeltaBuffer& delta) const {
    // 1. Per-form demand = sum of the matching per_tick requirement over operational,
    //    era-available facilities in this province.
    const auto current_era = static_cast<uint8_t>(state.technology.current_era);
    float demand_elec = 0.0f;
    float demand_mech = 0.0f;
    float demand_fuel = 0.0f;
    for (const NPCBusiness* biz : province_businesses) {
        const auto* facs = facility_registry_.find_by_business(biz->id);
        if (facs == nullptr)
            continue;
        for (const Facility& f : *facs) {
            if (!f.is_operational || f.province_id != province_idx)
                continue;
            const Recipe* r = recipe_registry_.find(f.recipe_id);
            if (r == nullptr || r->era_available > current_era)
                continue;
            demand_elec += r->energy_per_tick;
            demand_mech += r->mechanical_per_tick;
            demand_fuel += r->fuel_per_tick;
        }
    }

    ProvincePower power;  // defaults to all-met (1.0); forms with zero demand stay 1.0
    if (demand_elec <= 0.0f && demand_mech <= 0.0f && demand_fuel <= 0.0f) {
        return power;  // no powered facilities → nothing to supply (muscle-only)
    }

    // 2. Matter-free renewable flows from the province endowment (capacity is not
    //    depleted). Electricity taps solar/wind/geothermal + hydro; mechanical taps
    //    the same water/wind flows as direct drive (waterwheel/windmill).
    float renewable_elec = 0.0f;  // → electricity
    float mech_flow = 0.0f;       // → mechanical direct-drive
    if (province_idx < state.provinces.size()) {
        const Province& prov = state.provinces[province_idx];
        for (const ResourceDeposit& dep : prov.deposits) {
            const float cap = dep.quantity * dep.quality * cfg_.renewable_mwh_per_capacity;
            if (dep.type == ResourceType::SolarPotential ||
                dep.type == ResourceType::WindPotential || dep.type == ResourceType::Geothermal) {
                renewable_elec += cap;
            }
            if (dep.type == ResourceType::WindPotential) {
                mech_flow += cap;  // windmill direct drive
            }
        }
        // Hydropower / water wheels from river flow regime: sustained (perennial/
        // glacier-fed) flow supports steady work; ephemeral flow only seasonal.
        float hydro = 0.0f;
        switch (prov.geography.river_flow_regime) {
            case RiverFlowRegime::RainfedPerennial:
            case RiverFlowRegime::SnowmeltPerennial:
            case RiverFlowRegime::Glacierfed:
                hydro = cfg_.hydro_mwh_perennial;
                break;
            case RiverFlowRegime::SnowmeltEphemeral:
            case RiverFlowRegime::RainfedEphemeral:
                hydro = cfg_.hydro_mwh_seasonal;
                break;
            case RiverFlowRegime::None:
                break;
        }
        renewable_elec += hydro;  // hydroelectric
        mech_flow += hydro;       // water-wheel direct drive
    }

    // Canonical fuel order for deterministic accumulation. Biomass first (era-appropriate
    // and cheaper as heat/steam), then fossil. The same stock backs all three forms;
    // sequencing the burns through `available_supply` conserves matter across them.
    const std::vector<std::pair<std::string, float>> kBiomassThenFossil = {
        {"wood_chips", cfg_.biomass_mwh_per_fuel_unit},
        {"softwood_logs", cfg_.biomass_mwh_per_fuel_unit},
        {"hardwood_logs", cfg_.biomass_mwh_per_fuel_unit},
        {"thermal_coal", cfg_.fossil_mwh_per_fuel_unit},
        {"natural_gas", cfg_.fossil_mwh_per_fuel_unit},
        {"crude_oil", cfg_.fossil_mwh_per_fuel_unit},
    };

    // 3. Electricity: renewables matter-free, shortfall by burning fossil only (a grid
    //    runs on fossil/renewable, not raw cordwood). Preserves the prior energy model.
    if (demand_elec > 0.0f) {
        const float renewable_used = std::min(renewable_elec, demand_elec);
        const std::vector<std::pair<std::string, float>> kFossilOnly = {
            {"crude_oil", cfg_.fossil_mwh_per_fuel_unit},
            {"natural_gas", cfg_.fossil_mwh_per_fuel_unit},
            {"thermal_coal", cfg_.fossil_mwh_per_fuel_unit},
        };
        const float fossil_gen = burn_fuels(kFossilOnly, demand_elec - renewable_used, province_idx,
                                            state, available_supply, delta);
        const float generation = renewable_used + fossil_gen;
        const float consumed = std::min(generation, demand_elec);
        // Electricity as a real good: generated and consumed this tick. Net market
        // supply = generation - consumed (≈0; not stored at grid scale). demand_buffer
        // carries the consumption signal for price formation.
        const uint32_t elec_gid = lookup_good_id(state, "electricity");
        if (elec_gid != 0) {
            MarketDelta md{};
            md.good_id = elec_gid;
            md.region_id = province_idx;
            md.supply_delta = generation - consumed;
            md.demand_buffer_delta = demand_elec;
            delta.market_deltas.push_back(md);
        }
        power.electricity = std::max(0.0f, std::min(1.0f, generation / demand_elec));
    }

    // 4. Mechanical work: water/wind direct-drive matter-free, shortfall by burning fuel
    //    (steam engine). Draws the shared stock after the electricity burn.
    if (demand_mech > 0.0f) {
        const float flow_used = std::min(mech_flow, demand_mech);
        const float steam_gen = burn_fuels(kBiomassThenFossil, demand_mech - flow_used,
                                           province_idx, state, available_supply, delta);
        power.mechanical =
            std::max(0.0f, std::min(1.0f, (flow_used + steam_gen) / demand_mech));
    }

    // 5. Process heat: burning fuel only (no matter-free heat). Draws the shared stock
    //    after the electricity and mechanical burns.
    if (demand_fuel > 0.0f) {
        const float heat_gen = burn_fuels(kBiomassThenFossil, demand_fuel, province_idx, state,
                                          available_supply, delta);
        power.fuel = std::max(0.0f, std::min(1.0f, heat_gen / demand_fuel));
    }

    return power;
}

// ===========================================================================
// ProductionModule — per-facility processing
// ===========================================================================

void ProductionModule::process_facility(const NPCBusiness& biz, const Facility& facility,
                                        const WorldState& state, DeltaBuffer& delta,
                                        std::unordered_map<std::string, float>& available_supply,
                                        const ProvincePower& power, DeterministicRNG& /*rng*/) {
    // Look up recipe. Recipe ↔ facility cross-validation runs at world load
    // (RecipeCatalog::validate_against_goods); silently skipping here keeps
    // a stale or mod-introduced recipe_id from breaking determinism.
    const Recipe* recipe = recipe_registry_.find(facility.recipe_id);
    if (!recipe) {
        return;
    }

    // Motive-power throttle: scale by the bottleneck across only the power forms this
    // recipe actually requires. A form with a zero requirement does not constrain it,
    // so a water/biomass-powered recipe is unaffected by an electricity shortage.
    float power_ratio = 1.0f;
    if (recipe->energy_per_tick > 0.0f)
        power_ratio = std::min(power_ratio, power.electricity);
    if (recipe->mechanical_per_tick > 0.0f)
        power_ratio = std::min(power_ratio, power.mechanical);
    if (recipe->fuel_per_tick > 0.0f)
        power_ratio = std::min(power_ratio, power.fuel);

    // Skip recipes not yet available in the current era.
    if (recipe->era_available > static_cast<uint8_t>(state.technology.current_era)) {
        return;
    }

    // Extraction binding: a deposit-bound recipe can only run where the province
    // physically holds a matching, usable deposit. This is the existence
    // precondition — a resource that is not located here cannot be exploited here.
    // "Usable" = era-unlocked, accessible (accessibility > 0; permafrost/locked
    // deposits seed to 0 until thaw + tech), and not yet exhausted. The chosen
    // deposit's quality scales output and its remaining quantity caps it; running
    // the recipe depletes it (emitted as a DepositDelta). If no usable deposit
    // exists, the facility produces nothing and incurs no cost — it simply has
    // nothing to extract.
    const ResourceDeposit* extraction_deposit = nullptr;
    std::string extraction_primary_good;
    if (recipe->extracted_resource.has_value()) {
        extraction_deposit =
            find_extractable_deposit(state, biz.province_id, *recipe->extracted_resource);
        if (extraction_deposit == nullptr) {
            return;
        }
        // Primary extracted good = first non-byproduct output (output_1). Only the
        // primary draws the deposit down; byproducts ride along without depleting it.
        for (const auto& out : recipe->outputs) {
            if (!out.is_byproduct) {
                extraction_primary_good = out.good_id;
                break;
            }
        }
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
    // Hard inputs GATE output (bottleneck ratio); yield-modifier inputs (fertilizer for
    // crops, corn feed for livestock) instead BOOST it. A recipe with no fertilizer still
    // yields a subsistence base, scaling up to full yield as the modifier input is applied
    // — this is what lets the food chain bootstrap before the fertilizer/feed industry
    // exists (GDD agriculture yield model; CLAUDE.md: spec wins over the hard-input CSV).
    float bottleneck_ratio = 1.0f;  // from hard (gating) inputs only
    float modifier_avail = 1.0f;    // min availability across yield-modifier inputs (in [0,1])
    bool has_modifier_input = false;

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
        if (input->yield_modifier) {
            has_modifier_input = true;
            modifier_avail = std::min(modifier_avail, std::max(0.0f, std::min(1.0f, ratio)));
        } else {
            bottleneck_ratio = std::min(bottleneck_ratio, ratio);
        }
    }

    // Clamp bottleneck to [0, 1].
    bottleneck_ratio = std::max(0.0f, std::min(1.0f, bottleneck_ratio));

    // Yield-modifier factor: subsistence floor with no modifier input, rising to 1.0 as
    // the modifier is fully applied. 1.0 (no effect) when the recipe has no modifier input.
    float yield_modifier_factor =
        has_modifier_input
            ? cfg_.yield_modifier_floor + (1.0f - cfg_.yield_modifier_floor) * modifier_avail
            : 1.0f;

    // Consume inputs. Hard inputs are drawn at the bottleneck ratio; yield-modifier inputs
    // only in proportion to how much was actually applied (modifier_avail), so unfertilized
    // farming consumes no fertilizer. Record derived demand for each consumed input.
    for (const RecipeInput* input : sorted_inputs) {
        float draw = input->yield_modifier ? bottleneck_ratio * modifier_avail : bottleneck_ratio;
        float consumed = input->quantity_per_tick * draw;
        if (consumed <= 0.0f) {
            continue;
        }

        // Reduce available supply.
        auto it = available_supply.find(input->good_id);
        if (it != available_supply.end()) {
            it->second = std::max(0.0f, it->second - consumed);
        }

        // Write demand_buffer_delta for derived demand. Skip if the good is
        // unknown to the catalog (lookup returns 0) so we don't corrupt
        // whichever market happens to hold id 0.
        const uint32_t input_gid = lookup_good_id(state, input->good_id);
        if (input_gid != 0) {
            MarketDelta demand_delta{};
            demand_delta.good_id = input_gid;
            demand_delta.region_id = biz.province_id;
            demand_delta.demand_buffer_delta = consumed;
            // Conservation: the consumed input matter physically LEAVES the located
            // stock — it is transformed into output below, not merely "demanded".
            // Without this the stock would gain outputs while never losing inputs,
            // creating matter each tick. (Interface spec §Postconditions: "input
            // supply decreases and output supply increases".) The demand_buffer_delta
            // above is the price signal; this supply_delta is the physical draw-down.
            demand_delta.supply_delta = -consumed;
            delta.market_deltas.push_back(demand_delta);
        }
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
    // Per spec (INTERFACE.md): quality_ceiling is "further capped by
    // maturation_of(recipe.key_technology_node)". maturation_of() returns 0 when the
    // node isn't held, so an actor lacking the required technology produces
    // zero-quality (worthless) output — it cannot fake advanced quality it hasn't
    // developed. Commodity recipes (empty key_technology_node) are unaffected.
    if (!recipe->key_technology_node.empty()) {
        float maturation = biz.actor_tech_state.maturation_of(recipe->key_technology_node);
        quality_ceiling = std::min(quality_ceiling, maturation);
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

    // Extraction output is scaled by deposit grade: higher-quality ore/reserves
    // yield more usable output per unit of effort.
    float extraction_quality_scale = 1.0f;
    if (extraction_deposit != nullptr) {
        extraction_quality_scale =
            0.5f + 0.5f * std::max(0.0f, std::min(1.0f, extraction_deposit->quality));
    }
    float deposit_extracted = 0.0f;  // primary resource drawn from the deposit this tick

    // Waste accumulated this tick by waste good (industrial_waste / hazardous_waste).
    float hazardous_waste = 0.0f;
    float industrial_waste = 0.0f;

    for (const RecipeOutput* output : sorted_outputs) {
        float actual_output = output->quantity_per_tick * output_multiplier * bottleneck_ratio *
                              yield_modifier_factor * worker_multiplier *
                              facility.output_rate_modifier * power_ratio;

        // Clamp NaN or negative to 0.
        if (std::isnan(actual_output) || actual_output < 0.0f) {
            actual_output = 0.0f;
        }

        // Extraction: scale every output by deposit grade, and cap the primary
        // extracted good by what physically remains in the deposit (a nearly
        // exhausted deposit yields only its remainder before going to zero).
        if (extraction_deposit != nullptr) {
            actual_output *= extraction_quality_scale;
            if (output->good_id == extraction_primary_good) {
                float cap =
                    std::max(0.0f, extraction_deposit->quantity_remaining - deposit_extracted);
                actual_output = std::min(actual_output, cap);
                deposit_extracted += actual_output;
            }
        }

        if (actual_output <= 0.0f) {
            continue;
        }

        // Skip outputs whose good_id is unknown to the catalog. Treating an
        // unknown gid == 0 as a legitimate output would (a) corrupt the
        // gid == 0 market's supply with phantom volume and (b) book revenue
        // against whatever the gid == 0 market happens to be priced at —
        // both nonsense for a misconfigured recipe. Better to produce
        // nothing (no supply, no revenue) and surface the recipe defect
        // via zero output than to silently mis-attribute economic
        // activity.
        const uint32_t output_gid = lookup_good_id(state, output->good_id);
        if (output_gid == 0) {
            continue;
        }

        MarketDelta supply_delta{};
        supply_delta.good_id = output_gid;
        supply_delta.region_id = biz.province_id;
        supply_delta.supply_delta = actual_output;
        delta.market_deltas.push_back(supply_delta);

        // Calculate revenue using appropriate price.
        float price = get_price_for_business(biz, output_gid, state);
        total_revenue += actual_output * price;

        // Waste: a category-typed share of this output's throughput leaves the
        // process as waste (refining/chemicals → hazardous; mining/heavy industry →
        // industrial tailings; etc.). Requires the goods catalog for the category.
        if (state.goods_catalog) {
            const auto* def = state.goods_catalog->find(output->good_id);
            if (def != nullptr) {
                const WasteSpec ws = waste_for_category(def->category, cfg_);
                if (ws.good != nullptr) {
                    const float w = actual_output * ws.rate;
                    if (std::strcmp(ws.good, "hazardous_waste") == 0)
                        hazardous_waste += w;
                    else
                        industrial_waste += w;
                }
            }
        }
    }

    // Emit accumulated waste into the province as supply that must be handled
    // (it disperses via the standard surplus decay until processed).
    auto emit_waste = [&](const char* good, float qty) {
        if (qty <= 0.0f)
            return;
        const uint32_t gid = lookup_good_id(state, good);
        if (gid == 0)
            return;
        MarketDelta md{};
        md.good_id = gid;
        md.region_id = biz.province_id;
        md.supply_delta = qty;
        delta.market_deltas.push_back(md);
    };
    emit_waste("hazardous_waste", hazardous_waste);
    emit_waste("industrial_waste", industrial_waste);

    // Deplete the deposit by the primary resource extracted this tick. This is
    // what makes located resources finite: a worked deposit runs down and
    // eventually exhausts, ending extraction there.
    if (extraction_deposit != nullptr && deposit_extracted > 0.0f) {
        DepositDelta dd{};
        dd.province_id = biz.province_id;
        dd.deposit_id = extraction_deposit->id;
        dd.quantity_extracted = deposit_extracted;
        delta.deposit_deltas.push_back(dd);
    }

    // Operating cost scales with bottleneck_ratio: zero production = zero variable cost.
    float actual_cost = recipe->base_cost_per_tick * cost_multiplier * bottleneck_ratio;
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
    // Find the regional market for this good in this province via the
    // (good_id, province_id) composite-key index (with linear-scan fallback
    // for unit tests that build state piecemeal).
    if (const RegionalMarket* market = lookup_market(state, good_id, biz.province_id)) {
        if (biz.criminal_sector) {
            // Criminal sector uses informal price.
            // In V1, informal price is approximated as a discount on
            // spot_price. The full informal market model is expansion scope.
            return market->spot_price * cfg_.informal_price_discount;
        }
        return market->spot_price;
    }
    // No market found; return 0 to avoid uninitialized access.
    return 0.0f;
}

}  // namespace econlife
