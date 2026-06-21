#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "modules/economy/economy_types.h"  // BusinessSector, BusinessProfile

// PackageConfig — spec-correct defaults for all data-driven module parameters.
// Loaded from packages/base_game/config/ JSON files at startup.
// Missing files silently fall back to these defaults (all valid for simulation).
// Passed by const reference to module constructors; never modified at runtime.

namespace econlife {

struct PriceModelConfig {
    float adjustment_rate_default = 0.05f;
    float adjustment_rate_min = 0.01f;
    float adjustment_rate_max = 0.15f;
    float equilibrium_convergence_speed = 0.03f;
    float price_floor_multiplier = 0.1f;
    float price_ceiling_multiplier = 10.0f;
    float import_ceiling_premium = 1.3f;
    float export_floor_discount = 0.7f;
    float volatility_dampening = 0.85f;
    uint32_t spot_price_update_step = 5;
};

struct SupplyChainEconConfig {
    uint32_t shortage_propagation_delay_ticks = 1;
    float surplus_decay_rate = 0.02f;
    float bottleneck_output_penalty = 0.5f;
    uint32_t max_depth = 5;
};

struct LaborMarketConfig {
    uint32_t wage_update_frequency_ticks = 30;
    float minimum_wage_default = 15.0f;
    float wage_elasticity = 0.1f;
    float unemployment_equilibrium_rate = 0.05f;
    float skill_rust_rate = 0.001f;
    float skill_gain_rate = 0.005f;
};

struct NpcBusinessEconomyConfig {
    uint32_t quarterly_decision_interval_ticks = 90;
    float bankruptcy_threshold = -10000.0f;
    uint32_t consolidation_check_interval = 180;
    float startup_failure_rate_year1 = 0.20f;
    uint32_t max_businesses_per_province = 500;
};

struct TradeConfig {
    uint32_t lod1_offer_refresh_ticks = 30;
    uint32_t lod2_price_index_refresh_ticks = 365;
    float tariff_base_rate = 0.05f;
    float transport_cost_per_km_per_tonne = 0.05f;
    uint32_t transit_delay_base_ticks = 3;
};

struct BankingConfig {
    float base_interest_rate = 0.05f;  // 5% from economy.json
    float denial_dti_threshold = 0.43f;
    float max_loan_multiple = 5.0f;
    float min_credit_score = 0.30f;
    float inflation_target = 0.02f;
    float per_tick_base_interest_rate = 0.000027f;  // ~base_interest_rate/365/5
    float credit_risk_spread = 0.000082f;
    float collateral_rate_discount = 0.000014f;
    uint32_t default_grace_ticks = 3;
    float credit_score_payment_gain = 0.002f;
    float credit_score_miss_penalty = 0.01f;
    float max_loan_multiple_of_income = 36.0f;
    float criminal_conviction_penalty = 0.20f;
    float per_tick_denial_dti_threshold = 0.40f;  // per-tick DTI threshold from Constants
};

struct NpcBehaviorConfig {
    float motivation_financial_security = 0.25f;
    float motivation_social_standing = 0.15f;
    float motivation_personal_safety = 0.20f;
    float motivation_power_influence = 0.10f;
    float motivation_ideology = 0.10f;
    float motivation_loyalty = 0.10f;
    float motivation_self_preservation = 0.10f;
    float memory_decay_rate = 0.002f;
    float memory_decay_floor = 0.01f;
    float knowledge_confidence_decay_rate = 0.001f;
    float motivation_shift_rate = 0.001f;
    float risk_tolerance_default = 0.5f;
    float base_wage = 50.0f;
    // Informal/subsistence labor floor: there is always SOME work for willing
    // bodies (day labor, subsistence, barter). Work pays at least this fraction
    // of base_wage regardless of the formal employment rate — without it, the
    // wage = base_wage × employment_rate coupling is a death spiral (low
    // employment → worthless work → mass inaction → lower employment), and
    // unemployment pins at 1.0, which never happens in real economies.
    float informal_wage_floor = 0.30f;
    float base_illicit_income = 80.0f;
    float shop_cost_fraction = 0.05f;
    uint32_t memory_log_cap = 500;
};

struct RelationshipConfig {
    float decay_rate_per_30_ticks = 0.01f;
    float trust_decay_rate_per_batch = 0.02f;
    float fear_decay_rate_per_batch = 0.03f;
    uint32_t max_per_npc = 100;
};

struct InformantConfig {
    float base_flip_rate = 0.005f;  // spec: 0.005; hardcoded was 0.10f
    float max_flip_probability = 0.20f;
    float risk_factor_scale = 0.30f;
    float trust_factor_scale = 0.25f;
    float incrimination_suppression = 0.08f;
    float compartment_bonus_per_level = 0.05f;
    float pay_silence_cost = 50000.0f;
    float violence_multiplier = 3.0f;
    // Disclosure (informant flip) tuning — see informant_system INTERFACE.md.
    // Only knowledge with confidence above this threshold is disclosed.
    float disclosure_confidence_threshold = 0.40f;
    // Disclosed token actionability = confidence * cooperation_actionability_scale.
    float cooperation_actionability_scale = 0.80f;
    // InvestigatorMeter fill per disclosed token = actionability * this.
    float meter_fill_per_disclosure = 0.15f;
};

struct LegalProcessConfig {
    float conviction_threshold = 0.50f;
    float defense_quality_factor = 0.40f;
    uint32_t ticks_per_severity = 365;
    uint32_t double_jeopardy_cooldown = 1825;
    uint32_t charge_to_trial_min = 90;
    uint32_t charge_to_trial_max = 365;

    // --- v7 state-machine thresholds (legal_process INTERFACE.md) ---
    // Evidence-weight thresholds gating stage transitions.
    float arrest_evidence_threshold = 0.35f;     // investigation -> arrested
    float charge_evidence_threshold = 0.55f;     // arrested -> charged
    float dismissal_evidence_threshold = 0.25f;  // arrested/charged -> acquitted
    // Time-in-stage minimums (ticks).
    uint32_t investigation_to_charge_ticks = 60;  // ~2 months arrested -> charged
    uint32_t charge_to_trial_ticks = 180;         // ~6 months charged -> trial
    // Sentencing branch: severity-floor for custodial vs fine outcome.
    // CaseSeverity::serious = enum value 2 = severity 3 in spec terminology
    // (severity = static_cast<uint32_t>(enum) + 1).
    uint32_t custodial_sentence_severity_floor = 3;
    // Parole: defendant eligible once this fraction of sentence has been served.
    float parole_eligibility_fraction = 0.50f;
    // Bail amount auto-posted by player defendants with sufficient liquidity.
    // Reduces player wealth on post; no refund modelled at this stage.
    float bail_amount = 50000.0f;
    // Fine amount for non-custodial convictions (severity below floor).
    // Applied as a one-shot wealth deduction on transition convicted -> fined.
    float fine_amount_per_severity = 10000.0f;
    // Additive bump to the public_arrest_record EvidenceToken's weight on
    // arrest. Per player.h:159, "exposure" is not a scalar — it's the set
    // of evidence tokens the player is aware of. This config models the
    // PR damage of being publicly arrested on top of the underlying
    // case's evidence: token_weight = clamp(case.evidence_weight +
    // arrest_exposure_hit, 0.0, 1.0).
    float arrest_exposure_hit = 0.15f;
};

struct EvidenceConfig {
    float base_decay_rate = 0.002f;
    float actionability_floor = 0.10f;
    uint32_t batch_interval = 7;
    float credibility_threshold = 0.30f;
    float share_trust_threshold = 0.45f;
    float batch_decay_amount = 0.014f;
    float discredit_decay_multiplier = 5.0f;
    float trust_factor_min = 0.1f;
    float trust_factor_max = 1.0f;
    float criminal_evidence_actionability = 0.80f;
    float violation_evidence_actionability = 0.60f;
};

struct SafetyCeilingsConfig {
    // npc_capital_ceiling / business_cash_ceiling are NOT wealth caps — wealth is
    // bounded by the economy, not an arbitrary number (real fortunes have no
    // ceiling). They are used only as crash sentinels: non-finite (inf/NaN) values
    // are clamped to them so a genuine bug can't poison the sim. apply_deltas floors
    // capital at 0 but applies no upper magnitude cap.
    float npc_capital_ceiling = 1.0e9f;
    float business_cash_ceiling = 1.0e10f;
    float market_supply_ceiling = 1.0e8f;
    float market_price_ceiling = 1.0e6f;
    // Per-tick business revenue ceiling. Without it, revenue is unbounded
    // (output up to market_supply_ceiling × price up to market_price_ceiling can
    // reach ~1e14), which fed a wealth runaway: huge revenue -> a revenue-scaled
    // bank loan + owner draws -> owner wealth -> demand -> prices -> more revenue.
    // Normal business revenue is ~1e3/tick, so 1e7 (10,000x) does not constrain
    // legitimate growth while cutting the runaway off at the source.
    float business_revenue_ceiling = 1.0e7f;
};

struct RandomEventsConfig {
    float base_rate = 0.15f;
    float climate_event_amplifier = 1.5f;
    float instability_event_amplifier = 1.0f;
    float evidence_severity_threshold = 0.3f;
    float weight_natural = 0.25f;
    float weight_accident = 0.20f;
    float weight_economic = 0.30f;
    float weight_human = 0.25f;
    float natural_agri_mod_min = -0.40f;
    float natural_agri_mod_max = -0.05f;
    float natural_infra_dmg_min = 0.01f;
    float natural_infra_dmg_max = 0.15f;
    float accident_output_rate_min = -1.0f;
    float accident_output_rate_max = -0.10f;
    float accident_infra_dmg_min = 0.01f;
    float accident_infra_dmg_max = 0.05f;
    float economic_price_shift_min = 0.10f;
    float economic_price_shift_max = 0.40f;
};

struct InfluenceNetworkConfig {
    float trust_classification_threshold = 0.40f;
    float fear_classification_threshold = 0.35f;
    float fear_trust_ceiling = 0.20f;
    float catastrophic_trust_loss_threshold = -0.55f;
    float catastrophic_trust_floor = 0.10f;
    float recovery_ceiling_factor = 0.60f;
    float recovery_ceiling_minimum = 0.15f;
    float obligation_erosion_rate = 0.001f;
    float trust_weight = 0.35f;
    float obligation_weight = 0.25f;
    float fear_weight = 0.20f;
    float movement_weight = 0.20f;
    float diversity_bonus = 0.05f;
    uint32_t health_target_count = 10;
};

struct PoliticalCycleConfig {
    float support_threshold = 0.55f;
    float oppose_threshold = 0.35f;
    float majority_threshold = 0.50f;
    float resource_scale = 2.0f;
    float resource_max_effect = 0.15f;
    float event_modifier_cap = 0.20f;
    // A campaign auto-activates this many ticks before an office's election.
    uint32_t campaign_lead_time_ticks = 90;

