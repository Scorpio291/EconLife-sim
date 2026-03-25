#include "modules/community_response/community_response_module.h"

#include "core/world_state/world_state.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"

#include <algorithm>
#include <cmath>

namespace econlife {

// ---------------------------------------------------------------------------
// Static utility implementations
// ---------------------------------------------------------------------------

float CommunityResponseModule::compute_cohesion_sample(float social_capital,
                                                        float social_capital_max,
                                                        float stability_weight) {
    float sc_norm = std::min(std::max(social_capital / social_capital_max, 0.0f), 1.0f);
    float sw_clamped = std::min(std::max(stability_weight, 0.0f), 1.0f);
    return sc_norm * sw_clamped;
}

float CommunityResponseModule::compute_grievance_contribution(
    const std::vector<MemoryEntry>& memory_log, float memory_decay_floor) {
    float total = 0.0f;
    for (const auto& entry : memory_log) {
        if (entry.decay < memory_decay_floor) continue;
        float weight = memory_type_grievance_weight(entry.type);
        if (weight <= 0.0f) continue;
        if (entry.emotional_weight < 0.0f) {
            // Negative emotional_weight indicates grievance
            total += std::abs(entry.emotional_weight) * weight;
        }
    }
    return total;
}

float CommunityResponseModule::memory_type_grievance_weight(MemoryType type) {
    switch (type) {
        // Direct harm types = 1.0
        case MemoryType::witnessed_illegal_activity:  return 1.0f;
        case MemoryType::witnessed_safety_violation:  return 1.0f;
        case MemoryType::witnessed_wage_theft:        return 1.0f;
        case MemoryType::physical_hazard:             return 1.0f;
        case MemoryType::retaliation_experienced:     return 1.0f;

        // Economic harm = 0.5
        case MemoryType::employment_negative:         return 0.5f;
        case MemoryType::facility_quality:            return 0.5f;

        // All others = 0.0
        default:                                      return 0.0f;
    }
}

float CommunityResponseModule::compute_resource_access_sample(float capital,
                                                               float capital_normalizer,
                                                               float social_capital,
                                                               float social_normalizer) {
    float raw = (capital / capital_normalizer) + (social_capital / social_normalizer);
    return std::min(std::max(raw, 0.0f), 1.0f);
}

float CommunityResponseModule::ema_update(float current_value, float new_sample, float alpha) {
    return current_value * (1.0f - alpha) + new_sample * alpha;
}

CommunityResponseStage CommunityResponseModule::evaluate_stage(
    float grievance, float cohesion, float institutional_trust,
    float resource_access, bool has_leadership) {
    // Evaluate from highest to lowest; return highest satisfied stage.
    if (grievance >= 0.85f && resource_access >= 0.35f && has_leadership)
        return CommunityResponseStage::sustained_opposition;
    if (grievance >= 0.70f && cohesion >= 0.45f)
        return CommunityResponseStage::direct_action;
    if (grievance >= 0.56f && resource_access >= 0.25f)
        return CommunityResponseStage::economic_resistance;
    if (grievance >= 0.42f && institutional_trust >= 0.20f)
        return CommunityResponseStage::political_mobilization;
    if (grievance >= 0.28f && cohesion >= 0.25f)
        return CommunityResponseStage::organized_complaint;
    if (grievance >= 0.15f && cohesion >= 0.10f)
        return CommunityResponseStage::informal_complaint;
    return CommunityResponseStage::quiescent;
}

CommunityResponseStage CommunityResponseModule::apply_stage_transition(
    CommunityResponseStage current, CommunityResponseStage target,
    bool can_regress, bool opposition_org_exists) {
    uint8_t current_val = static_cast<uint8_t>(current);
    uint8_t target_val = static_cast<uint8_t>(target);

    if (target_val > current_val) {
        // Advance at most one step.
        return static_cast<CommunityResponseStage>(current_val + 1);
    } else if (target_val < current_val) {
        // Regression rules.
        if (opposition_org_exists &&
            current >= CommunityResponseStage::sustained_opposition) {
            return current;  // No regression once opposition org formed
        }
        if (!can_regress) return current;  // Cooldown not expired
        // Regress at most one step.
        return static_cast<CommunityResponseStage>(current_val - 1);
    }
    return current;  // No change
}

// ---------------------------------------------------------------------------
// execute — main tick entry point
// ---------------------------------------------------------------------------

void CommunityResponseModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Ensure province_states_ is sized correctly.
    while (province_states_.size() < state.provinces.size()) {
        province_states_.push_back(ProvinceOppositionState{});
    }

