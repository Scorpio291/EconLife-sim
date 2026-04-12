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
};

struct LegalProcessConfig {
    float conviction_threshold = 0.50f;
    float defense_quality_factor = 0.40f;
    uint32_t ticks_per_severity = 365;
    uint32_t double_jeopardy_cooldown = 1825;
    uint32_t charge_to_trial_min = 90;
    uint32_t charge_to_trial_max = 365;
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
    float npc_capital_ceiling = 1.0e9f;
    float business_cash_ceiling = 1.0e10f;
    float market_supply_ceiling = 1.0e8f;
    float market_price_ceiling = 1.0e6f;
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
};

// ---------------------------------------------------------------------------
// BusinessLifecycleConfig — era-driven stranded-asset penalties and new entrant spawning.
// Loaded from business_lifecycle.json.
// ---------------------------------------------------------------------------
struct StrandedSectorEntry {
    BusinessSector sector        = BusinessSector::energy;
    float          revenue_penalty = 0.0f;  // fractional reduction on revenue_per_tick
    float          cost_increase   = 0.0f;  // fractional increase on cost_per_tick
};

struct EmergingSectorEntry {
    BusinessSector  sector         = BusinessSector::technology;
    float           spawn_fraction = 0.0f;  // fraction of province biz count to spawn
    BusinessProfile profile        = BusinessProfile::fast_expander;
};

struct BusinessLifecycleConfig {
    // Maps target_era (uint8_t 2–10) → sectors penalised on transition into that era.
    std::map<uint8_t, std::vector<StrandedSectorEntry>> stranded_sectors;
    // Maps target_era (uint8_t 2–10) → sectors that spawn new entrants on that era.
    std::map<uint8_t, std::vector<EmergingSectorEntry>> emerging_sectors;
    // Revenue floor: no single-era shock can drop a business below this fraction
    // of its pre-transition revenue (prevents instant death from stacking penalties).
    float stranded_revenue_floor = 0.20f;
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
    float inaction_threshold = 0.10f;
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
};

struct CommunityResponseConfig {
    float ema_alpha = 0.05f;
    float social_capital_max = 100.0f;
    float capital_normalizer = 10000.0f;
    float social_normalizer = 50.0f;
    float memory_decay_floor = 0.01f;
    float grievance_normalizer = 10.0f;
    float grievance_shock_threshold = 0.15f;
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
};

struct AntitrustConfig {
    float market_share_threshold = 0.40f;
    float dominant_price_mover_threshold = 0.70f;
    float meter_fill_per_threshold_tick = 0.002f;
    float dominance_proposal_pressure_per_tick = 0.005f;
    float proposal_pressure_decay_rate = 0.01f;
    float proposal_threshold = 0.50f;
    uint32_t monthly_interval = 30;
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
    float dependent_work_efficiency = 0.70f;
    float active_work_efficiency = 0.50f;
    float terminal_work_efficiency = 0.20f;
    uint32_t recovery_attempt_threshold = 14;
    float craving_decay_rate_recovery = 0.003f;
    uint32_t full_recovery_ticks = 365;
    float recovery_success_threshold = 0.05f;
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
    float fiu_token_threshold = 0.35f;
    float fiu_meter_fill_scale = 0.10f;
    uint32_t fiu_monthly_interval = 30;
    uint32_t structuring_deposit_count_threshold = 8;
    float org_capacity_multiplier = 0.25f;
    uint32_t ticks_per_quarter = 90;
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
};

struct RegionalConditionsConfig {
    float stability_recovery_rate = 0.001f;
    float event_stability_impact = 0.05f;
    float infrastructure_decay_rate = 0.0002f;
    float drought_recovery_rate = 0.005f;
    float flood_recovery_rate = 0.01f;
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
};

struct PopulationAgingConfig {
    float cohort_income_update_rate = 0.05f;
    float cohort_employment_update_rate = 0.02f;
    float max_education_drift_per_year = 0.01f;
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
