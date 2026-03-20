#include "political_cycle_module.h"
#include "core/world_state/world_state.h"
#include "core/world_state/delta_buffer.h"
#include <algorithm>
#include <cmath>

namespace econlife {

float PoliticalCycleModule::compute_raw_vote_share(
    const std::unordered_map<std::string, float>& approval_by_demographic,
    const std::vector<DemographicWeight>& demographics)
{
    float weighted_sum = 0.0f;
    float weight_total = 0.0f;

    // Process demographics in sorted order for determinism
    std::vector<const DemographicWeight*> sorted;
    sorted.reserve(demographics.size());
    for (const auto& d : demographics) {
        sorted.push_back(&d);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const DemographicWeight* a, const DemographicWeight* b) {
                  return a->demographic < b->demographic;
              });

    for (const auto* d : sorted) {
        float approval = 0.5f;  // default
        auto it = approval_by_demographic.find(d->demographic);
        if (it != approval_by_demographic.end()) {
            approval = it->second;
        }
        float w = d->population_fraction * d->turnout_weight;
        weighted_sum += approval * w;
        weight_total += w;
    }

    if (weight_total <= 0.0f) {
        return 0.5f;  // coin flip fallback per spec
    }

    return weighted_sum / weight_total;
}

float PoliticalCycleModule::compute_resource_modifier(float resource_deployment) {
    float raw = std::tanh(resource_deployment * RESOURCE_SCALE) * RESOURCE_MAX_EFFECT;
    return std::clamp(raw, -RESOURCE_MAX_EFFECT, RESOURCE_MAX_EFFECT);
}

float PoliticalCycleModule::compute_event_modifier_total(const std::vector<float>& event_modifiers) {
    float total = 0.0f;
    for (float m : event_modifiers) {
        total += m;
    }
    return std::clamp(total, -EVENT_MODIFIER_CAP, EVENT_MODIFIER_CAP);
}

float PoliticalCycleModule::compute_final_vote_share(float raw_share, float resource_modifier,
                                                      float event_total) {
    return std::clamp(raw_share + resource_modifier + event_total, 0.0f, 1.0f);
}

float PoliticalCycleModule::compute_legislator_support(float motivation_alignment,
                                                        float obligation_bonus,
                                                        float constituency_pressure) {
    return motivation_alignment + obligation_bonus + constituency_pressure;
}

bool PoliticalCycleModule::compute_vote_passed(float votes_for, float votes_against,
                                                float majority_threshold) {
    float total = votes_for + votes_against;
    if (total <= 0.0f) return false;
    return (votes_for / total) > majority_threshold;
}

void PoliticalCycleModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Process elections for offices due this tick
    for (auto& office : political_state_.offices) {
        if (office.election_due_tick != state.current_tick) continue;

        // Find campaigns targeting this office
        for (auto& campaign : political_state_.campaigns) {
            if (campaign.office_id != office.id) continue;
            if (campaign.resolved) continue;

            float event_total = compute_event_modifier_total(campaign.event_modifiers);
            float resource_mod = compute_resource_modifier(campaign.resource_deployment);

            // Compute raw share from campaign approval
            float raw_share = 0.5f;
            if (!campaign.current_approval_by_demographic.empty()) {
                float sum = 0.0f;
                float count = 0.0f;
                for (const auto& [dem, approval] : campaign.current_approval_by_demographic) {
                    sum += approval;
                    count += 1.0f;
                }
                if (count > 0.0f) raw_share = sum / count;
            }

            float final_share = compute_final_vote_share(raw_share, resource_mod, event_total);

            NPCDelta npc_delta;
            npc_delta.npc_id = static_cast<uint32_t>(campaign.active_candidate_id);

            if (final_share >= office.win_threshold) {
                office.current_holder_id = campaign.active_candidate_id;
                npc_delta.new_memory_entry = MemoryEntry{
                    state.current_tick, MemoryType::event,
                    static_cast<uint32_t>(office.id),
                    0.8f, 0.01f, true
                };
            } else {
                npc_delta.new_memory_entry = MemoryEntry{
                    state.current_tick, MemoryType::event,
                    static_cast<uint32_t>(office.id),
                    -0.6f, 0.01f, true
                };
            }
            delta.npc_deltas.push_back(npc_delta);
            campaign.resolved = true;
        }

        office.election_due_tick = state.current_tick + office.term_length_ticks;
    }

    // Process legislative proposals due this tick
    for (auto& proposal : political_state_.proposals) {
        if (proposal.status != LegislativeProposalStatus::floor_debate) continue;
        if (proposal.vote_tick != state.current_tick) continue;

        bool passed = compute_vote_passed(proposal.votes_for, proposal.votes_against,
                                           MAJORITY_THRESHOLD);
        proposal.status = passed ? LegislativeProposalStatus::enacted
                                 : LegislativeProposalStatus::failed;
    }
}

}  // namespace econlife
