#include "drug_economy_module.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "core/rng/deterministic_rng.h"
#include "core/world_state/apply_deltas.h"  // lookup_good_id
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"

namespace econlife {

namespace {
// Derive the drug type a business produces from its facility's recipe output,
// rather than assuming cannabis. Maps the primary (non-byproduct) output good
// key to a DrugType; falls back to cannabis if no drug-producing facility/recipe
// is found.
DrugType drug_type_for_business(const WorldState& state, const NPCBusiness& biz) {
    for (const auto& fac : state.facilities) {
        if (fac.business_id != biz.id)
            continue;
        const Recipe* recipe = nullptr;
        for (const auto& r : state.loaded_recipes) {
            if (r.id == fac.recipe_id) {
                recipe = &r;
                break;
            }
        }
        if (!recipe)
            continue;
        for (const auto& out : recipe->outputs) {
            if (out.is_byproduct)
                continue;
            const std::string& g = out.good_id;
            if (g.find("methamphetamine") != std::string::npos)
                return DrugType::methamphetamine;
            if (g.find("synthetic_opioid") != std::string::npos)
                return DrugType::synthetic_opioid;
            if (g.find("designer_drug") != std::string::npos)
                return DrugType::designer_drug;
            if (g.find("cannabis") != std::string::npos)
                return DrugType::cannabis;
        }
    }
    return DrugType::cannabis;  // fallback when no drug recipe is found
}
}  // namespace

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

namespace {
// Real precursor good + units consumed per unit of drug output, by recipe.
// Conservation: synthetic drugs are made FROM drug_precursors (themselves
// synthesised from naphtha + acid), drawn from the located market stock — not
// conjured. cocaine/heroin are not produced here (EX-reserved in DrugType); their
// coca_leaf/poppy chain runs conserved through the production module. Cannabis has
// no cultivation chain yet, so it is left unbound (proxy) until one is added.
struct DrugPrecursor {
    const char* good;
    float ratio;
};
DrugPrecursor precursor_for_drug(DrugType d) {
    switch (d) {
        case DrugType::methamphetamine:
            return {"drug_precursors", 2.0f};
        case DrugType::synthetic_opioid:
            return {"drug_precursors", 1.5f};
        case DrugType::designer_drug:
            return {"drug_precursors", 1.0f};
        case DrugType::cannabis:
        default:
            return {nullptr, 0.0f};
    }
}

// The real catalog good a drug type trades as. Previously the module cast the
// DrugType enum value straight to a good_id (0..3), which COLLIDED with the first
// catalog goods (iron_ore=0, copper_ore=1, ...): drug supply/price/demand were
// silently read from and written to the iron-ore/copper/bauxite markets. Keying by
// the real good fixes that corruption.
const char* drug_good_string(DrugType d) {
    switch (d) {
        case DrugType::methamphetamine:
            return "methamphetamine";
        case DrugType::synthetic_opioid:
            return "synthetic_opioid";
        case DrugType::designer_drug:
            return "designer_drug";
        case DrugType::cannabis:
        default:
            return "cannabis_processed";
    }
}
}  // namespace

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
        // Enforcement bite (organizational resilience): if the operator is in
        // prison the enterprise runs at reduced capacity — a deputy keeps it going,
        // organized crime is not killed by decapitation — and recovers on release.
        // Crime is thus suppressed in proportion to how often operators are jailed
        // (enforcement strength, throttled by corruption) — the balancing feedback.
        float op_factor = 1.0f;
        if (biz->owner_id != 0) {
            const NPC* op = lookup_npc_by_id(state, biz->owner_id);
            if (op != nullptr && op->status == NPCStatus::imprisoned)
                op_factor = cfg_.operator_imprisoned_output;
        }

        // Output CAPACITY is still a proxy off business scale (full recipe-driven
        // production is the production module's job); but synthetic drugs are now
        // gated by — and consume — real located drug_precursors. You cannot cook
        // what you have no precursors for.
        const float capacity = biz->revenue_per_tick * 0.1f * op_factor;
        if (capacity <= 0.0f)
            continue;

        // Drug type from the business's facility recipe output (cannabis fallback).
        DrugType drug_type = drug_type_for_business(state, *biz);
        const uint32_t drug_gid = lookup_good_id(state, drug_good_string(drug_type));

        // Bottleneck output on real precursor availability and record the draw-down.
        const DrugPrecursor pre = precursor_for_drug(drug_type);
        float production_output = capacity;
        float precursor_consumed = 0.0f;
        uint32_t precursor_gid = 0;
        if (pre.good != nullptr && pre.ratio > 0.0f) {
            precursor_gid = lookup_good_id(state, pre.good);
            float available = 0.0f;
            if (precursor_gid != 0) {
                const uint64_t pkey = (static_cast<uint64_t>(precursor_gid) << 32) |
                                      static_cast<uint64_t>(province.id);
                auto pit = state.market_index_by_good_province.find(pkey);
                if (pit != state.market_index_by_good_province.end() &&
                    pit->second < state.regional_markets.size()) {
                    available = std::max(0.0f, state.regional_markets[pit->second].supply);
                }
            }
            production_output = std::min(capacity, available / pre.ratio);
            precursor_consumed = production_output * pre.ratio;
        }
        if (production_output <= 0.0f)
            continue;

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

        // Pricing: read the informal-market spot price for this drug good in this
        // province from RegionalMarket (keyed (good_id<<32)|province_id); fall
        // back to a baseline when no market exists for the good yet.
        float spot_price = 100.0f;  // baseline fallback
        {
            const uint64_t key =
                (static_cast<uint64_t>(drug_gid) << 32) | static_cast<uint64_t>(province.id);
            auto it = state.market_index_by_good_province.find(key);
            if (it != state.market_index_by_good_province.end() &&
                it->second < state.regional_markets.size()) {
                float p = state.regional_markets[it->second].spot_price;
                if (p > 0.0f)
                    spot_price = p;
            }
        }
        float revenue = 0.0f;
        if (tier == DrugMarketTier::wholesale) {
            revenue = production_output *
                      compute_wholesale_price(spot_price, cfg_.wholesale_price_fraction);
        } else {
            revenue = production_output * spot_price;
        }

        // Add supply to the informal market, keyed by the REAL drug good id.
        MarketDelta supply_delta;
        supply_delta.good_id = drug_gid;
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

        // Conservation: the precursors physically consumed LEAVE the located stock
        // (supply draw-down), with a matching demand signal for price formation.
        if (precursor_gid != 0 && precursor_consumed > 0.0f) {
            MarketDelta precursor_md;
            precursor_md.good_id = precursor_gid;
            precursor_md.region_id = province.id;
            precursor_md.supply_delta = -precursor_consumed;
            precursor_md.demand_buffer_delta = precursor_consumed;
            province_delta.market_deltas.push_back(precursor_md);
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

        // Aggregate addiction demand, keyed to a real drug good (cannabis_processed,
        // the most-consumed) rather than good_id 0 (which is iron_ore — the old
        // collision). Per-drug-type demand splitting is a later refinement.
        MarketDelta demand_delta;
        demand_delta.good_id = lookup_good_id(state, "cannabis_processed");
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
        const uint64_t rng_seed =
            state.world_seed ^ (static_cast<uint64_t>(state.current_tick) << 16) ^
            static_cast<uint64_t>(province.id) ^ (static_cast<uint64_t>(SEED_RNG_CONTEXT) << 32);
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
