// Real Estate Module — implementation.
// See real_estate_module.h for class declarations and
// docs/interfaces/real_estate/INTERFACE.md for the canonical specification.
//
// Per-tick processing per province:
//   1. Collect rental income for all rented properties (every tick).
//   2. On monthly ticks (current_tick % 30 == 0):
//      a. Recompute market_value from provincial conditions.
//      b. Derive rental_income_per_tick = market_value * rental_yield_rate.
//      c. Converge asking_price toward market_value.
//      d. Compute avg_property_value for province.
//   3. Assign commercial tenants to unoccupied commercial properties.

#include "modules/real_estate/real_estate_module.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/rng/deterministic_rng.h"
#include "core/world_state/apply_deltas.h"  // lookup_npc_by_id
#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"  // PlayerCharacter complete type
#include "core/world_state/world_state.h"

namespace econlife {

// ===========================================================================
// Property management
// ===========================================================================

void RealEstateModule::add_property(PropertyListing listing) {
    properties_.push_back(listing);
    // Maintain sorted order by id ascending for deterministic processing.
    std::sort(properties_.begin(), properties_.end(),
              [](const PropertyListing& a, const PropertyListing& b) { return a.id < b.id; });
}

const std::vector<PropertyListing>& RealEstateModule::properties() const {
    return properties_;
}

std::vector<PropertyListing>& RealEstateModule::properties() {
    return properties_;
}

// ===========================================================================
// Utilities
// ===========================================================================

float RealEstateModule::compute_market_value(const PropertyListing& prop,
                                             const Province& province) const {
    // Start with current market_value as baseline.
    float base_value = prop.market_value;
    if (base_value <= 0.0f) {
        base_value = 1.0f;  // prevent zero/negative base
    }

    // Criminal dominance penalty: reduces value by penalty_rate * dominance_index.
    // criminal_dominance_index is on RegionConditions.
    float dominance = province.cohort_stats->criminal_dominance_index;
    float dominance_penalty = dominance * cfg_.criminal_dominance_penalty;

    // Laundering premium: inflates value for launder-eligible properties.
    float launder_bonus = 0.0f;
    if (prop.launder_eligible) {
        launder_bonus = cfg_.laundering_premium;
    }

    // Apply modifiers: base_value * (1.0 - dominance_penalty + launder_bonus)
    // Clamp the multiplier to [0.1, 5.0] to prevent negative or extreme values.
    float multiplier = 1.0f - dominance_penalty + launder_bonus;
    multiplier = std::max(0.1f, std::min(5.0f, multiplier));

    return base_value * multiplier;
}

float RealEstateModule::compute_rental_income(float market_value, float rental_yield_rate) const {
    return market_value * rental_yield_rate;
}

void RealEstateModule::converge_asking_price(PropertyListing& prop, float rate) const {
    float gap = prop.market_value - prop.asking_price;
    prop.asking_price += gap * rate;

    // Ensure non-negative.
    if (prop.asking_price < 0.0f) {
        prop.asking_price = 0.0f;
    }
}

float RealEstateModule::compute_avg_property_value(const std::vector<PropertyListing>& props,
                                                   uint32_t province_id) const {
    float sum = 0.0f;
    uint32_t count = 0;

    // Process in id ascending order (vector is maintained sorted).
    for (const auto& prop : props) {
        if (prop.province_id == province_id) {
            sum += prop.market_value;
            ++count;
        }
    }

    if (count == 0) {
        return 0.0f;
    }

    return sum / static_cast<float>(count);
}

// ===========================================================================
// RealEstateModule — pre-tick initialization (main thread, before dispatch)
// ===========================================================================

void RealEstateModule::init_for_tick(const WorldState& /*state*/) {
    // Build per-province index so execute_province() only touches properties
    // belonging to its own province — no cross-province data races.
    province_property_indices_.clear();

    for (size_t i = 0; i < properties_.size(); ++i) {
        province_property_indices_[properties_[i].province_id].push_back(i);
    }
    // properties_ is already sorted by id ascending (maintained by add_property),
    // so the per-province index vectors inherit that order — deterministic.
}

// ===========================================================================
// RealEstateModule — per-province tick execution
// ===========================================================================

void RealEstateModule::execute_province(uint32_t province_idx, const WorldState& state,
                                        DeltaBuffer& province_delta) {
    // Build per-province index if not pre-built by init_for_tick()
    // (supports direct test calls without orchestrator).
    if (province_property_indices_.empty() && !properties_.empty()) {
        for (size_t i = 0; i < properties_.size(); ++i) {
            province_property_indices_[properties_[i].province_id].push_back(i);
        }
    }

    // Look up the pre-built index for this province. May be absent if no
    // properties exist in this province; Step 5 (homeless rate) still runs
    // against an empty `indices` vector so the rent_floor fallback path is
    // exercised whenever there are NPCs to sample.
    auto it = province_property_indices_.find(province_idx);
    static const std::vector<size_t> kEmptyIndices;
    const auto& indices = (it == province_property_indices_.end()) ? kEmptyIndices : it->second;

    const bool is_monthly_tick = (state.current_tick % cfg_.convergence_interval == 0);

    // Locate the province for market value computation.
    const Province* province = nullptr;
    if (province_idx < static_cast<uint32_t>(state.provinces.size())) {
        province = &state.provinces[province_idx];
    }

    // Determine player id (0 if no player).
    uint32_t player_id = 0;
    if (state.player != nullptr) {
        player_id = state.player->id;
    }

    // --- Step 1: Monthly market value recomputation and price convergence ---
    if (is_monthly_tick && province != nullptr) {
        for (size_t idx : indices) {
            auto& prop = properties_[idx];

            // Recompute market_value from provincial conditions.
            prop.market_value = compute_market_value(prop, *province);

            // Derive rental_income_per_tick from market_value (invariant).
            prop.rental_income_per_tick =
                compute_rental_income(prop.market_value, prop.rental_yield_rate);

            // Converge asking_price toward market_value.
            converge_asking_price(prop, cfg_.price_convergence_rate);
        }
    }

    // --- Step 2: Collect rental income for rented properties (every tick) ---
    for (size_t idx : indices) {
        const auto& prop = properties_[idx];

        if (!prop.rented) {
            continue;
        }

        float rental = compute_rental_income(prop.market_value, prop.rental_yield_rate);

        if (prop.owner_id == player_id && player_id != 0) {
            // Credit rental income to player.
            if (province_delta.player_delta.wealth_delta.has_value()) {
                province_delta.player_delta.wealth_delta.value() += rental;
            } else {
                province_delta.player_delta.wealth_delta = rental;
            }
        } else if (prop.owner_id != 0) {
            // Credit rental income to NPC owner.
            NPCDelta npc_delta{};
            npc_delta.npc_id = prop.owner_id;
            npc_delta.capital_delta = rental;
            province_delta.npc_deltas.push_back(npc_delta);
        }
    }

    // --- Step 3: Commercial tenant assignment ---
    // Find unoccupied commercial properties in this province and match them
    // to businesses that lack owned premises.
    for (size_t idx : indices) {
        auto& prop = properties_[idx];

        if (prop.type != PropertyType::commercial || prop.rented) {
            continue;
        }

        // Find a business in this province without owned commercial premises.
        for (const auto& biz : state.npc_businesses) {
            if (biz.province_id != province_idx) {
                continue;
            }

            // Check if this business already occupies a property.
            // Only search properties in the same province (our own indices).
            bool already_has_premises = false;
            for (size_t other_idx : indices) {
                const auto& other_prop = properties_[other_idx];
                if (other_prop.type == PropertyType::commercial && other_prop.rented &&
                    other_prop.tenant_id == biz.id) {
                    already_has_premises = true;
                    break;
                }
            }

            if (!already_has_premises) {
                prop.rented = true;
                prop.tenant_id = biz.id;
                break;  // one business per property
            }
        }
    }

    // --- Step 4: Monthly province avg_property_value update ---
    // Route through the province's region_id so apply_region_deltas matches
    // it correctly even if a future world generator decouples region_id
    // from province_idx (V1 worldgen assigns them 1:1).
    if (is_monthly_tick) {
        float avg_value = compute_avg_property_value(properties_, province_idx);
        RegionDelta region_delta{};
        region_delta.region_id = state.provinces[province_idx].region_id;
        region_delta.avg_property_value_update = avg_value;
        province_delta.region_deltas.push_back(region_delta);
    }

    // --- Step 5: Homeless rate sample (cohort_stats.homeless_rate) ---
    // An NPC is "housed" when their capital covers `homeless_rent_buffer_months`
    // of the province's mean residential rent. Provinces without residential
    // listings fall back to the configured rent_floor. Sample is converged
    // toward the stored rate to smooth per-tick sampling noise, matching the
    // pattern used by healthcare (sick_rate) and labor_market (unemployment).
    if (province_idx < state.npc_indices_by_province.size()) {
        float mean_residential_rent = 0.0f;
        uint32_t residential_count = 0;
        for (size_t i : indices) {
            const auto& prop = properties_[i];
            if (prop.type != PropertyType::residential)
                continue;
            mean_residential_rent += prop.rental_income_per_tick;
            ++residential_count;
        }
        if (residential_count > 0) {
            mean_residential_rent /= static_cast<float>(residential_count);
        } else {
            mean_residential_rent = cfg_.homeless_rent_floor;
        }
        const float affordability_threshold =
            mean_residential_rent * cfg_.homeless_rent_buffer_months;

        uint32_t active_count = 0;
        uint32_t unhoused_count = 0;
        for (uint32_t npc_idx : state.npc_indices_by_province[province_idx]) {
            if (npc_idx >= state.significant_npcs.size())
                continue;
            const NPC& npc = state.significant_npcs[npc_idx];
            if (npc.status != NPCStatus::active)
                continue;
            ++active_count;
            if (npc.capital < affordability_threshold) {
                ++unhoused_count;
            }
        }
        if (active_count > 0) {
            const float sample_homeless_fraction =
                static_cast<float>(unhoused_count) / static_cast<float>(active_count);
            const float current_homeless_rate =
                state.provinces[province_idx].cohort_stats
                    ? state.provinces[province_idx].cohort_stats->homeless_rate
                    : 0.0f;
            RegionDelta homeless_delta{};
            homeless_delta.region_id = state.provinces[province_idx].region_id;
            homeless_delta.homeless_rate_delta = cfg_.homeless_rate_convergence *
                                                 (sample_homeless_fraction - current_homeless_rate);
            province_delta.region_deltas.push_back(homeless_delta);
        }
    }
}

// ─── Phase 1 market — drain + NPC opportunistic buy ────────────────────────

namespace {

// Locate a PropertyListing by id within properties_. Returns nullptr if
// not found. O(N) linear scan — N is bounded by world's property count
// (hundreds to low thousands at V1 scale).
PropertyListing* find_property(std::vector<PropertyListing>& props, uint32_t id) {
    for (auto& p : props) {
        if (p.id == id)
            return &p;
    }
    return nullptr;
}

// Emit an EvidenceToken for a property transaction when its dollar
// amount crosses the institutional-visibility reporting threshold.
// Documentary type, public scope; source = seller, target = buyer.
void emit_transaction_evidence(DeltaBuffer& delta, uint32_t seller_id, uint32_t buyer_id,
                               uint32_t province_id, float price, float threshold,
                               uint32_t current_tick) {
    if (price < threshold)
        return;
    EvidenceDelta ev{};
    EvidenceToken token{};
    token.id = 0;  // assigned by evidence module
    token.type = EvidenceType::documentary;
    token.source_npc_id = seller_id;
    token.target_npc_id = buyer_id;
    // Actionability scaled by how far above threshold the transaction
    // is (bigger transactions = louder paper trail), capped at 0.50.
    token.actionability = std::clamp(0.20f + (price / threshold - 1.0f) * 0.10f, 0.20f, 0.50f);
    token.decay_rate = 0.0f;  // public registry records do not decay
    token.created_tick = current_tick;
    token.province_id = province_id;
    token.is_active = true;
    ev.new_token = token;
    delta.evidence_deltas.push_back(ev);
}

}  // namespace

void RealEstateModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Global post-pass for province-parallel modules. Phase 1: drain
    // pending_property_transactions emitted by player_actions, then run
    // the NPC opportunistic-buy scan over player-listed properties.
    // Both touch properties_ and must run single-threaded.

