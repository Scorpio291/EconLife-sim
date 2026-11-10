// Nation seed placement (WorldGen §9.5.1): separation is never relaxed, no seed is
// forced, and a shortfall is classified as the geography's or the selection's.

#include "core/world_gen/nation_seed_placement.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <limits>
#include <map>
#include <queue>

#include "core/world_gen/world_generator.h"

using namespace econlife;

namespace {

using Graph = std::vector<std::vector<uint32_t>>;

void link(Graph& g, uint32_t a, uint32_t b) {
    g[a].push_back(b);
    g[b].push_back(a);
}

Graph path_graph(uint32_t n) {
    Graph g(n);
    for (uint32_t i = 0; i + 1 < n; ++i)
        link(g, i, i + 1);
    return g;
}

NationSeedPlacementInput input_for(Graph g, uint32_t requested, uint32_t separation,
                                   std::vector<double> weights = {}) {
    NationSeedPlacementInput in;
    const uint32_t n = static_cast<uint32_t>(g.size());
    in.adjacency = std::move(g);
    for (uint32_t i = 0; i < n; ++i)
        in.candidates.push_back(i);
    in.weights = weights.empty() ? std::vector<double>(n, 1.0) : std::move(weights);
    in.requested = requested;
    in.separation_hops = separation;
    return in;
}

uint32_t hops(const Graph& g, uint32_t from, uint32_t to) {
    std::vector<uint32_t> d(g.size(), std::numeric_limits<uint32_t>::max());
    std::queue<uint32_t> q;
    d[from] = 0;
    q.push(from);
    while (!q.empty()) {
        uint32_t c = q.front();
        q.pop();
        for (uint32_t x : g[c])
            if (d[x] == std::numeric_limits<uint32_t>::max()) {
                d[x] = d[c] + 1;
                q.push(x);
            }
    }
    return d[to];
}

void require_separated(const Graph& g, const std::vector<uint32_t>& seeds, uint32_t sep) {
    for (size_t i = 0; i < seeds.size(); ++i)
        for (size_t j = i + 1; j < seeds.size(); ++j)
            REQUIRE(hops(g, seeds[i], seeds[j]) > sep);
}

}  // namespace

TEST_CASE("nation seeds: separation is never relaxed to meet the request",
          "[world_gen][nations][seed_placement]") {
    // Six cells in a row are at most five hops apart: two seeds fit at separation 3
    // (hops > 3), never three.
    const Graph g = path_graph(6);
    for (uint64_t seed = 1; seed <= 50; ++seed) {
        DeterministicRNG rng(seed);
        auto r = place_nation_seeds(input_for(g, 6, 3), rng);
        require_separated(g, r.seeds, 3);
        CHECK(r.report.max_feasible == 2);
        CHECK(r.report.max_feasible_exact);
        CHECK(r.report.placed == 2);
        CHECK(r.report.outcome == NationSeedOutcome::geography_limited);
    }
}

TEST_CASE("nation seeds: a compact window holds one seed, and says why",
          "[world_gen][nations][seed_placement]") {
    // A hex rosette (centre + six around it): diameter two hops.
    Graph g(7);
    for (uint32_t i = 1; i <= 6; ++i) {
        link(g, 0, i);
        link(g, i, i % 6 + 1);
    }
    DeterministicRNG rng(7);
    auto r = place_nation_seeds(input_for(g, 4, 3), rng);
    REQUIRE(r.seeds.size() == 1);
    CHECK(r.report.requested == 4);
    CHECK(r.report.max_feasible == 1);
    CHECK(r.report.outcome == NationSeedOutcome::geography_limited);
}

TEST_CASE("nation seeds: an unlucky order is repaired by exchange",
          "[world_gen][nations][seed_placement]") {
    // Star: the heavy centre is drawn first and blocks all three leaves. The
    // exchange swaps it for two leaves, and the fill adds the third.
    Graph g(4);
    link(g, 0, 1);
    link(g, 0, 2);
    link(g, 0, 3);
    DeterministicRNG rng(3);
    auto r = place_nation_seeds(input_for(g, 3, 1, {1e6, 1e-6, 1e-6, 1e-6}), rng);
    CHECK(r.report.placed == 3);
    CHECK(r.report.outcome == NationSeedOutcome::achieved);
    require_separated(g, r.seeds, 1);
}

