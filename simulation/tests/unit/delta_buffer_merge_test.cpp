// Coverage tests for DeltaBuffer::merge_from and PlayerDelta::merge_from.
//
// The "covers every field" test is a tripwire: when a developer adds a
// new field to DeltaBuffer or PlayerDelta and forgets to update
// merge_from in delta_buffer.cpp, this test fails. That is the whole
// point — the orchestrator's province merge silently dropped fields
// before merge_from existed (npc_business::dissolved_businesses was
// the live instance that prompted the refactor).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/delta_buffer.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

namespace {

// Populate every vector field with a single distinguishable entry and
// every PlayerDelta optional with a value. If you add a new field to
// DeltaBuffer or PlayerDelta, extend this helper AND the corresponding
// REQUIRE in test_delta_buffer_merge_covers_every_field.
void populate_every_field(DeltaBuffer& db, uint32_t tag) {
    {
        NPCDelta d{};
        d.npc_id = tag;
        db.npc_deltas.push_back(d);
    }

    db.player_delta.health_delta = static_cast<float>(tag) * 0.1f;
    db.player_delta.wealth_delta = static_cast<float>(tag) * 1.0f;
    db.player_delta.exhaustion_delta = static_cast<float>(tag) * 0.01f;
    {
        SkillDelta sd{};
        sd.skill_id = tag;
        sd.value = static_cast<float>(tag);
        db.player_delta.skill_delta = sd;
    }
    db.player_delta.new_evidence_awareness = tag;
    {
        RelationshipDelta rd{};
        rd.target_npc_id = tag;
        rd.trust_delta = static_cast<float>(tag);
        rd.respect_delta = static_cast<float>(tag);
        db.player_delta.relationship_delta = rd;
    }
    db.player_delta.new_province_id = tag;
    db.player_delta.new_travel_status = NPCTravelStatus::resident;

    {
        MarketDelta d{};
        d.good_id = tag;
        d.region_id = tag;
        db.market_deltas.push_back(d);
    }
    {
        EvidenceDelta d{};
        d.retired_token_id = tag;
        db.evidence_deltas.push_back(d);
    }
    {
        ConsequenceDelta d{};
        d.new_entry_id = tag;
        db.consequence_deltas.push_back(d);
    }
    {
        BusinessDelta d{};
        d.business_id = tag;
        db.business_deltas.push_back(d);
    }
    {
        RegionDelta d{};
        d.region_id = tag;
        db.region_deltas.push_back(d);
    }
    {
        CurrencyDelta d{};
        d.nation_id = tag;
        db.currency_deltas.push_back(d);
    }
    {
        TechnologyDelta d{};
        d.business_id = tag;
        db.technology_deltas.push_back(d);
    }
    {
        CalendarEntry e{};
        e.id = tag;
        db.new_calendar_entries.push_back(e);
    }
    {
        SceneCard s{};
        s.id = tag;
        db.new_scene_cards.push_back(s);
    }
    {
        ObligationNode n{};
        n.creditor_npc_id = tag;
        db.new_obligation_nodes.push_back(n);
    }
    {
        CrossProvinceDelta d{};
        d.source_province_id = tag;
        d.target_province_id = tag + 1;
        d.due_tick = tag;
        db.cross_province_deltas.push_back(d);
    }
    {
        DissolvedBusinessDelta d{};
        d.business_id = tag;
        db.dissolved_businesses.push_back(d);
    }
    {
        NewBusinessDelta d{};
        d.new_business.id = tag;
        db.new_businesses.push_back(d);
    }
    {
        SceneCardChoiceDelta d{};
        d.scene_card_id = tag;
        d.chosen_choice_id = tag;
        db.scene_card_choice_deltas.push_back(d);
    }
    {
        CalendarCommitDelta d{};
        d.calendar_entry_id = tag;
        d.committed = true;
        db.calendar_commit_deltas.push_back(d);
    }
}

}  // namespace

