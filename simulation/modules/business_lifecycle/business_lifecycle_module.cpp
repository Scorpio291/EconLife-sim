// business_lifecycle module — implementation.
// See business_lifecycle_module.h for class declarations and
// docs/interfaces/business_lifecycle/INTERFACE.md for the canonical specification.

#include "modules/business_lifecycle/business_lifecycle_module.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "core/rng/deterministic_rng.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"
#include "modules/economy/economy_types.h"

namespace econlife {

// ===========================================================================
// BusinessLifecycleModule — main tick execution
// ===========================================================================

void BusinessLifecycleModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Era-transition effects: apply_deltas sets era_started_tick = current_tick
    // when the era transitions. We fire effects one tick later so the new era is
    // visible in WorldState regardless of orchestration step grouping.
    if (state.current_tick != 0 &&
        state.technology.era_started_tick + 1 == state.current_tick) {
        uint8_t new_era = static_cast<uint8_t>(state.technology.current_era);
        apply_stranded_asset_penalties(state, delta, new_era);
        spawn_era_entrants(state, delta, new_era);
    }

    // Continuous firm genesis runs on its own cadence, every tick that cadence
    // is due — this is what lets a founding-seed world (zero firms) grow an
    // economy, and it self-limits once a province reaches its firm target.
    genesis_from_opportunity(state, delta);
}

// ===========================================================================
// Stranded-asset penalties
// ===========================================================================

void BusinessLifecycleModule::apply_stranded_asset_penalties(const WorldState& state,
                                                             DeltaBuffer& delta,
                                                             uint8_t new_era) const {
    auto it = cfg_.stranded_sectors.find(new_era);
    if (it == cfg_.stranded_sectors.end())
        return;

    // Process businesses in id-ascending order for deterministic delta accumulation.
    for (const auto& biz : state.npc_businesses) {
        for (const auto& entry : it->second) {
            if (biz.sector != entry.sector)
                continue;

            float new_rev = biz.revenue_per_tick * (1.0f - entry.revenue_penalty);
            // Clamp to floor so a single era shock cannot kill the business outright.
            float floor_rev = biz.revenue_per_tick * cfg_.stranded_revenue_floor;
            new_rev = std::max(new_rev, floor_rev);

            BusinessDelta bd{};
            bd.business_id = biz.id;
            bd.revenue_per_tick_update = new_rev;
            bd.cost_per_tick_update = biz.cost_per_tick * (1.0f + entry.cost_increase);
            delta.business_deltas.push_back(bd);
        }
    }
}

// ===========================================================================
// Era-entrant spawning
// ===========================================================================

void BusinessLifecycleModule::spawn_era_entrants(const WorldState& state, DeltaBuffer& delta,
                                                 uint8_t new_era) const {
    auto it = cfg_.emerging_sectors.find(new_era);
    if (it == cfg_.emerging_sectors.end())
        return;

    // Compute next unique business id (max existing + 1).
    uint32_t next_id = 1000u;
    for (const auto& b : state.npc_businesses) {
        if (b.id >= next_id)
            next_id = b.id + 1u;
    }

    // Fork RNG from world seed + era for full determinism.
    DeterministicRNG rng = DeterministicRNG(state.world_seed).fork(new_era * 10000u);

    for (uint32_t pi = 0; pi < static_cast<uint32_t>(state.provinces.size()); ++pi) {
        // Count current businesses in this province.
        uint32_t province_biz_count = 0;
        for (const auto& b : state.npc_businesses) {
            if (b.province_id == pi)
                ++province_biz_count;
        }

        float province_wealth = 1.0f;
        if (pi < state.provinces.size()) {
            province_wealth = state.provinces[pi].demographics.income_high_fraction * 2.0f +
                              state.provinces[pi].demographics.income_middle_fraction;
        }

        for (const auto& entry : it->second) {
            uint32_t to_spawn = static_cast<uint32_t>(std::max(
                1.0f, std::round(static_cast<float>(province_biz_count) * entry.spawn_fraction)));

            for (uint32_t s = 0; s < to_spawn; ++s) {
                // Find the first province resident that does not already own a business.
                uint32_t owner_id = 0;
                if (pi < state.npc_indices_by_home_province.size()) {
                    for (uint32_t idx : state.npc_indices_by_home_province[pi]) {
                        const NPC& npc = state.significant_npcs[idx];
                        bool already_owns = false;
                        for (const auto& b : state.npc_businesses) {
                            if (b.owner_id == npc.id) {
                                already_owns = true;
                                break;
                            }
                        }
                        if (!already_owns) {
                            owner_id = npc.id;
                            break;
                        }
                    }
                }
                // owner_id == 0 means no free NPC; financial_distribution will
                // log a warning and skip compensation, which is acceptable.

                NPCBusiness biz{};
                biz.id = next_id++;
                biz.owner_id = owner_id;
                biz.province_id = pi;
                biz.sector = entry.sector;
                biz.profile = entry.profile;
                biz.criminal_sector = false;

                // New entrants start lean relative to incumbents.
                biz.cash = (5000.0f + static_cast<float>(rng.next_uint(50000))) * province_wealth;
                biz.revenue_per_tick =
                    (50.0f + static_cast<float>(rng.next_uint(200))) * province_wealth;
                biz.cost_per_tick = biz.revenue_per_tick * (0.7f + rng.next_float() * 0.2f);
                biz.market_share = 0.01f + rng.next_float() * 0.05f;

                // Stagger quarterly decisions to avoid thundering-herd on
                // the first decision tick after spawn.
                biz.strategic_decision_tick = state.current_tick + rng.next_uint(90);
                biz.dispatch_day_offset = static_cast<uint8_t>(biz.id % 30);

                // Era-appropriate tech tier: new entrants launch with current-era
                // technology rather than Era 1 baseline.
                biz.actor_tech_state.effective_tech_tier = static_cast<float>(new_era);

                biz.default_activity_scope = VisibilityScope::institutional;

                NewBusinessDelta nbd{};
                nbd.new_business = std::move(biz);
                delta.new_businesses.push_back(std::move(nbd));
            }
        }
    }
}

