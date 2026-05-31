#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/apply_deltas.h"  // rebuild_npc_indices
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"
#include "modules/addiction/addiction_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

TEST_CASE("Addiction: casual to regular transition", "[addiction][tier10]") {
    AddictionState state;
    state.stage = AddictionStage::casual;
    state.consecutive_use_ticks = 30;
    state.craving = 0.35f;
    state.tolerance = 0.20f;

    auto next = AddictionModule::compute_next_stage(state);
    REQUIRE(next == AddictionStage::regular);
}

TEST_CASE("Addiction: casual stays casual below threshold", "[addiction][tier10]") {
    AddictionState state;
    state.stage = AddictionStage::casual;
    state.consecutive_use_ticks = 20;
    state.craving = 0.25f;

    auto next = AddictionModule::compute_next_stage(state);
    REQUIRE(next == AddictionStage::casual);
}

TEST_CASE("Addiction: regular to dependent transition", "[addiction][tier10]") {
    AddictionState state;
    state.stage = AddictionStage::regular;
    state.consecutive_use_ticks = 90;
    state.craving = 0.75f;
    state.tolerance = 0.35f;

    auto next = AddictionModule::compute_next_stage(state);
    REQUIRE(next == AddictionStage::dependent);
}

TEST_CASE("Addiction: dependent to active transition", "[addiction][tier10]") {
    AddictionState state;
    state.stage = AddictionStage::dependent;
    state.craving = 0.75f;
    state.consecutive_use_ticks = 60;

    auto next = AddictionModule::compute_next_stage(state);
    REQUIRE(next == AddictionStage::active);
}

TEST_CASE("Addiction: active to recovery with clean ticks", "[addiction][tier10]") {
    AddictionState state;
    state.stage = AddictionStage::active;
    state.clean_ticks = 14;

    auto next = AddictionModule::compute_next_stage(state);
    REQUIRE(next == AddictionStage::recovery);
}

TEST_CASE("Addiction: craving increment per stage", "[addiction][tier10]") {
    REQUIRE_THAT(AddictionModule::craving_increment(AddictionStage::casual),
                 WithinAbs(0.01f, 0.001f));
    REQUIRE_THAT(AddictionModule::craving_increment(AddictionStage::regular),
                 WithinAbs(0.02f, 0.001f));
    REQUIRE_THAT(AddictionModule::craving_increment(AddictionStage::dependent),
                 WithinAbs(0.03f, 0.001f));
    REQUIRE_THAT(AddictionModule::craving_increment(AddictionStage::active),
                 WithinAbs(0.05f, 0.001f));
    REQUIRE_THAT(AddictionModule::craving_increment(AddictionStage::recovery),
                 WithinAbs(-0.003f, 0.001f));
    REQUIRE_THAT(AddictionModule::craving_increment(AddictionStage::none), WithinAbs(0.0f, 0.001f));
}

TEST_CASE("Addiction: withdrawal damage at dependent stage", "[addiction][tier10]") {
    float dmg = AddictionModule::compute_withdrawal_damage(AddictionStage::dependent, 5);
    REQUIRE_THAT(dmg, WithinAbs(0.005f, 0.001f));
}

TEST_CASE("Addiction: no withdrawal below dependent", "[addiction][tier10]") {
    REQUIRE_THAT(AddictionModule::compute_withdrawal_damage(AddictionStage::casual, 10),
                 WithinAbs(0.0f, 0.001f));
    REQUIRE_THAT(AddictionModule::compute_withdrawal_damage(AddictionStage::regular, 10),
                 WithinAbs(0.0f, 0.001f));
}

TEST_CASE("Addiction: no withdrawal with zero supply gap", "[addiction][tier10]") {
    REQUIRE_THAT(AddictionModule::compute_withdrawal_damage(AddictionStage::dependent, 0),
                 WithinAbs(0.0f, 0.001f));
}

TEST_CASE("Addiction: work efficiency per stage", "[addiction][tier10]") {
    REQUIRE_THAT(AddictionModule::compute_work_efficiency(AddictionStage::none),
                 WithinAbs(1.0f, 0.01f));
    REQUIRE_THAT(AddictionModule::compute_work_efficiency(AddictionStage::casual),
                 WithinAbs(1.0f, 0.01f));
    REQUIRE_THAT(AddictionModule::compute_work_efficiency(AddictionStage::dependent),
                 WithinAbs(0.70f, 0.01f));
    REQUIRE_THAT(AddictionModule::compute_work_efficiency(AddictionStage::active),
                 WithinAbs(0.50f, 0.01f));
    REQUIRE_THAT(AddictionModule::compute_work_efficiency(AddictionStage::terminal),
                 WithinAbs(0.20f, 0.01f));
}

