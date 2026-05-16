#include "drug_economy_module.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/rng/deterministic_rng.h"
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
// Pre-parallel initialization
// ============================================================================

void DrugEconomyModule::init_for_tick(const WorldState& state) {
    // Clear per-tick production records. These are rebuilt during execute_province
    // but not appended to the shared vector — each province builds its own local
    // records and the module only keeps them for post-tick inspection.
    production_records_.clear();

    // Ensure legalization status vector covers all provinces.
    if (legalization_status_.size() < state.provinces.size()) {
        legalization_status_.resize(state.provinces.size());
    }
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
            output_quality = degrade_quality(base_quality, cfg_.wholesale_quality_degradation);
        } else {
            output_quality =
                degrade_quality(degrade_quality(base_quality, cfg_.wholesale_quality_degradation),
                                cfg_.retail_quality_degradation);
        }

        // Compute pricing
        // In full impl, this reads from RegionalMarket informal layer
        float spot_price = 100.0f;  // proxy
        float revenue = 0.0f;
        if (tier == DrugMarketTier::wholesale) {
            revenue = production_output *
                      compute_wholesale_price(spot_price, cfg_.wholesale_price_fraction);
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
        biz_revenue.output_quality_update = output_quality;
        province_delta.business_deltas.push_back(biz_revenue);

        // Compute precursor consumption (meth requires 2x precursor)
        if (drug_type == DrugType::methamphetamine) {
            float precursor =
                compute_precursor_consumption(production_output, cfg_.precursor_ratio_meth);
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

        // Production tracking omitted during parallel execution to avoid
        // data races. production_records_ is populated in init_for_tick
        // or inspected via the public accessor after the tick.
    }

    // Consumer demand from addiction rates
    float addiction_rate = province.cohort_stats ? province.cohort_stats->addiction_rate : 0.0f;
    if (addiction_rate > 0.0f && province.cohort_stats) {
        float demand = compute_addiction_demand(
            addiction_rate, province.cohort_stats->total_population, cfg_.demand_per_addict);

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

    // ---------------------------------------------------------------
    // Per-NPC addiction seeding
    // When supply exists in this province (drug businesses producing) or the
    // province already has an addiction rate > 0, roll a small per-tick
    // probability for active NPCs whose AddictionState.stage == none to enter
    // the addiction pipeline at stage=casual. AddictionModule advances stage
    // on subsequent ticks; the documented 1-tick lag is acceptable since
    // drug_economy `runs_before: ["addiction"]`.
    //
    // Saturation cap: once addiction_rate reaches SEED_SATURATION_CAP we
    // stop seeding new NPCs this tick — keeps growth bounded without further
    // population-level accounting.
    // ---------------------------------------------------------------
    const bool has_supply = !drug_businesses.empty();
    constexpr uint32_t SEED_RNG_CONTEXT = 0xD20EC04;  // "drug economy" namespace
    if ((has_supply || addiction_rate > 0.0f) &&
        addiction_rate < cfg_.addiction_seeding_saturation_cap &&
        province_idx < state.npc_indices_by_province.size()) {
        // Pick a substance key based on which drug type is being produced
        // locally. Default to cannabis: it's the V1 baseline and is the only
        // type emitted by the simplified production loop above. If multiple
        // drug types are produced, future work can weight selection by
        // per-type supply; for V1 cannabis is sufficient.
        const std::string substance_key = "cannabis";

        // Fork the RNG deterministically per (tick, province) so seeding is
        // reproducible. The seed mixes world_seed, current_tick, province.id,
        // and a module-specific context tag.
        const uint64_t rng_seed = state.world_seed ^
                                  (static_cast<uint64_t>(state.current_tick) << 16) ^
                                  static_cast<uint64_t>(province.id) ^
                                  (static_cast<uint64_t>(SEED_RNG_CONTEXT) << 32);
        DeterministicRNG rng(rng_seed);

        // Iterate NPC bucket for this province in npc_id ascending order.
        std::vector<uint32_t> npc_indices(state.npc_indices_by_province[province_idx].begin(),
                                          state.npc_indices_by_province[province_idx].end());
        std::sort(npc_indices.begin(), npc_indices.end(), [&](uint32_t a, uint32_t b) {
            return state.significant_npcs[a].id < state.significant_npcs[b].id;
        });

        // Scale seeding probability with addiction_rate so seeding accelerates
        // once a province is established but stays low at zero.
        const float scaled_probability =
            cfg_.addiction_seeding_probability * (1.0f + addiction_rate * 10.0f);

        for (uint32_t idx : npc_indices) {
            const NPC& npc = state.significant_npcs[idx];
            if (npc.status != NPCStatus::active)
                continue;
            if (npc.addiction_state.stage != AddictionStage::none)
                continue;
            if (rng.next_float() >= scaled_probability)
                continue;

            AddictionState seeded{};
            seeded.stage = AddictionStage::casual;
            seeded.substance_key = substance_key;
            seeded.consecutive_use_ticks = 1;

            NPCDelta seed_delta;
            seed_delta.npc_id = npc.id;
            seed_delta.set_addiction_state = seeded;
            province_delta.npc_deltas.push_back(seed_delta);
        }
    }
}

void DrugEconomyModule::execute(const WorldState& state, DeltaBuffer& delta) {
    production_records_.clear();
    for (uint32_t i = 0; i < state.provinces.size(); ++i) {
        execute_province(i, state, delta);
    }
}

// ─── Persistence helpers (schema v7) ────────────────────────────────────────
//
// Format (little-endian):
//   u32 schema_tag (1)
//   u32 prod_count
//   for each DrugProductionRecord:
//     u32 business_id, u8 drug_type, u8 market_tier
//     f32 output_quantity, f32 output_quality, f32 precursor_consumed
//     u32 province_id
//   u32 legalization_count
//   for each DrugLegalizationStatus (one per province):
//     u8 cannabis_legal, u8 methamphetamine_legal,
//     u8 synthetic_opioid_legal, u8 designer_drug_legal

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

void DrugEconomyModule::serialize_state(std::vector<uint8_t>& out) const {
    put_u32(out, 1u);
    put_u32(out, static_cast<uint32_t>(production_records_.size()));
    for (const auto& p : production_records_) {
        put_u32(out, p.business_id);
        out.push_back(static_cast<uint8_t>(p.drug_type));
        out.push_back(static_cast<uint8_t>(p.market_tier));
        put_f32(out, p.output_quantity);
        put_f32(out, p.output_quality);
        put_f32(out, p.precursor_consumed);
        put_u32(out, p.province_id);
    }
    put_u32(out, static_cast<uint32_t>(legalization_status_.size()));
    for (const auto& s : legalization_status_) {
        out.push_back(s.cannabis_legal ? 1u : 0u);
        out.push_back(s.methamphetamine_legal ? 1u : 0u);
        out.push_back(s.synthetic_opioid_legal ? 1u : 0u);
        out.push_back(s.designer_drug_legal ? 1u : 0u);
    }
}

bool DrugEconomyModule::deserialize_state(const uint8_t* data, size_t size) {
    Reader r{data, size};
    if (r.u32() != 1u)
        return false;
    uint32_t prod_count = r.u32();
    production_records_.clear();
    production_records_.reserve(prod_count);
    for (uint32_t i = 0; i < prod_count; ++i) {
        DrugProductionRecord p{};
        p.business_id = r.u32();
        p.drug_type = static_cast<DrugType>(r.u8());
        p.market_tier = static_cast<DrugMarketTier>(r.u8());
        p.output_quantity = r.f32();
        p.output_quality = r.f32();
        p.precursor_consumed = r.f32();
        p.province_id = r.u32();
        if (r.error)
            return false;
        production_records_.push_back(p);
    }
    uint32_t leg_count = r.u32();
    legalization_status_.clear();
    legalization_status_.reserve(leg_count);
    for (uint32_t i = 0; i < leg_count; ++i) {
        DrugLegalizationStatus s{};
        s.cannabis_legal = (r.u8() != 0);
        s.methamphetamine_legal = (r.u8() != 0);
        s.synthetic_opioid_legal = (r.u8() != 0);
        s.designer_drug_legal = (r.u8() != 0);
        if (r.error)
            return false;
        legalization_status_.push_back(s);
    }
    return !r.error;
}

}  // namespace econlife
