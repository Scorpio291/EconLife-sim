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
#include <string>

#include "core/rng/deterministic_rng.h"
#include "core/world_state/apply_deltas.h"  // lookup_npc_by_id
#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"  // PlayerCharacter complete type
#include "core/world_state/world_state.h"
#include "modules/banking/banking_module.h"        // Loan helpers (static methods)
#include "modules/banking/banking_types.h"         // LoanPurpose
#include "modules/scene_cards/scene_card_types.h"  // SceneCard, SceneSetting, SceneCardType

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

// ─── Phase 1+2 market — drain, settle, NPC opportunistic buy ───────────────

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

// Phase 8 — next free PropertyListing id (max existing + 1).
uint32_t next_property_id(const std::vector<PropertyListing>& props) {
    uint32_t max_id = 0;
    for (const auto& p : props) {
        if (p.id > max_id)
            max_id = p.id;
    }
    return max_id + 1;
}

// Phase 2: returns the active (stage == pending) PendingTransaction for
// a property, or nullptr if none. At most one active tx per property is
// enforced by the drain (buy rejected if already under contract).
PendingTransaction* find_active_pending_tx(std::vector<PendingTransaction>& txs,
                                           uint32_t property_id) {
    for (auto& tx : txs) {
        if (tx.property_id == property_id && tx.stage == PendingTxStage::pending)
            return &tx;
    }
    return nullptr;
}

// Allocate a fresh PendingTransaction id: max-existing + 1. Empty queue
// → 1. Matches the convention used elsewhere (next_business_id,
// next_calendar_id).
uint32_t next_pending_tx_id(const std::vector<PendingTransaction>& txs) {
    uint32_t max_id = 0;
    for (const auto& tx : txs) {
        if (tx.id > max_id)
            max_id = tx.id;
    }
    return max_id + 1;
}

// Phase 6 — auction helpers.
uint32_t next_auction_id(const std::vector<ActiveAuction>& auctions) {
    uint32_t max_id = 0;
    for (const auto& a : auctions) {
        if (a.id > max_id)
            max_id = a.id;
    }
    return max_id + 1;
}

ActiveAuction* find_open_auction(std::vector<ActiveAuction>& auctions, uint32_t auction_id) {
    for (auto& a : auctions) {
        if (a.id == auction_id && a.status == AuctionStatus::open)
            return &a;
    }
    return nullptr;
}

bool has_open_auction_for_asset(const std::vector<ActiveAuction>& auctions, uint32_t asset_id) {
    for (const auto& a : auctions) {
        if (a.asset_id == asset_id && a.status == AuctionStatus::open)
            return true;
    }
    return false;
}

// Phase 7 — a zoning change is "major" when it involves industrial use
// on either side, or develops raw_land (undeveloped) into a built
// class. Minor changes are residential<->commercial shuffles. Major
// changes face the local government's higher bar.
bool is_major_zoning_change(PropertyType current_type, PropertyType current_zoned,
                            PropertyType desired) {
    if (desired == PropertyType::industrial || current_zoned == PropertyType::industrial)
        return true;
    if (current_type == PropertyType::raw_land && desired != PropertyType::raw_land)
        return true;
    return false;
}

// Phase 8 — subtypes that can be subdivided into individually-ownable
// units.
bool is_subdivisible_subtype(const std::string& subtype_key) {
    return subtype_key == "apartment_block" || subtype_key == "office_tower" ||
           subtype_key == "mixed_use_building" || subtype_key == "warehouse_complex" ||
           subtype_key == "retail_center" || subtype_key == "industrial_park";
}

// Phase 8 — the unit subtype produced when a parent is subdivided.
std::string unit_subtype_for(const std::string& parent_subtype) {
    if (parent_subtype == "apartment_block")
        return "apartment_unit";
    if (parent_subtype == "office_tower")
        return "office_unit";
    if (parent_subtype == "retail_center")
        return "retail_unit";
    if (parent_subtype == "warehouse_complex")
        return "warehouse_unit";
    if (parent_subtype == "industrial_park")
        return "industrial_unit";
    if (parent_subtype == "mixed_use_building")
        return "mixed_use_unit";
    return "unit";
}

// Phase 7 — base land value per hectare by subtype_key. Used to seed
// raw_land market_value at generation; authored defaults the designer
// can retune. Unknown keys fall back to a neutral baseline.
float land_base_value_per_hectare(const std::string& subtype_key) {
    if (subtype_key == "farmland")
        return 8000.0f;
    if (subtype_key == "urban_residential")
        return 250000.0f;
    if (subtype_key == "urban_commercial")
        return 400000.0f;
    if (subtype_key == "urban_industrial")
        return 180000.0f;
    if (subtype_key == "coastal")
        return 120000.0f;
    if (subtype_key == "wilderness")
        return 2000.0f;
    if (subtype_key == "island")
        return 50000.0f;
    return 10000.0f;  // neutral fallback
}

// Phase 2: per-PropertyType close delay (config-driven). raw_land uses
// the commercial cadence (land conveyances are slower than residential).
uint32_t close_delay_for_type(const RealEstateConfig& cfg, PropertyType type) {
    switch (type) {
        case PropertyType::residential:
            return cfg.close_delay_residential;
        case PropertyType::commercial:
            return cfg.close_delay_commercial;
        case PropertyType::industrial:
            return cfg.close_delay_industrial;
        case PropertyType::raw_land:
            return cfg.close_delay_commercial;
    }
    return cfg.close_delay_residential;  // default for new types in later phases
}

// Phase 3 helpers — read trust/fear from an NPC's relationships toward a
// specific target. Returns 0.0 (neutral) when no relationship exists.
float find_trust_toward(const NPC& src, uint32_t target_id) {
    for (const auto& rel : src.relationships) {
        if (rel.target_npc_id == target_id)
            return rel.trust;
    }
    return 0.0f;
}

float find_fear_toward(const NPC& src, uint32_t target_id) {
    for (const auto& rel : src.relationships) {
        if (rel.target_npc_id == target_id)
            return rel.fear;
    }
    return 0.0f;
}

float logistic(float x) {
    return 1.0f / (1.0f + std::exp(-x));
}

