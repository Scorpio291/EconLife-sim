#include "currency_exchange_module.h"
#include "core/world_state/world_state.h"
#include "core/world_state/delta_buffer.h"
#include <algorithm>
#include <cmath>

namespace econlife {

float CurrencyExchangeModule::compute_macro_factor(float trade_balance, float inflation,
                                                    float credit_rating) {
    return 1.0f + (trade_balance * TRADE_BALANCE_WEIGHT)
               - (inflation * INFLATION_WEIGHT)
               - ((1.0f - credit_rating) * SOVEREIGN_RISK_WEIGHT);
}

float CurrencyExchangeModule::apply_rate_clamp(float rate, float baseline,
                                                float floor_frac, float ceiling_frac) {
    float floor_val = floor_frac * baseline;
    float ceiling_val = ceiling_frac * baseline;
    return std::clamp(rate, floor_val, ceiling_val);
}

float CurrencyExchangeModule::convert_currency(float amount, float seller_usd_rate,
                                                float buyer_usd_rate, float transaction_cost) {
    if (buyer_usd_rate <= 0.0f) return 0.0f;
    return amount * (seller_usd_rate / buyer_usd_rate) * (1.0f + transaction_cost);
}

bool CurrencyExchangeModule::is_peg_broken(float foreign_reserves) {
    return foreign_reserves <= PEG_BREAK_RESERVE_THRESHOLD;
}

bool CurrencyExchangeModule::is_weekly_tick(uint32_t current_tick) {
    return (current_tick % TICKS_PER_WEEK) == 0;
}

void CurrencyExchangeModule::execute(const WorldState& state, DeltaBuffer& delta) {
    if (!is_weekly_tick(state.current_tick)) return;

    // Process each currency in module-internal state
    for (auto& currency : currencies_) {
        if (currency.pegged) {
            if (is_peg_broken(currency.foreign_reserves)) {
                currency.pegged = false;
                // Currency crisis event would be queued
            }
            // Pegged: rate stays at peg_rate
            continue;
        }

        // Free-floating: apply macro model
        // In full implementation, would read nation economic indicators
        float macro_factor = 1.0f;  // neutral default
        float new_rate = currency.usd_rate * macro_factor;
        currency.usd_rate = apply_rate_clamp(new_rate, currency.usd_rate_baseline,
                                              FLOOR_FRACTION, CEILING_FRACTION);
    }
}

}  // namespace econlife