// ===========================================================================
// Continuous opportunity-driven firm genesis
// ===========================================================================

namespace {

// Founding economies are subsistence/retail-heavy and resource-grounded:
// fertile provinces lean agricultural, but everywhere needs food, basic retail
// and services before heavy industry, finance or tech (which arrive via growth
// and era entrants, not genesis).
BusinessSector choose_genesis_sector(const Province& prov, DeterministicRNG& rng) {
    float roll = rng.next_float();
    if (roll < prov.agricultural_productivity * 0.6f)
        return BusinessSector::agriculture;
    if (roll < 0.55f)
        return BusinessSector::food_beverage;
    if (roll < 0.80f)
        return BusinessSector::retail;
    return BusinessSector::services;
}

// A firm's earning power is grounded in what it actually DOES with the
// province's endowment — not a generic number. This is the early-age economy:
// sole proprietors (farmers, bakers, smiths, shopkeepers) producing real value
// from their own labour and the local resources at hand, not a modern firm
// dropped into a vacuum. A solo owner-operator on good ground is viable on their
// own; on poor ground they earn little (and would do better in another trade).
//   - Primary production (agriculture) tracks the worked land's productivity.
//   - Processing (food & beverage) follows local farming, sold to the populace.
//   - Consumer-facing trades (retail, services) track the local customer base.
// Returns a multiplier in roughly [0.3, 1.5].
float genesis_sector_endowment(const Province& prov, BusinessSector sector) {
    // Local customer base (demand side) for consumer-facing trades.
    float demand = std::max(0.1f, prov.demographics.income_high_fraction * 2.0f +
                                      prov.demographics.income_middle_fraction);
    switch (sector) {
        case BusinessSector::agriculture:
            // Worked land: fertile provinces sustain productive farms regardless
            // of how poor the populace currently is — the land yields food. This
            // is the exogenous productivity that breaks a subsistence economy out
            // of its poverty trap (cf. the project's resource-endowment grounding).
            return 0.3f + 1.2f * prov.agricultural_productivity;
        case BusinessSector::food_beverage:
            // Processing follows local farming, sold to the local populace.
            return 0.25f + 0.5f * prov.agricultural_productivity + 0.4f * demand;
        case BusinessSector::retail:
        case BusinessSector::services:
        default:
            // Serve the local populace — earning tracks the customer base.
            return 0.3f + 0.6f * demand;
    }
}

}  // namespace

