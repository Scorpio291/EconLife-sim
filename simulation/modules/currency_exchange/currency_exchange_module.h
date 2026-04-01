#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "currency_exchange_types.h"

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class CurrencyExchangeModule : public ITickModule {
   public:
    explicit CurrencyExchangeModule(const CurrencyExchangeConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "currency_exchange"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    std::vector<std::string_view> runs_after() const override {
        return {"commodity_trading", "government_budget"};
    }
    bool is_province_parallel() const noexcept override { return false; }
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Static utilities for testing ---
    static float compute_macro_factor(float trade_balance, float inflation, float credit_rating,
                                      float trade_balance_weight, float inflation_weight,
                                      float sovereign_risk_weight);
    static float apply_rate_clamp(float rate, float baseline, float floor_frac, float ceiling_frac);
    static float convert_currency(float amount, float seller_usd_rate, float buyer_usd_rate,
                                  float transaction_cost);
    static bool is_peg_broken(float foreign_reserves, float peg_break_reserve_threshold);
    static bool is_weekly_tick(uint32_t current_tick);

    static constexpr uint32_t TICKS_PER_WEEK = 7;

    const CurrencyExchangeConfig& config() const { return cfg_; }

   private:
    CurrencyExchangeConfig cfg_;
};

}  // namespace econlife