TEST_CASE("nation seeds: an order that greedy cannot repair still reaches the maximum",
          "[world_gen][nations][seed_placement]") {
    // Path a-b-c-d-e with b and d drawn first. Greedy keeps {b, d}, and no
    // 1-for-2 exchange improves it, but {a, c, e} exists. The exact pass takes
    // the priority order as far as the maximum allows and reaches three.
    const Graph g = path_graph(5);
    DeterministicRNG rng(11);
    auto r = place_nation_seeds(input_for(g, 3, 1, {1e-6, 1e6, 1e-6, 1e6, 1e-6}), rng);
    CHECK(r.report.placed == 3);
    CHECK(r.report.max_feasible == 3);
    CHECK(r.report.outcome == NationSeedOutcome::achieved);
    require_separated(g, r.seeds, 1);
}

TEST_CASE("nation seeds: the priority order is kept wherever the maximum allows",
          "[world_gen][nations][seed_placement]") {
    // Path of 9, separation 1: the maximum is 5 (even cells). If cell 4 is drawn
    // first it can be kept (0,2,4,6,8); the exact pass must keep it rather than
    // pick an arbitrary maximum set.
    const Graph g = path_graph(9);
    std::vector<double> w(9, 1e-6);
    w[4] = 1e6;
    DeterministicRNG rng(13);
    auto r = place_nation_seeds(input_for(g, 5, 1, w), rng);
    REQUIRE(r.report.placed == 5);
    CHECK(r.seeds.front() == 4);
}

TEST_CASE("nation seeds: unlinked islands are separate geography",
          "[world_gen][nations][seed_placement]") {
    // Three islands with no links: no hop path joins them, so each can seed.
    Graph g(3);
    DeterministicRNG rng(5);
    auto r = place_nation_seeds(input_for(g, 3, 3), rng);
    CHECK(r.report.placed == 3);
    CHECK(r.report.outcome == NationSeedOutcome::achieved);
}

TEST_CASE("nation seeds: no candidates places nothing and draws nothing",
          "[world_gen][nations][seed_placement]") {
    NationSeedPlacementInput in;
    in.adjacency = path_graph(4);
    in.requested = 2;
    in.separation_hops = 3;
    DeterministicRNG rng(9);
    const uint64_t before = rng.state();
    auto r = place_nation_seeds(in, rng);
    CHECK(r.seeds.empty());
    CHECK(r.report.outcome == NationSeedOutcome::no_candidates);
    CHECK(rng.state() == before);
}

TEST_CASE("nation seeds: large graphs use a sound ball-cover bound",
          "[world_gen][nations][seed_placement]") {
    // 400-cell path (above the exact-search size), separation 3: exactly 100
    // separated seeds fit (every fourth cell). Radius-1 balls cover the path with
    // ceil(400/3) = 134, a valid but loose bound; the report must not claim more
    // than it proved.
    const Graph g = path_graph(400);
    DeterministicRNG rng(21);
    auto r = place_nation_seeds(input_for(g, 400, 3), rng);
    require_separated(g, r.seeds, 3);
    CHECK(r.report.placed <= 100);
    CHECK(r.report.placed >= 80);  // greedy on a path keeps at least 4/5 of the maximum here
    CHECK(r.report.max_feasible >= r.report.placed);
    CHECK(r.report.max_feasible <= 134);
    if (r.report.placed < r.report.max_feasible)
        CHECK(r.report.outcome == NationSeedOutcome::undetermined);
}

TEST_CASE("nation seeds: placement is deterministic and order-independent of nothing but seed",
          "[world_gen][nations][seed_placement]") {
    const Graph g = path_graph(40);
    DeterministicRNG a(77), b(77);
    auto ra = place_nation_seeds(input_for(g, 20, 3), a);
    auto rb = place_nation_seeds(input_for(g, 20, 3), b);
    CHECK(ra.seeds == rb.seeds);
    CHECK(a.state() == b.state());
}

