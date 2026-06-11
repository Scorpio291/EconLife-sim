#include "modules/community_response/community_response_module.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"

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
        if (entry.decay < memory_decay_floor)
            continue;
        float weight = memory_type_grievance_weight(entry.type);
        if (weight <= 0.0f)
            continue;
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
        case MemoryType::witnessed_illegal_activity:
            return 1.0f;
        case MemoryType::witnessed_safety_violation:
            return 1.0f;
        case MemoryType::witnessed_wage_theft:
            return 1.0f;
        case MemoryType::physical_hazard:
            return 1.0f;
        case MemoryType::retaliation_experienced:
            return 1.0f;

        // Economic harm = 0.5
        case MemoryType::employment_negative:
            return 0.5f;
        case MemoryType::facility_quality:
            return 0.5f;

        // All others = 0.0
        default:
            return 0.0f;
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

CommunityResponseStage CommunityResponseModule::evaluate_stage(float grievance, float cohesion,
                                                               float institutional_trust,
                                                               float resource_access,
                                                               bool has_leadership) {
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
    CommunityResponseStage current, CommunityResponseStage target, bool can_regress,
    bool opposition_org_exists) {
    uint8_t current_val = static_cast<uint8_t>(current);
    uint8_t target_val = static_cast<uint8_t>(target);

    if (target_val > current_val) {
        // Advance at most one step.
        return static_cast<CommunityResponseStage>(current_val + 1);
    } else if (target_val < current_val) {
        // Regression rules.
        if (opposition_org_exists && current >= CommunityResponseStage::sustained_opposition) {
            return current;  // No regression once opposition org formed
        }
        if (!can_regress)
            return current;  // Cooldown not expired
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

        // Collect active residents of this province via the home_province
        // bucket index, sorted by id ascending.
        std::vector<const NPC*> province_npcs;
        if (province.id < state.npc_indices_by_home_province.size()) {
            for (uint32_t idx : state.npc_indices_by_home_province[province.id]) {
                const NPC& npc = state.significant_npcs[idx];
                if (npc.status == NPCStatus::active) {
                    province_npcs.push_back(&npc);
                }
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
                // Cohesion: social_capital * stability motivation (OutcomeType::self_preservation =
                // 6)
                float stability_w = npc->motivations.weights[6];
                cohesion_sum += compute_cohesion_sample(npc->social_capital,
                                                        cfg_.social_capital_max, stability_w);

                // Grievance: from negative memory entries
                grievance_sum +=
                    compute_grievance_contribution(npc->memory_log, cfg_.memory_decay_floor);

                // Resource access
                resource_sum +=
                    compute_resource_access_sample(npc->capital, cfg_.capital_normalizer,
                                                   npc->social_capital, cfg_.social_normalizer);
            }

            float npc_count = static_cast<float>(province_npcs.size());
            float cohesion_sample = cohesion_sum / npc_count;
            float resource_sample = resource_sum / npc_count;

            // Grievance target (GDD §14.2): a material-deprivation baseline plus a
            // bounded actor-specific (memory) component. Material grounding is the
            // restoring force the memory-only model lacked — when the economy is
            // healthy (low unemployment/inequality) grievance relaxes toward a low
            // baseline instead of saturating at the ceiling forever.
            float unemployment =
                province.cohort_stats ? province.cohort_stats->unemployment_rate : 0.0f;
            float inequality = province.conditions.inequality_index;
            float material_grievance =
                std::min(1.0f, cfg_.grievance_unemployment_weight * unemployment +
                                   cfg_.grievance_inequality_weight * inequality);
            float memory_grievance =
                std::min(grievance_sum / (npc_count * cfg_.grievance_normalizer), 1.0f);
            float grievance_sample = std::min(
                1.0f, material_grievance + cfg_.grievance_memory_weight * memory_grievance);

            // Check for grievance shock (per-tick increase > threshold).
            float grievance_raw_delta = grievance_sample - grievance;
            if (grievance_raw_delta > cfg_.grievance_shock_threshold) {
                // Shock: bypass EMA, apply directly.
                grievance = std::min(grievance + grievance_raw_delta, 1.0f);
            } else {
                grievance = ema_update(grievance, grievance_sample, cfg_.ema_alpha);
            }

            cohesion = ema_update(cohesion, cohesion_sample, cfg_.ema_alpha);
            resource_access = ema_update(resource_access, resource_sample, cfg_.ema_alpha);

            // Institutional trust: target based on base trust and corruption.
            float trust_target =
                std::min(std::max(0.5f - province.political.corruption_index * 0.5f, 0.0f), 1.0f);
            inst_trust = ema_update(inst_trust, trust_target, cfg_.ema_alpha);
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

        CommunityResponseStage target =
            evaluate_stage(grievance, cohesion, inst_trust, resource_access, has_leadership);

        // Check regression cooldown.
        auto& pstate = province_states_[pi];
        bool can_regress =
            (state.current_tick - pstate.last_stage_change_tick >= cfg_.regression_cooldown_ticks);

        CommunityResponseStage new_stage =
            apply_stage_transition(static_cast<CommunityResponseStage>(current_stage), target,
                                   can_regress, pstate.opposition_org_exists);

        if (static_cast<uint8_t>(new_stage) != current_stage) {
            pstate.last_stage_change_tick = state.current_tick;
        }

        // Opposition org formation: signal via ConsequenceDelta when sustained_opposition
        // is first reached so downstream modules can create the org entity.
        if (new_stage == CommunityResponseStage::sustained_opposition &&
            !pstate.opposition_org_exists) {
            pstate.opposition_org_exists = true;
            ConsequenceDelta cd;
            cd.new_consequence =
                make_consequence(province.id, ConsequenceCategory::political_consequence, 0, 0,
                                 province.id, state.current_tick);
            delta.consequence_deltas.push_back(cd);
        }

        // NOTE: grievance is owned here. Escalation flows grievance → stage, not
        // stage → grievance: a stage-intensity grievance bonus (removed) created a
        // self-reinforcing pump that pinned grievance at the ceiling. The material
        // grounding in grievance_sample is the restoring force.

        // Write deltas for this province.
        RegionDelta rd;
        rd.region_id = province.id;
        rd.cohesion_delta = cohesion - province.community.cohesion;
        rd.resource_access_delta = resource_access - province.community.resource_access;
        rd.grievance_delta = grievance - province.community.grievance_level;
        rd.institutional_trust_delta = inst_trust - province.community.institutional_trust;
        // Write response stage if it changed.
        if (static_cast<uint8_t>(new_stage) != current_stage) {
            rd.response_stage_replacement = static_cast<uint8_t>(new_stage);
        }
        delta.region_deltas.push_back(rd);
    }
}

// ─── Persistence helpers (schema v7) ────────────────────────────────────────
//
// Note: province_states_ is indexed by position (province slot, not
// province_id) — the existing module code populates it via push_back for
// every province at init time. Serialization preserves vector ordering.

namespace {

void put_u32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

struct Reader {
    const uint8_t* data;
    size_t size;
    size_t pos = 0;
    bool error = false;
    bool need(size_t n) {
        if (pos + n > size) {
            error = true;
            return false;
        }
        return true;
    }
    uint32_t u32() {
        if (!need(4))
            return 0;
        uint32_t v = data[pos] | (uint32_t(data[pos + 1]) << 8) | (uint32_t(data[pos + 2]) << 16) |
                     (uint32_t(data[pos + 3]) << 24);
        pos += 4;
        return v;
    }
    uint8_t u8() {
        if (!need(1))
            return 0;
        return data[pos++];
    }
};

}  // namespace

void CommunityResponseModule::serialize_state(std::vector<uint8_t>& out) const {
    put_u32(out, 1u);
    put_u32(out, static_cast<uint32_t>(province_states_.size()));
    for (const auto& s : province_states_) {
        out.push_back(s.opposition_org_exists ? 1u : 0u);
        put_u32(out, s.last_stage_change_tick);
    }
}

bool CommunityResponseModule::deserialize_state(const uint8_t* data, size_t size) {
    Reader r{data, size};
    if (r.u32() != 1u)
        return false;
    uint32_t count = r.u32();
    province_states_.clear();
    province_states_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        ProvinceOppositionState s{};
        s.opposition_org_exists = (r.u8() != 0);
        s.last_stage_change_tick = r.u32();
        if (r.error)
            return false;
        province_states_.push_back(s);
    }
    return !r.error;
}

}  // namespace econlife