    // ── 1. Drain player-initiated transactions ──
    //
    // Running player wealth tracks deductions across multiple buys in
    // the same tick (prevents over-commit when state.player->wealth
    // hasn't yet been updated by apply_deltas).
    float running_player_wealth =
        (state.player ? state.player->wealth : 0.0f) +
        delta.player_delta.wealth_delta.value_or(0.0f);

    for (const auto& req : state.pending_property_transactions) {
        PropertyListing* prop = find_property(properties_, req.property_id);
        if (!prop)
            continue;  // unknown property id

        switch (req.kind) {
            case PropertyTransactionKind::list: {
                // Ownership + positive-price are the only validations.
                // Negative/zero prices were already filtered by
                // player_actions; double-check defensively.
                if (prop->owner_id != req.actor_id)
                    break;
                if (req.price <= 0.0f)
                    break;
                prop->listed_for_sale = true;
                prop->asking_price = req.price;
                break;
            }
            case PropertyTransactionKind::unlist: {
                if (prop->owner_id != req.actor_id)
                    break;
                prop->listed_for_sale = false;
                break;
            }
            case PropertyTransactionKind::buy: {
                // Phase 1: at-or-above asking only; below-asking
                // offers are dropped (Phase 3 wires negotiation).
                if (!prop->listed_for_sale)
                    break;
                if (req.price < prop->asking_price)
                    break;
                // Buyer cannot buy from themselves (no self-sale).
                if (prop->owner_id == req.actor_id)
                    break;
                // Running-wealth check (handles multi-buy in one tick).
                if (running_player_wealth < req.price)
                    break;

                uint32_t seller_id = prop->owner_id;
                uint32_t buyer_id = req.actor_id;

                // Money flow: buyer (player) loses cash; seller (NPC or
                // player) gains it. If seller is the player (self-sale
                // is blocked above, so this only fires for hypothetical
                // future NPC-emitted buys), credit appropriately.
                delta.player_delta.wealth_delta =
                    delta.player_delta.wealth_delta.value_or(0.0f) - req.price;
                running_player_wealth -= req.price;

                if (seller_id != 0u && seller_id != (state.player ? state.player->id : 0u)) {
                    NPCDelta seller_delta{};
                    seller_delta.npc_id = seller_id;
                    seller_delta.capital_delta = req.price;
                    delta.npc_deltas.push_back(seller_delta);
                }

                emit_transaction_evidence(delta, seller_id, buyer_id, prop->province_id,
                                          req.price, cfg_.transaction_evidence_threshold,
                                          state.current_tick);

                // Ownership transfer + close out the listing.
                prop->owner_id = buyer_id;
                prop->listed_for_sale = false;
                prop->purchased_tick = state.current_tick;
                prop->purchase_price = req.price;
                // Tenancy survives owner change; rent stream redirects
                // to new owner from this tick onward.
                break;
            }
        }
    }