TEST_CASE("test_delta_buffer_merge_covers_every_field", "[world_state][delta_buffer]") {
    DeltaBuffer dst{};
    populate_every_field(dst, 1);

    DeltaBuffer src{};
    populate_every_field(src, 2);

    dst.merge_from(std::move(src));

    // Every vector field must have grown from 1 to 2 entries. If a new
    // DeltaBuffer field is added without merge_from support, the
    // populate_every_field helper above will be updated too — and the
    // corresponding line below will fail until merge_from is wired up.
    REQUIRE(dst.npc_deltas.size() == 2);
    REQUIRE(dst.market_deltas.size() == 2);
    REQUIRE(dst.evidence_deltas.size() == 2);
    REQUIRE(dst.consequence_deltas.size() == 2);
    REQUIRE(dst.business_deltas.size() == 2);
    REQUIRE(dst.region_deltas.size() == 2);
    REQUIRE(dst.currency_deltas.size() == 2);
    REQUIRE(dst.technology_deltas.size() == 2);
    REQUIRE(dst.new_calendar_entries.size() == 2);
    REQUIRE(dst.new_scene_cards.size() == 2);
    REQUIRE(dst.new_obligation_nodes.size() == 2);
    REQUIRE(dst.cross_province_deltas.size() == 2);
    REQUIRE(dst.dissolved_businesses.size() == 2);
    REQUIRE(dst.new_businesses.size() == 2);
    REQUIRE(dst.scene_card_choice_deltas.size() == 2);
    REQUIRE(dst.calendar_commit_deltas.size() == 2);

    // Append order is preserved: the dst-tagged entry comes first.
    REQUIRE(dst.npc_deltas[0].npc_id == 1);
    REQUIRE(dst.npc_deltas[1].npc_id == 2);
    REQUIRE(dst.market_deltas[0].good_id == 1);
    REQUIRE(dst.market_deltas[1].good_id == 2);
    REQUIRE(dst.dissolved_businesses[0].business_id == 1);
    REQUIRE(dst.dissolved_businesses[1].business_id == 2);
}

TEST_CASE("test_player_delta_merge_additive_and_replacement", "[world_state][delta_buffer]") {
    PlayerDelta dst{};
    dst.health_delta = 1.0f;
    dst.wealth_delta = 100.0f;
    dst.exhaustion_delta = 0.5f;
    {
        SkillDelta sd{};
        sd.skill_id = 1;
        sd.value = 1.0f;
        dst.skill_delta = sd;
    }
    dst.new_evidence_awareness = 100;
    dst.new_province_id = 5;
    dst.new_travel_status = NPCTravelStatus::resident;

    PlayerDelta src{};
    src.health_delta = 2.0f;
    src.wealth_delta = 50.0f;
    src.exhaustion_delta = 0.25f;
    {
        SkillDelta sd{};
        sd.skill_id = 7;
        sd.value = 9.0f;
        src.skill_delta = sd;
    }
    src.new_evidence_awareness = 200;
    src.new_province_id = 9;
    src.new_travel_status = NPCTravelStatus::in_transit;

    dst.merge_from(std::move(src));

    // Additive sums.
    REQUIRE_THAT(*dst.health_delta, WithinAbs(3.0f, 0.001f));
    REQUIRE_THAT(*dst.wealth_delta, WithinAbs(150.0f, 0.001f));
    REQUIRE_THAT(*dst.exhaustion_delta, WithinAbs(0.75f, 0.001f));

    // Replacement: incoming wins.
    REQUIRE(dst.skill_delta->skill_id == 7);
    REQUIRE_THAT(dst.skill_delta->value, WithinAbs(9.0f, 0.001f));
    REQUIRE(*dst.new_evidence_awareness == 200);
    REQUIRE(*dst.new_province_id == 9);
    REQUIRE(*dst.new_travel_status == NPCTravelStatus::in_transit);
}

TEST_CASE("test_player_delta_merge_seeds_when_dst_empty", "[world_state][delta_buffer]") {
    // When dst has no value for an additive field but src does, dst
    // should end up holding src's value (not a sum-of-nothing surprise).
    PlayerDelta dst{};
    PlayerDelta src{};
    src.health_delta = 5.0f;
    src.new_province_id = 3;

    dst.merge_from(std::move(src));

    REQUIRE(dst.health_delta.has_value());
    REQUIRE_THAT(*dst.health_delta, WithinAbs(5.0f, 0.001f));
    REQUIRE(dst.new_province_id.has_value());
    REQUIRE(*dst.new_province_id == 3);

    // Untouched additive fields stay empty.
    REQUIRE(!dst.wealth_delta.has_value());
    REQUIRE(!dst.exhaustion_delta.has_value());
}
