#pragma once

#include <cstdint>
#include <vector>

// ActorTechnologyState defined in shared_types.h (core stub).
// Must be included OUTSIDE the namespace to avoid pulling std headers into econlife.
#include "core/world_state/shared_types.h"

namespace econlife {

// ---------------------------------------------------------------------------
// BusinessSector — what an NPCBusiness does (§5)
//
// Invariant: BusinessSector is a semantic tag for categorisation and UI.
//   The authoritative signal for market layer selection (formal vs. informal)
//   is NPCBusiness.criminal_sector (bool), NOT this enum.
//   BusinessSector::criminal with criminal_sector = false is INVALID in V1;
//   the loader must reject and log an error if this combination appears.
// ---------------------------------------------------------------------------
enum class BusinessSector : uint8_t {
    manufacturing       = 0,   // Heavy and consumer goods manufacturing. High capital,
                                //   significant labor force, supply chain dependencies.
    food_beverage       = 1,   // Food production, processing, distribution, retail F&B.
                                //   High consumer demand visibility; addiction drug overlap
                                //   at precursor level.
    retail              = 2,   // Consumer goods retail. High cash flow, good laundering
                                //   vehicle, demand-signal sensitive.
    services            = 3,   // Professional services, consulting, cleaning, security
                                //   (legitimate). High labor dependence, variable margin.
    real_estate         = 4,   // Property development, management, sales. Key money
                                //   laundering sector. Price responds to criminal dominance.
    agriculture         = 5,   // Farming, livestock, aquaculture. Resource-dependent.
                                //   Climate conditions affect output.
    energy              = 6,   // Extraction of energy resources (oil, gas, coal) and
                                //   regional energy utility operations. [V1: extraction
                                //   only; power generation abstracted per Feature Tier List.]
    technology          = 7,   // Software, electronics, data services. High-margin,
                                //   cybercrime adjacent, R&D intensive.
    finance             = 8,   // Banking, insurance, investment. Laundering infrastructure.
                                //   FIU reporting obligation. Highly regulated.
    transport_logistics = 9,   // Trucking, rail, shipping, warehousing. Supply chain
                                //   backbone. Drug and contraband distribution vehicle.
    media               = 10,  // Newspapers, broadcast, digital platforms. Exposure
                                //   activation mechanism. Ownable for influence.
    security            = 11,  // Private security firms. Facility protection, enforcement
                                //   capacity. Corruption and criminal adjacency risk.
    research            = 12,  // Corporate and pharmaceutical R&D labs. Technology
                                //   advancement, criminal R&D overlap (designer drugs,
                                //   precursor synthesis).
    criminal            = 13,  // Explicitly criminal operations: drug production,
                                //   distribution, protection rackets, laundering fronts.
                                //   Uses informal market price signals, not formal market.
};

// ---------------------------------------------------------------------------
// BusinessProfile — NPC strategic behaviour archetype (§5)
//
// Determines which branch of the quarterly strategic decision matrix executes.
// Gives NPC businesses predictable but not scripted patterns — the player can
// learn a profile and exploit it.
// ---------------------------------------------------------------------------
enum class BusinessProfile : uint8_t {
    cost_cutter          = 0,  // Prioritises margin via cost reduction. Lays off workers,
                                //   switches to cheaper inputs when pressured.
    quality_player       = 1,  // Prioritises brand and quality investment. Differentiates
                                //   on output quality rather than matching competitor prices.
    fast_expander        = 2,  // Prioritises growth and market share acquisition. Acquires
                                //   bankrupt neighbours, increases capacity aggressively.
    defensive_incumbent  = 3,  // Prioritises stability and shareholder return. Activates
                                //   dividend policy, lobbies for regulatory barriers.
};

// ---------------------------------------------------------------------------
// BusinessScale — derived from monthly_revenue, recomputed monthly (§5.1a)
//
// Invariant: Scale classification is STICKY — re-evaluated once per 30 ticks.
//   Scale can only change by one level per evaluation.
//   Hysteresis: downgrade threshold = 80% of upgrade threshold.
//
// Scale thresholds are config-driven (simulation_config.json -> business):
//   scale_micro_ceiling  = 50,000    (~$50k/month revenue)
//   scale_small_ceiling  = 500,000   (~$500k/month revenue)
//   scale_medium_ceiling = 5,000,000 (~$5M/month revenue)
// ---------------------------------------------------------------------------
enum class BusinessScale : uint8_t {
    micro   = 0,  // monthly_revenue < config.business.scale_micro_ceiling
                   // Sole proprietor / small partnership equivalent.
                   // Single owner-operator. No formal separation of business
                   // and personal finances required.