    // --- National legitimacy roll-up (unrest response, §14/§15) ---
    // national_legitimacy aggregates provincial conditions (population-weighted),
    // EMA-smoothed. Weights need not sum to 1; the raw score is clamped to [0,1].
    float legitimacy_trust_weight = 0.35f;         // + institutional_trust
    float legitimacy_stability_weight = 0.45f;     // + stability_score
    float legitimacy_grievance_weight = 0.50f;     // - grievance_level
    float legitimacy_unemployment_weight = 0.30f;  // - unemployment_rate
    float legitimacy_ema_alpha = 0.05f;            // smoothing toward target per tick

    // --- Regime-differentiated unrest response (calibration doc) ---
    // Fired on a monthly cadence when national_legitimacy < crisis threshold.
    float legitimacy_crisis_threshold = 0.30f;  // below this the state must respond
    // Autocracy — suppression.
    float suppression_grievance_immediate = 0.15f;   // short-term dispersal (grievance cut)
    float suppression_grievance_floor_rise = 0.06f;  // martyr ratchet per crackdown
    float suppression_legitimacy_hit = 0.05f;        // legitimacy bleed per crackdown
    float collapse_legitimacy_floor = 0.08f;         // only the most illegitimate regimes fall
    uint32_t collapse_repression_count = 8;          // sustained crackdowns -> FailedState
    // Democracy / Federation — accountability.
    float crisis_approval_hit = 0.08f;          // incumbent approval craters per month in crisis
    float concession_grievance_relief = 0.10f;  // policy relief in worst provinces per month
    float concession_trust_restore = 0.02f;     // responsiveness rebuilds institutional trust
    uint32_t concession_province_count = 2;     // concessions target the worst-off provinces
    // FailedState — fragmentation.
    float failed_state_dominance_rise = 0.02f;  // criminal economy fills the vacuum per month
};

struct MediaSystemConfig {
    float cross_outlet_pickup_rate = 0.15f;
    float cross_outlet_amplification_factor = 0.50f;
    float social_amplification_multiplier = 2.50f;
    float exposure_per_amplification_unit = 0.02f;
    float crisis_evidence_threshold = 0.40f;
    float owner_suppression_base_rate = 0.50f;
    uint32_t propagation_window_ticks = 90;
    float editorial_independence_journalist_bonus = 0.30f;
};

struct CurrencyExchangeConfig {
    float trade_balance_weight = 0.30f;
    float inflation_weight = 0.40f;
    float sovereign_risk_weight = 0.30f;
    float peg_break_reserve_threshold = 0.15f;
    float floor_fraction = 0.20f;
    float ceiling_fraction = 5.00f;
    float fx_transaction_cost = 0.01f;
};

struct TradeInfrastructureConfig {
    float mode_speed_road = 800.0f;
    float mode_speed_rail = 700.0f;
    float mode_speed_sea = 900.0f;
    float mode_speed_river = 450.0f;
    float mode_speed_air = 10000.0f;
    float terrain_delay_coeff = 0.4f;
    float infra_delay_coeff = 0.6f;
    float max_concealment_modifier = 0.40f;
    float perishable_decay_base = 0.01f;
};

struct InvestigatorEngineConfig {
    float facility_count_normalizer = 5.0f;
    float detection_to_fill_rate_scale = 0.005f;
    float fill_rate_max = 0.01f;
    float personnel_violence_multiplier = 3.0f;
    float surveillance_threshold = 0.30f;
    float formal_inquiry_threshold = 0.60f;
    float raid_threshold = 0.80f;
    float warrant_trust_min = 0.30f;
    float decay_rate = 0.001f;
    float default_corruption_susceptibility = 0.5f;
};

struct NpcBusinessConfig {
    float cash_critical_months = 2.0f;
    float cash_comfortable_months = 3.0f;
    float cash_surplus_months = 5.0f;
    float exit_market_threshold = 0.05f;
    float exit_probability = 0.30f;
    float expansion_return_threshold = 0.15f;
    uint32_t ticks_per_quarter = 90;
    uint32_t dispatch_period = 30;
    float quality_player_rd_rate = 0.08f;
    float fast_expander_rd_rate = 0.05f;
    float cost_cutter_layoff_fraction = 0.10f;
    float board_captured_threshold = 0.25f;
    float board_risky_block_threshold = 0.70f;
    // Organic growth: a profitable legit business that reinvests compounds its
    // output capacity (and thus revenue) each strategic decision; a loss-making one
    // contracts. This is what lets successful legit enterprise build owner wealth —
    // a dispersed legit upper class — instead of leaving crime the only path to
    // riches. Per-decision (≈quarterly) multipliers; revenue is clamped to the
    // safety ceiling on apply. Facility-based firms have revenue recomputed by
    // production each tick, so this persists only for the abstract firms it targets.
    float organic_growth_rate = 0.10f;   // profitable + expanding: +10% per decision
    float organic_decline_rate = 0.06f;  // loss-making: -6% per decision
};

// ---------------------------------------------------------------------------
// BusinessLifecycleConfig — era-driven stranded-asset penalties and new entrant spawning.
// Loaded from business_lifecycle.json.
// ---------------------------------------------------------------------------
struct StrandedSectorEntry {
    BusinessSector sector = BusinessSector::energy;
    float revenue_penalty = 0.0f;  // fractional reduction on revenue_per_tick
    float cost_increase = 0.0f;    // fractional increase on cost_per_tick
};

struct EmergingSectorEntry {
    BusinessSector sector = BusinessSector::technology;
    float spawn_fraction = 0.0f;  // fraction of province biz count to spawn
    BusinessProfile profile = BusinessProfile::fast_expander;
};

struct BusinessLifecycleConfig {
    // Maps target_era (uint8_t 2–10) → sectors penalised on transition into that era.
    std::map<uint8_t, std::vector<StrandedSectorEntry>> stranded_sectors;
    // Maps target_era (uint8_t 2–10) → sectors that spawn new entrants on that era.
    std::map<uint8_t, std::vector<EmergingSectorEntry>> emerging_sectors;
    // Revenue floor: no single-era shock can drop a business below this fraction
    // of its pre-transition revenue (prevents instant death from stacking penalties).
    float stranded_revenue_floor = 0.20f;