TEST_CASE("nation seeds: generated worlds of 6, 40 and 100 provinces",
          "[world_gen][nations][seed_placement]") {
    // Observational sweep plus invariants. Nations equal seeds placed; the report
    // is consistent; nothing is forced. Prints the outcome distribution.
    for (uint32_t provinces : {6u, 40u, 100u}) {
        std::map<NationSeedOutcome, int> outcomes;
        uint32_t min_nations = std::numeric_limits<uint32_t>::max(), max_nations = 0;
        for (uint64_t seed = 1; seed <= 12; ++seed) {
            WorldGeneratorConfig config{};
            config.seed = seed;
            config.province_count = provinces;
            config.npc_count = 50;
            auto world = WorldGenerator::generate(config);
            const auto& rep = world.nation_seed_report;
            CHECK(world.nations.size() == rep.placed);
            CHECK(rep.placed >= 1);
            CHECK(rep.placed <= rep.max_feasible);
            CHECK(rep.max_feasible <= rep.candidate_count);
            CHECK(rep.separation_hops == config.nation_formation.seed_separation);
            CHECK(rep.outcome != NationSeedOutcome::no_candidates);
            // Every one of these is within exact-search size, so a shortfall is
            // always proven to be the geography's.
            CHECK(rep.max_feasible_exact);
            CHECK(rep.outcome != NationSeedOutcome::undetermined);
            if (provinces == 6) {
                // Six adjacent res-4 cells are at most two hops across.
                CHECK(rep.placed == 1);
                CHECK(rep.outcome == NationSeedOutcome::geography_limited);
            }
            outcomes[rep.outcome]++;
            min_nations = std::min<uint32_t>(min_nations, rep.placed);
            max_nations = std::max<uint32_t>(max_nations, rep.placed);
        }
        std::printf(
            "[seed_placement] %u provinces x 12 worlds: nations %u..%u; achieved %d, "
            "geography_limited %d, undetermined %d\n",
            provinces, min_nations, max_nations, outcomes[NationSeedOutcome::achieved],
            outcomes[NationSeedOutcome::geography_limited],
            outcomes[NationSeedOutcome::undetermined]);
    }
}

TEST_CASE("nation seeds: a dense adversarial graph falls back honestly",
          "[world_gen][nations][seed_placement]") {
    // Triangulated grids are the hard case for the exact search (many equal
    // maxima). 12x12 is solved exactly; 16x16 at separation 1 spends the node
    // budget, falls back to greedy + exchange, and must say it cannot prove the
    // shortfall rather than claim one.
    auto tri_grid = [](uint32_t side) {
        Graph g(side * side);
        for (uint32_t y = 0; y < side; ++y)
            for (uint32_t x = 0; x < side; ++x) {
                if (x + 1 < side)
                    link(g, y * side + x, y * side + x + 1);
                if (y + 1 < side)
                    link(g, y * side + x, (y + 1) * side + x);
                if (x + 1 < side && y + 1 < side)
                    link(g, y * side + x, (y + 1) * side + x + 1);
            }
        return g;
    };
    {
        const Graph g = tri_grid(12);
        DeterministicRNG rng(1);
        auto r = place_nation_seeds(input_for(g, 1000, 3), rng);
        require_separated(g, r.seeds, 3);
        CHECK(r.report.max_feasible_exact);
        CHECK(r.report.placed == r.report.max_feasible);
        CHECK(r.report.outcome == NationSeedOutcome::geography_limited);
    }
    {
        const Graph g = tri_grid(16);
        DeterministicRNG rng(1);
        auto r = place_nation_seeds(input_for(g, 1000, 1), rng);
        require_separated(g, r.seeds, 1);
        CHECK_FALSE(r.report.max_feasible_exact);
        CHECK(r.report.max_feasible > r.report.placed);
        CHECK(r.report.max_feasible < 256);  // the clique bound, not the trivial one
        CHECK(r.report.outcome == NationSeedOutcome::undetermined);
    }
}
