#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/apply_deltas.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/drug_economy/drug_economy_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

// ============================================================================
// Static utility tests
// ============================================================================

TEST_CASE("DrugEconomy: wholesale_price_fraction applied", "[drug_economy][tier8]") {
    // Retail spot_price 100, wholesale fraction 0.45 -> 45
    float price = DrugEconomyModule::compute_wholesale_price(100.0f, 0.45f);
    REQUIRE_THAT(price, WithinAbs(45.0f, 0.01f));
}

TEST_CASE("DrugEconomy: wholesale price zero when spot price zero", "[drug_economy][tier8]") {
    float price = DrugEconomyModule::compute_wholesale_price(0.0f, 0.45f);
    REQUIRE_THAT(price, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("DrugEconomy: quality degrades through distribution", "[drug_economy][tier8]") {
    // Production quality 0.95
    float wholesale_quality = DrugEconomyModule::degrade_quality(0.95f, 0.95f);
    REQUIRE_THAT(wholesale_quality, WithinAbs(0.9025f, 0.001f));

    float retail_quality = DrugEconomyModule::degrade_quality(wholesale_quality, 0.90f);
    REQUIRE_THAT(retail_quality, WithinAbs(0.81225f, 0.001f));
}

TEST_CASE("DrugEconomy: quality clamped to [0,1]", "[drug_economy][tier8]") {
    float result = DrugEconomyModule::degrade_quality(1.5f, 0.95f);
    REQUIRE(result <= 1.0f);

    float result2 = DrugEconomyModule::degrade_quality(0.0f, 0.95f);
    REQUIRE(result2 >= 0.0f);
}

TEST_CASE("DrugEconomy: addiction demand computation", "[drug_economy][tier8]") {
    // addiction_rate 0.05, population 100000, demand_per_addict 1.0
    float demand = DrugEconomyModule::compute_addiction_demand(0.05f, 100000, 1.0f);
    REQUIRE_THAT(demand, WithinAbs(5000.0f, 1.0f));
}

TEST_CASE("DrugEconomy: precursor consumption for meth", "[drug_economy][tier8]") {
    // 100 units meth output, ratio 2.0 -> 200 units precursor
    float precursor = DrugEconomyModule::compute_precursor_consumption(100.0f, 2.0f);
    REQUIRE_THAT(precursor, WithinAbs(200.0f, 0.01f));
}

TEST_CASE("DrugEconomy: drug legalization status check", "[drug_economy][tier8]") {
    DrugLegalizationStatus status{true, false, false, true};

    REQUIRE(DrugEconomyModule::is_drug_legal(status, DrugType::cannabis) == true);
    REQUIRE(DrugEconomyModule::is_drug_legal(status, DrugType::methamphetamine) == false);
    REQUIRE(DrugEconomyModule::is_drug_legal(status, DrugType::synthetic_opioid) == false);
    REQUIRE(DrugEconomyModule::is_drug_legal(status, DrugType::designer_drug) == true);
}

TEST_CASE("DrugEconomy: legal cannabis uses formal market", "[drug_economy][tier8]") {
    DrugLegalizationStatus status{true, false, false, true};
    REQUIRE(status.is_legal(DrugType::cannabis) == true);
}

TEST_CASE("DrugEconomy: illegal cannabis uses informal market", "[drug_economy][tier8]") {
    DrugLegalizationStatus status{false, false, false, true};
    REQUIRE(status.is_legal(DrugType::cannabis) == false);
}

TEST_CASE("DrugEconomy: meth chemical waste signature", "[drug_economy][tier8]") {
    // 50 units output, 0.15 waste per unit -> 7.5 -> clamped to 1.0 since max
    float waste = DrugEconomyModule::compute_meth_waste_signature(50.0f, 0.15f);
    REQUIRE(waste <= 1.0f);
    REQUIRE(waste > 0.0f);

    // Small output
    float waste_small = DrugEconomyModule::compute_meth_waste_signature(1.0f, 0.15f);
    REQUIRE_THAT(waste_small, WithinAbs(0.15f, 0.01f));
}

TEST_CASE("DrugEconomy: precursor shortage clamps production", "[drug_economy][tier8]") {
    // Lab needs 10 units but only 3 available
    // Output should be clamped proportionally: 3 / 2.0 ratio = 1.5 units drug
    float available_precursor = 3.0f;
    float ratio = 2.0f;
    float clamped_output = available_precursor / ratio;
    REQUIRE_THAT(clamped_output, WithinAbs(1.5f, 0.01f));
}

TEST_CASE("DrugEconomy: zero addiction rate produces zero demand", "[drug_economy][tier8]") {
    float demand = DrugEconomyModule::compute_addiction_demand(0.0f, 100000, 1.0f);
    REQUIRE_THAT(demand, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("DrugEconomy: wholesale price fraction constant is 0.45", "[drug_economy][tier8]") {
    REQUIRE_THAT(DrugEconomyConfig{}.wholesale_price_fraction, WithinAbs(0.45f, 0.001f));
}

TEST_CASE("DrugEconomy: quality degradation factors match spec", "[drug_economy][tier8]") {
    // Starting quality 1.0 through wholesale (0.95) then retail (0.90)
    float after_wholesale = DrugEconomyModule::degrade_quality(1.0f, 0.95f);
    REQUIRE_THAT(after_wholesale, WithinAbs(0.95f, 0.001f));

    float after_retail = DrugEconomyModule::degrade_quality(after_wholesale, 0.90f);
    REQUIRE_THAT(after_retail, WithinAbs(0.855f, 0.001f));
}

TEST_CASE("DrugEconomy: designer drug initially legal", "[drug_economy][tier8]") {
    DrugLegalizationStatus status{false, false, false, true};
    REQUIRE(DrugEconomyModule::is_drug_legal(status, DrugType::designer_drug) == true);
}

TEST_CASE("DrugEconomy: all drugs illegal in strict province", "[drug_economy][tier8]") {
    DrugLegalizationStatus status{false, false, false, false};
    REQUIRE(DrugEconomyModule::is_drug_legal(status, DrugType::cannabis) == false);
    REQUIRE(DrugEconomyModule::is_drug_legal(status, DrugType::methamphetamine) == false);
    REQUIRE(DrugEconomyModule::is_drug_legal(status, DrugType::synthetic_opioid) == false);
    REQUIRE(DrugEconomyModule::is_drug_legal(status, DrugType::designer_drug) == false);
}

// ============================================================================
// Addiction seeding tests
// ============================================================================
//
// drug_economy seeds NPC.addiction_state at stage=casual when supply or
// pre-existing addiction is present in a province. Tests use a high
// addiction_seeding_probability so the rolls fire deterministically without
// requiring a huge population.

namespace {

WorldState make_seeding_world(uint32_t npc_count, bool with_drug_business) {
    WorldState state{};
    state.current_tick = 1;
    state.world_seed = 1234;
    state.player.reset();
    state.lod2_price_index.reset();

    Province prov{};
    prov.cohort_stats = std::make_unique<RegionCohortStats>();
    prov.id = 0;
    prov.region_id = 0;
    prov.lod_level = SimulationLOD::full;
    prov.cohort_stats->total_population = npc_count;
    state.provinces.push_back(std::move(prov));

    for (uint32_t i = 0; i < npc_count; ++i) {
        NPC npc{};
        npc.id = 1000 + i;
        npc.role = NPCRole::worker;
        npc.status = NPCStatus::active;
        npc.current_province_id = 0;
        npc.home_province_id = 0;
        npc.addiction_state = AddictionState{};  // stage = none
        state.significant_npcs.push_back(npc);
    }

    if (with_drug_business) {
        NPCBusiness biz{};
        biz.id = 1;
        biz.criminal_sector = true;
        biz.province_id = 0;
        biz.revenue_per_tick = 100.0f;
        biz.market_share = 0.05f;
        biz.owner_id = 1000;
        state.npc_businesses.push_back(biz);
    }

    rebuild_npc_indices(state);
    return state;
}

uint32_t count_seeded(const DeltaBuffer& delta) {
    uint32_t n = 0;
    for (const auto& d : delta.npc_deltas) {
        if (d.set_addiction_state.has_value() &&
            d.set_addiction_state->stage == AddictionStage::casual) {
            ++n;
        }
    }
    return n;
}

}  // namespace

TEST_CASE("DrugEconomy: no seeding when no supply and zero addiction_rate",
          "[drug_economy][seeding][tier8]") {
    DrugEconomyConfig cfg{};
    cfg.addiction_seeding_probability = 1.0f;  // would seed every NPC if eligible
    DrugEconomyModule module(cfg);

    WorldState state = make_seeding_world(50, /*with_drug_business=*/false);
    REQUIRE(state.provinces[0].cohort_stats->addiction_rate == 0.0f);

    DeltaBuffer delta;
    module.init_for_tick(state);
    module.execute_province(0, state, delta);

    REQUIRE(count_seeded(delta) == 0);
}

TEST_CASE("DrugEconomy: seeds NPCs when drug business present", "[drug_economy][seeding][tier8]") {
    DrugEconomyConfig cfg{};
    cfg.addiction_seeding_probability = 1.0f;  // every eligible NPC seeded
    DrugEconomyModule module(cfg);

    WorldState state = make_seeding_world(50, /*with_drug_business=*/true);

    DeltaBuffer delta;
    module.init_for_tick(state);
    module.execute_province(0, state, delta);

    REQUIRE(count_seeded(delta) == 50);
    // Verify substance_key and stage set correctly.
    for (const auto& d : delta.npc_deltas) {
        if (d.set_addiction_state.has_value()) {
            REQUIRE(d.set_addiction_state->stage == AddictionStage::casual);
            REQUIRE(d.set_addiction_state->substance_key == "cannabis");
        }
    }
}

TEST_CASE("DrugEconomy: saturation cap halts further seeding", "[drug_economy][seeding][tier8]") {
    DrugEconomyConfig cfg{};
    cfg.addiction_seeding_probability = 1.0f;
    cfg.addiction_seeding_saturation_cap = 0.05f;
    DrugEconomyModule module(cfg);

    WorldState state = make_seeding_world(50, /*with_drug_business=*/true);
    // Province already at cap → no further seeding.
    state.provinces[0].cohort_stats->addiction_rate = 0.05f;

    DeltaBuffer delta;
    module.init_for_tick(state);
    module.execute_province(0, state, delta);

    REQUIRE(count_seeded(delta) == 0);
}

TEST_CASE("DrugEconomy: seeding is deterministic across runs",
          "[drug_economy][seeding][determinism][tier8]") {
    DrugEconomyConfig cfg{};
    cfg.addiction_seeding_probability = 0.3f;  // partial — exercises the RNG path
    DrugEconomyModule module_a(cfg);
    DrugEconomyModule module_b(cfg);

    WorldState state_a = make_seeding_world(100, /*with_drug_business=*/true);
    WorldState state_b = make_seeding_world(100, /*with_drug_business=*/true);

    DeltaBuffer delta_a, delta_b;
    module_a.init_for_tick(state_a);
    module_a.execute_province(0, state_a, delta_a);
    module_b.init_for_tick(state_b);
    module_b.execute_province(0, state_b, delta_b);

    REQUIRE(count_seeded(delta_a) > 0);
    REQUIRE(count_seeded(delta_a) < 100);  // not every NPC at 0.3 probability
    REQUIRE(count_seeded(delta_a) == count_seeded(delta_b));

    // The same set of NPC ids must be seeded.
    std::vector<uint32_t> ids_a, ids_b;
    for (const auto& d : delta_a.npc_deltas) {
        if (d.set_addiction_state.has_value())
            ids_a.push_back(d.npc_id);
    }
    for (const auto& d : delta_b.npc_deltas) {
        if (d.set_addiction_state.has_value())
            ids_b.push_back(d.npc_id);
    }
    REQUIRE(ids_a == ids_b);
}

TEST_CASE("DrugEconomy: skips NPCs already in the addiction pipeline",
          "[drug_economy][seeding][tier8]") {
    DrugEconomyConfig cfg{};
    cfg.addiction_seeding_probability = 1.0f;
    DrugEconomyModule module(cfg);

    WorldState state = make_seeding_world(10, /*with_drug_business=*/true);
    // Half the NPCs are already in the pipeline.
    for (uint32_t i = 0; i < 5; ++i) {
        state.significant_npcs[i].addiction_state.stage = AddictionStage::regular;
        state.significant_npcs[i].addiction_state.substance_key = "cannabis";
    }
    rebuild_npc_indices(state);

    DeltaBuffer delta;
    module.init_for_tick(state);
    module.execute_province(0, state, delta);

    // Only the 5 NPCs at stage=none should be seeded.
    REQUIRE(count_seeded(delta) == 5);
}
