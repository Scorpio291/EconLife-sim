#pragma once

// protection_rackets module header.
// Province-parallel execution: each province's rackets operate independently
// on local businesses and criminal org presence.
//
// Processes all active ProtectionRacket records each tick: collects payment,
// escalates enforcement, accumulates community grievance, handles lifecycle.
//
// See docs/interfaces/protection_rackets/INTERFACE.md for the canonical specification.

#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "protection_rackets_types.h"

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;
struct NPCBusiness;

class ProtectionRacketsModule : public ITickModule {
   public:
    explicit ProtectionRacketsModule(const ProtectionRacketsConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "protection_rackets"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    std::vector<std::string_view> runs_after() const override { return {"criminal_operations"}; }

    std::vector<std::string_view> runs_before() const override {
        return {"community_response", "investigator_engine"};
    }

    bool is_province_parallel() const noexcept override { return true; }

    void init_for_tick(const WorldState& state) override;
    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override;

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Static utility functions (public for testing) ---

    // Compute demand per tick from business revenue
    static float compute_demand_per_tick(float business_revenue_per_tick, float demand_rate);

    // Compute community grievance contribution
    static float compute_grievance_contribution(float demand_per_tick,
                                                float grievance_per_demand_unit);

    // Compute refusal probability
    static float compute_refusal_probability(bool is_defensive_incumbent,
                                             float criminal_dominance_index,
                                             float regulatory_violation_severity,
                                             float incumbent_refuse_probability,
                                             float default_refuse_probability);

    // Determine escalation stage from ticks overdue
    static RacketEscalationStage determine_escalation_stage(uint32_t ticks_overdue,
                                                             uint32_t warning_threshold,
                                                             uint32_t property_damage_threshold,
                                                             uint32_t violence_threshold,
                                                             uint32_t abandonment_threshold);

    // Check if business can pay demand
    static bool can_business_pay(float business_cash, float demand_per_tick);

    // Compute LE fill rate multiplier during violence escalation
    static float compute_violence_le_multiplier(float base_fill_rate,
                                                float personnel_violence_multiplier);

   private:
    ProtectionRacketsConfig cfg_;
    // Internal state: racket records
    std::vector<ProtectionRacket> rackets_;
};

}  // namespace econlife
