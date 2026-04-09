#pragma once

// investigator_engine module header.
// Province-parallel execution: each province's investigators operate independently.
//
// Processes all LE, regulator, journalist, and NGO investigator NPCs each tick.
// Updates InvestigatorMeter levels from regional facility signals and evidence,
// derives status thresholds, resolves investigation targets, and advances
// case-building toward prosecution.
//
// See docs/interfaces/investigator_engine/INTERFACE.md for the canonical specification.

#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "investigator_engine_types.h"

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;
struct NPC;
struct NPCBusiness;
struct FacilitySignals;

class InvestigatorEngineModule : public ITickModule {
   public:
    explicit InvestigatorEngineModule(const InvestigatorEngineConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "investigator_engine"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    std::vector<std::string_view> runs_after() const override {
        return {"criminal_operations", "facility_signals", "evidence"};
    }

    std::vector<std::string_view> runs_before() const override {
        return {"legal_process", "informant_system"};
    }

    bool is_province_parallel() const noexcept override { return true; }

    void init_for_tick(const WorldState& state) override;

    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override;

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Static utility functions (public for testing) ---

    // Compute regional signal from criminal facilities in a province
    // regional_signal = sum(net_signal) / facility_count_normalizer
    static float compute_regional_signal(const std::vector<float>& facility_net_signals,
                                         float facility_count_normalizer);

    // Compute fill rate from regional signal
    // fill_rate = clamp(regional_signal * detection_to_fill_rate_scale, 0.0, fill_rate_max)
    static float compute_fill_rate(float regional_signal, float detection_to_fill_rate_scale,
                                   float fill_rate_max);

    // Apply corruption reduction to fill rate
    // fill_rate *= (1.0 - corruption_susceptibility * regional_corruption_coverage)
    static float apply_corruption_modifier(float fill_rate, float corruption_susceptibility,
                                           float regional_corruption_coverage);

    // Derive investigator status from meter level
    static uint8_t derive_status(float current_level, float surveillance_threshold,
                                 float formal_inquiry_threshold, float raid_threshold);

    // Resolve target: argmax of known criminal actors' signal contribution
    static uint32_t resolve_target(
        const std::vector<std::pair<uint32_t, float>>& actor_signal_contributions);

    // Compute meter decay when detection risk is zero
    static float compute_decay(float current_level, float decay_rate);

   private:
    InvestigatorEngineConfig cfg_;
    // Internal state: per-investigator case tracking
    std::vector<InvestigationCase> cases_;
};

}  // namespace econlife
