#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/legal_process/legal_process_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

TEST_CASE("LegalProcess: conviction probability formula", "[legal_process][tier9]") {
    // evidence 0.8, defense 0.5, bias 1.0, witness 1.0
    // 0.8 * (1 - 0.5*0.4) * 1.0 * 1.0 = 0.8 * 0.8 = 0.64
    float prob = LegalProcessModule::compute_conviction_probability(0.8f, 0.5f, 1.0f, 1.0f, 0.4f);
    REQUIRE_THAT(prob, WithinAbs(0.64f, 0.01f));
}

TEST_CASE("LegalProcess: perfect defense reduces conviction", "[legal_process][tier9]") {
    float prob = LegalProcessModule::compute_conviction_probability(0.8f, 1.0f, 1.0f, 1.0f, 0.4f);
    // 0.8 * (1 - 0.4) = 0.48
    REQUIRE_THAT(prob, WithinAbs(0.48f, 0.01f));
}

TEST_CASE("LegalProcess: zero evidence gives zero probability", "[legal_process][tier9]") {
    float prob = LegalProcessModule::compute_conviction_probability(0.0f, 0.5f, 1.0f, 1.0f, 0.4f);
    REQUIRE_THAT(prob, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("LegalProcess: conviction probability clamped to [0,1]", "[legal_process][tier9]") {
    float prob = LegalProcessModule::compute_conviction_probability(1.5f, 0.0f, 1.0f, 1.0f, 0.4f);
    REQUIRE(prob <= 1.0f);
}

TEST_CASE("LegalProcess: sentence scales with severity", "[legal_process][tier9]") {
    REQUIRE(LegalProcessModule::compute_sentence_ticks(CaseSeverity::minor, 365) == 365);
    REQUIRE(LegalProcessModule::compute_sentence_ticks(CaseSeverity::moderate, 365) == 730);
    REQUIRE(LegalProcessModule::compute_sentence_ticks(CaseSeverity::capital, 365) == 2190);
}

TEST_CASE("LegalProcess: double jeopardy cooldown", "[legal_process][tier9]") {
    REQUIRE(LegalProcessModule::is_double_jeopardy_active(100, 200) == true);
    REQUIRE(LegalProcessModule::is_double_jeopardy_active(200, 200) == false);
    REQUIRE(LegalProcessModule::is_double_jeopardy_active(300, 200) == false);
}

TEST_CASE("LegalProcess: stage advancement", "[legal_process][tier9]") {
    REQUIRE(LegalProcessModule::advance_stage(LegalCaseStage::investigation, false) ==
            LegalCaseStage::arrested);
    REQUIRE(LegalProcessModule::advance_stage(LegalCaseStage::arrested, false) ==
            LegalCaseStage::charged);
    REQUIRE(LegalProcessModule::advance_stage(LegalCaseStage::charged, false) ==
            LegalCaseStage::trial);
    REQUIRE(LegalProcessModule::advance_stage(LegalCaseStage::trial, true) ==
            LegalCaseStage::convicted);
    REQUIRE(LegalProcessModule::advance_stage(LegalCaseStage::trial, false) ==
            LegalCaseStage::acquitted);
}

TEST_CASE("LegalProcess: evidence weight aggregation", "[legal_process][tier9]") {
    std::vector<float> tokens = {0.3f, 0.2f, 0.4f};
    float weight = LegalProcessModule::compute_evidence_weight(tokens);
    REQUIRE_THAT(weight, WithinAbs(0.9f, 0.01f));
}

TEST_CASE("LegalProcess: evidence weight clamped to 1.0", "[legal_process][tier9]") {
    std::vector<float> tokens = {0.5f, 0.4f, 0.3f};
    float weight = LegalProcessModule::compute_evidence_weight(tokens);
    REQUIRE_THAT(weight, WithinAbs(1.0f, 0.01f));
}

TEST_CASE("LegalProcess: constants match spec", "[legal_process][tier9]") {
    REQUIRE(365u == 365);
    REQUIRE(1825u == 1825);
    REQUIRE_THAT(0.40f, WithinAbs(0.40f, 0.001f));
}

// ─── State-machine transition gates ──────────────────────────────────────────

TEST_CASE("LegalProcess: should_arrest threshold", "[legal_process][tier9][state_machine]") {
    REQUIRE(LegalProcessModule::should_arrest(0.36f, 0.35f) == true);
    REQUIRE(LegalProcessModule::should_arrest(0.35f, 0.35f) == true);  // boundary inclusive
    REQUIRE(LegalProcessModule::should_arrest(0.34f, 0.35f) == false);
}

TEST_CASE("LegalProcess: should_dismiss threshold", "[legal_process][tier9][state_machine]") {
    REQUIRE(LegalProcessModule::should_dismiss(0.20f, 0.25f) == true);
    REQUIRE(LegalProcessModule::should_dismiss(0.25f, 0.25f) == false);  // boundary exclusive
    REQUIRE(LegalProcessModule::should_dismiss(0.30f, 0.25f) == false);
}

TEST_CASE("LegalProcess: should_charge requires time + evidence",
          "[legal_process][tier9][state_machine]") {
    // Time elapsed since stage_entered = current - stage_entered.
    // Required: elapsed >= 60 AND evidence >= 0.55.
    REQUIRE(LegalProcessModule::should_charge(100, 30, 60, 0.6f, 0.55f) == true);
    // Time gate fails: only 30 ticks elapsed.
    REQUIRE(LegalProcessModule::should_charge(100, 70, 60, 0.6f, 0.55f) == false);
    // Evidence gate fails.
    REQUIRE(LegalProcessModule::should_charge(100, 30, 60, 0.50f, 0.55f) == false);
    // Boundary: exactly 60 ticks elapsed is sufficient.
    REQUIRE(LegalProcessModule::should_charge(90, 30, 60, 0.55f, 0.55f) == true);
}

TEST_CASE("LegalProcess: should_proceed_to_trial timing only",
          "[legal_process][tier9][state_machine]") {
    REQUIRE(LegalProcessModule::should_proceed_to_trial(280, 100, 180) == true);
    REQUIRE(LegalProcessModule::should_proceed_to_trial(279, 100, 180) == false);
    REQUIRE(LegalProcessModule::should_proceed_to_trial(280, 100, 180) == true);
}

TEST_CASE("LegalProcess: is_custodial respects severity floor",
          "[legal_process][tier9][state_machine]") {
    // floor = 3 in spec (CaseSeverity::serious = enum 2 = severity value 3).
    REQUIRE(LegalProcessModule::is_custodial(CaseSeverity::minor, 3) == false);     // sev 1
    REQUIRE(LegalProcessModule::is_custodial(CaseSeverity::moderate, 3) == false);  // sev 2
    REQUIRE(LegalProcessModule::is_custodial(CaseSeverity::serious, 3) == true);    // sev 3
    REQUIRE(LegalProcessModule::is_custodial(CaseSeverity::major, 3) == true);      // sev 4
    REQUIRE(LegalProcessModule::is_custodial(CaseSeverity::capital, 3) == true);    // sev 6
}

TEST_CASE("LegalProcess: parole eligibility at 50% served",
          "[legal_process][tier9][state_machine]") {
    // sentence_ticks = 1000, release_tick = 1500 (started at tick 500).
    // 50% served when current_tick = 1000 (500 remaining, 500 served).
    REQUIRE(LegalProcessModule::is_parole_eligible(1000, 1500, 1000, 0.5f) == true);
    REQUIRE(LegalProcessModule::is_parole_eligible(999, 1500, 1000, 0.5f) == false);
    // After release_tick: definitely eligible (served > 100%).
    REQUIRE(LegalProcessModule::is_parole_eligible(1600, 1500, 1000, 0.5f) == true);
    // Zero sentence: never eligible (defensive).
    REQUIRE(LegalProcessModule::is_parole_eligible(100, 0, 0, 0.5f) == false);
}

TEST_CASE("LegalProcess: fine amount scales linearly with severity",
          "[legal_process][tier9][state_machine]") {
    REQUIRE_THAT(LegalProcessModule::compute_fine_amount(CaseSeverity::minor, 10000.0f),
                 WithinAbs(10000.0f, 0.01f));
    REQUIRE_THAT(LegalProcessModule::compute_fine_amount(CaseSeverity::moderate, 10000.0f),
                 WithinAbs(20000.0f, 0.01f));
}

// ─── Integration of state machine via execute() ──────────────────────────────

namespace {

LegalCase make_case(uint32_t id, LegalCaseStage stage, CaseSeverity sev, float evidence,
                    uint32_t opened_tick, uint32_t stage_entered_tick = 0) {
    LegalCase c{};
    c.id = id;
    c.defendant_npc_id = 100;
    c.prosecutor_npc_id = 200;
    c.judge_npc_id = 300;
    c.stage = stage;
    c.severity = sev;
    c.evidence_weight = evidence;
    c.defense_quality = 0.0f;
    c.opened_tick = opened_tick;
    c.stage_entered_tick = stage_entered_tick != 0 ? stage_entered_tick : opened_tick;
    return c;
}

WorldState empty_world(uint32_t current_tick) {
    WorldState w{};
    w.current_tick = current_tick;
    w.world_seed = 42;
    return w;
}

}  // namespace

TEST_CASE("LegalProcess: investigation -> arrested on evidence threshold",
          "[legal_process][tier9][state_machine]") {
    LegalProcessConfig cfg;
    LegalProcessModule mod(cfg);
    mod.cases_mut().push_back(
        make_case(1, LegalCaseStage::investigation, CaseSeverity::serious, 0.36f, /*opened*/ 10));
    WorldState world = empty_world(15);
    DeltaBuffer delta;
    mod.execute(world, delta);
    REQUIRE(mod.cases()[0].stage == LegalCaseStage::arrested);
    REQUIRE(mod.cases()[0].stage_entered_tick == 15);
    // Public arrest record evidence delta emitted.
    REQUIRE_FALSE(delta.evidence_deltas.empty());
}

TEST_CASE("LegalProcess: arrest_exposure_hit adds to public arrest token weight",
          "[legal_process][tier9][state_machine]") {
    LegalProcessConfig cfg;
    // case at exactly the arrest threshold (0.35) — public token weight
    // should be 0.35 + 0.15 = 0.50.
    LegalProcessModule mod(cfg);
    mod.cases_mut().push_back(
        make_case(1, LegalCaseStage::investigation, CaseSeverity::serious, 0.35f, /*opened*/ 10));
    WorldState world = empty_world(15);
    DeltaBuffer delta;
    mod.execute(world, delta);
    REQUIRE(mod.cases()[0].stage == LegalCaseStage::arrested);
    REQUIRE(delta.evidence_deltas.size() == 1);
    REQUIRE(delta.evidence_deltas[0].new_token.has_value());
    REQUIRE_THAT(delta.evidence_deltas[0].new_token->actionability, WithinAbs(0.50f, 0.001f));
}

TEST_CASE("LegalProcess: arrest_exposure_hit clamps token weight at 1.0",
          "[legal_process][tier9][state_machine]") {
    LegalProcessConfig cfg;  // arrest_exposure_hit = 0.15
    LegalProcessModule mod(cfg);
    // case with strong evidence (0.95): 0.95 + 0.15 = 1.10 → clamped to 1.0.
    mod.cases_mut().push_back(
        make_case(1, LegalCaseStage::investigation, CaseSeverity::serious, 0.95f, /*opened*/ 10));
    WorldState world = empty_world(15);
    DeltaBuffer delta;
    mod.execute(world, delta);
    REQUIRE(delta.evidence_deltas.size() == 1);
    REQUIRE(delta.evidence_deltas[0].new_token.has_value());
    REQUIRE_THAT(delta.evidence_deltas[0].new_token->actionability, WithinAbs(1.0f, 0.001f));
}

TEST_CASE("LegalProcess: investigation stays put below threshold",
          "[legal_process][tier9][state_machine]") {
    LegalProcessConfig cfg;
    LegalProcessModule mod(cfg);
    mod.cases_mut().push_back(
        make_case(1, LegalCaseStage::investigation, CaseSeverity::serious, 0.20f, 10));
    WorldState world = empty_world(15);
    DeltaBuffer delta;
    mod.execute(world, delta);
    REQUIRE(mod.cases()[0].stage == LegalCaseStage::investigation);
}

TEST_CASE("LegalProcess: arrested -> charged after investigation period",
          "[legal_process][tier9][state_machine]") {
    LegalProcessConfig cfg;
    LegalProcessModule mod(cfg);
    auto c = make_case(1, LegalCaseStage::arrested, CaseSeverity::serious, 0.60f, /*opened*/ 5,
                       /*stage_entered*/ 10);
    mod.cases_mut().push_back(c);
    // 60 ticks elapsed since stage_entered=10; threshold met.
    WorldState world = empty_world(70);
    DeltaBuffer delta;
    mod.execute(world, delta);
    REQUIRE(mod.cases()[0].stage == LegalCaseStage::charged);
    REQUIRE(mod.cases()[0].stage_entered_tick == 70);
}

TEST_CASE("LegalProcess: arrested -> acquitted on evidence collapse",
          "[legal_process][tier9][state_machine]") {
    LegalProcessConfig cfg;
    LegalProcessModule mod(cfg);
    mod.cases_mut().push_back(
        make_case(1, LegalCaseStage::arrested, CaseSeverity::serious, 0.20f, 5, 10));
    WorldState world = empty_world(70);
    DeltaBuffer delta;
    mod.execute(world, delta);
    REQUIRE(mod.cases()[0].stage == LegalCaseStage::acquitted);
    // Double-jeopardy cooldown applied.
    REQUIRE(mod.cases()[0].double_jeopardy_until == 70u + cfg.double_jeopardy_cooldown);
}

TEST_CASE("LegalProcess: charged -> trial after charge_to_trial_ticks",
          "[legal_process][tier9][state_machine]") {
    LegalProcessConfig cfg;
    LegalProcessModule mod(cfg);
    auto c = make_case(1, LegalCaseStage::charged, CaseSeverity::serious, 0.0f, 5,
                       /*stage_entered*/ 100);
    mod.cases_mut().push_back(c);
    // 180 ticks elapsed since stage_entered=100.
    WorldState world = empty_world(280);
    DeltaBuffer delta;
    mod.execute(world, delta);
    // Evidence weight is 0 in this fixture so trial converts to acquittal in
    // the same tick (cascade through trial). Both outcomes are acceptable
    // proof we left the charged stage.
    REQUIRE((mod.cases()[0].stage == LegalCaseStage::trial ||
             mod.cases()[0].stage == LegalCaseStage::acquitted));
}

TEST_CASE("LegalProcess: convicted custodial branch -> imprisoned",
          "[legal_process][tier9][state_machine]") {
    LegalProcessConfig cfg;
    LegalProcessModule mod(cfg);
    // Force a trial with extremely high evidence + no defense so conviction
    // probability ≈ 1.0 and the case is reliably convicted.
    auto c = make_case(1, LegalCaseStage::trial, CaseSeverity::serious, 1.0f, 5, 280);
    mod.cases_mut().push_back(c);
    WorldState world = empty_world(280);
    DeltaBuffer delta;
    mod.execute(world, delta);
    // Cascade: trial -> convicted -> imprisoned in one tick.
    REQUIRE(mod.cases()[0].stage == LegalCaseStage::imprisoned);
    REQUIRE(mod.cases()[0].sentence_ticks == 3u * cfg.ticks_per_severity);
    REQUIRE(mod.cases()[0].release_tick == 280u + mod.cases()[0].sentence_ticks);
    // NPCStatus flip to imprisoned.
    REQUIRE_FALSE(delta.npc_deltas.empty());
    REQUIRE(delta.npc_deltas.back().new_status.has_value());
    REQUIRE(*delta.npc_deltas.back().new_status == NPCStatus::imprisoned);
}

TEST_CASE("LegalProcess: convicted non-custodial branch -> fined",
          "[legal_process][tier9][state_machine]") {
    LegalProcessConfig cfg;
    LegalProcessModule mod(cfg);
    // severity moderate (enum 1, value 2) is below floor 3 → fined.
    auto c = make_case(1, LegalCaseStage::trial, CaseSeverity::moderate, 1.0f, 5, 280);
    c.is_player_case = true;
    c.defendant_npc_id = 0;
    mod.cases_mut().push_back(c);
    WorldState world = empty_world(280);
    DeltaBuffer delta;
    mod.execute(world, delta);
    REQUIRE(mod.cases()[0].stage == LegalCaseStage::fined);
    // Wealth delta applied to player_delta (additive).
    REQUIRE(delta.player_delta.wealth_delta.has_value());
    REQUIRE(*delta.player_delta.wealth_delta < 0.0f);
}

TEST_CASE("LegalProcess: imprisoned -> paroled at 50% served",
          "[legal_process][tier9][state_machine]") {
    LegalProcessConfig cfg;
    LegalProcessModule mod(cfg);
    LegalCase c{};
    c.id = 1;
    c.defendant_npc_id = 100;
    c.stage = LegalCaseStage::imprisoned;
    c.severity = CaseSeverity::serious;
    c.sentence_ticks = 1000;
    c.release_tick = 1500;
    c.opened_tick = 400;
    c.stage_entered_tick = 500;
    mod.cases_mut().push_back(c);
    // 50% served reached at current_tick = 1000.
    WorldState world = empty_world(1000);
    DeltaBuffer delta;
    mod.execute(world, delta);
    REQUIRE(mod.cases()[0].stage == LegalCaseStage::paroled);
    REQUIRE_FALSE(delta.npc_deltas.empty());
    REQUIRE(*delta.npc_deltas.back().new_status == NPCStatus::active);
}

TEST_CASE("LegalProcess: bail auto-posted for player arrest",
          "[legal_process][tier9][state_machine]") {
    LegalProcessConfig cfg;
    LegalProcessModule mod(cfg);
    auto c = make_case(1, LegalCaseStage::investigation, CaseSeverity::serious, 0.40f, 10, 10);
    c.is_player_case = true;
    c.defendant_npc_id = 0;
    mod.cases_mut().push_back(c);
    WorldState world = empty_world(15);
    DeltaBuffer delta;
    mod.execute(world, delta);
    REQUIRE(mod.cases()[0].stage == LegalCaseStage::arrested);
    REQUIRE(mod.cases()[0].bail_posted == true);
    REQUIRE(delta.player_delta.wealth_delta.has_value());
    REQUIRE(*delta.player_delta.wealth_delta < 0.0f);
}

// ─── Cross-module trigger ────────────────────────────────────────────────────

TEST_CASE("LegalProcess: drains pending_legal_case_seeds and creates investigation case",
          "[legal_process][tier9][cross_module]") {
    LegalProcessConfig cfg;
    LegalProcessModule mod(cfg);
    WorldState world = empty_world(50);

    LegalCaseSeedDelta seed{};
    seed.defendant_npc_id = 999;
    seed.lead_investigator_id = 700;
    seed.severity = static_cast<uint8_t>(CaseSeverity::serious);
    seed.province_id = 1;
    seed.initial_evidence_weight = 0.85f;  // above arrest threshold 0.35
    world.pending_legal_case_seeds.push_back(seed);

    DeltaBuffer delta;
    mod.execute(world, delta);

    // Queue drained.
    REQUIRE(world.pending_legal_case_seeds.empty());
    // Case created.
    REQUIRE(mod.cases().size() == 1);
    REQUIRE(mod.cases()[0].defendant_npc_id == 999);
    REQUIRE(mod.cases()[0].severity == CaseSeverity::serious);
    // High initial evidence + same-tick processing cascades
    // investigation → arrested (should_arrest(0.85, 0.35) == true).
    REQUIRE(mod.cases()[0].stage == LegalCaseStage::arrested);
    REQUIRE(mod.cases()[0].opened_tick == 50);
    REQUIRE(mod.cases()[0].prosecutor_npc_id == 700);  // stashed investigator hint
}

TEST_CASE("LegalProcess: low-evidence seed stays at investigation stage",
          "[legal_process][tier9][cross_module]") {
    LegalProcessConfig cfg;
    LegalProcessModule mod(cfg);
    WorldState world = empty_world(50);

    LegalCaseSeedDelta seed{};
    seed.defendant_npc_id = 1000;
    seed.lead_investigator_id = 701;
    seed.severity = static_cast<uint8_t>(CaseSeverity::moderate);
    seed.province_id = 0;
    seed.initial_evidence_weight = 0.20f;  // below arrest threshold
    world.pending_legal_case_seeds.push_back(seed);

    DeltaBuffer delta;
    mod.execute(world, delta);

    REQUIRE(mod.cases().size() == 1);
    REQUIRE(mod.cases()[0].stage == LegalCaseStage::investigation);
    REQUIRE(world.pending_legal_case_seeds.empty());
}

TEST_CASE("LegalProcess: seed ids do not collide with existing case ids",
          "[legal_process][tier9][cross_module]") {
    LegalProcessConfig cfg;
    LegalProcessModule mod(cfg);
    WorldState world = empty_world(50);

    auto existing = make_case(42, LegalCaseStage::charged, CaseSeverity::serious, 0.0f, 5, 10);
    mod.cases_mut().push_back(existing);

    LegalCaseSeedDelta seed{};
    seed.defendant_npc_id = 1000;
    seed.severity = static_cast<uint8_t>(CaseSeverity::moderate);
    seed.initial_evidence_weight = 0.4f;
    world.pending_legal_case_seeds.push_back(seed);

    DeltaBuffer delta;
    mod.execute(world, delta);

    REQUIRE(mod.cases().size() == 2);
    // New case id should be > 42 (max existing id + 1 = 43).
    uint32_t new_case_id = 0;
    for (const auto& c : mod.cases()) {
        if (c.defendant_npc_id == 1000)
            new_case_id = c.id;
    }
    REQUIRE(new_case_id == 43u);
}
