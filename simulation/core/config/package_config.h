#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/world_state/shared_types.h"  // kTicksPerYear (the calendar constant)
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
    // While a drought/flood is active, its agricultural modifier is driven DOWN
    // toward a severity-set floor (1 - severity) at this per-tick rate, chosen to
    // outpace regional_conditions' recovery so the harvest stays suppressed for the
    // event's duration and only heals once it ends.
    float natural_agri_depress_rate = 0.05f;
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
    std::vector<std::string> active_regimes = {"subsistence", "barter",     "coinage",   "money",
                                               "feudal",      "mercantile", "industrial"};

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

    // --- Manorialism (M5): the feudal class structure ---
    // In the stratified pre-market regimes (feudal/mercantile/industrial) a tithe
    // concentrates the new proto-capital toward a small lord/elite stratum instead of
    // the egalitarian even split of the commons — the origin of the lord/peasant
    // divide and the inequality medieval politics runs on. Conserved (a skewed
    // distribution of the SAME proto-capital each tick, not minting); it raises
    // capital inequality, and the concentrated capital is the founder war-chest for
    // entry-time firm genesis. The earlier commons regimes stay egalitarian.
    std::vector<std::string> manorial_regimes = {"feudal", "mercantile", "industrial"};
    float manorial_tithe_rate = 0.5f;     // share of new proto-capital that tithes to lords
    float manorial_lord_fraction = 0.1f;  // fraction of residents who are lords (>=1 lord)

    // --- Seasonality harvest failures (M6a): the third episodic hazard, acting on
    // FOOD (not mortality). On top of the chronic seasonality food penalty, a bad
    // harvest year — probability and depth scaled by the world's `seasonality` dial —
    // cuts that year's output, driving famine pressure and a specialist setback
    // through the existing food loop. Conserved (less food produced). ---
    float seasonality_failure_base_rate = 0.06f;  // annual bad-harvest prob at seasonality=1
    float seasonality_failure_severity = 0.4f;    // max output cut in a bad year at seasonality=1

    // --- Chronic world-hazard channels on food (M6a; distinct from the background
    // mortality scalar — each hazard also acts through its signature channel) ---
    // Predators prey on herds/draft animals -> a chronic food penalty, strongest early
    // and WANING as accumulated knowledge/technique clears them (the §5.5 coupling).
    float predator_food_penalty = 0.10f;         // food loss at predators=1, uncleared
    float predator_clearance_halfsat = 5000.0f;  // knowledge at which predator pressure halves
    // A hostile/toxic atmosphere caps the carrying ceiling (planetary; never wanes).
    float atmosphere_cap_penalty = 0.12f;        // ceiling loss at atmosphere=1

    // Specialization ceiling: a society can't free MORE than this share of its people
    // from food work. Pre-industrial economies ran ~80-90% farmers, so a non-farming
    // share above ~10-15% is not physically sustainable on commons agriculture. The
    // actual specialist share is GROUNDED in the food balance below; this is the hard
    // upper bound that keeps the knowledge climb at a believable pace and the population
    // tracking the carrying ceiling (most hands stay on the land).
    float max_specialist_fraction = 0.15f;

    // Specialist ceiling rises as the economy MONETIZES, then as production
    // INSTITUTIONALIZES. Money and markets move surplus past barter's double-
    // coincidence limit; guilds/towns, long-distance trade and finance, and finally
    // the factory system each sustain a larger non-farming (specialist/urban) share.
    // A society climbs commons -> barter -> coinage -> money -> feudal (guild/manor)
    // -> mercantile (proto-capitalist) -> industrial (factory/wage labour). Selected
    // by the era's economic_regime; an unlisted regime falls back to
    // max_specialist_fraction. (The actual share is GROUNDED in the food balance
    // below — this is only the upper bound; the carrying ceiling must rise via
    // knowledge/tech to actually free this many hands.)
    float specialist_ceiling_subsistence = 0.15f;
    float specialist_ceiling_barter = 0.15f;
    float specialist_ceiling_coinage = 0.18f;
    float specialist_ceiling_money = 0.22f;
    float specialist_ceiling_feudal = 0.27f;      // medieval towns, guilds, clergy/nobility
    float specialist_ceiling_mercantile = 0.35f;  // workshops, shipping, early finance
    float specialist_ceiling_industrial = 0.45f;  // factory system, wage labour, urbanization

    // --- Granary food economy (grounded specialization + reserves) ---
    // Specialists (incl. knowledge-keepers) are NOT funded by a heuristic surplus or a
    // hand-placed floor: they are simply the people the farmers don't need. Each year
    // the farmers produce enough to feed everyone (and, while reserves are low, a little
    // extra to bank); whoever is left over is free to specialize. As knowledge raises
    // yield, fewer farmers are needed to feed the population, so more people are freed —
    // the grounded engine of rising specialization.
    //
    // The granary (cohort_stats.food_store, a conserved per-province stock) banks the
    // production surplus up to `granary_reserve_years` of consumption and is drawn down
    // in deficit years, so a bad harvest doesn't immediately starve the population or
    // its elite. When farming below the reserve target, the society produces an extra
    // `granary_topup_fraction` of need to refill the store.
    float granary_reserve_years = 3.0f;    // granary capacity, in years of consumption
    float granary_spoilage_rate = 0.05f;   // fraction of stored food lost per year (rot/pests).
                                           // Maintaining reserves therefore demands a permanent
                                           // production surplus (≈ spoilage × reserve), which is
                                           // what frees a standing specialist class — grounded,
                                           // not a margin.
    float granary_build_rate = 0.10f;      // fraction of the reserve gap a society aims to close
                                           // per year while under-stocked (extra farming).
    // Defaults to the canonical kTicksPerYear rather than re-spelling 365: modules
    // that gate annually on the constant (warfare, knowledge, population_aging) and
    // modules that use this field (subsistence, grain_logistics) must agree, or the
    // granary fills on one calendar while armies eat on another.
    uint32_t ticks_per_year = kTicksPerYear;  // for converting per-tick rates to annual stocks

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
    std::vector<std::string> active_regimes = {"subsistence", "barter",     "coinage",   "money",
                                               "feudal",      "mercantile", "industrial"};
    // Knowledge per year per unit of knowledge-worker output.
    //
    // Calibrated against the real Neolithic span. A dawn earthlike society holds
    // ~21k people at the subsistence regime's 0.15 freed-stratum ceiling, so
    // knowledge_workers = pop x 0.15 x learned_share(0.03) = pop x 0.0045, and at
    // elder output 0.2 with pressure 0.68 the annual rate is pop x 6.12e-4 x scalar.
    // Population roughly doubles across the era (21k -> ~40k), so integrating over
    // ~6,700 years at an average ~30k people: 3,830 = 6,700 x 18.4 x scalar gives
    // scalar ~= 0.031.
    //
    // It was 0.4 when production came from a fixed sample of ~6 tracked individuals;
    // the driver is now the learned share of the whole population, which is ~50x
    // larger and GROWS. History accelerating as populations grow and institutions
    // improve (elder 0.2 -> scribe 0.6 -> scholar 1.0) is the intended behaviour, not
    // something the thresholds should have to fake.
    float production_scalar = 0.030f;

    // Share of the freed (non-farming) stratum who are knowledge-keepers: priests,
    // elders, scribes, scholars. The rest of the stratum are artisans, builders,
    // traders, soldiers and servants. A few percent is the historical order for
    // pre-modern literate/learned classes.
    float learned_share_of_specialists = 0.03f;

    // --- Exceptional individuals (the Newton/Einstein/Socrates term) ---
    // Rare minds make discrete LEAPS, not incremental gains. The chance of one arising
    // scales with the number of people doing knowledge work (more minds, more chances),
    // and arrives as a physical first-arrival probability 1 - exp(-rate).
    float genius_rate_per_worker_year = 2.0e-5f;  // per knowledge-worker per year
    // What a leap is worth, in REAL units: one exceptional mind working for
    // genius_leap_years, counted as if it were genius_equivalent_workers ordinary
    // knowledge-keepers. So a leap is bounded by what a PERSON can do, lifted by the
    // era's institutions (per-worker output rises elder -> scribe -> scholar) and by
    // the accumulated tech multiplier — never by the size of the whole society.
    //
    // The earlier form multiplied the society's TOTAL annual output, which meant a
    // single mind in a million-person civilisation contributed thirty years of all its
    // knowledge work, and with ~15,000 knowledge workers the arrival probability
    // reached ~26%/yr, so leaps supplied ~8x the ordinary rate and dominated the climb.
    // A genius is one person, however brilliant.
    float genius_leap_years = 30.0f;
    float genius_equivalent_workers = 250.0f;  // a great mind's output vs an ordinary keeper
    // Annual attrition. Civilizational knowledge is CUMULATIVE — embodied in surviving
    // people, practices and (later) writing, it is essentially never lost on historical
    // timescales (dark-age regression is a rare exception, out of V1 scope). Zero here:
    // a transient population dip must NOT erase accumulated knowledge, or a society that
    // overshoots its food supply would lose its technique and lock into the Malthusian
    // trap instead of recovering and climbing on. The era thresholds govern the pace.
    float decay_per_year = 0.0f;

    // --- Adversity drives invention (Boserup intensification + the Deathworlders
    // premise) ---
    // Knowledge is not produced only by a thin dedicated elite. Under PRESSURE — a hard
    // world, or a population pressing on its food supply — the whole population
    // innovates: necessity is the mother of invention (Boserup's answer to Malthus;
    // the WW2/space-race leaps). This term scales with the WHOLE population (more minds)
    // and with adversity, so (a) it gives a grounded escape from the Malthusian wall
    // (scarcity itself spurs the intensification that lifts the ceiling), and (b) harsh
    // worlds out-innovate comfortable gardens, making the World-Class spectrum matter.
    float population_innovation_rate = 1.5e-6f;  // knowledge/yr per person at unit pressure
    float adversity_base = 0.35f;               // drive with no special pressure (idle curiosity)
    float adversity_hazard_weight = 0.6f;       // drive from the world's hazard above a gentle one
    float adversity_scarcity_weight = 1.4f;     // drive from food scarcity (Malthusian pressure)
    float adversity_garden_hazard = 0.45f;      // hazard level below which a world is "comfortable"
    float adversity_pressure_cap = 3.0f;        // ceiling on the pressure multiplier
};