    small   = 1,  // monthly_revenue < config.business.scale_small_ceiling
                   // Small LLC / S-corp equivalent.
                   // Formal salary recommended; distributions available.
                   // No mandatory board.

    medium  = 2,  // monthly_revenue < config.business.scale_medium_ceiling
                   // Private mid-market company.
                   // Salary + bonus + dividends. Board composition
                   // matters for compensation approval.

    large   = 3,  // monthly_revenue >= config.business.scale_medium_ceiling
                   // Large private or public company.
                   // Full executive compensation package. Board compensation
                   // committee approval required. Public disclosure if listed.
};

// ---------------------------------------------------------------------------
// VisibilityScope — Information Visibility for All Actors (§5)
//
// OPSEC is not a criminal-exclusive concern. Every actor — legitimate
// corporation, criminal operation, political office — manages how much of
// their activity is observable by other actors. VisibilityScope governs
// this universally. It applies to activities, documents, relationships,
// and business operations.
//
// VisibilityScope is set per-activity at the time an action is taken.
// It is stored on the relevant event or relationship record, not on the
// actor. The actor's default_activity_scope (on NPCBusiness) provides
// the fallback when no activity-level override is set.
// ---------------------------------------------------------------------------
enum class VisibilityScope : uint8_t {
    public_info     = 0,  // Observable by any actor. Published financials,
                           //   news, public contracts, listed prices, patent filings.
    industry        = 1,  // Observable by sector peers, trade press, analysts.
                           //   Revenue estimates, hiring signals, supplier hints,
                           //   industry conference disclosures.
    institutional   = 2,  // Observable by regulators, tax authority, licensed
                           //   inspectors. Audit-accessible records, tax filings,
                           //   mandatory compliance documentation.
    internal        = 3,  // Observable only by the owning actor and direct employees.
                           //   R&D pipelines, trade secrets, internal pricing,
                           //   unreported financials, strategic plans.
    concealed       = 4,  // Requires active investigation to surface.
                           //   Default for criminal activity. Also applies to
                           //   covert political funding, industrial espionage,
                           //   and any activity the actor is actively hiding.
                           //   FacilitySignals.scrutiny_mitigation governs how
                           //   much physical signal leaks despite concealed intent.
};

// ---------------------------------------------------------------------------
// BuyerType — Consumer demand behaviour archetype (§5)
//
// Set per NPC at world generation from their Background and Trait;
// updated quarterly.
// ---------------------------------------------------------------------------
enum class BuyerType : uint8_t {
    price_sensitive  = 0,  // Maximises value/price ratio. Demand strongly elastic to price.
    quality_seeker   = 1,  // Prioritises quality score regardless of price.
                            //   Effective in high-income NPC cohorts.
    brand_loyal      = 2,  // Sticky to established brand; higher price tolerance within
                            //   trusted brand. Switching cost modelled as inertia on demand.
    necessity_buyer  = 3,  // Buys minimum required quantity regardless of price
                            //   (food, medicine, fuel). Near-zero price elasticity.
};

// ---------------------------------------------------------------------------
// CompensationMechanism — how an owner extracts value from a business (§5.1a)
//
// Availability by scale:
//   owners_draw:     micro only
//   salary_only:     small, medium, large
//   salary_bonus:    small, medium, large
//   salary_dividend: medium, large
//   full_package:    large only
// ---------------------------------------------------------------------------
enum class CompensationMechanism : uint8_t {
    owners_draw    = 0,  // Informal draw: owner pulls cash directly from business account
                          // on demand. No formal salary recorded. All business profit
                          // is personal income. Available: micro only.
                          // Visibility: high — large irregular cash movements are
                          // a classic AML signal. VisibilityScope: internal -> institutional
                          // once transaction volume exceeds reporting threshold.

