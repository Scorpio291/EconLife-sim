// Module existence tests — verify all core modules instantiate and report
// the correct name() and package_id(). Each test catches accidental renames,
// missing factories, or constructor signature changes.

#include <catch2/catch_test_macros.hpp>

#include "modules/addiction/addiction_module.h"
#include "modules/alternative_identity/alternative_identity_module.h"
#include "modules/antitrust/antitrust_module.h"
#include "modules/banking/banking_module.h"
#include "modules/business_lifecycle/business_lifecycle_module.h"
#include "modules/calendar/calendar_module.h"
#include "modules/commodity_trading/commodity_trading_module.h"
#include "modules/community_response/community_response_module.h"
#include "modules/criminal_operations/criminal_operations_module.h"
#include "modules/currency_exchange/currency_exchange_module.h"
#include "modules/designer_drug/designer_drug_module.h"
#include "modules/drug_economy/drug_economy_module.h"
#include "modules/evidence/evidence_module.h"
#include "modules/facility_signals/facility_signals_module.h"
#include "modules/financial_distribution/financial_distribution_module.h"
#include "modules/government_budget/government_budget_module.h"
#include "modules/healthcare/healthcare_module.h"
#include "modules/influence_network/influence_network_module.h"
#include "modules/informant_system/informant_system_module.h"
#include "modules/investigator_engine/investigator_engine_module.h"
#include "modules/labor_market/labor_market_module.h"
#include "modules/legal_process/legal_process_module.h"
#include "modules/lod_system/lod_system_module.h"
#include "modules/media_system/media_system_module.h"
#include "modules/money_laundering/money_laundering_module.h"
#include "modules/npc_behavior/npc_behavior_module.h"
#include "modules/npc_business/npc_business_module.h"
#include "modules/npc_spending/npc_spending_module.h"
#include "modules/obligation_network/obligation_network_module.h"
#include "modules/persistence/persistence_module.h"
#include "modules/political_cycle/political_cycle_module.h"
#include "modules/population_aging/population_aging_module.h"
#include "modules/price_engine/price_engine_module.h"
#include "modules/production/production_module.h"
#include "modules/protection_rackets/protection_rackets_module.h"
#include "modules/random_events/random_events_module.h"
#include "modules/real_estate/real_estate_module.h"
#include "modules/regional_conditions/regional_conditions_module.h"
#include "modules/scene_cards/scene_cards_module.h"
#include "modules/seasonal_agriculture/seasonal_agriculture_module.h"
#include "modules/supply_chain/supply_chain_module.h"
#include "modules/technology/technology_module.h"
#include "modules/trade_infrastructure/trade_infrastructure_module.h"
#include "modules/trust_updates/trust_updates_module.h"
#include "modules/weapons_trafficking/weapons_trafficking_module.h"

using namespace econlife;

// ── Tier 1: No inter-module dependencies ────────────────────────────────────

