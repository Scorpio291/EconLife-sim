#include "modules/media_system/media_system_module.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "core/world_state/player.h"
#include "core/world_state/world_state.h"

namespace econlife {

// ---------------------------------------------------------------------------
// Static utility functions
// ---------------------------------------------------------------------------

float MediaSystemModule::compute_evidence_weight(const std::vector<float>& actionabilities) {
    if (actionabilities.empty())
        return 0.0f;
    float sum = 0.0f;
    for (float a : actionabilities)
        sum += a;
    return std::clamp(sum / static_cast<float>(actionabilities.size()), 0.0f, 1.0f);
}

bool MediaSystemModule::evaluate_editorial_filter(float editorial_independence,
                                                  float owner_suppression_rate, float roll) {
    float threshold = editorial_independence * (1.0f - owner_suppression_rate);
    return roll < threshold;
}

float MediaSystemModule::compute_pickup_probability(float evidence_weight,
                                                    float other_outlet_credibility,
                                                    float cross_outlet_pickup_rate) {
    float prob = evidence_weight * other_outlet_credibility * cross_outlet_pickup_rate;
    return std::clamp(prob, 0.0f, 1.0f);
}

float MediaSystemModule::compute_social_amplification(float current_amplification,
                                                      float social_reach, float social_multiplier,
                                                      float evidence_weight) {
    float amp = current_amplification * social_reach * social_multiplier * (1.0f + evidence_weight);
    if (std::isnan(amp))
        return 0.0f;
    return std::max(0.0f, amp);
}

float MediaSystemModule::compute_exposure_delta(float amplification, float exposure_per_unit) {
    return std::max(0.0f, amplification * exposure_per_unit);
}

bool MediaSystemModule::is_within_propagation_window(uint32_t published_tick, uint32_t current_tick,
                                                     uint32_t window_ticks) {
    if (current_tick < published_tick)
        return false;
    return (current_tick - published_tick) <= window_ticks;
}

// ---------------------------------------------------------------------------
// Execute
// ---------------------------------------------------------------------------

void MediaSystemModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Phase 1: Create new stories from journalist evidence awareness
    create_stories_from_journalists(state, delta);

    // Phase 2: Propagate active stories
    propagate_stories(state, delta);

    // Phase 3: Convert exposure from damaging stories
    convert_exposure(state, delta);

    // Phase 4: Expire old stories
    expire_old_stories(state.current_tick);
}