    // --- Continuous opportunity-driven firm genesis ---
    // Businesses are born from unmet local opportunity, not only at era
    // transitions. A province supports about (resident named-NPCs /
    // firms_per_resident_denominator) legitimate firms — the same ~1-per-10
    // density world gen seeds (SettlementGenerator::create_businesses). A
    // founding-seed world (zero firms) therefore bootstraps toward the same
    // equilibrium the full seed starts at, while an already-seeded world sits
    // at saturation and genesis stays quiet (protecting the emergence baseline).
    // Founders are real residents who commit their own capital — no money from
    // nothing — so firm formation is bound to accumulated local wealth.
    bool genesis_enabled = true;
    uint32_t genesis_cadence_ticks = 30;            // evaluate monthly
    float firms_per_resident_denominator = 10.0f;   // target = residents / this
    float genesis_saturation_deadband = 0.10f;      // spawn only while >10% under target
    float genesis_gap_fill_fraction = 0.10f;        // fill 10% of the unmet gap per evaluation
    float founder_min_capital = 6000.0f;            // a founder needs at least this to start
    float founder_investment_fraction = 0.30f;      // and seeds the firm with this share of it
    // Economic regimes (era_catalog economic_regime) in which the flat per-resident
    // genesis runs. In pre-market regimes (subsistence/barter/...) firms are an
    // anachronism — the economy is livelihoods, not businesses — so genesis is
    // suppressed there until a livelihood realistically becomes a firm (employs
    // non-household labour). Empty = run in every regime (legacy behaviour).
    std::vector<std::string> genesis_active_regimes = {"modern", "near_future", "space_age"};
};

struct GovernmentBudgetConfig {
    uint32_t ticks_per_quarter = 90;
    float infrastructure_decay_per_quarter = 0.01f;
    float infrastructure_investment_scale = 1000000.0f;
    float debt_warning_ratio = 2.0f;
    float debt_crisis_ratio = 4.0f;
    float city_revenue_fraction = 0.25f;
    float corruption_evidence_threshold = 500000.0f;
    float spending_stability_scale = 0.0001f;
    float spending_crime_scale = 0.0001f;
    float spending_inequality_scale = 0.0001f;
    float cohort_mod_working_class = 0.40f;
    float cohort_mod_professional = 0.85f;
    float cohort_mod_corporate = 1.00f;
    float cohort_mod_criminal_adjacent = 0.10f;
    // Property-tax estimate inputs: properties ~ population / household_size,
    // each valued at the province avg_property_value, taxed at the annual rate.
    float household_size = 3.0f;
    float property_tax_annual_rate = 0.005f;

    // --- Progressive personal wealth tax (regime-dependent restoring force) ---
    // Business profit and criminal proceeds credit owner capital every tick with
    // no wealth-proportional outflow, so capital concentrates by default — as it
    // does in the real world. The ONLY thing that pushes it back is a state with
    // the policy and capacity to redistribute. This quarterly progressive tax is
    // that mechanism: it deducts from significant-NPC capital above an exemption
    // at a rate that climbs with wealth, and funds the national budget (the
    // services/welfare that relieve grievance). It is NOT universal — its strength
    // is scaled per nation by government type (see wealth_tax_redistribution_*),
    // so an accountable welfare state bounds concentration while a kleptocratic
    // autocracy or a collapsed state does not. There is no automatic march toward
    // equality; redistribution happens only where institutions enforce it.
    float wealth_tax_exemption = 1.0e5f;            // capital below this is untaxed
    float wealth_tax_base_rate = 0.05f;             // annual marginal rate at the exemption
    float wealth_tax_max_rate = 0.75f;              // annual marginal rate cap for the very rich
    float wealth_tax_progressivity_scale = 1.0e6f;  // taxable wealth at which the cap is reached
    // Per-regime redistribution strength: a [0,1] multiplier on the wealth tax,
    // reflecting fiscal capacity + political will to tax the powerful. Real-world
    // spread: Nordic-style social democracy redistributes heavily; a federation of
    // accountable states likewise; a kleptocratic autocracy lets elites capture
    // the state (nominal taxes, unenforced on the rich); a failed state has no
    // functioning fiscal apparatus at all. Indexed by GovernmentType value
    // (0 Democracy, 1 Autocracy, 2 Federation, 3 FailedState).
    float wealth_tax_redistribution_democracy = 1.00f;
    float wealth_tax_redistribution_autocracy = 0.25f;
    float wealth_tax_redistribution_federation = 1.00f;
    float wealth_tax_redistribution_failed_state = 0.00f;

    // --- Regime-dependent rule of law: seizure of illicit (criminal) wealth ---
    // Criminal proceeds are hidden from the taxman, so the income/wealth tax never
    // reaches them — the only thing that bounds an illicit fortune is enforcement,
    // and that varies by regime. A state with real rule of law seizes proceeds of
    // crime (asset forfeiture); a kleptocratic autocracy shields its criminal-
    // political elite; a failed state has no enforcement apparatus at all. This
    // quarterly seizure strips a fraction of criminal-role NPCs' capital above an
    // exemption, scaled per nation by the rule_of_law factor; proceeds fund the
    // national budget. Criminal NPCs pay this INSTEAD of the wealth tax (their
    // wealth is exposed to enforcement, not to the revenue service).
    float criminal_seizure_exemption = 1.0e5f;   // illicit capital below this is below notice
    float criminal_seizure_annual_rate = 0.60f;  // annual fraction seized at full rule of law
    float rule_of_law_democracy = 1.00f;
    float rule_of_law_autocracy = 0.30f;  // elite capture: the connected go untouched
    float rule_of_law_federation = 1.00f;
    float rule_of_law_failed_state = 0.00f;

    // --- Individual avoidance: the law does not bind equally ---
    // A significant NPC with a fortune is not one of the masses — they have teams
    // (lawyers, accountants, offshore structures, laundering networks) and political
    // protection that shield a fraction of any levy (tax OR seizure). This is
    // deliberately NOT a flat function of wealth: it is composed from the
    // individual's OWN attributes, so two equally-rich actors with different
    // connections, savvy, and role dodge by very different amounts. Each weight is
    // an additive contribution to the shielded fraction, clamped to avoidance_max.
    // Applied as (1 - avoidance) on the computed quarterly levy.
    float avoidance_max = 0.85f;  // cap — even the best-shielded pay something
    float avoidance_base = 0.0f;
    float avoidance_w_social = 0.25f;     // social_capital: lobbyists, officials, access
    float avoidance_social_norm = 80.0f;  // social_capital scale for normalization
    float avoidance_w_contacts = 0.15f;   // contact_ids: fixers/accountants/bankers on call
    float avoidance_contacts_norm = 10.0f;
    float avoidance_w_risk = 0.10f;    // appetite for aggressive schemes
    float avoidance_w_money = 0.10f;   // financial_gain motivation weight
    float avoidance_w_wealth = 0.15f;  // affords better teams — modest, not the whole story
    float avoidance_wealth_threshold = 1.0e6f;  // shielding capacity ramps above a millionaire
    float avoidance_wealth_scale = 1.0e8f;
    float avoidance_w_innate = 0.15f;      // stable per-NPC aptitude (cunning, inherited advisors)
    float avoidance_role_enabler = 0.30f;  // accountant / lawyer / banker / fixer
    float avoidance_role_corporate = 0.20f;  // corporate_executive: corporate structures
    float avoidance_role_criminal = 0.25f;   // criminal_operator: laundering networks
    float avoidance_role_political =
        0.20f;  // politician / judge / official / regulator: protection
};

struct HealthcareConfig {
    float base_recovery_rate = 0.001f;
    float critical_health_threshold = 0.30f;
    float treatment_health_boost = 0.25f;
    float overload_threshold = 0.85f;
    float overload_quality_penalty = 0.999f;
    float labour_impairment_threshold = 0.50f;
    float labour_supply_impact = 0.80f;
    float capacity_per_treatment = 0.001f;
};

struct SceneCardsConfig {
    uint32_t max_scene_cards_per_tick = 5;
    float trust_weight = 0.7f;
    float risk_weight = 0.3f;
};

struct CommodityTradingConfig {
    float market_impact_threshold = 0.05f;
    float market_impact_coefficient = 0.01f;
    float capital_gains_tax_rate = 0.15f;
};

struct PriceEngineConfig {
    float supply_floor = 0.01f;
    float default_price_adjustment_rate = 0.10f;
    float max_price_change_per_tick = 0.25f;
    float export_floor_coeff = 0.40f;
    float import_ceiling_coeff = 3.0f;
    float default_base_price = 1.0f;
};

// Commons subsistence (pre-market food production). Drives the SubsistenceModule:
// in subsistence/barter-regime eras the whole province population produces food
// directly from the land (no firms, no markets), and the surplus over need is the
// master variable that later frees labour for specialists and trade.
struct SubsistenceConfig {
    // Economic regimes (era_catalog economic_regime) in which the commons food path
    // is active. In any other regime the module is inert (markets feed the population).
    std::vector<std::string> active_regimes = {"subsistence", "barter"};

    // Per-head food need per tick (same unit as the food output below).
    float per_capita_food_per_tick = 1.0f;

    // Natural-capital weights: how much each province endowment contributes to the
    // food-carrying ceiling. Food potential is a labour-worked draw on these.
    float weight_agricultural_productivity = 1.0f;  // farmland / soil fertility
    float weight_arable_land = 0.5f;                 // arable fraction
    float weight_forest_forage = 0.3f;               // forageable biomass
    float weight_fisheries = 0.4f;                   // fish stock

    // Carrying ceiling scale: max food a province's natural capital can yield is
    // capacity_per_unit * (weighted natural capital). Output approaches this ceiling
    // as labour grows (diminishing returns), so fixed land caps how many it feeds.
    float ceiling_per_capital_unit = 4000.0f;
    // Labour at which output reaches ~63% of the natural-capital ceiling (1 - 1/e).
    float labor_half_saturation = 1500.0f;