void BusinessLifecycleModule::genesis_from_opportunity(const WorldState& state,
                                                       DeltaBuffer& delta) const {
    if (!cfg_.genesis_enabled || state.current_tick == 0)
        return;
    // Fire the first cohort at the founding moment (tick 1), while founders'
    // savings are still intact — a founding world has no firms, hence no wages,
    // so capital drains quickly; the earliest entrepreneurs must form firms now,
    // and those firms create the wages that sustain later formation. Thereafter
    // evaluate on the monthly cadence.
    if (state.current_tick != 1 && state.current_tick % cfg_.genesis_cadence_ticks != 0)
        return;

    // Regime gate: firms are an anachronism in the pre-market (commons) regimes —
    // the dawn economy is livelihoods, not businesses. Run the flat per-resident
    // genesis only in regimes where firms are realistic (modern and beyond). An
    // empty list keeps the legacy "every regime" behaviour.
    if (!cfg_.genesis_active_regimes.empty()) {
        const EraDefinition* era = state.era_catalog.by_index(state.technology.current_era);
        const std::string regime = era ? era->economic_regime : std::string();
        bool active = false;
        for (const auto& r : cfg_.genesis_active_regimes) {
            if (r == regime) {
                active = true;
                break;
            }
        }
        if (!active)
            return;
    }

    const float denom = std::max(1.0f, cfg_.firms_per_resident_denominator);

    // Owners already committed — existing firms plus any spawned earlier this
    // tick (e.g. era entrants) — so no founder is double-counted and ids stay
    // unique across both spawn paths.
    std::unordered_set<uint32_t> claimed_owners;
    for (const auto& b : state.npc_businesses)
        claimed_owners.insert(b.owner_id);
    for (const auto& nbd : delta.new_businesses)
        claimed_owners.insert(nbd.new_business.owner_id);

    uint32_t next_id = 1000u;
    for (const auto& b : state.npc_businesses)
        if (b.id >= next_id)
            next_id = b.id + 1u;
    for (const auto& nbd : delta.new_businesses)
        if (nbd.new_business.id >= next_id)
            next_id = nbd.new_business.id + 1u;

    uint8_t era = static_cast<uint8_t>(state.technology.current_era);
    DeterministicRNG rng = DeterministicRNG(state.world_seed).fork(0x6E5151u + state.current_tick);

    for (uint32_t pi = 0; pi < static_cast<uint32_t>(state.provinces.size()); ++pi) {
        if (pi >= state.npc_indices_by_home_province.size())
            continue;

        // The resident founder pool sets the supportable firm target, matching
        // the world-gen seeding density (~1 firm per 10 named NPCs).
        uint32_t resident_count =
            static_cast<uint32_t>(state.npc_indices_by_home_province[pi].size());
        if (resident_count == 0)
            continue;

        float target = static_cast<float>(resident_count) / denom;

        // Current legitimate firm count (criminal firms are a separate market,
        // grounded elsewhere, and do not count against legitimate saturation).
        uint32_t current = 0;
        for (const auto& b : state.npc_businesses)
            if (b.province_id == pi && !b.criminal_sector)
                ++current;
        for (const auto& nbd : delta.new_businesses)
            if (nbd.new_business.province_id == pi && !nbd.new_business.criminal_sector)
                ++current;

        float gap = target - static_cast<float>(current);
        if (gap <= target * cfg_.genesis_saturation_deadband)
            continue;  // at (or near) saturation — no unmet opportunity, stay quiet

        // At the founding moment fill the whole gap (limited only by how many
        // residents can afford to found); thereafter top up a fraction per month
        // as accumulated wealth permits.
        float fill = (state.current_tick == 1) ? 1.0f : cfg_.genesis_gap_fill_fraction;
        uint32_t to_spawn = static_cast<uint32_t>(std::max(1.0f, std::round(gap * fill)));

        const Province& prov = state.provinces[pi];

        for (uint32_t s = 0; s < to_spawn; ++s) {
            // Next eligible founder: local resident, owns no firm yet, and holds
            // enough capital to seed one. Deterministic resident-list order.
            uint32_t founder_id = 0;
            float founder_capital = 0.0f;
            for (uint32_t idx : state.npc_indices_by_home_province[pi]) {
                const NPC& npc = state.significant_npcs[idx];
                if (claimed_owners.count(npc.id))
                    continue;
                if (npc.capital < cfg_.founder_min_capital)
                    continue;
                founder_id = npc.id;
                founder_capital = npc.capital;
                break;
            }
            if (founder_id == 0)
                break;  // no capital-holding founder left in this province

            claimed_owners.insert(founder_id);
            float investment = founder_capital * cfg_.founder_investment_fraction;

            NPCBusiness biz{};
            biz.id = next_id++;
            biz.owner_id = founder_id;
            biz.province_id = pi;
            biz.criminal_sector = false;
            biz.sector = choose_genesis_sector(prov, rng);
            biz.profile = BusinessProfile::cost_cutter;  // founders start scrappy

            // Earning power is grounded in what the firm DOES with the province's
            // endowment (worked land, local customer base) — a solo owner-operator
            // produces real value from their own labour and the resources at hand,
            // not from a generic demand number. The firm is seeded only from the
            // founder's own capital (conserved, not minted) and starts cash-poor,
            // building its balance sheet from operating profit. Lower cost ratio
            // than a hand-seeded incumbent: a one-person trade pays itself rather
            // than carrying a payroll, so it can be viable on its own.
            float endowment = genesis_sector_endowment(prov, biz.sector);
            biz.cash = investment;
            biz.revenue_per_tick = (200.0f + static_cast<float>(rng.next_uint(800))) * endowment;
            biz.cost_per_tick = biz.revenue_per_tick * (0.55f + rng.next_float() * 0.25f);
            biz.market_share = 0.01f + rng.next_float() * 0.05f;
            biz.strategic_decision_tick = state.current_tick + rng.next_uint(90);
            biz.dispatch_day_offset = static_cast<uint8_t>(biz.id % 30);
            biz.actor_tech_state.effective_tech_tier = static_cast<float>(era);
            biz.default_activity_scope = VisibilityScope::institutional;

            NewBusinessDelta nbd{};
            nbd.new_business = std::move(biz);
            delta.new_businesses.push_back(std::move(nbd));

            // The founder commits capital into the firm (conserved, not minted).
            NPCDelta nd{};
            nd.npc_id = founder_id;
            nd.capital_delta = -investment;
            delta.npc_deltas.push_back(nd);
        }
    }
}

}  // namespace econlife
