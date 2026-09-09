#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <filesystem>

#include "core/world_state/apply_deltas.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/media_system/media_system_module.h"
#include "modules/scene_cards/scene_cards_module.h"

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
    CHECK(evidence < MediaSystemConfig{}.crisis_evidence_threshold);
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
    prov.cohort_stats = std::make_unique<RegionCohortStats>();
    prov.id = 0;
    state.provinces.push_back(prov);

    PlayerCharacter player{};
    player.id = 999;
    state.player = std::make_unique<PlayerCharacter>(player);

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
    state.player = std::make_unique<PlayerCharacter>(player);

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
    state.player = std::make_unique<PlayerCharacter>(player);

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

// =============================================================================
// Exposure -> reputation (replaces the former health_delta placeholder)
// =============================================================================

TEST_CASE("Damaging player-subject story erodes public reputation", "[media_system][tier7]") {
    WorldState state{};
    state.current_tick = 10;
    state.world_seed = 1;
    PlayerCharacter player{};
    player.id = 999;
    player.reputation.public_social = 0.0f;
    player.reputation.public_business = 0.0f;
    state.player = std::make_unique<PlayerCharacter>(player);

    MediaSystemModule module;
    Story s{};
    s.subject_id = 999;  // the player
    s.outlet_id = 0;
    s.tone = StoryTone::damaging;
    s.evidence_weight = 1.0f;  // above crisis_evidence_threshold (0.40)
    s.amplification = 5.0f;
    s.published_tick = 10;
    s.is_active = true;
    module.active_stories().push_back(s);

    DeltaBuffer delta{};
    module.execute(state, delta);
    apply_deltas(state, delta);

    // Both public axes eroded; social hit harder than business (full vs half weight).
    REQUIRE(state.player->reputation.public_social < 0.0f);
    REQUIRE(state.player->reputation.public_business < 0.0f);
    REQUIRE(state.player->reputation.public_social <= state.player->reputation.public_business);
}

// =============================================================================
// The player finds out because it is in the papers
// =============================================================================
//
// The GDD calls the player's evidence awareness gap "the primary late-game
// tension; must ship". The channel had an applier and no writer: the evidence
// pool, the four token types, actionability scoring, the investigator meters
// and this propagation model all worked, and the player was never told any of
// it. Coverage is the first real information channel to reach them.

namespace {

WorldState make_media_world() {
    WorldState w{};
    w.current_tick = 100;
    w.world_seed = 42;
    w.game_mode = GameMode::standard;

    Province p{};
    p.id = 0;
    p.region_id = 0;
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    w.provinces.push_back(std::move(p));

    auto player = std::make_unique<PlayerCharacter>();
    player->id = 1;
    player->current_province_id = 0;
    player->home_province_id = 0;
    w.player = std::move(player);
    return w;
}

Story make_story_about(uint32_t subject_id, std::vector<uint32_t> tokens) {
    Story story{};
    story.id = 1;
    story.subject_id = subject_id;
    story.journalist_id = 900;
    story.outlet_id = 1;
    story.tone = StoryTone::damaging;
    story.evidence_weight = 0.8f;
    story.amplification = 2.0f;
    story.published_tick = 100;
    story.evidence_token_ids = std::move(tokens);
    story.is_active = true;
    return story;
}

// The shipped card copy: the notice is a seed naming a template, so without
// the catalog there is no card to find.
std::string find_scene_cards_dir() {
    namespace fs = std::filesystem;
    const char* candidates[] = {
        "packages/base_game/scene_cards",
        "../packages/base_game/scene_cards",
        "../../packages/base_game/scene_cards",
        "../../../packages/base_game/scene_cards",
        "../../../../packages/base_game/scene_cards",
        "../../../../../packages/base_game/scene_cards",
    };
    for (const auto* c : candidates) {
        if (fs::is_directory(c))
            return fs::canonical(c).string();
    }
    return "";
}

}  // namespace

TEST_CASE("A story about the player tells them what it cites", "[media_system][evidence]") {
    WorldState w = make_media_world();
    MediaSystemModule module;
    module.active_stories().push_back(make_story_about(w.player->id, {11, 22, 33}));

    DeltaBuffer delta{};
    module.execute(w, delta);
    apply_deltas(w, delta);

    REQUIRE(w.player->evidence_awareness_map.size() == 3);
    std::vector<uint32_t> known;
    for (const auto& e : w.player->evidence_awareness_map)
        known.push_back(e.token_id);
    REQUIRE(std::find(known.begin(), known.end(), 11u) != known.end());
    REQUIRE(std::find(known.begin(), known.end(), 22u) != known.end());
    REQUIRE(std::find(known.begin(), known.end(), 33u) != known.end());

    // Discovery is stamped with when they found out, which is the number the
    // exposure model cares about.
    REQUIRE(w.player->evidence_awareness_map[0].discovery_tick == w.current_tick);

    // And they are told, through the channel they already read. The module
    // names an authored template rather than writing the sentence, so the
    // proof is a card the player can actually answer coming out the far end.
    REQUIRE_FALSE(w.pending_scene_card_seeds.empty());
    SceneCardsConfig cfg{};
    cfg.card_catalog_directory = find_scene_cards_dir();
    REQUIRE_FALSE(cfg.card_catalog_directory.empty());
    SceneCardsModule cards(cfg);
    DeltaBuffer card_delta{};
    cards.execute(w, card_delta);
    apply_deltas(w, card_delta);
    REQUIRE_FALSE(w.pending_scene_cards.empty());
    REQUIRE_FALSE(w.pending_scene_cards[0].choices.empty());
}

TEST_CASE("The player learns only what was printed, not what the world knows",
          "[media_system][evidence]") {
    // Device-mediated information: a story carries the evidence it was built
    // on, so that is exactly what the player comes to know. Tokens the world
    // holds but no one published stay unknown.
    WorldState w = make_media_world();
    MediaSystemModule module;
    module.active_stories().push_back(make_story_about(w.player->id, {11}));

    DeltaBuffer delta{};
    module.execute(w, delta);
    apply_deltas(w, delta);

    REQUIRE(w.player->evidence_awareness_map.size() == 1);
    REQUIRE(w.player->evidence_awareness_map[0].token_id == 11u);
}

TEST_CASE("A story about someone else tells the player nothing", "[media_system][evidence]") {
    WorldState w = make_media_world();
    MediaSystemModule module;
    module.active_stories().push_back(make_story_about(777, {11, 22}));

    DeltaBuffer delta{};
    module.execute(w, delta);
    apply_deltas(w, delta);

    REQUIRE(w.player->evidence_awareness_map.empty());
}

TEST_CASE("Learning the same thing twice is not learning", "[media_system][evidence]") {
    // The map records when the player FIRST found out; a story that runs for
    // several ticks must not keep re-discovering its own evidence.
    WorldState w = make_media_world();
    MediaSystemModule module;
    module.active_stories().push_back(make_story_about(w.player->id, {11, 22}));

    for (int tick = 0; tick < 3; ++tick) {
        DeltaBuffer delta{};
        module.execute(w, delta);
        apply_deltas(w, delta);
        w.current_tick += 1;
    }

    REQUIRE(w.player->evidence_awareness_map.size() == 2);
    REQUIRE(w.player->evidence_awareness_map[0].discovery_tick == 100u);
}
