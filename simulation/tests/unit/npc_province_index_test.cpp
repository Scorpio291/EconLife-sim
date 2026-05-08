// Coverage tests for WorldState::npc_indices_by_province and the
// rebuild_npc_indices() helper. The bucket index is the substrate for
// migrating filter-shape "for npc : significant_npcs" scans (audit H5).

#include <algorithm>
#include <catch2/catch_test_macros.hpp>

#include "core/world_state/apply_deltas.h"
#include "core/world_state/world_state.h"

using namespace econlife;

namespace {

// Build a WorldState with `province_count` provinces and NPCs round-robin
// assigned to provinces by id ascending. Avoids the test_world_factory
// dependency so failures in this test don't get masked by factory drift.
WorldState make_world(uint32_t province_count, uint32_t npc_count) {
    WorldState w{};
    w.current_tick = 0;
    w.world_seed = 1;
    w.game_mode = GameMode::standard;

    for (uint32_t p = 0; p < province_count; ++p) {
        Province prov{};
        prov.id = p;
        w.provinces.push_back(prov);
    }
    for (uint32_t i = 0; i < npc_count; ++i) {
        NPC npc{};
        npc.id = 100 + i;
        npc.current_province_id = i % province_count;
        npc.status = NPCStatus::active;
        w.significant_npcs.push_back(npc);
    }
    return w;
}

// Brute-force ground truth: for each province, the indices of significant_npcs
// whose current_province_id matches, in vector order.
std::vector<std::vector<uint32_t>> brute_force(const WorldState& w) {
    std::vector<std::vector<uint32_t>> out(w.provinces.size());
    for (size_t i = 0; i < w.significant_npcs.size(); ++i) {
        uint32_t p = w.significant_npcs[i].current_province_id;
        if (p < w.provinces.size()) {
            out[p].push_back(static_cast<uint32_t>(i));
        }
    }
    return out;
}

}  // namespace

TEST_CASE("rebuild_npc_indices: matches brute force on a fresh world", "[world_state][npc_index]") {
    auto w = make_world(/*province_count=*/4, /*npc_count=*/40);
    rebuild_npc_indices(w);

    REQUIRE(w.npc_indices_by_province.size() == w.provinces.size());
    REQUIRE(w.npc_indices_by_province == brute_force(w));

    // Each bucket should hold exactly the round-robin share.
    for (size_t p = 0; p < w.provinces.size(); ++p) {
        REQUIRE(w.npc_indices_by_province[p].size() == 10);
    }
}

TEST_CASE("rebuild_npc_indices: empty NPC list yields empty buckets", "[world_state][npc_index]") {
    auto w = make_world(/*province_count=*/3, /*npc_count=*/0);
    rebuild_npc_indices(w);

    REQUIRE(w.npc_indices_by_province.size() == 3);
    for (const auto& bucket : w.npc_indices_by_province) {
        REQUIRE(bucket.empty());
    }
}

TEST_CASE("rebuild_npc_indices: zero provinces yields empty index", "[world_state][npc_index]") {
    WorldState w{};
    NPC orphan{};
    orphan.id = 7;
    orphan.current_province_id = 5;  // dangling — no provinces exist
    w.significant_npcs.push_back(orphan);

    rebuild_npc_indices(w);
    REQUIRE(w.npc_indices_by_province.empty());
}

TEST_CASE("rebuild_npc_indices: skips NPCs with out-of-range province ids",
          "[world_state][npc_index]") {
    auto w = make_world(/*province_count=*/2, /*npc_count=*/4);
    // Push one NPC with a bogus province_id.
    NPC bogus{};
    bogus.id = 999;
    bogus.current_province_id = 42;
    w.significant_npcs.push_back(bogus);

    rebuild_npc_indices(w);

    size_t total = 0;
    for (const auto& bucket : w.npc_indices_by_province) {
        total += bucket.size();
    }
    REQUIRE(total == 4);  // bogus is excluded
}

TEST_CASE("rebuild_npc_indices: bucket order matches significant_npcs vector order",
          "[world_state][npc_index]") {
    auto w = make_world(/*province_count=*/2, /*npc_count=*/8);
    rebuild_npc_indices(w);

    // Province 0 collects indices 0,2,4,6; province 1 collects 1,3,5,7.
    REQUIRE(w.npc_indices_by_province[0] == std::vector<uint32_t>{0, 2, 4, 6});
    REQUIRE(w.npc_indices_by_province[1] == std::vector<uint32_t>{1, 3, 5, 7});
}

TEST_CASE("rebuild_npc_indices: rebuild is idempotent", "[world_state][npc_index]") {
    auto w = make_world(/*province_count=*/3, /*npc_count=*/15);

    rebuild_npc_indices(w);
    auto first = w.npc_indices_by_province;

    rebuild_npc_indices(w);
    REQUIRE(w.npc_indices_by_province == first);

    rebuild_npc_indices(w);
    REQUIRE(w.npc_indices_by_province == first);
}

TEST_CASE("apply_deltas refreshes npc_indices_by_province",
          "[world_state][npc_index][apply_deltas]") {
    auto w = make_world(/*province_count=*/3, /*npc_count=*/6);
    rebuild_npc_indices(w);

    // Pre-state sanity: round-robin → 2 NPCs each.
    for (const auto& bucket : w.npc_indices_by_province) {
        REQUIRE(bucket.size() == 2);
    }

    // Mutate the index out-of-band to prove apply_deltas refreshes it.
    w.npc_indices_by_province.clear();

    DeltaBuffer delta{};
    NPCDelta nd{};
    nd.npc_id = w.significant_npcs[0].id;
    nd.capital_delta = 1.0f;
    delta.npc_deltas.push_back(nd);

    apply_deltas(w, delta);

    REQUIRE(w.npc_indices_by_province == brute_force(w));
}
