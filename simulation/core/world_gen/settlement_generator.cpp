// SettlementGenerator — population attractiveness, NPC seeding, and business creation.
// Split from WorldGenerator to keep world_gen focused on physical geography.

#include "core/world_gen/settlement_generator.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace econlife {

// ===========================================================================
// Stage 9 — Population and Infrastructure Seeding (WorldGen v0.18 §9)
// ===========================================================================

void SettlementGenerator::seed_population_attractiveness(WorldState& world, DeterministicRNG& rng,
                                                         const WorldGeneratorConfig& config) {
    const auto& p = config.population;

    for (auto& prov : world.provinces) {
        // -----------------------------------------------------------------
        // §9.2 — Disease burden from climate zone + standing water + elevation.
        // -----------------------------------------------------------------
        float burden = 0.0f;
        KoppenZone kz = prov.climate.koppen_zone;

        if (kz == KoppenZone::Af || kz == KoppenZone::Am) {
            burden += 0.55f;  // hyperendemic tropical rainforest
        } else if (kz == KoppenZone::Aw || kz == KoppenZone::BSh) {
            burden += 0.35f;  // tropical savanna/steppe; seasonal
        } else if (kz == KoppenZone::Cfa || kz == KoppenZone::Cwa) {
            burden += 0.15f;  // humid subtropical
        }

        // Standing water amplifies vector habitat.
        if (prov.climate.flood_vulnerability > 0.6f) {
            burden += 0.15f;
        }

        // Elevation reduces disease (malaria vector altitude limit ~2,000m).
        if (prov.geography.elevation_avg_m > 2000.0f) {
            burden *= 0.10f;
        } else if (prov.geography.elevation_avg_m > 1500.0f) {
            burden *= 0.35f;
        } else if (prov.geography.elevation_avg_m > 1000.0f) {
            burden *= 0.65f;
        }

        prov.disease_burden = std::min(1.0f, std::max(0.0f, burden));

        // -----------------------------------------------------------------
        // §9.1 — Settlement attractiveness formula.
        // -----------------------------------------------------------------

        // Check for geothermal deposit.
        bool has_geothermal = false;
        for (const auto& d : prov.deposits) {
            if (d.type == ResourceType::Geothermal) {
                has_geothermal = true;
                break;
            }
        }

        float base = prov.agricultural_productivity * p.w_ag_productivity +
                     prov.geography.port_capacity * p.w_port_capacity +
                     prov.geography.river_access * p.w_river_access +
                     (1.0f - prov.geography.terrain_roughness) * p.w_terrain_flatness +
                     (prov.soil_type == SoilType::Alluvial ? 1.0f : 0.0f) * p.w_alluvial_soil +
                     (has_geothermal ? 1.0f : 0.0f) * p.w_geothermal +
                     (prov.soil_type == SoilType::Andisol ? 1.0f : 0.0f) * p.w_volcanic_soil;

        // Altitude ceiling — hard physiological limits.
        if (prov.geography.elevation_avg_m > 4500.0f) {
            base *= p.alt_4500m_mult;
        } else if (prov.geography.elevation_avg_m > 3500.0f) {
            base *= p.alt_3500m_mult;
        } else if (prov.geography.elevation_avg_m > 2500.0f) {
            base *= p.alt_2500m_mult;
        } else if (prov.geography.elevation_avg_m > 1500.0f) {
            base *= p.alt_1500m_mult;
        }

        // Disease burden penalty (§9.2).
        base *= (1.0f - prov.disease_burden * p.disease_max_penalty);

        // Environmental penalties — genuine uninhabitability.
        if (kz == KoppenZone::BWh || kz == KoppenZone::BWk) {
            base *= p.desert_mult;
        }
        if (kz == KoppenZone::EF) {
            base *= p.ice_cap_mult;
        }
        if (kz == KoppenZone::ET) {
            base *= p.tundra_mult;
        }
        if (prov.geography.terrain_roughness > 0.85f) {
            base *= p.extreme_terrain_mult;
        }
        if (prov.island_isolation) {
            base *= p.island_isolation_mult;
        }
        if (prov.has_permafrost) {
            base *= p.continuous_permafrost_mult;
        }

        float score = std::max(0.0f, std::min(1.0f, base));
        prov.settlement_attractiveness = score;

        // -----------------------------------------------------------------
        // §9.3 — Infrastructure derivation from attractiveness.
        // Flood vulnerability reduces infrastructure (costlier construction)
        // without reducing population attractiveness.
        // -----------------------------------------------------------------
        float infra = score * p.infra_attract_scale -
                      prov.climate.flood_vulnerability * p.infra_flood_penalty +
                      (rng.next_float() - 0.5f) * 2.0f * p.infra_variance_sigma;
        infra = std::max(0.0f, std::min(1.0f, infra));

        // Blend with archetype infrastructure (50/50) to maintain diversity.
        prov.infrastructure_rating = prov.infrastructure_rating * 0.50f + infra * 0.50f;

        // -----------------------------------------------------------------
        // §9.4 — Population adjustment from attractiveness.
        // -----------------------------------------------------------------
        float multiplier = p.multiplier_base + score * p.multiplier_range;
        multiplier *= (p.rng_variation_base + rng.next_float() * p.rng_variation_range);

        uint32_t new_pop = static_cast<uint32_t>(
            static_cast<float>(prov.demographics.total_population) * multiplier + 0.5f);
        prov.demographics.total_population = std::max(p.population_floor, new_pop);
    }
}

