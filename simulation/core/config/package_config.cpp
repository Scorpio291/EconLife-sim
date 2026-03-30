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

        const auto rel = j.value("relationships", nlohmann::json::object());
        cfg.relationships.decay_rate_per_30_ticks =
            rel.value("decay_rate_per_30_ticks", cfg.relationships.decay_rate_per_30_ticks);
        cfg.relationships.max_per_npc =
            rel.value("max_relationships_per_npc", cfg.relationships.max_per_npc);
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

    return cfg;
}

}  // namespace econlife