// Grain logistics — the "tyranny of the ox" (medieval band §3.5). A draft team eats
// the grain it hauls, so surplus has a hard economic radius; water transport is an
// order of magnitude cheaper than land. Computes per-province NET FEEDABLE SURPLUS
// (own surplus + what neighbours can deliver before the oxen eat it). Conserved: the
// grain consumed in transit is accounted as draft sustenance, not vanished. Built as
// the general "link -> deliverable fraction" case so the space age (lightspeed) can
// extend it (see EconLife_Logistics_and_Political_Scale_v01.md).
struct GrainLogisticsConfig {
    // Active only in the pre-market (commons) regimes — same arc as subsistence. In
    // market eras there is no commons grain surplus to haul, so the module is inert.
    std::vector<std::string> active_regimes = {"subsistence", "barter",     "coinage",   "money",
                                               "feudal",      "mercantile", "industrial"};
    // Per-hop haulage cost of a baseline LAND link (fraction of the load eaten by the
    // team reaching an adjacent province). Mode/terrain/road/gravity scale it. Single
    // hop for now; multi-hop reach and a real centroid distance are the D6 refinement.
    float k_base = 0.5f;
    // Mode multipliers on the cost — the heart of "water makes cities". Land is the
    // tyranny; rivers and coasts are far cheaper (the cargo isn't eaten the same way).
    float land_mode = 1.0f;
    float river_mode = 0.10f;
    float maritime_mode = 0.05f;
    float terrain_weight = 1.0f;  // transit_terrain_cost raises cost (mountains/swamp block hauling)
    float infra_relief = 0.8f;    // infrastructure_bonus (roads) lowers cost
    float gravity_weight = 0.5f;  // gravity above 1g raises cost (heavier world -> smaller radius)