    for (uint32_t pi = 0; pi < state.provinces.size(); ++pi) {
        const auto& province = state.provinces[pi];

        // Collect active NPCs in this province, sorted by id ascending.
        std::vector<const NPC*> province_npcs;
        for (const auto& npc : state.significant_npcs) {
            if (npc.home_province_id == province.id && npc.status == NPCStatus::active) {
                province_npcs.push_back(&npc);
            }
        }
        std::sort(province_npcs.begin(), province_npcs.end(),
                  [](const NPC* a, const NPC* b) { return a->id < b->id; });

        // Current community metrics.
        float cohesion = province.community.cohesion;
        float grievance = province.community.grievance_level;
        float inst_trust = province.community.institutional_trust;
        float resource_access = province.community.resource_access;
        uint8_t current_stage = province.community.response_stage;

        if (!province_npcs.empty()) {
            // Compute EMA samples for all four metrics.
            float cohesion_sum = 0.0f;
            float grievance_sum = 0.0f;
            float resource_sum = 0.0f;

            for (const auto* npc : province_npcs) {
                // Cohesion: social_capital * stability motivation (OutcomeType::self_preservation = 6)
                float stability_w = npc->motivations.weights[6];
                cohesion_sum += compute_cohesion_sample(
                    npc->social_capital, Constants::social_capital_max, stability_w);

                // Grievance: from negative memory entries
                grievance_sum += compute_grievance_contribution(
                    npc->memory_log, Constants::memory_decay_floor);

                // Resource access
                resource_sum += compute_resource_access_sample(
                    npc->capital, Constants::capital_normalizer,
                    npc->social_capital, Constants::social_normalizer);
            }

            float npc_count = static_cast<float>(province_npcs.size());
            float cohesion_sample = cohesion_sum / npc_count;
            float grievance_sample = std::min(
                grievance_sum / (npc_count * Constants::grievance_normalizer), 1.0f);
            float resource_sample = resource_sum / npc_count;

            // Check for grievance shock (per-tick increase > threshold).
            float grievance_raw_delta = grievance_sample - grievance;
            if (grievance_raw_delta > Constants::grievance_shock_threshold) {
                // Shock: bypass EMA, apply directly.
                grievance = std::min(grievance + grievance_raw_delta, 1.0f);
            } else {
                grievance = ema_update(grievance, grievance_sample, Constants::ema_alpha);
            }

            cohesion = ema_update(cohesion, cohesion_sample, Constants::ema_alpha);
            resource_access = ema_update(resource_access, resource_sample, Constants::ema_alpha);

            // Institutional trust: target based on base trust and corruption.
            float trust_target = std::min(std::max(
                0.5f - province.political.corruption_index * 0.5f, 0.0f), 1.0f);
            inst_trust = ema_update(inst_trust, trust_target, Constants::ema_alpha);
        }

        // Clamp all metrics to [0, 1].
        cohesion = std::min(std::max(cohesion, 0.0f), 1.0f);
        grievance = std::min(std::max(grievance, 0.0f), 1.0f);
        inst_trust = std::min(std::max(inst_trust, 0.0f), 1.0f);
        resource_access = std::min(std::max(resource_access, 0.0f), 1.0f);

        // Evaluate target stage.
        bool has_leadership = false;  // simplified: no leadership check in V1 bootstrap
        for (const auto* npc : province_npcs) {
            if (npc->role == NPCRole::community_leader && npc->social_capital > 30.0f) {
                has_leadership = true;
                break;
            }
        }

        CommunityResponseStage target = evaluate_stage(
            grievance, cohesion, inst_trust, resource_access, has_leadership);

        // Check regression cooldown.
        auto& pstate = province_states_[pi];
        bool can_regress = (state.current_tick - pstate.last_stage_change_tick >=
                            Constants::regression_cooldown_ticks);

        CommunityResponseStage new_stage = apply_stage_transition(
            static_cast<CommunityResponseStage>(current_stage),
            target, can_regress, pstate.opposition_org_exists);

        if (static_cast<uint8_t>(new_stage) != current_stage) {
            pstate.last_stage_change_tick = state.current_tick;
        }

        // Opposition org formation: signal via ConsequenceDelta when sustained_opposition
        // is first reached so downstream modules can create the org entity.
        if (new_stage == CommunityResponseStage::sustained_opposition &&
            !pstate.opposition_org_exists) {
            pstate.opposition_org_exists = true;
            ConsequenceDelta cd;
            cd.new_entry_id = province.id;  // Encodes province as pending org formation signal.
            delta.consequence_deltas.push_back(cd);
        }

        // Stage-intensity grievance accumulation: higher stages drive additional grievance
        // proportional to stage ordinal (quiescent = 0, sustained_opposition = 6).
        // Per INTERFACE.md, stage checks fire every 7 ticks. Apply the bonus only at
        // those intervals to prevent runaway positive feedback.
        float stage_grievance_bonus = 0.0f;
        if (state.current_tick % 7 == 0) {
            static constexpr float stage_grievance_rate = 0.002f;
            stage_grievance_bonus = static_cast<float>(static_cast<uint8_t>(new_stage))
                                    * stage_grievance_rate;
        }

        // Write deltas for this province.
        RegionDelta rd;
        rd.region_id = province.id;
        rd.cohesion_delta = cohesion - province.community.cohesion;
        rd.resource_access_delta = resource_access - province.community.resource_access;
        rd.grievance_delta = (grievance - province.community.grievance_level)
                             + stage_grievance_bonus;
        rd.institutional_trust_delta = inst_trust - province.community.institutional_trust;
        // Write response stage if it changed.
        if (static_cast<uint8_t>(new_stage) != current_stage) {
            rd.response_stage_replacement = static_cast<uint8_t>(new_stage);
        }
        delta.region_deltas.push_back(rd);
    }
}

}  // namespace econlife