    // ── 2. Clear queue via const_cast ──
    //
    // Same carve-out player_actions uses on deferred_work_queue.
    auto& mutable_queue =
        const_cast<std::vector<PropertyTransactionRequest>&>(state.pending_property_transactions);
    mutable_queue.clear();

    // ── 3. NPC opportunistic buy scan ──
    //
    // For each property listed by the player at a meaningful discount,
    // find the first NPC in the same province (id ascending) with
    // sufficient capital who passes a deal-strength roll.
    if (!state.player)
        return;
    const uint32_t player_id = state.player->id;
    DeterministicRNG rng(state.world_seed ^ (static_cast<uint64_t>(state.current_tick) * 0xB7E1u));

    // Track per-NPC running capital so multi-buy in one tick respects
    // the NPC's actual cash position (analogous to running_player_wealth).
    std::unordered_map<uint32_t, float> running_npc_capital;
    auto npc_cash = [&](const NPC& n) -> float& {
        auto it = running_npc_capital.find(n.id);
        if (it != running_npc_capital.end())
            return it->second;
        return running_npc_capital.emplace(n.id, n.capital).first->second;
    };

    // Iterate properties in id-ascending order (already maintained).
    for (auto& prop : properties_) {
        if (prop.owner_id != player_id)
            continue;
        if (!prop.listed_for_sale)
            continue;
        if (prop.market_value <= 0.0f)
            continue;
        float deal_ratio = prop.asking_price / prop.market_value;
        if (deal_ratio >= cfg_.npc_buyer_deal_max_ratio)
            continue;  // not a deal — no opportunistic buyers
        float deal_strength = std::clamp(1.0f - deal_ratio, 0.0f, 1.0f);
        float p_buy_per_npc = std::clamp(deal_strength * cfg_.npc_opportunistic_buy_rate,
                                         0.0f, 1.0f);

        // Iterate NPCs in this province in id ascending order. First
        // successful roll wins. RNG is forked deterministically per
        // (tick, property_id) so the buyer is reproducible.
        DeterministicRNG prop_rng(rng.next_uint(0xFFFFFFFFu) ^
                                  (static_cast<uint64_t>(prop.id) * 0x9E37u));

        // Build a sorted view of NPCs in this province.
        std::vector<const NPC*> candidates;
        for (const auto& n : state.significant_npcs) {
            if (n.home_province_id != prop.province_id)
                continue;
            if (n.id == player_id)
                continue;
            candidates.push_back(&n);
        }
        std::sort(candidates.begin(), candidates.end(),
                  [](const NPC* a, const NPC* b) { return a->id < b->id; });

        for (const NPC* n : candidates) {
            float& cash = npc_cash(*n);
            if (cash < prop.asking_price)
                continue;
            float roll = prop_rng.next_float();
            if (roll >= p_buy_per_npc)
                continue;

            // Match: NPC buys at asking_price.
            cash -= prop.asking_price;
            // Player wealth credited; deltas accumulated.
            delta.player_delta.wealth_delta =
                delta.player_delta.wealth_delta.value_or(0.0f) + prop.asking_price;
            NPCDelta buyer_delta{};
            buyer_delta.npc_id = n->id;
            buyer_delta.capital_delta = -prop.asking_price;
            delta.npc_deltas.push_back(buyer_delta);

            emit_transaction_evidence(delta, player_id, n->id, prop.province_id,
                                      prop.asking_price, cfg_.transaction_evidence_threshold,
                                      state.current_tick);

            prop.owner_id = n->id;
            prop.listed_for_sale = false;
            prop.purchased_tick = state.current_tick;
            prop.purchase_price = prop.asking_price;
            break;  // one buyer per property per tick
        }
    }
}

