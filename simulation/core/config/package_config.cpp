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
        cfg.npc_behavior.knowledge_confidence_decay_rate = nb.value(
            "knowledge_confidence_decay_rate", cfg.npc_behavior.knowledge_confidence_decay_rate);
        cfg.npc_behavior.motivation_shift_rate =
            nb.value("motivation_shift_rate", cfg.npc_behavior.motivation_shift_rate);
        cfg.npc_behavior.base_wage = nb.value("base_wage", cfg.npc_behavior.base_wage);
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
        cfg.production.informal_price_discount =
            j.value("informal_price_discount", cfg.production.informal_price_discount);
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
        cfg.consequence_delays.evidence_max_age_ticks =
            ed.value("max_age_ticks", cfg.consequence_delays.evidence_max_age_ticks);

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
        cfg.random_events.climate_event_amplifier =
            re.value("climate_event_amplifier", cfg.random_events.climate_event_amplifier);
        cfg.random_events.instability_event_amplifier =
            re.value("instability_event_amplifier", cfg.random_events.instability_event_amplifier);
        cfg.random_events.evidence_severity_threshold =
            re.value("evidence_severity_threshold", cfg.random_events.evidence_severity_threshold);
        cfg.random_events.weight_natural =
            re.value("weight_natural", cfg.random_events.weight_natural);
        cfg.random_events.weight_accident =
            re.value("weight_accident", cfg.random_events.weight_accident);
        cfg.random_events.weight_economic =
            re.value("weight_economic", cfg.random_events.weight_economic);
        cfg.random_events.weight_human = re.value("weight_human", cfg.random_events.weight_human);
        cfg.random_events.natural_agri_mod_min =
            re.value("natural_agri_mod_min", cfg.random_events.natural_agri_mod_min);
        cfg.random_events.natural_agri_mod_max =
            re.value("natural_agri_mod_max", cfg.random_events.natural_agri_mod_max);
        cfg.random_events.natural_infra_dmg_min =
            re.value("natural_infra_dmg_min", cfg.random_events.natural_infra_dmg_min);
        cfg.random_events.natural_infra_dmg_max =
            re.value("natural_infra_dmg_max", cfg.random_events.natural_infra_dmg_max);
        cfg.random_events.accident_output_rate_min =
            re.value("accident_output_rate_min", cfg.random_events.accident_output_rate_min);
        cfg.random_events.accident_output_rate_max =
            re.value("accident_output_rate_max", cfg.random_events.accident_output_rate_max);
        cfg.random_events.accident_infra_dmg_min =
            re.value("accident_infra_dmg_min", cfg.random_events.accident_infra_dmg_min);
        cfg.random_events.accident_infra_dmg_max =
            re.value("accident_infra_dmg_max", cfg.random_events.accident_infra_dmg_max);
        cfg.random_events.economic_price_shift_min =
            re.value("economic_price_shift_min", cfg.random_events.economic_price_shift_min);
        cfg.random_events.economic_price_shift_max =
            re.value("economic_price_shift_max", cfg.random_events.economic_price_shift_max);

        const auto in = j.value("influence_network", nlohmann::json::object());
        cfg.influence_network.trust_classification_threshold = in.value(
            "trust_classification_threshold", cfg.influence_network.trust_classification_threshold);
        cfg.influence_network.fear_classification_threshold = in.value(
            "fear_classification_threshold", cfg.influence_network.fear_classification_threshold);
        cfg.influence_network.fear_trust_ceiling =
            in.value("fear_trust_ceiling", cfg.influence_network.fear_trust_ceiling);
        cfg.influence_network.catastrophic_trust_loss_threshold =
            in.value("catastrophic_trust_loss_threshold",
                     cfg.influence_network.catastrophic_trust_loss_threshold);
        cfg.influence_network.catastrophic_trust_floor =
            in.value("catastrophic_trust_floor", cfg.influence_network.catastrophic_trust_floor);
        cfg.influence_network.recovery_ceiling_factor =
            in.value("recovery_ceiling_factor", cfg.influence_network.recovery_ceiling_factor);
        cfg.influence_network.recovery_ceiling_minimum =
            in.value("recovery_ceiling_minimum", cfg.influence_network.recovery_ceiling_minimum);
        cfg.influence_network.obligation_erosion_rate =
            in.value("obligation_erosion_rate", cfg.influence_network.obligation_erosion_rate);
        cfg.influence_network.trust_weight =
            in.value("trust_weight", cfg.influence_network.trust_weight);
        cfg.influence_network.obligation_weight =
            in.value("obligation_weight", cfg.influence_network.obligation_weight);
        cfg.influence_network.fear_weight =
            in.value("fear_weight", cfg.influence_network.fear_weight);
        cfg.influence_network.movement_weight =
            in.value("movement_weight", cfg.influence_network.movement_weight);
        cfg.influence_network.diversity_bonus =
            in.value("diversity_bonus", cfg.influence_network.diversity_bonus);
        cfg.influence_network.health_target_count =
            in.value("health_target_count", cfg.influence_network.health_target_count);

        const auto pc = j.value("political_cycle", nlohmann::json::object());
        cfg.political_cycle.support_threshold =
            pc.value("support_threshold", cfg.political_cycle.support_threshold);
        cfg.political_cycle.oppose_threshold =
            pc.value("oppose_threshold", cfg.political_cycle.oppose_threshold);
        cfg.political_cycle.majority_threshold =
            pc.value("majority_threshold", cfg.political_cycle.majority_threshold);
        cfg.political_cycle.resource_scale =
            pc.value("resource_scale", cfg.political_cycle.resource_scale);
        cfg.political_cycle.resource_max_effect =
            pc.value("resource_max_effect", cfg.political_cycle.resource_max_effect);
        cfg.political_cycle.event_modifier_cap =
            pc.value("event_modifier_cap", cfg.political_cycle.event_modifier_cap);

        const auto ms = j.value("media_system", nlohmann::json::object());
        cfg.media_system.cross_outlet_pickup_rate =
            ms.value("cross_outlet_pickup_rate", cfg.media_system.cross_outlet_pickup_rate);
        cfg.media_system.cross_outlet_amplification_factor =
            ms.value("cross_outlet_amplification_factor",
                     cfg.media_system.cross_outlet_amplification_factor);
        cfg.media_system.social_amplification_multiplier = ms.value(
            "social_amplification_multiplier", cfg.media_system.social_amplification_multiplier);
        cfg.media_system.exposure_per_amplification_unit = ms.value(
            "exposure_per_amplification_unit", cfg.media_system.exposure_per_amplification_unit);
        cfg.media_system.crisis_evidence_threshold =
            ms.value("crisis_evidence_threshold", cfg.media_system.crisis_evidence_threshold);
        cfg.media_system.owner_suppression_base_rate =
            ms.value("owner_suppression_base_rate", cfg.media_system.owner_suppression_base_rate);
        cfg.media_system.propagation_window_ticks =
            ms.value("propagation_window_ticks", cfg.media_system.propagation_window_ticks);
        cfg.media_system.editorial_independence_journalist_bonus =
            ms.value("editorial_independence_journalist_bonus",
                     cfg.media_system.editorial_independence_journalist_bonus);

        const auto ce = j.value("currency_exchange", nlohmann::json::object());
        cfg.currency_exchange.trade_balance_weight =
            ce.value("trade_balance_weight", cfg.currency_exchange.trade_balance_weight);
        cfg.currency_exchange.inflation_weight =
            ce.value("inflation_weight", cfg.currency_exchange.inflation_weight);
        cfg.currency_exchange.sovereign_risk_weight =
            ce.value("sovereign_risk_weight", cfg.currency_exchange.sovereign_risk_weight);
        cfg.currency_exchange.peg_break_reserve_threshold = ce.value(
            "peg_break_reserve_threshold", cfg.currency_exchange.peg_break_reserve_threshold);
        cfg.currency_exchange.floor_fraction =
            ce.value("floor_fraction", cfg.currency_exchange.floor_fraction);
        cfg.currency_exchange.ceiling_fraction =
            ce.value("ceiling_fraction", cfg.currency_exchange.ceiling_fraction);
        cfg.currency_exchange.fx_transaction_cost =
            ce.value("fx_transaction_cost", cfg.currency_exchange.fx_transaction_cost);

        const auto ti = j.value("trade_infrastructure", nlohmann::json::object());
        cfg.trade_infrastructure.mode_speed_road =
            ti.value("mode_speed_road", cfg.trade_infrastructure.mode_speed_road);
        cfg.trade_infrastructure.mode_speed_rail =
            ti.value("mode_speed_rail", cfg.trade_infrastructure.mode_speed_rail);
        cfg.trade_infrastructure.mode_speed_sea =
            ti.value("mode_speed_sea", cfg.trade_infrastructure.mode_speed_sea);
        cfg.trade_infrastructure.mode_speed_river =
            ti.value("mode_speed_river", cfg.trade_infrastructure.mode_speed_river);
        cfg.trade_infrastructure.mode_speed_air =
            ti.value("mode_speed_air", cfg.trade_infrastructure.mode_speed_air);
        cfg.trade_infrastructure.terrain_delay_coeff =
            ti.value("terrain_delay_coeff", cfg.trade_infrastructure.terrain_delay_coeff);
        cfg.trade_infrastructure.infra_delay_coeff =
            ti.value("infra_delay_coeff", cfg.trade_infrastructure.infra_delay_coeff);
        cfg.trade_infrastructure.max_concealment_modifier =
            ti.value("max_concealment_modifier", cfg.trade_infrastructure.max_concealment_modifier);
        cfg.trade_infrastructure.perishable_decay_base =
            ti.value("perishable_decay_base", cfg.trade_infrastructure.perishable_decay_base);

        const auto ie = j.value("investigator_engine", nlohmann::json::object());
        cfg.investigator_engine.facility_count_normalizer = ie.value(
            "facility_count_normalizer", cfg.investigator_engine.facility_count_normalizer);
        cfg.investigator_engine.detection_to_fill_rate_scale = ie.value(
            "detection_to_fill_rate_scale", cfg.investigator_engine.detection_to_fill_rate_scale);
        cfg.investigator_engine.fill_rate_max =
            ie.value("fill_rate_max", cfg.investigator_engine.fill_rate_max);
        cfg.investigator_engine.personnel_violence_multiplier = ie.value(
            "personnel_violence_multiplier", cfg.investigator_engine.personnel_violence_multiplier);
        cfg.investigator_engine.surveillance_threshold =
            ie.value("surveillance_threshold", cfg.investigator_engine.surveillance_threshold);
        cfg.investigator_engine.formal_inquiry_threshold =
            ie.value("formal_inquiry_threshold", cfg.investigator_engine.formal_inquiry_threshold);
        cfg.investigator_engine.raid_threshold =
            ie.value("raid_threshold", cfg.investigator_engine.raid_threshold);
        cfg.investigator_engine.warrant_trust_min =
            ie.value("warrant_trust_min", cfg.investigator_engine.warrant_trust_min);
        cfg.investigator_engine.decay_rate =
            ie.value("decay_rate", cfg.investigator_engine.decay_rate);
        cfg.investigator_engine.default_corruption_susceptibility =
            ie.value("default_corruption_susceptibility",
                     cfg.investigator_engine.default_corruption_susceptibility);

        const auto nbc = j.value("npc_business", nlohmann::json::object());
        cfg.npc_business.cash_critical_months =
            nbc.value("cash_critical_months", cfg.npc_business.cash_critical_months);
        cfg.npc_business.cash_comfortable_months =
            nbc.value("cash_comfortable_months", cfg.npc_business.cash_comfortable_months);
        cfg.npc_business.cash_surplus_months =
            nbc.value("cash_surplus_months", cfg.npc_business.cash_surplus_months);
        cfg.npc_business.exit_market_threshold =
            nbc.value("exit_market_threshold", cfg.npc_business.exit_market_threshold);
        cfg.npc_business.exit_probability =
            nbc.value("exit_probability", cfg.npc_business.exit_probability);
        cfg.npc_business.expansion_return_threshold =
            nbc.value("expansion_return_threshold", cfg.npc_business.expansion_return_threshold);
        cfg.npc_business.ticks_per_quarter =
            nbc.value("ticks_per_quarter", cfg.npc_business.ticks_per_quarter);
        cfg.npc_business.dispatch_period =
            nbc.value("dispatch_period", cfg.npc_business.dispatch_period);
        cfg.npc_business.quality_player_rd_rate =
            nbc.value("quality_player_rd_rate", cfg.npc_business.quality_player_rd_rate);
        cfg.npc_business.fast_expander_rd_rate =
            nbc.value("fast_expander_rd_rate", cfg.npc_business.fast_expander_rd_rate);
        cfg.npc_business.cost_cutter_layoff_fraction =
            nbc.value("cost_cutter_layoff_fraction", cfg.npc_business.cost_cutter_layoff_fraction);
        cfg.npc_business.board_captured_threshold =
            nbc.value("board_captured_threshold", cfg.npc_business.board_captured_threshold);
        cfg.npc_business.board_risky_block_threshold =
            nbc.value("board_risky_block_threshold", cfg.npc_business.board_risky_block_threshold);

        const auto gb = j.value("government_budget", nlohmann::json::object());
        cfg.government_budget.ticks_per_quarter =
            gb.value("ticks_per_quarter", cfg.government_budget.ticks_per_quarter);
        cfg.government_budget.infrastructure_decay_per_quarter =
            gb.value("infrastructure_decay_per_quarter",
                     cfg.government_budget.infrastructure_decay_per_quarter);
        cfg.government_budget.infrastructure_investment_scale =
            gb.value("infrastructure_investment_scale",
                     cfg.government_budget.infrastructure_investment_scale);
        cfg.government_budget.debt_warning_ratio =
            gb.value("debt_warning_ratio", cfg.government_budget.debt_warning_ratio);
        cfg.government_budget.debt_crisis_ratio =
            gb.value("debt_crisis_ratio", cfg.government_budget.debt_crisis_ratio);
        cfg.government_budget.city_revenue_fraction =
            gb.value("city_revenue_fraction", cfg.government_budget.city_revenue_fraction);
        cfg.government_budget.corruption_evidence_threshold = gb.value(
            "corruption_evidence_threshold", cfg.government_budget.corruption_evidence_threshold);
        cfg.government_budget.spending_stability_scale =
            gb.value("spending_stability_scale", cfg.government_budget.spending_stability_scale);
        cfg.government_budget.spending_crime_scale =
            gb.value("spending_crime_scale", cfg.government_budget.spending_crime_scale);
        cfg.government_budget.spending_inequality_scale =
            gb.value("spending_inequality_scale", cfg.government_budget.spending_inequality_scale);
        cfg.government_budget.cohort_mod_working_class =
            gb.value("cohort_mod_working_class", cfg.government_budget.cohort_mod_working_class);
        cfg.government_budget.cohort_mod_professional =
            gb.value("cohort_mod_professional", cfg.government_budget.cohort_mod_professional);
        cfg.government_budget.cohort_mod_corporate =
            gb.value("cohort_mod_corporate", cfg.government_budget.cohort_mod_corporate);
        cfg.government_budget.cohort_mod_criminal_adjacent = gb.value(
            "cohort_mod_criminal_adjacent", cfg.government_budget.cohort_mod_criminal_adjacent);

        const auto hc = j.value("healthcare", nlohmann::json::object());
        cfg.healthcare.base_recovery_rate =
            hc.value("base_recovery_rate", cfg.healthcare.base_recovery_rate);
        cfg.healthcare.critical_health_threshold =
            hc.value("critical_health_threshold", cfg.healthcare.critical_health_threshold);
        cfg.healthcare.treatment_health_boost =
            hc.value("treatment_health_boost", cfg.healthcare.treatment_health_boost);
        cfg.healthcare.overload_threshold =
            hc.value("overload_threshold", cfg.healthcare.overload_threshold);
        cfg.healthcare.overload_quality_penalty =
            hc.value("overload_quality_penalty", cfg.healthcare.overload_quality_penalty);
        cfg.healthcare.labour_impairment_threshold =
            hc.value("labour_impairment_threshold", cfg.healthcare.labour_impairment_threshold);
        cfg.healthcare.labour_supply_impact =
            hc.value("labour_supply_impact", cfg.healthcare.labour_supply_impact);
        cfg.healthcare.capacity_per_treatment =
            hc.value("capacity_per_treatment", cfg.healthcare.capacity_per_treatment);

        const auto scc = j.value("scene_cards", nlohmann::json::object());
        cfg.scene_cards.max_scene_cards_per_tick =
            scc.value("max_scene_cards_per_tick", cfg.scene_cards.max_scene_cards_per_tick);
        cfg.scene_cards.trust_weight = scc.value("trust_weight", cfg.scene_cards.trust_weight);
        cfg.scene_cards.risk_weight = scc.value("risk_weight", cfg.scene_cards.risk_weight);

        const auto ct = j.value("commodity_trading", nlohmann::json::object());
        cfg.commodity_trading.market_impact_threshold =
            ct.value("market_impact_threshold", cfg.commodity_trading.market_impact_threshold);
        cfg.commodity_trading.market_impact_coefficient =
            ct.value("market_impact_coefficient", cfg.commodity_trading.market_impact_coefficient);
        cfg.commodity_trading.capital_gains_tax_rate =
            ct.value("capital_gains_tax_rate", cfg.commodity_trading.capital_gains_tax_rate);

        const auto pe = j.value("price_engine", nlohmann::json::object());
        cfg.price_engine.supply_floor = pe.value("supply_floor", cfg.price_engine.supply_floor);
        cfg.price_engine.default_price_adjustment_rate = pe.value(
            "default_price_adjustment_rate", cfg.price_engine.default_price_adjustment_rate);
        cfg.price_engine.max_price_change_per_tick =
            pe.value("max_price_change_per_tick", cfg.price_engine.max_price_change_per_tick);
        cfg.price_engine.export_floor_coeff =
            pe.value("export_floor_coeff", cfg.price_engine.export_floor_coeff);
        cfg.price_engine.import_ceiling_coeff =
            pe.value("import_ceiling_coeff", cfg.price_engine.import_ceiling_coeff);
        cfg.price_engine.default_base_price =
            pe.value("default_base_price", cfg.price_engine.default_base_price);

        const auto sa = j.value("seasonal_agriculture", nlohmann::json::object());
        cfg.seasonal_agriculture.ticks_per_year =
            sa.value("ticks_per_year", cfg.seasonal_agriculture.ticks_per_year);
        cfg.seasonal_agriculture.planting_duration_ticks =
            sa.value("planting_duration_ticks", cfg.seasonal_agriculture.planting_duration_ticks);
        cfg.seasonal_agriculture.harvest_duration_ticks =
            sa.value("harvest_duration_ticks", cfg.seasonal_agriculture.harvest_duration_ticks);
        cfg.seasonal_agriculture.fallow_soil_recovery_rate = sa.value(
            "fallow_soil_recovery_rate", cfg.seasonal_agriculture.fallow_soil_recovery_rate);
        cfg.seasonal_agriculture.soil_health_max =
            sa.value("soil_health_max", cfg.seasonal_agriculture.soil_health_max);
        cfg.seasonal_agriculture.soil_health_min_monoculture = sa.value(
            "soil_health_min_monoculture", cfg.seasonal_agriculture.soil_health_min_monoculture);
        cfg.seasonal_agriculture.monoculture_penalty_threshold =
            sa.value("monoculture_penalty_threshold",
                     cfg.seasonal_agriculture.monoculture_penalty_threshold);
        cfg.seasonal_agriculture.monoculture_soil_penalty_rate =
            sa.value("monoculture_soil_penalty_rate",
                     cfg.seasonal_agriculture.monoculture_soil_penalty_rate);
        cfg.seasonal_agriculture.southern_hemisphere_offset = sa.value(
            "southern_hemisphere_offset", cfg.seasonal_agriculture.southern_hemisphere_offset);
        cfg.seasonal_agriculture.perennial_base =
            sa.value("perennial_base", cfg.seasonal_agriculture.perennial_base);
        cfg.seasonal_agriculture.perennial_amplitude =
            sa.value("perennial_amplitude", cfg.seasonal_agriculture.perennial_amplitude);
        cfg.seasonal_agriculture.livestock_base =
            sa.value("livestock_base", cfg.seasonal_agriculture.livestock_base);
        cfg.seasonal_agriculture.livestock_amplitude =
            sa.value("livestock_amplitude", cfg.seasonal_agriculture.livestock_amplitude);
        cfg.seasonal_agriculture.timber_multiplier =
            sa.value("timber_multiplier", cfg.seasonal_agriculture.timber_multiplier);

        const auto rst = j.value("real_estate", nlohmann::json::object());
        cfg.real_estate.residential_yield_rate =
            rst.value("residential_yield_rate", cfg.real_estate.residential_yield_rate);
        cfg.real_estate.commercial_yield_rate =
            rst.value("commercial_yield_rate", cfg.real_estate.commercial_yield_rate);
        cfg.real_estate.industrial_yield_rate =
            rst.value("industrial_yield_rate", cfg.real_estate.industrial_yield_rate);
        cfg.real_estate.price_convergence_rate =
            rst.value("price_convergence_rate", cfg.real_estate.price_convergence_rate);
        cfg.real_estate.convergence_interval =
            rst.value("convergence_interval", cfg.real_estate.convergence_interval);
        cfg.real_estate.criminal_dominance_penalty =
            rst.value("criminal_dominance_penalty", cfg.real_estate.criminal_dominance_penalty);
        cfg.real_estate.laundering_premium =
            rst.value("laundering_premium", cfg.real_estate.laundering_premium);
        cfg.real_estate.transaction_evidence_threshold = rst.value(
            "transaction_evidence_threshold", cfg.real_estate.transaction_evidence_threshold);

        const auto fd = j.value("financial_distribution", nlohmann::json::object());
        cfg.financial_distribution.ticks_per_quarter =
            fd.value("ticks_per_quarter", cfg.financial_distribution.ticks_per_quarter);
        cfg.financial_distribution.deferred_salary_max_ticks = fd.value(
            "deferred_salary_max_ticks", cfg.financial_distribution.deferred_salary_max_ticks);
        cfg.financial_distribution.draw_reporting_threshold = fd.value(
            "draw_reporting_threshold", cfg.financial_distribution.draw_reporting_threshold);
        cfg.financial_distribution.ticks_per_month =
            fd.value("ticks_per_month", cfg.financial_distribution.ticks_per_month);
        cfg.financial_distribution.cash_surplus_months =
            fd.value("cash_surplus_months", cfg.financial_distribution.cash_surplus_months);
        cfg.financial_distribution.board_rubber_stamp_threshold =
            fd.value("board_rubber_stamp_threshold",
                     cfg.financial_distribution.board_rubber_stamp_threshold);
        cfg.financial_distribution.board_approval_bonus_threshold =
            fd.value("board_approval_bonus_threshold",
                     cfg.financial_distribution.board_approval_bonus_threshold);
        cfg.financial_distribution.default_tax_withholding_rate =
            fd.value("default_tax_withholding_rate",
                     cfg.financial_distribution.default_tax_withholding_rate);
        cfg.financial_distribution.owners_draw_fraction =
            fd.value("owners_draw_fraction", cfg.financial_distribution.owners_draw_fraction);
        cfg.financial_distribution.wage_theft_emotional_weight = fd.value(
            "wage_theft_emotional_weight", cfg.financial_distribution.wage_theft_emotional_weight);

        const auto nbm = j.value("npc_behavior_module", nlohmann::json::object());
        cfg.npc_behavior_module.inaction_threshold =
            nbm.value("inaction_threshold", cfg.npc_behavior_module.inaction_threshold);
        cfg.npc_behavior_module.min_risk_discount =
            nbm.value("min_risk_discount", cfg.npc_behavior_module.min_risk_discount);
        cfg.npc_behavior_module.risk_sensitivity_coeff =
            nbm.value("risk_sensitivity_coeff", cfg.npc_behavior_module.risk_sensitivity_coeff);
        cfg.npc_behavior_module.trust_ev_bonus =
            nbm.value("trust_ev_bonus", cfg.npc_behavior_module.trust_ev_bonus);
        cfg.npc_behavior_module.recovery_ceiling_minimum =
            nbm.value("recovery_ceiling_minimum", cfg.npc_behavior_module.recovery_ceiling_minimum);

        const auto on = j.value("obligation_network", nlohmann::json::object());
        cfg.obligation_network.escalation_rate_base =
            on.value("escalation_rate_base", cfg.obligation_network.escalation_rate_base);
        cfg.obligation_network.escalation_threshold =
            on.value("escalation_threshold", cfg.obligation_network.escalation_threshold);
        cfg.obligation_network.critical_threshold =
            on.value("critical_threshold", cfg.obligation_network.critical_threshold);
        cfg.obligation_network.hostile_action_threshold =
            on.value("hostile_action_threshold", cfg.obligation_network.hostile_action_threshold);
        cfg.obligation_network.wealth_reference_scale =
            on.value("wealth_reference_scale", cfg.obligation_network.wealth_reference_scale);
        cfg.obligation_network.max_wealth_factor =
            on.value("max_wealth_factor", cfg.obligation_network.max_wealth_factor);
        cfg.obligation_network.trust_erosion_per_tick =
            on.value("trust_erosion_per_tick", cfg.obligation_network.trust_erosion_per_tick);
        cfg.obligation_network.orphan_obligation_timeout_ticks =
            on.value("orphan_obligation_timeout_ticks",
                     cfg.obligation_network.orphan_obligation_timeout_ticks);

        const auto co = j.value("criminal_operations", nlohmann::json::object());
        cfg.criminal_operations.quarterly_interval =
            co.value("quarterly_interval", cfg.criminal_operations.quarterly_interval);
        cfg.criminal_operations.le_heat_threshold =
            co.value("le_heat_threshold", cfg.criminal_operations.le_heat_threshold);
        cfg.criminal_operations.territory_pressure_conflict_threshold =
            co.value("territory_pressure_conflict_threshold",
                     cfg.criminal_operations.territory_pressure_conflict_threshold);
        cfg.criminal_operations.cash_comfortable_months =
            co.value("cash_comfortable_months", cfg.criminal_operations.cash_comfortable_months);
        cfg.criminal_operations.cash_low_threshold =
            co.value("cash_low_threshold", cfg.criminal_operations.cash_low_threshold);
        cfg.criminal_operations.territory_pressure_expand_threshold =
            co.value("territory_pressure_expand_threshold",
                     cfg.criminal_operations.territory_pressure_expand_threshold);
        cfg.criminal_operations.le_heat_expand_threshold =
            co.value("le_heat_expand_threshold", cfg.criminal_operations.le_heat_expand_threshold);
        cfg.criminal_operations.expansion_initial_dominance = co.value(
            "expansion_initial_dominance", cfg.criminal_operations.expansion_initial_dominance);
        cfg.criminal_operations.cash_per_expansion_slot =
            co.value("cash_per_expansion_slot", cfg.criminal_operations.cash_per_expansion_slot);
        cfg.criminal_operations.min_expansion_team_size =
            co.value("min_expansion_team_size", cfg.criminal_operations.min_expansion_team_size);
        cfg.criminal_operations.expansion_refund_fraction = co.value(
            "expansion_refund_fraction", cfg.criminal_operations.expansion_refund_fraction);
        cfg.criminal_operations.dormant_dominance_decay_rate = co.value(
            "dormant_dominance_decay_rate", cfg.criminal_operations.dormant_dominance_decay_rate);

        const auto cr = j.value("community_response", nlohmann::json::object());
        cfg.community_response.ema_alpha = cr.value("ema_alpha", cfg.community_response.ema_alpha);
        cfg.community_response.social_capital_max =
            cr.value("social_capital_max", cfg.community_response.social_capital_max);
        cfg.community_response.capital_normalizer =
            cr.value("capital_normalizer", cfg.community_response.capital_normalizer);
        cfg.community_response.social_normalizer =
            cr.value("social_normalizer", cfg.community_response.social_normalizer);
        cfg.community_response.memory_decay_floor =
            cr.value("memory_decay_floor", cfg.community_response.memory_decay_floor);
        cfg.community_response.grievance_normalizer =
            cr.value("grievance_normalizer", cfg.community_response.grievance_normalizer);
        cfg.community_response.grievance_shock_threshold =
            cr.value("grievance_shock_threshold", cfg.community_response.grievance_shock_threshold);
        cfg.community_response.resistance_revenue_penalty = cr.value(
            "resistance_revenue_penalty", cfg.community_response.resistance_revenue_penalty);
        cfg.community_response.trauma_grievance_floor_scale = cr.value(
            "trauma_grievance_floor_scale", cfg.community_response.trauma_grievance_floor_scale);
        cfg.community_response.trauma_trust_ceiling_scale = cr.value(
            "trauma_trust_ceiling_scale", cfg.community_response.trauma_trust_ceiling_scale);
        cfg.community_response.regression_cooldown_ticks =
            cr.value("regression_cooldown_ticks", cfg.community_response.regression_cooldown_ticks);

        const auto ns = j.value("npc_spending", nlohmann::json::object());
        cfg.npc_spending.reference_income =
            ns.value("reference_income", cfg.npc_spending.reference_income);
        cfg.npc_spending.max_income_factor =
            ns.value("max_income_factor", cfg.npc_spending.max_income_factor);
        cfg.npc_spending.min_price_factor =
            ns.value("min_price_factor", cfg.npc_spending.min_price_factor);
        cfg.npc_spending.default_base_demand_units =
            ns.value("default_base_demand_units", cfg.npc_spending.default_base_demand_units);
        cfg.npc_spending.default_income_elasticity =
            ns.value("default_income_elasticity", cfg.npc_spending.default_income_elasticity);
        cfg.npc_spending.default_price_elasticity =
            ns.value("default_price_elasticity", cfg.npc_spending.default_price_elasticity);
        cfg.npc_spending.default_base_price =
            ns.value("default_base_price", cfg.npc_spending.default_base_price);
        cfg.npc_spending.default_quality_weight =
            ns.value("default_quality_weight", cfg.npc_spending.default_quality_weight);

        const auto at = j.value("antitrust", nlohmann::json::object());
        cfg.antitrust.market_share_threshold =
            at.value("market_share_threshold", cfg.antitrust.market_share_threshold);
        cfg.antitrust.dominant_price_mover_threshold = at.value(
            "dominant_price_mover_threshold", cfg.antitrust.dominant_price_mover_threshold);
        cfg.antitrust.meter_fill_per_threshold_tick =
            at.value("meter_fill_per_threshold_tick", cfg.antitrust.meter_fill_per_threshold_tick);
        cfg.antitrust.dominance_proposal_pressure_per_tick =
            at.value("dominance_proposal_pressure_per_tick",
                     cfg.antitrust.dominance_proposal_pressure_per_tick);
        cfg.antitrust.proposal_pressure_decay_rate =
            at.value("proposal_pressure_decay_rate", cfg.antitrust.proposal_pressure_decay_rate);
        cfg.antitrust.proposal_threshold =
            at.value("proposal_threshold", cfg.antitrust.proposal_threshold);
        cfg.antitrust.monthly_interval =
            at.value("monthly_interval", cfg.antitrust.monthly_interval);

        const auto fs = j.value("facility_signals", nlohmann::json::object());
        cfg.facility_signals.default_weight =
            fs.value("default_weight", cfg.facility_signals.default_weight);
        cfg.facility_signals.karst_mitigation_bonus =
            fs.value("karst_mitigation_bonus", cfg.facility_signals.karst_mitigation_bonus);
        cfg.facility_signals.facility_count_normalizer =
            fs.value("facility_count_normalizer", cfg.facility_signals.facility_count_normalizer);
        cfg.facility_signals.detection_to_fill_rate_scale = fs.value(
            "detection_to_fill_rate_scale", cfg.facility_signals.detection_to_fill_rate_scale);
        cfg.facility_signals.fill_rate_max =
            fs.value("fill_rate_max", cfg.facility_signals.fill_rate_max);
        cfg.facility_signals.surveillance_threshold =
            fs.value("surveillance_threshold", cfg.facility_signals.surveillance_threshold);
        cfg.facility_signals.formal_inquiry_threshold =
            fs.value("formal_inquiry_threshold", cfg.facility_signals.formal_inquiry_threshold);
        cfg.facility_signals.raid_threshold =
            fs.value("raid_threshold", cfg.facility_signals.raid_threshold);
        cfg.facility_signals.notice_threshold =
            fs.value("notice_threshold", cfg.facility_signals.notice_threshold);
        cfg.facility_signals.audit_threshold =
            fs.value("audit_threshold", cfg.facility_signals.audit_threshold);
        cfg.facility_signals.enforcement_threshold =
            fs.value("enforcement_threshold", cfg.facility_signals.enforcement_threshold);
        cfg.facility_signals.meter_decay_rate =
            fs.value("meter_decay_rate", cfg.facility_signals.meter_decay_rate);
        cfg.facility_signals.personnel_violence_multiplier = fs.value(
            "personnel_violence_multiplier", cfg.facility_signals.personnel_violence_multiplier);

        const auto ad = j.value("addiction", nlohmann::json::object());
        cfg.addiction.tolerance_per_use_casual =
            ad.value("tolerance_per_use_casual", cfg.addiction.tolerance_per_use_casual);
        cfg.addiction.regular_use_threshold =
            ad.value("regular_use_threshold", cfg.addiction.regular_use_threshold);
        cfg.addiction.dependency_threshold =
            ad.value("dependency_threshold", cfg.addiction.dependency_threshold);
        cfg.addiction.dependency_tolerance_floor =
            ad.value("dependency_tolerance_floor", cfg.addiction.dependency_tolerance_floor);
        cfg.addiction.active_craving_threshold =
            ad.value("active_craving_threshold", cfg.addiction.active_craving_threshold);
        cfg.addiction.active_duration_ticks =
            ad.value("active_duration_ticks", cfg.addiction.active_duration_ticks);
        cfg.addiction.withdrawal_health_hit =
            ad.value("withdrawal_health_hit", cfg.addiction.withdrawal_health_hit);
        cfg.addiction.dependent_work_efficiency =
            ad.value("dependent_work_efficiency", cfg.addiction.dependent_work_efficiency);
        cfg.addiction.active_work_efficiency =
            ad.value("active_work_efficiency", cfg.addiction.active_work_efficiency);
        cfg.addiction.terminal_work_efficiency =
            ad.value("terminal_work_efficiency", cfg.addiction.terminal_work_efficiency);
        cfg.addiction.recovery_attempt_threshold =
            ad.value("recovery_attempt_threshold", cfg.addiction.recovery_attempt_threshold);
        cfg.addiction.craving_decay_rate_recovery =
            ad.value("craving_decay_rate_recovery", cfg.addiction.craving_decay_rate_recovery);
        cfg.addiction.full_recovery_ticks =
            ad.value("full_recovery_ticks", cfg.addiction.full_recovery_ticks);
        cfg.addiction.recovery_success_threshold =
            ad.value("recovery_success_threshold", cfg.addiction.recovery_success_threshold);
        cfg.addiction.terminal_health_threshold =
            ad.value("terminal_health_threshold", cfg.addiction.terminal_health_threshold);
        cfg.addiction.terminal_persistence_ticks =
            ad.value("terminal_persistence_ticks", cfg.addiction.terminal_persistence_ticks);
        cfg.addiction.rate_delta_per_active_npc =
            ad.value("rate_delta_per_active_npc", cfg.addiction.rate_delta_per_active_npc);
        cfg.addiction.labour_impact_per_addict =
            ad.value("labour_impact_per_addict", cfg.addiction.labour_impact_per_addict);
        cfg.addiction.healthcare_load_per_addict =
            ad.value("healthcare_load_per_addict", cfg.addiction.healthcare_load_per_addict);
        cfg.addiction.grievance_per_addict_fraction =
            ad.value("grievance_per_addict_fraction", cfg.addiction.grievance_per_addict_fraction);
        cfg.addiction.casual_craving_inc =
            ad.value("casual_craving_inc", cfg.addiction.casual_craving_inc);
        cfg.addiction.regular_craving_inc =
            ad.value("regular_craving_inc", cfg.addiction.regular_craving_inc);
        cfg.addiction.dependent_craving_inc =
            ad.value("dependent_craving_inc", cfg.addiction.dependent_craving_inc);
        cfg.addiction.active_craving_inc =
            ad.value("active_craving_inc", cfg.addiction.active_craving_inc);
        cfg.addiction.casual_to_regular_craving =
            ad.value("casual_to_regular_craving", cfg.addiction.casual_to_regular_craving);
        cfg.addiction.regular_to_dependent_craving =
            ad.value("regular_to_dependent_craving", cfg.addiction.regular_to_dependent_craving);

        const auto ai = j.value("alternative_identity", nlohmann::json::object());
        cfg.alternative_identity.documentation_decay_rate =
            ai.value("documentation_decay_rate", cfg.alternative_identity.documentation_decay_rate);
        cfg.alternative_identity.documentation_build_rate =
            ai.value("documentation_build_rate", cfg.alternative_identity.documentation_build_rate);
        cfg.alternative_identity.burn_threshold =
            ai.value("burn_threshold", cfg.alternative_identity.burn_threshold);
        cfg.alternative_identity.witness_confidence =
            ai.value("witness_confidence", cfg.alternative_identity.witness_confidence);
        cfg.alternative_identity.forensic_confidence =
            ai.value("forensic_confidence", cfg.alternative_identity.forensic_confidence);

        const auto pr = j.value("protection_rackets", nlohmann::json::object());
        cfg.protection_rackets.demand_rate =
            pr.value("demand_rate", cfg.protection_rackets.demand_rate);
        cfg.protection_rackets.grievance_per_demand_unit =
            pr.value("grievance_per_demand_unit", cfg.protection_rackets.grievance_per_demand_unit);
        cfg.protection_rackets.incumbent_refuse_probability = pr.value(
            "incumbent_refuse_probability", cfg.protection_rackets.incumbent_refuse_probability);
        cfg.protection_rackets.default_refuse_probability = pr.value(
            "default_refuse_probability", cfg.protection_rackets.default_refuse_probability);
        cfg.protection_rackets.personnel_violence_multiplier = pr.value(
            "personnel_violence_multiplier", cfg.protection_rackets.personnel_violence_multiplier);
        cfg.protection_rackets.warning_threshold =
            pr.value("warning_threshold", cfg.protection_rackets.warning_threshold);
        cfg.protection_rackets.property_damage_threshold =
            pr.value("property_damage_threshold", cfg.protection_rackets.property_damage_threshold);
        cfg.protection_rackets.violence_threshold =
            pr.value("violence_threshold", cfg.protection_rackets.violence_threshold);
        cfg.protection_rackets.abandonment_threshold =
            pr.value("abandonment_threshold", cfg.protection_rackets.abandonment_threshold);
        cfg.protection_rackets.property_damage_severity =
            pr.value("property_damage_severity", cfg.protection_rackets.property_damage_severity);
        cfg.protection_rackets.memory_emotional_weight_warning =
            pr.value("memory_emotional_weight_warning",
                     cfg.protection_rackets.memory_emotional_weight_warning);

        const auto ml = j.value("money_laundering", nlohmann::json::object());
        cfg.money_laundering.structuring_token_interval =
            ml.value("structuring_token_interval", cfg.money_laundering.structuring_token_interval);
        cfg.money_laundering.shell_chain_evidence_interval = ml.value(
            "shell_chain_evidence_interval", cfg.money_laundering.shell_chain_evidence_interval);
        cfg.money_laundering.trade_invoice_evidence_interval =
            ml.value("trade_invoice_evidence_interval",
                     cfg.money_laundering.trade_invoice_evidence_interval);
        cfg.money_laundering.commingling_evidence_interval = ml.value(
            "commingling_evidence_interval", cfg.money_laundering.commingling_evidence_interval);
        cfg.money_laundering.max_chain_depth =
            ml.value("max_chain_depth", cfg.money_laundering.max_chain_depth);
        cfg.money_laundering.commingle_capacity_fraction = ml.value(
            "commingle_capacity_fraction", cfg.money_laundering.commingle_capacity_fraction);
        cfg.money_laundering.rate_commingle_max =
            ml.value("rate_commingle_max", cfg.money_laundering.rate_commingle_max);
        cfg.money_laundering.crypto_evidence_skill_divisor = ml.value(
            "crypto_evidence_skill_divisor", cfg.money_laundering.crypto_evidence_skill_divisor);
        cfg.money_laundering.fiu_token_threshold =
            ml.value("fiu_token_threshold", cfg.money_laundering.fiu_token_threshold);
        cfg.money_laundering.fiu_meter_fill_scale =
            ml.value("fiu_meter_fill_scale", cfg.money_laundering.fiu_meter_fill_scale);
        cfg.money_laundering.fiu_monthly_interval =
            ml.value("fiu_monthly_interval", cfg.money_laundering.fiu_monthly_interval);
        cfg.money_laundering.structuring_deposit_count_threshold =
            ml.value("structuring_deposit_count_threshold",
                     cfg.money_laundering.structuring_deposit_count_threshold);
        cfg.money_laundering.org_capacity_multiplier =
            ml.value("org_capacity_multiplier", cfg.money_laundering.org_capacity_multiplier);
        cfg.money_laundering.ticks_per_quarter =
            ml.value("ticks_per_quarter", cfg.money_laundering.ticks_per_quarter);

        const auto dd = j.value("designer_drug", nlohmann::json::object());
        cfg.designer_drug.detection_threshold =
            dd.value("detection_threshold", cfg.designer_drug.detection_threshold);
        cfg.designer_drug.base_review_duration =
            dd.value("base_review_duration", cfg.designer_drug.base_review_duration);
        cfg.designer_drug.unscheduled_margin =
            dd.value("unscheduled_margin", cfg.designer_drug.unscheduled_margin);
        cfg.designer_drug.scheduled_margin =
            dd.value("scheduled_margin", cfg.designer_drug.scheduled_margin);
        cfg.designer_drug.no_successor_margin =
            dd.value("no_successor_margin", cfg.designer_drug.no_successor_margin);
        cfg.designer_drug.monthly_interval =
            dd.value("monthly_interval", cfg.designer_drug.monthly_interval);

        const auto de = j.value("drug_economy", nlohmann::json::object());
        cfg.drug_economy.wholesale_price_fraction =
            de.value("wholesale_price_fraction", cfg.drug_economy.wholesale_price_fraction);
        cfg.drug_economy.wholesale_quality_degradation = de.value(
            "wholesale_quality_degradation", cfg.drug_economy.wholesale_quality_degradation);
        cfg.drug_economy.retail_quality_degradation =
            de.value("retail_quality_degradation", cfg.drug_economy.retail_quality_degradation);
        cfg.drug_economy.meth_waste_per_unit =
            de.value("meth_waste_per_unit", cfg.drug_economy.meth_waste_per_unit);
        cfg.drug_economy.demand_per_addict =
            de.value("demand_per_addict", cfg.drug_economy.demand_per_addict);
        cfg.drug_economy.precursor_ratio_meth =
            de.value("precursor_ratio_meth", cfg.drug_economy.precursor_ratio_meth);
        cfg.drug_economy.designer_legal_margin_mult =
            de.value("designer_legal_margin_mult", cfg.drug_economy.designer_legal_margin_mult);

        const auto rc = j.value("regional_conditions", nlohmann::json::object());
        cfg.regional_conditions.stability_recovery_rate =
            rc.value("stability_recovery_rate", cfg.regional_conditions.stability_recovery_rate);
        cfg.regional_conditions.event_stability_impact =
            rc.value("event_stability_impact", cfg.regional_conditions.event_stability_impact);
        cfg.regional_conditions.infrastructure_decay_rate = rc.value(
            "infrastructure_decay_rate", cfg.regional_conditions.infrastructure_decay_rate);
        cfg.regional_conditions.drought_recovery_rate =
            rc.value("drought_recovery_rate", cfg.regional_conditions.drought_recovery_rate);
        cfg.regional_conditions.flood_recovery_rate =
            rc.value("flood_recovery_rate", cfg.regional_conditions.flood_recovery_rate);

        const auto tu = j.value("trust_updates", nlohmann::json::object());
        cfg.trust_updates.catastrophic_trust_loss_threshold =
            tu.value("catastrophic_trust_loss_threshold",
                     cfg.trust_updates.catastrophic_trust_loss_threshold);
        cfg.trust_updates.catastrophic_trust_floor =
            tu.value("catastrophic_trust_floor", cfg.trust_updates.catastrophic_trust_floor);
        cfg.trust_updates.recovery_ceiling_factor =
            tu.value("recovery_ceiling_factor", cfg.trust_updates.recovery_ceiling_factor);
        cfg.trust_updates.recovery_ceiling_minimum =
            tu.value("recovery_ceiling_minimum", cfg.trust_updates.recovery_ceiling_minimum);
        cfg.trust_updates.significant_change_threshold = tu.value(
            "significant_change_threshold", cfg.trust_updates.significant_change_threshold);
        cfg.trust_updates.trust_min = tu.value("trust_min", cfg.trust_updates.trust_min);
        cfg.trust_updates.trust_max = tu.value("trust_max", cfg.trust_updates.trust_max);
        cfg.trust_updates.default_recovery_ceiling =
            tu.value("default_recovery_ceiling", cfg.trust_updates.default_recovery_ceiling);

        const auto wt = j.value("weapons_trafficking", nlohmann::json::object());
        cfg.weapons_trafficking.base_price_small_arms =
            wt.value("base_price_small_arms", cfg.weapons_trafficking.base_price_small_arms);
        cfg.weapons_trafficking.base_price_ammunition =
            wt.value("base_price_ammunition", cfg.weapons_trafficking.base_price_ammunition);
        cfg.weapons_trafficking.base_price_heavy_weapons =
            wt.value("base_price_heavy_weapons", cfg.weapons_trafficking.base_price_heavy_weapons);
        cfg.weapons_trafficking.base_price_converted_legal = wt.value(
            "base_price_converted_legal", cfg.weapons_trafficking.base_price_converted_legal);
        cfg.weapons_trafficking.price_floor_supply =
            wt.value("price_floor_supply", cfg.weapons_trafficking.price_floor_supply);
        cfg.weapons_trafficking.max_diversion_fraction =
            wt.value("max_diversion_fraction", cfg.weapons_trafficking.max_diversion_fraction);
        cfg.weapons_trafficking.chain_custody_actionability = wt.value(
            "chain_custody_actionability", cfg.weapons_trafficking.chain_custody_actionability);
        cfg.weapons_trafficking.embargo_meter_spike =
            wt.value("embargo_meter_spike", cfg.weapons_trafficking.embargo_meter_spike);
        cfg.weapons_trafficking.trust_threshold_diversion = wt.value(
            "trust_threshold_diversion", cfg.weapons_trafficking.trust_threshold_diversion);

        const auto pa = j.value("population_aging", nlohmann::json::object());
        cfg.population_aging.cohort_income_update_rate =
            pa.value("cohort_income_update_rate", cfg.population_aging.cohort_income_update_rate);
        cfg.population_aging.cohort_employment_update_rate = pa.value(
            "cohort_employment_update_rate", cfg.population_aging.cohort_employment_update_rate);
        cfg.population_aging.max_education_drift_per_year = pa.value(
            "max_education_drift_per_year", cfg.population_aging.max_education_drift_per_year);

        const auto ls = j.value("lod_system", nlohmann::json::object());
        cfg.lod_system.lod2_min_modifier =
            ls.value("lod2_min_modifier", cfg.lod_system.lod2_min_modifier);
        cfg.lod_system.lod2_max_modifier =
            ls.value("lod2_max_modifier", cfg.lod_system.lod2_max_modifier);
        cfg.lod_system.lod2_smoothing_rate =
            ls.value("lod2_smoothing_rate", cfg.lod_system.lod2_smoothing_rate);
        cfg.lod_system.supply_floor = ls.value("supply_floor", cfg.lod_system.supply_floor);

        const auto sc = j.value("supply_chain_module", nlohmann::json::object());
        cfg.supply_chain_module.base_transport_rate =
            sc.value("base_transport_rate", cfg.supply_chain_module.base_transport_rate);
        cfg.supply_chain_module.terrain_cost_coeff =
            sc.value("terrain_cost_coeff", cfg.supply_chain_module.terrain_cost_coeff);
        cfg.supply_chain_module.infra_speed_coeff =
            sc.value("infra_speed_coeff", cfg.supply_chain_module.infra_speed_coeff);
        cfg.supply_chain_module.road_speed =
            sc.value("road_speed", cfg.supply_chain_module.road_speed);
        cfg.supply_chain_module.rail_speed =
            sc.value("rail_speed", cfg.supply_chain_module.rail_speed);
        cfg.supply_chain_module.sea_speed =
            sc.value("sea_speed", cfg.supply_chain_module.sea_speed);
        cfg.supply_chain_module.river_speed =
            sc.value("river_speed", cfg.supply_chain_module.river_speed);
        cfg.supply_chain_module.air_speed =
            sc.value("air_speed", cfg.supply_chain_module.air_speed);
        cfg.supply_chain_module.max_concealment_modifier =
            sc.value("max_concealment_modifier", cfg.supply_chain_module.max_concealment_modifier);
        cfg.supply_chain_module.base_interception_risk =
            sc.value("base_interception_risk", cfg.supply_chain_module.base_interception_risk);
        cfg.supply_chain_module.default_perishable_decay_rate = sc.value(
            "default_perishable_decay_rate", cfg.supply_chain_module.default_perishable_decay_rate);

        const auto lm = j.value("labor_module", nlohmann::json::object());
        cfg.labor_module.wage_adjustment_rate =
            lm.value("wage_adjustment_rate", cfg.labor_module.wage_adjustment_rate);
        cfg.labor_module.wage_floor = lm.value("wage_floor", cfg.labor_module.wage_floor);
        cfg.labor_module.wage_ceiling_multiplier =
            lm.value("wage_ceiling_multiplier", cfg.labor_module.wage_ceiling_multiplier);
        cfg.labor_module.pool_size_public =
            lm.value("pool_size_public", cfg.labor_module.pool_size_public);
        cfg.labor_module.pool_size_professional =
            lm.value("pool_size_professional", cfg.labor_module.pool_size_professional);
        cfg.labor_module.pool_size_referral =
            lm.value("pool_size_referral", cfg.labor_module.pool_size_referral);
        cfg.labor_module.reputation_threshold =
            lm.value("reputation_threshold", cfg.labor_module.reputation_threshold);
        cfg.labor_module.reputation_pool_penalty_scale = lm.value(
            "reputation_pool_penalty_scale", cfg.labor_module.reputation_pool_penalty_scale);
        cfg.labor_module.salary_premium_per_rep_point =
            lm.value("salary_premium_per_rep_point", cfg.labor_module.salary_premium_per_rep_point);
        cfg.labor_module.voluntary_departure_threshold = lm.value(
            "voluntary_departure_threshold", cfg.labor_module.voluntary_departure_threshold);
        cfg.labor_module.departure_base_rate =
            lm.value("departure_base_rate", cfg.labor_module.departure_base_rate);
        cfg.labor_module.reputation_default =
            lm.value("reputation_default", cfg.labor_module.reputation_default);
        cfg.labor_module.deferred_salary_max_ticks =
            lm.value("deferred_salary_max_ticks", cfg.labor_module.deferred_salary_max_ticks);
        cfg.labor_module.personal_referral_trust_min =
            lm.value("personal_referral_trust_min", cfg.labor_module.personal_referral_trust_min);
        cfg.labor_module.monthly_tick_interval =
            lm.value("monthly_tick_interval", cfg.labor_module.monthly_tick_interval);
    }

    // -----------------------------------------------------------------
    // business_lifecycle.json
    // -----------------------------------------------------------------
    {
        auto j = load_json(config_dir + "/business_lifecycle.json");

        cfg.business_lifecycle.stranded_revenue_floor =
            j.value("stranded_revenue_floor", cfg.business_lifecycle.stranded_revenue_floor);

        // Parse BusinessSector from string.
        auto parse_sector = [](const std::string& s) -> BusinessSector {
            if (s == "food_beverage")
                return BusinessSector::food_beverage;
            if (s == "retail")
                return BusinessSector::retail;
            if (s == "services")
                return BusinessSector::services;
            if (s == "real_estate")
                return BusinessSector::real_estate;
            if (s == "agriculture")
                return BusinessSector::agriculture;
            if (s == "energy")
                return BusinessSector::energy;
            if (s == "technology")
                return BusinessSector::technology;
            if (s == "finance")
                return BusinessSector::finance;
            if (s == "transport_logistics")
                return BusinessSector::transport_logistics;
            if (s == "media")
                return BusinessSector::media;
            if (s == "security")
                return BusinessSector::security;
            if (s == "research")
                return BusinessSector::research;
            if (s == "criminal")
                return BusinessSector::criminal;
            return BusinessSector::manufacturing;  // default
        };

        // Parse BusinessProfile from string.
        auto parse_profile = [](const std::string& s) -> BusinessProfile {
            if (s == "quality_player")
                return BusinessProfile::quality_player;
            if (s == "fast_expander")
                return BusinessProfile::fast_expander;
            if (s == "defensive_incumbent")
                return BusinessProfile::defensive_incumbent;
            return BusinessProfile::cost_cutter;  // default
        };

        const auto& stranded_j = j.value("stranded_sectors", nlohmann::json::object());
        for (const auto& [era_str, entries] : stranded_j.items()) {
            uint8_t era = static_cast<uint8_t>(std::stoi(era_str));
            for (const auto& e : entries) {
                StrandedSectorEntry entry{};
                entry.sector = parse_sector(e.value("sector", std::string("energy")));
                entry.revenue_penalty = e.value("revenue_penalty", 0.0f);
                entry.cost_increase = e.value("cost_increase", 0.0f);
                cfg.business_lifecycle.stranded_sectors[era].push_back(entry);
            }
        }

        const auto& emerging_j = j.value("emerging_sectors", nlohmann::json::object());
        for (const auto& [era_str, entries] : emerging_j.items()) {
            uint8_t era = static_cast<uint8_t>(std::stoi(era_str));
            for (const auto& e : entries) {
                EmergingSectorEntry entry{};
                entry.sector = parse_sector(e.value("sector", std::string("technology")));
                entry.spawn_fraction = e.value("spawn_fraction", 0.0f);
                entry.profile = parse_profile(e.value("profile", std::string("fast_expander")));
                cfg.business_lifecycle.emerging_sectors[era].push_back(entry);
            }
        }
    }

    return cfg;
}

}  // namespace econlife
