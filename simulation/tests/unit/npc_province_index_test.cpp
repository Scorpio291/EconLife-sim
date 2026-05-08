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

TEST_CASE("rebuild_npc_indices: builds npc_index_by_id", "[world_state][npc_index]") {
    auto w = make_world(/*province_count=*/2, /*npc_count=*/4);
    rebuild_npc_indices(w);

    REQUIRE(w.npc_index_by_id.size() == 4);
    for (size_t i = 0; i < w.significant_npcs.size(); ++i) {
        auto it = w.npc_index_by_id.find(w.significant_npcs[i].id);
        REQUIRE(it != w.npc_index_by_id.end());
        REQUIRE(it->second == i);
    }
}

TEST_CASE("rebuild_npc_indices: id index covers out-of-range province NPCs",
          "[world_state][npc_index]") {
    // The province bucket skips NPCs whose current_province_id is out of
    // range; the id index must still reach them so callers that only need
    // "find NPC by id" don't lose entries.
    auto w = make_world(/*province_count=*/2, /*npc_count=*/2);
    NPC orphan{};
    orphan.id = 9999;
    orphan.current_province_id = 42;  // out of range
    w.significant_npcs.push_back(orphan);

    rebuild_npc_indices(w);

    REQUIRE(w.npc_index_by_id.count(9999) == 1);
    REQUIRE(w.npc_index_by_id[9999] == w.significant_npcs.size() - 1);
}

TEST_CASE("apply_deltas refreshes npc_index_by_id", "[world_state][npc_index][apply_deltas]") {
    auto w = make_world(/*province_count=*/2, /*npc_count=*/2);
    rebuild_npc_indices(w);
    w.npc_index_by_id.clear();

    DeltaBuffer delta{};
    NPCDelta nd{};
    nd.npc_id = w.significant_npcs[0].id;
    nd.capital_delta = 1.0f;
    delta.npc_deltas.push_back(nd);

    apply_deltas(w, delta);

    REQUIRE(w.npc_index_by_id.size() == 2);
    for (size_t i = 0; i < w.significant_npcs.size(); ++i) {
        REQUIRE(w.npc_index_by_id[w.significant_npcs[i].id] == i);
    }
}

TEST_CASE("rebuild_npc_indices: builds home_province bucket independently of current_province",
          "[world_state][npc_index]") {
    // NPC 0 lives in province 0 but is currently in province 1 (e.g. mid-trip).
    // The two buckets must reflect the two semantics.
    auto w = make_world(/*province_count=*/2, /*npc_count=*/0);
    NPC npc{};
    npc.id = 100;
    npc.home_province_id = 0;
    npc.current_province_id = 1;
    npc.status = NPCStatus::active;
    w.significant_npcs.push_back(npc);

    rebuild_npc_indices(w);

    // current_province bucket: under province 1.
    REQUIRE(w.npc_indices_by_province[0].empty());
    REQUIRE(w.npc_indices_by_province[1] == std::vector<uint32_t>{0});

    // home_province bucket: under province 0.
    REQUIRE(w.npc_indices_by_home_province[0] == std::vector<uint32_t>{0});
    REQUIRE(w.npc_indices_by_home_province[1].empty());
}

TEST_CASE("lookup_npc_by_id: prefers index, falls back when index is empty",
          "[world_state][npc_index]") {
    auto w = make_world(/*province_count=*/1, /*npc_count=*/3);

    // No rebuild yet: id index is empty, fallback path triggers.
    REQUIRE(w.npc_index_by_id.empty());
    const NPC* npc = lookup_npc_by_id(w, w.significant_npcs[1].id);
    REQUIRE(npc != nullptr);
    REQUIRE(npc->id == w.significant_npcs[1].id);

    // Now rebuild and assert the index is consulted (we mutate the index
    // out-of-band to a wrong value and the fallback must NOT kick in — once
    // the index is populated, absence is real).
    rebuild_npc_indices(w);
    w.npc_index_by_id.erase(w.significant_npcs[1].id);
    // Index is non-empty (still has the other entries) so absence is real.
    REQUIRE(lookup_npc_by_id(w, w.significant_npcs[1].id) == nullptr);
}

// --- Market indices ---------------------------------------------------------