    salary_only    = 1,  // Fixed per-tick salary as operating cost. Owner is formally
                          // an employee of their own company. Salary independent of profit.
                          // Unpaid if cash insufficient (deferred liability).
                          // Available: small, medium, large.
                          // Visibility: low — salary is normal payroll; unremarkable.

    salary_bonus   = 2,  // Salary + periodic performance bonus. Bonus computed quarterly
                          // or annually as a % of net profit above threshold.
                          // Available: small, medium, large.

    salary_dividend = 3, // Salary + dividends on ownership stake. Dividends paid
                          // from retained earnings, not operating cash flow.
                          // Requires: share structure on the business (public_float_pct > 0
                          // or explicit dividend_eligible_shares field).
                          // Board approval required at medium and large scale.
                          // Available: medium, large.

    full_package   = 4,  // Salary + bonus + equity compensation (options/RSUs vesting
                          // over time) + perks. Standard for large/public companies.
                          // Equity compensation creates a deferred wealth transfer
                          // (EquityGrant vesting schedule).
                          // Board compensation committee approval required.
                          // Public disclosure required if business is listed (StockListing exists).
                          // Available: large only.
};

// ---------------------------------------------------------------------------
// EquityGrant — a single equity compensation grant on a business (§5.1a)
// ---------------------------------------------------------------------------
struct EquityGrant {
    uint32_t business_id;
    float    shares_granted;          // total shares in this grant
    float    shares_vested;           // accumulated; increases each tick by vesting_rate
    float    vesting_rate;            // shares_granted / vesting_period_ticks
    uint32_t grant_tick;
    uint32_t cliff_tick;              // no vesting before this tick (typical: 1 year = 365 ticks)
    uint32_t full_vest_tick;          // all shares vested by this tick (typical: 4 years)
    float    strike_price;            // for options: price at grant; 0.0 for RSUs
                                       // Vested RSUs transfer at current stock price on exercise.
                                       // Vested options: player commands exercise; profit =
                                       //   (current_price - strike_price) x shares_exercised.
};

// ---------------------------------------------------------------------------
// ExecutiveCompensation — how the controlling actor extracts value (§5.1a)
//
// Each NPCBusiness with an owner carries an ExecutiveCompensation record.
// This is a strategic decision the player sets explicitly; NPC owner-operators
// set it via their motivation model at quarterly decision time.
// ---------------------------------------------------------------------------
struct ExecutiveCompensation {
    CompensationMechanism mechanism;

    // --- Salary component (salary_only, salary_bonus, salary_dividend, full_package) ---
    float salary_per_tick;            // recorded as cost_per_tick addition; paid each tick
                                       // if business.cash >= salary_per_tick; else deferred.
                                       // Player sets this. NPC owner sets at quarterly decision.
                                       // Reasonable salary constraint (small+): IRS-equivalent
                                       // rule — must be >= config.business.reasonable_salary_floor
                                       // x regional_wage_by_skill[Management] to avoid
                                       // regulatory scrutiny for distribution-only strategies.

    // --- Bonus component (salary_bonus, full_package) ---
    float bonus_rate;                 // fraction of quarterly net profit paid as bonus; 0.0-1.0
                                       // Net profit = revenue_per_tick x TICKS_PER_QUARTER
                                       //            - cost_per_tick x TICKS_PER_QUARTER
                                       //            - salary_per_tick x TICKS_PER_QUARTER
                                       // Bonus only paid if net profit > 0.
                                       // Medium/large: board approval required if bonus_rate
                                       // exceeds config.business.board_approval_bonus_threshold.

    // --- Distribution / dividend component (salary_dividend, full_package) ---
    float dividend_yield_target;      // fraction of retained earnings distributed per quarter
                                       // Retained earnings = accumulated net profit held in business.
                                       // Actual payout: min(retained_earnings x dividend_yield_target,
                                       //                    business.cash - working_capital_floor)
                                       // where working_capital_floor = cost_per_tick
                                       //                               x config.business.cash_surplus_months
                                       // Medium/large: board vote required (see Board Approval).