TEST_CASE("Addiction: rate delta from stage transition", "[addiction][tier10]") {
    // Entering counted stage
    float enter = AddictionModule::compute_addiction_rate_delta(AddictionStage::regular,
                                                                AddictionStage::dependent);
    REQUIRE_THAT(enter, WithinAbs(0.001f, 0.0001f));

    // Leaving counted stage (recovery)
    float leave = AddictionModule::compute_addiction_rate_delta(AddictionStage::active,
                                                                AddictionStage::recovery);
    REQUIRE_THAT(leave, WithinAbs(-0.001f, 0.0001f));

    // No change within counted stages
    float same = AddictionModule::compute_addiction_rate_delta(AddictionStage::dependent,
                                                               AddictionStage::active);
    REQUIRE_THAT(same, WithinAbs(0.0f, 0.0001f));
}

TEST_CASE("Addiction: recovery complete check", "[addiction][tier10]") {
    REQUIRE(AddictionModule::is_recovery_complete(365, 0.04f) == true);
    REQUIRE(AddictionModule::is_recovery_complete(365, 0.06f) == false);
    REQUIRE(AddictionModule::is_recovery_complete(300, 0.04f) == false);
}

TEST_CASE("Addiction: config defaults match spec", "[addiction][tier10]") {
    constexpr AddictionConfig cfg{};
    REQUIRE_THAT(cfg.tolerance_per_use_casual, WithinAbs(0.05f, 0.001f));
    REQUIRE(cfg.regular_use_threshold == 30);
    REQUIRE(cfg.dependency_threshold == 90);
    REQUIRE_THAT(cfg.withdrawal_health_hit, WithinAbs(0.005f, 0.001f));
    REQUIRE_THAT(cfg.dependent_work_efficiency, WithinAbs(0.70f, 0.01f));
    REQUIRE(cfg.full_recovery_ticks == 365);
    REQUIRE_THAT(cfg.terminal_health_threshold, WithinAbs(0.15f, 0.01f));
    REQUIRE(cfg.terminal_persistence_ticks == 90);
}

// --- Wired-state proof-of-life --------------------------------------------
//
// Before the set_addiction_state() seeder existed, no NPC could enter the
// addiction state machine — the module ran every tick but skipped every NPC
// because addiction_states_ was empty. These tests prove that with a seeded
// state, execute_province() actually steps the state machine and the
// changes persist across ticks.
//
// The cross-module seeding hookup (drug_economy → addiction) is the open
// architectural question — see docs/session_logs/flagged_issues.md.

namespace {

WorldState make_world_with_npc(uint32_t npc_id, uint32_t province_id) {
    WorldState w{};
    w.current_tick = 0;
    w.world_seed = 1;
    w.game_mode = GameMode::standard;

    Province p{};
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    p.id = province_id;
    w.provinces.push_back(p);

    NPC npc{};
    npc.id = npc_id;
    npc.current_province_id = province_id;
    npc.home_province_id = province_id;
    npc.status = NPCStatus::active;
    npc.capital = 10000.0f;
    w.significant_npcs.push_back(npc);

    rebuild_npc_indices(w);
    return w;
}

}  // namespace

TEST_CASE("Addiction: seeded state persists craving accumulation across ticks",
          "[addiction][tier10][state]") {
    auto world = make_world_with_npc(/*npc_id=*/100, /*province=*/0);

    // Per-NPC addiction state now lives on NPC::addiction_state. Seed
    // directly; AddictionModule reads from there and writes back through
    // NPCDelta::set_addiction_state, which apply_deltas persists.
    AddictionState seed{};
    seed.stage = AddictionStage::casual;
    seed.substance_key = "cocaine";
    seed.tolerance = 0.10f;
    seed.craving = 0.20f;
    seed.consecutive_use_ticks = 5;
    world.significant_npcs[0].addiction_state = seed;

    AddictionModule module;
    DeltaBuffer delta{};
    module.execute_province(0, world, delta);
    apply_deltas(world, delta);

    // The state machine should have stepped: craving incremented per stage
    // (casual: +0.01), tolerance grew (consecutive_use_ticks > 0 in casual),
    // and the new state was persisted onto NPC.
    const AddictionState& after = world.significant_npcs[0].addiction_state;
    REQUIRE(after.craving > seed.craving);
    REQUIRE(after.tolerance > seed.tolerance);
}