namespace {

WorldState make_market_world(uint32_t province_count, uint32_t goods_per_province) {
    WorldState w{};
    w.current_tick = 0;
    w.world_seed = 1;
    w.game_mode = GameMode::standard;

    for (uint32_t p = 0; p < province_count; ++p) {
        Province prov{};
        prov.id = p;
        w.provinces.push_back(prov);
    }
    for (uint32_t p = 0; p < province_count; ++p) {
        for (uint32_t g = 0; g < goods_per_province; ++g) {
            RegionalMarket m{};
            m.good_id = g;
            m.province_id = p;
            m.spot_price = 10.0f + static_cast<float>(g);
            m.equilibrium_price = m.spot_price;
            m.supply = 100.0f;
            w.regional_markets.push_back(m);
        }
    }
    return w;
}

}  // namespace

TEST_CASE("rebuild_npc_indices: builds market_indices_by_province", "[world_state][market_index]") {
    auto w = make_market_world(/*province_count=*/3, /*goods_per_province=*/4);
    rebuild_npc_indices(w);

    REQUIRE(w.market_indices_by_province.size() == w.provinces.size());
    for (uint32_t p = 0; p < w.provinces.size(); ++p) {
        REQUIRE(w.market_indices_by_province[p].size() == 4);
        for (uint32_t i : w.market_indices_by_province[p]) {
            REQUIRE(w.regional_markets[i].province_id == p);
        }
    }
}

TEST_CASE("rebuild_npc_indices: builds market_index_by_good_province",
          "[world_state][market_index]") {
    auto w = make_market_world(/*province_count=*/2, /*goods_per_province=*/3);
    rebuild_npc_indices(w);

    REQUIRE(w.market_index_by_good_province.size() == w.regional_markets.size());
    for (size_t i = 0; i < w.regional_markets.size(); ++i) {
        const auto& m = w.regional_markets[i];
        const uint64_t key = (static_cast<uint64_t>(m.good_id) << 32) | m.province_id;
        auto it = w.market_index_by_good_province.find(key);
        REQUIRE(it != w.market_index_by_good_province.end());
        REQUIRE(it->second == i);
    }
}

TEST_CASE("lookup_market: prefers index, falls back when empty", "[world_state][market_index]") {
    auto w = make_market_world(/*province_count=*/2, /*goods_per_province=*/2);

    // Pre-rebuild: index is empty, fallback triggers.
    REQUIRE(w.market_index_by_good_province.empty());
    const RegionalMarket* m = lookup_market(w, /*good=*/1, /*province=*/1);
    REQUIRE(m != nullptr);
    REQUIRE(m->good_id == 1);
    REQUIRE(m->province_id == 1);

    // Post-rebuild: a missing key returns nullptr (no fallback).
    rebuild_npc_indices(w);
    REQUIRE(lookup_market(w, /*good=*/99, /*province=*/0) == nullptr);
}

TEST_CASE("markets_in_province: prefers bucket, falls back when empty",
          "[world_state][market_index]") {
    auto w = make_market_world(/*province_count=*/2, /*goods_per_province=*/3);

    // Pre-rebuild: bucket is empty, fallback materialises a fresh vector.
    REQUIRE(w.market_indices_by_province.empty());
    auto fallback = markets_in_province(w, /*province=*/1);
    REQUIRE(fallback.size() == 3);
    for (uint32_t i : fallback) {
        REQUIRE(w.regional_markets[i].province_id == 1);
    }

    rebuild_npc_indices(w);
    auto via_index = markets_in_province(w, /*province=*/1);
    REQUIRE(via_index.size() == 3);
    REQUIRE(via_index == w.market_indices_by_province[1]);

    // Province with no markets (out-of-range) returns empty.
    REQUIRE(markets_in_province(w, /*province=*/42).empty());
}

TEST_CASE("apply_deltas refreshes market indices after a market_deltas pass",
          "[world_state][market_index][apply_deltas]") {
    auto w = make_market_world(/*province_count=*/2, /*goods_per_province=*/2);
    rebuild_npc_indices(w);
    w.market_index_by_good_province.clear();
    w.market_indices_by_province.clear();

    DeltaBuffer delta{};
    MarketDelta md{};
    md.good_id = 0;
    md.region_id = 0;
    md.supply_delta = 5.0f;
    delta.market_deltas.push_back(md);

    apply_deltas(w, delta);

    REQUIRE(w.market_index_by_good_province.size() == w.regional_markets.size());
    REQUIRE(w.market_indices_by_province.size() == w.provinces.size());
}