TEST_CASE("production module exists and registers", "[module][tier1]") {
    ProductionModule mod;
    REQUIRE(mod.name() == "production");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("calendar module exists and registers", "[module][tier1]") {
    CalendarModule mod;
    REQUIRE(mod.name() == "calendar");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("scene_cards module exists and registers", "[module][tier1]") {
    SceneCardsModule mod;
    REQUIRE(mod.name() == "scene_cards");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("random_events module exists and registers", "[module][tier1]") {
    RandomEventsModule mod;
    REQUIRE(mod.name() == "random_events");
    REQUIRE(mod.package_id() == "base_game");
}

// ── Tier 2: Depends on Production ───────────────────────────────────────────

TEST_CASE("supply_chain module exists and registers", "[module][tier2]") {
    SupplyChainModule mod;
    REQUIRE(mod.name() == "supply_chain");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("labor_market module exists and registers", "[module][tier2]") {
    LaborMarketModule mod;
    REQUIRE(mod.name() == "labor_market");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("seasonal_agriculture module exists and registers", "[module][tier2]") {
    SeasonalAgricultureModule mod;
    REQUIRE(mod.name() == "seasonal_agriculture");
    REQUIRE(mod.package_id() == "base_game");
}

// ── Tier 3: Depends on Supply Chain ─────────────────────────────────────────

TEST_CASE("price_engine module exists and registers", "[module][tier3]") {
    PriceEngineModule mod;
    REQUIRE(mod.name() == "price_engine");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("trade_infrastructure module exists and registers", "[module][tier3]") {
    TradeInfrastructureModule mod;
    REQUIRE(mod.name() == "trade_infrastructure");
    REQUIRE(mod.package_id() == "base_game");
}

// ── Tier 4: Depends on Price Engine ─────────────────────────────────────────

TEST_CASE("financial_distribution module exists and registers", "[module][tier4]") {
    FinancialDistributionModule mod;
    REQUIRE(mod.name() == "financial_distribution");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("npc_business module exists and registers", "[module][tier4]") {
    NpcBusinessModule mod;
    REQUIRE(mod.name() == "npc_business");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("commodity_trading module exists and registers", "[module][tier4]") {
    CommodityTradingModule mod;
    REQUIRE(mod.name() == "commodity_trading");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("real_estate module exists and registers", "[module][tier4]") {
    RealEstateModule mod;
    REQUIRE(mod.name() == "real_estate");
    REQUIRE(mod.package_id() == "base_game");
}

// ── Tier 5: Depends on Financial Distribution ───────────────────────────────

TEST_CASE("npc_behavior module exists and registers", "[module][tier5]") {
    NpcBehaviorModule mod;
    REQUIRE(mod.name() == "npc_behavior");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("banking module exists and registers", "[module][tier5]") {
    BankingModule mod;
    REQUIRE(mod.name() == "banking");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("government_budget module exists and registers", "[module][tier5]") {
    GovernmentBudgetModule mod;
    REQUIRE(mod.name() == "government_budget");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("healthcare module exists and registers", "[module][tier5]") {
    HealthcareModule mod;
    REQUIRE(mod.name() == "healthcare");
    REQUIRE(mod.package_id() == "base_game");
}

// ── Tier 6: Depends on NPC Behavior ────────────────────────────────────────

TEST_CASE("npc_spending module exists and registers", "[module][tier6]") {
    NpcSpendingModule mod;
    REQUIRE(mod.name() == "npc_spending");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("evidence module exists and registers", "[module][tier6]") {
    EvidenceModule mod;
    REQUIRE(mod.name() == "evidence");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("obligation_network module exists and registers", "[module][tier6]") {
    ObligationNetworkModule mod;
    REQUIRE(mod.name() == "obligation_network");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("community_response module exists and registers", "[module][tier6]") {
    CommunityResponseModule mod;
    REQUIRE(mod.name() == "community_response");
    REQUIRE(mod.package_id() == "base_game");
}

// ── Tier 7: Depends on Evidence ────────────────────────────────────────────

TEST_CASE("facility_signals module exists and registers", "[module][tier7]") {
    FacilitySignalsModule mod;
    REQUIRE(mod.name() == "facility_signals");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("criminal_operations module exists and registers", "[module][tier7]") {
    CriminalOperationsModule mod;
    REQUIRE(mod.name() == "criminal_operations");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("media_system module exists and registers", "[module][tier7]") {
    MediaSystemModule mod;
    REQUIRE(mod.name() == "media_system");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("antitrust module exists and registers", "[module][tier7]") {
    AntitrustModule mod;
    REQUIRE(mod.name() == "antitrust");
    REQUIRE(mod.package_id() == "base_game");
}

// ── Tier 8: Depends on Criminal Operations ─────────────────────────────────

TEST_CASE("investigator_engine module exists and registers", "[module][tier8]") {
    InvestigatorEngineModule mod;
    REQUIRE(mod.name() == "investigator_engine");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("money_laundering module exists and registers", "[module][tier8]") {
    MoneyLaunderingModule mod;
    REQUIRE(mod.name() == "money_laundering");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("drug_economy module exists and registers", "[module][tier8]") {
    DrugEconomyModule mod;
    REQUIRE(mod.name() == "drug_economy");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("weapons_trafficking module exists and registers", "[module][tier8]") {
    WeaponsTraffickingModule mod;
    REQUIRE(mod.name() == "weapons_trafficking");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("protection_rackets module exists and registers", "[module][tier8]") {
    ProtectionRacketsModule mod;
    REQUIRE(mod.name() == "protection_rackets");
    REQUIRE(mod.package_id() == "base_game");
}

// ── Tier 9: Depends on Investigator Engine ─────────────────────────────────

TEST_CASE("legal_process module exists and registers", "[module][tier9]") {
    LegalProcessModule mod;
    REQUIRE(mod.name() == "legal_process");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("informant_system module exists and registers", "[module][tier9]") {
    InformantSystemModule mod;
    REQUIRE(mod.name() == "informant_system");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("alternative_identity module exists and registers", "[module][tier9]") {
    AlternativeIdentityModule mod;
    REQUIRE(mod.name() == "alternative_identity");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("designer_drug module exists and registers", "[module][tier9]") {
    DesignerDrugModule mod;
    REQUIRE(mod.name() == "designer_drug");
    REQUIRE(mod.package_id() == "base_game");
}

// ── Tier 10: Depends on Community Response ─────────────────────────────────

TEST_CASE("political_cycle module exists and registers", "[module][tier10]") {
    PoliticalCycleModule mod;
    REQUIRE(mod.name() == "political_cycle");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("influence_network module exists and registers", "[module][tier10]") {
    InfluenceNetworkModule mod;
    REQUIRE(mod.name() == "influence_network");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("trust_updates module exists and registers", "[module][tier10]") {
    TrustUpdatesModule mod;
    REQUIRE(mod.name() == "trust_updates");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("addiction module exists and registers", "[module][tier10]") {
    AddictionModule mod;
    REQUIRE(mod.name() == "addiction");
    REQUIRE(mod.package_id() == "base_game");
}

// ── Tier 11: Mixed Dependencies ────────────────────────────────────────────

TEST_CASE("regional_conditions module exists and registers", "[module][tier11]") {
    RegionalConditionsModule mod;
    REQUIRE(mod.name() == "regional_conditions");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("population_aging module exists and registers", "[module][tier11]") {
    PopulationAgingModule mod;
    REQUIRE(mod.name() == "population_aging");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("currency_exchange module exists and registers", "[module][tier11]") {
    CurrencyExchangeModule mod;
    REQUIRE(mod.name() == "currency_exchange");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("lod_system module exists and registers", "[module][tier11]") {
    LodSystemModule mod;
    REQUIRE(mod.name() == "lod_system");
    REQUIRE(mod.package_id() == "base_game");
}

// ── Tier 12: Persistence ───────────────────────────────────────────────────

TEST_CASE("persistence module exists and registers", "[module][tier12]") {
    PersistenceModule mod;
    REQUIRE(mod.name() == "persistence");
    REQUIRE(mod.package_id() == "base_game");
}

// ── Additional modules added during V1 implementation ──────────────────────

TEST_CASE("business_lifecycle module exists and registers", "[module][tier11]") {
    BusinessLifecycleModule mod{BusinessLifecycleConfig{}};
    REQUIRE(mod.name() == "business_lifecycle");
    REQUIRE(mod.package_id() == "base_game");
}

TEST_CASE("technology module exists and registers", "[module][tier11]") {
    TechnologyModule mod;
    REQUIRE(mod.name() == "technology");
    REQUIRE(mod.package_id() == "base_game");
}
