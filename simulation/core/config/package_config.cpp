#include "core/config/package_config.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace econlife {

namespace {

// Load a JSON file; returns empty object if file does not exist or cannot be opened.
static nlohmann::json load_json(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        return nlohmann::json::object();
    return nlohmann::json::parse(f);
}

}  // namespace

PackageConfig load_package_config(const std::string& config_dir) {
    PackageConfig cfg{};  // all fields initialised to spec-correct defaults

    // -----------------------------------------------------------------
    // economy.json
    // -----------------------------------------------------------------
    {
        auto j = load_json(config_dir + "/economy.json");

        const auto pm = j.value("price_model", nlohmann::json::object());
        cfg.price_model.adjustment_rate_default =
            pm.value("adjustment_rate_default", cfg.price_model.adjustment_rate_default);
        cfg.price_model.adjustment_rate_min =
            pm.value("adjustment_rate_min", cfg.price_model.adjustment_rate_min);
        cfg.price_model.adjustment_rate_max =
            pm.value("adjustment_rate_max", cfg.price_model.adjustment_rate_max);
        cfg.price_model.equilibrium_convergence_speed = pm.value(
            "equilibrium_convergence_speed", cfg.price_model.equilibrium_convergence_speed);
        cfg.price_model.price_floor_multiplier =
            pm.value("price_floor_multiplier", cfg.price_model.price_floor_multiplier);
        cfg.price_model.price_ceiling_multiplier =
            pm.value("price_ceiling_multiplier", cfg.price_model.price_ceiling_multiplier);
        cfg.price_model.import_ceiling_premium =
            pm.value("import_ceiling_premium", cfg.price_model.import_ceiling_premium);
        cfg.price_model.export_floor_discount =
            pm.value("export_floor_discount", cfg.price_model.export_floor_discount);
        cfg.price_model.volatility_dampening =
            pm.value("volatility_dampening", cfg.price_model.volatility_dampening);
        cfg.price_model.spot_price_update_step =
            pm.value("spot_price_update_step", cfg.price_model.spot_price_update_step);

        const auto sc = j.value("supply_chain", nlohmann::json::object());
        cfg.supply_chain.shortage_propagation_delay_ticks = sc.value(
            "shortage_propagation_delay_ticks", cfg.supply_chain.shortage_propagation_delay_ticks);
        cfg.supply_chain.surplus_decay_rate =
            sc.value("surplus_decay_rate_per_tick", cfg.supply_chain.surplus_decay_rate);
        cfg.supply_chain.bottleneck_output_penalty =
            sc.value("bottleneck_output_penalty", cfg.supply_chain.bottleneck_output_penalty);
        cfg.supply_chain.max_depth = sc.value("max_supply_chain_depth", cfg.supply_chain.max_depth);

        const auto lm = j.value("labor_market", nlohmann::json::object());
        cfg.labor_market.wage_update_frequency_ticks =
            lm.value("wage_update_frequency_ticks", cfg.labor_market.wage_update_frequency_ticks);
        cfg.labor_market.minimum_wage_default =
            lm.value("minimum_wage_default", cfg.labor_market.minimum_wage_default);
        cfg.labor_market.wage_elasticity =
            lm.value("wage_elasticity", cfg.labor_market.wage_elasticity);
        cfg.labor_market.unemployment_equilibrium_rate = lm.value(
            "unemployment_equilibrium_rate", cfg.labor_market.unemployment_equilibrium_rate);
        cfg.labor_market.skill_rust_rate =
            lm.value("skill_rust_rate_per_tick", cfg.labor_market.skill_rust_rate);
        cfg.labor_market.skill_gain_rate =
            lm.value("skill_gain_rate_per_tick", cfg.labor_market.skill_gain_rate);

        const auto fin = j.value("financial", nlohmann::json::object());
        cfg.banking.base_interest_rate = fin.value("interest_rate_base_percent", 5.0f) / 100.0f;
        cfg.banking.inflation_target = fin.value("inflation_target_percent", 2.0f) / 100.0f;

        const auto nb = j.value("npc_business", nlohmann::json::object());
        cfg.npc_business_economy.quarterly_decision_interval_ticks =
            nb.value("quarterly_decision_interval_ticks",
                     cfg.npc_business_economy.quarterly_decision_interval_ticks);
        cfg.npc_business_economy.bankruptcy_threshold =
            nb.value("bankruptcy_threshold_capital", cfg.npc_business_economy.bankruptcy_threshold);
        cfg.npc_business_economy.consolidation_check_interval =
            nb.value("consolidation_check_interval_ticks",
                     cfg.npc_business_economy.consolidation_check_interval);
        cfg.npc_business_economy.startup_failure_rate_year1 = nb.value(
            "startup_failure_rate_year1", cfg.npc_business_economy.startup_failure_rate_year1);
        cfg.npc_business_economy.max_businesses_per_province = nb.value(
            "max_businesses_per_province", cfg.npc_business_economy.max_businesses_per_province);

        const auto tr = j.value("trade", nlohmann::json::object());
        cfg.trade.lod1_offer_refresh_ticks =
            tr.value("lod1_trade_offer_refresh_ticks", cfg.trade.lod1_offer_refresh_ticks);
        cfg.trade.lod2_price_index_refresh_ticks =
            tr.value("lod2_price_index_refresh_ticks", cfg.trade.lod2_price_index_refresh_ticks);
        cfg.trade.tariff_base_rate = tr.value("tariff_base_rate_percent", 5.0f) / 100.0f;
        cfg.trade.transport_cost_per_km_per_tonne =
            tr.value("transport_cost_per_km_per_tonne", cfg.trade.transport_cost_per_km_per_tonne);
        cfg.trade.transit_delay_base_ticks =
            tr.value("transit_delay_base_ticks", cfg.trade.transit_delay_base_ticks);

        const auto sc2 = j.value("safety_ceilings", nlohmann::json::object());
        cfg.safety_ceilings.npc_capital_ceiling =
            sc2.value("npc_capital_ceiling", cfg.safety_ceilings.npc_capital_ceiling);
        cfg.safety_ceilings.business_cash_ceiling =
            sc2.value("business_cash_ceiling", cfg.safety_ceilings.business_cash_ceiling);
        cfg.safety_ceilings.market_supply_ceiling =
            sc2.value("market_supply_ceiling", cfg.safety_ceilings.market_supply_ceiling);
        cfg.safety_ceilings.market_price_ceiling =
            sc2.value("market_price_ceiling", cfg.safety_ceilings.market_price_ceiling);
    }

    // -----------------------------------------------------------------
    // npc.json
    // -----------------------------------------------------------------
    {
        auto j = load_json(config_dir + "/npc.json");

        const auto nb = j.value("npc_behavior", nlohmann::json::object());
        const auto mw = nb.value("motivation_weights", nlohmann::json::object());
        cfg.npc_behavior.motivation_financial_security =
            mw.value("financial_security", cfg.npc_behavior.motivation_financial_security);
        cfg.npc_behavior.motivation_social_standing =
            mw.value("social_standing", cfg.npc_behavior.motivation_social_standing);
        cfg.npc_behavior.motivation_personal_safety =
            mw.value("personal_safety", cfg.npc_behavior.motivation_personal_safety);
        cfg.npc_behavior.motivation_power_influence =
            mw.value("power_influence", cfg.npc_behavior.motivation_power_influence);
        cfg.npc_behavior.motivation_ideology =
            mw.value("ideology", cfg.npc_behavior.motivation_ideology);
        cfg.npc_behavior.motivation_loyalty =
            mw.value("loyalty", cfg.npc_behavior.motivation_loyalty);
        cfg.npc_behavior.motivation_self_preservation =
            mw.value("self_preservation", cfg.npc_behavior.motivation_self_preservation);
        cfg.npc_behavior.memory_decay_rate =
            nb.value("memory_decay_rate_per_tick", cfg.npc_behavior.memory_decay_rate);
        cfg.npc_behavior.risk_tolerance_default =
            nb.value("risk_tolerance_default", cfg.npc_behavior.risk_tolerance_default);
        cfg.npc_behavior.memory_log_cap =
            nb.value("memory_log_cap", cfg.npc_behavior.memory_log_cap);
        cfg.npc_behavior.memory_decay_floor =
            nb.value("memory_decay_floor", cfg.npc_behavior.memory_decay_floor);
        cfg.npc_behavior.knowledge_confidence_decay_rate =
            nb.value("knowledge_confidence_decay_rate", cfg.npc_behavior.knowledge_confidence_decay_rate);
        cfg.npc_behavior.motivation_shift_rate =
            nb.value("motivation_shift_rate", cfg.npc_behavior.motivation_shift_rate);
        cfg.npc_behavior.base_wage =
            nb.value("base_wage", cfg.npc_behavior.base_wage);
        cfg.npc_behavior.base_illicit_income =
            nb.value("base_illicit_income", cfg.npc_behavior.base_illicit_income);
        cfg.npc_behavior.shop_cost_fraction =
            nb.value("shop_cost_fraction", cfg.npc_behavior.shop_cost_fraction);

        const auto rel = j.value("relationships", nlohmann::json::object());
        cfg.relationships.decay_rate_per_30_ticks =
            rel.value("decay_rate_per_30_ticks", cfg.relationships.decay_rate_per_30_ticks);
        cfg.relationships.max_per_npc =
            rel.value("max_relationships_per_npc", cfg.relationships.max_per_npc);
        cfg.relationships.trust_decay_rate_per_batch =
            rel.value("trust_decay_rate_per_batch", cfg.relationships.trust_decay_rate_per_batch);
        cfg.relationships.fear_decay_rate_per_batch =
            rel.value("fear_decay_rate_per_batch", cfg.relationships.fear_decay_rate_per_batch);
    }

    // -----------------------------------------------------------------
    // production_config.json
    // -----------------------------------------------------------------
    {
        auto j = load_json(config_dir + "/production_config.json");
        cfg.production.tech_tier_output_bonus =
            j.value("tech_tier_output_bonus_per_tier", cfg.production.tech_tier_output_bonus);
        cfg.production.tech_tier_cost_reduction =
            j.value("tech_tier_cost_reduction_per_tier", cfg.production.tech_tier_cost_reduction);
        cfg.production.tech_quality_ceiling_base =
            j.value("tech_quality_ceiling_base", cfg.production.tech_quality_ceiling_base);
        cfg.production.tech_quality_ceiling_step =
            j.value("tech_quality_ceiling_step", cfg.production.tech_quality_ceiling_step);
        cfg.production.worker_productivity_diminishing =
            j.value("worker_productivity_diminishing_factor",
                    cfg.production.worker_productivity_diminishing);
        cfg.production.minimum_input_fraction =
            j.value("minimum_input_fraction_to_produce", cfg.production.minimum_input_fraction);
    }

    // -----------------------------------------------------------------
    // rnd_config.json
    // -----------------------------------------------------------------
    {
        auto j = load_json(config_dir + "/rnd_config.json");
        cfg.rnd.maturation_rate_coeff =
            j.value("maturation_rate_coeff", cfg.rnd.maturation_rate_coeff);
        cfg.rnd.maturation_difficulty_per_level =
            j.value("maturation_difficulty_per_level", cfg.rnd.maturation_difficulty_per_level);
        cfg.rnd.base_research_success_rate =
            j.value("base_research_success_rate", cfg.rnd.base_research_success_rate);
        cfg.rnd.domain_knowledge_bonus_coeff =
            j.value("domain_knowledge_bonus_coeff", cfg.rnd.domain_knowledge_bonus_coeff);
        cfg.rnd.unexpected_discovery_probability =
            j.value("unexpected_discovery_probability", cfg.rnd.unexpected_discovery_probability);
        cfg.rnd.patent_preemption_check_rate =
            j.value("patent_preemption_check_rate", cfg.rnd.patent_preemption_check_rate);
        cfg.rnd.knowledge_decay_rate =
            j.value("knowledge_decay_rate", cfg.rnd.knowledge_decay_rate);
        cfg.rnd.era_transition_threshold =
            j.value("era_transition_threshold", cfg.rnd.era_transition_threshold);
        cfg.rnd.patent_duration_ticks =
            j.value("patent_duration_ticks", cfg.rnd.patent_duration_ticks);
    }

    // -----------------------------------------------------------------
    // consequence_delays.json
    // -----------------------------------------------------------------
    {
        auto j = load_json(config_dir + "/consequence_delays.json");
        const auto cd = j.value("consequence_delays", nlohmann::json::object());

        const auto wb = cd.value("whistleblower_emergence", nlohmann::json::object());
        cfg.consequence_delays.whistleblower_min =
            wb.value("base_delay_min", cfg.consequence_delays.whistleblower_min);
        cfg.consequence_delays.whistleblower_max =
            wb.value("base_delay_max", cfg.consequence_delays.whistleblower_max);

        const auto ji = cd.value("journalist_investigation_start", nlohmann::json::object());
        cfg.consequence_delays.journalist_invest_min =
            ji.value("base_delay_min", cfg.consequence_delays.journalist_invest_min);
        cfg.consequence_delays.journalist_invest_max =
            ji.value("base_delay_max", cfg.consequence_delays.journalist_invest_max);

        const auto ri = cd.value("regulator_investigation_start", nlohmann::json::object());
        cfg.consequence_delays.regulator_invest_min =
            ri.value("base_delay_min", cfg.consequence_delays.regulator_invest_min);
        cfg.consequence_delays.regulator_invest_max =
            ri.value("base_delay_max", cfg.consequence_delays.regulator_invest_max);

        const auto le = cd.value("law_enforcement_response", nlohmann::json::object());
        cfg.consequence_delays.law_enforcement_min =
            le.value("base_delay_min", cfg.consequence_delays.law_enforcement_min);
        cfg.consequence_delays.law_enforcement_max =
            le.value("base_delay_max", cfg.consequence_delays.law_enforcement_max);

        const auto oe = cd.value("obligation_escalation", nlohmann::json::object());
        cfg.consequence_delays.obligation_escalation_min =
            oe.value("base_delay_min", cfg.consequence_delays.obligation_escalation_min);
        cfg.consequence_delays.obligation_escalation_max =
            oe.value("base_delay_max", cfg.consequence_delays.obligation_escalation_max);

        const auto ed = cd.value("evidence_decay", nlohmann::json::object());
        cfg.consequence_delays.evidence_decay_interval =
            ed.value("base_delay", cfg.consequence_delays.evidence_decay_interval);

        const auto rd = cd.value("relationship_decay", nlohmann::json::object());
        cfg.consequence_delays.relationship_decay_interval =
            rd.value("base_delay", cfg.consequence_delays.relationship_decay_interval);

        const auto bd = cd.value("npc_business_decision", nlohmann::json::object());
        cfg.consequence_delays.npc_business_decision =
            bd.value("base_delay", cfg.consequence_delays.npc_business_decision);

        const auto ct = cd.value("legal_process_charge_to_trial", nlohmann::json::object());
        cfg.consequence_delays.charge_to_trial_min =
            ct.value("base_delay_min", cfg.consequence_delays.charge_to_trial_min);
        cfg.consequence_delays.charge_to_trial_max =
            ct.value("base_delay_max", cfg.consequence_delays.charge_to_trial_max);

        const auto cr = cd.value("community_response_stage_advance", nlohmann::json::object());
        cfg.consequence_delays.community_response_stage_min =
            cr.value("base_delay_min", cfg.consequence_delays.community_response_stage_min);
        cfg.consequence_delays.community_response_stage_max =
            cr.value("base_delay_max", cfg.consequence_delays.community_response_stage_max);
    }

    // -----------------------------------------------------------------
    // modules.json — per-module tunable parameters
    // -----------------------------------------------------------------
    {
        auto j = load_json(config_dir + "/modules.json");

        const auto re = j.value("random_events", nlohmann::json::object());
        cfg.random_events.base_rate = re.value("base_rate", cfg.random_events.base_rate);
        cfg.random_events.climate_event_amplifier = re.value("climate_event_amplifier", cfg.random_events.climate_event_amplifier);
        cfg.random_events.instability_event_amplifier = re.value("instability_event_amplifier", cfg.random_events.instability_event_amplifier);
        cfg.random_events.evidence_severity_threshold = re.value("evidence_severity_threshold", cfg.random_events.evidence_severity_threshold);
        cfg.random_events.weight_natural = re.value("weight_natural", cfg.random_events.weight_natural);
        cfg.random_events.weight_accident = re.value("weight_accident", cfg.random_events.weight_accident);
        cfg.random_events.weight_economic = re.value("weight_economic", cfg.random_events.weight_economic);
        cfg.random_events.weight_human = re.value("weight_human", cfg.random_events.weight_human);
        cfg.random_events.natural_agri_mod_min = re.value("natural_agri_mod_min", cfg.random_events.natural_agri_mod_min);
        cfg.random_events.natural_agri_mod_max = re.value("natural_agri_mod_max", cfg.random_events.natural_agri_mod_max);
        cfg.random_events.natural_infra_dmg_min = re.value("natural_infra_dmg_min", cfg.random_events.natural_infra_dmg_min);
        cfg.random_events.natural_infra_dmg_max = re.value("natural_infra_dmg_max", cfg.random_events.natural_infra_dmg_max);
        cfg.random_events.accident_output_rate_min = re.value("accident_output_rate_min", cfg.random_events.accident_output_rate_min);
        cfg.random_events.accident_output_rate_max = re.value("accident_output_rate_max", cfg.random_events.accident_output_rate_max);
        cfg.random_events.accident_infra_dmg_min = re.value("accident_infra_dmg_min", cfg.random_events.accident_infra_dmg_min);
        cfg.random_events.accident_infra_dmg_max = re.value("accident_infra_dmg_max", cfg.random_events.accident_infra_dmg_max);
        cfg.random_events.economic_price_shift_min = re.value("economic_price_shift_min", cfg.random_events.economic_price_shift_min);
        cfg.random_events.economic_price_shift_max = re.value("economic_price_shift_max", cfg.random_events.economic_price_shift_max);

        const auto in = j.value("influence_network", nlohmann::json::object());
        cfg.influence_network.trust_classification_threshold = in.value("trust_classification_threshold", cfg.influence_network.trust_classification_threshold);
        cfg.influence_network.fear_classification_threshold = in.value("fear_classification_threshold", cfg.influence_network.fear_classification_threshold);
        cfg.influence_network.fear_trust_ceiling = in.value("fear_trust_ceiling", cfg.influence_network.fear_trust_ceiling);
        cfg.influence_network.catastrophic_trust_loss_threshold = in.value("catastrophic_trust_loss_threshold", cfg.influence_network.catastrophic_trust_loss_threshold);
        cfg.influence_network.catastrophic_trust_floor = in.value("catastrophic_trust_floor", cfg.influence_network.catastrophic_trust_floor);
        cfg.influence_network.recovery_ceiling_factor = in.value("recovery_ceiling_factor", cfg.influence_network.recovery_ceiling_factor);
        cfg.influence_network.recovery_ceiling_minimum = in.value("recovery_ceiling_minimum", cfg.influence_network.recovery_ceiling_minimum);
        cfg.influence_network.obligation_erosion_rate = in.value("obligation_erosion_rate", cfg.influence_network.obligation_erosion_rate);
        cfg.influence_network.trust_weight = in.value("trust_weight", cfg.influence_network.trust_weight);
        cfg.influence_network.obligation_weight = in.value("obligation_weight", cfg.influence_network.obligation_weight);
        cfg.influence_network.fear_weight = in.value("fear_weight", cfg.influence_network.fear_weight);
        cfg.influence_network.movement_weight = in.value("movement_weight", cfg.influence_network.movement_weight);
        cfg.influence_network.diversity_bonus = in.value("diversity_bonus", cfg.influence_network.diversity_bonus);
        cfg.influence_network.health_target_count = in.value("health_target_count", cfg.influence_network.health_target_count);

        const auto pc = j.value("political_cycle", nlohmann::json::object());
        cfg.political_cycle.support_threshold = pc.value("support_threshold", cfg.political_cycle.support_threshold);
        cfg.political_cycle.oppose_threshold = pc.value("oppose_threshold", cfg.political_cycle.oppose_threshold);
        cfg.political_cycle.majority_threshold = pc.value("majority_threshold", cfg.political_cycle.majority_threshold);
        cfg.political_cycle.resource_scale = pc.value("resource_scale", cfg.political_cycle.resource_scale);
        cfg.political_cycle.resource_max_effect = pc.value("resource_max_effect", cfg.political_cycle.resource_max_effect);
        cfg.political_cycle.event_modifier_cap = pc.value("event_modifier_cap", cfg.political_cycle.event_modifier_cap);

        const auto ms = j.value("media_system", nlohmann::json::object());
        cfg.media_system.cross_outlet_pickup_rate = ms.value("cross_outlet_pickup_rate", cfg.media_system.cross_outlet_pickup_rate);
        cfg.media_system.cross_outlet_amplification_factor = ms.value("cross_outlet_amplification_factor", cfg.media_system.cross_outlet_amplification_factor);
        cfg.media_system.social_amplification_multiplier = ms.value("social_amplification_multiplier", cfg.media_system.social_amplification_multiplier);
        cfg.media_system.exposure_per_amplification_unit = ms.value("exposure_per_amplification_unit", cfg.media_system.exposure_per_amplification_unit);
        cfg.media_system.crisis_evidence_threshold = ms.value("crisis_evidence_threshold", cfg.media_system.crisis_evidence_threshold);
        cfg.media_system.owner_suppression_base_rate = ms.value("owner_suppression_base_rate", cfg.media_system.owner_suppression_base_rate);
        cfg.media_system.propagation_window_ticks = ms.value("propagation_window_ticks", cfg.media_system.propagation_window_ticks);
        cfg.media_system.editorial_independence_journalist_bonus = ms.value("editorial_independence_journalist_bonus", cfg.media_system.editorial_independence_journalist_bonus);

        const auto ce = j.value("currency_exchange", nlohmann::json::object());
        cfg.currency_exchange.trade_balance_weight = ce.value("trade_balance_weight", cfg.currency_exchange.trade_balance_weight);
        cfg.currency_exchange.inflation_weight = ce.value("inflation_weight", cfg.currency_exchange.inflation_weight);
        cfg.currency_exchange.sovereign_risk_weight = ce.value("sovereign_risk_weight", cfg.currency_exchange.sovereign_risk_weight);
        cfg.currency_exchange.peg_break_reserve_threshold = ce.value("peg_break_reserve_threshold", cfg.currency_exchange.peg_break_reserve_threshold);
        cfg.currency_exchange.floor_fraction = ce.value("floor_fraction", cfg.currency_exchange.floor_fraction);
        cfg.currency_exchange.ceiling_fraction = ce.value("ceiling_fraction", cfg.currency_exchange.ceiling_fraction);
        cfg.currency_exchange.fx_transaction_cost = ce.value("fx_transaction_cost", cfg.currency_exchange.fx_transaction_cost);

        const auto ti = j.value("trade_infrastructure", nlohmann::json::object());
        cfg.trade_infrastructure.mode_speed_road = ti.value("mode_speed_road", cfg.trade_infrastructure.mode_speed_road);
        cfg.trade_infrastructure.mode_speed_rail = ti.value("mode_speed_rail", cfg.trade_infrastructure.mode_speed_rail);
        cfg.trade_infrastructure.mode_speed_sea = ti.value("mode_speed_sea", cfg.trade_infrastructure.mode_speed_sea);
        cfg.trade_infrastructure.mode_speed_river = ti.value("mode_speed_river", cfg.trade_infrastructure.mode_speed_river);
        cfg.trade_infrastructure.mode_speed_air = ti.value("mode_speed_air", cfg.trade_infrastructure.mode_speed_air);
        cfg.trade_infrastructure.terrain_delay_coeff = ti.value("terrain_delay_coeff", cfg.trade_infrastructure.terrain_delay_coeff);
        cfg.trade_infrastructure.infra_delay_coeff = ti.value("infra_delay_coeff", cfg.trade_infrastructure.infra_delay_coeff);
        cfg.trade_infrastructure.max_concealment_modifier = ti.value("max_concealment_modifier", cfg.trade_infrastructure.max_concealment_modifier);
        cfg.trade_infrastructure.perishable_decay_base = ti.value("perishable_decay_base", cfg.trade_infrastructure.perishable_decay_base);

        const auto ie = j.value("investigator_engine", nlohmann::json::object());
        cfg.investigator_engine.facility_count_normalizer = ie.value("facility_count_normalizer", cfg.investigator_engine.facility_count_normalizer);
        cfg.investigator_engine.detection_to_fill_rate_scale = ie.value("detection_to_fill_rate_scale", cfg.investigator_engine.detection_to_fill_rate_scale);
        cfg.investigator_engine.fill_rate_max = ie.value("fill_rate_max", cfg.investigator_engine.fill_rate_max);
        cfg.investigator_engine.personnel_violence_multiplier = ie.value("personnel_violence_multiplier", cfg.investigator_engine.personnel_violence_multiplier);
        cfg.investigator_engine.surveillance_threshold = ie.value("surveillance_threshold", cfg.investigator_engine.surveillance_threshold);
        cfg.investigator_engine.formal_inquiry_threshold = ie.value("formal_inquiry_threshold", cfg.investigator_engine.formal_inquiry_threshold);
        cfg.investigator_engine.raid_threshold = ie.value("raid_threshold", cfg.investigator_engine.raid_threshold);
        cfg.investigator_engine.warrant_trust_min = ie.value("warrant_trust_min", cfg.investigator_engine.warrant_trust_min);
        cfg.investigator_engine.decay_rate = ie.value("decay_rate", cfg.investigator_engine.decay_rate);
        cfg.investigator_engine.default_corruption_susceptibility = ie.value("default_corruption_susceptibility", cfg.investigator_engine.default_corruption_susceptibility);

        const auto nbc = j.value("npc_business", nlohmann::json::object());
        cfg.npc_business.cash_critical_months = nbc.value("cash_critical_months", cfg.npc_business.cash_critical_months);
        cfg.npc_business.cash_comfortable_months = nbc.value("cash_comfortable_months", cfg.npc_business.cash_comfortable_months);
        cfg.npc_business.cash_surplus_months = nbc.value("cash_surplus_months", cfg.npc_business.cash_surplus_months);
        cfg.npc_business.exit_market_threshold = nbc.value("exit_market_threshold", cfg.npc_business.exit_market_threshold);
        cfg.npc_business.exit_probability = nbc.value("exit_probability", cfg.npc_business.exit_probability);
        cfg.npc_business.expansion_return_threshold = nbc.value("expansion_return_threshold", cfg.npc_business.expansion_return_threshold);
        cfg.npc_business.ticks_per_quarter = nbc.value("ticks_per_quarter", cfg.npc_business.ticks_per_quarter);
        cfg.npc_business.dispatch_period = nbc.value("dispatch_period", cfg.npc_business.dispatch_period);
        cfg.npc_business.quality_player_rd_rate = nbc.value("quality_player_rd_rate", cfg.npc_business.quality_player_rd_rate);
        cfg.npc_business.fast_expander_rd_rate = nbc.value("fast_expander_rd_rate", cfg.npc_business.fast_expander_rd_rate);
        cfg.npc_business.cost_cutter_layoff_fraction = nbc.value("cost_cutter_layoff_fraction", cfg.npc_business.cost_cutter_layoff_fraction);
        cfg.npc_business.board_captured_threshold = nbc.value("board_captured_threshold", cfg.npc_business.board_captured_threshold);
        cfg.npc_business.board_risky_block_threshold = nbc.value("board_risky_block_threshold", cfg.npc_business.board_risky_block_threshold);

        const auto gb = j.value("government_budget", nlohmann::json::object());
        cfg.government_budget.ticks_per_quarter = gb.value("ticks_per_quarter", cfg.government_budget.ticks_per_quarter);
        cfg.government_budget.infrastructure_decay_per_quarter = gb.value("infrastructure_decay_per_quarter", cfg.government_budget.infrastructure_decay_per_quarter);
        cfg.government_budget.infrastructure_investment_scale = gb.value("infrastructure_investment_scale", cfg.government_budget.infrastructure_investment_scale);
        cfg.government_budget.debt_warning_ratio = gb.value("debt_warning_ratio", cfg.government_budget.debt_warning_ratio);
        cfg.government_budget.debt_crisis_ratio = gb.value("debt_crisis_ratio", cfg.government_budget.debt_crisis_ratio);
        cfg.government_budget.city_revenue_fraction = gb.value("city_revenue_fraction", cfg.government_budget.city_revenue_fraction);
        cfg.government_budget.corruption_evidence_threshold = gb.value("corruption_evidence_threshold", cfg.government_budget.corruption_evidence_threshold);
        cfg.government_budget.spending_stability_scale = gb.value("spending_stability_scale", cfg.government_budget.spending_stability_scale);
        cfg.government_budget.spending_crime_scale = gb.value("spending_crime_scale", cfg.government_budget.spending_crime_scale);
        cfg.government_budget.spending_inequality_scale = gb.value("spending_inequality_scale", cfg.government_budget.spending_inequality_scale);
        cfg.government_budget.cohort_mod_working_class = gb.value("cohort_mod_working_class", cfg.government_budget.cohort_mod_working_class);
        cfg.government_budget.cohort_mod_professional = gb.value("cohort_mod_professional", cfg.government_budget.cohort_mod_professional);
        cfg.government_budget.cohort_mod_corporate = gb.value("cohort_mod_corporate", cfg.government_budget.cohort_mod_corporate);
        cfg.government_budget.cohort_mod_criminal_adjacent = gb.value("cohort_mod_criminal_adjacent", cfg.government_budget.cohort_mod_criminal_adjacent);

        const auto hc = j.value("healthcare", nlohmann::json::object());
        cfg.healthcare.base_recovery_rate = hc.value("base_recovery_rate", cfg.healthcare.base_recovery_rate);
        cfg.healthcare.critical_health_threshold = hc.value("critical_health_threshold", cfg.healthcare.critical_health_threshold);
        cfg.healthcare.treatment_health_boost = hc.value("treatment_health_boost", cfg.healthcare.treatment_health_boost);
        cfg.healthcare.overload_threshold = hc.value("overload_threshold", cfg.healthcare.overload_threshold);
        cfg.healthcare.overload_quality_penalty = hc.value("overload_quality_penalty", cfg.healthcare.overload_quality_penalty);
        cfg.healthcare.labour_impairment_threshold = hc.value("labour_impairment_threshold", cfg.healthcare.labour_impairment_threshold);
        cfg.healthcare.labour_supply_impact = hc.value("labour_supply_impact", cfg.healthcare.labour_supply_impact);
        cfg.healthcare.capacity_per_treatment = hc.value("capacity_per_treatment", cfg.healthcare.capacity_per_treatment);

        const auto scc = j.value("scene_cards", nlohmann::json::object());
        cfg.scene_cards.max_scene_cards_per_tick = scc.value("max_scene_cards_per_tick", cfg.scene_cards.max_scene_cards_per_tick);
        cfg.scene_cards.trust_weight = scc.value("trust_weight", cfg.scene_cards.trust_weight);
        cfg.scene_cards.risk_weight = scc.value("risk_weight", cfg.scene_cards.risk_weight);
    }

    return cfg;
}

}  // namespace econlife