void MediaSystemModule::create_stories_from_journalists(const WorldState& state,
                                                        DeltaBuffer& delta) {
    // Sort outlets by id for determinism
    std::sort(outlets_.begin(), outlets_.end(),
              [](const MediaOutlet& a, const MediaOutlet& b) { return a.id < b.id; });

    for (const auto& outlet : outlets_) {
        // Sort journalist ids for determinism
        std::vector<uint32_t> sorted_journalist_ids = outlet.journalist_ids;
        std::sort(sorted_journalist_ids.begin(), sorted_journalist_ids.end());

        for (uint32_t journalist_id : sorted_journalist_ids) {
            // Find journalist NPC
            const NPC* journalist = nullptr;
            for (const auto& npc : state.significant_npcs) {
                if (npc.id == journalist_id && npc.role == NPCRole::journalist &&
                    npc.status == NPCStatus::active) {
                    journalist = &npc;
                    break;
                }
            }
            if (!journalist)
                continue;

            // Check journalist's known_evidence for publishable tokens
            for (const auto& knowledge : journalist->known_evidence) {
                if (knowledge.type != KnowledgeType::evidence_token)
                    continue;

                // Find the evidence token
                const EvidenceToken* token = nullptr;
                for (const auto& et : state.evidence_pool) {
                    if (et.id == knowledge.subject_id && et.is_active) {
                        token = &et;
                        break;
                    }
                }
                if (!token)
                    continue;
                if (token->actionability < cfg_.crisis_evidence_threshold)
                    continue;

                // Check if story already exists for this token+outlet
                bool already_published = false;
                for (const auto& story : active_stories_) {
                    for (uint32_t eid : story.evidence_token_ids) {
                        if (eid == token->id && story.outlet_id == outlet.id) {
                            already_published = true;
                            break;
                        }
                    }
                    if (already_published)
                        break;
                }
                if (already_published)
                    continue;

                // Editorial filter for owner-suppressed stories
                bool is_player_owned = (outlet.owner_npc_id == state.player->id);
                bool story_about_player = (token->target_npc_id == state.player->id);

                if (is_player_owned && story_about_player) {
                    // Apply editorial filter: use deterministic roll from confidence
                    float roll = knowledge.confidence;
                    if (!evaluate_editorial_filter(outlet.editorial_independence,
                                                   cfg_.owner_suppression_base_rate, roll)) {
                        // Story suppressed; give journalist memory
                        NPCDelta npc_delta;
                        npc_delta.npc_id = journalist_id;
                        MemoryEntry mem;
                        mem.tick_timestamp = state.current_tick;
                        mem.type = MemoryType::observation;
                        mem.subject_id = state.player->id;
                        mem.emotional_weight = -0.30f;
                        mem.decay = 1.0f;
                        mem.is_actionable = true;
                        npc_delta.new_memory_entry = mem;
                        delta.npc_deltas.push_back(npc_delta);
                        continue;
                    }
                }

                // Publish the story
                Story story{};
                story.id = next_story_id_++;
                story.subject_id = token->target_npc_id;
                story.journalist_id = journalist_id;
                story.outlet_id = outlet.id;
                story.tone = (token->target_npc_id == state.player->id) ? StoryTone::damaging
                                                                        : StoryTone::neutral;
                story.evidence_weight = token->actionability;
                story.amplification = 1.0f;
                story.published_tick = state.current_tick;
                story.evidence_token_ids.push_back(token->id);
                story.is_active = true;
                active_stories_.push_back(story);

                // EvidenceDelta: published story creates a new documentary evidence token
                {
                    EvidenceDelta ev_delta;
                    EvidenceToken doc_token;
                    // Offset story IDs into a dedicated range for documentary tokens
                    doc_token.id = story.id + 0x00100000u;
                    doc_token.type = EvidenceType::documentary;
                    doc_token.source_npc_id = journalist_id;
                    doc_token.target_npc_id = token->target_npc_id;
                    doc_token.actionability = token->actionability * outlet.credibility;
                    doc_token.decay_rate = 0.002f;
                    doc_token.created_tick = state.current_tick;
                    doc_token.province_id = outlet.province_id;
                    doc_token.is_active = true;
                    ev_delta.new_token = doc_token;
                    delta.evidence_deltas.push_back(ev_delta);
                }

                // NPCDelta: journalist gets a memory entry for the publish event
                {
                    NPCDelta journalist_delta;
                    journalist_delta.npc_id = journalist_id;
                    MemoryEntry mem;
                    mem.tick_timestamp = state.current_tick;
                    mem.type = MemoryType::event;
                    mem.subject_id = token->target_npc_id;
                    mem.emotional_weight = 0.40f;  // publishing is a positive career event
                    mem.decay = 0.005f;
                    mem.is_actionable = false;
                    journalist_delta.new_memory_entry = mem;
                    delta.npc_deltas.push_back(journalist_delta);
                }

                // RegionDelta: published story raises grievance and shifts institutional trust
                {
                    RegionDelta rdelta;
                    rdelta.region_id = outlet.province_id;
                    // Investigative stories exposing wrongdoing raise grievance
                    float weight = token->actionability * outlet.reach;
                    rdelta.grievance_delta = weight * 0.03f;
                    // Damaging stories about power figures erode institutional trust
                    if (story.tone == StoryTone::damaging) {
                        rdelta.institutional_trust_delta = -weight * 0.02f;
                    } else {
                        // Neutral investigative reporting slightly improves trust in press
                        rdelta.institutional_trust_delta = weight * 0.005f;
                    }
                    delta.region_deltas.push_back(rdelta);
                }
            }
        }
    }
}