// Phase 3 — NPC seller's accept probability for a below-asking buyer offer.
// See RealEstateConfig comments for the formula.
float npc_accept_probability(const NPC& seller, uint32_t buyer_id, float offer_price,
                             float market_value, const RealEstateConfig& cfg) {
    if (market_value <= 0.0f)
        return 0.0f;
    float price_ratio = offer_price / market_value;
    float trust = find_trust_toward(seller, buyer_id);
    float fear = find_fear_toward(seller, buyer_id);
    float pressure = 0.0f;
    if (cfg.distress_capital_threshold > 0.0f) {
        pressure = std::clamp(1.0f - seller.capital / cfg.distress_capital_threshold, 0.0f, 1.0f);
    }
    float score = price_ratio + cfg.trust_accept_weight * trust +
                  cfg.fear_accept_weight * fear + cfg.capital_pressure_weight * pressure;
    return logistic((score - 1.0f) * cfg.sigmoid_steepness);
}

// Phase 3 — find the active negotiation for a given scene_card_id, if any.
NegotiationContext* find_negotiation_by_card(std::vector<NegotiationContext>& negs,
                                             uint32_t scene_card_id) {
    for (auto& n : negs) {
        if (n.scene_card_id == scene_card_id)
            return &n;
    }
    return nullptr;
}

// Phase 3 — true if any active negotiation references the given property.
bool has_active_negotiation_for_property(const std::vector<NegotiationContext>& negs,
                                         uint32_t property_id) {
    for (const auto& n : negs) {
        if (n.property_id == property_id)
            return true;
    }
    return false;
}

// Phase 3 — locate a SceneCard by id within pending_scene_cards.
const SceneCard* find_scene_card(const WorldState& state, uint32_t scene_card_id) {
    for (const auto& c : state.pending_scene_cards) {
        if (c.id == scene_card_id)
            return &c;
    }
    return nullptr;
}

// Phase 3 — allocate the next SceneCard id from existing pending cards.
uint32_t next_scene_card_id(const WorldState& state, const DeltaBuffer& delta) {
    uint32_t max_id = 0;
    for (const auto& c : state.pending_scene_cards) {
        if (c.id > max_id)
            max_id = c.id;
    }
    for (const auto& c : delta.new_scene_cards) {
        if (c.id > max_id)
            max_id = c.id;
    }
    return max_id + 1;
}

// Phase 3 — choice ids for accept/decline on NPC-offer SceneCards.
constexpr uint32_t CHOICE_ACCEPT_OFFER = 1;
constexpr uint32_t CHOICE_DECLINE_OFFER = 2;

// Phase 4 — minimum down-payment fraction by property type. raw_land
// uses the industrial floor (lenders want more equity on undeveloped
// parcels).
float min_down_payment_for_type(const RealEstateConfig& cfg, PropertyType type) {
    switch (type) {
        case PropertyType::residential:
            return cfg.min_down_payment_residential;
        case PropertyType::commercial:
            return cfg.min_down_payment_commercial;
        case PropertyType::industrial:
            return cfg.min_down_payment_industrial;
        case PropertyType::raw_land:
            return cfg.min_down_payment_industrial;
    }
    return cfg.min_down_payment_residential;
}

// Phase 4 — mortgage approval check for the player. Cash and at-or-
// above-asking buys never call this; mortgage / mixed buys do. Returns
// true iff:
//   * down_payment_fraction >= min_down_payment_for_type
//   * principal <= player.wealth × player_max_loan_multiplier_of_wealth
//   * credit_score >= banking's min for property_purchase (default 0.5
//     for borrowers with no prior loans; we assume the player baseline
//     until banking starts tracking it)
//   * banking's evaluate_loan_application accepts the (score, dti=0)
bool approve_player_mortgage(const PlayerCharacter& player, float offer_price,
                             float down_payment_fraction, PropertyType type,
                             const RealEstateConfig& cfg) {
    if (down_payment_fraction < min_down_payment_for_type(cfg, type))
        return false;
    float principal = offer_price * (1.0f - down_payment_fraction);
    if (principal <= 0.0f)
        return true;  // 100% cash equivalent — no loan needed
    if (player.wealth <= 0.0f)
        return false;  // negative wealth means insolvent; no underwriting
    float max_loan = player.wealth * cfg.player_max_loan_multiplier_of_wealth;
    if (principal > max_loan)
        return false;
    // Use banking's static helper for credit-score / DTI gate. The
    // player's credit score defaults to 0.5 (same as banking's
    // BorrowerCredit initialiser) until they take a loan. dti = 0
    // approximation since the player has no recurring income stream
    // tracked in V1; debt service is bounded by player_max_loan
    // independently.
    constexpr float kPlayerBaselineCreditScore = 0.5f;
    constexpr float kBankingDenialDtiThreshold = 0.40f;  // matches BankingConfig default
    return BankingModule::evaluate_loan_application(
        kPlayerBaselineCreditScore, /*dti=*/0.0f, LoanPurpose::property_purchase,
        kBankingDenialDtiThreshold);
}

