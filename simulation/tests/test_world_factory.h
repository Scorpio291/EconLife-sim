#pragma once

// Test World Factory — creates valid WorldState instances for scenario and
// determinism tests. Every field is initialized to a consistent, non-zero state
// so that modules have meaningful data to process.

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

#include "core/rng/deterministic_rng.h"
#include "core/tick/thread_pool.h"
#include "core/tick/tick_orchestrator.h"
#include "core/world_state/apply_deltas.h"  // rebuild_npc_indices
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"

namespace econlife {
namespace test {

// ---------------------------------------------------------------------------
// create_test_player — minimal valid PlayerCharacter
// ---------------------------------------------------------------------------
inline PlayerCharacter create_test_player(uint32_t home_province_id) {
    PlayerCharacter player{};
    player.id = 1;
    player.background = Background::MiddleClass;
    player.traits = {Trait::Analytical, Trait::Disciplined, Trait::Cautious};
    player.starting_province_id = home_province_id;
    player.home_province_id = home_province_id;
    player.current_province_id = home_province_id;
    player.travel_status = NPCTravelStatus::resident;
    player.health.current_health = 1.0f;
    player.health.base_lifespan = 75.0f;
    player.health.lifespan_projection = 75.0f;
    player.health.exhaustion_accumulator = 0.0f;
    player.health.degradation_rate = 0.0001f;
    player.age = 30.0f;
    player.reputation = {0.0f, 0.0f, 0.0f, 0.0f};
    player.wealth = 50000.0f;
    player.net_assets = 50000.0f;
    player.network_health = {0.5f, 0.3f, 0.2f, 0.1f};
    player.movement_follower_count = 0;
    player.ironman_eligible = false;
    player.residence_id = 0;
    player.partner_npc_id = 0;
    player.designated_heir_npc_id = 0;
    player.restoration_history.restoration_count = 0;
    return player;
}

// ---------------------------------------------------------------------------
// create_test_province — one province with valid defaults
// ---------------------------------------------------------------------------
inline Province create_test_province(uint32_t id, uint32_t region_id, uint32_t nation_id) {
    Province p{};
    p.id = id;
    p.h3_index = static_cast<H3Index>(0x840000000 + id);
    p.fictional_name = "Province_" + std::to_string(id);
    p.real_world_reference = "test";
    p.region_id = region_id;
    p.nation_id = nation_id;

    // Geography
    p.geography.latitude = 45.0f + static_cast<float>(id) * 5.0f;
    p.geography.longitude = 10.0f + static_cast<float>(id) * 10.0f;
    p.geography.elevation_avg_m = 200.0f;
    p.geography.terrain_roughness = 0.3f;
    p.geography.forest_coverage = 0.4f;
    p.geography.arable_land_fraction = 0.5f;
    p.geography.coastal_length_km = 100.0f;
    p.geography.is_landlocked = false;
    p.geography.port_capacity = 0.5f;
    p.geography.river_access = 0.3f;
    p.geography.area_km2 = 1770.0f;

    // Climate
    p.climate.koppen_zone = KoppenZone::Cfb;
    p.climate.temperature_avg_c = 15.0f;
    p.climate.temperature_min_c = -5.0f;
    p.climate.temperature_max_c = 35.0f;
    p.climate.precipitation_mm = 800.0f;
    p.climate.precipitation_seasonality = 0.3f;
    p.climate.drought_vulnerability = 0.2f;
    p.climate.flood_vulnerability = 0.15f;
    p.climate.wildfire_vulnerability = 0.1f;
    p.climate.climate_stress_current = 0.1f;

    // Demographics
    p.demographics.total_population = 500000;
    p.demographics.median_age = 38.0f;
    p.demographics.education_level = 0.6f;
    p.demographics.income_low_fraction = 0.3f;
    p.demographics.income_middle_fraction = 0.5f;
    p.demographics.income_high_fraction = 0.2f;
    p.demographics.political_lean = 0.0f;

    // Economy
    p.infrastructure_rating = 0.6f;
    p.agricultural_productivity = 0.7f;
    p.energy_cost_baseline = 0.05f;
    p.trade_openness = 0.5f;

    // Simulation
    p.lod_level = SimulationLOD::full;
    p.community = {0.6f, 0.2f, 0.6f, 0.5f, 0};
    p.political = {0, 0.5f, 365, 0.2f};
    p.conditions = {
        0.7f,   // stability_score
        0.3f,   // inequality_index
        0.1f,   // crime_rate
        0.05f,  // addiction_rate
        0.05f,  // criminal_dominance_index
        0.7f,   // formal_employment_rate
        0.8f,   // regulatory_compliance_index
        1.0f,   // drought_modifier (no drought)
        1.0f    // flood_modifier (no flood)
    };

    p.has_karst = false;
    p.historical_trauma_index = 0.1f;
    p.cohort_stats.reset();

    return p;
}

// ---------------------------------------------------------------------------
// create_test_npc — one NPC with valid defaults
// ---------------------------------------------------------------------------
inline NPC create_test_npc(uint32_t id, NPCRole role, uint32_t province_id) {
    NPC npc{};
    npc.id = id;
    npc.role = role;
    npc.risk_tolerance = 0.5f;
    npc.capital = 10000.0f;
    npc.social_capital = 0.3f;
    npc.movement_follower_count = 0;
    npc.home_province_id = province_id;
    npc.current_province_id = province_id;
    npc.travel_status = NPCTravelStatus::resident;
    npc.status = NPCStatus::active;

    // Balanced motivation vector (sums to 1.0)
    npc.motivations.weights = {0.20f, 0.15f, 0.15f, 0.05f, 0.05f, 0.10f, 0.20f, 0.10f};

    return npc;
}

// ---------------------------------------------------------------------------
// create_test_market — one regional market
// ---------------------------------------------------------------------------
inline RegionalMarket create_test_market(uint32_t good_id, uint32_t province_id, float base_price,
                                         float supply, float demand) {
    RegionalMarket m{};
    m.good_id = good_id;
    m.province_id = province_id;
    m.spot_price = base_price;
    m.equilibrium_price = base_price;
    m.adjustment_rate = 0.1f;
    m.supply = supply;
    m.demand_buffer = demand;
    m.import_price_ceiling = 0.0f;
    m.export_price_floor = 0.0f;
    return m;
}

// ---------------------------------------------------------------------------
// create_test_business — one NPC business
// ---------------------------------------------------------------------------
inline NPCBusiness create_test_business(uint32_t id, uint32_t owner_id, uint32_t province_id,
                                        BusinessSector sector) {
    NPCBusiness b{};
    b.id = id;
    b.sector = sector;
    b.profile = BusinessProfile::defensive_incumbent;
    b.cash = 100000.0f;
    b.revenue_per_tick = 500.0f;
    b.cost_per_tick = 400.0f;
    b.market_share = 0.1f;
    b.strategic_decision_tick = 30;
    b.dispatch_day_offset = static_cast<uint8_t>(id % 30);
    b.actor_tech_state.effective_tech_tier = 1.0f;
    b.criminal_sector = (sector == BusinessSector::criminal);
    b.province_id = province_id;
    b.regulatory_violation_severity = 0.0f;
    b.default_activity_scope =
        b.criminal_sector ? VisibilityScope::concealed : VisibilityScope::institutional;
    b.owner_id = owner_id;
    b.deferred_salary_liability = 0.0f;
    b.accounts_payable_float = 0.0f;
    return b;
}

// ---------------------------------------------------------------------------
// create_test_world — full valid WorldState for testing
// ---------------------------------------------------------------------------
// Parameters:
//   seed           — deterministic RNG seed
//   npc_count      — number of significant NPCs to create (distributed across provinces)
//   province_count — number of provinces (1-6)
//   goods_count    — number of goods per province market (default 10)
// ---------------------------------------------------------------------------
inline WorldState create_test_world(uint64_t seed, uint32_t npc_count = 100,
                                    uint32_t province_count = 3, uint32_t goods_count = 10) {
    WorldState world{};
    world.current_tick = 0;
    world.world_seed = seed;
    world.ticks_this_session = 0;
    world.game_mode = GameMode::standard;
    world.current_schema_version = 1;
    world.network_health_dirty = false;
    world.lod2_price_index.reset();

    // --- Nation ---
    Nation nation{};
    nation.id = 0;
    nation.name = "TestNation";
    nation.currency_code = "TST";
    nation.government_type = GovernmentType::Democracy;
    nation.political_cycle = {0, 0.5f, false, 365};
    nation.corporate_tax_rate = 0.20f;
    nation.income_tax_rate_top_bracket = 0.35f;
    nation.tariff_schedule = nullptr;
    nation.lod1_profile = std::nullopt;  // LOD 0 (player's home)

    // --- Regions (1 region per province for simplicity) ---
    for (uint32_t r = 0; r < province_count; ++r) {
        Region region{};
        region.id = r;
        region.fictional_name = "Region_" + std::to_string(r);
        region.nation_id = 0;
        region.province_ids.push_back(r);
        world.region_groups.push_back(region);
    }

    // --- Provinces ---
    for (uint32_t p = 0; p < province_count; ++p) {
        Province prov = create_test_province(p, p, 0);
        nation.province_ids.push_back(p);
        world.provinces.push_back(std::move(prov));
    }

    // Add province links (linear chain)
    for (uint32_t p = 0; p < province_count; ++p) {
        if (p > 0) {
            ProvinceLink link{};
            link.neighbor_h3 = world.provinces[p - 1].h3_index;
            link.type = LinkType::Land;
            link.shared_border_km = 50.0f;
            link.transit_terrain_cost = 0.3f;
            link.infrastructure_bonus = 0.5f;
            world.provinces[p].links.push_back(link);
        }
        if (p + 1 < province_count) {
            ProvinceLink link{};
            link.neighbor_h3 = world.provinces[p + 1].h3_index;
            link.type = LinkType::Land;
            link.shared_border_km = 50.0f;
            link.transit_terrain_cost = 0.3f;
            link.infrastructure_bonus = 0.5f;
            world.provinces[p].links.push_back(link);
        }
    }

    world.nations.push_back(std::move(nation));

    // --- Markets (goods_count goods per province) ---
    for (uint32_t p = 0; p < province_count; ++p) {
        for (uint32_t g = 0; g < goods_count; ++g) {
            float base_price = 10.0f + static_cast<float>(g) * 5.0f;
            float supply = 100.0f + static_cast<float>(g % 3) * 50.0f;
            float demand = 80.0f + static_cast<float>(g % 4) * 30.0f;
            world.regional_markets.push_back(create_test_market(g, p, base_price, supply, demand));
            world.provinces[p].market_ids.push_back(
                static_cast<uint32_t>(world.regional_markets.size()) - 1);
        }
    }

    // --- NPCs (distributed round-robin across provinces) ---
    DeterministicRNG rng(seed);
    static constexpr NPCRole npc_roles[] = {NPCRole::worker,         NPCRole::worker,
                                            NPCRole::worker,         NPCRole::corporate_executive,
                                            NPCRole::middle_manager, NPCRole::banker,
                                            NPCRole::lawyer,         NPCRole::journalist,
                                            NPCRole::politician,     NPCRole::community_leader};
    static constexpr uint32_t role_count = sizeof(npc_roles) / sizeof(npc_roles[0]);

    for (uint32_t i = 0; i < npc_count; ++i) {
        uint32_t prov_id = i % province_count;
        NPCRole role = npc_roles[i % role_count];
        NPC npc = create_test_npc(100 + i, role, prov_id);

        // Vary capital using RNG
        npc.capital = 5000.0f + static_cast<float>(rng.next_uint(45000));
        npc.risk_tolerance = 0.2f + static_cast<float>(rng.next_uint(60)) / 100.0f;

        world.provinces[prov_id].significant_npc_ids.push_back(npc.id);
        world.significant_npcs.push_back(std::move(npc));
    }

    // --- Businesses (roughly 1 per 5 NPCs) ---
    uint32_t biz_count = std::max(1u, npc_count / 5);
    static constexpr BusinessSector biz_sectors[] = {
        BusinessSector::manufacturing, BusinessSector::food_beverage, BusinessSector::retail,
        BusinessSector::services,      BusinessSector::agriculture,   BusinessSector::technology};
    static constexpr uint32_t sector_count = sizeof(biz_sectors) / sizeof(biz_sectors[0]);

    for (uint32_t i = 0; i < biz_count; ++i) {
        uint32_t prov_id = i % province_count;
        uint32_t owner_id = 100 + (i * 5);  // every 5th NPC owns a business
        BusinessSector sector = biz_sectors[i % sector_count];
        NPCBusiness biz = create_test_business(1000 + i, owner_id, prov_id, sector);
        biz.cash = 50000.0f + static_cast<float>(rng.next_uint(200000));
        world.npc_businesses.push_back(std::move(biz));
    }

    // Build the province → significant_npcs index. Modules read this between
    // ticks; the orchestrator + apply_deltas keep it fresh thereafter.
    rebuild_npc_indices(world);

    return world;
}

// ---------------------------------------------------------------------------
// run_ticks — execute N ticks on a WorldState using a TickOrchestrator
// ---------------------------------------------------------------------------
inline void run_ticks(WorldState& world, TickOrchestrator& orchestrator, ThreadPool& pool,
                      uint32_t tick_count) {
    for (uint32_t i = 0; i < tick_count; ++i) {
        orchestrator.execute_tick(world, pool);
    }
}

// ---------------------------------------------------------------------------
// serialize_world_state — simple deterministic serialization for comparison
// Serializes key numeric fields in canonical order for bit-comparison.
// Not a full persistence serializer — just enough for determinism tests.
//
// Covers, in canonical order:
//   - current_tick
//   - significant_npcs (id-sorted): id, capital, status, risk_tolerance,
//       motivations.weights, current_province_id, home_province_id,
//       travel_status, social_capital, movement_follower_count,
//       memory_log (count + first MAX_MEMORY_PROBE entries by canonical order),
//       relationships (count + per-entry trust/fear/obligation/recovery_ceiling
//       in target_npc_id-sorted order), known_evidence/known_relationships
//       sizes
//   - regional_markets ((good_id, province_id)-sorted): good_id, province_id,
//       spot_price, equilibrium_price, supply, demand_buffer,
//       import_price_ceiling, export_price_floor
//   - npc_businesses (id-sorted): id, cash, revenue_per_tick, cost_per_tick
//   - province conditions
//   - goods_catalog (numeric_id-sorted): size + per-entry numeric_id + base_price
//   - npc_indices_by_province (per-province bucket size + first index)
//   - npc_index_by_id (size; map content is implied by the NPC list above)
//   - npc_indices_by_home_province (per-province bucket size)
//   - market_indices_by_province (per-province bucket size)
//   - market_index_by_good_province (size)
//
// Adding new state to WorldState that should be deterministic? Extend this
// helper too — otherwise divergence in the new field would pass silently.
// ---------------------------------------------------------------------------
inline std::vector<uint8_t> serialize_world_state(const WorldState& world) {
    std::vector<uint8_t> bytes;

    auto push_u32 = [&](uint32_t v) {
        bytes.push_back(static_cast<uint8_t>(v & 0xFF));
        bytes.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        bytes.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        bytes.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    };
    auto push_float = [&](float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        push_u32(bits);
    };

    push_u32(world.current_tick);

    // NPCs in id order
    std::vector<const NPC*> sorted_npcs;
    for (const auto& npc : world.significant_npcs) {
        sorted_npcs.push_back(&npc);
    }
    std::sort(sorted_npcs.begin(), sorted_npcs.end(),
              [](const NPC* a, const NPC* b) { return a->id < b->id; });

    // Cap on per-NPC memory entries serialised. The full log can hold up to
    // MAX_MEMORY_ENTRIES (500); we serialise the first MAX_MEMORY_PROBE in
    // canonical order so the bytes vector stays bounded but a divergence in
    // memory formation/decay still surfaces.
    static constexpr size_t MAX_MEMORY_PROBE = 16;

    push_u32(static_cast<uint32_t>(sorted_npcs.size()));
    for (const auto* npc : sorted_npcs) {
        push_u32(npc->id);
        push_float(npc->capital);
        push_u32(static_cast<uint32_t>(npc->status));
        push_float(npc->risk_tolerance);
        for (float w : npc->motivations.weights) {
            push_float(w);
        }

        // Location — currently-omitted in legacy harness; divergence in
        // travel/migration would otherwise pass silently.
        push_u32(npc->current_province_id);
        push_u32(npc->home_province_id);
        push_u32(static_cast<uint32_t>(npc->travel_status));

        push_float(npc->social_capital);
        push_u32(npc->movement_follower_count);

        // Memory log — count + first N entries in canonical (tick_timestamp,
        // subject_id) order. Sorting copies into a local vector to avoid
        // mutating world state.
        push_u32(static_cast<uint32_t>(npc->memory_log.size()));
        std::vector<const MemoryEntry*> sorted_mem;
        sorted_mem.reserve(npc->memory_log.size());
        for (const auto& m : npc->memory_log)
            sorted_mem.push_back(&m);
        std::sort(sorted_mem.begin(), sorted_mem.end(),
                  [](const MemoryEntry* a, const MemoryEntry* b) {
                      if (a->tick_timestamp != b->tick_timestamp)
                          return a->tick_timestamp < b->tick_timestamp;
                      if (a->subject_id != b->subject_id)
                          return a->subject_id < b->subject_id;
                      return static_cast<uint8_t>(a->type) < static_cast<uint8_t>(b->type);
                  });
        const size_t mem_count = std::min(sorted_mem.size(), MAX_MEMORY_PROBE);
        for (size_t i = 0; i < mem_count; ++i) {
            const auto* m = sorted_mem[i];
            push_u32(m->tick_timestamp);
            push_u32(static_cast<uint32_t>(m->type));
            push_u32(m->subject_id);
            push_float(m->emotional_weight);
            push_float(m->decay);
            push_u32(m->is_actionable ? 1u : 0u);
        }

        // Relationships — count + all entries sorted by target_npc_id.
        push_u32(static_cast<uint32_t>(npc->relationships.size()));
        std::vector<const Relationship*> sorted_rel;
        sorted_rel.reserve(npc->relationships.size());
        for (const auto& r : npc->relationships)
            sorted_rel.push_back(&r);
        std::sort(sorted_rel.begin(), sorted_rel.end(),
                  [](const Relationship* a, const Relationship* b) {
                      return a->target_npc_id < b->target_npc_id;
                  });
        for (const auto* r : sorted_rel) {
            push_u32(r->target_npc_id);
            push_float(r->trust);
            push_float(r->fear);
            push_float(r->obligation_balance);
            push_float(r->recovery_ceiling);
            push_u32(r->last_interaction_tick);
        }

        // Knowledge — counts only; entry-level comparison covered when those
        // mechanics produce delta-driven changes.
        push_u32(static_cast<uint32_t>(npc->known_evidence.size()));
        push_u32(static_cast<uint32_t>(npc->known_relationships.size()));
    }

    // Markets in (good_id, province_id) order
    std::vector<const RegionalMarket*> sorted_markets;
    for (const auto& m : world.regional_markets) {
        sorted_markets.push_back(&m);
    }
    std::sort(sorted_markets.begin(), sorted_markets.end(),
              [](const RegionalMarket* a, const RegionalMarket* b) {
                  if (a->good_id != b->good_id)
                      return a->good_id < b->good_id;
                  return a->province_id < b->province_id;
              });

    push_u32(static_cast<uint32_t>(sorted_markets.size()));
    for (const auto* m : sorted_markets) {
        push_u32(m->good_id);
        push_u32(m->province_id);
        push_float(m->spot_price);
        push_float(m->equilibrium_price);
        push_float(m->supply);
        push_float(m->demand_buffer);
        push_float(m->import_price_ceiling);
        push_float(m->export_price_floor);
    }

    // Businesses in id order
    std::vector<const NPCBusiness*> sorted_biz;
    for (const auto& b : world.npc_businesses) {
        sorted_biz.push_back(&b);
    }
    std::sort(sorted_biz.begin(), sorted_biz.end(),
              [](const NPCBusiness* a, const NPCBusiness* b) { return a->id < b->id; });

    push_u32(static_cast<uint32_t>(sorted_biz.size()));
    for (const auto* b : sorted_biz) {
        push_u32(b->id);
        push_float(b->cash);
        push_float(b->revenue_per_tick);
        push_float(b->cost_per_tick);
    }

    // Province conditions
    for (const auto& prov : world.provinces) {
        push_u32(prov.id);
        push_float(prov.conditions.stability_score);
        push_float(prov.conditions.crime_rate);
        push_float(prov.conditions.inequality_index);
        push_float(prov.community.grievance_level);
        push_float(prov.community.cohesion);
    }

    // Goods catalog — numeric_id and base_price per entry, numeric_id-sorted.
    // A nullptr catalog is serialised as 0u (count). The catalog's own load
    // path produces sequential numeric_ids starting at 0, so sorting is
    // idempotent on already-canonical input.
    if (world.goods_catalog) {
        const auto& goods = world.goods_catalog->goods();
        push_u32(static_cast<uint32_t>(goods.size()));
        std::vector<const GoodDefinition*> sorted_goods;
        sorted_goods.reserve(goods.size());
        for (const auto& g : goods)
            sorted_goods.push_back(&g);
        std::sort(sorted_goods.begin(), sorted_goods.end(),
                  [](const GoodDefinition* a, const GoodDefinition* b) {
                      return a->numeric_id < b->numeric_id;
                  });
        for (const auto* g : sorted_goods) {
            push_u32(g->numeric_id);
            push_float(g->base_price);
        }
    } else {
        push_u32(0u);
    }

    // Computed indices — comparing their contents catches non-deterministic
    // index rebuild order (e.g. iterating an unordered_map of NPCs during
    // rebuild). Bucket sizes are sufficient; the underlying data is already
    // serialised above so size-equality + per-NPC-equality implies content
    // equality.
    push_u32(static_cast<uint32_t>(world.npc_indices_by_province.size()));
    for (const auto& bucket : world.npc_indices_by_province) {
        push_u32(static_cast<uint32_t>(bucket.size()));
    }
    push_u32(static_cast<uint32_t>(world.npc_indices_by_home_province.size()));
    for (const auto& bucket : world.npc_indices_by_home_province) {
        push_u32(static_cast<uint32_t>(bucket.size()));
    }
    push_u32(static_cast<uint32_t>(world.npc_index_by_id.size()));
    push_u32(static_cast<uint32_t>(world.market_indices_by_province.size()));
    for (const auto& bucket : world.market_indices_by_province) {
        push_u32(static_cast<uint32_t>(bucket.size()));
    }
    push_u32(static_cast<uint32_t>(world.market_index_by_good_province.size()));

    return bytes;
}

}  // namespace test
}  // namespace econlife
