// v7 module-private state persistence — exercises the
// ITickModule::serialize_state / deserialize_state hook through
// PersistenceModule's optional-modules overload.
//
// Covers the three V1 opt-ins: random_events, protection_rackets, real_estate.

#include <catch2/catch_test_macros.hpp>

#include "core/world_state/world_state.h"
#include "modules/persistence/persistence_module.h"
#include "modules/protection_rackets/protection_rackets_module.h"
#include "modules/random_events/random_events_module.h"
#include "modules/real_estate/real_estate_module.h"

using namespace econlife;

namespace {

WorldState minimal_world() {
    WorldState w{};
    w.current_tick = 1;
    w.world_seed = 1;
    w.game_mode = GameMode::standard;
    return w;
}

}  // namespace

TEST_CASE("v7 module-state: random_events active_events round-trip",
          "[persistence][v7][module_state][serialization]") {
    auto world = minimal_world();
    RandomEventsModule mod;

    ActiveRandomEvent e1{};
    e1.id = 7;
    e1.template_id = "drought";
    e1.template_key = "drought";
    e1.province_id = 0;
    e1.category = EventCategory::natural;
    e1.severity = 0.42f;
    e1.started_tick = 10;
    e1.end_tick = 100;
    e1.evidence_generated = false;
    e1.effects_applied_this_tick = true;
    mod.add_active_event(e1);

    ActiveRandomEvent e2{};
    e2.id = 8;
    e2.template_id = "factory_fire";
    e2.template_key = "factory_fire";
    e2.province_id = 2;
    e2.category = EventCategory::accident;
    e2.severity = 0.9f;
    e2.started_tick = 12;
    e2.end_tick = 15;
    e2.evidence_generated = true;
    e2.effects_applied_this_tick = false;
    mod.add_active_event(e2);

    auto bytes = PersistenceModule::serialize(world, {&mod});

    RandomEventsModule restored_mod;
    WorldState restored;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    const auto& restored_events = restored_mod.active_events();
    REQUIRE(restored_events.size() == 2);
    REQUIRE(restored_events[0].id == 7);
    REQUIRE(restored_events[0].template_id == "drought");
    REQUIRE(restored_events[0].severity == 0.42f);
    REQUIRE(restored_events[0].end_tick == 100);
    REQUIRE(restored_events[1].id == 8);
    REQUIRE(restored_events[1].template_id == "factory_fire");
    REQUIRE(restored_events[1].category == EventCategory::accident);
    REQUIRE(restored_events[1].evidence_generated == true);
}

TEST_CASE("v7 module-state: protection_rackets round-trip",
          "[persistence][v7][module_state][serialization]") {
    auto world = minimal_world();
    ProtectionRacketsModule mod;

    ProtectionRacket r1{};
    r1.id = 100;
    r1.criminal_org_id = 7;
    r1.target_business_id = 42;
    r1.demand_per_tick = 12.5f;
    r1.status = RacketStatus::active;
    r1.escalation_stage = RacketEscalationStage::demand_issued;
    r1.last_payment_tick = 30;
    r1.demand_issued_tick = 0;
    r1.community_grievance_contribution = 0.025f;
    mod.rackets_mut().push_back(r1);

    ProtectionRacket r2{};
    r2.id = 101;
    r2.criminal_org_id = 7;
    r2.target_business_id = 43;
    r2.demand_per_tick = 8.0f;
    r2.status = RacketStatus::refused;
    r2.escalation_stage = RacketEscalationStage::violence;
    r2.last_payment_tick = 0;
    r2.demand_issued_tick = 5;
    r2.community_grievance_contribution = 0.016f;
    mod.rackets_mut().push_back(r2);

    auto bytes = PersistenceModule::serialize(world, {&mod});

    ProtectionRacketsModule restored_mod;
    WorldState restored;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    REQUIRE(restored_mod.rackets().size() == 2);
    REQUIRE(restored_mod.rackets()[0].id == 100);
    REQUIRE(restored_mod.rackets()[0].demand_per_tick == 12.5f);
    REQUIRE(restored_mod.rackets()[0].status == RacketStatus::active);
    REQUIRE(restored_mod.rackets()[1].id == 101);
    REQUIRE(restored_mod.rackets()[1].escalation_stage == RacketEscalationStage::violence);
    REQUIRE(restored_mod.rackets()[1].community_grievance_contribution == 0.016f);
}