    // Proto-capital: the food surplus *above* subsistence is stored (grain, herds,
    // tools) and controlled by the province's resident heads/founders — the origin
    // of capital and inequality. Each tick a pool of proto_capital_rate * (surplus
    // food beyond need) accrues, split among resident significant NPCs. This is the
    // wealth that later funds the first firms (founder-capital-gated genesis). 0
    // disables it. Active only in the commons regimes (same gate as production).
    float proto_capital_rate = 0.02f;

    // Specialization: the share of a province's resident livelihoods that surplus
    // can free from food work into Layer-2 specialists (artisan/healer/trader/...).
    // 0 at surplus <= 1 (all hands needed for food), rising with surplus to this cap.
    float max_specialist_fraction = 0.5f;
    // Persistent elite floor: a province keeps at least this many Layer-2 specialists
    // even when surplus is thin or negative — the sticky literate/ruling core (temples,
    // elder councils, scholars) a society maintains through lean years before it would
    // ever dissolve them. Because knowledge-keepers lead the Layer-2 list, this floor
    // keeps the knowledge engine alive through famines/overshoot-crashes, so a stalled
    // society keeps creeping forward and can lift its carrying ceiling back out of the
    // trap rather than dying there. Commons path only (Layer-2 is inert in market eras).
    uint32_t commons_min_specialists_per_province = 2;

    // Knowledge -> productivity (the Malthusian escape): accumulated knowledge (from
    // scholars) raises how many people a given land can feed, letting a society grow
    // AND keep a surplus instead of equilibrating at bare subsistence. The boost
    // SATURATES with diminishing returns —
    //   ceiling *= 1 + knowledge_productivity_max * K / (K + knowledge_productivity_halfsat)
    // — so it tends toward a realistic ceiling (modern farming feeds ~20-30x the
    // neolithic per unit land, not 1000x). A linear coupling instead explodes the
    // ceiling at high knowledge, blowing population past any demographic brake and
    // collapsing the surplus (and with it the specialist class) in the upper eras.
    // max = total multiplier headroom; halfsat = knowledge at half the boost. 0 max
    // disables the coupling.
    float knowledge_productivity_max = 26.0f;
    float knowledge_productivity_halfsat = 35000.0f;

    // Seasonality (climate swing) reduces food reliability — lean seasons cut the
    // harvest. Applied RELATIVE to Earth's seasonality, so an earthlike world is
    // neutral; harsher swings penalize food, gentler ones help. (Gravity does NOT
    // affect food — it acts on falls/structures, not the harvest.)
    float seasonality_food_penalty = 0.5f;
};

// Knowledge engine: scholars/scribes turn surplus into accumulated knowledge, which
// (via subsistence productivity + era thresholds) lets a society escape the
// Malthusian trap and move forward. All pre-market; modern tech uses the tech module.
struct KnowledgeConfig {
    // Economic regimes in which the knowledge engine runs (it is otherwise inert).
    std::vector<std::string> active_regimes = {"subsistence", "barter", "mercantile", "industrial"};
    float production_scalar = 0.4f;  // knowledge/year per unit of scholar knowledge_output
    // Annual attrition. Civilizational knowledge is CUMULATIVE — it is essentially
    // never lost on historical timescales (dark-age regression is a rare exception, not
    // the rule, and is out of V1 scope). Proportional decay imposes an equilibrium
    // ceiling (level -> production/decay); kept this tiny so even the dawn's thin
    // knowledge trickle accumulates far past every era threshold, i.e. the era
    // THRESHOLDS — not a decay ceiling — govern the pace of the climb.
    float decay_per_year = 0.00001f;
};

struct SeasonalAgricultureConfig {
    uint32_t ticks_per_year = 365;
    uint32_t planting_duration_ticks = 7;
    uint32_t harvest_duration_ticks = 14;
    float fallow_soil_recovery_rate = 0.003f;
    float soil_health_max = 1.0f;
    float soil_health_min_monoculture = 0.5f;
    uint16_t monoculture_penalty_threshold = 3;
    float monoculture_soil_penalty_rate = 0.002f;
    uint32_t southern_hemisphere_offset = 182;
    float perennial_base = 0.85f;
    float perennial_amplitude = 0.25f;
    float livestock_base = 0.85f;
    float livestock_amplitude = 0.10f;
    float timber_multiplier = 1.0f;

    // --- Fisheries (Schaefer surplus-production model) ---
    // Coastal/freshwater provinces fish their stock each tick: logistic growth
    // (r·N·(1−N/K)) replenishes it; fishing effort harvests a fraction. Effort
    // below the intrinsic growth rate is sustainable; push it higher (config/mods)
    // and the stock is overfished toward collapse — the shared-access problem.
    // current_stock and carrying_capacity are normalized [0,1]; catch is scaled to
    // tonnes of fish_wild for the market.
    float fishing_effort = 0.06f;          // fraction of stock harvested per tick
    float fishing_catch_to_tonnes = 5000.0f;  // normalized-stock → fish_wild tonnes
};

struct RealEstateConfig {
    float residential_yield_rate = 0.003f;
    float commercial_yield_rate = 0.004f;
    float industrial_yield_rate = 0.005f;
    float price_convergence_rate = 0.05f;
    uint32_t convergence_interval = 30;
    float criminal_dominance_penalty = 0.15f;
    float laundering_premium = 0.10f;
    float transaction_evidence_threshold = 50000.0f;
    // Homeless-rate accounting (drives cohort_stats.homeless_rate via
    // RegionDelta.homeless_rate_delta). An NPC is considered "housed" when
    // their capital covers `homeless_rent_buffer_months` of the province's
    // mean residential rent. Provinces with no residential listings fall
    // back to the configured baseline rent_floor for the comparison.
    float homeless_rent_buffer_months = 3.0f;
    float homeless_rent_floor = 50.0f;
    float homeless_rate_convergence = 0.05f;

    // Phase 1 market — symmetric at-asking cash, instant settle.
    // NPCs opportunistically buy player-listed properties when the
    // asking price is meaningfully below the property's market value
    // (a deal). Per-tick scan: for each player-listed property with
    // asking_price < market_value * npc_buyer_deal_max_ratio, evaluate
    // NPCs in same province with capital >= asking_price; deal_strength
    // = (1 - asking/market), each candidate NPC rolls
    //   p = deal_strength * npc_opportunistic_buy_rate
    // against the per-tick deterministic RNG. First successful roll
    // (in NPC id ascending order) wins. Below-market listings are the
    // only buy trigger in Phase 1; market-rate or above-market
    // listings produce no NPC buyers (extends in Phase 4 with
    // demand-driven NPC offers and counter-offer flow).
    float npc_buyer_deal_max_ratio = 0.90f;    // asking/market under this = "a deal"
    float npc_opportunistic_buy_rate = 0.02f;  // base rate (multiplied by deal_strength)

    // Phase 2 — multi-tick close delays per PropertyType. A buy accepted
    // at-or-above asking creates a PendingTransaction whose close_tick is
    // (current_tick + delay). The transaction settles when real_estate's
    // global post-pass observes close_tick <= current_tick, transferring
    // ownership and money. Defaults model rough due-diligence time:
    // residential closes fast, commercial/industrial slower.
    uint32_t close_delay_residential = 7;
    uint32_t close_delay_commercial = 30;
    uint32_t close_delay_industrial = 30;

    // Phase 3 — relationship-driven negotiation on below-asking offers.
    // The seller's accept_score is:
    //     price_ratio (offer/market_value)
    //   + trust_accept_weight  * relationship.trust   (-1..1)
    //   + fear_accept_weight   * relationship.fear    ( 0..1)
    //   + capital_pressure_weight * (1 - capital / distress_capital_threshold)
    // p_accept = sigmoid((accept_score - 1.0) * sigmoid_steepness)
    // A trusted friend with a desperate NPC seller can buy at a steep
    // discount; a stranger gets no discount; rivals or wealthy holders
    // hold out for full price.
    float trust_accept_weight = 0.20f;
    float fear_accept_weight = 0.15f;
    float capital_pressure_weight = 0.30f;
    float distress_capital_threshold = 50000.0f;  // NPCs below this = 100% pressure
    float sigmoid_steepness = 6.0f;

    // Phase 3 — NPC offers on player listings (below asking, scene-card
    // delivered). Fires when the listing is NOT a sub-90%-of-market deal
    // (those still get the at-asking opportunistic buy). The NPC's offer
    // is a uniform draw in [market * npc_offer_min_ratio,
    // asking * npc_offer_max_ratio]; the SceneCard exposes accept/decline.
    // Player has negotiation_deadline_ticks to respond before the
    // context expires.
    float npc_offer_base_rate = 0.005f;
    float npc_offer_min_ratio = 0.85f;
    float npc_offer_max_ratio = 0.95f;
    uint32_t negotiation_deadline_ticks = 14;

    // Counter-offers: when an NPC seller rejects a serious below-asking
    // cash offer (offer >= asking × negotiation_counter_min_ratio), it
    // counters at offer + (asking − offer) × negotiation_counter_split
    // via a SceneCard the player can accept or decline. Closes the
    // negotiation loop without free numeric input.
    float negotiation_counter_min_ratio = 0.70f;
    float negotiation_counter_split = 0.50f;

