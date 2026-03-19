#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "modules/npc_spending/npc_spending_module.h"
#include "core/world_state/world_state.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"

#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace econlife;

// ---------------------------------------------------------------------------
// Static utility tests
// ---------------------------------------------------------------------------

TEST_CASE("test_compute_income_factor_basic", "[npc_spending][tier6]") {
    // capital == reference_income -> income_factor == 1.0
    float f = NpcSpendingModule::compute_income_factor(1000.0f, 1000.0f, 1.0f, 5.0f);
    REQUIRE_THAT(f, WithinAbs(1.0f, 0.001f));

    // capital == 3x reference -> income_factor == 3.0 for elasticity 1.0
    float f2 = NpcSpendingModule::compute_income_factor(3000.0f, 1000.0f, 1.0f, 5.0f);
    REQUIRE_THAT(f2, WithinAbs(3.0f, 0.001f));
}

TEST_CASE("test_compute_income_factor_zero_capital", "[npc_spending][tier6]") {
    float f = NpcSpendingModule::compute_income_factor(0.0f, 1000.0f, 1.0f, 5.0f);
    REQUIRE_THAT(f, WithinAbs(0.0f, 0.001f));
}

TEST_CASE("test_income_factor_clamped_at_max", "[npc_spending][tier6]") {
    // capital = 100x reference -> would be 100.0, but clamped at 5.0
    float f = NpcSpendingModule::compute_income_factor(100000.0f, 1000.0f, 1.0f, 5.0f);
    REQUIRE_THAT(f, WithinAbs(5.0f, 0.001f));
}

TEST_CASE("test_compute_price_factor_basic", "[npc_spending][tier6]") {
    // spot_price == base_price -> price_factor == 1.0 for any buyer type
    float f = NpcSpendingModule::compute_price_factor(10.0f, 10.0f, -1.0f,
                                                       BuyerType::brand_loyal, 0.05f);
    REQUIRE_THAT(f, WithinAbs(1.0f, 0.001f));
}

TEST_CASE("test_price_factor_floored_at_min", "[npc_spending][tier6]") {
    // spot_price = 10000x base -> extremely high price, factor drops toward 0
    // but must be >= min_price_factor (0.05)
    float f = NpcSpendingModule::compute_price_factor(10.0f, 100000.0f, -1.0f,
                                                       BuyerType::price_sensitive, 0.05f);
    REQUIRE(f >= 0.05f);
}

TEST_CASE("test_compute_quality_factor_basic", "[npc_spending][tier6]") {
    // quality_seeker: quality_weight = 0.6, batch > market -> factor > 1.0
    float f = NpcSpendingModule::compute_quality_factor(0.8f, 0.5f, BuyerType::quality_seeker);
    // 1.0 + 0.6 * (0.8 - 0.5) = 1.0 + 0.18 = 1.18
    REQUIRE_THAT(f, WithinAbs(1.18f, 0.01f));
}

TEST_CASE("test_compute_quality_factor_price_sensitive_ignores_quality", "[npc_spending][tier6]") {
    // price_sensitive: quality_weight = 0.0 -> always 1.0
    float f = NpcSpendingModule::compute_quality_factor(1.0f, 0.0f, BuyerType::price_sensitive);
    REQUIRE_THAT(f, WithinAbs(1.0f, 0.001f));
}

TEST_CASE("test_buyer_type_elasticity_modulator", "[npc_spending][tier6]") {
    REQUIRE_THAT(NpcSpendingModule::buyer_type_elasticity_modulator(BuyerType::necessity_buyer),
                 WithinAbs(0.1f, 0.001f));
    REQUIRE_THAT(NpcSpendingModule::buyer_type_elasticity_modulator(BuyerType::price_sensitive),
                 WithinAbs(1.5f, 0.001f));
    REQUIRE_THAT(NpcSpendingModule::buyer_type_elasticity_modulator(BuyerType::quality_seeker),
                 WithinAbs(0.6f, 0.001f));
    REQUIRE_THAT(NpcSpendingModule::buyer_type_elasticity_modulator(BuyerType::brand_loyal),
                 WithinAbs(0.8f, 0.001f));
}

TEST_CASE("test_buyer_type_quality_weight", "[npc_spending][tier6]") {
    REQUIRE_THAT(NpcSpendingModule::buyer_type_quality_weight(BuyerType::price_sensitive),
                 WithinAbs(0.0f, 0.001f));
    REQUIRE_THAT(NpcSpendingModule::buyer_type_quality_weight(BuyerType::brand_loyal),
                 WithinAbs(0.3f, 0.001f));
    REQUIRE_THAT(NpcSpendingModule::buyer_type_quality_weight(BuyerType::quality_seeker),
                 WithinAbs(0.6f, 0.001f));
    REQUIRE_THAT(NpcSpendingModule::buyer_type_quality_weight(BuyerType::necessity_buyer),
                 WithinAbs(0.0f, 0.001f));
}

TEST_CASE("test_demand_contribution_non_negative", "[npc_spending][tier6]") {
    float d = NpcSpendingModule::compute_demand_contribution(1.0f, 0.0f, 1.0f, 1.0f);
    REQUIRE(d >= 0.0f);
}

// ---------------------------------------------------------------------------
// Price sensitivity tests
// ---------------------------------------------------------------------------

TEST_CASE("test_price_sensitive_buyer_high_elasticity", "[npc_spending][tier6]") {
    // price_sensitive faces 2x price: elasticity * 1.5 modulator
    float price_sensitive_f = NpcSpendingModule::compute_price_factor(
        10.0f, 20.0f, -1.0f, BuyerType::price_sensitive, 0.05f);
    // brand_loyal faces same 2x price: elasticity * 0.8 modulator
    float brand_loyal_f = NpcSpendingModule::compute_price_factor(
        10.0f, 20.0f, -1.0f, BuyerType::brand_loyal, 0.05f);
    // price_sensitive should have lower price_factor (more demand reduction)
    REQUIRE(price_sensitive_f < brand_loyal_f);
}

