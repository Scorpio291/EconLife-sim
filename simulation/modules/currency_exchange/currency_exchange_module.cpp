#include "currency_exchange_module.h"

#include <algorithm>
#include <cmath>

#include "core/rng/deterministic_rng.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"

namespace econlife {

float CurrencyExchangeModule::compute_macro_factor(float trade_balance, float inflation,
                                                   float credit_rating, float trade_balance_weight,
                                                   float inflation_weight,
                                                   float sovereign_risk_weight) {
    return 1.0f + (trade_balance * trade_balance_weight) - (inflation * inflation_weight) -
           ((1.0f - credit_rating) * sovereign_risk_weight);
}

float CurrencyExchangeModule::apply_rate_clamp(float rate, float baseline, float floor_frac,
                                               float ceiling_frac) {
    float floor_val = floor_frac * baseline;
    float ceiling_val = ceiling_frac * baseline;
    return std::clamp(rate, floor_val, ceiling_val);
}

float CurrencyExchangeModule::convert_currency(float amount, float seller_usd_rate,
                                               float buyer_usd_rate, float transaction_cost) {
    if (buyer_usd_rate <= 0.0f)
        return 0.0f;
    return amount * (seller_usd_rate / buyer_usd_rate) * (1.0f + transaction_cost);
}

bool CurrencyExchangeModule::is_peg_broken(float foreign_reserves,
                                           float peg_break_reserve_threshold) {
    return foreign_reserves <= peg_break_reserve_threshold;
}

bool CurrencyExchangeModule::is_weekly_tick(uint32_t current_tick) {
    return (current_tick % TICKS_PER_WEEK) == 0;
}

void CurrencyExchangeModule::execute(const WorldState& state, DeltaBuffer& delta) {
    if (!is_weekly_tick(state.current_tick))
        return;

    DeterministicRNG rng(state.world_seed + state.current_tick * 7919);

    // Process each currency from WorldState in nation_id ascending order (deterministic).
    for (const auto& currency : state.currencies) {
        if (currency.pegged) {
            if (is_peg_broken(currency.foreign_reserves, cfg_.peg_break_reserve_threshold)) {
                // Peg break: emit delta to set pegged = false.
                CurrencyDelta cd{};
                cd.nation_id = currency.nation_id;
                cd.pegged_update = false;
                delta.currency_deltas.push_back(cd);

                // Trigger a currency_crisis ActiveRandomEvent. Severity
                // scales with the depth of the reserve shortfall: a peg
                // breaking at exactly the threshold lands at 0.40 (the
                // template minimum); reserves at zero (or negative)
                // land at 0.95 (template max). The trigger lands in
                // state.pending_random_event_triggers and is consumed by
                // random_events at the start of next tick (this module
                // runs at Tier 11, random_events at Tier 1). One trigger
                // per peg break per province — we use the nation's
                // home province as the event location.
                float threshold = cfg_.peg_break_reserve_threshold;
                float depth = (threshold > 0.0f)
                                  ? std::clamp(1.0f - currency.foreign_reserves / threshold,
                                               0.0f, 1.0f)
                                  : 1.0f;
                // Locate a representative province for the affected nation
                // (CurrencyRecord has no home_province_id field). First
                // match in id-ascending order is deterministic.
                uint32_t event_province_id = 0;
                bool found = false;
                for (const auto& prov : state.provinces) {
                    if (prov.nation_id == currency.nation_id) {
                        event_province_id = prov.id;
                        found = true;
                        break;
                    }
                }
                if (found) {
                    RandomEventTriggerDelta trig{};
                    trig.template_key = "currency_crisis";
                    trig.province_id = event_province_id;
                    trig.severity = 0.40f + depth * (0.95f - 0.40f);
                    delta.new_random_event_triggers.push_back(trig);
                }
            }
            // Pegged: rate stays at peg_rate; no rate delta needed.
            continue;
        }

        // Free-floating: compute macro-driven rate adjustment.
        // Look up nation economic indicators.
        float trade_balance = 0.0f;
        float inflation = 0.0f;
        float credit_rating = 0.8f;  // default
        for (const auto& nation : state.nations) {
            if (nation.id == currency.nation_id) {
                trade_balance = nation.trade_balance_fraction;
                inflation = nation.inflation_rate;
                credit_rating = nation.credit_rating;
                break;
            }
        }

        float macro_factor =
            compute_macro_factor(trade_balance, inflation, credit_rating, cfg_.trade_balance_weight,
                                 cfg_.inflation_weight, cfg_.sovereign_risk_weight);

        // Add noise term using DeterministicRNG for this nation.
        float noise = 0.0f;
        if (currency.volatility > 0.0f) {
            // Approximate normal distribution from uniform: Box-Muller.
            float u1 = rng.next_float();
            float u2 = rng.next_float();
            // Clamp u1 away from 0 to avoid log(0).
            u1 = std::max(u1, 0.0001f);
            noise =
                currency.volatility * std::sqrt(-2.0f * std::log(u1)) * std::cos(6.2831853f * u2);
        }

        float new_rate = currency.usd_rate * (macro_factor + noise);

        // Guard against NaN from extreme values.
        if (std::isnan(new_rate) || std::isinf(new_rate)) {
            new_rate = currency.usd_rate;  // fallback to previous rate
        }

        new_rate = apply_rate_clamp(new_rate, currency.usd_rate_baseline, cfg_.floor_fraction,
                                    cfg_.ceiling_fraction);

        // Only emit delta if rate actually changed.
        if (new_rate != currency.usd_rate) {
            CurrencyDelta cd{};
            cd.nation_id = currency.nation_id;
            cd.usd_rate_update = new_rate;
            delta.currency_deltas.push_back(cd);
        }
    }
}

}  // namespace econlife
