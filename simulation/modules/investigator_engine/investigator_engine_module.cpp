#include "investigator_engine_module.h"

#include "core/world_state/world_state.h"
#include "core/world_state/player.h"
#include "modules/facility_signals/facility_signals_types.h"
#include "modules/criminal_operations/criminal_operations_types.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace econlife {

// ============================================================================
// Static utility functions
// ============================================================================

float InvestigatorEngineModule::compute_regional_signal(
    const std::vector<float>& facility_net_signals,
    float facility_count_normalizer)
{
    if (facility_net_signals.empty() || facility_count_normalizer <= 0.0f) {
        return 0.0f;
    }
    float sum = 0.0f;
    for (float s : facility_net_signals) {
        sum += s;
    }
    return sum / facility_count_normalizer;
}

float InvestigatorEngineModule::compute_fill_rate(
    float regional_signal,
    float detection_to_fill_rate_scale,
    float fill_rate_max)
{
    float raw = regional_signal * detection_to_fill_rate_scale;
    return std::clamp(raw, 0.0f, fill_rate_max);
}

float InvestigatorEngineModule::apply_corruption_modifier(
    float fill_rate,
    float corruption_susceptibility,
    float regional_corruption_coverage)
{
    float modifier = 1.0f - corruption_susceptibility * regional_corruption_coverage;
    modifier = std::clamp(modifier, 0.0f, 1.0f);
    return fill_rate * modifier;
}

uint8_t InvestigatorEngineModule::derive_status(
    float current_level,
    float surveillance_threshold,
    float formal_inquiry_threshold,
    float raid_threshold)
{
    // Check in descending threshold order per INTERFACE.md invariant
    if (current_level >= raid_threshold) {
        return static_cast<uint8_t>(InvestigatorMeterStatus::raid_imminent);
    }
    if (current_level >= formal_inquiry_threshold) {
        return static_cast<uint8_t>(InvestigatorMeterStatus::formal_inquiry);
    }
    if (current_level >= surveillance_threshold) {
        return static_cast<uint8_t>(InvestigatorMeterStatus::surveillance);
    }
    return static_cast<uint8_t>(InvestigatorMeterStatus::inactive);
}

uint32_t InvestigatorEngineModule::resolve_target(
    const std::vector<std::pair<uint32_t, float>>& actor_signal_contributions)
{
    if (actor_signal_contributions.empty()) {
        return 0;  // sentinel: no known criminal actor
    }
    uint32_t best_id = 0;
    float best_signal = -1.0f;
    for (const auto& [actor_id, signal] : actor_signal_contributions) {
        if (signal > best_signal) {
            best_signal = signal;
            best_id = actor_id;
        }
    }
    return best_id;
}

float InvestigatorEngineModule::compute_decay(
    float current_level,
    float decay_rate)
{
    float decayed = current_level - decay_rate;
    return std::max(0.0f, decayed);
}

// ============================================================================
// Province-parallel execution
// ============================================================================