// ===========================================================================
// NPC population seeding
// ===========================================================================

void SettlementGenerator::create_npcs(WorldState& world, DeterministicRNG& rng,
                                      const WorldGeneratorConfig& config) {
    // Role distribution weights — determines the workforce composition.
    // Workers are the majority; specialists are rarer.
    struct RoleWeight {
        NPCRole role;
        uint32_t weight;  // relative frequency
    };
    static constexpr RoleWeight role_distribution[] = {
        {NPCRole::worker, 40},
        {NPCRole::corporate_executive, 5},
        {NPCRole::middle_manager, 8},
        {NPCRole::banker, 3},
        {NPCRole::lawyer, 3},
        {NPCRole::accountant, 3},
        {NPCRole::journalist, 2},
        {NPCRole::politician, 3},
        {NPCRole::community_leader, 3},
        {NPCRole::law_enforcement, 5},
        {NPCRole::regulator, 2},
        {NPCRole::prosecutor, 1},
        {NPCRole::judge, 1},
        {NPCRole::criminal_operator, 4},
        {NPCRole::criminal_enforcer, 3},
        {NPCRole::fixer, 2},
        {NPCRole::union_organizer, 1},
        {NPCRole::family_member, 8},
        {NPCRole::media_editor, 1},
        {NPCRole::ngo_investigator, 1},
    };
    static constexpr uint32_t total_weight =
        40 + 5 + 8 + 3 + 3 + 3 + 2 + 3 + 3 + 5 + 2 + 1 + 1 + 4 + 3 + 2 + 1 + 8 + 1 + 1;

    // Distribute NPCs across provinces weighted by population.
    uint64_t total_pop = 0;
    for (const auto& p : world.provinces) {
        total_pop += p.demographics.total_population;
    }
    if (total_pop == 0)
        total_pop = 1;

    uint32_t npc_id_counter = 100;  // start NPC IDs at 100

    for (uint32_t p = 0; p < static_cast<uint32_t>(world.provinces.size()); ++p) {
        // NPCs per province proportional to population.
        float pop_fraction = static_cast<float>(world.provinces[p].demographics.total_population) /
                             static_cast<float>(total_pop);
        uint32_t npcs_for_province =
            std::max(1u, static_cast<uint32_t>(config.npc_count * pop_fraction + 0.5f));

        for (uint32_t i = 0; i < npcs_for_province; ++i) {
            // Select role via weighted random.
            uint32_t roll = rng.next_uint(total_weight);
            NPCRole role = NPCRole::worker;  // default
            uint32_t cumulative = 0;
            for (const auto& rw : role_distribution) {
                cumulative += rw.weight;
                if (roll < cumulative) {
                    role = rw.role;
                    break;
                }
            }

            NPC npc{};
            npc.id = npc_id_counter++;
            npc.role = role;
            npc.home_province_id = p;
            npc.current_province_id = p;
            npc.travel_status = static_cast<NPCTravelStatus>(0);  // resident
            npc.status = NPCStatus::active;

            // Capital varies by role.
            float capital_base;
            switch (role) {
                case NPCRole::corporate_executive:
                case NPCRole::banker:
                    capital_base = 50000.0f + static_cast<float>(rng.next_uint(100000));
                    break;
                case NPCRole::lawyer:
                case NPCRole::politician:
                case NPCRole::judge:
                case NPCRole::media_editor:
                    capital_base = 30000.0f + static_cast<float>(rng.next_uint(50000));
                    break;
                case NPCRole::middle_manager:
                case NPCRole::accountant:
                    capital_base = 20000.0f + static_cast<float>(rng.next_uint(30000));
                    break;
                case NPCRole::criminal_operator:
                    capital_base = 15000.0f + static_cast<float>(rng.next_uint(80000));
                    break;
                case NPCRole::fixer:
                    capital_base = 10000.0f + static_cast<float>(rng.next_uint(60000));
                    break;
                default:
                    capital_base = 5000.0f + static_cast<float>(rng.next_uint(25000));
                    break;
            }
            npc.capital = capital_base;

            // Risk tolerance varies by role.
            switch (role) {
                case NPCRole::criminal_operator:
                case NPCRole::criminal_enforcer:
                case NPCRole::fixer:
                    npc.risk_tolerance = 0.5f + rng.next_float() * 0.4f;
                    break;
                case NPCRole::law_enforcement:
                case NPCRole::judge:
                    npc.risk_tolerance = 0.2f + rng.next_float() * 0.3f;
                    break;
                default:
                    npc.risk_tolerance = 0.2f + rng.next_float() * 0.6f;
                    break;
            }

            // Social capital varies by role.
            switch (role) {
                case NPCRole::community_leader:
                case NPCRole::politician:
                    npc.social_capital = 0.5f + rng.next_float() * 0.4f;
                    break;
                case NPCRole::journalist:
                case NPCRole::media_editor:
                    npc.social_capital = 0.3f + rng.next_float() * 0.4f;
                    break;
                default:
                    npc.social_capital = 0.1f + rng.next_float() * 0.4f;
                    break;
            }

            npc.movement_follower_count = 0;

            // Motivation vector — 8 weights summing to 1.0.
            // Order: financial_security, social_standing, personal_safety, power_influence,
            //        ideology, loyalty, self_preservation, risk_seeking
            float weights[8];
            float weight_sum = 0.0f;
            for (int w = 0; w < 8; ++w) {
                weights[w] = 0.05f + rng.next_float() * 0.3f;
                weight_sum += weights[w];
            }
            // Bias based on role.
            switch (role) {
                case NPCRole::corporate_executive:
                case NPCRole::banker:
                    weights[0] *= 2.0f;        // financial_security
                    weight_sum += weights[0];  // adjust
                    break;
                case NPCRole::politician:
                case NPCRole::community_leader:
                    weights[3] *= 2.0f;  // power_influence
                    weight_sum += weights[3];
                    break;
                case NPCRole::criminal_operator:
                case NPCRole::criminal_enforcer:
                    weights[0] *= 1.5f;  // financial
                    weights[7] *= 1.5f;  // risk_seeking
                    weight_sum += weights[0] * 0.5f + weights[7] * 0.5f;
                    break;
                case NPCRole::law_enforcement:
                case NPCRole::prosecutor:
                    weights[4] *= 1.5f;  // ideology
                    weights[6] *= 1.5f;  // self_preservation
                    weight_sum += weights[4] * 0.5f + weights[6] * 0.5f;
                    break;
                default:
                    break;
            }
            // Recalculate sum and normalize to 1.0.
            weight_sum = 0.0f;
            for (int w = 0; w < 8; ++w)
                weight_sum += weights[w];
            for (int w = 0; w < 8; ++w)
                weights[w] /= weight_sum;
            npc.motivations.weights = {weights[0], weights[1], weights[2], weights[3],
                                       weights[4], weights[5], weights[6], weights[7]};

            world.provinces[p].significant_npc_ids.push_back(npc.id);
            world.significant_npcs.push_back(std::move(npc));
        }
    }
}

