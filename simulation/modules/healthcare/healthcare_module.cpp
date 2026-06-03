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

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "core/world_state/apply_deltas.h"  // lookup_npc_by_id
#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"  // PlayerCharacter complete type
#include "core/world_state/world_state.h"

namespace econlife {

// ===========================================================================
// HealthcareModule — tick execution
// ===========================================================================

void HealthcareModule::execute_province(uint32_t province_idx, const WorldState& state,
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
        const NPC* npc = lookup_npc_by_id(state, record.npc_id);
        if (!npc)
            continue;
        if (npc->current_province_id != province_idx)
            continue;
        if (npc->status != NPCStatus::active)
            continue;

        province_npcs.push_back(&record);
    }

    // Sort by npc_id ascending (canonical order per CLAUDE.md).
    std::sort(
        province_npcs.begin(), province_npcs.end(),
        [](const NpcHealthRecord* a, const NpcHealthRecord* b) { return a->npc_id < b->npc_id; });

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
        const NPC* npc = lookup_npc_by_id(state, hr->npc_id);
        if (!npc)
            continue;

        // ---------------------------------------------------------------
        // Step 1: Passive Health Recovery
        // ---------------------------------------------------------------
        float recovery = compute_passive_recovery(profile.access_level, profile.quality_level,
                                                  cfg_.base_recovery_rate);

        hr->health += recovery;

        // ---------------------------------------------------------------
        // Step 2: Treatment for critically ill NPCs
        // ---------------------------------------------------------------
        if (hr->health < cfg_.critical_health_threshold && profile.access_level > 0.0f &&
            npc->capital >= profile.cost_per_treatment) {
            float boost =
                compute_treatment_boost(profile.quality_level, cfg_.treatment_health_boost);

            hr->health += boost;

            // Deduct treatment cost from NPC capital.
            NPCDelta cost_delta{};
            cost_delta.npc_id = hr->npc_id;
            cost_delta.capital_delta = -profile.cost_per_treatment;
            province_delta.npc_deltas.push_back(cost_delta);

            // Update capacity utilisation.
            profile.capacity_utilisation += cfg_.capacity_per_treatment;
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

            // Notify consequence system of critical/fatal health event.
            ConsequenceDelta cons_delta{};
            cons_delta.new_consequence =
                make_consequence(hr->npc_id, ConsequenceCategory::social_consequence, 0, hr->npc_id,
                                 0, state.current_tick);
            province_delta.consequence_deltas.push_back(cons_delta);
        }

        // ---------------------------------------------------------------
        // Sick leave counting (for Step 5)
        // ---------------------------------------------------------------
        if (hr->health < cfg_.labour_impairment_threshold) {
            sick_count++;
        }
    }

    // ---------------------------------------------------------------
    // Step 4: Overload Quality Degradation
    // ---------------------------------------------------------------
    profile.quality_level =
        compute_overload_quality(profile.quality_level, profile.capacity_utilisation,
                                 cfg_.overload_threshold, cfg_.overload_quality_penalty);

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
        labour_force, phs->sick_leave_fraction, cfg_.labour_supply_impact);

    // ---------------------------------------------------------------
    // Step 6: Health Crisis + sick_rate monitor — RegionDelta
    // The province-level sick_rate (cohort_stats->sick_rate) tracks the
    // fraction of the working-age population whose health is below the
    // labour-impairment threshold (default 0.5). Per-tick sample noise is
    // smoothed by converging at SICK_RATE_CONVERGENCE_RATE rather than
    // overwriting outright. If the resulting sick fraction crosses the
    // crisis threshold, apply the stability penalty (existing behavior).
    // ---------------------------------------------------------------
    constexpr float kHealthCrisisThreshold = 0.15f;
    constexpr float SICK_RATE_CONVERGENCE_RATE = 0.05f;

    // labour_force is the number of NPCs in this province that the module
    // processed (built earlier in execute_province). Treat it as the
    // sample denominator — using significant_npcs as a proxy for the
    // working-age share of cohort_stats.total_population is acceptable
    // for V1; the stored rate converges to the true sample over many
    // ticks regardless of sampling skew.
    if (labour_force > 0) {
        const float sample_sick_fraction =
            static_cast<float>(sick_count) / static_cast<float>(labour_force);
        const float current_sick_rate = state.provinces[province_idx].cohort_stats
                                            ? state.provinces[province_idx].cohort_stats->sick_rate
                                            : 0.0f;
        const float sick_rate_delta_value =
            SICK_RATE_CONVERGENCE_RATE * (sample_sick_fraction - current_sick_rate);

        RegionDelta region_delta{};
        region_delta.region_id = state.provinces[province_idx].region_id;
        region_delta.sick_rate_delta = sick_rate_delta_value;
        if (phs->sick_leave_fraction > kHealthCrisisThreshold) {
            region_delta.stability_delta = -0.01f * phs->sick_leave_fraction;
        }
        province_delta.region_deltas.push_back(region_delta);
    } else if (phs->sick_leave_fraction > kHealthCrisisThreshold) {
        // Edge case: zero labour force but crisis flag (shouldn't happen,
        // but preserves the prior unconditional emission shape).
        RegionDelta region_delta{};
        region_delta.region_id = state.provinces[province_idx].region_id;
        region_delta.stability_delta = -0.01f * phs->sick_leave_fraction;
        province_delta.region_deltas.push_back(region_delta);
    }
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