void InvestigatorEngineModule::execute_province(
    uint32_t province_idx,
    const WorldState& state,
    DeltaBuffer& province_delta)
{
    if (province_idx >= state.provinces.size()) return;
    const auto& province = state.provinces[province_idx];

    // Phase 1: Collect criminal facility net_signals in this province (sorted by business_id)
    std::vector<float> criminal_net_signals;
    std::map<uint32_t, float> actor_signal_map;  // owner_id -> cumulative signal

    // Collect criminal businesses sorted by id ascending for deterministic accumulation
    std::vector<const NPCBusiness*> criminal_businesses;
    for (const auto& biz : state.npc_businesses) {
        if (biz.criminal_sector && biz.province_id == province.id) {
            criminal_businesses.push_back(&biz);
        }
    }
    std::sort(criminal_businesses.begin(), criminal_businesses.end(),
              [](const NPCBusiness* a, const NPCBusiness* b) { return a->id < b->id; });

    for (const auto* biz : criminal_businesses) {
        // Use regulatory_violation_severity as proxy net_signal
        // In full impl, this reads from FacilitySignals computed in facility_signals step
        float net_signal = biz->regulatory_violation_severity;
        criminal_net_signals.push_back(net_signal);
        actor_signal_map[biz->owner_id] += net_signal;
    }

    float regional_signal = compute_regional_signal(criminal_net_signals, FACILITY_COUNT_NORMALIZER);

    // Phase 2: Process investigator NPCs in this province
    std::vector<const NPC*> investigators;
    for (const auto& npc : state.significant_npcs) {
        if (npc.current_province_id != province.id) continue;
        if (npc.status != NPCStatus::active) continue;
        if (npc.role == NPCRole::law_enforcement ||
            npc.role == NPCRole::regulator ||
            npc.role == NPCRole::journalist ||
            npc.role == NPCRole::ngo_investigator) {
            investigators.push_back(&npc);
        }
    }
    std::sort(investigators.begin(), investigators.end(),
              [](const NPC* a, const NPC* b) { return a->id < b->id; });

    float corruption_coverage = province.political.corruption_index;

    for (const auto* inv : investigators) {
        bool is_le  = (inv->role == NPCRole::law_enforcement);
        bool is_ngo = (inv->role == NPCRole::ngo_investigator);
        bool is_reg = (inv->role == NPCRole::regulator);
        bool is_jrn = (inv->role == NPCRole::journalist);

        float fill_rate = 0.0f;
        if (is_le || is_ngo) {
            fill_rate = compute_fill_rate(regional_signal, DETECTION_TO_FILL_RATE_SCALE, FILL_RATE_MAX);
        } else if (is_reg) {
            // Regulators: lower signal aggregate (chemical + traffic only)
            float regulator_signal = regional_signal * 0.5f;
            fill_rate = compute_fill_rate(regulator_signal, DETECTION_TO_FILL_RATE_SCALE, FILL_RATE_MAX);
        } else if (is_jrn) {
            // Journalists: evidence-driven, lower rate
            float journalist_signal = regional_signal * 0.25f;
            fill_rate = compute_fill_rate(journalist_signal, DETECTION_TO_FILL_RATE_SCALE, FILL_RATE_MAX);
        }

        // Apply corruption modifier (NGO investigators are immune per INTERFACE.md)
        if (!is_ngo) {
            fill_rate = apply_corruption_modifier(fill_rate, DEFAULT_CORRUPTION_SUSCEPTIBILITY, corruption_coverage);
        }

        // Find or create case for this investigator
        InvestigationCase* found_case = nullptr;
        for (auto& c : cases_) {
            if (c.investigator_npc_id == inv->id) {
                found_case = &c;
                break;
            }
        }

        if (!found_case) {
            InvestigatorType inv_type = InvestigatorType::law_enforcement;
            if (is_reg) inv_type = InvestigatorType::regulator;
            else if (is_jrn) inv_type = InvestigatorType::journalist;
            else if (is_ngo) inv_type = InvestigatorType::ngo_investigator;

            cases_.push_back(InvestigationCase{
                inv->id, inv_type, 0, 0.0f, 0.0f,
                static_cast<uint8_t>(InvestigatorMeterStatus::inactive),
                0, false, province.id
            });
            found_case = &cases_.back();
        }

        // If no fill rate signal, decay the meter
        if (fill_rate <= 0.0f && !found_case->formally_opened) {
            found_case->current_level = compute_decay(found_case->current_level, DECAY_RATE);
        } else {
            found_case->fill_rate = fill_rate;
            found_case->current_level = std::clamp(found_case->current_level + fill_rate, 0.0f, 1.0f);
        }

        // Formally opened investigations don't close on signal drop
        if (found_case->formally_opened && fill_rate <= 0.0f) {
            found_case->current_level = compute_decay(found_case->current_level, DECAY_RATE);
            found_case->current_level = std::max(found_case->current_level, FORMAL_INQUIRY_THRESHOLD);
        }

        // Derive status
        uint8_t old_status = found_case->status;
        found_case->status = derive_status(
            found_case->current_level,
            SURVEILLANCE_THRESHOLD, FORMAL_INQUIRY_THRESHOLD, RAID_THRESHOLD);

        // Resolve target
        std::vector<std::pair<uint32_t, float>> contributions(
            actor_signal_map.begin(), actor_signal_map.end());
        found_case->target_id = resolve_target(contributions);

        auto new_status = static_cast<InvestigatorMeterStatus>(found_case->status);

        // Surveillance transition: generate physical evidence token
        if (new_status >= InvestigatorMeterStatus::surveillance &&
            old_status < static_cast<uint8_t>(InvestigatorMeterStatus::surveillance)) {
            EvidenceDelta ev_delta;
            ev_delta.new_token = EvidenceToken{
                0, EvidenceType::physical,
                inv->id, found_case->target_id,
                0.3f, 0.001f,
                state.current_tick, province.id, true
            };
            province_delta.evidence_deltas.push_back(ev_delta);
        }

        // Formal inquiry transition: mark opened, queue consequence
        if (new_status >= InvestigatorMeterStatus::formal_inquiry && !found_case->formally_opened) {
            found_case->formally_opened = true;
            found_case->opened_tick = state.current_tick;
            ConsequenceDelta cons;
            cons.new_entry_id = inv->id;
            province_delta.consequence_deltas.push_back(cons);
        }

        // Raid imminent transition: queue raid consequence
        if (new_status >= InvestigatorMeterStatus::raid_imminent &&
            old_status < static_cast<uint8_t>(InvestigatorMeterStatus::raid_imminent)) {
            ConsequenceDelta cons;
            cons.new_entry_id = inv->id;
            province_delta.consequence_deltas.push_back(cons);
        }
    }
}

void InvestigatorEngineModule::execute(const WorldState& state, DeltaBuffer& delta) {
    for (uint32_t i = 0; i < state.provinces.size(); ++i) {
        execute_province(i, state, delta);
    }
}

}  // namespace econlife
