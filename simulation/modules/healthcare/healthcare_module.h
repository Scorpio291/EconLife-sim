#pragma once

// Healthcare Module — province-parallel tick module that processes NPC health
// changes each tick: passive recovery, treatment for critical NPCs, hospital
// capacity tracking, quality degradation when overloaded, and sick leave
// computation for labour force participation.
//
// See docs/interfaces/healthcare/INTERFACE.md for the canonical specification.

#include <string_view>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "modules/healthcare/healthcare_types.h"

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;
struct NPC;

// ---------------------------------------------------------------------------
// HealthcareModule — ITickModule implementation for NPC health processing
// ---------------------------------------------------------------------------
class HealthcareModule : public ITickModule {
   public:
    explicit HealthcareModule(const HealthcareConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "healthcare"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    std::vector<std::string_view> runs_after() const override { return {"financial_distribution"}; }

    // End of Pass 1 chain; no current-tick dependents.
    std::vector<std::string_view> runs_before() const override { return {}; }

    bool is_province_parallel() const noexcept override { return true; }

    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override;

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Internal per-province healthcare state ---
    // Module owns this because WorldState Province does not carry
    // HealthcareProfile in the current V1 bootstrap schema. Injected
    // during module initialization or from tests.
    struct ProvinceHealthState {
        uint32_t province_id;
        HealthcareProfile profile;
        float sick_leave_fraction = 0.0f;
        float effective_labour_supply = 0.0f;
    };

    // --- NPC health state (module-internal) ---
    // WorldState NPC struct does not have a health field in V1 bootstrap.
    // The healthcare module tracks health internally.
    struct NpcHealthRecord {
        uint32_t npc_id;
        float health = 1.0f;
        uint32_t last_treatment_tick = 0;
    };

    // --- State access (for test injection and module initialization) ---

    std::vector<ProvinceHealthState>& province_health_states() { return province_health_states_; }
    const std::vector<ProvinceHealthState>& province_health_states() const {
        return province_health_states_;
    }

    std::vector<NpcHealthRecord>& npc_health_records() { return npc_health_records_; }
    const std::vector<NpcHealthRecord>& npc_health_records() const { return npc_health_records_; }

    // Find NPC health record by id. Returns nullptr if not found.
    NpcHealthRecord* find_npc_health(uint32_t npc_id);
    const NpcHealthRecord* find_npc_health(uint32_t npc_id) const;

    // Find province health state by id. Returns nullptr if not found.
    ProvinceHealthState* find_province_health(uint32_t province_id);
    const ProvinceHealthState* find_province_health(uint32_t province_id) const;

    // --- Static utility functions exposed for testing ---

    // Compute passive health recovery delta.
    static float compute_passive_recovery(float access_level, float quality_level,
                                          float base_recovery_rate);

    // Compute treatment health boost.
    static float compute_treatment_boost(float quality_level, float treatment_health_boost);

    // Compute quality level after potential overload degradation.
    static float compute_overload_quality(float quality_level, float capacity_utilisation,
                                          float overload_threshold, float overload_quality_penalty);

    // Compute sick leave fraction (handles zero labour_force).
    static float compute_sick_leave_fraction(uint32_t sick_count, uint32_t labour_force);

    // Compute effective labour supply after sick leave impact.
    static float compute_effective_labour_supply(uint32_t labour_force, float sick_leave_fraction,
                                                 float labour_supply_impact);

   private:
    HealthcareConfig cfg_;
    std::vector<ProvinceHealthState> province_health_states_;
    std::vector<NpcHealthRecord> npc_health_records_;
};

}  // namespace econlife