float HealthcareModule::compute_passive_recovery(float access_level, float quality_level,
                                                 float base_recovery_rate) {
    return access_level * quality_level * base_recovery_rate;
}

float HealthcareModule::compute_treatment_boost(float quality_level, float treatment_health_boost) {
    return treatment_health_boost * quality_level;
}

float HealthcareModule::compute_overload_quality(float quality_level, float capacity_utilisation,
                                                 float overload_threshold,
                                                 float overload_quality_penalty) {
    if (capacity_utilisation > overload_threshold) {
        return quality_level * overload_quality_penalty;
    }
    return quality_level;
}

float HealthcareModule::compute_sick_leave_fraction(uint32_t sick_count, uint32_t labour_force) {
    if (labour_force == 0) {
        return 0.0f;
    }
    return static_cast<float>(sick_count) / static_cast<float>(labour_force);
}

float HealthcareModule::compute_effective_labour_supply(uint32_t labour_force,
                                                        float sick_leave_fraction,
                                                        float labour_supply_impact) {
    return static_cast<float>(labour_force) * (1.0f - sick_leave_fraction * labour_supply_impact);
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

const HealthcareModule::NpcHealthRecord* HealthcareModule::find_npc_health(uint32_t npc_id) const {
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

// ─── Persistence helpers (schema v7) ────────────────────────────────────────

namespace {

void put_u32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void put_f32(std::vector<uint8_t>& out, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    put_u32(out, bits);
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
    float f32() {
        uint32_t bits = u32();
        float v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }
};

}  // namespace

void HealthcareModule::serialize_state(std::vector<uint8_t>& out) const {
    put_u32(out, 1u);
    put_u32(out, static_cast<uint32_t>(province_health_states_.size()));
    for (const auto& p : province_health_states_) {
        put_u32(out, p.province_id);
        put_f32(out, p.profile.access_level);
        put_f32(out, p.profile.quality_level);
        put_f32(out, p.profile.cost_per_treatment);
        put_f32(out, p.profile.capacity_utilisation);
        put_f32(out, p.sick_leave_fraction);
        put_f32(out, p.effective_labour_supply);
    }
    put_u32(out, static_cast<uint32_t>(npc_health_records_.size()));
    for (const auto& n : npc_health_records_) {
        put_u32(out, n.npc_id);
        put_f32(out, n.health);
        put_u32(out, n.last_treatment_tick);
    }
}

bool HealthcareModule::deserialize_state(const uint8_t* data, size_t size) {
    Reader r{data, size};
    if (r.u32() != 1u)
        return false;
    uint32_t pc = r.u32();
    province_health_states_.clear();
    province_health_states_.reserve(pc);
    for (uint32_t i = 0; i < pc; ++i) {
        ProvinceHealthState p{};
        p.province_id = r.u32();
        p.profile.access_level = r.f32();
        p.profile.quality_level = r.f32();
        p.profile.cost_per_treatment = r.f32();
        p.profile.capacity_utilisation = r.f32();
        p.sick_leave_fraction = r.f32();
        p.effective_labour_supply = r.f32();
        if (r.error)
            return false;
        province_health_states_.push_back(p);
    }
    uint32_t nc = r.u32();
    npc_health_records_.clear();
    npc_health_records_.reserve(nc);
    for (uint32_t i = 0; i < nc; ++i) {
        NpcHealthRecord n{};
        n.npc_id = r.u32();
        n.health = r.f32();
        n.last_treatment_tick = r.u32();
        if (r.error)
            return false;
        npc_health_records_.push_back(n);
    }
    return !r.error;
}

}  // namespace econlife
