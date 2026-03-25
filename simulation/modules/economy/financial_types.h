#pragma once

#include <cstdint>
#include <map>

namespace econlife {

// =====================================================================
// Financial Markets (§17) and Commodity Trading (§42)
// =====================================================================

// ---------------------------------------------------------------------------
// StockListing — a publicly listed business entity (§17.1)
//
// Processing runs at tick step 19 (after income/dividend settlement).
//
// Invariant: current_price is updated each tick at step 19 via:
//   price_new = price_old
//       x (1.0 + earnings_momentum x config.stock.earnings_weight)
//       x (1.0 + sentiment_factor  x config.stock.sentiment_weight)
//       x (1.0 + random_noise)
//   Clamped to [ipo_price x config.stock.price_floor_fraction, infinity).
//
// Invariant: An NPCBusiness or player-owned business may list when
//   revenue > config.stock.ipo_revenue_threshold for at least
//   config.stock.ipo_qualifying_ticks consecutive ticks.
//   public_float_pct defaults to 0.30 at IPO.
// ---------------------------------------------------------------------------
struct StockListing {
    uint32_t id;
    uint32_t npc_business_id;  // or player_business_id for player-owned listed entity
    float shares_outstanding;  // total shares in existence
    float public_float_pct;    // 0.0-1.0; fraction of shares tradeable on open market
    float current_price;       // game currency per share; updated each tick step 19
    float ipo_price;           // reference; set at listing time
    uint32_t listed_tick;
    float trailing_eps;        // earnings per share; rolling 30-tick average
    float dividend_per_share;  // paid each month tick (TICKS_PER_MONTH); 0.0 if no dividend policy
    float book_value_per_share;  // assets - liabilities / shares_outstanding
};

// ---------------------------------------------------------------------------
// StockPortfolio — holdings for a single actor (§17.1)
//
// Invariant: unrealized_gain is recomputed at tick step 19 (display only).
// ---------------------------------------------------------------------------
struct StockPortfolio {
    uint32_t owner_id;                   // NPC id or player id
    std::map<uint32_t, float> holdings;  // listing_id -> shares held
    float unrealized_gain;               // for display; recomputed at tick step 19
};

// ---------------------------------------------------------------------------
// GovernmentBond — a sovereign debt instrument (§17.2)
//
// Invariant: credit_rating is in [0.0, 1.0]; 1.0 = AAA.
//   Computed from fiscal health:
//   credit_rating = clamp(1.0 - debt_to_gdp x config.bond.debt_rating_sensitivity
//                             - deficit_fraction x config.bond.deficit_rating_sensitivity,
//                             0.0, 1.0)
//
// Invariant: current_yield rises as current_price falls (inversely related).
//   current_yield = config.bond.base_risk_free_rate + risk_premium
//   current_price = face_value x coupon_rate / current_yield  (simplified V1)
//
// Invariant: maturity_absolute_tick = issued_tick + maturity_ticks.
//
// Nations issue new bonds at the start of each simulated quarter
// (every TICKS_PER_MONTH x 3 ticks) when fiscal_deficit > 0.
// ---------------------------------------------------------------------------
struct GovernmentBond {
    uint32_t id;
    uint32_t nation_id;
    float face_value;         // game currency; principal repaid at maturity
    float coupon_rate;        // annual interest rate; fixed at issuance
    float current_yield;      // market yield; rises as price falls (inversely related)
    float current_price;      // market price; updated at tick step 19
    float credit_rating;      // 0.0-1.0; 1.0 = AAA; computed from fiscal health
    uint32_t maturity_ticks;  // ticks from issuance until principal repayment
    uint32_t issued_tick;
    uint32_t maturity_absolute_tick;  // issued_tick + maturity_ticks
};

// ---------------------------------------------------------------------------
// BondHolding — a single actor's position in a government bond (§17.2)
// ---------------------------------------------------------------------------
struct BondHolding {
    uint32_t bond_id;
    uint32_t holder_id;    // NPC id or player id
    float quantity;        // number of bond units held
    float purchase_price;  // for unrealized gain/loss calculation
};

// ---------------------------------------------------------------------------
// PositionType — long or short commodity position (§42.1)
// ---------------------------------------------------------------------------
enum class PositionType : uint8_t {
    long_position = 0,   // Bought units; profit if spot_price rises.
    short_position = 1,  // Sold units not yet owned (borrowed from market pool);
                         // profit if spot_price falls.
                         // V1: short positions simplified — no borrowing
                         // mechanism; short is a deferred sell at entry_price
                         // settled at market exit. No margin call mechanic in V1.
};

// ---------------------------------------------------------------------------
// CommodityPosition — player/NPC investment position in a commodity (§42.1)
//
// Invariant: current_value is derived (quantity x spot_price); not stored
//   persistently. Recomputed at read time.
// Invariant: exit_tick == 0 while position is open; set on close.
// Invariant: realised_pnl == 0.0 while open; set on close:
//   long:  (exit_price - entry_price) x quantity
//   short: (entry_price - exit_price) x quantity
//
// Market impact: positions exceeding
//   RegionalMarket.supply x config.trading.market_impact_threshold
//   affect spot_price via supply/demand modifier.
//
// Tax: realised_pnl > 0 generates capital gains; collected at next
//   provincial tax step (§31).
// ---------------------------------------------------------------------------
struct CommodityPosition {
    uint32_t id;
    uint32_t actor_id;     // player_id or npc_id
    uint32_t good_id;      // references goods.csv
    uint32_t province_id;  // which RegionalMarket this position is in
    PositionType position_type;
    float quantity;       // units held
    float entry_price;    // spot_price at position open
    float current_value;  // derived: quantity x spot_price(current); not stored
    uint32_t opened_tick;
    uint32_t exit_tick;  // 0 while open; set on close
    float realised_pnl;  // 0.0 while open; set on close
                         // long:  (exit_price - entry_price) x quantity
                         // short: (entry_price - exit_price) x quantity
};

}  // namespace econlife
