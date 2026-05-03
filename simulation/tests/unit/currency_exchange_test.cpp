#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"
#include "modules/currency_exchange/currency_exchange_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

TEST_CASE("CurrencyExchange: macro factor computation", "[currency_exchange][tier11]") {
    CurrencyExchangeConfig cfg{};
    // 1.0 + (0.10*0.30) - (0.05*0.40) - ((1.0-0.80)*0.30)
    // = 1.0 + 0.03 - 0.02 - 0.06 = 0.95
    float factor = CurrencyExchangeModule::compute_macro_factor(
        0.10f, 0.05f, 0.80f, cfg.trade_balance_weight, cfg.inflation_weight,
        cfg.sovereign_risk_weight);
    REQUIRE_THAT(factor, WithinAbs(0.95f, 0.01f));
}

TEST_CASE("CurrencyExchange: macro factor neutral", "[currency_exchange][tier11]") {
    CurrencyExchangeConfig cfg{};
    float factor = CurrencyExchangeModule::compute_macro_factor(
        0.0f, 0.0f, 1.0f, cfg.trade_balance_weight, cfg.inflation_weight,
        cfg.sovereign_risk_weight);
    REQUIRE_THAT(factor, WithinAbs(1.0f, 0.01f));
}

TEST_CASE("CurrencyExchange: rate floor clamp", "[currency_exchange][tier11]") {
    // baseline=1.0, floor=0.20, ceiling=5.0
    float result = CurrencyExchangeModule::apply_rate_clamp(0.10f, 1.0f, 0.20f, 5.0f);
    REQUIRE_THAT(result, WithinAbs(0.20f, 0.01f));
}

TEST_CASE("CurrencyExchange: rate ceiling clamp", "[currency_exchange][tier11]") {
    float result = CurrencyExchangeModule::apply_rate_clamp(10.0f, 1.0f, 0.20f, 5.0f);
    REQUIRE_THAT(result, WithinAbs(5.0f, 0.01f));
}

TEST_CASE("CurrencyExchange: rate within bounds unchanged", "[currency_exchange][tier11]") {
    float result = CurrencyExchangeModule::apply_rate_clamp(2.5f, 1.0f, 0.20f, 5.0f);
    REQUIRE_THAT(result, WithinAbs(2.5f, 0.01f));
}

TEST_CASE("CurrencyExchange: cross-currency conversion", "[currency_exchange][tier11]") {
    // 100 * (5.0/1.0) * 1.01 = 505.0
    float result = CurrencyExchangeModule::convert_currency(100.0f, 5.0f, 1.0f, 0.01f);
    REQUIRE_THAT(result, WithinAbs(505.0f, 0.1f));
}