TEST_CASE("Addiction: stage progresses casual -> regular over enough ticks",
          "[addiction][tier10][state]") {
    auto world = make_world_with_npc(/*npc_id=*/100, /*province=*/0);

    AddictionState seed{};
    seed.stage = AddictionStage::casual;
    seed.craving = 0.295f;            // one tick (+0.01) crosses 0.30 threshold
    seed.consecutive_use_ticks = 30;  // meets regular_use_threshold
    world.significant_npcs[0].addiction_state = seed;

    // One tick raises craving by craving_increment(casual) (0.01 default),
    // pushing it past the 0.30 casual_to_regular_craving threshold;
    // compute_next_stage advances stage to regular.
    AddictionModule module;
    DeltaBuffer delta{};
    module.execute_province(0, world, delta);
    apply_deltas(world, delta);

    REQUIRE(world.significant_npcs[0].addiction_state.stage == AddictionStage::regular);
}

TEST_CASE("Addiction: NPCs with stage=none are invisible to the module",
          "[addiction][tier10][state]") {
    auto world = make_world_with_npc(/*npc_id=*/100, /*province=*/0);

    // NPC::addiction_state defaults to {stage=none, ...}; no substance
    // pathway has seeded the NPC into the state machine.
    REQUIRE(world.significant_npcs[0].addiction_state.stage == AddictionStage::none);

    AddictionModule module;
    DeltaBuffer delta{};
    module.execute_province(0, world, delta);

    REQUIRE(delta.npc_deltas.empty());
    REQUIRE(delta.region_deltas.empty());
    // State stays at the default after the no-op run.
    REQUIRE(world.significant_npcs[0].addiction_state.stage == AddictionStage::none);
}

// --- Per-tick state-machine maturation ------------------------------------
//
// These tests exercise the per-tick effects the module applies on top of the
// static transition helpers: use/clean tick accrual, recovery completion,
// deterministic relapse, and the one-time active-stage memory.

namespace {

void step_addiction(WorldState& world, AddictionModule& module) {
    world.current_tick += 1;
    DeltaBuffer delta{};
    module.execute_province(0, world, delta);
    apply_deltas(world, delta);
}

}  // namespace

TEST_CASE("Addiction: consecutive_use_ticks accrual drives casual -> regular over ticks",
          "[addiction][tier10][state]") {
    auto world = make_world_with_npc(/*npc_id=*/100, /*province=*/0);

    AddictionState seed{};
    seed.stage = AddictionStage::casual;
    seed.substance_key = "cocaine";
    seed.craving = 0.20f;
    seed.consecutive_use_ticks = 0;  // must accrue organically to reach 30
    world.significant_npcs[0].addiction_state = seed;

    AddictionModule module;
    // 30 ticks: craving climbs 0.01/tick past the 0.30 threshold and
    // consecutive_use_ticks accrues to the regular_use_threshold (30). Without
    // the per-tick counter increment the NPC would be frozen at casual forever.
    for (int i = 0; i < 30; ++i) {
        step_addiction(world, module);
    }

    const AddictionState& after = world.significant_npcs[0].addiction_state;
    REQUIRE(after.consecutive_use_ticks >= 30);
    REQUIRE(after.stage == AddictionStage::regular);
}

TEST_CASE("Addiction: clean_ticks accrual completes recovery to none",
          "[addiction][tier10][state]") {
    auto world = make_world_with_npc(/*npc_id=*/100, /*province=*/0);

    AddictionState seed{};
    seed.stage = AddictionStage::recovery;
    seed.substance_key = "cocaine";
    seed.craving = 0.0f;  // relapse_probability 0 -> no relapse, decay below 0.05
    seed.clean_ticks = 364;
    world.significant_npcs[0].addiction_state = seed;

    AddictionModule module;
    // One tick pushes clean_ticks to full_recovery_ticks (365); with craving 0
    // the recovery-success threshold is met and the machine clears to none.
    step_addiction(world, module);

    REQUIRE(world.significant_npcs[0].addiction_state.stage == AddictionStage::none);
    REQUIRE(world.significant_npcs[0].addiction_state.substance_key.empty());
}