TEST_CASE("v7 module-state: real_estate properties round-trip",
          "[persistence][v7][module_state][serialization]") {
    auto world = minimal_world();
    RealEstateModule mod;

    PropertyListing p1{};
    p1.id = 1;
    p1.type = PropertyType::residential;
    p1.province_id = 0;
    p1.owner_id = 99;
    p1.asking_price = 250000.0f;
    p1.market_value = 230000.0f;
    p1.rental_yield_rate = 0.003f;
    p1.rental_income_per_tick = 690.0f;
    p1.rented = true;
    p1.tenant_id = 1500;
    p1.launder_eligible = false;
    p1.purchased_tick = 100;
    p1.purchase_price = 200000.0f;
    mod.add_property(p1);

    PropertyListing p2{};
    p2.id = 2;
    p2.type = PropertyType::commercial;
    p2.province_id = 1;
    p2.owner_id = 100;
    p2.asking_price = 800000.0f;
    p2.market_value = 750000.0f;
    p2.rental_yield_rate = 0.004f;
    p2.rental_income_per_tick = 3000.0f;
    p2.rented = false;
    p2.tenant_id = 0;
    p2.launder_eligible = true;
    p2.purchased_tick = 200;
    p2.purchase_price = 600000.0f;
    mod.add_property(p2);

    auto bytes = PersistenceModule::serialize(world, {&mod});

    RealEstateModule restored_mod;
    WorldState restored;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    const auto& restored_props = restored_mod.properties();
    REQUIRE(restored_props.size() == 2);
    REQUIRE(restored_props[0].id == 1);
    REQUIRE(restored_props[0].type == PropertyType::residential);
    REQUIRE(restored_props[0].rented == true);
    REQUIRE(restored_props[0].tenant_id == 1500);
    REQUIRE(restored_props[0].market_value == 230000.0f);
    REQUIRE(restored_props[1].id == 2);
    REQUIRE(restored_props[1].launder_eligible == true);
    REQUIRE(restored_props[1].rental_income_per_tick == 3000.0f);
}

TEST_CASE("v7 module-state: all three modules in one save",
          "[persistence][v7][module_state][serialization]") {
    auto world = minimal_world();
    RandomEventsModule re;
    ProtectionRacketsModule pr;
    RealEstateModule rs;

    ActiveRandomEvent e{};
    e.id = 1;
    e.template_id = "drought";
    e.province_id = 0;
    e.severity = 0.5f;
    e.started_tick = 5;
    e.end_tick = 50;
    re.add_active_event(e);

    ProtectionRacket r{};
    r.id = 1;
    r.criminal_org_id = 1;
    r.target_business_id = 1;
    r.demand_per_tick = 10.0f;
    r.status = RacketStatus::active;
    pr.rackets_mut().push_back(r);

    PropertyListing p{};
    p.id = 1;
    p.type = PropertyType::industrial;
    p.province_id = 0;
    p.market_value = 100000.0f;
    rs.add_property(p);

    auto bytes = PersistenceModule::serialize(world, {&re, &pr, &rs});

    RandomEventsModule re_r;
    ProtectionRacketsModule pr_r;
    RealEstateModule rs_r;
    WorldState restored;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&re_r, &pr_r, &rs_r}) ==
            RestoreResult::success);

    REQUIRE(re_r.active_events().size() == 1);
    REQUIRE(pr_r.rackets().size() == 1);
    REQUIRE(rs_r.properties().size() == 1);
    REQUIRE(rs_r.properties()[0].type == PropertyType::industrial);
}

TEST_CASE("v7 module-state: modules in save but absent from load are skipped",
          "[persistence][v7][module_state][serialization]") {
    auto world = minimal_world();
    RandomEventsModule re;
    ProtectionRacketsModule pr;

    ActiveRandomEvent e{};
    e.id = 1;
    e.template_id = "drought";
    e.province_id = 0;
    re.add_active_event(e);

    ProtectionRacket r{};
    r.id = 1;
    r.status = RacketStatus::active;
    pr.rackets_mut().push_back(r);

    auto bytes = PersistenceModule::serialize(world, {&re, &pr});

    // Only RandomEventsModule provided on load — the protection_rackets
    // block in the save is skipped without error (forward compatibility).
    RandomEventsModule re_r;
    WorldState restored;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&re_r}) == RestoreResult::success);
    REQUIRE(re_r.active_events().size() == 1);
}

TEST_CASE("v7 module-state: empty modules list serializes count=0",
          "[persistence][v7][module_state][serialization]") {
    auto world = minimal_world();
    // No modules passed — section header is written with count 0, then read.
    auto bytes_no_modules = PersistenceModule::serialize(world);
    auto bytes_empty_vec = PersistenceModule::serialize(world, {});
    REQUIRE(bytes_no_modules == bytes_empty_vec);

    WorldState restored;
    REQUIRE(PersistenceModule::deserialize(bytes_no_modules, restored) == RestoreResult::success);
}