    // --- Urbanization (M3): the catchment surplus becomes town population ---
    // The non-farm (urban) populace a province can sustain = its net feedable surplus
    // divided by what one mouth eats (same unit as subsistence per-capita food),
    // capped at the province population. This is the aggregate medieval town economy
    // for history-gen; individual workshop/manor/castle entities materialize at player
    // entry. River/coastal hubs (large catchment) grow towns; stranded inland stays
    // rural — urbanization spatially driven by the ox-cart limit.
    float urban_per_capita_food = 1.0f;

    // --- Real grain flows (G4): the catchment is a FLOW, not a view ---
    // Stored grain diffuses down the scarcity gradient along links (kin networks,
    // tribute, trade-in-kind): each year a share of the granary-FULLNESS gap between
    // neighbours closes, and the haul pays the ox law (the transit loss is eaten by
    // the teams — an explicit conserved sink). Kinetic constant, per year.
    float grain_trade_rate_per_year = 0.1f;
};

// Warfare (M6c foundation): war between province-polities (chiefdoms/lords at the dawn).
// NOT scripted raids — a stronger polity attacks a weaker, REACHABLE neighbour when the
// power balance favours it (the EV decision; treaties/alliances/backstabbing/empires +
// grain spoils are the subsequent M6c layers). Conserved: war kills (cohort casualties
// both sides). See EconLife_War_and_Diplomacy_v01.md.
struct WarfareConfig {
    // Active in the pre-market arc (dawn polities war). Modern war is the political_cycle's.
    std::vector<std::string> active_regimes = {"subsistence", "barter",     "coinage",   "money",
                                               "feudal",      "mercantile", "industrial"};
    float aggression_ratio = 1.3f;     // attack only with >= this strength edge (risk policy)
    float base_aggression_prob = 0.04f;  // annual prob a qualifying opportunity is taken
    // --- The grounded war economy (G2): armies are PEOPLE eating GRAIN. All
    // constants are defensible in real units (grounding doctrine).
    float levy_fraction = 0.10f;      // share of population an agrarian polity can field
    float campaign_days = 120.0f;     // an attacking army's campaign season
    float defense_days = 60.0f;       // the defender mobilizes once invaded
    float soldier_ration_mult = 2.0f;  // campaign consumption vs civilian (baggage, waste, animals)
    float forage_share = 0.5f;        // ration share coverable off the land (pillage / home fields);
                                      // the rest must come from the GRANARY (food_store) — an
                                      // unprovisioned army fights at forage strength
    float battle_lethality = 0.10f;   // fraction of the enemy's effective strength that becomes
                                      // your dead over a season (Lanchester attrition coefficient)
    float sack_fraction = 0.25f;      // of the loser's granary a victorious sack reaches
    float carry_per_soldier = 100.0f;  // food units a soldier + cart share hauls home (loot limit);
                                       // what is sacked but cannot be carried is BURNED (conserved
                                       // to an explicit destruction sink)
    // Spoils (M6c-2): a won war plunders a fraction of the loser's accumulated wealth
    // (resident proto-capital), transferred to the victor — conserved (the victor's
    // residents gain exactly what the loser's lose). And richer neighbours are more
    // tempting: the attack probability scales with the defender's wealth share (attack
    // the weak AND rich — the rational EV).
    float plunder_fraction = 0.2f;  // share of the loser's resident wealth seized on a win
    float prize_weight = 1.0f;      // how much a rich target raises the attack probability
    // Diplomatic relations (M6c-3): war damages relations, sustained peace heals them,
    // and warm relations deter an attack — so long-peaceful neighbours drift into a
    // de-facto alliance (a treaty in all but name) while feuds fester. Per province-
    // pair, held as warfare-module state.
    float relation_war_hit = 0.3f;       // relations drop this much on a war (toward -1)
    float relation_peace_heal = 0.02f;   // relations heal this much per peaceful year (toward +1)
    float relation_deter_weight = 0.9f;  // how strongly warm relations suppress an attack
    // Alliances & backstabbing (M6c-4): a defender's ALLIES (warm-relation neighbours)
    // add their power to its defence, so a hegemon faces a COALITION — the balance of
    // power, bounding conquest. Betraying an ally still happens when the prize is worth
    // it, but a known BACKSTABBER's relations with everyone sour (the reputation
    // economy: pariahs lose their alliances and get ganged up on).
    float ally_threshold = 0.3f;                // relation at/above which a neighbour is an ally
    float backstab_reputation_penalty = 0.25f;  // a betrayal additionally sours the betrayer's
                                                // relations with ALL its neighbours
    // Emergent nesting polities (M6c-5, design §5.4): every populated province starts
    // as its own polity (ownership emerges from settlement; unsettled land is
    // ownerless). Repeated decisive wins ABSORB the loser's whole polity into the
    // victor's (conquest consolidates up the settlement->kingdom->empire ladder);
    // polity members pool power (internal peace, external weight); and a member whose
    // own power outgrows the rest of its polity SECEDES (the hold problem — an empire
    // that can no longer overawe a member cannot keep it).
    uint32_t absorb_after_wins = 3;   // decisive wins (same attacker->defender) to absorb
    float secession_power_ratio = 0.8f;  // member secedes when own power > ratio x rest-of-polity
    // The conqueror multipliers (M6c-6, design §5.5) — how the rare empire BREAKS the
    // bounded-war default, each archetype through a different gate:
    // ALEXANDER — leadership: rarely, a polity seat produces an exceptional commander
    // whose tenure multiplies the polity's power; when the leader dies the multiplier
    // vanishes and the hold problem fragments what institutions never caught up with.
    float leadership_rate = 0.004f;       // annual prob a seat produces a great commander
    float leadership_power_mult = 2.5f;   // polity power multiplier while the leader lives
    uint32_t leadership_tenure_years = 30;  // the conqueror's active span
    // GENGHIS — mobility: a steppe polity (herd-fed cavalry, no grain supply line)
    // projects force PAST the adjacency reach — it can strike 2-hop targets.
    float steppe_arable_max = 0.2f;   // a province this un-arable ...
    float steppe_forest_max = 0.3f;   // ... and this open is steppe (grassland)
    float cavalry_polity_min_share = 0.5f;  // steppe share of polity power to fight as cavalry
    // ROME — cohesion: integration grows with tenure and SATURATES on the
    // assimilation timescale (no hard cap; diminishing returns are the mechanism:
    // cohesion = 1 + gain_max * years / (years + halfsat)). A fresh conquest is as
    // fragile as the day it was taken; one held for generations is integrated.
    float cohesion_gain_max = 2.0f;        // asymptotic integration advantage (3x total hold)
    float cohesion_halfsat_years = 50.0f;  // ~two generations: the assimilation timescale
};