TEST_CASE("Addiction: certain relapse returns recovery NPC to active",
          "[addiction][tier10][state]") {
    auto world = make_world_with_npc(/*npc_id=*/100, /*province=*/0);

    AddictionState seed{};
    seed.stage = AddictionStage::recovery;
    seed.substance_key = "cocaine";
    seed.craving = 1.0f;  // relapse_probability ~1.0 -> next_float() always below it
    seed.clean_ticks = 10;
    world.significant_npcs[0].addiction_state = seed;

    AddictionModule module;
    step_addiction(world, module);

    // relapse_probability after one recovery decay tick is 0.997, and
    // next_float() is in [0,1): the draw is below it, so the NPC relapses.
    REQUIRE(world.significant_npcs[0].addiction_state.stage == AddictionStage::active);
    REQUIRE(world.significant_npcs[0].addiction_state.clean_ticks == 0);
}

TEST_CASE("Addiction: relapse draws are deterministic across identical runs",
          "[addiction][tier10][state][determinism]") {
    auto run = []() {
        auto world = make_world_with_npc(/*npc_id=*/100, /*province=*/0);
        AddictionState seed{};
        seed.stage = AddictionStage::recovery;
        seed.substance_key = "cocaine";
        seed.craving = 0.50f;  // partial relapse probability -> RNG actually matters
        seed.clean_ticks = 20;
        world.significant_npcs[0].addiction_state = seed;

        AddictionModule module;
        for (int i = 0; i < 5; ++i) {
            step_addiction(world, module);
        }
        return world.significant_npcs[0].addiction_state;
    };

    AddictionState a = run();
    AddictionState b = run();
    REQUIRE(a.stage == b.stage);
    REQUIRE(a.clean_ticks == b.clean_ticks);
    REQUIRE(a.consecutive_use_ticks == b.consecutive_use_ticks);
    REQUIRE_THAT(a.craving, WithinAbs(b.craving, 1e-6f));
}

TEST_CASE("Addiction: entering active stage emits one-time impairment memory",
          "[addiction][tier10][state]") {
    auto world = make_world_with_npc(/*npc_id=*/100, /*province=*/0);

    AddictionState seed{};
    seed.stage = AddictionStage::dependent;
    seed.substance_key = "cocaine";
    seed.craving = 0.70f;             // at active_craving_threshold
    seed.consecutive_use_ticks = 60;  // at active_duration_ticks
    world.significant_npcs[0].addiction_state = seed;

    AddictionModule module;
    world.current_tick += 1;
    DeltaBuffer delta{};
    module.execute_province(0, world, delta);

    // The dependent -> active transition fires; exactly one NPCDelta carries a
    // new memory entry marking impaired function.
    REQUIRE(delta.npc_deltas.size() == 1);
    REQUIRE(delta.npc_deltas[0].new_memory_entry.has_value());
    REQUIRE(delta.npc_deltas[0].set_addiction_state.has_value());
    REQUIRE(delta.npc_deltas[0].set_addiction_state->stage == AddictionStage::active);

    // A second tick at active stage must NOT re-emit the memory.
    apply_deltas(world, delta);
    world.current_tick += 1;
    DeltaBuffer delta2{};
    module.execute_province(0, world, delta2);
    REQUIRE(delta2.npc_deltas.size() == 1);
    REQUIRE_FALSE(delta2.npc_deltas[0].new_memory_entry.has_value());
}

// --- Withdrawal health, terminal stage, and death -------------------------
//
// withdrawal_health is the addiction-local body-health proxy. It falls when a
// dependent+ NPC can't afford supply (a supply gap), recovers when supplied,
// gates entry into the terminal stage after sustained deprivation, and kills
// the NPC when it reaches 0.0 at terminal.

TEST_CASE("Addiction: unsupplied dependent NPC accrues supply gap and loses health",
          "[addiction][tier10][state]") {
    auto world = make_world_with_npc(/*npc_id=*/100, /*province=*/0);

    AddictionState seed{};
    seed.stage = AddictionStage::dependent;
    seed.substance_key = "cocaine";
    seed.craving = 0.50f;
    seed.consecutive_use_ticks = 90;
    seed.tolerance = 0.40f;
    seed.withdrawal_health = 1.0f;
    world.significant_npcs[0].addiction_state = seed;
    // Can't afford the dependent-stage spend (30) -> supply gap -> withdrawal.
    world.significant_npcs[0].capital = 5.0f;

    AddictionModule module;
    step_addiction(world, module);

    const AddictionState& after = world.significant_npcs[0].addiction_state;
    REQUIRE(after.supply_gap_ticks == 1);
    REQUIRE(after.withdrawal_health < 1.0f);
    // No charge when unsupplied: capital unchanged.
    REQUIRE_THAT(world.significant_npcs[0].capital, WithinAbs(5.0f, 1e-6f));
}