    // --- Equity component (full_package only) ---
    std::vector<EquityGrant> equity_grants;
                                       // Grants accumulate over time. Player commands exercise
                                       // of vested options/RSUs via player action; not automatic.

    // --- Owners draw (micro only) ---
    // No struct field — player commands a draw as an on-demand action.
    // Each draw: player specifies amount; deducted from business.cash immediately;
    // added to player.wealth; generates a DeltaBuffer entry for both.
    // Draws above config.business.draw_reporting_threshold per month generate
    // a suspicious_transaction evidence token at VisibilityScope::institutional.
};

// ---------------------------------------------------------------------------
// BoardComposition — board of directors representation (§5.1a)
//
// At medium and large scale, some compensation decisions require board
// approval. The board is represented as a set of NPC relationships with
// defined composition.
//
// Invariant: independence_score range is [0.0, 1.0].
//   0.0 = fully captured board (rubber stamp).
//   1.0 = fully independent board.
// ---------------------------------------------------------------------------
struct BoardComposition {
    std::vector<uint32_t> member_npc_ids;  // NPCs serving on the board
    float independence_score;              // 0.0-1.0; fraction of members not
                                            // personally obligated to the player.
                                            // 0.0 = fully captured board (rubber stamp).
                                            // 1.0 = fully independent board.
    uint32_t next_approval_tick;           // next quarterly board meeting
};

// ---------------------------------------------------------------------------
// RegionalMarket — per-good per-province market state (§5)
//
// Invariant: province_id refers to a Province, NOT a Region (Region is thin
//   grouping only).
// Invariant: supply does NOT include goods in transit; only local production
//   this tick + TransitShipments with arrival_tick == current_tick.
// Invariant: import_price_ceiling and export_price_floor of 0.0 mean no
//   active LOD 1 offer; config coefficients are used as fallback.
// ---------------------------------------------------------------------------
struct RegionalMarket {
    uint32_t good_id;
    uint32_t province_id;               // province this market belongs to (not region_id)
    float spot_price;
    float equilibrium_price;             // recalculated each tick
    float adjustment_rate;               // good-type dependent: financial fast, housing slow
    float supply;                        // local production this tick + transit arrivals this tick (§18.9)
                                          // goods in transit to this province are NOT included until arrival_tick
    float demand_buffer;                 // from previous tick (one-tick lag)
    float import_price_ceiling;          // set on LOD 1 trade offer acceptance (§18.16):
                                          // = offer.offer_price for the accepted import offer.
                                          // Applied immediately on acceptance — price cannot
                                          // exceed what LOD 1 importers will pay even while
                                          // goods are in transit.
                                          // 0.0 = no active LOD 1 import offer for this good.
                                          // When non-zero, overrides config.import_ceiling_coeff
                                          // as the effective upper clamp in the equilibrium
                                          // price formula (Step 1).
    float export_price_floor;            // set when a LOD 1 nation publishes an import bid
                                          // for this good (they want to buy it at offer_price).
                                          // = bid offer_price; province can always sell to LOD 1
                                          // at this price, so domestic price won't fall below it.
                                          // 0.0 = no active LOD 1 export bid for this good.
                                          // When non-zero, overrides config.export_floor_coeff
                                          // as the effective lower clamp in Step 1.
};

// ---------------------------------------------------------------------------
// NPCBusiness — NPC (and player-owned) business entity (§5, §5.1, §5.1a, §43.2)
//
// Canonical rule: A player-owned business is an NPCBusiness record with
//   owner_id = player_id. There is no separate PlayerBusiness struct.
//   The player's restaurant, factory, or criminal distribution network is
//   modeled identically to any NPC business.
//
// Invariant: criminal_sector is the authoritative signal for market layer
//   selection (formal vs. informal).
// Invariant: BusinessSector::criminal with criminal_sector == false is
//   INVALID in V1; the loader must reject and log an error.
// Invariant: dispatch_day_offset is set at world gen as hash(id) % 30.
//   Player-owned businesses set dispatch_day_offset to 0 (never consulted).
// ---------------------------------------------------------------------------
struct NPCBusiness {
    uint32_t id;
    BusinessSector sector;
    BusinessProfile profile;              // cost_cutter, quality_player, fast_expander, defensive_incumbent
    float cash;
    float revenue_per_tick;
    float cost_per_tick;
    float market_share;                   // in their regional sector
    uint32_t strategic_decision_tick;     // when they next make a quarterly decision; see dispatch_day_offset
    uint8_t  dispatch_day_offset;         // 0-29; set at world gen as hash(id) % 30
                                           // first decision at world_start_tick + offset; then every 30 ticks
                                           // spreads NPC business dispatch load evenly across each month
                                           // player-owned businesses dispatch on command; no offset
    ActorTechnologyState actor_tech_state; // per-actor technology portfolio (see R&D doc Part 3.5)
                                           // replaces flat technology_tier float.
                                           // Effective tech tier is derived at recipe execution time:
                                           //   derived_tech_tier = max over active facilities of
                                           //   facility.tech_tier weighted by actor_tech_state.maturation_of(recipe.key_technology_node)
                                           // Quality ceiling computation uses both facility tier
                                           //   and actor maturation_level (see Commodities & Factories Part 7)
    bool criminal_sector;                 // true for businesses in criminal supply chain;
                                           // uses informal market price rather than formal spot price
    uint32_t province_id;                 // province this business operates in; used for market lookup
    float regulatory_violation_severity;  // 0.0-1.0; legal-but-noncompliant ops
                                           // 0.0 = fully compliant (default)
                                           // 0.0-0.5 = noncompliant: generates
                                           //   evidence tokens, feeds
                                           //   RegulatorScrutinyMeter fill additive
                                           // 0.5-1.0 = severe: triggers
                                           //   enforcement_action consequence when
                                           //   scrutiny_meter crosses threshold
                                           // Does NOT change criminal_sector or
                                           //   market layer.
                                           // Populated by: labor disputes, safety
                                           //   incident consequences, audit findings,
                                           //   player/NPC decisions.
                                           // Decays at
                                           //   config.scrutiny.violation_decay_rate
                                           //   per tick when no new violations.
                                           // Field applies universally: NPC businesses
                                           //   and player-operated facilities carry
                                           //   identical semantics and decay rules.
    VisibilityScope default_activity_scope; // fallback scope applied to activities
                                            //   when no activity-level override is set.
                                            // Legitimate businesses: institutional
                                            //   (regulators can audit by default).
                                            // Criminal businesses (criminal_sector=true):
                                            //   concealed (all activity hidden by default;
                                            //   FacilitySignals govern physical leakage).
                                            // Individual activities may override:
                                            //   a legitimate business can conduct a
                                            //   concealed covert political donation;
                                            //   a criminal business can make a
                                            //   public_info charitable donation as cover.

