#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/tick/tick_module.h"
#include "currency_exchange_types.h"

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class CurrencyExchangeModule : public ITickModule {
   public:
    std::string_view name() const noexcept override { return "currency_exchange"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    std::vector<std::string_view> runs_after() const override {
        return {"commodity_trading", "government_budget"};
    }
    bool is_province_parallel() const noexcept override { return false; }
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Static utilities for testing ---
    static float compute_macro_factor(float trade_balance, float inflation, float credit_rating);
    static float apply_rate_clamp(float rate, float baseline, float floor_frac, float ceiling_frac);
    static float convert_currency(float amount, float seller_usd_rate, float buyer_usd_rate,
                                  float transaction_cost);
    static bool is_peg_broken(float foreign_reserves);
    static bool is_weekly_tick(uint32_t current_tick);

    // Constants
    static constexpr float TRADE_BALANCE_WEIGHT = 0.30f;
    static constexpr float INFLATION_WEIGHT = 0.40f;
    static constexpr float SOVEREIGN_RISK_WEIGHT = 0.30f;
    static constexpr float PEG_BREAK_RESERVE_THRESHOLD = 0.15f;
    static constexpr float FLOOR_FRACTION = 0.20f;
    static constexpr float CEILING_FRACTION = 5.00f;
    static constexpr float FX_TRANSACTION_COST = 0.01f;
    static constexpr uint32_t TICKS_PER_WEEK = 7;
};

}  // namespace econlife