// Emit an EvidenceToken for a property transaction when its dollar
// amount crosses the institutional-visibility reporting threshold.
// Documentary type, public scope; source = seller, target = buyer.
void emit_transaction_evidence(DeltaBuffer& delta, uint32_t seller_id, uint32_t buyer_id,
                               uint32_t province_id, float price, float threshold,
                               uint32_t current_tick, uint8_t location_flags = 0u) {
    if (price < threshold)
        return;
    // Phase 9: offshore parcels conceal their ownership transfers — the
    // offshore registry leaves no institutional paper trail (this is the
    // laundering appeal of offshore property). No evidence token emitted.
    if (location_flags & LocationFlag_Offshore)
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
    // Global post-pass for province-parallel modules.
    //   0. Phase 3: resolve active negotiations from player SceneCard
    //      choices (accept → PendingTransaction; decline/expire → drop).
    //   1. Drain pending_property_transactions. List/unlist apply
    //      immediately; buys at-or-above asking create a pending tx;
    //      buys below asking go through the NPC accept-roll (Phase 3);
    //      cancel marks an active pending tx cancelled.
    //   2. Settle PendingTransactions whose close_tick has arrived.
    //   3. Scan player listings for opportunistic NPC buyers; at-asking
    //      deal buys create pending txs; otherwise NPCs may offer below
    //      asking via a SceneCard (Phase 3, tracked in active_negotiations_).
    //   4. Prune terminal-state PendingTransactions.

    // pending_transactions is logically write-side staging from the
    // module's point of view but lives on WorldState. const_cast is the
    // established carve-out (see DeferredWorkQueue, pending_legal_case_seeds).
    auto& pending_txs =
        const_cast<std::vector<PendingTransaction>&>(state.pending_transactions);
    auto& auctions = const_cast<std::vector<ActiveAuction>&>(state.active_auctions);

    const uint32_t player_id = state.player ? state.player->id : 0u;

    // Running player wealth: tracks deductions across multiple actions
    // in one tick (e.g. paying a bail, listing+buying, settling N tx
    // simultaneously). state.player->wealth has not yet been adjusted
    // by this module's deltas.
    float running_player_wealth =
        (state.player ? state.player->wealth : 0.0f) +
        delta.player_delta.wealth_delta.value_or(0.0f);

    // Per-NPC running capital for the same reason.
    std::unordered_map<uint32_t, float> running_npc_capital;
    auto npc_cash = [&](uint32_t npc_id, float starting) -> float& {
        auto it = running_npc_capital.find(npc_id);
        if (it != running_npc_capital.end())
            return it->second;
        return running_npc_capital.emplace(npc_id, starting).first->second;
    };

    // Helper: emit ownership-transfer side effects + EvidenceDelta.
    // Used by both the same-tick settlement path (rare) and the close-
    // tick settlement path. Updates property fields, debits buyer,
    // credits seller, emits evidence if price >= threshold.
    auto settle_transfer = [&](PropertyListing& prop, uint32_t buyer_id, uint32_t seller_id,
                               float price) {
        if (buyer_id == player_id) {
            delta.player_delta.wealth_delta =
                delta.player_delta.wealth_delta.value_or(0.0f) - price;
            running_player_wealth -= price;
        } else {
            NPCDelta buyer_delta{};
            buyer_delta.npc_id = buyer_id;
            buyer_delta.capital_delta = -price;
            delta.npc_deltas.push_back(buyer_delta);
            // Find NPC's starting capital lazily.
            const NPC* n = lookup_npc_by_id(state, buyer_id);
            if (n)
                npc_cash(buyer_id, n->capital) -= price;
        }
        if (seller_id == player_id) {
            delta.player_delta.wealth_delta =
                delta.player_delta.wealth_delta.value_or(0.0f) + price;
            running_player_wealth += price;
        } else if (seller_id != 0u) {
            NPCDelta seller_delta{};
            seller_delta.npc_id = seller_id;
            seller_delta.capital_delta = price;
            delta.npc_deltas.push_back(seller_delta);
            const NPC* n = lookup_npc_by_id(state, seller_id);
            if (n)
                npc_cash(seller_id, n->capital) += price;
        }
        emit_transaction_evidence(delta, seller_id, buyer_id, prop.province_id, price,
                                  cfg_.transaction_evidence_threshold, state.current_tick,
                                  prop.location_flags);
        prop.owner_id = buyer_id;
        prop.listed_for_sale = false;
        prop.purchased_tick = state.current_tick;
        prop.purchase_price = price;
    };

    // ── 0a. Phase 5: drain pending_property_foreclosures ──
    //
    // Banking (Tier 5 previous tick) emitted PropertyForeclosureRequests
    // when secured property_purchase loans defaulted. Apply each:
    //   * Transfer property ownership to lender (lender_id; 0 = bank
    //     sentinel → unowned-state).
    //   * Clear listed_for_sale (Phase 6 auctions will re-list separately).
    //   * Cancel any active PendingTransaction on the property
    //     (under-contract guard does not apply to involuntary seizures).
    //   * Drop any active negotiation on the property.
    if (!state.pending_property_foreclosures.empty()) {
        for (const auto& fc : state.pending_property_foreclosures) {
            PropertyListing* prop = find_property(properties_, fc.property_id);
            if (!prop)
                continue;
            // Only seize if the borrower is still the owner — if the
            // property was sold between default and this drain, the
            // foreclosure is a no-op (the new owner is innocent).
            if (prop->owner_id != fc.borrower_id)
                continue;
            // Cancel any pending tx on this property.
            if (auto* tx = find_active_pending_tx(pending_txs, prop->id)) {
                tx->stage = PendingTxStage::cancelled;
            }
            // Drop active negotiation, if any.
            active_negotiations_.erase(
                std::remove_if(active_negotiations_.begin(), active_negotiations_.end(),
                               [&](const NegotiationContext& n) {
                                   return n.property_id == prop->id;
                               }),
                active_negotiations_.end());
            // Transfer ownership to lender (0 = anonymous bank).
            prop->owner_id = fc.lender_id;
            prop->listed_for_sale = false;
            prop->purchased_tick = state.current_tick;
            prop->purchase_price = 0.0f;  // seizure, not a sale

            // Phase 6: the bank's liquidation policy (lender_id == 0 =
            // anonymous bank) is to auction the seized property. Open an
            // auction with reserve = market_value × reserve_fraction.
            // (Future: private creditors with non-zero lender_id may
            // choose to hold instead.)
            if (fc.lender_id == 0u && prop->market_value > 0.0f &&
                !has_open_auction_for_asset(auctions, prop->id)) {
                ActiveAuction auction{};
                auction.id = next_auction_id(auctions);
                auction.asset_id = prop->id;
                auction.consigner_id = fc.lender_id;
                auction.reserve_price = prop->market_value * cfg_.auction_reserve_fraction;
                auction.opened_tick = state.current_tick;
                auction.closes_tick = state.current_tick + cfg_.auction_duration_ticks;
                auction.status = AuctionStatus::open;
                auction.current_high_bidder_id = 0;
                auction.current_high_bid = 0.0f;
                auctions.push_back(auction);
            }
        }
        auto& mutable_queue = const_cast<std::vector<PropertyForeclosureRequest>&>(
            state.pending_property_foreclosures);
        mutable_queue.clear();
    }

    // ── 0a2. Phase 6: drain auction bid requests (player + NPC) ──
    //
    // Validate each bid: auction exists & open, not yet past close,
    // bid exceeds current high, bidder can afford. Winning bids update
    // the auction's current high; losing/invalid bids are dropped.
    if (!state.pending_auction_bid_requests.empty()) {
        for (const auto& bid : state.pending_auction_bid_requests) {
            ActiveAuction* auction = find_open_auction(auctions, bid.auction_id);
            if (!auction)
                continue;
            if (state.current_tick > auction->closes_tick)
                continue;
            if (bid.bid_amount <= auction->current_high_bid)
                continue;
            if (bid.bid_amount < auction->reserve_price &&
                auction->current_high_bidder_id == 0) {
                // First bid must at least reach reserve to register; sub-
                // reserve opening bids are dropped (keeps the high-bid
                // ladder meaningful).
                continue;
            }
            // Affordability: player tracked via running wealth; NPCs via
            // running capital.
            float bidder_cash;
            if (bid.bidder_id == player_id) {
                bidder_cash = running_player_wealth;
            } else {
                const NPC* n = lookup_npc_by_id(state, bid.bidder_id);
                bidder_cash = n ? npc_cash(bid.bidder_id, n->capital) : 0.0f;
            }
            if (bidder_cash < bid.bid_amount)
                continue;
            auction->current_high_bidder_id = bid.bidder_id;
            auction->current_high_bid = bid.bid_amount;
            auction->bids.push_back(AuctionBid{bid.bidder_id, bid.bid_amount, state.current_tick});
        }
        auto& mutable_bids =
            const_cast<std::vector<AuctionBidRequest>&>(state.pending_auction_bid_requests);
        mutable_bids.clear();
    }

    // ── 0a3. Phase 6: NPC auction-bid scan ──
    //
    // For each open auction, NPCs in the asset's province may raise the
    // bid. Deterministic RNG fork per (tick, auction).
    if (state.player) {
        DeterministicRNG auction_rng(state.world_seed ^
                                     (static_cast<uint64_t>(state.current_tick) * 0x27D4u));
        for (auto& auction : auctions) {
            if (auction.status != AuctionStatus::open)
                continue;
            if (state.current_tick >= auction.closes_tick)
                continue;
            PropertyListing* prop = find_property(properties_, auction.asset_id);
            if (!prop)
                continue;
            float max_bid_value = prop->market_value * cfg_.npc_auction_max_value_ratio;
            // Next bid the NPC would place: raise current high by the
            // increment, or open at the reserve if no bids yet.
            float next_bid = (auction.current_high_bid > 0.0f)
                                 ? auction.current_high_bid *
                                       (1.0f + cfg_.npc_auction_bid_increment)
                                 : auction.reserve_price;
            if (next_bid > max_bid_value)
                continue;  // bidding past sane value — NPCs bow out

            DeterministicRNG a_rng(auction_rng.next_uint(0xFFFFFFFFu) ^
                                   (static_cast<uint64_t>(auction.id) * 0x9E37u));
            std::vector<const NPC*> candidates;
            for (const auto& n : state.significant_npcs) {
                if (n.home_province_id != prop->province_id)
                    continue;
                if (n.id == player_id)
                    continue;
                // Don't bid against yourself if you're already the high bidder.
                if (n.id == auction.current_high_bidder_id)
                    continue;
                candidates.push_back(&n);
            }
            std::sort(candidates.begin(), candidates.end(),
                      [](const NPC* a, const NPC* b) { return a->id < b->id; });
            for (const NPC* n : candidates) {
                float& cash = npc_cash(n->id, n->capital);
                if (cash < next_bid)
                    continue;
                if (a_rng.next_float() >= cfg_.npc_auction_bid_rate)
                    continue;
                auction.current_high_bidder_id = n->id;
                auction.current_high_bid = next_bid;
                auction.bids.push_back(AuctionBid{n->id, next_bid, state.current_tick});
                break;  // one raise per auction per tick
            }
        }
    }

    // ── 0a4. Phase 6: settle auctions whose window has closed ──
    if (!auctions.empty()) {
        for (auto& auction : auctions) {
            if (auction.status != AuctionStatus::open)
                continue;
            if (state.current_tick < auction.closes_tick)
                continue;
            PropertyListing* prop = find_property(properties_, auction.asset_id);
            if (!prop) {
                auction.status = AuctionStatus::cancelled;
                continue;
            }
            // No qualifying bid → withdrawn (consigner keeps asset).
            if (auction.current_high_bidder_id == 0 ||
                auction.current_high_bid < auction.reserve_price) {
                auction.status = AuctionStatus::closed_no_reserve;
                continue;
            }
            // Re-validate winner can still pay.
            uint32_t winner = auction.current_high_bidder_id;
            float price = auction.current_high_bid;
            bool can_pay;
            if (winner == player_id) {
                can_pay = running_player_wealth >= price;
            } else {
                const NPC* n = lookup_npc_by_id(state, winner);
                can_pay = n && npc_cash(winner, n->capital) >= price;
            }
            if (!can_pay) {
                // Winner defaulted at settlement → no sale.
                auction.status = AuctionStatus::closed_no_reserve;
                continue;
            }
            // settle_transfer handles buyer debit / seller credit /
            // evidence / ownership. consigner_id 0 (bank) → no credit
            // recipient (funds recovered by anonymous bank).
            settle_transfer(*prop, winner, auction.consigner_id, price);
            auction.status = AuctionStatus::closed_sold;
        }
        // Prune terminal-state auctions.
        auctions.erase(std::remove_if(auctions.begin(), auctions.end(),
                                      [](const ActiveAuction& a) {
                                          return a.status != AuctionStatus::open;
                                      }),
                       auctions.end());
    }

    // ── 0b. Phase 7: drain zoning-change requests ──
    //
    // Each request is decided immediately by a deterministic local-
    // government roll. Approval probability = base (minor vs major) +
    // province criminal_dominance_index × zoning_corruption_bonus
    // (capture/bribery proxy), clamped to [0, 1]. On approval the
    // property's zoned_use changes and market_value is nudged toward
    // the target-class land baseline.
    if (!state.pending_zoning_requests.empty()) {
        for (const auto& zr : state.pending_zoning_requests) {
            PropertyListing* prop = find_property(properties_, zr.property_id);
            if (!prop)
                continue;
            if (prop->owner_id != zr.actor_id)
                continue;  // only the owner may apply
            PropertyType desired = static_cast<PropertyType>(zr.desired_use);
            if (desired == prop->zoned_use)
                continue;  // no-op

            // Province corruption proxy.
            float corruption = 0.0f;
            for (const auto& prov : state.provinces) {
                if (prov.id == prop->province_id && prov.cohort_stats) {
                    corruption = prov.cohort_stats->criminal_dominance_index;
                    break;
                }
            }
            bool major = is_major_zoning_change(prop->type, prop->zoned_use, desired);
            float base_prob =
                major ? cfg_.zoning_major_approval_prob : cfg_.zoning_minor_approval_prob;
            float p_approve =
                std::clamp(base_prob + corruption * cfg_.zoning_corruption_bonus, 0.0f, 1.0f);

            DeterministicRNG zr_rng(state.world_seed ^
                                    (static_cast<uint64_t>(state.current_tick) * 0x41C6u) ^
                                    (static_cast<uint64_t>(prop->id) * 0x9E37u) ^
                                    (static_cast<uint64_t>(zr.desired_use) * 0x2545u));
            if (zr_rng.next_float() >= p_approve)
                continue;  // application denied

            // Approved: re-zone and nudge market_value toward the
            // target land baseline (uses subtype_key if present, else a
            // class-neutral baseline keyed off the desired use).
            prop->zoned_use = desired;
            float target_value = prop->market_value;
            if (!prop->subtype_key.empty() && prop->parcel_area_hectares > 0.0f) {
                target_value =
                    land_base_value_per_hectare(prop->subtype_key) * prop->parcel_area_hectares;
            }
            prop->market_value +=
                (target_value - prop->market_value) * cfg_.zoning_revaluation_rate;
        }
        auto& mutable_zoning =
            const_cast<std::vector<ZoningChangeRequest>&>(state.pending_zoning_requests);
        mutable_zoning.clear();
    }

    // ── 0c. Phase 8: drain subdivision / merge requests ──
    if (!state.pending_subdivision_requests.empty()) {
        for (const auto& sr : state.pending_subdivision_requests) {
            PropertyListing* parent = find_property(properties_, sr.property_id);
            if (!parent)
                continue;
            if (parent->owner_id != sr.actor_id)
                continue;  // owner-only

            if (sr.kind == SubdivisionKind::subdivide) {
                if (parent->subdivided)
                    continue;  // already split
                if (!is_subdivisible_subtype(parent->subtype_key))
                    continue;  // not a divisible building
                uint32_t n = sr.n_units;
                if (n < cfg_.subdivision_min_units || n > cfg_.subdivision_max_units)
                    continue;
                // Can't subdivide while under contract / in auction.
                if (find_active_pending_tx(pending_txs, parent->id) != nullptr)
                    continue;
                if (has_open_auction_for_asset(auctions, parent->id))
                    continue;

                // Capture parent fields into locals: push_back below may
                // reallocate properties_ and invalidate `parent`.
                const uint32_t parent_id = parent->id;
                const PropertyType parent_type = parent->type;
                const uint32_t parent_province = parent->province_id;
                const uint32_t parent_owner = parent->owner_id;
                const float parent_yield = parent->rental_yield_rate;
                const bool parent_launder = parent->launder_eligible;
                const PropertyType parent_zoned = parent->zoned_use;
                const float per_unit_value =
                    (parent->market_value / static_cast<float>(n)) * cfg_.subdivision_unit_premium;
                const std::string unit_subtype = unit_subtype_for(parent->subtype_key);
                const uint32_t next_id = next_property_id(properties_);
                for (uint32_t u = 0; u < n; ++u) {
                    PropertyListing child{};
                    child.id = next_id + u;
                    child.type = parent_type;
                    child.province_id = parent_province;
                    child.owner_id = parent_owner;
                    child.market_value = per_unit_value;
                    child.asking_price = per_unit_value;
                    child.rental_yield_rate = parent_yield;
                    child.rental_income_per_tick =
                        compute_rental_income(per_unit_value, parent_yield);
                    child.rented = false;
                    child.tenant_id = 0;
                    child.launder_eligible = parent_launder;
                    child.purchased_tick = state.current_tick;
                    child.purchase_price = 0.0f;
                    child.listed_for_sale = false;
                    child.subtype_key = unit_subtype;
                    child.parcel_area_hectares = 0.0f;
                    child.zoned_use = parent_zoned;
                    child.parent_property_id = parent_id;
                    child.unit_count = 1;
                    child.subdivided = false;
                    properties_.push_back(child);
                }
                // Re-fetch parent (push_back may have reallocated).
                parent = find_property(properties_, parent_id);
                parent->subdivided = true;
                parent->unit_count = n;
                parent->listed_for_sale = false;  // shell: not directly buyable
                parent->rented = false;
                parent->tenant_id = 0;
                province_property_indices_.clear();  // new properties invalidate index
            } else {
                // Merge: actor must own the parent and every live child;
                // no child may be under contract / in auction.
                if (!parent->subdivided)
                    continue;
                bool ok = true;
                float summed_value = 0.0f;
                for (const auto& child : properties_) {
                    if (child.parent_property_id != parent->id)
                        continue;
                    if (child.owner_id != sr.actor_id) {
                        ok = false;
                        break;
                    }
                    if (find_active_pending_tx(pending_txs, child.id) != nullptr ||
                        has_open_auction_for_asset(auctions, child.id)) {
                        ok = false;
                        break;
                    }
                    summed_value += child.market_value;
                }
                if (!ok)
                    continue;
                // Remove children; restore parent.
                uint32_t parent_id = parent->id;
                properties_.erase(
                    std::remove_if(properties_.begin(), properties_.end(),
                                   [parent_id](const PropertyListing& p) {
                                       return p.parent_property_id == parent_id;
                                   }),
                    properties_.end());
                parent = find_property(properties_, parent_id);
                parent->subdivided = false;
                parent->unit_count = 1;
                if (summed_value > 0.0f)
                    parent->market_value = summed_value;
                province_property_indices_.clear();
            }
        }
        auto& mutable_sub = const_cast<std::vector<PropertySubdivisionRequest>&>(
            state.pending_subdivision_requests);
        mutable_sub.clear();
    }

    // ── 0. Phase 3: resolve active negotiations ──
    //
    // For each active NegotiationContext, look up the linked SceneCard
    // in pending_scene_cards. If the player has chosen, act on the
    // choice. Otherwise check deadline. Resolved/expired contexts are
    // removed from active_negotiations_.
    {
        std::vector<NegotiationContext> survivors;
        survivors.reserve(active_negotiations_.size());
        for (const auto& neg : active_negotiations_) {
            const SceneCard* card = find_scene_card(state, neg.scene_card_id);
            if (!card) {
                // Card vanished (defensive — no path removes them in V1).
                continue;
            }
            if (card->chosen_choice_id == CHOICE_ACCEPT_OFFER) {
                // Validate the buyer can still afford and there's no
                // active pending tx on the property (e.g. player already
                // sold to someone else in the meantime).
                PropertyListing* prop = find_property(properties_, neg.property_id);
                if (!prop)
                    continue;
                if (find_active_pending_tx(pending_txs, prop->id) != nullptr)
                    continue;  // already under contract; drop
                if (prop->owner_id != neg.seller_id)
                    continue;  // ownership changed mid-negotiation; drop
                const NPC* buyer = lookup_npc_by_id(state, neg.buyer_id);
                if (!buyer)
                    continue;
                if (npc_cash(buyer->id, buyer->capital) < neg.offer_price)
                    continue;  // buyer broke; drop

                // Reserve buyer's cash and create pending tx at the
                // offer_price (the offer was below the asking, so this
                // settles at the negotiated discount).
                npc_cash(buyer->id, buyer->capital) -= neg.offer_price;
                PendingTransaction tx{};
                tx.id = next_pending_tx_id(pending_txs);
                tx.property_id = prop->id;
                tx.buyer_id = neg.buyer_id;
                tx.seller_id = neg.seller_id;
                tx.offer_price = neg.offer_price;
                tx.offered_tick = state.current_tick;
                tx.close_tick = state.current_tick + close_delay_for_type(cfg_, prop->type);
                tx.stage = PendingTxStage::pending;
                pending_txs.push_back(tx);
                continue;  // negotiation consumed
            }
            if (card->chosen_choice_id == CHOICE_DECLINE_OFFER) {
                continue;  // player rejected; drop
            }
            // Not yet chosen — check deadline.
            if (state.current_tick > neg.deadline_tick) {
                continue;  // expired; drop
            }
            survivors.push_back(neg);
        }
        active_negotiations_ = std::move(survivors);
    }

    // ── 1. Drain incoming requests ──
    for (const auto& req : state.pending_property_transactions) {
        PropertyListing* prop = find_property(properties_, req.property_id);
        if (!prop)
            continue;

        switch (req.kind) {
            case PropertyTransactionKind::list: {
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
                if (!prop->listed_for_sale)
                    break;
                if (prop->subdivided)
                    break;  // Phase 8: a subdivided parent is a dormant shell
                if (prop->owner_id == req.actor_id)
                    break;
                // Under-contract guard: at most one active pending_tx
                // per property. Subsequent buys are rejected until the
                // first settles, expires, or is cancelled.
                if (find_active_pending_tx(pending_txs, prop->id) != nullptr)
                    break;

                // Phase 4: derive cash-portion required from payment
                // method. Cash → full price; mortgage → type-minimum
                // down payment (max financing); mixed → caller-supplied
                // down_payment_fraction (clamped to >= type minimum).
                float type_min_dp = min_down_payment_for_type(cfg_, prop->type);
                float dpf;
                if (req.payment_method == PaymentMethod::cash) {
                    dpf = 1.0f;
                } else if (req.payment_method == PaymentMethod::mortgage) {
                    dpf = type_min_dp;
                } else {
                    dpf = std::clamp(req.down_payment_fraction, type_min_dp, 1.0f);
                    // For mixed: if caller asked below type minimum, reject
                    // rather than silently bumping (so the caller knows their
                    // structure is non-conforming).
                    if (req.down_payment_fraction < type_min_dp - 1e-6f)
                        break;
                }
                float cash_required = req.price * dpf;
                if (running_player_wealth < cash_required)
                    break;

                // Phase 4: mortgage / mixed offers must clear underwriting
                // for the player at offer time.
                if (req.payment_method != PaymentMethod::cash) {
                    if (!state.player || req.actor_id != state.player->id)
                        break;  // NPCs don't have player-style mortgages in V1
                    if (!approve_player_mortgage(*state.player, req.price, dpf, prop->type, cfg_))
                        break;  // underwriting denied
                }

                // Phase 3: below-asking offers roll the relationship-
                // driven accept formula against the seller NPC. At-or-
                // above asking continues to auto-accept.
                if (req.price < prop->asking_price) {
                    const NPC* seller = lookup_npc_by_id(state, prop->owner_id);
                    if (!seller)
                        break;  // state-owned or unknown — no negotiation
                    DeterministicRNG roll_rng(state.world_seed ^
                                              (static_cast<uint64_t>(state.current_tick) *
                                               0x9E37u) ^
                                              (static_cast<uint64_t>(prop->id) * 0xB7E1u) ^
                                              (static_cast<uint64_t>(req.actor_id) * 0x5851u));
                    float p_accept = npc_accept_probability(*seller, req.actor_id, req.price,
                                                            prop->market_value, cfg_);
                    if (roll_rng.next_float() >= p_accept)
                        break;  // rejected
                }

                // Reserve buyer's cash portion only (not the full price
                // when mortgaged).
                running_player_wealth -= cash_required;

                PendingTransaction tx{};
                tx.id = next_pending_tx_id(pending_txs);
                tx.property_id = prop->id;
                tx.buyer_id = req.actor_id;
                tx.seller_id = prop->owner_id;
                tx.offer_price = req.price;
                tx.offered_tick = state.current_tick;
                tx.close_tick = state.current_tick + close_delay_for_type(cfg_, prop->type);
                tx.stage = PendingTxStage::pending;
                tx.payment_method = req.payment_method;
                tx.down_payment_fraction = dpf;
                tx.interest_rate = cfg_.mortgage_interest_rate;
                tx.loan_maturity_ticks =
                    (req.payment_method == PaymentMethod::cash) ? 0u : cfg_.mortgage_term_ticks;
                pending_txs.push_back(tx);
                break;
            }
            case PropertyTransactionKind::cancel: {
                PendingTransaction* tx = find_active_pending_tx(pending_txs, req.property_id);
                if (!tx)
                    break;
                // Only buyer or seller may cancel.
                if (req.actor_id != tx->buyer_id && req.actor_id != tx->seller_id)
                    break;
                // Refund the buyer's reservation in running wealth so a
                // subsequent buy in the same tick can use the cash.
                if (tx->buyer_id == player_id)
                    running_player_wealth += tx->offer_price;
                tx->stage = PendingTxStage::cancelled;
                break;
            }
        }
    }

    // Clear the incoming queue.
    auto& mutable_in =
        const_cast<std::vector<PropertyTransactionRequest>&>(state.pending_property_transactions);
    mutable_in.clear();

    // ── 2. Settle PendingTransactions whose close_tick has arrived ──
    //
    // Iterate by index because settle_transfer may push to delta and
    // we need to keep the queue stable until pruning.
    for (auto& tx : pending_txs) {
        if (tx.stage != PendingTxStage::pending)
            continue;
        if (tx.close_tick > state.current_tick)
            continue;
        PropertyListing* prop = find_property(properties_, tx.property_id);
        if (!prop) {
            // Underlying property vanished (defensive — no path in
            // Phase 2 deletes properties). Mark cancelled.
            tx.stage = PendingTxStage::cancelled;
            continue;
        }
        // Phase 4: cash-portion is what the buyer must cover at close.
        // Mortgage/mixed deals are financed by a loan that covers the
        // remainder; the LoanRecord is created via NewLoanRequest.
        float cash_portion = tx.offer_price * tx.down_payment_fraction;
        float loan_principal = tx.offer_price - cash_portion;

        bool buyer_can_pay = false;
        if (tx.buyer_id == player_id) {
            buyer_can_pay = running_player_wealth >= cash_portion;
        } else {
            const NPC* n = lookup_npc_by_id(state, tx.buyer_id);
            if (n)
                buyer_can_pay = npc_cash(tx.buyer_id, n->capital) >= cash_portion;
        }

        if (!buyer_can_pay) {
            tx.stage = PendingTxStage::expired;
            continue;
        }
        // settle_transfer handles the cash debit / seller credit /
        // evidence / ownership transfer for the entire offer_price.
        // For mortgaged deals the buyer-debit must only be the
        // down-payment; we override by passing cash_portion as the
        // buyer-side debit, then separately credit the seller for the
        // full price (loan-funded portion). To keep settle_transfer
        // simple, we inline the split here when financing is involved.
        if (loan_principal <= 0.0f) {
            settle_transfer(*prop, tx.buyer_id, tx.seller_id, tx.offer_price);
        } else {
            // Buyer pays only the cash portion.
            if (tx.buyer_id == player_id) {
                delta.player_delta.wealth_delta =
                    delta.player_delta.wealth_delta.value_or(0.0f) - cash_portion;
                running_player_wealth -= cash_portion;
            }
            // Seller is credited the full offer price (bank funds the gap).
            if (tx.seller_id == player_id) {
                delta.player_delta.wealth_delta =
                    delta.player_delta.wealth_delta.value_or(0.0f) + tx.offer_price;
                running_player_wealth += tx.offer_price;
            } else if (tx.seller_id != 0u) {
                NPCDelta seller_delta{};
                seller_delta.npc_id = tx.seller_id;
                seller_delta.capital_delta = tx.offer_price;
                delta.npc_deltas.push_back(seller_delta);
            }
            emit_transaction_evidence(delta, tx.seller_id, tx.buyer_id, prop->province_id,
                                      tx.offer_price, cfg_.transaction_evidence_threshold,
                                      state.current_tick, prop->location_flags);
            prop->owner_id = tx.buyer_id;
            prop->listed_for_sale = false;
            prop->purchased_tick = state.current_tick;
            prop->purchase_price = tx.offer_price;

            // Emit the new loan request — banking creates the LoanRecord
            // at the start of its execute() in this same tick.
            NewLoanRequest loan_req{};
            loan_req.borrower_id = tx.buyer_id;
            loan_req.lender_id = 0u;  // anonymous bank
            loan_req.purpose = static_cast<uint8_t>(LoanPurpose::property_purchase);
            loan_req.principal = loan_principal;
            loan_req.interest_rate = tx.interest_rate;
            loan_req.repayment_per_tick = BankingModule::compute_repayment_per_tick(
                loan_principal, tx.interest_rate, tx.loan_maturity_ticks);
            loan_req.maturity_tick = state.current_tick + tx.loan_maturity_ticks;
            loan_req.collateral_id = prop->id;
            delta.new_loan_requests.push_back(loan_req);
        }
        tx.stage = PendingTxStage::settled;
    }

    // ── 3. NPC opportunistic-buy / below-asking-offer scan ──
    //
    // Two paths per player listing:
    //   a) Deal listing (asking/market < npc_buyer_deal_max_ratio):
    //      opportunistic at-asking buy (Phase 1/2 behavior — creates a
    //      pending tx directly).
    //   b) Non-deal listing (asking/market >= ratio): NPCs may emit a
    //      below-asking offer (Phase 3) delivered via SceneCard. Player
    //      decides accept/decline; resolution lives in the negotiation
    //      pass at the top of execute().
    //
    // Either path is gated by: property listed_for_sale, no active
    // pending tx, no active negotiation.
    if (state.player) {
        DeterministicRNG rng(state.world_seed ^
                             (static_cast<uint64_t>(state.current_tick) * 0xB7E1u));
        for (auto& prop : properties_) {
            if (prop.owner_id != player_id)
                continue;
            if (!prop.listed_for_sale)
                continue;
            if (prop.market_value <= 0.0f)
                continue;
            if (find_active_pending_tx(pending_txs, prop.id) != nullptr)
                continue;  // under contract
            if (has_active_negotiation_for_property(active_negotiations_, prop.id))
                continue;  // negotiation already in flight

            float deal_ratio = prop.asking_price / prop.market_value;

            DeterministicRNG prop_rng(rng.next_uint(0xFFFFFFFFu) ^
                                      (static_cast<uint64_t>(prop.id) * 0x9E37u));

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

            if (deal_ratio < cfg_.npc_buyer_deal_max_ratio) {
                // Path A: deal listing → at-asking opportunistic buy.
                float deal_strength = std::clamp(1.0f - deal_ratio, 0.0f, 1.0f);
                float p_buy_per_npc =
                    std::clamp(deal_strength * cfg_.npc_opportunistic_buy_rate, 0.0f, 1.0f);

                for (const NPC* n : candidates) {
                    float& cash = npc_cash(n->id, n->capital);
                    if (cash < prop.asking_price)
                        continue;
                    float roll = prop_rng.next_float();
                    if (roll >= p_buy_per_npc)
                        continue;

                    cash -= prop.asking_price;
                    PendingTransaction tx{};
                    tx.id = next_pending_tx_id(pending_txs);
                    tx.property_id = prop.id;
                    tx.buyer_id = n->id;
                    tx.seller_id = prop.owner_id;
                    tx.offer_price = prop.asking_price;
                    tx.offered_tick = state.current_tick;
                    tx.close_tick =
                        state.current_tick + close_delay_for_type(cfg_, prop.type);
                    tx.stage = PendingTxStage::pending;
                    pending_txs.push_back(tx);
                    break;  // one buyer per property per tick
                }
            } else {
                // Path B (Phase 3): below-asking offer via SceneCard.
                // Per-NPC probability is small (base rate), independent
                // of price. Offer price drawn from
                // [market * min_ratio, asking * max_ratio]. NPC must have
                // capital to cover the offer.
                for (const NPC* n : candidates) {
                    float& cash = npc_cash(n->id, n->capital);
                    float min_offer = prop.market_value * cfg_.npc_offer_min_ratio;
                    float max_offer = prop.asking_price * cfg_.npc_offer_max_ratio;
                    if (max_offer <= min_offer)
                        continue;  // pathological pricing — skip
                    if (cash < min_offer)
                        continue;
                    float roll = prop_rng.next_float();
                    if (roll >= cfg_.npc_offer_base_rate)
                        continue;

                    float price_roll = prop_rng.next_float();
                    float offer_price = min_offer + price_roll * (max_offer - min_offer);
                    if (cash < offer_price)
                        continue;  // randomly drawn too high

                    // Emit SceneCard for player decision. We do NOT
                    // reserve the NPC's capital yet — the offer may be
                    // declined or expire. Reservation happens at the
                    // accept moment in the negotiation-resolution pass.
                    SceneCard card{};
                    card.id = next_scene_card_id(state, delta);
                    card.type = SceneCardType::meeting;
                    card.setting = SceneSetting::private_office;
                    card.npc_id = n->id;
                    DialogueLine line{};
                    line.speaker_npc_id = n->id;
                    line.text = "I'd like to make an offer on your property.";
                    line.emotional_tone = 0.4f;
                    card.dialogue.push_back(line);
                    PlayerChoice accept{};
                    accept.id = CHOICE_ACCEPT_OFFER;
                    accept.label = "Accept offer";
                    accept.description = "Sell at the offered price.";
                    accept.consequence_id = 0;
                    PlayerChoice decline{};
                    decline.id = CHOICE_DECLINE_OFFER;
                    decline.label = "Decline";
                    decline.description = "Stay on the market.";
                    decline.consequence_id = 0;
                    card.choices.push_back(accept);
                    card.choices.push_back(decline);
                    card.npc_presentation_state = 0.5f;
                    card.is_authored = false;
                    card.chosen_choice_id = 0;
                    delta.new_scene_cards.push_back(card);

                    NegotiationContext neg{};
                    neg.scene_card_id = card.id;
                    neg.property_id = prop.id;
                    neg.buyer_id = n->id;
                    neg.seller_id = prop.owner_id;
                    neg.offer_price = offer_price;
                    neg.offered_tick = state.current_tick;
                    neg.deadline_tick = state.current_tick + cfg_.negotiation_deadline_ticks;
                    active_negotiations_.push_back(neg);
                    break;  // one offer per property per tick
                }
            }
        }
    }

    // ── 4. Prune terminal-state PendingTransactions ──
    pending_txs.erase(std::remove_if(pending_txs.begin(), pending_txs.end(),
                                     [](const PendingTransaction& tx) {
                                         return tx.stage != PendingTxStage::pending;
                                     }),
                      pending_txs.end());
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

void put_str(std::vector<uint8_t>& out, const std::string& s) {
    put_u32(out, static_cast<uint32_t>(s.size()));
    for (char c : s)
        out.push_back(static_cast<uint8_t>(c));
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
    std::string str() {
        uint32_t len = u32();
        if (!need(len))
            return std::string();
        std::string s(reinterpret_cast<const char*>(data + pos), len);
        pos += len;
        return s;
    }
};

}  // namespace

