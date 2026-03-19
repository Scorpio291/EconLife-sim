#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "modules/antitrust/antitrust_module.h"
#include "core/world_state/world_state.h"
#include "core/world_state/player.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

// =============================================================================
// Static utility tests
// =============================================================================

TEST_CASE("Supply share computation", "[antitrust][tier7]") {
    float share = AntitrustModule::compute_supply_share(450.0f, 1000.0f);
    CHECK_THAT(share, WithinAbs(0.45f, 0.01f));
}

TEST_CASE("Supply share zero total returns zero", "[antitrust][tier7]") {
    float share = AntitrustModule::compute_supply_share(100.0f, 0.0f);
    CHECK_THAT(share, WithinAbs(0.0f, 0.001f));
}

TEST_CASE("Supply share clamped to 1.0", "[antitrust][tier7]") {
    float share = AntitrustModule::compute_supply_share(1500.0f, 1000.0f);
    CHECK_THAT(share, WithinAbs(1.0f, 0.001f));
}

TEST_CASE("Tier 1 below threshold no trigger", "[antitrust][tier7]") {
    CHECK(AntitrustModule::is_tier1_triggered(0.30f) == false);
    CHECK(AntitrustModule::is_tier1_triggered(0.39f) == false);
}

TEST_CASE("Tier 1 at threshold triggers", "[antitrust][tier7]") {
    CHECK(AntitrustModule::is_tier1_triggered(0.40f) == true);
    CHECK(AntitrustModule::is_tier1_triggered(0.50f) == true);
}

TEST_CASE("Tier 2 below threshold no trigger", "[antitrust][tier7]") {
    CHECK(AntitrustModule::is_tier2_triggered(0.60f) == false);
    CHECK(AntitrustModule::is_tier2_triggered(0.69f) == false);
}

TEST_CASE("Tier 2 at threshold triggers", "[antitrust][tier7]") {
    CHECK(AntitrustModule::is_tier2_triggered(0.70f) == true);
    CHECK(AntitrustModule::is_tier2_triggered(0.90f) == true);
}

TEST_CASE("Meter fill increment value", "[antitrust][tier7]") {
    CHECK_THAT(AntitrustModule::compute_meter_fill_increment(),
               WithinAbs(0.002f, 0.0001f));
}

TEST_CASE("Pressure increment value", "[antitrust][tier7]") {
    CHECK_THAT(AntitrustModule::compute_pressure_increment(),
               WithinAbs(0.005f, 0.0001f));
}

TEST_CASE("Pressure decay value", "[antitrust][tier7]") {
    CHECK_THAT(AntitrustModule::compute_pressure_decay(),
               WithinAbs(0.01f, 0.001f));
}

TEST_CASE("Proposal generation threshold", "[antitrust][tier7]") {
    CHECK(AntitrustModule::should_generate_proposal(0.49f) == false);
    CHECK(AntitrustModule::should_generate_proposal(0.50f) == true);
    CHECK(AntitrustModule::should_generate_proposal(0.80f) == true);
}

// =============================================================================
// Integration tests
// =============================================================================

TEST_CASE("Monthly check fires and reschedules", "[antitrust][tier7]") {
    WorldState state{};
    state.current_tick = 30;

    Province prov{};
    prov.id = 0;
    state.provinces.push_back(prov);

    PlayerCharacter player{};
    player.id = 999;
    state.player = &player;

    AntitrustModule module;
    module.next_check_tick() = 30;

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Should have rescheduled to tick 60
    CHECK(module.next_check_tick() == 60);
}

TEST_CASE("Criminal sector excluded from antitrust", "[antitrust][tier7]") {
    WorldState state{};
    state.current_tick = 30;

    Province prov{};
    prov.id = 0;
    state.provinces.push_back(prov);

    // Criminal business with high revenue
    NPCBusiness criminal_biz{};
    criminal_biz.id = 1;
    criminal_biz.province_id = 0;
    criminal_biz.criminal_sector = true;
    criminal_biz.revenue_per_tick = 10000.0f;
    criminal_biz.owner_id = 100;
    state.npc_businesses.push_back(criminal_biz);

    // Small legitimate business
    NPCBusiness legit_biz{};
    legit_biz.id = 2;
    legit_biz.province_id = 0;
    legit_biz.criminal_sector = false;
    legit_biz.revenue_per_tick = 100.0f;
    legit_biz.owner_id = 200;
    state.npc_businesses.push_back(legit_biz);

    RegionalMarket rm{};
    rm.good_id = 1;
    rm.province_id = 0;
    rm.supply = 10000.0f;
    state.regional_markets.push_back(rm);

    PlayerCharacter player{};
    player.id = 999;
    state.player = &player;

    AntitrustModule module;
    module.next_check_tick() = 30;

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Criminal business should not generate antitrust actions
    // Only the legitimate business is checked; it has 100% of formal market
    // which would be tier1 + tier2 triggered for actor_id=200
    // The criminal business output is not in the actor_output map
}