    // Phase 4 — mortgage financing. Down-payment minimums per property
    // type (offers with down_payment_fraction below the floor are
    // rejected at the drain). Player-mortgage approval uses banking's
    // min_credit_score_for_purpose (LoanPurpose::property_purchase)
    // and a player-specific max-loan rule:
    //     max_loan = player.wealth × player_max_loan_multiplier_of_wealth
    // The loan's repayment cadence uses mortgage_term_ticks and the
    // banking-supplied compute_repayment_per_tick.
    float min_down_payment_residential = 0.10f;
    float min_down_payment_commercial = 0.25f;
    float min_down_payment_industrial = 0.35f;
    float player_max_loan_multiplier_of_wealth = 10.0f;
    float mortgage_interest_rate = 0.00025f;  // per-tick rate ≈ ~9% annual
    uint32_t mortgage_term_ticks = 10950u;    // ~30 years at 365 ticks/year

    // Phase 6 — auctions. Bank-foreclosed properties auto-open an
    // auction with reserve = market_value × auction_reserve_fraction,
    // running for auction_duration_ticks. Each tick, NPCs in the
    // property's province with sufficient capital evaluate open
    // auctions: per-NPC bid probability = npc_auction_bid_rate; a
    // bidding NPC raises the current high by a
    // npc_auction_bid_increment fraction (of the current high, or of
    // the reserve when there are no bids yet). Player bids arrive via
    // PlaceAuctionBidAction. At close_tick the high bid wins if it
    // meets reserve, else the auction closes_no_reserve (consigner
    // keeps the asset).
    float auction_reserve_fraction = 0.70f;  // reserve = market_value × this
    uint32_t auction_duration_ticks = 30u;
    float npc_auction_bid_rate = 0.05f;         // per-NPC per-tick bid probability
    float npc_auction_bid_increment = 0.05f;    // raise fraction over current high
    float npc_auction_max_value_ratio = 1.10f;  // NPC won't bid above market × this

    // Phase 7 — raw land + zoning. A RequestZoningChangeAction is
    // decided by a deterministic local-government roll. A "minor"
    // change (between residential/commercial) approves at
    // zoning_minor_approval_prob; a "major" change (anything involving
    // industrial, or developing raw_land into a built class) approves at
    // zoning_major_approval_prob. Province criminal_dominance_index acts
    // as a capture/bribery proxy that *raises* approval odds by up to
    // zoning_corruption_bonus. On approval the property's market_value
    // is nudged toward the target class baseline by
    // zoning_revaluation_rate. Land subtype base values (per hectare)
    // seed market_value when raw_land parcels are generated.
    float zoning_minor_approval_prob = 0.60f;
    float zoning_major_approval_prob = 0.25f;
    float zoning_corruption_bonus = 0.30f;
    float zoning_revaluation_rate = 0.20f;

    // Phase 8 — subdivision. A subdivisible building can be split into
    // [subdivision_min_units, subdivision_max_units] child units. Each
    // child is valued at (parent_value / n) × subdivision_unit_premium —
    // individually-sellable units carry a small premium over their pro
    // rata share. Re-merge sums the live children's values back into the
    // parent.
    uint32_t subdivision_min_units = 2u;
    uint32_t subdivision_max_units = 100u;
    float subdivision_unit_premium = 1.10f;

    // Phase 10 — business acquisition. Offer price = revenue_per_tick ×
    // acquisition_ticks_per_month × offer_multiple. The owner accepts
    // with probability logistic(((offer_multiple / fair_multiple) - 1 +
    // trust × trust_accept_weight) × sigmoid_steepness). On acceptance a
    // PendingBusinessAcquisition settles after
    // acquisition_due_diligence_ticks. Mortgage/mixed finances against
    // the target business (collateral_id = business_id).
    float acquisition_fair_multiple = 6.0f;
    uint32_t acquisition_ticks_per_month = 30u;
    uint32_t acquisition_due_diligence_ticks = 60u;
    float acquisition_min_down_payment = 0.40f;  // floor for financed business buys

    // Phase 11 — construction. A RequestConstructionBidsAction opens a
    // contract; every construction-sector firm in the parcel's province
    // submits a bid priced at construction_base_cost × (1 + margin),
    // where margin is drawn deterministically in
    // [construction_margin_min, construction_margin_max] and scaled up by
    // construction_remote_cost_multiplier for LocationFlag_Remote
    // parcels. A player-owned contractor bids at zero margin (internal
    // cost). completion_ticks is construction_base_ticks. On award the
    // bid amount is escrowed; the facility is delivered at completion and
    // the contractor's business is paid. If the client owns no business
    // in the province, one is auto-created to hold the new facility.
    float construction_base_cost = 200000.0f;
    uint32_t construction_base_ticks = 90u;
    float construction_margin_min = 0.10f;
    float construction_margin_max = 0.35f;
    float construction_remote_cost_multiplier = 1.50f;
    uint32_t construction_default_bidding_window = 14u;

    // Phase 12 — property tax (assessed every property_tax_quarter_ticks
    // against the owner; offshore parcels exempt). Annual rate is divided
    // by 4 per quarterly assessment. Owners who can't pay accrue
    // unpaid_tax_balance and a delinquency count. Phase 13 — after
    // tax_lien_quarters delinquent quarters a lien is filed (blocks
    // voluntary sale); after tax_sale_quarters the parcel goes to a
    // government-consigned auction with reserve = unpaid balance.
    float property_tax_annual_residential = 0.005f;
    float property_tax_annual_commercial = 0.010f;
    float property_tax_annual_industrial = 0.015f;
    float property_tax_annual_raw_land = 0.001f;
    uint32_t property_tax_quarter_ticks = 90u;
    uint8_t tax_lien_quarters = 2u;
    uint8_t tax_sale_quarters = 4u;
};

struct FinancialDistributionConfig {
    uint32_t ticks_per_quarter = 91;
    uint32_t deferred_salary_max_ticks = 30;
    float draw_reporting_threshold = 20000.0f;
    uint32_t ticks_per_month = 30;
    float cash_surplus_months = 5.0f;
    float board_rubber_stamp_threshold = 0.3f;
    float board_approval_bonus_threshold = 0.25f;
    float default_tax_withholding_rate = 0.20f;
    float owners_draw_fraction = 0.5f;
    float wage_theft_emotional_weight = -0.6f;
};

struct NpcBehaviorModuleConfig {
    // Inaction gate, calibrated to the achievable EV scale (GDD §3: routine
    // decisions pick the highest-utility action; `waiting` is an edge state).
    // EV = motivation_weight × probability × magnitude: a balanced NPC's best
    // candidate lands at ~0.05 in normal conditions and ~0.11 at the
    // financially-driven extreme. At 0.03, NPCs act in normal conditions and
    // only idle when conditions are dismal AND their motivation profile poorly
    // matches the available actions. (The previous 0.10 sat ABOVE the entire
    // achievable scale, so ~96% of NPCs fell to `waiting` from day one.)
    float inaction_threshold = 0.03f;
    float min_risk_discount = 0.05f;
    float risk_sensitivity_coeff = 2.0f;
    float trust_ev_bonus = 0.3f;
    float recovery_ceiling_minimum = 0.15f;
};

struct ObligationNetworkConfig {
    float escalation_rate_base = 0.001f;
    float escalation_threshold = 1.5f;
    float critical_threshold = 3.0f;
    float hostile_action_threshold = 0.7f;
    float wealth_reference_scale = 1000000.0f;
    float max_wealth_factor = 2.0f;
    float trust_erosion_per_tick = -0.001f;
    uint32_t orphan_obligation_timeout_ticks = 180;
};

struct CriminalOperationsConfig {
    uint32_t quarterly_interval = 90;
    float le_heat_threshold = 0.60f;
    float territory_pressure_conflict_threshold = 0.60f;
    float cash_comfortable_months = 3.0f;
    float cash_low_threshold = 0.50f;
    float territory_pressure_expand_threshold = 0.30f;
    float le_heat_expand_threshold = 0.30f;
    float expansion_initial_dominance = 0.05f;
    float cash_per_expansion_slot = 5000.0f;
    uint32_t min_expansion_team_size = 2;
    float expansion_refund_fraction = 0.50f;
    float dormant_dominance_decay_rate = 0.001f;
    // Starting cash for an organization assembled by the formation bootstrap
    // (one org per province with criminal NPCs). Drives the initial cash_level
    // the strategic-decision matrix reads. Anchored to one full expansion team
    // (cash_per_expansion_slot * min_expansion_team_size = 5000 * 2) so a fresh
    // org can afford exactly one expansion before needing income.
    float initial_org_cash = 10000.0f;
    // Ticks of criminal-business revenue bundled into the dirty_amount of the
    // laundering operation seeded on org formation (a month, ticks_per_month).
    float launder_seed_income_ticks = 30.0f;
};

struct CommunityResponseConfig {
    float ema_alpha = 0.05f;
    float social_capital_max = 100.0f;
    float capital_normalizer = 10000.0f;
    float social_normalizer = 50.0f;
    float memory_decay_floor = 0.01f;
    float grievance_normalizer = 10.0f;
    float grievance_shock_threshold = 0.15f;
    // Grievance = material deprivation (GDD §14.2) + actor-specific wrongs.
    // Without the material grounding, grievance is a pure memory accumulator
    // that saturates to 1.0 regardless of economic reality and never relaxes.
    float grievance_unemployment_weight = 0.70f;  // joblessness is the dominant systemic wrong
    float grievance_inequality_weight = 0.40f;    // inequality drives a slower-burning grievance
    float grievance_memory_weight = 0.40f;        // actor-specific wrongs modulate on top (bounded)
    float resistance_revenue_penalty = -0.15f;
    float trauma_grievance_floor_scale = 0.25f;
    float trauma_trust_ceiling_scale = 0.30f;
    uint32_t regression_cooldown_ticks = 7;
};

struct NpcSpendingConfig {
    float reference_income = 1000.0f;
    float max_income_factor = 5.0f;
    float min_price_factor = 0.05f;
    float default_base_demand_units = 1.0f;
    float default_income_elasticity = 1.0f;
    float default_price_elasticity = -1.0f;
    float default_base_price = 10.0f;
    float default_quality_weight = 0.0f;
    // Civilian waste: a fraction of consumed goods becomes municipal_waste each tick
    // (conservation — consumption does not annihilate matter; it produces garbage
    // that accumulates in the province until handled).
    float civilian_waste_rate = 0.30f;

