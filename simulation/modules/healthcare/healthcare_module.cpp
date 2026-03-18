// Healthcare Module — implementation.
// See healthcare_module.h for class declarations and
// docs/interfaces/healthcare/INTERFACE.md for the canonical specification.
//
// Processing order per province (sorted by NPC id ascending):
//   Step 1: Passive health recovery for all active NPCs
//   Step 2: Treatment for critically ill NPCs who can afford care
//   Step 3: NPC death check (health <= 0.0)
//   Step 4: Overload quality degradation
//   Step 5: Sick leave fraction and effective labour supply computation

#include "modules/healthcare/healthcare_module.h"
#include "core/world_state/world_state.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"  // PlayerCharacter complete type

#include <algorithm>
#include <cmath>
#include <vector>

namespace econlife {

// ===========================================================================
// HealthcareModule — tick execution
// ===========================================================================

void HealthcareModule::execute_province(uint32_t province_idx,
                                         const WorldState& state,
                                         DeltaBuffer& province_delta) {
    // Find province health state for this province.
    ProvinceHealthState* phs = find_province_health(province_idx);
    if (!phs) {
        return;  // No healthcare state registered for this province.
    }

    HealthcareProfile& profile = phs->profile;

    // Collect active NPCs in this province, sorted by npc_id ascending
    // for deterministic processing order.
    std::vector<NpcHealthRecord*> province_npcs;
    for (auto& record : npc_health_records_) {
        // Look up the NPC in WorldState to check province and status.
        const NPC* npc = nullptr;
        for (const auto& n : state.significant_npcs) {
            if (n.id == record.npc_id) {
                npc = &n;
                break;
            }
        }
        if (!npc) continue;
        if (npc->current_province_id != province_idx) continue;
        if (npc->status != NPCStatus::active) continue;

        province_npcs.push_back(&record);
    }

    // Sort by npc_id ascending (canonical order per CLAUDE.md).
    std::sort(province_npcs.begin(), province_npcs.end(),
              [](const NpcHealthRecord* a, const NpcHealthRecord* b) {
                  return a->npc_id < b->npc_id;
              });

    // Counters for sick leave computation.
    uint32_t sick_count = 0;
    uint32_t labour_force = static_cast<uint32_t>(province_npcs.size());

    // Process each NPC through the health pipeline.
    for (NpcHealthRecord* hr : province_npcs) {
        // Skip NPCs already dead at entry — ensure status is dead.
        if (hr->health <= 0.0f) {
            NPCDelta death_delta{};
            death_delta.npc_id = hr->npc_id;
            death_delta.new_status = NPCStatus::dead;
            province_delta.npc_deltas.push_back(death_delta);
            continue;
        }

        // Look up NPC for capital check.
        const NPC* npc = nullptr;
        for (const auto& n : state.significant_npcs) {
            if (n.id == hr->npc_id) {
                npc = &n;
                break;
            }
        }
        if (!npc) continue;

        // ---------------------------------------------------------------
        // Step 1: Passive Health Recovery
        // ---------------------------------------------------------------
        float recovery = compute_passive_recovery(
            profile.access_level,
            profile.quality_level,
            Constants::base_recovery_rate);

        hr->health += recovery;

        // ---------------------------------------------------------------
        // Step 2: Treatment for critically ill NPCs
        // ---------------------------------------------------------------
        if (hr->health < Constants::critical_health_threshold
            && profile.access_level > 0.0f
            && npc->capital >= profile.cost_per_treatment) {

            float boost = compute_treatment_boost(
                profile.quality_level,
                Constants::treatment_health_boost);

            hr->health += boost;

            // Deduct treatment cost from NPC capital.
            NPCDelta cost_delta{};
            cost_delta.npc_id = hr->npc_id;
            cost_delta.capital_delta = -profile.cost_per_treatment;
            province_delta.npc_deltas.push_back(cost_delta);

            // Update capacity utilisation.
            profile.capacity_utilisation += Constants::capacity_per_treatment;
            if (profile.capacity_utilisation > 1.0f) {
                profile.capacity_utilisation = 1.0f;
            }

            // Update last treatment tick.
            hr->last_treatment_tick = state.current_tick;

            // Generate health event memory.
            NPCDelta mem_delta{};
            mem_delta.npc_id = hr->npc_id;
            MemoryEntry mem{};
            mem.tick_timestamp = state.current_tick;
            mem.type = MemoryType::event;
            mem.subject_id = hr->npc_id;
            mem.emotional_weight = -0.3f;  // negative = health concern
            mem.decay = 1.0f;
            mem.is_actionable = false;
            mem_delta.new_memory_entry = mem;
            province_delta.npc_deltas.push_back(mem_delta);
        }

        // Clamp health to [0.0, 1.0].
        if (hr->health > 1.0f) {
            hr->health = 1.0f;
        }
        if (hr->health < 0.0f) {
            hr->health = 0.0f;
        }

        // ---------------------------------------------------------------
        // Step 3: NPC Death Check
        // ---------------------------------------------------------------
        if (hr->health <= 0.0f) {
            NPCDelta death_delta{};
            death_delta.npc_id = hr->npc_id;
            death_delta.new_status = NPCStatus::dead;
            province_delta.npc_deltas.push_back(death_delta);
        }

        // ---------------------------------------------------------------
        // Sick leave counting (for Step 5)
        // ---------------------------------------------------------------
        if (hr->health < Constants::labour_impairment_threshold) {
            sick_count++;
        }
    }

    // ---------------------------------------------------------------
    // Step 4: Overload Quality Degradation
    // ---------------------------------------------------------------
    profile.quality_level = compute_overload_quality(
        profile.quality_level,
        profile.capacity_utilisation,
        Constants::overload_threshold,
        Constants::overload_quality_penalty);

    // Clamp quality to [0.0, 1.0].
    if (profile.quality_level > 1.0f) {
        profile.quality_level = 1.0f;
    }
    if (profile.quality_level < 0.0f) {
        profile.quality_level = 0.0f;
    }

    // ---------------------------------------------------------------
    // Step 5: Sick Leave Fraction and Effective Labour Supply
    // ---------------------------------------------------------------
    phs->sick_leave_fraction = compute_sick_leave_fraction(sick_count, labour_force);
    phs->effective_labour_supply = compute_effective_labour_supply(
        labour_force,
        phs->sick_leave_fraction,
        Constants::labour_supply_impact);
}

void HealthcareModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Province-parallel modules dispatch through execute_province().
    // This fallback processes all provinces sequentially if called directly.
    for (uint32_t p = 0; p < static_cast<uint32_t>(state.provinces.size()); ++p) {
        execute_province(p, state, delta);
    }
}