// ─── Persistence helpers ────────────────────────────────────────────────────
//
// Encodes properties_ as a self-contained byte block. Format (little-endian):
//   u32 schema_tag (1 == this layout)
//   u32 count
//   for each PropertyListing:
//     u32 id, u8 type, u32 province_id, u32 owner_id
//     f32 asking_price, f32 market_value, f32 rental_yield_rate, f32 rental_income_per_tick
//     u8 rented, u32 tenant_id, u8 launder_eligible
//     u32 purchased_tick, f32 purchase_price

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

void RealEstateModule::serialize_state(std::vector<uint8_t>& out) const {
    // schema_tag = 2 adds trailing `listed_for_sale` byte per record
    // (Phase 1 market). v1 records have an implicit listed_for_sale = false.
    put_u32(out, 2u);
    put_u32(out, static_cast<uint32_t>(properties_.size()));
    for (const auto& p : properties_) {
        put_u32(out, p.id);
        out.push_back(static_cast<uint8_t>(p.type));
        put_u32(out, p.province_id);
        put_u32(out, p.owner_id);
        put_f32(out, p.asking_price);
        put_f32(out, p.market_value);
        put_f32(out, p.rental_yield_rate);
        put_f32(out, p.rental_income_per_tick);
        out.push_back(p.rented ? 1u : 0u);
        put_u32(out, p.tenant_id);
        out.push_back(p.launder_eligible ? 1u : 0u);
        put_u32(out, p.purchased_tick);
        put_f32(out, p.purchase_price);
        out.push_back(p.listed_for_sale ? 1u : 0u);
    }
}

bool RealEstateModule::deserialize_state(const uint8_t* data, size_t size) {
    Reader r{data, size};
    uint32_t schema_tag = r.u32();
    if (schema_tag != 1u && schema_tag != 2u)
        return false;
    uint32_t count = r.u32();
    properties_.clear();
    properties_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        PropertyListing p{};
        p.id = r.u32();
        p.type = static_cast<PropertyType>(r.u8());
        p.province_id = r.u32();
        p.owner_id = r.u32();
        p.asking_price = r.f32();
        p.market_value = r.f32();
        p.rental_yield_rate = r.f32();
        p.rental_income_per_tick = r.f32();
        p.rented = (r.u8() != 0);
        p.tenant_id = r.u32();
        p.launder_eligible = (r.u8() != 0);
        p.purchased_tick = r.u32();
        p.purchase_price = r.f32();
        if (schema_tag >= 2u)
            p.listed_for_sale = (r.u8() != 0);
        else
            p.listed_for_sale = false;
        if (r.error)
            return false;
        properties_.push_back(p);
    }
    // Restoring properties invalidates the per-province index; clear it so
    // the next init_for_tick (or execute_province's fallback) rebuilds.
    province_property_indices_.clear();
    return !r.error;
}

}  // namespace econlife