    // Background-population food subsistence (Part C — demand-side grounding).
    // The whole province population must eat, not just the significant NPCs. Each
    // tick the background population generates inelastic (necessity) demand for the
    // food basket below, sized by head count: food_need = total_population *
    // per_capita_food_per_tick, distributed across the basket proportional to local
    // availability ("eat what's on the shelf"). This demand is fused with NPC demand
    // per market so the two never double-spend the same supply; the population has no
    // individual wallet (cohorts are abstract), so it is not charged cash — the demand
    // signal raises food prices, which is what food producers earn against.
    float per_capita_food_per_tick = 0.0008f;  // subsistence food units per person per tick
    std::vector<std::string> food_basket = {
        "flour",       "rice",          "beef",          "pork",
        "poultry_meat", "dairy_products", "fish_wild",     "fish_farmed",
        "packaged_food", "refined_sugar", "soy_oil_refined"};
};

struct AntitrustConfig {
    float market_share_threshold = 0.40f;
    float dominant_price_mover_threshold = 0.70f;
    float meter_fill_per_threshold_tick = 0.002f;
    float dominance_proposal_pressure_per_tick = 0.005f;
    float proposal_pressure_decay_rate = 0.01f;
    float proposal_threshold = 0.50f;
    uint32_t monthly_interval = 30;
    // Structural remedy: when an antitrust proposal passes, the province's
    // dominant actor is fined this fraction of its capital (civil penalty,
    // not a criminal case).
    float enforcement_fine_capital_fraction = 0.15f;
};

struct FacilitySignalsConfig {
    float default_weight = 0.25f;
    float karst_mitigation_bonus = 0.10f;
    float facility_count_normalizer = 5.0f;
    float detection_to_fill_rate_scale = 0.005f;
    float fill_rate_max = 0.01f;
    float surveillance_threshold = 0.30f;
    float formal_inquiry_threshold = 0.60f;
    float raid_threshold = 0.80f;
    float notice_threshold = 0.25f;
    float audit_threshold = 0.50f;
    float enforcement_threshold = 0.75f;
    float meter_decay_rate = 0.001f;
    float personnel_violence_multiplier = 3.0f;
};

struct ProductionConfig {
    float tech_tier_output_bonus = 0.08f;
    float tech_tier_cost_reduction = 0.05f;
    float tech_quality_ceiling_base = 0.5f;
    float tech_quality_ceiling_step = 0.1f;
    float worker_productivity_diminishing = 0.15f;
    float minimum_input_fraction = 0.1f;
    float informal_price_discount = 0.7f;
    // Yield-modifier inputs (Part C / GDD agriculture model): the fraction of full
    // output a recipe yields when a yield-modifier input (fertilizer for crops, corn
    // feed for livestock) is entirely absent. The subsistence/"hunter-gatherer" base
    // from which the food chain bootstraps before the fertilizer/feed industry exists;
    // applying the modifier input scales output from this floor up to 1.0 (full yield).
    float yield_modifier_floor = 0.4f;

    // --- Energy (electricity as a generated, consumed good) ---
    // Electricity is generated each tick from the province's endowment and consumed
    // by production (recipe.energy_per_tick). Bind energy to physics: renewable
    // capacity (solar/wind/geothermal deposits + hydro) generates electricity
    // matter-free; the shortfall is met by BURNING the province's fossil fuel stock
    // (coal/gas/oil), consuming that matter. If generation can't meet demand,
    // production browns out (output scaled by the supply/demand ratio). Energy-rich
    // provinces run at full output; fossil-dependent ones draw down finite reserves;
    // energy-poor ones are output-limited — the comparative-advantage signal.
    float renewable_mwh_per_capacity = 0.02f;  // electricity per unit deposit capacity·quality
    float hydro_mwh_perennial = 2000.0f;       // hydro generation by river_flow_regime
    float hydro_mwh_seasonal = 800.0f;
    float fossil_mwh_per_fuel_unit = 50.0f;  // electricity per unit fossil fuel burned
    // --- Motive power (mechanical work + process heat) ---
    // Beyond electricity, recipes can require mechanical work (mills, hammers, pumps:
    // recipe.mechanical_per_tick) and process heat (smelting, kilns: recipe.fuel_per_tick).
    // Mechanical is met by water/wind direct-drive (the same non-depleting renewable
    // flows as hydro/wind electricity), with any shortfall by burning fuel (steam).
    // Heat is met by burning fuel. Burning biomass (wood) and fossil consumes that
    // matter — conserved, drawn from the same province stock as electricity generation,
    // so the three power forms compete for one fuel pool. Biomass first (era-appropriate),
    // then fossil. Provinces with no flows and no fuel are power-poor — a real disadvantage,
    // not a hard rail; recipes needing no power (muscle only) are unaffected.
    float biomass_mwh_per_fuel_unit = 15.0f;  // work/heat per unit biomass burned
    float electricity_base_price = 30.0f;