struct SeasonalAgricultureConfig {
    uint32_t ticks_per_year = kTicksPerYear;  // see the note on SubsistenceConfig
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
    // below the intrinsic growth rate is sustainable — equilibrium stock is
    // N* = K(1 − F/r) — while F pushed above r (config/mods) overfishes the stock
    // toward collapse: the shared-access problem.
    // current_stock and carrying_capacity are normalized [0,1]; catch is scaled to
    // tonnes of fish_wild for the market.
    //
    // UNITS: fishing_effort is an ANNUAL harvest fraction, matching
    // FisheriesProfile.intrinsic_growth_rate, which WorldGen v0.16 defines per
    // simulated year. seasonal_agriculture divides both by ticks_per_year, so one
    // daily tick integrates 1/365 of the biological year. (Before 2026-07-25 the
    // module applied both rates once per tick with no annual gate — the fishery
    // ran 365 biological years per year, landing ~90x the declared MSY.)
    //
    // 0.15/yr = 15% of standing biomass landed per year: the exploitation rate of
    // a fully-exploited-but-not-overfished stock (managed groundfish sit at
    // F ≈ 0.1–0.3 yr⁻¹). It is below r for every access type WorldGen seeds —
    // including slow-growing Offshore cod at r = 0.25/yr — so the baseline world
    // is sustainable as claimed above, and equilibrium landings F·K(1 − F/r) come
    // to 75–98% of the Schaefer MSY (r·K/4) across r = 0.25–0.60.
    float fishing_effort = 0.15f;             // ANNUAL fraction of stock harvested
                                              // (module converts to per-tick)
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
    // Substance purchase grounded as quantity x price RESPONSE. The base spend at a
    // stage is consumption_units x baseline_substance_price (the calibrated cost at
    // the drug's NORMAL price); it then scales by how far the substance's market
    // spot price sits above/below its OWN equilibrium in the NPC's province (a
    // scarcity premium), so spending rises when the drug gets dear and an NPC cannot
    // buy a substance that is out of stock. Using the price RATIO (not the raw spot
    // price) keeps the calibration independent of the absolute drug-price scale. At
    // normal price these units reproduce the former 5/15/30/50/20 per-stage spend.
    float baseline_substance_price = 10.0f;
    float casual_consumption_units = 0.5f;
    float regular_consumption_units = 1.5f;
    float dependent_consumption_units = 3.0f;
    float active_consumption_units = 5.0f;
    float terminal_consumption_units = 2.0f;
    // RAIL RETIRED (2026-07-26): min/max_substance_price_factor = [0.25, 4.0] used to band
    // the scarcity premium. Both operands of spot/equilibrium are guarded strictly
    // positive, so the ratio was finite and the band was pure gameplay shaping — and it
    // truncated the very dynamic the mechanism exists to produce (a real drug drought runs
    // well past 4x, and premium -> spend -> affordability -> supply gap -> withdrawal is
    // the whole chain). The unbanded ratio now passes through; when equilibrium_price is
    // not a meaningful reference the premium falls back to a neutral 1.0 (the NPC pays
    // baseline_substance_price). See addiction_module.cpp.
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
    // Production grounding (conservation): a compound is synthesised by the creator's
    // criminal operation from real, located precursor stock that is physically
    // consumed — it does not appear from nothing. base_output_per_tick is the
    // operation's per-tick capacity (scaled by the market margin); output is
    // bottlenecked on available precursors, matching the drug_economy chain.
    float base_output_per_tick = 10.0f;
    std::string precursor_good = "drug_precursors";
    float precursor_ratio = 1.0f;
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
    // --- Stability target (grounded restoring force) ---
    // Province stability gravitates toward the level its conditions actually
    // SUPPORT, not blindly toward 1.0. The target is built from already-tracked
    // province signals (each in [0,1]); stability then moves toward it at
    // stability_recovery_rate, minus the degradation from active community-response
    // unrest. Employment, infrastructure and institutional trust raise the
    // supportable level; crime, criminal capture, inequality and popular grievance
    // lower it. base 0.5 = a neutral province with no strong signal either way.
    float stability_target_base = 0.50f;
    float stability_w_employment = 0.25f;
    float stability_w_infrastructure = 0.15f;
    float stability_w_trust = 0.10f;
    float stability_w_crime = 0.30f;
    float stability_w_criminal_dominance = 0.20f;
    float stability_w_inequality = 0.15f;
    float stability_w_grievance = 0.20f;
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
    float food_surplus_birth_cap = 2.0f;          // max birth multiplier from surplus. A fed
                                                  // population grows and consumes productivity gains
                                                  // (the Malthusian reality), keeping the surplus
                                                  // modest and the population tracking the carrying
                                                  // ceiling rather than ballooning a huge surplus.
    float food_deficit_mortality_strength = 4.0f;  // extra mortality per unit of deficit; a real
                                                   // famine culls hard, so an overshot population
                                                   // self-corrects back to its food supply
                                                   // quickly rather than sticking in deficit
    // A surplus also RELIEVES mortality (well-fed people survive better), neutral at
    // surplus == 1.0. This lets a fed pre-market population hold/grow despite the
    // dawn's low stability inflating base mortality — and it stabilises the
    // population below carrying capacity under pressure, leaving a permanent surplus
    // margin (the headroom that funds specialists/scholars).
    float food_surplus_mortality_relief = 0.15f;  // mortality cut per unit of surplus (small: a
                                                  // surplus eases survival only a little — disease
                                                  // and the world's hazards, not food, dominate
                                                  // pre-modern mortality)
    float food_mortality_floor = 0.5f;            // mortality never falls below this fraction
    // Pre-market (commons) demographic balance point — the Malthusian PREVENTIVE CHECK.
    // A dawn society's births/deaths track the harvest, not the modern political
    // "stability" proxy; commons demographics use this fixed value as the birth/death
    // balance instead. It is set a little below the carrying ceiling so a fed population
    // stabilises JUST below the maximum the land can feed (historically, preventive
    // checks held populations short of absolute starvation) — that gap is the real,
    // grounded surplus that frees a standing specialist class. Higher => population
    // pushes closer to the ceiling (less surplus); lower => more headroom.
    float commons_stability_floor = 0.76f;
    // Generational hardiness: a population's adaptation (cohort_stats.hardiness) drifts
    // toward the world's hazard level by this fraction per year — slow, generational
    // (5%/yr, so the maladaptation transient after a transplant lasts decades).
    // The mortality RATE scales with how far hardiness falls short of the world's demand.
    float hardiness_drift_rate = 0.05f;
    // Divide-by-zero sentinel ONLY: the rate multiplier is world_hazard / hardiness, so
    // hardiness is never divided by less than this. It is not an outcome bound — the
    // outcome is bounded physically by the rate->probability conversion
    // p = 1 - exp(-rate) in population_aging, which saturates at 100% mortality.
    // (apply_deltas already holds hardiness in [0.05, 5.0], so this only bites on states
    // written outside that path; it therefore still trims the very deepest transient and
    // is worth a review pass of its own.)
    float hardiness_floor = 0.10f;
    // RAIL RETIRED (2026-07-26): hazard_mortality_min/max = [0.15, 3.0] used to clamp the
    // maladaptation multiplier (world_hazard / hardiness). Both operands were finite, so
    // it was never a sentinel — it was a behaviour-shaping cap, and it bound precisely
    // during the multi-decade maladaptation transient that IS the deathworld-colonisation
    // arc (garden-bred hardiness 0.19 on a deathworld => ratio 5.66, pinned to 3.0: a
    // 1.9x softening of the culling). The old defence of the minimum was also wrong: the
    // medicine multiplier was applied AFTER the clamp, so the final multiplier could sit
    // below it anyway. Mortality is now composed as an annual RATE and the annual
    // probability arrives as 1 - exp(-rate) — self-bounding at 100%, no cap. The
    // calibration arithmetic lives in population_aging_module.cpp.

