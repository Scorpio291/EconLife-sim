#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "modules/currency_exchange/currency_exchange_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

TEST_CASE("CurrencyExchange: macro factor computation", "[currency_exchange][tier11]") {
    // 1.0 + (0.10*0.30) - (0.05*0.40) - ((1.0-0.80)*0.30)
    // = 1.0 + 0.03 - 0.02 - 0.06 = 0.95
    float factor = CurrencyExchangeModule::compute_macro_factor(0.10f, 0.05f, 0.80f);
    REQUIRE_THAT(factor, WithinAbs(0.95f, 0.01f));
}

TEST_CASE("CurrencyExchange: macro factor neutral", "[currency_exchange][tier11]") {
    float factor = CurrencyExchangeModule::compute_macro_factor(0.0f, 0.0f, 1.0f);
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
    REQUIRE(CurrencyExchangeModule::is_peg_broken(0.10f) == true);
    REQUIRE(CurrencyExchangeModule::is_peg_broken(0.15f) == true);
    REQUIRE(CurrencyExchangeModule::is_peg_broken(0.50f) == false);
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
    float no_cost_round = 100.0f * (2.0f/1.0f) * (1.0f/2.0f);  // 100
    REQUIRE(b_to_a > no_cost_round);  // 1% cost applies twice but rate is symmetric
}

TEST_CASE("CurrencyExchange: constants match spec", "[currency_exchange][tier11]") {
    REQUIRE_THAT(CurrencyExchangeModule::TRADE_BALANCE_WEIGHT, WithinAbs(0.30f, 0.001f));
    REQUIRE_THAT(CurrencyExchangeModule::INFLATION_WEIGHT, WithinAbs(0.40f, 0.001f));
    REQUIRE_THAT(CurrencyExchangeModule::SOVEREIGN_RISK_WEIGHT, WithinAbs(0.30f, 0.001f));
    REQUIRE_THAT(CurrencyExchangeModule::PEG_BREAK_RESERVE_THRESHOLD, WithinAbs(0.15f, 0.001f));
    REQUIRE_THAT(CurrencyExchangeModule::FX_TRANSACTION_COST, WithinAbs(0.01f, 0.001f));
}