void RealEstateModule::serialize_state(std::vector<uint8_t>& out) const {
    // schema_tag history:
    //   1 = initial properties_ vector (Phase 0)
    //   2 = adds listed_for_sale byte per property (Phase 1)
    //   3 = adds active_negotiations_ trailing block (Phase 3)
    //   4 = adds subtype_key + parcel_area_hectares + zoned_use per
    //       property (Phase 7 raw land + zoning)
    //   5 = adds parent_property_id + unit_count + subdivided per
    //       property (Phase 8 subdivision)
    //   6 = adds location_flags per property (Phase 9)
    put_u32(out, 6u);
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
        // schema_tag 4: land taxonomy + zoning.
        put_str(out, p.subtype_key);
        put_f32(out, p.parcel_area_hectares);
        out.push_back(static_cast<uint8_t>(p.zoned_use));
        // schema_tag 5: subdivision.
        put_u32(out, p.parent_property_id);
        put_u32(out, p.unit_count);
        out.push_back(p.subdivided ? 1u : 0u);
        // schema_tag 6: location flags.
        out.push_back(p.location_flags);
    }
    // schema_tag 3: trailing active_negotiations_ block.
    put_u32(out, static_cast<uint32_t>(active_negotiations_.size()));
    for (const auto& n : active_negotiations_) {
        put_u32(out, n.scene_card_id);
        put_u32(out, n.property_id);
        put_u32(out, n.buyer_id);
        put_u32(out, n.seller_id);
        put_f32(out, n.offer_price);
        put_u32(out, n.offered_tick);
        put_u32(out, n.deadline_tick);
    }
}

