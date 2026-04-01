#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <vector>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/evidence/evidence_module.h"

using Catch::Matchers::WithinAbs;
using namespace econlife;

// ---------------------------------------------------------------------------
// Static utility tests
// ---------------------------------------------------------------------------

TEST_CASE("test_compute_decay_amount_credible", "[evidence][tier6]") {
    // Credible holder: decay = base_rate * 1.0 * batch_interval
    float decay = EvidenceModule::compute_decay_amount(0.002f, true, 5.0f, 7);
    REQUIRE_THAT(decay, WithinAbs(0.014f, 0.0001f));
}

TEST_CASE("test_compute_decay_amount_discredited", "[evidence][tier6]") {
    // Discredited holder: decay = base_rate * 5.0 * batch_interval
    float decay = EvidenceModule::compute_decay_amount(0.002f, false, 5.0f, 7);
    REQUIRE_THAT(decay, WithinAbs(0.070f, 0.0001f));
}

TEST_CASE("test_apply_actionability_decay_basic", "[evidence][tier6]") {
    float result = EvidenceModule::apply_actionability_decay(1.0f, 0.014f, 0.10f);
    REQUIRE_THAT(result, WithinAbs(0.986f, 0.001f));
}

TEST_CASE("test_apply_actionability_decay_floor_clamp", "[evidence][tier6]") {
    // Decay would push below floor
    float result = EvidenceModule::apply_actionability_decay(0.12f, 0.10f, 0.10f);
    REQUIRE_THAT(result, WithinAbs(0.10f, 0.001f));
}

TEST_CASE("test_actionability_floor_never_breached", "[evidence][tier6]") {
    // Simulate many decay batches
    float actionability = 1.0f;
    float floor = 0.10f;
    float decay_per_batch = 0.014f;

    for (int i = 0; i < 200; ++i) {
        actionability =
            EvidenceModule::apply_actionability_decay(actionability, decay_per_batch, floor);
    }
    REQUIRE(actionability >= floor);
    REQUIRE_THAT(actionability, WithinAbs(floor, 0.001f));
}

TEST_CASE("test_evaluate_holder_credibility", "[evidence][tier6]") {
    // Above threshold
    REQUIRE(EvidenceModule::evaluate_holder_credibility(0.50f, 0.30f) == true);
    // At threshold
    REQUIRE(EvidenceModule::evaluate_holder_credibility(0.30f, 0.30f) == true);
    // Below threshold
    REQUIRE(EvidenceModule::evaluate_holder_credibility(0.20f, 0.30f) == false);
}

TEST_CASE("test_normalize_trust_to_factor", "[evidence][tier6]") {
    constexpr float kMin = EvidenceConfig{}.trust_factor_min;
    constexpr float kMax = EvidenceConfig{}.trust_factor_max;
    // Negative trust -> minimum factor
    REQUIRE_THAT(EvidenceModule::normalize_trust_to_factor(-0.5f, kMin, kMax), WithinAbs(0.1f, 0.001f));
    // Zero trust -> minimum factor
    REQUIRE_THAT(EvidenceModule::normalize_trust_to_factor(0.0f, kMin, kMax), WithinAbs(0.1f, 0.001f));
    // Full trust -> maximum factor
    REQUIRE_THAT(EvidenceModule::normalize_trust_to_factor(1.0f, kMin, kMax), WithinAbs(1.0f, 0.001f));
    // Half trust -> mid factor
    float mid = EvidenceModule::normalize_trust_to_factor(0.5f, kMin, kMax);
    REQUIRE(mid > 0.1f);
    REQUIRE(mid < 1.0f);
    // 0.1 + 0.5 * 0.9 = 0.55
    REQUIRE_THAT(mid, WithinAbs(0.55f, 0.01f));
}

TEST_CASE("test_can_share_with_player", "[evidence][tier6]") {
    REQUIRE(EvidenceModule::can_share_with_player(0.50f, 0.45f) == true);
    REQUIRE(EvidenceModule::can_share_with_player(0.45f, 0.45f) == true);
    REQUIRE(EvidenceModule::can_share_with_player(0.30f, 0.45f) == false);
}

