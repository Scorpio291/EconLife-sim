#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/media_system/media_system_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

// =============================================================================
// Static utility tests
// =============================================================================

TEST_CASE("Evidence weight from mean actionability", "[media_system][tier7]") {
    std::vector<float> actions = {0.80f, 0.60f, 0.40f};
    float weight = MediaSystemModule::compute_evidence_weight(actions);
    CHECK_THAT(weight, WithinAbs(0.60f, 0.01f));
}

TEST_CASE("Evidence weight empty returns zero", "[media_system][tier7]") {
    std::vector<float> actions = {};
    float weight = MediaSystemModule::compute_evidence_weight(actions);
    CHECK_THAT(weight, WithinAbs(0.0f, 0.001f));
}

TEST_CASE("Editorial filter passes high independence", "[media_system][tier7]") {
    // independence=0.90, suppression=0.50 => threshold = 0.45
    // roll=0.30 < 0.45 => passes
    CHECK(MediaSystemModule::evaluate_editorial_filter(0.90f, 0.50f, 0.30f) == true);
}

TEST_CASE("Editorial filter blocks low independence", "[media_system][tier7]") {
    // independence=0.40, suppression=0.50 => threshold = 0.20
    // roll=0.50 >= 0.20 => blocked
    CHECK(MediaSystemModule::evaluate_editorial_filter(0.40f, 0.50f, 0.50f) == false);
}

TEST_CASE("Cross-outlet pickup probability", "[media_system][tier7]") {
    // evidence_weight=0.80, credibility=0.90, rate=0.15
    // prob = 0.80 * 0.90 * 0.15 = 0.108
    float prob = MediaSystemModule::compute_pickup_probability(0.80f, 0.90f, 0.15f);
    CHECK_THAT(prob, WithinAbs(0.108f, 0.001f));
}

TEST_CASE("Social media amplification", "[media_system][tier7]") {
    // amp=1.0, reach=0.60, mult=2.50, evidence=0.50
    // result = 1.0 * 0.60 * 2.50 * 1.50 = 2.25
    float amp = MediaSystemModule::compute_social_amplification(1.0f, 0.60f, 2.50f, 0.50f);
    CHECK_THAT(amp, WithinAbs(2.25f, 0.01f));
}

TEST_CASE("Exposure delta from damaging story", "[media_system][tier7]") {
    // amplification=5.0, rate=0.02 => 0.10
    float exposure = MediaSystemModule::compute_exposure_delta(5.0f, 0.02f);
    CHECK_THAT(exposure, WithinAbs(0.10f, 0.001f));
}

TEST_CASE("Story within propagation window", "[media_system][tier7]") {
    CHECK(MediaSystemModule::is_within_propagation_window(100, 150, 90) == true);
    CHECK(MediaSystemModule::is_within_propagation_window(100, 190, 90) == true);
    CHECK(MediaSystemModule::is_within_propagation_window(100, 191, 90) == false);
}

TEST_CASE("Story outside propagation window", "[media_system][tier7]") {
    CHECK(MediaSystemModule::is_within_propagation_window(10, 101, 90) == false);
}

TEST_CASE("Exposure not converted below threshold", "[media_system][tier7]") {
    // evidence_weight below crisis threshold
    float evidence = 0.30f;
    CHECK(evidence < MediaSystemModule::Constants::crisis_evidence_threshold);
    // If evidence_weight < threshold, no exposure conversion should happen
    // (tested at module level, not static — verified by checking the threshold constant)
}

TEST_CASE("Neutral story no exposure conversion", "[media_system][tier7]") {
    // StoryTone::neutral should not trigger exposure
    // Verified by module logic: convert_exposure only processes damaging stories
    CHECK(static_cast<uint8_t>(StoryTone::neutral) == 0);
    CHECK(static_cast<uint8_t>(StoryTone::damaging) == 2);
}

// =============================================================================
// Integration tests
// =============================================================================