bool RealEstateModule::deserialize_state(const uint8_t* data, size_t size) {
    Reader r{data, size};
    uint32_t schema_tag = r.u32();
    if (schema_tag < 1u || schema_tag > 6u)
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
        // schema_tag 4: land taxonomy + zoning. Pre-v4 records default
        // to no subtype, zero area, and zoned_use == their own type.
        if (schema_tag >= 4u) {
            p.subtype_key = r.str();
            p.parcel_area_hectares = r.f32();
            p.zoned_use = static_cast<PropertyType>(r.u8());
        } else {
            p.subtype_key.clear();
            p.parcel_area_hectares = 0.0f;
            p.zoned_use = p.type;
        }
        // schema_tag 5: subdivision. Pre-v5 records are standalone.
        if (schema_tag >= 5u) {
            p.parent_property_id = r.u32();
            p.unit_count = r.u32();
            p.subdivided = (r.u8() != 0);
        } else {
            p.parent_property_id = 0;
            p.unit_count = 1;
            p.subdivided = false;
        }
        // schema_tag 6: location flags. Pre-v6 records are flag-free.
        if (schema_tag >= 6u)
            p.location_flags = r.u8();
        else
            p.location_flags = 0u;
        if (r.error)
            return false;
        properties_.push_back(p);
    }
    active_negotiations_.clear();
    if (schema_tag >= 3u) {
        uint32_t neg_count = r.u32();
        active_negotiations_.reserve(neg_count);
        for (uint32_t i = 0; i < neg_count; ++i) {
            NegotiationContext n{};
            n.scene_card_id = r.u32();
            n.property_id = r.u32();
            n.buyer_id = r.u32();
            n.seller_id = r.u32();
            n.offer_price = r.f32();
            n.offered_tick = r.u32();
            n.deadline_tick = r.u32();
            active_negotiations_.push_back(n);
        }
    }
    if (r.error)
        return false;
    // Restoring properties invalidates the per-province index; clear it so
    // the next init_for_tick (or execute_province's fallback) rebuilds.
    province_property_indices_.clear();
    return !r.error;
}

}  // namespace econlife
