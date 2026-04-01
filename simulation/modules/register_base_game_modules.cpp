#include "register_base_game_modules.h"

#include <memory>

#include "core/tick/tick_orchestrator.h"

// Tier 1: No inter-module dependencies
#include "modules/calendar/calendar_module.h"
#include "modules/production/production_module.h"
#include "modules/random_events/random_events_module.h"
#include "modules/scene_cards/scene_cards_module.h"
#include "modules/technology/technology_module.h"

// Tier 2: Depends on production
#include "modules/labor_market/labor_market_module.h"
#include "modules/seasonal_agriculture/seasonal_agriculture_module.h"
#include "modules/supply_chain/supply_chain_module.h"

// Tier 3: Depends on supply chain
#include "modules/price_engine/price_engine_module.h"
#include "modules/trade_infrastructure/trade_infrastructure_module.h"

// Tier 4: Depends on price engine
#include "modules/commodity_trading/commodity_trading_module.h"
#include "modules/financial_distribution/financial_distribution_module.h"
#include "modules/npc_business/npc_business_module.h"
#include "modules/real_estate/real_estate_module.h"

// Tier 5: Depends on financial distribution
#include "modules/banking/banking_module.h"
#include "modules/government_budget/government_budget_module.h"
#include "modules/healthcare/healthcare_module.h"
#include "modules/npc_behavior/npc_behavior_module.h"

// Tier 6: Depends on NPC behavior
#include "modules/community_response/community_response_module.h"
#include "modules/evidence/evidence_module.h"
#include "modules/npc_spending/npc_spending_module.h"
#include "modules/obligation_network/obligation_network_module.h"

// Tier 7: Depends on evidence
#include "modules/antitrust/antitrust_module.h"
#include "modules/criminal_operations/criminal_operations_module.h"
#include "modules/facility_signals/facility_signals_module.h"
#include "modules/media_system/media_system_module.h"

// Tier 8: Depends on criminal operations
#include "modules/drug_economy/drug_economy_module.h"
#include "modules/investigator_engine/investigator_engine_module.h"
#include "modules/money_laundering/money_laundering_module.h"
#include "modules/protection_rackets/protection_rackets_module.h"
#include "modules/weapons_trafficking/weapons_trafficking_module.h"

// Tier 9: Depends on investigator engine
#include "modules/alternative_identity/alternative_identity_module.h"
#include "modules/designer_drug/designer_drug_module.h"
#include "modules/informant_system/informant_system_module.h"
#include "modules/legal_process/legal_process_module.h"

// Tier 10: Depends on community response
#include "modules/addiction/addiction_module.h"
#include "modules/influence_network/influence_network_module.h"
#include "modules/political_cycle/political_cycle_module.h"
#include "modules/trust_updates/trust_updates_module.h"

// Tier 11: Mixed dependencies
#include "modules/currency_exchange/currency_exchange_module.h"
#include "modules/lod_system/lod_system_module.h"
#include "modules/population_aging/population_aging_module.h"
#include "modules/regional_conditions/regional_conditions_module.h"

// Tier 12: Persistence
#include "modules/persistence/persistence_module.h"

namespace econlife {

void register_base_game_modules(TickOrchestrator& orchestrator, const PackageConfig& config) {
    // Tier 1
    orchestrator.register_module(std::make_unique<CalendarModule>());
    orchestrator.register_module(std::make_unique<TechnologyModule>());
    orchestrator.register_module(std::make_unique<ProductionModule>(config.production));
    orchestrator.register_module(std::make_unique<SceneCardsModule>(config.scene_cards));
    orchestrator.register_module(std::make_unique<RandomEventsModule>(config.random_events));

    // Tier 2
    orchestrator.register_module(std::make_unique<SupplyChainModule>());
    orchestrator.register_module(std::make_unique<LaborMarketModule>());
    orchestrator.register_module(std::make_unique<SeasonalAgricultureModule>());

    // Tier 3
    orchestrator.register_module(std::make_unique<PriceEngineModule>(config.price_model));
    orchestrator.register_module(std::make_unique<TradeInfrastructureModule>(config.trade_infrastructure));

    // Tier 4
    orchestrator.register_module(std::make_unique<FinancialDistributionModule>());
    orchestrator.register_module(std::make_unique<NpcBusinessModule>(config.npc_business));
    orchestrator.register_module(std::make_unique<CommodityTradingModule>());
    orchestrator.register_module(std::make_unique<RealEstateModule>());

    // Tier 5
    orchestrator.register_module(std::make_unique<NpcBehaviorModule>(config.npc_behavior));
    orchestrator.register_module(std::make_unique<BankingModule>(config.banking));
    orchestrator.register_module(std::make_unique<GovernmentBudgetModule>(config.government_budget));
    orchestrator.register_module(std::make_unique<HealthcareModule>(config.healthcare));

    // Tier 6
    orchestrator.register_module(std::make_unique<NpcSpendingModule>());
    orchestrator.register_module(std::make_unique<EvidenceModule>(config.evidence));
    orchestrator.register_module(std::make_unique<ObligationNetworkModule>());
    orchestrator.register_module(
        std::make_unique<CommunityResponseModule>(config.consequence_delays));

    // Tier 7
    orchestrator.register_module(std::make_unique<FacilitySignalsModule>());
    orchestrator.register_module(std::make_unique<CriminalOperationsModule>());
    orchestrator.register_module(std::make_unique<MediaSystemModule>(config.media_system));
    orchestrator.register_module(std::make_unique<AntitrustModule>());

    // Tier 8
    orchestrator.register_module(std::make_unique<InvestigatorEngineModule>(config.investigator_engine));
    orchestrator.register_module(std::make_unique<MoneyLaunderingModule>());
    orchestrator.register_module(std::make_unique<DrugEconomyModule>());
    orchestrator.register_module(std::make_unique<WeaponsTraffickingModule>());
    orchestrator.register_module(std::make_unique<ProtectionRacketsModule>());

    // Tier 9
    orchestrator.register_module(std::make_unique<LegalProcessModule>(config.legal_process));
    orchestrator.register_module(std::make_unique<InformantSystemModule>(config.informant));
    orchestrator.register_module(std::make_unique<AlternativeIdentityModule>());
    orchestrator.register_module(std::make_unique<DesignerDrugModule>());

    // Tier 10
    orchestrator.register_module(std::make_unique<PoliticalCycleModule>(config.political_cycle));
    orchestrator.register_module(std::make_unique<InfluenceNetworkModule>(config.influence_network));
    orchestrator.register_module(std::make_unique<TrustUpdatesModule>());
    orchestrator.register_module(std::make_unique<AddictionModule>());

    // Tier 11
    orchestrator.register_module(std::make_unique<RegionalConditionsModule>());
    orchestrator.register_module(std::make_unique<PopulationAgingModule>());
    orchestrator.register_module(std::make_unique<CurrencyExchangeModule>(config.currency_exchange));
    orchestrator.register_module(std::make_unique<LodSystemModule>());

    // Tier 12
    orchestrator.register_module(std::make_unique<PersistenceModule>());
}

}  // namespace econlife