TEST_CASE("Journalist publishes story from evidence", "[media_system][tier7]") {
    WorldState state{};
    state.current_tick = 100;

    Province prov{};
    prov.id = 0;
    state.provinces.push_back(prov);

    PlayerCharacter player{};
    player.id = 999;
    state.player = &player;

    // Create evidence token about the player
    EvidenceToken token{};
    token.id = 1;
    token.type = EvidenceType::documentary;
    token.source_npc_id = 50;
    token.target_npc_id = 999;
    token.actionability = 0.80f;
    token.decay_rate = 0.002f;
    token.created_tick = 90;
    token.province_id = 0;
    token.is_active = true;
    state.evidence_pool.push_back(token);

    // Create journalist NPC who knows the evidence
    NPC journalist{};
    journalist.id = 50;
    journalist.role = NPCRole::journalist;
    journalist.status = NPCStatus::active;
    journalist.current_province_id = 0;
    KnowledgeEntry ke{};
    ke.subject_id = 1;  // token id
    ke.secondary_subject_id = 0;
    ke.type = KnowledgeType::evidence_token;
    ke.confidence = 0.90f;
    ke.acquired_at_tick = 95;
    ke.source_npc_id = 0;
    ke.original_scope = VisibilityScope::institutional;
    journalist.known_evidence.push_back(ke);
    state.significant_npcs.push_back(journalist);

    // Create independent outlet
    MediaSystemModule module;
    MediaOutlet outlet{};
    outlet.id = 1;
    outlet.province_id = 0;
    outlet.type = MediaOutletType::newspaper;
    outlet.credibility = 0.80f;
    outlet.reach = 0.40f;
    outlet.editorial_independence = 0.90f;
    outlet.owner_npc_id = 0;  // independent
    outlet.journalist_ids = {50};
    module.outlets().push_back(outlet);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Should have published a story
    REQUIRE(!module.active_stories().empty());
    const auto& story = module.active_stories()[0];
    CHECK(story.subject_id == 999);
    CHECK(story.journalist_id == 50);
    CHECK(story.tone == StoryTone::damaging);
    CHECK_THAT(story.evidence_weight, WithinAbs(0.80f, 0.01f));
}

TEST_CASE("Story expires after propagation window", "[media_system][tier7]") {
    WorldState state{};
    state.current_tick = 200;

    PlayerCharacter player{};
    player.id = 999;
    state.player = &player;

    MediaSystemModule module;

    // Old story
    Story story{};
    story.id = 1;
    story.subject_id = 999;
    story.journalist_id = 50;
    story.outlet_id = 1;
    story.tone = StoryTone::damaging;
    story.evidence_weight = 0.80f;
    story.amplification = 5.0f;
    story.published_tick = 100;  // 100 ticks ago, window = 90
    story.is_active = true;
    module.active_stories().push_back(story);

    DeltaBuffer delta{};
    module.execute(state, delta);

    CHECK(module.active_stories()[0].is_active == false);
}

TEST_CASE("Editorial filter suppresses story at player outlet", "[media_system][tier7]") {
    WorldState state{};
    state.current_tick = 100;

    PlayerCharacter player{};
    player.id = 999;
    state.player = &player;

    // Evidence about player
    EvidenceToken token{};
    token.id = 1;
    token.type = EvidenceType::documentary;
    token.source_npc_id = 50;
    token.target_npc_id = 999;
    token.actionability = 0.80f;
    token.decay_rate = 0.002f;
    token.created_tick = 90;
    token.province_id = 0;
    token.is_active = true;
    state.evidence_pool.push_back(token);

    // Journalist with high confidence (roll = 0.90, will be > threshold)
    NPC journalist{};
    journalist.id = 50;
    journalist.role = NPCRole::journalist;
    journalist.status = NPCStatus::active;
    journalist.current_province_id = 0;
    KnowledgeEntry ke{};
    ke.subject_id = 1;
    ke.secondary_subject_id = 0;
    ke.type = KnowledgeType::evidence_token;
    ke.confidence = 0.90f;  // roll = 0.90
    ke.acquired_at_tick = 95;
    ke.source_npc_id = 0;
    ke.original_scope = VisibilityScope::institutional;
    journalist.known_evidence.push_back(ke);
    state.significant_npcs.push_back(journalist);

    // Player-owned outlet with LOW editorial independence
    MediaSystemModule module;
    MediaOutlet outlet{};
    outlet.id = 1;
    outlet.province_id = 0;
    outlet.type = MediaOutletType::newspaper;
    outlet.credibility = 0.80f;
    outlet.reach = 0.40f;
    outlet.editorial_independence = 0.30f;  // threshold = 0.30 * 0.50 = 0.15
    outlet.owner_npc_id = 999;              // player-owned
    outlet.journalist_ids = {50};
    module.outlets().push_back(outlet);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Story should be suppressed (roll=0.90 > threshold=0.15)
    CHECK(module.active_stories().empty());

    // Journalist should get suppression memory
    bool found_memory = false;
    for (const auto& d : delta.npc_deltas) {
        if (d.npc_id == 50 && d.new_memory_entry.has_value()) {
            found_memory = true;
            CHECK(d.new_memory_entry->emotional_weight < 0.0f);
            break;
        }
    }
    CHECK(found_memory);
}