// ===========================================================================
// Business seeding
// ===========================================================================

void SettlementGenerator::create_businesses(WorldState& world, DeterministicRNG& rng,
                                            const WorldGeneratorConfig& config) {
    // Business count: roughly 1 per 10 NPCs, minimum 1 per province.
    uint32_t total_businesses = std::max(static_cast<uint32_t>(world.provinces.size()),
                                         static_cast<uint32_t>(world.significant_npcs.size()) / 10);

    // Sector distribution per province — influenced by archetype.
    // Collect executive/corporate NPCs as potential owners.
    struct OwnerCandidate {
        uint32_t npc_id;
        uint32_t province_id;
    };
    std::vector<OwnerCandidate> owner_pool;
    for (const auto& npc : world.significant_npcs) {
        if (npc.role == NPCRole::corporate_executive || npc.role == NPCRole::middle_manager ||
            npc.role == NPCRole::criminal_operator || npc.role == NPCRole::fixer) {
            owner_pool.push_back({npc.id, npc.home_province_id});
        }
    }

    // If not enough owners, also include bankers and lawyers.
    if (owner_pool.size() < total_businesses) {
        for (const auto& npc : world.significant_npcs) {
            if (npc.role == NPCRole::banker || npc.role == NPCRole::lawyer) {
                owner_pool.push_back({npc.id, npc.home_province_id});
            }
        }
    }

    // Fallback: any NPC can own a business.
    if (owner_pool.empty()) {
        for (const auto& npc : world.significant_npcs) {
            owner_pool.push_back({npc.id, npc.home_province_id});
        }
    }

    // Sector options for formal businesses.
    static constexpr BusinessSector formal_sectors[] = {
        BusinessSector::manufacturing, BusinessSector::food_beverage,
        BusinessSector::retail,        BusinessSector::services,
        BusinessSector::agriculture,   BusinessSector::technology,
        BusinessSector::real_estate,   BusinessSector::energy,
        BusinessSector::finance,       BusinessSector::transport_logistics,
        BusinessSector::media,         BusinessSector::research,
    };
    static constexpr uint32_t formal_sector_count =
        sizeof(formal_sectors) / sizeof(formal_sectors[0]);

    // Business profiles.
    static constexpr BusinessProfile profiles[] = {
        BusinessProfile::cost_cutter,
        BusinessProfile::quality_player,
        BusinessProfile::fast_expander,
        BusinessProfile::defensive_incumbent,
    };

    uint32_t biz_id_counter = 1000;

    for (uint32_t b = 0; b < total_businesses; ++b) {
        uint32_t owner_idx = rng.next_uint(static_cast<uint32_t>(owner_pool.size()));
        const auto& owner = owner_pool[owner_idx];

        bool is_criminal = false;
        // Criminal businesses: ~10% of total, owned by criminal operators.
        for (const auto& npc : world.significant_npcs) {
            if (npc.id == owner.npc_id &&
                (npc.role == NPCRole::criminal_operator || npc.role == NPCRole::fixer)) {
                is_criminal = (rng.next_uint(3) == 0);  // 33% chance for criminal NPCs
                break;
            }
        }

        NPCBusiness biz{};
        biz.id = biz_id_counter++;
        biz.owner_id = owner.npc_id;
        biz.province_id = owner.province_id;
        biz.criminal_sector = is_criminal;

        if (is_criminal) {
            biz.sector = BusinessSector::criminal;
        } else {
            biz.sector = formal_sectors[rng.next_uint(formal_sector_count)];
        }

        biz.profile = profiles[rng.next_uint(4)];

        // Cash and revenue based on province demographics.
        float province_wealth = 1.0f;
        if (owner.province_id < world.provinces.size()) {
            province_wealth =
                world.provinces[owner.province_id].demographics.income_high_fraction * 2.0f +
                world.provinces[owner.province_id].demographics.income_middle_fraction;
        }
        biz.cash = (20000.0f + static_cast<float>(rng.next_uint(200000))) * province_wealth;
        biz.revenue_per_tick = (200.0f + static_cast<float>(rng.next_uint(800))) * province_wealth;
        biz.cost_per_tick =
            biz.revenue_per_tick * (0.6f + rng.next_float() * 0.3f);  // 60-90% cost ratio

        biz.market_share = 0.02f + rng.next_float() * 0.15f;
        biz.strategic_decision_tick = rng.next_uint(90);  // stagger quarterly decisions
        biz.dispatch_day_offset = static_cast<uint8_t>(biz.id % 30);
        biz.actor_tech_state.effective_tech_tier = 1.0f;
        biz.regulatory_violation_severity = is_criminal ? 0.3f + rng.next_float() * 0.5f : 0.0f;
        biz.default_activity_scope =
            is_criminal ? VisibilityScope::concealed : VisibilityScope::institutional;
        biz.deferred_salary_liability = 0.0f;
        biz.accounts_payable_float = 0.0f;

        world.npc_businesses.push_back(std::move(biz));
    }
}

// ===========================================================================
// Facility assignment — gives each business 1-3 facilities with recipes
// ===========================================================================

// Maps business sector to preferred facility type categories.

}  // namespace econlife