// ===========================================================================
// Static Utility Functions
// ===========================================================================

float HealthcareModule::compute_passive_recovery(float access_level,
                                                  float quality_level,
                                                  float base_recovery_rate) {
    return access_level * quality_level * base_recovery_rate;
}

float HealthcareModule::compute_treatment_boost(float quality_level,
                                                 float treatment_health_boost) {
    return treatment_health_boost * quality_level;
}

float HealthcareModule::compute_overload_quality(float quality_level,
                                                  float capacity_utilisation,
                                                  float overload_threshold,
                                                  float overload_quality_penalty) {
    if (capacity_utilisation > overload_threshold) {
        return quality_level * overload_quality_penalty;
    }
    return quality_level;
}

float HealthcareModule::compute_sick_leave_fraction(uint32_t sick_count,
                                                     uint32_t labour_force) {
    if (labour_force == 0) {
        return 0.0f;
    }
    return static_cast<float>(sick_count) / static_cast<float>(labour_force);
}

float HealthcareModule::compute_effective_labour_supply(uint32_t labour_force,
                                                         float sick_leave_fraction,
                                                         float labour_supply_impact) {
    return static_cast<float>(labour_force)
         * (1.0f - sick_leave_fraction * labour_supply_impact);
}

// ===========================================================================
// Lookup Helpers
// ===========================================================================

HealthcareModule::NpcHealthRecord* HealthcareModule::find_npc_health(uint32_t npc_id) {
    for (auto& rec : npc_health_records_) {
        if (rec.npc_id == npc_id) {
            return &rec;
        }
    }
    return nullptr;
}

const HealthcareModule::NpcHealthRecord* HealthcareModule::find_npc_health(
        uint32_t npc_id) const {
    for (const auto& rec : npc_health_records_) {
        if (rec.npc_id == npc_id) {
            return &rec;
        }
    }
    return nullptr;
}

HealthcareModule::ProvinceHealthState* HealthcareModule::find_province_health(
        uint32_t province_id) {
    for (auto& phs : province_health_states_) {
        if (phs.province_id == province_id) {
            return &phs;
        }
    }
    return nullptr;
}

const HealthcareModule::ProvinceHealthState* HealthcareModule::find_province_health(
        uint32_t province_id) const {
    for (const auto& phs : province_health_states_) {
        if (phs.province_id == province_id) {
            return &phs;
        }
    }
    return nullptr;
}

}  // namespace econlife
