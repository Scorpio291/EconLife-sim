#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"
#include "modules/political_cycle/political_cycle_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

namespace {
// Minimal world: one nation (given government type) + one province with a
// single working_urban_mid cohort (so demographic weights are well-defined).
WorldState make_political_world(uint32_t tick, GovernmentType gov) {
    WorldState w{};
    w.current_tick = tick;
    w.world_seed = 1;
    Nation n{};
    n.id = 0;
    n.government_type = gov;
    w.nations.push_back(n);
    Province p{};
    p.id = 0;
    p.nation_id = 0;
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    PopulationCohort c;
    c.group = DemographicGroup::working_urban_mid;
    c.size = 1000;
    p.cohort_stats->cohorts[c.group] = c;
    w.provinces.push_back(std::move(p));
    return w;
}
NPC make_politician(uint32_t id, uint32_t province) {
    NPC npc{};
    npc.id = id;
    npc.role = NPCRole::politician;
    npc.status = NPCStatus::active;
    npc.current_province_id = province;
    return npc;
}
}  // namespace

TEST_CASE("PoliticalCycle: raw vote share weighted calculation", "[political_cycle][tier10]") {
    std::unordered_map<std::string, float> approval = {{"working_class", 0.7f},
                                                       {"corporate", 0.3f}};
    std::vector<DemographicWeight> demographics = {{"working_class", 0.6f, 1.0f},
                                                   {"corporate", 0.4f, 1.0f}};
    // (0.7*0.6 + 0.3*0.4) / (0.6 + 0.4) = (0.42 + 0.12) / 1.0 = 0.54
    float share = PoliticalCycleModule::compute_raw_vote_share(approval, demographics);
    REQUIRE_THAT(share, WithinAbs(0.54f, 0.01f));
}

TEST_CASE("PoliticalCycle: vote share default approval for missing demographic",
          "[political_cycle][tier10]") {
    std::unordered_map<std::string, float> approval = {{"working_class", 0.8f}};
    std::vector<DemographicWeight> demographics = {{"working_class", 0.5f, 1.0f},
                                                   {"corporate", 0.5f, 1.0f}};
    // working_class: 0.8*0.5 = 0.40; corporate uses default 0.5: 0.5*0.5 = 0.25
    // total = 0.65 / 1.0 = 0.65
    float share = PoliticalCycleModule::compute_raw_vote_share(approval, demographics);
    REQUIRE_THAT(share, WithinAbs(0.65f, 0.01f));
}

TEST_CASE("PoliticalCycle: zero weight demographics return 0.5", "[political_cycle][tier10]") {
    std::unordered_map<std::string, float> approval;
    std::vector<DemographicWeight> demographics = {{"empty", 0.0f, 0.0f}};
    float share = PoliticalCycleModule::compute_raw_vote_share(approval, demographics);
    REQUIRE_THAT(share, WithinAbs(0.5f, 0.01f));
}

TEST_CASE("PoliticalCycle: resource modifier diminishing returns", "[political_cycle][tier10]") {
    PoliticalCycleConfig cfg{};
    float low = PoliticalCycleModule::compute_resource_modifier(0.5f, cfg.resource_scale,
                                                                cfg.resource_max_effect);
    float high = PoliticalCycleModule::compute_resource_modifier(5.0f, cfg.resource_scale,
                                                                 cfg.resource_max_effect);

    // Both within bounds
    REQUIRE(low >= -cfg.resource_max_effect);
    REQUIRE(low <= cfg.resource_max_effect);
    REQUIRE(high >= -cfg.resource_max_effect);
    REQUIRE(high <= cfg.resource_max_effect);

    // Diminishing returns: 5.0 produces less than 10x the effect of 0.5
    REQUIRE(high < low * 10.0f);
    REQUIRE(high > low);
}

TEST_CASE("PoliticalCycle: event modifiers capped at 20%", "[political_cycle][tier10]") {
    PoliticalCycleConfig cfg{};
    std::vector<float> mods = {0.10f, 0.10f, 0.10f, 0.10f, 0.10f};
    float total = PoliticalCycleModule::compute_event_modifier_total(mods, cfg.event_modifier_cap);
    REQUIRE_THAT(total, WithinAbs(0.20f, 0.01f));
}

