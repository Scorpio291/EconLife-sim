#pragma once

#include <cstdint>
#include <string>

// PackageConfig — spec-correct defaults for all data-driven module parameters.
// Loaded from packages/base_game/config/ JSON files at startup.
// Missing files silently fall back to these defaults (all valid for simulation).
// Passed by const reference to module constructors; never modified at runtime.

namespace econlife {

struct PriceModelConfig {
    float adjustment_rate_default          = 0.05f;
    float adjustment_rate_min              = 0.01f;
    float adjustment_rate_max              = 0.15f;
    float equilibrium_convergence_speed    = 0.03f;
    float price_floor_multiplier           = 0.1f;
    float price_ceiling_multiplier         = 10.0f;
    float import_ceiling_premium           = 1.3f;
    float export_floor_discount            = 0.7f;
    float volatility_dampening             = 0.85f;
    uint32_t spot_price_update_step        = 5;
};

struct SupplyChainEconConfig {
    uint32_t shortage_propagation_delay_ticks = 1;
    float    surplus_decay_rate               = 0.02f;
    float    bottleneck_output_penalty        = 0.5f;
    uint32_t max_depth                        = 5;
};

struct LaborMarketConfig {
    uint32_t wage_update_frequency_ticks      = 30;
    float    minimum_wage_default             = 15.0f;
    float    wage_elasticity                  = 0.1f;
    float    unemployment_equilibrium_rate    = 0.05f;
    float    skill_rust_rate                  = 0.001f;
    float    skill_gain_rate                  = 0.005f;
};

struct NpcBusinessEconomyConfig {
    uint32_t quarterly_decision_interval_ticks = 90;
    float    bankruptcy_threshold              = -10000.0f;
    uint32_t consolidation_check_interval      = 180;
    float    startup_failure_rate_year1        = 0.20f;
    uint32_t max_businesses_per_province       = 500;
};

struct TradeConfig {
    uint32_t lod1_offer_refresh_ticks          = 30;
    uint32_t lod2_price_index_refresh_ticks    = 365;
    float    tariff_base_rate                  = 0.05f;
    float    transport_cost_per_km_per_tonne   = 0.05f;
    uint32_t transit_delay_base_ticks          = 3;
};

struct BankingConfig {
    float    base_interest_rate                = 0.05f;   // 5% from economy.json
    float    denial_dti_threshold              = 0.43f;
    float    max_loan_multiple                 = 5.0f;
    float    min_credit_score                  = 0.30f;
    float    inflation_target                  = 0.02f;
};

struct NpcBehaviorConfig {
    float    motivation_financial_security     = 0.25f;
    float    motivation_social_standing        = 0.15f;
    float    motivation_personal_safety        = 0.20f;
    float    motivation_power_influence        = 0.10f;
    float    motivation_ideology               = 0.10f;
    float    motivation_loyalty                = 0.10f;
    float    motivation_self_preservation      = 0.10f;
    float    memory_decay_rate                 = 0.002f;
    float    risk_tolerance_default            = 0.5f;
    uint32_t memory_log_cap                    = 500;
};

struct RelationshipConfig {
    float    decay_rate_per_30_ticks           = 0.01f;
    uint32_t max_per_npc                       = 100;
};

struct InformantConfig {
    float    base_flip_rate                    = 0.005f;  // spec: 0.005; hardcoded was 0.10f
    float    max_flip_probability              = 0.20f;
    float    risk_factor_scale                 = 0.30f;
    float    trust_factor_scale                = 0.25f;
    float    incrimination_suppression         = 0.08f;
    float    compartment_bonus_per_level       = 0.05f;
    float    pay_silence_cost                  = 50000.0f;
    float    violence_multiplier               = 3.0f;
};

struct LegalProcessConfig {
    float    conviction_threshold              = 0.50f;
    float    defense_quality_factor            = 0.40f;
    uint32_t ticks_per_severity                = 365;
    uint32_t double_jeopardy_cooldown          = 1825;
    uint32_t charge_to_trial_min               = 90;
    uint32_t charge_to_trial_max               = 365;
};

struct EvidenceConfig {
    float    base_decay_rate                   = 0.002f;
    float    actionability_floor               = 0.10f;
    uint32_t batch_interval                    = 7;
    float    credibility_threshold             = 0.30f;
    float    share_trust_threshold             = 0.45f;
};

struct ProductionConfig {
    float    tech_tier_output_bonus            = 0.08f;
    float    tech_tier_cost_reduction          = 0.05f;
    float    tech_quality_ceiling_base         = 0.5f;
    float    tech_quality_ceiling_step         = 0.1f;
    float    worker_productivity_diminishing   = 0.15f;
    float    minimum_input_fraction            = 0.1f;
};

struct RndConfig {
    float    maturation_rate_coeff             = 0.40f;
    float    maturation_difficulty_per_level   = 2.0f;
    float    base_research_success_rate        = 0.75f;
    float    domain_knowledge_bonus_coeff      = 0.30f;
    float    unexpected_discovery_probability  = 0.05f;
    float    patent_preemption_check_rate      = 0.02f;
    float    knowledge_decay_rate              = 0.0001f;
    float    era_transition_threshold          = 0.70f;
    uint32_t patent_duration_ticks             = 7300;
};

struct ConsequenceDelayConfig {
    uint32_t whistleblower_min                 = 30;
    uint32_t whistleblower_max                 = 180;
    uint32_t journalist_invest_min             = 14;
    uint32_t journalist_invest_max             = 90;
    uint32_t regulator_invest_min              = 30;
    uint32_t regulator_invest_max              = 120;
    uint32_t law_enforcement_min               = 7;
    uint32_t law_enforcement_max               = 60;
    uint32_t obligation_escalation_min         = 90;
    uint32_t obligation_escalation_max         = 365;
    uint32_t evidence_decay_interval           = 7;
    uint32_t relationship_decay_interval       = 30;
    uint32_t npc_business_decision             = 90;
    uint32_t charge_to_trial_min               = 90;
    uint32_t charge_to_trial_max               = 365;
    uint32_t community_response_stage_min      = 30;
    uint32_t community_response_stage_max      = 180;
};

struct PackageConfig {
    PriceModelConfig           price_model;
    SupplyChainEconConfig supply_chain;
    LaborMarketConfig          labor_market;
    NpcBusinessEconomyConfig   npc_business_economy;
    TradeConfig                trade;
    BankingConfig              banking;
    NpcBehaviorConfig          npc_behavior;
    RelationshipConfig         relationships;
    InformantConfig            informant;
    LegalProcessConfig         legal_process;
    EvidenceConfig             evidence;
    ProductionConfig           production;
    RndConfig                  rnd;
    ConsequenceDelayConfig     consequence_delays;
};

// Load PackageConfig from a directory containing JSON config files.
// Missing files silently use spec-correct struct defaults.
// Throws std::runtime_error only on malformed JSON (not on missing files).
PackageConfig load_package_config(const std::string& config_dir);

}  // namespace econlife