    // --- Disease epidemics (M6a): the first world-classification hazard brought in
    // distinctly as an EPISODIC shock (the plague dips real population history shows).
    // Each year a province may suffer an outbreak — a mortality spike scaled by the
    // world's `disease` hazard dial AND crowding (urban_population fraction; towns are
    // disease vectors, so disease brakes urbanization). Conserved (people die via the
    // normal cohort mortality path). Active only in the pre-market (commons) arc:
    // medicine releases disease-as-population-check in the modern era (the hockey-stick
    // cause). Memoryless single-year spikes for now; multi-year outbreaks + spread
    // along trade links are the follow-up (design §5.5, D8).
    float epidemic_base_rate = 0.02f;       // annual outbreak HAZARD RATE at disease=1, rural;
                                            // probability arrives as 1 - exp(-rate) (Poisson) —
                                            // physically bounded, no cap (grounding doctrine)
    float epidemic_density_weight = 2.0f;   // urban crowding multiplies the outbreak rate
    float epidemic_severity = 1.2f;         // mortality-multiplier bump at disease=1, rural

    // --- Geology disasters (M6a): the second episodic hazard. Quakes / great storms /
    // wildfires scaled by the world's `geology` dial — a mortality spike, NOT density-
    // dependent (a quake hits everyone). Conserved; pre-market gate (engineering blunts
    // it in the modern era). Population only here; infrastructure/grain-store damage is
    // the follow-up. ---
    float geology_disaster_base_rate = 0.015f;  // annual disaster HAZARD RATE at geology=1;
                                                // probability = 1 - exp(-rate), no cap
    float geology_disaster_severity = 1.0f;     // mortality-multiplier bump at geology=1

    // Radiation chronically depresses FERTILITY (a distinct channel from the
    // background mortality scalar; planetary, never wanes). M6a chronic split.
    float radiation_fertility_penalty = 0.18f;   // birth-rate loss at radiation=1
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
    GrainLogisticsConfig grain_logistics;
    WarfareConfig warfare;
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
