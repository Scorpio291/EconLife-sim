#include "register_base_game_modules.h"

#include <memory>

#include "core/tick/tick_orchestrator.h"

// Section grouping below is by module domain, not dependency tier.
// Execution order is decided by each module's runs_after()/runs_before()
// declarations and resolved by TickOrchestrator's topological sort —
// the registration order here only matters as a tiebreaker (see M1
// in docs/CODEBASE_AUDIT_REPORT.md).

// Player input
#include "modules/player_actions/player_actions_module.h"

// Production, markets, and trade
#include "modules/business_lifecycle/business_lifecycle_module.h"
#include "modules/commodity_trading/commodity_trading_module.h"
#include "modules/labor_market/labor_market_module.h"
#include "modules/npc_business/npc_business_module.h"
#include "modules/price_engine/price_engine_module.h"
#include "modules/production/production_module.h"
#include "modules/real_estate/real_estate_module.h"
#include "modules/seasonal_agriculture/seasonal_agriculture_module.h"
#include "modules/supply_chain/supply_chain_module.h"
#include "modules/trade_infrastructure/trade_infrastructure_module.h"

// Finance, government, and population
#include "modules/banking/banking_module.h"
#include "modules/currency_exchange/currency_exchange_module.h"
#include "modules/financial_distribution/financial_distribution_module.h"
#include "modules/government_budget/government_budget_module.h"
#include "modules/healthcare/healthcare_module.h"
#include "modules/population_aging/population_aging_module.h"

// Calendar, scenes, technology, and random events
#include "modules/calendar/calendar_module.h"
#include "modules/random_events/random_events_module.h"
#include "modules/scene_cards/scene_cards_module.h"
#include "modules/technology/technology_module.h"

// NPC behaviour, spending, relationships
#include "modules/npc_behavior/npc_behavior_module.h"
#include "modules/npc_spending/npc_spending_module.h"
#include "modules/obligation_network/obligation_network_module.h"
#include "modules/trust_updates/trust_updates_module.h"

// Community, political, and regional dynamics
#include "modules/community_response/community_response_module.h"
#include "modules/influence_network/influence_network_module.h"
#include "modules/political_cycle/political_cycle_module.h"
#include "modules/regional_conditions/regional_conditions_module.h"

// Evidence, investigation, antitrust, and media
#include "modules/antitrust/antitrust_module.h"
#include "modules/evidence/evidence_module.h"
#include "modules/facility_signals/facility_signals_module.h"
#include "modules/investigator_engine/investigator_engine_module.h"
#include "modules/legal_process/legal_process_module.h"
#include "modules/media_system/media_system_module.h"

// Criminal economy
#include "modules/addiction/addiction_module.h"
#include "modules/alternative_identity/alternative_identity_module.h"
#include "modules/criminal_operations/criminal_operations_module.h"
#include "modules/designer_drug/designer_drug_module.h"
#include "modules/drug_economy/drug_economy_module.h"
#include "modules/informant_system/informant_system_module.h"
#include "modules/money_laundering/money_laundering_module.h"
#include "modules/protection_rackets/protection_rackets_module.h"
#include "modules/weapons_trafficking/weapons_trafficking_module.h"

// Infrastructure: LOD and persistence
#include "modules/lod_system/lod_system_module.h"
#include "modules/persistence/persistence_module.h"