TEST_CASE("test_necessity_buyer_inelastic", "[npc_spending][tier6]") {
    // necessity_buyer at 5x price: elasticity * 0.1 modulator -> barely affected
    float necessity_f = NpcSpendingModule::compute_price_factor(
        10.0f, 50.0f, -1.0f, BuyerType::necessity_buyer, 0.05f);
    // price_sensitive at same price: much more affected
    float sensitive_f = NpcSpendingModule::compute_price_factor(
        10.0f, 50.0f, -1.0f, BuyerType::price_sensitive, 0.05f);
    REQUIRE(necessity_f > sensitive_f);
    // necessity_buyer factor should still be close to 1.0
    REQUIRE(necessity_f > 0.5f);
}

TEST_CASE("test_income_elasticity_scales_demand", "[npc_spending][tier6]") {
    // Higher income -> higher demand for normal good (income_elasticity > 0)
    float low_income_f = NpcSpendingModule::compute_income_factor(
        500.0f, 1000.0f, 1.0f, 5.0f);
    float high_income_f = NpcSpendingModule::compute_income_factor(
        3000.0f, 1000.0f, 1.0f, 5.0f);
    REQUIRE(high_income_f > low_income_f);
}

TEST_CASE("test_quality_seeker_prefers_high_quality", "[npc_spending][tier6]") {
    // quality_seeker with high batch quality
    float high_q = NpcSpendingModule::compute_quality_factor(0.9f, 0.5f, BuyerType::quality_seeker);
    float low_q = NpcSpendingModule::compute_quality_factor(0.3f, 0.5f, BuyerType::quality_seeker);
    REQUIRE(high_q > low_q);
}

// ---------------------------------------------------------------------------
// Integration tests (module-level)
// ---------------------------------------------------------------------------

TEST_CASE("test_basic_consumer_demand_adds_to_demand_buffer", "[npc_spending][tier6]") {
    NpcSpendingModule module;

    WorldState state{};
    state.current_tick = 100;
    state.world_seed = 42;

    // Create one province
    Province p{};
    p.id = 0;
    state.provinces.push_back(p);

    // Create 5 active NPCs in province 0
    for (uint32_t i = 1; i <= 5; ++i) {
        NPC npc{};
        npc.id = i;
        npc.home_province_id = 0;
        npc.status = NPCStatus::active;
        npc.capital = 1000.0f;  // reference income
        state.significant_npcs.push_back(npc);
    }

    // Create one market (food) in province 0
    RegionalMarket market{};
    market.good_id = 1;
    market.province_id = 0;
    market.spot_price = 10.0f;  // == base_price -> price_factor = 1.0
    state.regional_markets.push_back(market);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Each NPC: income_factor=1.0, price_factor=1.0, quality_factor=1.0
    // demand_per_npc = 1.0 * 1.0 * 1.0 * 1.0 = 1.0
    // total = 5.0
    REQUIRE(delta.market_deltas.size() >= 1);
    float total_demand = 0.0f;
    for (const auto& md : delta.market_deltas) {
        if (md.good_id == 1 && md.region_id == 0 && md.demand_buffer_delta.has_value()) {
            total_demand += md.demand_buffer_delta.value();
        }
    }
    REQUIRE_THAT(total_demand, WithinAbs(5.0f, 0.01f));
}

TEST_CASE("test_inactive_npc_excluded", "[npc_spending][tier6]") {
    NpcSpendingModule module;

    WorldState state{};
    state.current_tick = 100;
    state.world_seed = 42;

    Province p{};
    p.id = 0;
    state.provinces.push_back(p);

    // Active NPC
    NPC npc1{};
    npc1.id = 1;
    npc1.home_province_id = 0;
    npc1.status = NPCStatus::active;
    npc1.capital = 1000.0f;
    state.significant_npcs.push_back(npc1);

    // Imprisoned NPC — should be excluded
    NPC npc2{};
    npc2.id = 2;
    npc2.home_province_id = 0;
    npc2.status = NPCStatus::imprisoned;
    npc2.capital = 5000.0f;
    state.significant_npcs.push_back(npc2);

    RegionalMarket market{};
    market.good_id = 1;
    market.province_id = 0;
    market.spot_price = 10.0f;
    state.regional_markets.push_back(market);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Only NPC 1 contributes: demand = 1.0
    float total_demand = 0.0f;
    for (const auto& md : delta.market_deltas) {
        if (md.good_id == 1 && md.demand_buffer_delta.has_value()) {
            total_demand += md.demand_buffer_delta.value();
        }
    }
    REQUIRE_THAT(total_demand, WithinAbs(1.0f, 0.01f));
}

TEST_CASE("test_zero_population_province_no_crash", "[npc_spending][tier6]") {
    NpcSpendingModule module;

    WorldState state{};
    state.current_tick = 100;
    state.world_seed = 42;

    Province p{};
    p.id = 0;
    state.provinces.push_back(p);

    // No NPCs, one market
    RegionalMarket market{};
    market.good_id = 1;
    market.province_id = 0;
    market.spot_price = 10.0f;
    state.regional_markets.push_back(market);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // No demand generated, no crash
    float total_demand = 0.0f;
    for (const auto& md : delta.market_deltas) {
        if (md.demand_buffer_delta.has_value()) {
            total_demand += md.demand_buffer_delta.value();
        }
    }
    REQUIRE_THAT(total_demand, WithinAbs(0.0f, 0.001f));
}