void MediaSystemModule::propagate_stories(const WorldState& state, DeltaBuffer& delta) {
    // Sort active stories by id for determinism
    std::sort(active_stories_.begin(), active_stories_.end(),
              [](const Story& a, const Story& b) { return a.id < b.id; });

    for (auto& story : active_stories_) {
        if (!story.is_active)
            continue;
        if (!is_within_propagation_window(story.published_tick, state.current_tick,
                                          cfg_.propagation_window_ticks)) {
            continue;
        }

        // Cross-outlet pickup
        for (const auto& outlet : outlets_) {
            if (outlet.id == story.outlet_id)
                continue;

            // Find the story's outlet province
            uint32_t story_province = 0;
            for (const auto& so : outlets_) {
                if (so.id == story.outlet_id) {
                    story_province = so.province_id;
                    break;
                }
            }

            // Same province only for cross-outlet pickup
            if (outlet.province_id != story_province)
                continue;

            float pickup_prob = compute_pickup_probability(
                story.evidence_weight, outlet.credibility, cfg_.cross_outlet_pickup_rate);

            // Deterministic pickup: use (story.id * outlet.id) % 100 as roll
            float roll = static_cast<float>((story.id * outlet.id) % 100) / 100.0f;
            if (roll < pickup_prob) {
                story.amplification += outlet.reach * cfg_.cross_outlet_amplification_factor;
            }
        }

        // Social media amplification
        for (const auto& outlet : outlets_) {
            if (outlet.type != MediaOutletType::social_platform)
                continue;

            // Find story's province
            uint32_t story_province = 0;
            for (const auto& so : outlets_) {
                if (so.id == story.outlet_id) {
                    story_province = so.province_id;
                    break;
                }
            }
            if (outlet.province_id != story_province)
                continue;

            float social_amp = compute_social_amplification(
                story.amplification, outlet.reach, cfg_.social_amplification_multiplier,
                story.evidence_weight);
            story.amplification += social_amp;
        }
    }
}

void MediaSystemModule::convert_exposure(const WorldState& state, DeltaBuffer& delta) {
    // Accumulate exposure per subject (sorted by story.id for determinism)
    for (const auto& story : active_stories_) {
        if (!story.is_active)
            continue;
        if (story.tone != StoryTone::damaging)
            continue;
        if (story.evidence_weight < cfg_.crisis_evidence_threshold)
            continue;

        // Find the outlet province for region effects
        uint32_t story_province = 0;
        for (const auto& outlet : outlets_) {
            if (outlet.id == story.outlet_id) {
                story_province = outlet.province_id;
                break;
            }
        }

        float exposure =
            compute_exposure_delta(story.amplification, cfg_.exposure_per_amplification_unit);

        if (story.subject_id == state.player->id) {
            // V1: accumulate into player health_delta as reputation proxy
            if (!delta.player_delta.health_delta.has_value()) {
                delta.player_delta.health_delta = 0.0f;
            }
            // Exposure doesn't directly damage health; this is a placeholder
            // for the reputation system. The player delta tracks it.
        }

        // RegionDelta: ongoing damaging coverage raises regional grievance and
        // erodes institutional trust proportional to amplified exposure
        if (exposure > 0.0f) {
            RegionDelta rdelta;
            rdelta.region_id = story_province;
            rdelta.grievance_delta = exposure * 0.01f;
            rdelta.institutional_trust_delta = -exposure * 0.005f;
            delta.region_deltas.push_back(rdelta);
        }
    }
}

void MediaSystemModule::expire_old_stories(uint32_t current_tick) {
    for (auto& story : active_stories_) {
        if (story.is_active && !is_within_propagation_window(story.published_tick, current_tick,
                                                             cfg_.propagation_window_ticks)) {
            story.is_active = false;
        }
    }
}

}  // namespace econlife