namespace econlife {

void register_base_game_modules(TickOrchestrator& orchestrator, const PackageConfig& config) {
    // Player input
    orchestrator.register_module(std::make_unique<PlayerActionsModule>());

    // Production, markets, and trade
    orchestrator.register_module(std::make_unique<CalendarModule>());
    orchestrator.register_module(std::make_unique<TechnologyModule>(config.rnd));
    orchestrator.register_module(
        std::make_unique<BusinessLifecycleModule>(config.business_lifecycle));
    orchestrator.register_module(std::make_unique<ProductionModule>(config.production));
    orchestrator.register_module(std::make_unique<SceneCardsModule>(config.scene_cards));
    orchestrator.register_module(std::make_unique<RandomEventsModule>(config.random_events));
    orchestrator.register_module(std::make_unique<SupplyChainModule>(config.supply_chain_module));
    orchestrator.register_module(std::make_unique<LaborMarketModule>(config.labor_module));
    orchestrator.register_module(
        std::make_unique<SeasonalAgricultureModule>(config.seasonal_agriculture));
    orchestrator.register_module(
        std::make_unique<PriceEngineModule>(config.price_model, config.price_engine));
    orchestrator.register_module(
        std::make_unique<TradeInfrastructureModule>(config.trade_infrastructure));
    orchestrator.register_module(
        std::make_unique<FinancialDistributionModule>(config.financial_distribution));
    orchestrator.register_module(std::make_unique<NpcBusinessModule>(config.npc_business));
    orchestrator.register_module(
        std::make_unique<CommodityTradingModule>(config.commodity_trading));
    orchestrator.register_module(std::make_unique<RealEstateModule>(config.real_estate));

    // Finance, government, and population
    orchestrator.register_module(
        std::make_unique<NpcBehaviorModule>(config.npc_behavior_module, config.npc_behavior));
    orchestrator.register_module(std::make_unique<BankingModule>(config.banking));
    orchestrator.register_module(
        std::make_unique<GovernmentBudgetModule>(config.government_budget));
    orchestrator.register_module(std::make_unique<HealthcareModule>(config.healthcare));

    // NPC behaviour, spending, relationships
    orchestrator.register_module(std::make_unique<NpcSpendingModule>(config.npc_spending));
    orchestrator.register_module(std::make_unique<EvidenceModule>(config.evidence));
    orchestrator.register_module(
        std::make_unique<ObligationNetworkModule>(config.obligation_network));
    orchestrator.register_module(
        std::make_unique<CommunityResponseModule>(config.community_response));

    // Evidence, investigation, antitrust, and media
    orchestrator.register_module(std::make_unique<FacilitySignalsModule>(config.facility_signals));
    orchestrator.register_module(
        std::make_unique<CriminalOperationsModule>(config.criminal_operations));
    orchestrator.register_module(std::make_unique<MediaSystemModule>(config.media_system));
    orchestrator.register_module(std::make_unique<AntitrustModule>(config.antitrust));

    // Criminal economy
    orchestrator.register_module(
        std::make_unique<InvestigatorEngineModule>(config.investigator_engine));
    orchestrator.register_module(std::make_unique<MoneyLaunderingModule>(config.money_laundering));
    orchestrator.register_module(std::make_unique<DrugEconomyModule>(config.drug_economy));
    orchestrator.register_module(
        std::make_unique<WeaponsTraffickingModule>(config.weapons_trafficking));
    orchestrator.register_module(
        std::make_unique<ProtectionRacketsModule>(config.protection_rackets));
    orchestrator.register_module(std::make_unique<LegalProcessModule>(config.legal_process));
    orchestrator.register_module(std::make_unique<InformantSystemModule>(config.informant));
    orchestrator.register_module(
        std::make_unique<AlternativeIdentityModule>(config.alternative_identity));
    orchestrator.register_module(std::make_unique<DesignerDrugModule>(config.designer_drug));

    // Community, political, and regional dynamics
    orchestrator.register_module(std::make_unique<PoliticalCycleModule>(config.political_cycle));
    orchestrator.register_module(
        std::make_unique<InfluenceNetworkModule>(config.influence_network));
    orchestrator.register_module(std::make_unique<TrustUpdatesModule>(config.trust_updates));
    orchestrator.register_module(std::make_unique<AddictionModule>(config.addiction));
    orchestrator.register_module(
        std::make_unique<RegionalConditionsModule>(config.regional_conditions));
    orchestrator.register_module(std::make_unique<PopulationAgingModule>(config.population_aging));
    orchestrator.register_module(
        std::make_unique<CurrencyExchangeModule>(config.currency_exchange));

    // Infrastructure: LOD and persistence
    orchestrator.register_module(std::make_unique<LodSystemModule>(config.lod_system));
    orchestrator.register_module(std::make_unique<PersistenceModule>());
}

}  // namespace econlife