    // --- Waste / byproducts (conservation: processes do not vanish matter; the
    // share not embodied in the product becomes waste that must be handled) ---
    // Production emits waste proportional to output, typed by the output good's
    // category: dirty industries (petroleum/chemicals refining) throw off large
    // volumes of hazardous waste; heavy industry/mining throw off industrial waste
    // and tailings; light/organic industries less. Covers every product via its
    // category — no per-recipe authoring. Waste accumulates in the province (and
    // disperses via the standard surplus decay) until handled.
    float waste_rate_hazardous = 0.35f;   // petroleum, chemicals, pharma → hazardous_waste
    float waste_rate_ewaste = 0.20f;      // electronics → hazardous_waste
    float waste_rate_heavy = 0.25f;       // heavy_industry, metals, geological, vehicles
    float waste_rate_construction = 0.20f;  // construction, structural
    float waste_rate_light = 0.12f;       // food, agricultural, textiles, timber, biological
};

struct RndConfig {
    float maturation_rate_coeff = 0.40f;
    float maturation_difficulty_per_level = 2.0f;
    float base_research_success_rate = 0.75f;
    float domain_knowledge_bonus_coeff = 0.30f;
    float unexpected_discovery_probability = 0.05f;
    float patent_preemption_check_rate = 0.02f;
    float knowledge_decay_rate = 0.0001f;
    float era_transition_threshold = 0.70f;
    uint32_t patent_duration_ticks = 7300;
};

struct ConsequenceDelayConfig {
    uint32_t whistleblower_min = 30;
    uint32_t whistleblower_max = 180;
    uint32_t journalist_invest_min = 14;
    uint32_t journalist_invest_max = 90;
    uint32_t regulator_invest_min = 30;
    uint32_t regulator_invest_max = 120;
    uint32_t law_enforcement_min = 7;
    uint32_t law_enforcement_max = 60;
    uint32_t obligation_escalation_min = 90;
    uint32_t obligation_escalation_max = 365;
    uint32_t evidence_decay_interval = 7;
    uint32_t evidence_max_age_ticks = 1000;  // hard expiry: tokens older than this are GC'd
    uint32_t relationship_decay_interval = 30;
    uint32_t npc_business_decision = 90;
    uint32_t charge_to_trial_min = 90;
    uint32_t charge_to_trial_max = 365;
    uint32_t community_response_stage_min = 30;
    uint32_t community_response_stage_max = 180;
};

struct AddictionConfig {
    float tolerance_per_use_casual = 0.05f;
    uint32_t regular_use_threshold = 30;
    uint32_t dependency_threshold = 90;
    float dependency_tolerance_floor = 0.30f;
    float active_craving_threshold = 0.70f;
    uint32_t active_duration_ticks = 60;
    float withdrawal_health_hit = 0.005f;
    // Per-tick recovery of addiction-local withdrawal_health when the NPC is
    // supplied (no supply gap). Lets a dependent NPC who keeps paying for
    // substance heal back toward 1.0, so terminal death requires a sustained
    // supply gap rather than merely reaching the dependent stage.
    float withdrawal_health_recovery = 0.01f;
    float dependent_work_efficiency = 0.70f;
    float active_work_efficiency = 0.50f;
    float terminal_work_efficiency = 0.20f;
    uint32_t recovery_attempt_threshold = 14;
    float craving_decay_rate_recovery = 0.003f;
    uint32_t full_recovery_ticks = 365;
    float recovery_success_threshold = 0.05f;
    // Weight applied to craving when deriving relapse_probability during
    // recovery (relapse_probability = craving * relapse_history_weight). 1.0
    // means relapse risk tracks current craving directly; craving decays each
    // recovery tick, so risk falls over time per INTERFACE.md.
    float relapse_history_weight = 1.0f;
    float terminal_health_threshold = 0.15f;
    uint32_t terminal_persistence_ticks = 90;
    float rate_delta_per_active_npc = 0.001f;
    float labour_impact_per_addict = 0.80f;
    float healthcare_load_per_addict = 0.50f;
    float grievance_per_addict_fraction = 0.30f;
    float casual_craving_inc = 0.01f;
    float regular_craving_inc = 0.02f;
    float dependent_craving_inc = 0.03f;
    float active_craving_inc = 0.05f;
    float casual_to_regular_craving = 0.30f;
    float regular_to_dependent_craving = 0.70f;
};

struct AlternativeIdentityConfig {
    float documentation_decay_rate = 0.001f;
    float documentation_build_rate = 0.005f;
    float burn_threshold = 0.10f;
    float witness_confidence = 0.70f;
    float forensic_confidence = 0.55f;
};

struct ProtectionRacketsConfig {
    float demand_rate = 0.08f;
    float grievance_per_demand_unit = 0.00001f;
    float incumbent_refuse_probability = 0.40f;
    float default_refuse_probability = 0.20f;
    float personnel_violence_multiplier = 3.0f;
    uint32_t warning_threshold = 5;
    uint32_t property_damage_threshold = 15;
    uint32_t violence_threshold = 30;
    uint32_t abandonment_threshold = 60;
    float property_damage_severity = 0.4f;
    float memory_emotional_weight_warning = -0.5f;
};

struct MoneyLaunderingConfig {
    uint32_t structuring_token_interval = 7;
    uint32_t shell_chain_evidence_interval = 30;
    uint32_t trade_invoice_evidence_interval = 20;
    uint32_t commingling_evidence_interval = 15;
    uint32_t max_chain_depth = 5;
    float commingle_capacity_fraction = 0.40f;
    float rate_commingle_max = 5000.0f;
    float crypto_evidence_skill_divisor = 10.0f;
    // crypto_mixing evidence: traceability of the mixer and a proxy for LE
    // intelligence capability (NPC skill levels are not modelled, so the LE term
    // is a config proxy). Fed into compute_crypto_evidence_probability.
    float crypto_mixer_traceability = 0.5f;
    float crypto_le_skill_proxy = 0.5f;
    float fiu_token_threshold = 0.35f;
    float fiu_meter_fill_scale = 0.10f;
    uint32_t fiu_monthly_interval = 30;
    uint32_t structuring_deposit_count_threshold = 8;
    float org_capacity_multiplier = 0.25f;
    uint32_t ticks_per_quarter = 90;
    // Conversion loss applied to a laundering operation opened from a
    // LaunderingSeedDelta (criminal_operations producer). The operation's
    // launder_rate_per_tick is derived at drain time as
    // dirty_amount / ticks_per_quarter (an absolute currency/tick amount), so
    // a seeded batch washes over one quarter. 0.05 matches the money_laundering
    // INTERFACE worked example (transfer 1000 -> 950 clean).
    float seed_conversion_loss_rate = 0.05f;
};

struct DesignerDrugConfig {
    float detection_threshold = 2.5f;
    uint32_t base_review_duration = 180;
    float unscheduled_margin = 2.5f;
    float scheduled_margin = 1.0f;
    float no_successor_margin = 0.80f;
    uint32_t monthly_interval = 30;
};

struct DrugEconomyConfig {
    float wholesale_price_fraction = 0.45f;
    float wholesale_quality_degradation = 0.95f;
    float retail_quality_degradation = 0.90f;
    float meth_waste_per_unit = 0.15f;
    float demand_per_addict = 1.0f;
    float precursor_ratio_meth = 2.0f;
    float designer_legal_margin_mult = 1.5f;
    // Per-tick probability that an active stage==none NPC in a province with
    // drug supply (or pre-existing addiction_rate > 0) is seeded into the
    // addiction state machine at stage=casual. Scaled up by the province's
    // current addiction_rate; clamped by addiction_seeding_saturation_cap.
    float addiction_seeding_probability = 0.00005f;
    // Once cohort_stats.addiction_rate reaches this fraction, drug_economy
    // stops seeding new NPCs this tick. Keeps growth bounded; further
    // progression is driven by AddictionModule's stage transitions.
    float addiction_seeding_saturation_cap = 0.05f;

    // Enforcement bite + organizational resilience. When a criminal business's
    // operator (owner_id) is imprisoned, the enterprise does NOT shut down — a
    // deputy keeps it running at reduced capacity (gangs/mafias are extremely hard
    // to dismantle by decapitation), and it recovers fully on the operator's
    // release/parole. So enforcement SUPPRESSES the criminal economy in proportion
    // to how often operators are jailed (which scales with enforcement strength and
    // is throttled by corruption) — the balancing feedback that makes well-policed,
    // low-corruption places crime-light and weak/corrupt places crime-infested,
    // without ever fully eliminating organized crime.
    float operator_imprisoned_output = 0.5f;  // output multiplier while operator jailed

    // Base per-business drug output capacity per tick. Output is this fixed capacity
    // (a real throughput limit — labor/facilities/distribution), bottlenecked by
    // real precursor availability and scaled by the enforcement factor. It is NOT
    // derived from the business's own revenue: the old `revenue_per_tick * 0.1`
    // proxy was a positive-feedback loop (output→revenue→output) that, once drugs
    // got realistic prices, exploded criminal wealth to the safety ceiling in ~1
    // year. Anchoring output to capacity + inputs breaks that loop structurally.
    float base_drug_output = 5.0f;
};

struct RegionalConditionsConfig {
    float stability_recovery_rate = 0.001f;
    float event_stability_impact = 0.05f;
    float infrastructure_decay_rate = 0.0002f;
    float drought_recovery_rate = 0.005f;
    float flood_recovery_rate = 0.01f;
    // Weight on the significant-NPC wealth-concentration signal when it exceeds the
    // cohort income gini as the province inequality target. < 1.0 so that a single
    // dominant owner cannot alone peg inequality; broad concentration is needed to
    // dominate. 0.85 lets a fully-concentrated boom reach ~0.85 inequality (feeding
    // grievance) while leaving headroom below the saturation ceiling.
    float wealth_inequality_weight = 0.85f;