TEST_CASE("test_evidence_propagation_confidence_scaling", "[evidence][tier6]") {
    // sharer_confidence=0.8, trust=0.5 -> factor=0.55 -> received=0.44
    float received = EvidenceModule::compute_propagation_confidence(
        0.8f, 0.5f, EvidenceConfig{}.trust_factor_min, EvidenceConfig{}.trust_factor_max);
    REQUIRE_THAT(received, WithinAbs(0.44f, 0.01f));
}

TEST_CASE("test_share_trust_threshold_blocks_low_trust", "[evidence][tier6]") {
    // Trust = 0.30 < threshold 0.45 -> cannot share
    REQUIRE(EvidenceModule::can_share_with_player(0.30f, 0.45f) == false);
}

// ---------------------------------------------------------------------------
// Decay over time tests
// ---------------------------------------------------------------------------

TEST_CASE("test_actionability_decay_credible_holder", "[evidence][tier6]") {
    // Credible holder: decay 0.014 per batch (every 7 ticks)
    // After 100 batches (700 ticks): decay = 100 * 0.014 = 1.4 total
    // Starting at 1.0, clamped at 0.10: ~0.10 after enough batches
    float a = 1.0f;
    for (int batch = 0; batch < 50; ++batch) {
        a = EvidenceModule::apply_actionability_decay(a, 0.014f, 0.10f);
    }
    // After 50 batches: 1.0 - 50*0.014 = 1.0 - 0.70 = 0.30
    REQUIRE_THAT(a, WithinAbs(0.30f, 0.01f));
}

TEST_CASE("test_actionability_decay_discredited_holder", "[evidence][tier6]") {
    // Discredited: decay 0.070 per batch
    // After 10 batches: 1.0 - 10*0.070 = 0.30
    float a = 1.0f;
    for (int batch = 0; batch < 10; ++batch) {
        a = EvidenceModule::apply_actionability_decay(a, 0.070f, 0.10f);
    }
    REQUIRE_THAT(a, WithinAbs(0.30f, 0.01f));
}

// ---------------------------------------------------------------------------
// Integration tests
// ---------------------------------------------------------------------------

TEST_CASE("test_criminal_business_generates_evidence", "[evidence][tier6]") {
    EvidenceModule module;

    WorldState state{};
    state.current_tick = 100;
    state.world_seed = 42;

    // Create one province
    Province p{};
    p.id = 0;
    state.provinces.push_back(p);

    // Create a criminal business
    NPCBusiness biz{};
    biz.id = 1;
    biz.owner_id = 10;
    biz.province_id = 0;
    biz.criminal_sector = true;
    biz.regulatory_violation_severity = 0.0f;
    state.npc_businesses.push_back(biz);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Should have created one evidence token
    bool found_new_token = false;
    for (const auto& ed : delta.evidence_deltas) {
        if (ed.new_token.has_value()) {
            const auto& token = ed.new_token.value();
            REQUIRE(token.type == EvidenceType::physical);
            REQUIRE(token.source_npc_id == 10);
            REQUIRE(token.target_npc_id == 10);
            REQUIRE(token.province_id == 0);
            REQUIRE(token.is_active == true);
            REQUIRE_THAT(
                token.actionability,
                WithinAbs(EvidenceConfig{}.criminal_evidence_actionability, 0.01f));
            found_new_token = true;
        }
    }
    REQUIRE(found_new_token);
}

TEST_CASE("test_player_aware_proxy_on_direct_creation", "[evidence][tier6]") {
    // Verify that tokens from criminal businesses have is_active=true
    // (in V1 bootstrap, player_aware is approximated via is_active flag)
    EvidenceModule module;

    WorldState state{};
    state.current_tick = 100;
    state.world_seed = 42;

    Province p{};
    p.id = 0;
    state.provinces.push_back(p);

    NPCBusiness biz{};
    biz.id = 1;
    biz.owner_id = 10;
    biz.province_id = 0;
    biz.criminal_sector = true;
    state.npc_businesses.push_back(biz);

    DeltaBuffer delta{};
    module.execute(state, delta);

    for (const auto& ed : delta.evidence_deltas) {
        if (ed.new_token.has_value()) {
            REQUIRE(ed.new_token.value().is_active == true);
        }
    }
}