    // --- Field addition from §5.1: Ownership ---
    uint32_t owner_id;                    // 0 = independent NPC business (no player or parent-org owner)
                                           // player_id = player-owned business
                                           // npc_id    = NPC-owned subsidiary (e.g., criminal org front company)
                                           // Ownership does not alter tick processing — same production,
                                           // market, and scrutiny logic runs regardless of owner_id.
                                           // Ownership determines: profit transfer, acquisition
                                           // eligibility, IPO listing authorization, and board composition.

    // --- Field addition from §5.1a: Deferred salary ---
    float deferred_salary_liability;      // Accumulated unpaid salary when business.cash < salary_per_tick.
                                           // Deferred salary is paid first when cash recovers.
                                           // Sustained deferred salary (> config.business.deferred_salary_max_ticks
                                           // ticks) generates a witnessed_wage_theft memory entry.

    // --- Field addition from §43.2: Working capital approximation ---
    float accounts_payable_float;         // estimated outstanding supplier obligations.
                                           // Approximated as: cost_per_tick x config.business.payment_lag_ticks.
                                           // Conceptually: the business owes its last N ticks of input costs.
                                           // Used by: bankruptcy evaluation (if cash < accounts_payable_float,
                                           //   business is cash-flow insolvent even if technically profitable).
                                           // Updated monthly; not per-tick.
                                           // Full AR/AP model with invoice timing is EX scope.
};

}  // namespace econlife