    // --- Waste handling + pollution ---
    // Each tick a province "handles" (removes) a fraction of its accumulated waste —
    // waste management capacity, scaling with infrastructure — so unhandled waste
    // only builds where generation outpaces handling (dirty/low-infrastructure
    // provinces). The residual pollutes: a saturating function of the hazardous-
    // weighted waste stock raises sick_rate (pollution illness → mortality via
    // population_aging). Routed to health, NOT grievance (single-owner discipline).
    float waste_handling_base = 0.40f;        // baseline fraction handled per tick
    float waste_handling_infra = 0.40f;       // additional handling × infrastructure_rating
    float waste_handling_hazardous_scale = 0.5f;  // hazardous waste is harder to handle
    float hazardous_pollution_weight = 3.0f;  // hazardous waste pollutes far more per unit
    float waste_pollution_halfsat = 500.0f;   // weighted-waste stock at half-max pollution
    float waste_pollution_sick_scale = 0.02f;  // max per-tick sick_rate contribution
};

struct TrustUpdatesConfig {
    float catastrophic_trust_loss_threshold = -0.55f;
    float catastrophic_trust_floor = 0.10f;
    float recovery_ceiling_factor = 0.60f;
    float recovery_ceiling_minimum = 0.15f;
    float significant_change_threshold = 0.10f;
    float trust_min = -1.0f;
    float trust_max = 1.0f;
    float default_recovery_ceiling = 1.0f;
};

struct WeaponsTraffickingConfig {
    float base_price_small_arms = 500.0f;
    float base_price_ammunition = 50.0f;
    float base_price_heavy_weapons = 5000.0f;
    float base_price_converted_legal = 300.0f;
    float price_floor_supply = 1.0f;
    float max_diversion_fraction = 0.30f;
    float chain_custody_actionability = 0.60f;
    float embargo_meter_spike = 0.25f;
    float trust_threshold_diversion = 0.60f;
    // Diversion at/above this regulatory-violation severity is treated as
    // heavy/embargoed-weapons trafficking and triggers an embargo investigation
    // (high awareness => not suppressible by local corruption).
    float embargo_severity_threshold = 0.70f;
    float embargo_awareness = 1.8f;
};

struct PopulationAgingConfig {
    float cohort_income_update_rate = 0.05f;
    float cohort_employment_update_rate = 0.02f;
    float max_education_drift_per_year = 0.01f;
    // Annual births/deaths (applied to cohort sizes once per year). Birth count
    // scales with healthcare access and stability (survival); deaths scale with
    // instability and addiction, and are higher for retiree cohorts.
    float base_annual_birth_rate = 0.012f;
    float base_annual_death_rate = 0.008f;
    float retiree_mortality_multiplier = 4.0f;
    // Significant-NPC natural death: NPCs past natural_lifespan_years face an
    // annual death roll scaled up as health falls. Age advances 1 year per year.
    float natural_lifespan_years = 80.0f;
    float natural_death_annual_prob = 0.10f;  // at full health, past lifespan
    // Subsistence food coupling (the Malthusian loop). cohort_stats->
    // subsistence_surplus_ratio scales births up to a cap and raises mortality
    // under a deficit. Both are NEUTRAL at surplus == 1.0 (and surplus is 1.0 in
    // market eras, where the commons module is inert), so this changes nothing
    // outside the pre-market regime. Pre-market: surplus > 1 grows the population
    // toward carrying capacity; surplus < 1 (famine) culls it back.
    float food_surplus_birth_cap = 2.5f;          // max birth multiplier from surplus
    float food_deficit_mortality_strength = 1.5f;  // extra mortality per unit of deficit
    // A surplus also RELIEVES mortality (well-fed people survive better), neutral at
    // surplus == 1.0. This lets a fed pre-market population hold/grow despite the
    // dawn's low stability inflating base mortality — and it stabilises the
    // population below carrying capacity under pressure, leaving a permanent surplus
    // margin (the headroom that funds specialists/scholars).
    float food_surplus_mortality_relief = 0.6f;  // mortality cut per unit of surplus
    float food_mortality_floor = 0.5f;           // mortality never falls below this fraction
    // Pre-market (commons) demographics are FOOD-driven, not politics-driven: a
    // subsistence band's births/deaths track the harvest, not the modern political
    // "stability" proxy (which a dawn world tanks for simply lacking income/markets).
    // In commons regimes the effective stability used by births/deaths is floored
    // here so low modern-stability doesn't crush reproduction; surplus does the work.
    float commons_stability_floor = 0.8f;
    // Commons comfort margin. The food coupling is NEUTRAL at surplus == 1.0, so the
    // dawn population's births/deaths balance right at the bare carrying capacity —
    // leaving NO surplus headroom, so no labour is ever freed for specialists and the
    // society is locked in the Malthusian trap. In commons regimes the demographics
    // instead chase a surplus offset DOWN by this margin (they treat surplus below
    // 1 + margin as lean and stop growing), so the population settles at a modest
    // permanent surplus (~1 + margin). That margin funds a thin knowledge-elite
    // (elders/scribes/scholars) — the slow engine that, over millennia, lifts the
    // carrying ceiling and escapes the trap. Commons-only (inert in market eras, where
    // surplus is 1.0 and the offset would otherwise misfire — so it is gated off).
    float commons_surplus_margin = 0.30f;
    // Generational hardiness: a population's adaptation (cohort_stats.hardiness) drifts
    // toward the world's hazard level by this fraction per year — slow, generational.
    // Mortality scales with how far hardiness falls short of the world's demand, never
    // dividing by less than hardiness_floor (so a wholly-unadapted people still bottoms
    // out rather than blowing up).
    float hardiness_drift_rate = 0.05f;
    float hardiness_floor = 0.10f;
};

struct LodSystemConfig {
    float lod2_min_modifier = 0.50f;
    float lod2_max_modifier = 2.00f;
    float lod2_smoothing_rate = 0.30f;
    float supply_floor = 1.0f;
};

struct SupplyChainModuleConfig {
    float base_transport_rate = 0.01f;
    float terrain_cost_coeff = 0.5f;
    float infra_speed_coeff = 0.5f;
    float road_speed = 300.0f;
    float rail_speed = 600.0f;
    float sea_speed = 500.0f;
    float river_speed = 200.0f;
    float air_speed = 2000.0f;
    float max_concealment_modifier = 0.40f;
    float base_interception_risk = 0.05f;
    float default_perishable_decay_rate = 0.02f;
};

struct LaborModuleConfig {
    float wage_adjustment_rate = 0.03f;
    float wage_floor = 0.01f;
    float wage_ceiling_multiplier = 5.0f;
    uint32_t pool_size_public = 12;
    uint32_t pool_size_professional = 5;
    uint32_t pool_size_referral = 3;
    float reputation_threshold = 0.3f;
    float reputation_pool_penalty_scale = 8.0f;
    float salary_premium_per_rep_point = 0.5f;
    float voluntary_departure_threshold = 0.35f;
    float departure_base_rate = 0.08f;
    float reputation_default = 0.5f;
    uint32_t deferred_salary_max_ticks = 30;
    float personal_referral_trust_min = 0.4f;
    uint32_t monthly_tick_interval = 30;

    // --- Formal labor demand (job-posting generation) ---
    // A business posts jobs to staff up toward a headcount derived from its
    // scale. offered_wage is kept a fraction of revenue_per_worker so the wage
    // bill stays solvent (~wage_revenue_fraction of revenue at full staffing).
    float revenue_per_worker = 80.0f;        // revenue_per_tick that supports one formal worker
    float wage_revenue_fraction = 0.40f;     // offered_wage = revenue_per_worker * this
    uint32_t max_workers_per_business = 12;  // headcount cap
    uint32_t job_posting_duration_ticks = 60;
    uint32_t applicants_per_posting = 4;
    uint32_t max_new_postings_per_business = 2;  // per monthly hiring cycle
    // Distress layoffs: a business can only sustain the headcount its margin can
    // pay (`(revenue - cost) / offered_wage`). When it is over that — because
    // revenue fell or costs rose — it sheds the excess (formal -> informal work,
    // and an employment_negative memory that feeds community grievance). This is
    // the bridge from business health into the social economy.
    uint32_t max_layoffs_per_business = 3;  // per monthly cycle (gradual, not a cliff)
};

struct PackageConfig {
    PriceModelConfig price_model;
    SupplyChainEconConfig supply_chain;
    LaborMarketConfig labor_market;
    NpcBusinessEconomyConfig npc_business_economy;
    TradeConfig trade;
    BankingConfig banking;
    NpcBehaviorConfig npc_behavior;
    RelationshipConfig relationships;
    InformantConfig informant;
    LegalProcessConfig legal_process;
    EvidenceConfig evidence;
    SafetyCeilingsConfig safety_ceilings;
    ProductionConfig production;
    RndConfig rnd;
    ConsequenceDelayConfig consequence_delays;
    RandomEventsConfig random_events;
    InfluenceNetworkConfig influence_network;
    PoliticalCycleConfig political_cycle;
    MediaSystemConfig media_system;
    CurrencyExchangeConfig currency_exchange;
    TradeInfrastructureConfig trade_infrastructure;
    InvestigatorEngineConfig investigator_engine;
    NpcBusinessConfig npc_business;
    GovernmentBudgetConfig government_budget;
    HealthcareConfig healthcare;
    SceneCardsConfig scene_cards;
    CommodityTradingConfig commodity_trading;
    PriceEngineConfig price_engine;
    SeasonalAgricultureConfig seasonal_agriculture;
    SubsistenceConfig subsistence;
    KnowledgeConfig knowledge;
    RealEstateConfig real_estate;
    FinancialDistributionConfig financial_distribution;
    NpcBehaviorModuleConfig npc_behavior_module;
    ObligationNetworkConfig obligation_network;
    CriminalOperationsConfig criminal_operations;
    CommunityResponseConfig community_response;
    NpcSpendingConfig npc_spending;
    AntitrustConfig antitrust;
    FacilitySignalsConfig facility_signals;
    AddictionConfig addiction;
    AlternativeIdentityConfig alternative_identity;
    ProtectionRacketsConfig protection_rackets;
    MoneyLaunderingConfig money_laundering;
    DesignerDrugConfig designer_drug;
    DrugEconomyConfig drug_economy;
    RegionalConditionsConfig regional_conditions;
    TrustUpdatesConfig trust_updates;
    WeaponsTraffickingConfig weapons_trafficking;
    PopulationAgingConfig population_aging;
    LodSystemConfig lod_system;
    SupplyChainModuleConfig supply_chain_module;
    LaborModuleConfig labor_module;
    BusinessLifecycleConfig business_lifecycle;
};

// Load PackageConfig from a directory containing JSON config files.
// Missing files silently use spec-correct struct defaults.
// Throws std::runtime_error only on malformed JSON (not on missing files).
PackageConfig load_package_config(const std::string& config_dir);

}  // namespace econlife