TEST_CASE("PoliticalCycle: negative event modifiers capped", "[political_cycle][tier10]") {
    PoliticalCycleConfig cfg{};
    std::vector<float> mods = {-0.15f, -0.15f};
    float total = PoliticalCycleModule::compute_event_modifier_total(mods, cfg.event_modifier_cap);
    REQUIRE_THAT(total, WithinAbs(-0.20f, 0.01f));
}

TEST_CASE("PoliticalCycle: final vote share clamped to [0,1]", "[political_cycle][tier10]") {
    float high = PoliticalCycleModule::compute_final_vote_share(0.90f, 0.15f, 0.20f);
    REQUIRE_THAT(high, WithinAbs(1.0f, 0.01f));

    float low = PoliticalCycleModule::compute_final_vote_share(0.05f, -0.15f, -0.20f);
    REQUIRE_THAT(low, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("PoliticalCycle: legislator support computation", "[political_cycle][tier10]") {
    float support = PoliticalCycleModule::compute_legislator_support(0.3f, 0.1f, 0.15f);
    REQUIRE_THAT(support, WithinAbs(0.55f, 0.01f));
}

TEST_CASE("PoliticalCycle: legislative vote resolution passes", "[political_cycle][tier10]") {
    REQUIRE(PoliticalCycleModule::compute_vote_passed(60.0f, 40.0f, 0.50f) == true);
    REQUIRE(PoliticalCycleModule::compute_vote_passed(50.0f, 50.0f, 0.50f) == false);
    REQUIRE(PoliticalCycleModule::compute_vote_passed(0.0f, 0.0f, 0.50f) == false);
}

TEST_CASE("PoliticalCycle: constants match spec", "[political_cycle][tier10]") {
    PoliticalCycleConfig cfg{};
    REQUIRE_THAT(cfg.support_threshold, WithinAbs(0.55f, 0.001f));
    REQUIRE_THAT(cfg.oppose_threshold, WithinAbs(0.35f, 0.001f));
    REQUIRE_THAT(cfg.majority_threshold, WithinAbs(0.50f, 0.001f));
    REQUIRE_THAT(cfg.resource_scale, WithinAbs(2.0f, 0.001f));
    REQUIRE_THAT(cfg.resource_max_effect, WithinAbs(0.15f, 0.001f));
}

TEST_CASE("PoliticalCycle: resource modifier at zero deployment", "[political_cycle][tier10]") {
    PoliticalCycleConfig cfg{};
    float mod = PoliticalCycleModule::compute_resource_modifier(0.0f, cfg.resource_scale,
                                                                cfg.resource_max_effect);
    REQUIRE_THAT(mod, WithinAbs(0.0f, 0.001f));
}

// ===========================================================================
// Election pipeline: office seeding, government gating, coalition-weighted
// resolution, and incumbent turnover (built out from the former stub).
// ===========================================================================

TEST_CASE("PoliticalCycle: seeds one governor office per province", "[political_cycle][tier10]") {
    auto world = make_political_world(/*tick=*/1, GovernmentType::Democracy);
    world.significant_npcs.push_back(make_politician(50, 0));

    PoliticalCycleModule module;
    DeltaBuffer delta{};
    module.execute(world, delta);

    REQUIRE(module.state().offices.size() == 1);
    const auto& office = module.state().offices[0];
    REQUIRE(office.province_id == 0);
    REQUIRE(office.current_holder_id == 50);  // the politician
    REQUIRE(office.office_type == PoliticalOfficeType::governor);
}

TEST_CASE("PoliticalCycle: incumbent re-elected on high approval", "[political_cycle][tier10]") {
    auto world = make_political_world(/*tick=*/100, GovernmentType::Democracy);
    world.significant_npcs.push_back(make_politician(50, 0));

    PoliticalCycleModule module;
    // Pre-seed the office so form_offices no-ops and we control the scenario.
    PoliticalOffice office{};
    office.id = 1;
    office.province_id = 0;
    office.current_holder_id = 50;
    office.election_due_tick = 100;  // due now
    office.win_threshold = 0.5f;
    office.approval_by_demographic["working_urban_mid"] = 0.80f;  // strong approval
    module.state().offices.push_back(office);

    DeltaBuffer delta{};
    module.execute(world, delta);

    REQUIRE(module.state().offices[0].current_holder_id == 50);            // retained
    REQUIRE(module.state().offices[0].election_due_tick == 100u + 1460u);  // rescheduled
}

TEST_CASE("PoliticalCycle: incumbent loses, challenger installed", "[political_cycle][tier10]") {
    auto world = make_political_world(/*tick=*/100, GovernmentType::Democracy);
    world.significant_npcs.push_back(make_politician(50, 0));  // incumbent
    world.significant_npcs.push_back(make_politician(60, 0));  // challenger

    PoliticalCycleModule module;
    PoliticalOffice office{};
    office.id = 1;
    office.province_id = 0;
    office.current_holder_id = 50;
    office.election_due_tick = 100;
    office.win_threshold = 0.5f;
    office.approval_by_demographic["working_urban_mid"] = 0.20f;  // weak -> loses
    module.state().offices.push_back(office);

    DeltaBuffer delta{};
    module.execute(world, delta);

    REQUIRE(module.state().offices[0].current_holder_id == 60);  // challenger installed
}

TEST_CASE("PoliticalCycle: autocracy skips election (holder unchanged)",
          "[political_cycle][tier10]") {
    auto world = make_political_world(/*tick=*/100, GovernmentType::Autocracy);
    world.significant_npcs.push_back(make_politician(50, 0));
    world.significant_npcs.push_back(make_politician(60, 0));

    PoliticalCycleModule module;
    PoliticalOffice office{};
    office.id = 1;
    office.province_id = 0;
    office.current_holder_id = 50;
    office.election_due_tick = 100;
    office.approval_by_demographic["working_urban_mid"] = 0.20f;  // would lose if held
    module.state().offices.push_back(office);

    DeltaBuffer delta{};
    module.execute(world, delta);

    REQUIRE(module.state().offices[0].current_holder_id == 50);  // no turnover under autocracy
    REQUIRE(module.state().offices[0].election_due_tick == 100u + 1460u);
}

TEST_CASE("PoliticalCycle: endorsement boosts a demographic's approval",
          "[political_cycle][tier10]") {
    std::unordered_map<std::string, float> approval = {{"working_urban_mid", 0.50f}};
    std::vector<Endorsement> endorsements;
    Endorsement e;
    e.primary_demographic = "working_urban_mid";
    e.approval_bonus = 0.08f;
    endorsements.push_back(e);

    PoliticalCycleModule::apply_endorsement_bonuses(approval, endorsements);
    REQUIRE_THAT(approval["working_urban_mid"], WithinAbs(0.58f, 1e-4f));

    // Clamps to 1.0.
    approval["working_urban_mid"] = 0.97f;
    PoliticalCycleModule::apply_endorsement_bonuses(approval, endorsements);
    REQUIRE_THAT(approval["working_urban_mid"], WithinAbs(1.0f, 1e-4f));
}

TEST_CASE("PoliticalCycle: campaign auto-activates within the lead-time window",
          "[political_cycle][tier10]") {
    // Election due 50 ticks out; lead time is 90, so a campaign should open.
    auto world = make_political_world(/*tick=*/100, GovernmentType::Democracy);
    world.significant_npcs.push_back(make_politician(50, 0));

    PoliticalCycleModule module;
    PoliticalOffice office{};
    office.id = 1;
    office.province_id = 0;
    office.current_holder_id = 50;
    office.election_due_tick = 150;  // 50 ticks away (< 90 lead time)
    office.approval_by_demographic["working_urban_mid"] = 0.6f;
    module.state().offices.push_back(office);

    DeltaBuffer delta{};
    module.execute(world, delta);

    REQUIRE(module.state().campaigns.size() == 1);
    const auto& camp = module.state().campaigns[0];
    REQUIRE(camp.office_id == 1);
    REQUIRE(camp.active_candidate_id == 50);
    REQUIRE(camp.election_tick == 150);

    // Idempotent: a second tick does not open a duplicate campaign.
    world.current_tick = 101;
    DeltaBuffer d2{};
    module.execute(world, d2);
    REQUIRE(module.state().campaigns.size() == 1);
}

TEST_CASE("PoliticalCycle: no campaign opens outside the lead-time window",
          "[political_cycle][tier10]") {
    auto world = make_political_world(/*tick=*/100, GovernmentType::Democracy);
    world.significant_npcs.push_back(make_politician(50, 0));

    PoliticalCycleModule module;
    PoliticalOffice office{};
    office.id = 1;
    office.province_id = 0;
    office.current_holder_id = 50;
    office.election_due_tick = 1000;  // far away (> 90 lead time)
    module.state().offices.push_back(office);

    DeltaBuffer delta{};
    module.execute(world, delta);
    REQUIRE(module.state().campaigns.empty());
}

// ===========================================================================
// Legislative pipeline: stage progression, NPC-legislator polling, resolution.
// ===========================================================================

namespace {
NPC make_legislator(uint32_t id, float ideological_weight) {
    NPC npc{};
    npc.id = id;
    npc.role = NPCRole::politician;
    npc.status = NPCStatus::active;
    npc.current_province_id = 0;
    npc.motivations.weights.fill(0.0f);
    npc.motivations.weights[static_cast<size_t>(OutcomeType::ideological)] = ideological_weight;
    return npc;
}
}  // namespace

TEST_CASE("PoliticalCycle: proposal advances through committee to floor",
          "[political_cycle][tier10]") {
    auto world = make_political_world(/*tick=*/10, GovernmentType::Democracy);
    PoliticalCycleModule module;
    LegislativeProposal prop{};
    prop.id = 1;
    prop.status = LegislativeProposalStatus::drafted;
    prop.sponsor_id = 0;  // no sponsor check
    prop.vote_tick = 1000;
    module.state().proposals.push_back(prop);

    DeltaBuffer d{};
    module.execute(world, d);  // drafted -> in_committee
    REQUIRE(module.state().proposals[0].status == LegislativeProposalStatus::in_committee);
    module.execute(world, d);  // in_committee -> floor_debate
    REQUIRE(module.state().proposals[0].status == LegislativeProposalStatus::floor_debate);
    module.execute(world, d);  // floor_debate, but vote_tick far away -> stays
    REQUIRE(module.state().proposals[0].status == LegislativeProposalStatus::floor_debate);
}

TEST_CASE("PoliticalCycle: floor vote resolves via NPC legislator poll",
          "[political_cycle][tier10]") {
    auto world = make_political_world(/*tick=*/50, GovernmentType::Democracy);
    // 3 ideologically-decisive (support) legislators, 1 opposed.
    world.significant_npcs.push_back(make_legislator(10, 0.70f));  // for
    world.significant_npcs.push_back(make_legislator(11, 0.70f));  // for
    world.significant_npcs.push_back(make_legislator(12, 0.70f));  // for
    world.significant_npcs.push_back(make_legislator(13, 0.20f));  // against

    PoliticalCycleModule module;
    LegislativeProposal prop{};
    prop.id = 7;
    prop.status = LegislativeProposalStatus::floor_debate;
    prop.sponsor_id = 0;
    prop.vote_tick = 50;  // due now
    module.state().proposals.push_back(prop);

    DeltaBuffer d{};
    module.execute(world, d);
    // 3 for vs 1 against -> 0.75 > 0.50 -> enacted, consequence queued.
    REQUIRE(module.state().proposals[0].status == LegislativeProposalStatus::enacted);
    bool consequence = false;
    for (const auto& c : d.consequence_deltas)
        if (c.new_entry_id.has_value() && *c.new_entry_id == 7u)
            consequence = true;
    REQUIRE(consequence);
}

TEST_CASE("PoliticalCycle: proposal with dead sponsor fails", "[political_cycle][tier10]") {
    auto world = make_political_world(/*tick=*/50, GovernmentType::Democracy);
    NPC sponsor{};
    sponsor.id = 99;
    sponsor.role = NPCRole::politician;
    sponsor.status = NPCStatus::dead;
    world.significant_npcs.push_back(sponsor);

    PoliticalCycleModule module;
    LegislativeProposal prop{};
    prop.id = 3;
    prop.status = LegislativeProposalStatus::in_committee;
    prop.sponsor_id = 99;
    prop.vote_tick = 50;
    module.state().proposals.push_back(prop);

    DeltaBuffer d{};
    module.execute(world, d);
    REQUIRE(module.state().proposals[0].status == LegislativeProposalStatus::failed);
}