TEST_CASE("Addiction: supplied dependent NPC pays and recovers health",
          "[addiction][tier10][state]") {
    auto world = make_world_with_npc(/*npc_id=*/100, /*province=*/0);

    AddictionState seed{};
    seed.stage = AddictionStage::dependent;
    seed.substance_key = "cocaine";
    seed.craving = 0.50f;
    seed.consecutive_use_ticks = 90;
    seed.tolerance = 0.40f;
    seed.withdrawal_health = 0.50f;  // depressed, room to recover
    world.significant_npcs[0].addiction_state = seed;
    world.significant_npcs[0].capital = 1000.0f;  // can afford supply

    AddictionModule module;
    step_addiction(world, module);

    const AddictionState& after = world.significant_npcs[0].addiction_state;
    REQUIRE(after.supply_gap_ticks == 0);
    REQUIRE(after.withdrawal_health > 0.50f);                                   // recovered
    REQUIRE_THAT(world.significant_npcs[0].capital, WithinAbs(970.0f, 1e-3f));  // paid 30
}

TEST_CASE("Addiction: sustained deprivation drives terminal stage then death",
          "[addiction][tier10][state]") {
    // Tight config so the test runs in a handful of ticks: terminal after 3
    // sub-threshold ticks, big health hit, no recovery headroom needed.
    AddictionConfig cfg{};
    cfg.terminal_health_threshold = 0.90f;  // easy to fall below
    cfg.terminal_persistence_ticks = 3;     // terminal after 3 deprived ticks
    cfg.withdrawal_health_hit = 0.20f;      // 5 hits from 1.0 reach 0.0

    auto world = make_world_with_npc(/*npc_id=*/100, /*province=*/0);
    AddictionState seed{};
    seed.stage = AddictionStage::dependent;
    seed.substance_key = "cocaine";
    seed.craving = 0.20f;
    seed.consecutive_use_ticks = 90;
    seed.tolerance = 0.40f;
    seed.withdrawal_health = 1.0f;
    world.significant_npcs[0].addiction_state = seed;
    world.significant_npcs[0].capital = 0.0f;  // permanently unsupplied

    AddictionModule module{cfg};

    bool saw_terminal = false;
    bool saw_death = false;
    for (int i = 0; i < 12 && !saw_death; ++i) {
        world.current_tick += 1;
        DeltaBuffer delta{};
        module.execute_province(0, world, delta);
        for (const auto& d : delta.npc_deltas) {
            if (d.set_addiction_state.has_value() &&
                d.set_addiction_state->stage == AddictionStage::terminal) {
                saw_terminal = true;
            }
            if (d.new_status.has_value() && *d.new_status == NPCStatus::dead) {
                saw_death = true;
            }
        }
        apply_deltas(world, delta);
    }

    REQUIRE(saw_terminal);
    REQUIRE(saw_death);
    REQUIRE(world.significant_npcs[0].status == NPCStatus::dead);
}

TEST_CASE("Addiction: terminal death emits consequence and clamps health at zero",
          "[addiction][tier10][state]") {
    AddictionConfig cfg{};
    cfg.withdrawal_health_hit = 0.50f;  // one tick from 0.4 reaches 0.0

    auto world = make_world_with_npc(/*npc_id=*/100, /*province=*/0);
    AddictionState seed{};
    seed.stage = AddictionStage::terminal;  // already terminal
    seed.substance_key = "cocaine";
    seed.withdrawal_health = 0.40f;
    world.significant_npcs[0].addiction_state = seed;

    AddictionModule module{cfg};
    world.current_tick += 1;
    DeltaBuffer delta{};
    module.execute_province(0, world, delta);

    REQUIRE(delta.npc_deltas.size() == 1);
    REQUIRE(delta.npc_deltas[0].new_status.has_value());
    REQUIRE(*delta.npc_deltas[0].new_status == NPCStatus::dead);
    REQUIRE(delta.consequence_deltas.size() == 1);
    REQUIRE(delta.npc_deltas[0].set_addiction_state->withdrawal_health <= 0.0f);
}