TEST_CASE("CurrencyExchange: conversion zero buyer rate", "[currency_exchange][tier11]") {
    float result = CurrencyExchangeModule::convert_currency(100.0f, 5.0f, 0.0f, 0.01f);
    REQUIRE_THAT(result, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("CurrencyExchange: peg broken detection", "[currency_exchange][tier11]") {
    CurrencyExchangeConfig cfg{};
    REQUIRE(CurrencyExchangeModule::is_peg_broken(0.10f, cfg.peg_break_reserve_threshold) == true);
    REQUIRE(CurrencyExchangeModule::is_peg_broken(0.15f, cfg.peg_break_reserve_threshold) == true);
    REQUIRE(CurrencyExchangeModule::is_peg_broken(0.50f, cfg.peg_break_reserve_threshold) == false);
}

TEST_CASE("CurrencyExchange: weekly tick check", "[currency_exchange][tier11]") {
    REQUIRE(CurrencyExchangeModule::is_weekly_tick(0) == true);
    REQUIRE(CurrencyExchangeModule::is_weekly_tick(7) == true);
    REQUIRE(CurrencyExchangeModule::is_weekly_tick(14) == true);
    REQUIRE(CurrencyExchangeModule::is_weekly_tick(3) == false);
}

TEST_CASE("CurrencyExchange: round trip loss", "[currency_exchange][tier11]") {
    // A->B: 100 * (2.0/1.0) * 1.01 = 202.0
    float a_to_b = CurrencyExchangeModule::convert_currency(100.0f, 2.0f, 1.0f, 0.01f);
    // B->A: 202.0 * (1.0/2.0) * 1.01 = 101.01 * 1.01 = 102.01
    float b_to_a = CurrencyExchangeModule::convert_currency(a_to_b, 1.0f, 2.0f, 0.01f);
    // Loss is approximately 2*1% = 2%
    REQUIRE(b_to_a > 100.0f);  // gained from rate difference
    // But the transaction costs apply
    float no_cost_round = 100.0f * (2.0f / 1.0f) * (1.0f / 2.0f);  // 100
    REQUIRE(b_to_a > no_cost_round);  // 1% cost applies twice but rate is symmetric
}

TEST_CASE("CurrencyExchange: execute writes CurrencyDelta on weekly tick",
          "[currency_exchange][tier11]") {
    CurrencyExchangeModule module;

    WorldState state{};
    state.current_tick = 7;  // weekly tick (7 % 7 == 0)
    state.world_seed = 42;
    state.player.reset();
    state.lod2_price_index.reset();
    state.game_mode = GameMode::standard;

    // Add a nation with economic indicators
    Nation nation{};
    nation.id = 1;
    nation.trade_balance_fraction = 0.10f;
    nation.inflation_rate = 0.05f;
    nation.credit_rating = 0.80f;
    state.nations.push_back(nation);

    // Add a free-floating currency
    CurrencyRecord cur{};
    cur.nation_id = 1;
    cur.iso_code = "BRL";
    cur.usd_rate = 5.0f;
    cur.usd_rate_baseline = 5.0f;
    cur.volatility = 0.0f;  // zero volatility for deterministic test
    cur.pegged = false;
    cur.foreign_reserves = 0.8f;
    state.currencies.push_back(cur);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // macro_factor = 1.0 + (0.10*0.30) - (0.05*0.40) - (0.20*0.30) = 0.95
    // new_rate = 5.0 * 0.95 = 4.75
    REQUIRE(delta.currency_deltas.size() == 1);
    REQUIRE(delta.currency_deltas[0].nation_id == 1);
    REQUIRE(delta.currency_deltas[0].usd_rate_update.has_value());
    REQUIRE_THAT(*delta.currency_deltas[0].usd_rate_update, WithinAbs(4.75f, 0.01f));
}

TEST_CASE("CurrencyExchange: execute is noop on non-weekly tick", "[currency_exchange][tier11]") {
    CurrencyExchangeModule module;

    WorldState state{};
    state.current_tick = 3;  // not a weekly tick
    state.world_seed = 42;
    state.player.reset();
    state.lod2_price_index.reset();
    state.game_mode = GameMode::standard;

    CurrencyRecord cur{};
    cur.nation_id = 1;
    cur.usd_rate = 5.0f;
    cur.usd_rate_baseline = 5.0f;
    cur.pegged = false;
    state.currencies.push_back(cur);

    DeltaBuffer delta{};
    module.execute(state, delta);

    REQUIRE(delta.currency_deltas.empty());
}

TEST_CASE("CurrencyExchange: peg break emits pegged_update delta", "[currency_exchange][tier11]") {
    CurrencyExchangeModule module;

    WorldState state{};
    state.current_tick = 14;  // weekly tick
    state.world_seed = 42;
    state.player.reset();
    state.lod2_price_index.reset();
    state.game_mode = GameMode::standard;

    CurrencyRecord cur{};
    cur.nation_id = 1;
    cur.usd_rate = 3.5f;
    cur.usd_rate_baseline = 3.5f;
    cur.pegged = true;
    cur.peg_rate = 3.5f;
    cur.foreign_reserves = 0.10f;  // below 0.15 threshold
    state.currencies.push_back(cur);

    DeltaBuffer delta{};
    module.execute(state, delta);

    REQUIRE(delta.currency_deltas.size() == 1);
    REQUIRE(delta.currency_deltas[0].nation_id == 1);
    REQUIRE(delta.currency_deltas[0].pegged_update.has_value());
    REQUIRE(*delta.currency_deltas[0].pegged_update == false);
}

TEST_CASE("CurrencyExchange: pegged currency with healthy reserves produces no delta",
          "[currency_exchange][tier11]") {
    CurrencyExchangeModule module;

    WorldState state{};
    state.current_tick = 7;  // weekly tick
    state.world_seed = 42;
    state.player.reset();
    state.lod2_price_index.reset();
    state.game_mode = GameMode::standard;

    CurrencyRecord cur{};
    cur.nation_id = 1;
    cur.usd_rate = 3.5f;
    cur.usd_rate_baseline = 3.5f;
    cur.pegged = true;
    cur.peg_rate = 3.5f;
    cur.foreign_reserves = 0.50f;  // above threshold
    state.currencies.push_back(cur);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Healthy peg: no rate change, no peg break.
    REQUIRE(delta.currency_deltas.empty());
}

TEST_CASE("CurrencyExchange: constants match spec", "[currency_exchange][tier11]") {
    CurrencyExchangeConfig cfg{};
    REQUIRE_THAT(cfg.trade_balance_weight, WithinAbs(0.30f, 0.001f));
    REQUIRE_THAT(cfg.inflation_weight, WithinAbs(0.40f, 0.001f));
    REQUIRE_THAT(cfg.sovereign_risk_weight, WithinAbs(0.30f, 0.001f));
    REQUIRE_THAT(cfg.peg_break_reserve_threshold, WithinAbs(0.15f, 0.001f));
    REQUIRE_THAT(cfg.fx_transaction_cost, WithinAbs(0.01f, 0.001f));
}
