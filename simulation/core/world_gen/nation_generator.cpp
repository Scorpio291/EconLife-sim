// NationGenerator — nation formation, nomadic population, and capital seeding.
// Split from WorldGenerator to keep world_gen focused on physical geography.

#include "core/world_gen/nation_generator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>

#include "core/world_gen/h3_utils.h"

namespace econlife {

// ===========================================================================
// Stage 9.5 — Nation formation (WorldGen v0.18 §9.5)
// ===========================================================================

// Helper: compute terrain resistance for crossing a province link.
// Mountains, rivers, and maritime crossings raise resistance; open flatland is cheap.
// Uses NationFormationParams for all thresholds so the algorithm is tunable.
static float compute_terrain_resistance(const ProvinceLink& link, const Province& neighbor,
                                        const WorldGeneratorConfig::NationFormationParams& nfp) {
    float resistance = 1.0f;

    // Maritime crossing: ocean is slower than land conquest
    if (link.type == LinkType::Maritime) {
        resistance += nfp.maritime_resistance;
    }

    // Elevation change proxy: high transit_terrain_cost means steep terrain
    if (link.transit_terrain_cost > nfp.steep_terrain_threshold) {
        resistance *= (1.0f + link.transit_terrain_cost);
    }

    // River crossing barrier (river links represent major navigable rivers)
    if (link.type == LinkType::River) {
        resistance *= nfp.river_crossing_mult;
    }

    // Uninhabitable provinces are expensive to cross (deserts, ice caps)
    if (neighbor.settlement_attractiveness < 0.05f) {
        resistance *= nfp.uninhabitable_mult;
    }

    return resistance;
}

// Helper: classify nation size from province count.
static NationSize classify_nation_size(size_t province_count) {
    if (province_count <= 3)
        return NationSize::Microstate;
    if (province_count <= 12)
        return NationSize::Small;
    if (province_count <= 40)
        return NationSize::Medium;
    if (province_count <= 120)
        return NationSize::Large;
    return NationSize::Continental;
}

// Helper: NationSize to string for JSON serialization.
static const char* nation_size_str(NationSize s) {
    switch (s) {
        case NationSize::Microstate:
            return "microstate";
        case NationSize::Small:
            return "small";
        case NationSize::Medium:
            return "medium";
        case NationSize::Large:
            return "large";
        case NationSize::Continental:
            return "continental";
    }
    return "small";
}

// Language families for V1 world generation. On non-Earth worlds all families
// get equal geographic affinity weight — the list provides naming diversity.
static const char* v1_language_families[] = {
    "germanic", "romance", "slavic", "sinitic",      "arabic",
    "turkic",   "indic",   "bantu",  "austronesian", "quechuan",
};
static constexpr size_t v1_language_family_count = 10;

void NationGenerator::form_nations(WorldState& world, DeterministicRNG& rng,
                                   const WorldGeneratorConfig& config) {
    const auto& provinces = world.provinces;
    const uint32_t prov_count = static_cast<uint32_t>(provinces.size());
    if (prov_count == 0)
        return;

    const auto& nfp = config.nation_formation;

    // -----------------------------------------------------------------------
    // Build h3_index → province_id lookup (O(1) neighbor resolution).
    // Built FIRST so all subsequent passes use O(1) lookups instead of O(n) scans.
    // -----------------------------------------------------------------------
    std::unordered_map<H3Index, uint32_t> h3_to_idx;
    h3_to_idx.reserve(prov_count);
    for (uint32_t i = 0; i < prov_count; ++i) {
        h3_to_idx[provinces[i].h3_index] = i;
    }

    // -----------------------------------------------------------------------
    // §9.5.1 — Nation Seed Placement
    // -----------------------------------------------------------------------
    // Collect habitable provinces: attractiveness > 0.10, not EF ice cap.
    // Provinces below uninhabitable_threshold (0.02) are excluded from nation
    // assignment entirely — they become unclaimed territory per spec.
    std::vector<uint32_t> habitable;
    habitable.reserve(prov_count);
    for (uint32_t i = 0; i < prov_count; ++i) {
        const auto& p = provinces[i];
        if (p.settlement_attractiveness > 0.10f && p.climate.koppen_zone != KoppenZone::EF) {
            habitable.push_back(i);
        }
    }
    if (habitable.empty()) {
        // Degenerate world: all provinces are habitable for nation assignment.
        for (uint32_t i = 0; i < prov_count; ++i)
            habitable.push_back(i);
    }

    // Target nation count: sqrt(habitable) × scale, clamped [min, max].
    // For small worlds (habitable < min²/scale²), we gracefully reduce below
    // the spec minimum to avoid more nations than provinces. The spec [20,400]
    // targets Earth-scale (1000+ provinces); V1 with 6 provinces gets ~4.
    uint32_t raw_target = static_cast<uint32_t>(std::sqrt(static_cast<float>(habitable.size())) *
                                                nfp.seed_count_scale);
    uint32_t target_count = std::clamp(raw_target, nfp.seed_count_min, nfp.seed_count_max);
    // Never more nations than habitable provinces; minimum 2 for geopolitical tension.
    target_count = std::min(target_count, static_cast<uint32_t>(habitable.size()));
    target_count = std::max(target_count, std::min(2u, static_cast<uint32_t>(habitable.size())));

    // Build attractiveness² weights for seed selection bias.
    // Attractiveness² biases toward high-value inland/coastal cores, not thin margins.
    std::vector<float> weights(habitable.size());
    float total_weight = 0.0f;
    for (size_t i = 0; i < habitable.size(); ++i) {
        float a = provinces[habitable[i]].settlement_attractiveness;
        weights[i] = a * a;
        total_weight += weights[i];
    }

    // Track which provinces are available for seed selection.
    // Using a bool vector is O(1) per check vs O(log n) for unordered_set.
    std::vector<bool> available(prov_count, false);
    for (uint32_t pid : habitable)
        available[pid] = true;

    std::vector<uint32_t> seed_province_ids;
    seed_province_ids.reserve(target_count);

    // Minimum separation in graph hops (BFS distance through ProvinceLinks).
    // Spec §9.5.1: "no two seeds closer than 3 H3 grid-disks."
    // For small worlds (< seed_separation * 2 provinces), reduce to avoid
    // excluding all candidates.
    uint32_t effective_separation = nfp.seed_separation;
    if (habitable.size() < effective_separation * 2) {
        effective_separation = 0;  // small worlds: no separation constraint
    }

    // Weighted sampling loop. On each iteration we pick one seed, then
    // exclude neighbors within effective_separation via BFS.
    // Instead of recomputing total_weight from scratch each time (O(n)),
    // we subtract removed weights incrementally.
    uint32_t max_attempts = static_cast<uint32_t>(habitable.size()) * 3;
    for (uint32_t attempt = 0; seed_province_ids.size() < target_count && attempt < max_attempts;
         ++attempt) {
        if (total_weight <= 0.0f)
            break;

        // Weighted random selection.
        float roll = rng.next_float() * total_weight;
        float cumulative = 0.0f;
        uint32_t chosen = habitable[0];
        size_t chosen_idx = 0;
        for (size_t i = 0; i < habitable.size(); ++i) {
            if (!available[habitable[i]])
                continue;
            cumulative += weights[i];
            if (cumulative >= roll) {
                chosen = habitable[i];
                chosen_idx = i;
                break;
            }
        }

        if (!available[chosen])
            continue;

        seed_province_ids.push_back(chosen);
        available[chosen] = false;
        total_weight -= weights[chosen_idx];
        weights[chosen_idx] = 0.0f;

        // BFS exclusion zone: mark neighbors within effective_separation as unavailable.
        if (effective_separation > 0) {
            std::queue<std::pair<uint32_t, uint32_t>> bfs;
            bfs.push({chosen, 0});
            std::unordered_set<uint32_t> visited_bfs;
            visited_bfs.insert(chosen);
            while (!bfs.empty()) {
                auto [cur, dist] = bfs.front();
                bfs.pop();
                if (dist >= effective_separation)
                    continue;
                for (const auto& link : provinces[cur].links) {
                    auto it = h3_to_idx.find(link.neighbor_h3);
                    if (it == h3_to_idx.end())
                        continue;
                    uint32_t nid = it->second;
                    if (visited_bfs.count(nid))
                        continue;
                    visited_bfs.insert(nid);
                    if (available[nid]) {
                        available[nid] = false;
                        // Find and zero this province's weight to keep total_weight accurate.
                        for (size_t i = 0; i < habitable.size(); ++i) {
                            if (habitable[i] == nid) {
                                total_weight -= weights[i];
                                weights[i] = 0.0f;
                                break;
                            }
                        }
                    }
                    bfs.push({nid, dist + 1});
                }
            }
        }
    }

    // Fallback: if no seeds were placed, use the highest-attractiveness province.
    if (seed_province_ids.empty()) {
        uint32_t best = 0;
        for (uint32_t i = 1; i < prov_count; ++i) {
            if (provinces[i].settlement_attractiveness >
                provinces[best].settlement_attractiveness) {
                best = i;
            }
        }
        seed_province_ids.push_back(best);
    }

    const uint32_t nation_count = static_cast<uint32_t>(seed_province_ids.size());

    // -----------------------------------------------------------------------
    // §9.5.2 — Voronoi Growth with Terrain Resistance
    // -----------------------------------------------------------------------
    // Identify uninhabitable provinces that should remain unclaimed.
    std::vector<bool> uninhabitable(prov_count, false);
    for (uint32_t i = 0; i < prov_count; ++i) {
        if (provinces[i].settlement_attractiveness < nfp.uninhabitable_threshold &&
            provinces[i].climate.koppen_zone == KoppenZone::EF) {
            uninhabitable[i] = true;
        }
        // Deep desert with near-zero attractiveness also unclaimed.
        if (provinces[i].settlement_attractiveness < nfp.uninhabitable_threshold &&
            (provinces[i].climate.koppen_zone == KoppenZone::BWh ||
             provinces[i].climate.koppen_zone == KoppenZone::BWk)) {
            uninhabitable[i] = true;
        }
    }

    // Identify island provinces (all links are Maritime) for special post-pass assignment.
    std::vector<bool> is_island(prov_count, false);
    for (uint32_t i = 0; i < prov_count; ++i) {
        if (provinces[i].links.empty())
            continue;
        bool all_maritime = true;
        for (const auto& link : provinces[i].links) {
            if (link.type != LinkType::Maritime) {
                all_maritime = false;
                break;
            }
        }
        if (all_maritime)
            is_island[i] = true;
    }

    // Dijkstra-style priority queue: (cost, province_id, nation_index).
    using PQEntry = std::tuple<float, uint32_t, uint32_t>;
    std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<PQEntry>> pq;

    std::vector<float> best_cost(prov_count, std::numeric_limits<float>::max());
    std::vector<int32_t> assignment(prov_count, -1);  // -1 = unassigned

    // Initialize seeds. Uninhabitable seeds shouldn't occur (filtered in habitable),
    // but guard anyway.
    for (uint32_t ni = 0; ni < nation_count; ++ni) {
        uint32_t pid = seed_province_ids[ni];
        best_cost[pid] = 0.0f;
        assignment[pid] = static_cast<int32_t>(ni);
        pq.push({0.0f, pid, ni});
    }

    // Main Voronoi flood-fill. Skip uninhabitable and island provinces (islands
    // get special assignment in the post-pass below).
    while (!pq.empty()) {
        auto [cost, current, nation_idx] = pq.top();
        pq.pop();

        if (cost > best_cost[current])
            continue;

        for (const auto& link : provinces[current].links) {
            auto it = h3_to_idx.find(link.neighbor_h3);
            if (it == h3_to_idx.end())
                continue;
            uint32_t neighbor_id = it->second;

            // Skip uninhabitable provinces — they remain unclaimed territory.
            if (uninhabitable[neighbor_id])
                continue;

            // Skip islands during main pass — they get post-pass assignment.
            if (is_island[neighbor_id])
                continue;

            if (assignment[neighbor_id] >= 0 && best_cost[neighbor_id] <= cost)
                continue;

            float resistance = compute_terrain_resistance(link, provinces[neighbor_id], nfp);
            float new_cost = cost + resistance;

            if (new_cost < best_cost[neighbor_id]) {
                best_cost[neighbor_id] = new_cost;
                assignment[neighbor_id] = static_cast<int32_t>(nation_idx);
                pq.push({new_cost, neighbor_id, nation_idx});
            }
        }
    }

    // §9.5.2 post-pass: assign island provinces to the nation that owns the
    // maritime-adjacent province with the longest shared border.
    for (uint32_t i = 0; i < prov_count; ++i) {
        if (!is_island[i])
            continue;
        if (uninhabitable[i])
            continue;

        int32_t best_nation = -1;
        float best_border_km = -1.0f;
        for (const auto& link : provinces[i].links) {
            auto it = h3_to_idx.find(link.neighbor_h3);
            if (it == h3_to_idx.end())
                continue;
            uint32_t nid = it->second;
            if (assignment[nid] >= 0 && link.shared_border_km > best_border_km) {
                best_border_km = link.shared_border_km;
                best_nation = assignment[nid];
            }
        }
        // If no assigned neighbor found (all neighbors are also islands),
        // try any assigned neighbor via BFS at distance 2.
        if (best_nation < 0) {
            for (const auto& link : provinces[i].links) {
                auto it = h3_to_idx.find(link.neighbor_h3);
                if (it == h3_to_idx.end())
                    continue;
                uint32_t nid = it->second;
                for (const auto& link2 : provinces[nid].links) {
                    auto it2 = h3_to_idx.find(link2.neighbor_h3);
                    if (it2 == h3_to_idx.end())
                        continue;
                    if (assignment[it2->second] >= 0) {
                        best_nation = assignment[it2->second];
                        break;
                    }
                }
                if (best_nation >= 0)
                    break;
            }
        }
        if (best_nation >= 0) {
            assignment[i] = best_nation;
        }
        // else: truly isolated island — remains unclaimed (rare).
    }

    // Provinces that remain unassigned (no links, not uninhabitable) → first nation.
    for (uint32_t i = 0; i < prov_count; ++i) {
        if (assignment[i] < 0 && !uninhabitable[i]) {
            assignment[i] = 0;
        }
    }

    // -----------------------------------------------------------------------
    // Build Nation objects
    // -----------------------------------------------------------------------
    world.nations.clear();
    world.region_groups.clear();

    // Scalable name generation: combine prefix + root, cycling through roots.
    // 8 prefixes × 60 roots = 480 unique names, sufficient for max 400 nations.
    static const char* nation_prefixes[] = {
        "Republic of ", "Kingdom of ",  "Federation of ",   "Commonwealth of ",
        "State of ",    "Dominion of ", "Principality of ", "Union of ",
    };
    static constexpr size_t n_prefixes = 8;
    static const char* nation_roots[] = {
        "Avalon",   "Corvana",  "Delvoria",  "Estmarch",  "Fenwick",  "Galdria",    "Haldane",
        "Irindel",  "Jastova",  "Keldara",   "Lorantia",  "Morvaine", "Narthia",    "Ostenveld",
        "Pelluria", "Quentara", "Rhedania",  "Sylvarna",  "Torvalis", "Ulmendia",   "Veldmark",
        "Wyverna",  "Xandria",  "Yelthar",   "Zaranthia", "Almuria",  "Brestova",   "Calendra",
        "Damavand", "Eldrathi", "Frostmark", "Gelvaine",  "Halvoria", "Istharan",   "Jorvald",
        "Korinth",  "Lynthia",  "Marvesta",  "Novogard",  "Orelund",  "Praethos",   "Quelaria",
        "Rochfort", "Silvaine", "Thalmund",  "Umbravia",  "Valdanis", "Westmark",   "Xenthira",
        "Yrvandia", "Zephyria", "Arcandia",  "Belmoris",  "Calderon", "Dravenholm", "Elysar",
        "Fyrnhold", "Grenmark", "Helmgaard", "Isolvar",
    };
    static constexpr size_t n_roots = 60;

    for (uint32_t ni = 0; ni < nation_count; ++ni) {
        Nation nation{};
        nation.id = ni;

        // Deterministic name from RNG + cycling through root/prefix pools.
        uint32_t prefix_idx = static_cast<uint32_t>(rng.next_float() * n_prefixes) % n_prefixes;
        uint32_t root_idx = ni % n_roots;
        nation.name = std::string(nation_prefixes[prefix_idx]) + nation_roots[root_idx];
        // Currency code: first 3 chars of root, uppercased.
        std::string root_str(nation_roots[root_idx]);
        nation.currency_code = root_str.substr(0, 3);
        for (auto& c : nation.currency_code)
            c = static_cast<char>(std::toupper(c));

        // Government type weighted by RNG roll.
        float gov_roll = rng.next_float();
        if (gov_roll < 0.50f)
            nation.government_type = GovernmentType::Democracy;
        else if (gov_roll < 0.75f)
            nation.government_type = GovernmentType::Federation;
        else if (gov_roll < 0.95f)
            nation.government_type = GovernmentType::Autocracy;
        else
            nation.government_type = GovernmentType::FailedState;

        nation.political_cycle = {0, 0.50f + rng.next_float() * 0.20f, false,
                                  365 * (3 + static_cast<uint32_t>(rng.next_float() * 3))};
        nation.corporate_tax_rate = 0.15f + rng.next_float() * 0.20f;
        nation.income_tax_rate_top_bracket = 0.20f + rng.next_float() * 0.30f;
        nation.trade_balance_fraction = 0.0f;
        nation.inflation_rate = 0.01f + rng.next_float() * 0.05f;
        nation.credit_rating = 0.40f + rng.next_float() * 0.50f;
        nation.tariff_schedule = nullptr;

        // First nation (player's home) is LOD 0; others are LOD 1.
        if (ni == 0) {
            nation.lod1_profile = std::nullopt;
        } else {
            Lod1NationProfile lod1{};
            lod1.export_margin = 0.10f + rng.next_float() * 0.15f;
            lod1.import_premium = 0.05f + rng.next_float() * 0.10f;
            lod1.trade_openness = 0.30f + rng.next_float() * 0.40f;
            lod1.tech_tier_modifier = 0.80f + rng.next_float() * 0.40f;
            lod1.population_modifier = 0.70f + rng.next_float() * 0.60f;
            lod1.research_investment = 0.0f;
            lod1.current_tier = 1;
            nation.lod1_profile = lod1;
        }

        // Collect member provinces.
        for (uint32_t pi = 0; pi < prov_count; ++pi) {
            if (assignment[pi] == static_cast<int32_t>(ni)) {
                nation.province_ids.push_back(pi);
            }
        }

        nation.size_class = classify_nation_size(nation.province_ids.size());

        world.nations.push_back(std::move(nation));
    }

    // Assign nation_id on each province. Unclaimed provinces get a sentinel
    // value equal to nation_count (no valid nation). For V1 runtime compat,
    // unclaimed provinces still get nation_id = 0 (player's nation handles them).
    for (uint32_t i = 0; i < prov_count; ++i) {
        if (assignment[i] >= 0) {
            world.provinces[i].nation_id = static_cast<uint32_t>(assignment[i]);
        } else {
            // Unclaimed territory: assign to nation 0 for V1 runtime compat.
            // Stage 10 can read uninhabitable[] to detect these.
            world.provinces[i].nation_id = 0;
        }
    }

    // Create one region per province (V1 simplification).
    for (uint32_t r = 0; r < prov_count; ++r) {
        Region region{};
        region.id = r;
        region.fictional_name = "Region_" + std::to_string(r);
        region.nation_id = world.provinces[r].nation_id;
        region.province_ids.push_back(r);
        world.region_groups.push_back(std::move(region));
    }

    // -----------------------------------------------------------------------
    // §9.5.3 — Language Family Assignment
    // -----------------------------------------------------------------------
    // Two-pass: initial geographic assignment, then neighbor propagation.

    // Pass 1: each nation's seed province gets a language family by geographic affinity.
    for (auto& nation : world.nations) {
        if (nation.province_ids.empty())
            continue;
        // Use seed province (first province_id is the seed from Voronoi growth).
        uint32_t seed_pid = seed_province_ids[std::min(
            nation.id, static_cast<uint32_t>(seed_province_ids.size() - 1))];
        const auto& seed_prov = provinces[seed_pid];
        float abs_lat = std::fabs(seed_prov.geography.latitude);

        // Build affinity weights for each language family.
        float family_weights[v1_language_family_count];
        for (size_t fi = 0; fi < v1_language_family_count; ++fi) {
            family_weights[fi] = 1.0f;  // neutral default
        }
        // Germanic: mid-high latitude, wetter regions
        if (abs_lat > 45.0f && abs_lat < 65.0f)
            family_weights[0] = 1.5f;
        // Romance: mid latitude
        if (abs_lat > 25.0f && abs_lat < 50.0f)
            family_weights[1] = 1.4f;
        // Slavic: continental mid-high latitude
        if (abs_lat > 40.0f && abs_lat < 65.0f)
            family_weights[2] = 1.3f;
        // Sinitic: mid latitude East Asian analog
        if (abs_lat > 20.0f && abs_lat < 45.0f)
            family_weights[3] = 1.3f;
        // Arabic: low-mid latitude, arid zones
        if (abs_lat < 35.0f)
            family_weights[4] = 1.4f;
        // Turkic: continental steppe
        if (abs_lat > 30.0f && abs_lat < 55.0f)
            family_weights[5] = 1.2f;
        // Indic: tropical-subtropical
        if (abs_lat < 30.0f)
            family_weights[6] = 1.3f;
        // Bantu: tropical Africa analog
        if (abs_lat < 25.0f)
            family_weights[7] = 1.4f;
        // Austronesian: coastal tropical (island/maritime cultures)
        if (abs_lat < 20.0f && seed_prov.geography.coastal_length_km > 20.0f)
            family_weights[8] = 1.5f;
        // Quechuan: highland
        if (seed_prov.geography.elevation_avg_m > 2000.0f)
            family_weights[9] = 1.5f;

        float total = 0.0f;
        for (size_t fi = 0; fi < v1_language_family_count; ++fi)
            total += family_weights[fi];
        float roll = rng.next_float() * total;
        float cum = 0.0f;
        size_t chosen_fi = 0;
        for (size_t fi = 0; fi < v1_language_family_count; ++fi) {
            cum += family_weights[fi];
            if (cum >= roll) {
                chosen_fi = fi;
                break;
            }
        }
        nation.language_family_id = v1_language_families[chosen_fi];
    }

    // Pass 2: neighbor propagation — chance to inherit largest neighbor's language.
    // Process largest nations first (they set regional tone).
    std::vector<uint32_t> nation_order(nation_count);
    for (uint32_t ni = 0; ni < nation_count; ++ni)
        nation_order[ni] = ni;
    std::sort(nation_order.begin(), nation_order.end(), [&](uint32_t a, uint32_t b) {
        return world.nations[a].province_ids.size() > world.nations[b].province_ids.size();
    });

    // Build nation adjacency from province links.
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> nation_adj;
    for (uint32_t pi = 0; pi < prov_count; ++pi) {
        if (assignment[pi] < 0)
            continue;  // skip unclaimed
        for (const auto& link : provinces[pi].links) {
            auto it = h3_to_idx.find(link.neighbor_h3);
            if (it == h3_to_idx.end())
                continue;
            if (assignment[it->second] < 0)
                continue;  // skip unclaimed neighbor
            uint32_t ni_a = world.provinces[pi].nation_id;
            uint32_t ni_b = world.provinces[it->second].nation_id;
            if (ni_a != ni_b) {
                nation_adj[ni_a].insert(ni_b);
                nation_adj[ni_b].insert(ni_a);
            }
        }
    }

    for (uint32_t ni : nation_order) {
        auto adj_it = nation_adj.find(ni);
        if (adj_it == nation_adj.end() || adj_it->second.empty())
            continue;

        // Find largest neighbor.
        uint32_t largest_neighbor = *adj_it->second.begin();
        for (uint32_t nj : adj_it->second) {
            if (world.nations[nj].province_ids.size() >
                world.nations[largest_neighbor].province_ids.size()) {
                largest_neighbor = nj;
            }
        }

        if (rng.next_float() < nfp.language_propagation_chance) {
            world.nations[ni].language_family_id =
                world.nations[largest_neighbor].language_family_id;
        }

        // Secondary language: most common different-language neighbor.
        std::unordered_map<std::string, uint32_t> diff_lang_counts;
        for (uint32_t nj : adj_it->second) {
            if (world.nations[nj].language_family_id != world.nations[ni].language_family_id) {
                diff_lang_counts[world.nations[nj].language_family_id]++;
            }
        }
        if (!diff_lang_counts.empty()) {
            auto best =
                std::max_element(diff_lang_counts.begin(), diff_lang_counts.end(),
                                 [](const auto& a, const auto& b) { return a.second < b.second; });
            world.nations[ni].secondary_language_id = best->first;
        }
    }

    // -----------------------------------------------------------------------
    // §9.5.4 — Border Change Seeding
    // -----------------------------------------------------------------------
    // Build a notable resource threshold: 75th percentile of all deposit quantities.
    std::vector<float> all_quantities;
    for (const auto& prov : world.provinces) {
        for (const auto& dep : prov.deposits) {
            all_quantities.push_back(dep.quantity);
        }
    }
    float notable_threshold = 0.50f;
    if (!all_quantities.empty()) {
        std::sort(all_quantities.begin(), all_quantities.end());
        notable_threshold = all_quantities[all_quantities.size() * 3 / 4];
    }

    for (auto& prov : world.provinces) {
        float instability = 0.0f;

        // Border location: frontier provinces are contested.
        bool is_border_prov = false;
        for (const auto& link : prov.links) {
            auto it = h3_to_idx.find(link.neighbor_h3);
            if (it == h3_to_idx.end())
                continue;
            if (link.type == LinkType::Land || link.type == LinkType::River) {
                if (world.provinces[it->second].nation_id != prov.nation_id) {
                    is_border_prov = true;
                    break;
                }
            }
        }
        if (is_border_prov)
            instability += nfp.border_instability;

        // Strategic value: notable resources.
        for (const auto& dep : prov.deposits) {
            if (dep.quantity > notable_threshold) {
                instability += nfp.resource_instability;
                break;
            }
        }

        // High settlement attractiveness attracts conquest.
        if (prov.settlement_attractiveness > 0.70f)
            instability += nfp.attractiveness_instability;

        // Mountain passes and plateaus are contested.
        if (prov.is_mountain_pass)
            instability += nfp.chokepoint_instability;

        // Infra gap: predicted vs actual infrastructure.
        float predicted_infra = prov.settlement_attractiveness * 0.70f;
        prov.infra_gap = prov.infrastructure_rating - predicted_infra;

        // Colonial signature: infra_gap > 0.20 suggests external development.
        if (prov.infra_gap > 0.20f)
            instability += nfp.colonial_instability;

        // Poisson draw: instability → expected count → actual count.
        float expected = instability * nfp.instability_to_expected;
        int32_t count = 0;
        float remaining = expected;
        while (remaining > 0.0f && count < nfp.max_border_changes) {
            float u = rng.next_float();
            if (u < 0.001f)
                u = 0.001f;
            remaining += std::log(u);  // log(u) is negative
            if (remaining > 0.0f)
                ++count;
        }
        prov.border_change_count = std::max(0, std::min(count, nfp.max_border_changes));

        // Colonial development flag.
        prov.has_colonial_development_event =
            (prov.infra_gap > 0.20f && prov.border_change_count > 0 &&
             prov.infrastructure_rating > 0.50f);
    }

    // -----------------------------------------------------------------------
    // Aggregate nation-level fields
    // -----------------------------------------------------------------------
    for (auto& nation : world.nations) {
        if (nation.province_ids.empty())
            continue;

        float sum_attract = 0.0f;
        float sum_infra = 0.0f;
        for (uint32_t pid : nation.province_ids) {
            sum_attract += world.provinces[pid].settlement_attractiveness;
            sum_infra += world.provinces[pid].infrastructure_rating;
        }
        float n = static_cast<float>(nation.province_ids.size());
        nation.gdp_index = std::clamp(sum_attract / n, 0.0f, 1.0f);
        float mean_infra = sum_infra / n;
        nation.governance_quality =
            std::clamp(mean_infra + (rng.next_float() - 0.5f) * 0.20f, 0.0f, 1.0f);

        for (uint32_t pid : nation.province_ids) {
            if (world.provinces[pid].has_colonial_development_event) {
                nation.is_colonial_power = true;
                break;
            }
        }
    }
}

// ===========================================================================
// Stage 9.6 — Nomadic population (WorldGen v0.18 §9.6)
// ===========================================================================

void NationGenerator::seed_nomadic_population(WorldState& world, DeterministicRNG& rng,
                                              const WorldGeneratorConfig& config) {
    const float realisation = config.nation_formation.nomadic_realisation_factor;
    for (auto& prov : world.provinces) {
        float pastoral_cap = 0.0f;

        // Pastoral carrying capacity from Köppen zone.
        switch (prov.climate.koppen_zone) {
            case KoppenZone::BSk:
            case KoppenZone::BSh:
                pastoral_cap = 0.65f;  // steppe: classic pastoral zone
                break;
            case KoppenZone::BWh:
            case KoppenZone::BWk:
                pastoral_cap = 0.20f;  // desert: sparse; oases anchor movement
                break;
            case KoppenZone::Aw:
                pastoral_cap = 0.45f;  // savanna: seasonal transhumance
                break;
            case KoppenZone::ET:
                pastoral_cap = 0.30f;  // tundra: reindeer pastoralism
                break;
            case KoppenZone::Dfc:
            case KoppenZone::Dfd:
                pastoral_cap = 0.15f;  // taiga fringe; limited
                break;
            default:
                pastoral_cap = 0.0f;
                break;
        }

        // Reduce by terrain roughness (mountains break up grazing range).
        pastoral_cap *= (1.0f - prov.geography.terrain_roughness * 0.50f);

        // Reduce where agricultural productivity already supports dense settlement.
        pastoral_cap *= std::max(0.0f, 1.0f - prov.agricultural_productivity * 1.5f);

        prov.pastoral_carrying_capacity = std::clamp(pastoral_cap, 0.0f, 1.0f);

        // Nomadic fraction: highest where pastoral capacity is high and settled capacity low.
        if (pastoral_cap > 0.10f) {
            prov.nomadic_population_fraction = pastoral_cap * realisation;
        } else {
            prov.nomadic_population_fraction = 0.0f;
        }
    }
}

// ===========================================================================
// Stage 9.7 — Nation capital seeding (WorldGen v0.18 §9.7)
// ===========================================================================

void NationGenerator::seed_nation_capitals(WorldState& world, const WorldGeneratorConfig& config) {
    (void)config;  // available for future tuning
    for (auto& nation : world.nations) {
        if (nation.province_ids.empty())
            continue;

        // Filter eligible provinces: no continuous permafrost, elevation < 4000m,
        // not badlands/tundra/glacial terrain.
        std::vector<uint32_t> eligible;
        for (uint32_t pid : nation.province_ids) {
            const auto& p = world.provinces[pid];
            if (p.has_permafrost && p.climate.koppen_zone == KoppenZone::EF)
                continue;
            if (p.geography.elevation_avg_m >= 4000.0f)
                continue;
            if (p.has_badlands)
                continue;
            if (p.is_glacial_scoured && p.agricultural_productivity < 0.10f)
                continue;
            eligible.push_back(pid);
        }
        if (eligible.empty()) {
            eligible = nation.province_ids;  // fallback: no constraint
        }

        // Select highest settlement_attractiveness province.
        uint32_t capital_pid = eligible[0];
        float best_attract = world.provinces[capital_pid].settlement_attractiveness;
        for (uint32_t pid : eligible) {
            float a = world.provinces[pid].settlement_attractiveness;
            if (a > best_attract) {
                best_attract = a;
                capital_pid = pid;
            }
        }

        world.provinces[capital_pid].is_nation_capital = true;
        nation.capital_province_id = capital_pid;
    }
}

}  // namespace econlife
