// Geography from the H3 cell (B1), checked against references rather than only
// against itself (Review R1 F-06/F-07/F-08):
//   - known H3 coordinate cases, pentagons, the poles and the antimeridian;
//   - the terrain mechanisms on constructed provinces, not averaged worlds;
//   - geographic fields survive save/load bit-exactly and are deterministic.

#include <h3api.h>

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstring>
#include <numbers>
#include <set>

#include "core/world_gen/h3_utils.h"
#include "core/world_gen/world_generator.h"
#include "modules/persistence/persistence_module.h"

using namespace econlife;

namespace {

constexpr double kEarthRadiusKm = 6371.0088;  // IUGG mean radius, as H3 uses

double great_circle_km(double lat1, double lng1, double lat2, double lng2) {
    const double r = std::numbers::pi / 180.0;
    const double dlat = (lat2 - lat1) * r;
    const double dlng = (lng2 - lng1) * r;
    const double a =
        std::sin(dlat / 2) * std::sin(dlat / 2) +
        std::cos(lat1 * r) * std::cos(lat2 * r) * std::sin(dlng / 2) * std::sin(dlng / 2);
    return 2.0 * kEarthRadiusKm * std::asin(std::sqrt(std::min(1.0, a)));
}

// Radius of a disc with the cell's area: a point inside the cell is within
// about this far of its centre (a hexagon's corner is 1.10x this away).
double cell_radius_km(H3Index cell) {
    return std::sqrt(h3_utils::cell_area_km2(cell) / std::numbers::pi);
}

void check_centre_is_valid(H3Index cell) {
    const auto c = h3_utils::cell_center_lat_lng(cell);
    REQUIRE(std::isfinite(c.lat));
    REQUIRE(std::isfinite(c.lng));
    CHECK(c.lat >= -90.0);
    CHECK(c.lat <= 90.0);
    CHECK(c.lng >= -180.0);
    CHECK(c.lng <= 180.0);
    // The centre lies in its own cell.
    CHECK(h3_utils::lat_lng_to_cell(c.lat, c.lng, getResolution(cell)) == cell);
}

bool bit_equal(float a, float b) {
    return std::memcmp(&a, &b, sizeof(float)) == 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// F-07: H3 reference cases
// ---------------------------------------------------------------------------

TEST_CASE("geography: a published H3 index and its centre", "[world_gen][b1][h3]") {
    // The H3 project's own example: the Statue of Liberty at resolution 10 is
    // 8a2a1072b59ffff. A res-10 cell is ~0.015 km^2, so the centre lies within
    // ~0.1 km of the point.
    const H3Index cell = h3_utils::lat_lng_to_cell(40.689167, -74.044444, 10);
    CHECK(cell == 0x8a2a1072b59ffffULL);
    const auto c = h3_utils::cell_center_lat_lng(cell);
    CHECK(great_circle_km(c.lat, c.lng, 40.689167, -74.044444) < 0.1);
}

TEST_CASE("geography: the twelve pentagons have valid centres, areas and neighbours",
          "[world_gen][b1][h3]") {
    H3Index pentagons[12] = {};
    REQUIRE(getPentagons(4, pentagons) == E_SUCCESS);
    std::set<H3Index> distinct(std::begin(pentagons), std::end(pentagons));
    REQUIRE(distinct.size() == 12);
    for (H3Index p : pentagons) {
        INFO("pentagon " << std::hex << p);
        REQUIRE(h3_utils::is_pentagon(p));
        check_centre_is_valid(p);
        // Five neighbours, each a valid cell that is NOT a pentagon at res 4.
        const auto nbrs = h3_utils::grid_neighbors(p);
        CHECK(nbrs.size() == 5);
        double hex_area_sum = 0.0;
        for (H3Index n : nbrs) {
            CHECK_FALSE(h3_utils::is_pentagon(n));
            check_centre_is_valid(n);
            hex_area_sum += h3_utils::cell_area_km2(n);
        }
        // A pentagon covers about 5/6 of a neighbouring hexagon's area.
        const double ratio = h3_utils::cell_area_km2(p) / (hex_area_sum / 5.0);
        CHECK(ratio > 0.70);
        CHECK(ratio < 0.95);
        // A province window grown from a pentagon is contiguous and distinct.
        const auto group = h3_utils::grid_compact_group(p, 6);
        CHECK(std::set<H3Index>(group.begin(), group.end()).size() == 6);
    }
}

TEST_CASE("geography: the cells at the poles", "[world_gen][b1][h3]") {
    for (double pole : {90.0, -90.0}) {
        INFO("pole " << pole);
        // At the pole every longitude names the same point, so the same cell.
        const H3Index cell = h3_utils::lat_lng_to_cell(pole, 0.0, 4);
        for (double lng : {-180.0, -90.0, 45.0, 179.0})
            CHECK(h3_utils::lat_lng_to_cell(pole, lng, 4) == cell);
        check_centre_is_valid(cell);
        const auto c = h3_utils::cell_center_lat_lng(cell);
        // The pole lies inside the cell, so the centre is within a cell radius of it.
        CHECK(great_circle_km(c.lat, c.lng, pole, 0.0) < 1.2 * cell_radius_km(cell));
        CHECK(std::abs(c.lat) > 89.0);
        // Its neighbours ring the pole: every one is further from it than the centre.
        for (H3Index n : h3_utils::grid_neighbors(cell)) {
            const auto nc = h3_utils::cell_center_lat_lng(n);
            CHECK(std::abs(nc.lat) < std::abs(c.lat) + 1e-9);
        }
    }
}

TEST_CASE("geography: cells across the antimeridian", "[world_gen][b1][h3]") {
    // Two points 0.1 degrees apart across longitude 180 at several latitudes.
    for (double lat : {0.0, 35.0, -52.0, 71.0}) {
        INFO("latitude " << lat);
        const H3Index east = h3_utils::lat_lng_to_cell(lat, 179.95, 4);
        const H3Index west = h3_utils::lat_lng_to_cell(lat, -179.95, 4);
        check_centre_is_valid(east);
        check_centre_is_valid(west);
        // Each point is within its cell (a cell radius of its centre), measured as a
        // great circle, which is indifferent to the sign flip at 180.
        const auto ce = h3_utils::cell_center_lat_lng(east);
        const auto cw = h3_utils::cell_center_lat_lng(west);
        CHECK(great_circle_km(ce.lat, ce.lng, lat, 179.95) < 1.2 * cell_radius_km(east));
        CHECK(great_circle_km(cw.lat, cw.lng, lat, -179.95) < 1.2 * cell_radius_km(west));
        // The two cells are the same or adjacent: the date line is not a seam.
        int64_t d = -1;
        REQUIRE(gridDistance(east, west, &d) == E_SUCCESS);
        CHECK(d <= 1);
        // A window grown there is contiguous, and each neighbour pair is close as
        // a great circle even where the longitudes have opposite signs.
        const auto group = h3_utils::grid_compact_group(east, 6);
        CHECK(group.size() == 6);
        for (H3Index a : group)
            for (H3Index b : h3_utils::grid_neighbors(a)) {
                const auto pa = h3_utils::cell_center_lat_lng(a);
                const auto pb = h3_utils::cell_center_lat_lng(b);
                CHECK(great_circle_km(pa.lat, pa.lng, pb.lat, pb.lng) < 2.5 * cell_radius_km(a));
            }
    }
}

// ---------------------------------------------------------------------------
// F-08: terrain mechanisms on constructed provinces
// ---------------------------------------------------------------------------

namespace {

WorldState constructed_world(uint32_t n) {
    WorldState w{};
    w.provinces.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        w.provinces[i].id = i;
        w.provinces[i].h3_index = 0x8400000000000000ULL + i;  // distinct keys only
        w.h3_province_map[w.provinces[i].h3_index] = i;
    }
    return w;
}

void land_link(WorldState& w, uint32_t a, uint32_t b, float cost) {
    ProvinceLink la{};
    la.neighbor_h3 = w.provinces[b].h3_index;
    la.type = LinkType::Land;
    la.transit_terrain_cost = cost;
    w.provinces[a].links.push_back(la);
    ProvinceLink lb = la;
    lb.neighbor_h3 = w.provinces[a].h3_index;
    w.provinces[b].links.push_back(lb);
}

}  // namespace

TEST_CASE("terrain: elevation scales with roughness, exactly as the mechanism states",
          "[world_gen][b1][terrain]") {
    WorldGeneratorConfig cfg{};
    const auto& t = cfg.terrain;
    WorldState w = constructed_world(4);
    const float roughness[] = {0.0f, 0.5f, 1.0f, 0.0f};
    const float elevation[] = {1000.0f, 1000.0f, 1000.0f, 1.0f};
    for (uint32_t i = 0; i < 4; ++i) {
        w.provinces[i].geography.terrain_roughness = roughness[i];
        w.provinces[i].geography.elevation_avg_m = elevation[i];
    }
    WorldGenerator::refine_province_geography(w, cfg);
    for (uint32_t i = 0; i < 3; ++i) {
        const float expected =
            elevation[i] * (t.elev_roughness_base + roughness[i] * t.elev_roughness_range);
        CHECK(w.provinces[i].geography.elevation_avg_m == expected);
    }
    // Rougher is higher, at equal starting elevation.
    CHECK(w.provinces[0].geography.elevation_avg_m < w.provinces[1].geography.elevation_avg_m);
    CHECK(w.provinces[1].geography.elevation_avg_m < w.provinces[2].geography.elevation_avg_m);
    // A low, flat province stops at the floor.
    CHECK(w.provinces[3].geography.elevation_avg_m == t.elev_floor_m);
}

TEST_CASE("terrain: a high rough province between lowlands is a pass, and only then",
          "[world_gen][b1][terrain]") {
    WorldGeneratorConfig cfg{};
    const auto& t = cfg.terrain;
    // 0 is the ridge; 1 and 2 are lowlands; 3 is a high neighbour; 4 is an island.
    WorldState w = constructed_world(5);
    auto& g = w.provinces;
    g[0].geography.terrain_roughness = 0.9f;
    g[0].geography.elevation_avg_m = 2000.0f;
    g[1].geography.elevation_avg_m = 400.0f;
    g[2].geography.elevation_avg_m = 600.0f;
    g[3].geography.elevation_avg_m = 1800.0f;
    land_link(w, 0, 1, 0.5f);
    land_link(w, 0, 2, 0.5f);
    land_link(w, 0, 3, 0.5f);
    ProvinceLink sea{};
    sea.neighbor_h3 = g[1].h3_index;
    sea.type = LinkType::Maritime;
    g[4].links.push_back(sea);

    WorldGenerator::detect_terrain_flags(w, cfg);

    REQUIRE(g[0].is_mountain_pass);
    for (const auto& link : g[0].links) {
        const uint32_t nb = w.h3_province_map.at(link.neighbor_h3);
        const bool low = g[nb].geography.elevation_avg_m <
                         g[0].geography.elevation_avg_m * t.pass_low_neighbor_fraction;
        const float expected =
            low ? std::max(t.pass_transit_cost_floor, 0.5f * t.pass_transit_cost_factor) : 0.5f;
        CHECK(link.transit_terrain_cost == expected);
    }
    CHECK_FALSE(g[1].is_mountain_pass);  // low
    CHECK_FALSE(g[3].is_mountain_pass);  // high but smooth
    CHECK(g[4].island_isolation);
    CHECK_FALSE(g[1].island_isolation);

    // With only one low neighbour the ridge is not a pass.
    WorldState w2 = constructed_world(3);
    w2.provinces[0].geography.terrain_roughness = 0.9f;
    w2.provinces[0].geography.elevation_avg_m = 2000.0f;
    w2.provinces[1].geography.elevation_avg_m = 400.0f;
    w2.provinces[2].geography.elevation_avg_m = 1900.0f;
    land_link(w2, 0, 1, 0.5f);
    land_link(w2, 0, 2, 0.5f);
    WorldGenerator::detect_terrain_flags(w2, cfg);
    CHECK_FALSE(w2.provinces[0].is_mountain_pass);
}

// ---------------------------------------------------------------------------
// F-08: persistence and determinism of geographic data
// ---------------------------------------------------------------------------

TEST_CASE("geography: cell, position and area survive save and load bit-exactly",
          "[world_gen][b1][persistence]") {
    for (uint64_t seed : {5ULL, 77ULL, 1234ULL}) {
        INFO("seed " << seed);
        WorldGeneratorConfig cfg{};
        cfg.seed = seed;
        cfg.province_count = 6;
        cfg.npc_count = 50;
        const WorldState world = WorldGenerator::generate(cfg);
        const auto bytes = PersistenceModule::serialize(world);
        WorldState restored{};
        REQUIRE(PersistenceModule::deserialize(bytes, restored) == RestoreResult::success);
        REQUIRE(restored.provinces.size() == world.provinces.size());
        for (size_t i = 0; i < world.provinces.size(); ++i) {
            const auto& a = world.provinces[i];
            const auto& b = restored.provinces[i];
            CHECK(a.h3_index == b.h3_index);
            CHECK(bit_equal(a.geography.latitude, b.geography.latitude));
            CHECK(bit_equal(a.geography.longitude, b.geography.longitude));
            CHECK(bit_equal(a.geography.area_km2, b.geography.area_km2));
            CHECK(bit_equal(a.geography.elevation_avg_m, b.geography.elevation_avg_m));
            CHECK(bit_equal(a.climate.temperature_avg_c, b.climate.temperature_avg_c));
            // And the loaded position is still the loaded cell's centre: the cache
            // cannot drift from the authority across a save.
            const auto c = h3_utils::cell_center_lat_lng(b.h3_index);
            CHECK(bit_equal(b.geography.latitude, static_cast<float>(c.lat)));
            CHECK(bit_equal(b.geography.longitude, static_cast<float>(c.lng)));
        }
    }
}

TEST_CASE("geography: the same seed places the same cells, a different seed elsewhere",
          "[world_gen][b1][determinism]") {
    auto cells = [](uint64_t seed) {
        WorldGeneratorConfig cfg{};
        cfg.seed = seed;
        cfg.province_count = 6;
        cfg.npc_count = 50;
        const WorldState w = WorldGenerator::generate(cfg);
        std::vector<std::tuple<H3Index, float, float, float>> out;
        for (const auto& p : w.provinces)
            out.emplace_back(p.h3_index, p.geography.latitude, p.geography.longitude,
                             p.geography.area_km2);
        return out;
    };
    const auto a = cells(31);
    const auto b = cells(31);
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        CHECK(std::get<0>(a[i]) == std::get<0>(b[i]));
        CHECK(bit_equal(std::get<1>(a[i]), std::get<1>(b[i])));
        CHECK(bit_equal(std::get<2>(a[i]), std::get<2>(b[i])));
        CHECK(bit_equal(std::get<3>(a[i]), std::get<3>(b[i])));
    }
    CHECK(std::get<0>(cells(32)[0]) != std::get<0>(a[0]));
}