TEST_CASE("Tier 1 triggers regulator meter fill", "[antitrust][tier7]") {
    WorldState state{};
    state.current_tick = 30;

    Province prov{};
    prov.id = 0;
    state.provinces.push_back(prov);

    // Dominant legitimate business
    NPCBusiness biz1{};
    biz1.id = 1;
    biz1.province_id = 0;
    biz1.criminal_sector = false;
    biz1.revenue_per_tick = 600.0f;
    biz1.owner_id = 100;
    state.npc_businesses.push_back(biz1);

    // Small competitor
    NPCBusiness biz2{};
    biz2.id = 2;
    biz2.province_id = 0;
    biz2.criminal_sector = false;
    biz2.revenue_per_tick = 400.0f;
    biz2.owner_id = 200;
    state.npc_businesses.push_back(biz2);

    RegionalMarket rm{};
    rm.good_id = 1;
    rm.province_id = 0;
    rm.supply = 1000.0f;
    state.regional_markets.push_back(rm);

    // Regulator NPC
    NPC reg_npc{};
    reg_npc.id = 50;
    reg_npc.role = NPCRole::regulator;
    reg_npc.current_province_id = 0;
    reg_npc.status = NPCStatus::active;
    state.significant_npcs.push_back(reg_npc);

    PlayerCharacter player{};
    player.id = 999;
    state.player = &player;

    AntitrustModule module;
    module.next_check_tick() = 30;

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Actor 100 has 60% share (> 0.40 threshold), should trigger Tier 1
    // Regulator meter should be filled
    bool found_reg_fill = false;
    for (const auto& d : delta.npc_deltas) {
        if (d.npc_id == 50 && d.motivation_delta.has_value()) {
            found_reg_fill = true;
            CHECK_THAT(*d.motivation_delta,
                       WithinAbs(0.002f, 0.001f));
            break;
        }
    }
    CHECK(found_reg_fill);
}

TEST_CASE("Tier 2 accumulates proposal pressure", "[antitrust][tier7]") {
    WorldState state{};
    state.current_tick = 30;

    Province prov{};
    prov.id = 0;
    state.provinces.push_back(prov);

    // Highly dominant business (75% share)
    NPCBusiness biz1{};
    biz1.id = 1;
    biz1.province_id = 0;
    biz1.criminal_sector = false;
    biz1.revenue_per_tick = 750.0f;
    biz1.owner_id = 100;
    state.npc_businesses.push_back(biz1);

    // Small competitor
    NPCBusiness biz2{};
    biz2.id = 2;
    biz2.province_id = 0;
    biz2.criminal_sector = false;
    biz2.revenue_per_tick = 250.0f;
    biz2.owner_id = 200;
    state.npc_businesses.push_back(biz2);

    RegionalMarket rm{};
    rm.good_id = 1;
    rm.province_id = 0;
    rm.supply = 1000.0f;
    state.regional_markets.push_back(rm);

    PlayerCharacter player{};
    player.id = 999;
    state.player = &player;

    AntitrustModule module;
    module.next_check_tick() = 30;

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Actor 100 has 75% share (>= 0.70 threshold), Tier 2 triggered
    CHECK(module.proposal_pressure().count(0) > 0);
    CHECK(module.proposal_pressure()[0] > 0.0f);
}

TEST_CASE("Pressure decays when no dominant actor", "[antitrust][tier7]") {
    WorldState state{};
    state.current_tick = 30;

    Province prov{};
    prov.id = 0;
    state.provinces.push_back(prov);

    // No businesses with dominant share
    NPCBusiness biz1{};
    biz1.id = 1;
    biz1.province_id = 0;
    biz1.criminal_sector = false;
    biz1.revenue_per_tick = 500.0f;
    biz1.owner_id = 100;
    state.npc_businesses.push_back(biz1);

    NPCBusiness biz2{};
    biz2.id = 2;
    biz2.province_id = 0;
    biz2.criminal_sector = false;
    biz2.revenue_per_tick = 500.0f;
    biz2.owner_id = 200;
    state.npc_businesses.push_back(biz2);

    RegionalMarket rm{};
    rm.good_id = 1;
    rm.province_id = 0;
    rm.supply = 1000.0f;
    state.regional_markets.push_back(rm);

    PlayerCharacter player{};
    player.id = 999;
    state.player = &player;

    AntitrustModule module;
    module.next_check_tick() = 30;

    // Pre-seed some pressure
    module.proposal_pressure()[0] = 0.20f;

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Both actors at 50% share — no Tier 2 trigger
    // Pressure should have decayed
    CHECK(module.proposal_pressure()[0] < 0.20f);
}

TEST_CASE("Zero supply good skipped", "[antitrust][tier7]") {
    WorldState state{};
    state.current_tick = 30;

    Province prov{};
    prov.id = 0;
    state.provinces.push_back(prov);

    RegionalMarket rm{};
    rm.good_id = 1;
    rm.province_id = 0;
    rm.supply = 0.0f;  // zero supply
    state.regional_markets.push_back(rm);

    PlayerCharacter player{};
    player.id = 999;
    state.player = &player;

    AntitrustModule module;
    module.next_check_tick() = 30;

    DeltaBuffer delta{};
    // Should not crash with zero supply
    module.execute(state, delta);
    CHECK(delta.npc_deltas.empty());
}
