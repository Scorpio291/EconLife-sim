// DeltaBuffer / PlayerDelta merge implementations.
// See delta_buffer.h for the per-field merge policy.

#include "core/world_state/delta_buffer.h"

#include <utility>

namespace econlife {

namespace {

template <typename T>
void move_extend(std::vector<T>& dst, std::vector<T>&& src) {
    if (dst.empty()) {
        dst = std::move(src);
        return;
    }
    dst.reserve(dst.size() + src.size());
    for (auto& item : src) {
        dst.push_back(std::move(item));
    }
    src.clear();
}

}  // namespace

void PlayerDelta::merge_from(PlayerDelta&& other) {
    // Additive optionals: sum incoming into existing (or seed with incoming).
    if (other.health_delta.has_value()) {
        health_delta = health_delta.value_or(0.0f) + *other.health_delta;
    }
    if (other.wealth_delta.has_value()) {
        wealth_delta = wealth_delta.value_or(0.0f) + *other.wealth_delta;
    }
    if (other.exhaustion_delta.has_value()) {
        exhaustion_delta = exhaustion_delta.value_or(0.0f) + *other.exhaustion_delta;
    }

    // Replacement optionals: incoming wins when set.
    if (other.skill_delta.has_value()) {
        skill_delta = std::move(other.skill_delta);
    }
    if (other.new_evidence_awareness.has_value()) {
        new_evidence_awareness = other.new_evidence_awareness;
    }
    if (other.relationship_delta.has_value()) {
        relationship_delta = std::move(other.relationship_delta);
    }
    if (other.new_province_id.has_value()) {
        new_province_id = other.new_province_id;
    }
    if (other.new_travel_status.has_value()) {
        new_travel_status = other.new_travel_status;
    }
}

void DeltaBuffer::merge_from(DeltaBuffer&& other) {
    move_extend(npc_deltas, std::move(other.npc_deltas));
    player_delta.merge_from(std::move(other.player_delta));
    move_extend(market_deltas, std::move(other.market_deltas));
    move_extend(evidence_deltas, std::move(other.evidence_deltas));
    move_extend(consequence_deltas, std::move(other.consequence_deltas));
    move_extend(business_deltas, std::move(other.business_deltas));
    move_extend(region_deltas, std::move(other.region_deltas));
    move_extend(currency_deltas, std::move(other.currency_deltas));
    move_extend(technology_deltas, std::move(other.technology_deltas));
    move_extend(new_calendar_entries, std::move(other.new_calendar_entries));
    move_extend(new_scene_cards, std::move(other.new_scene_cards));
    move_extend(new_obligation_nodes, std::move(other.new_obligation_nodes));
    move_extend(cross_province_deltas, std::move(other.cross_province_deltas));
    move_extend(dissolved_businesses, std::move(other.dissolved_businesses));
    move_extend(new_businesses, std::move(other.new_businesses));
    move_extend(scene_card_choice_deltas, std::move(other.scene_card_choice_deltas));
    move_extend(calendar_commit_deltas, std::move(other.calendar_commit_deltas));
}

}  // namespace econlife
