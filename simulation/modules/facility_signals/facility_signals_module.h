#pragma once

// Facility Signals Module — province-parallel tick module that computes
// observable signal composites for facilities, applies scrutiny mitigation,
// and feeds results into investigator and regulator meters.
//
// See docs/interfaces/facility_signals/INTERFACE.md for the canonical specification.

#include "core/tick/tick_module.h"
#include "modules/facility_signals/facility_signals_types.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;
struct NPC;

// ---------------------------------------------------------------------------
// FacilitySignalsModule — ITickModule implementation
// ---------------------------------------------------------------------------
class FacilitySignalsModule : public ITickModule {
public:
    std::string_view name() const noexcept override { return "facility_signals"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    bool is_province_parallel() const noexcept override { return true; }

    std::vector<std::string_view> runs_after() const override {
        return {"evidence"};
    }

    std::vector<std::string_view> runs_before() const override {
        return {"investigator_engine"};
    }

    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override;
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Module-internal facility signal storage ---
    std::vector<FacilitySignals>& facility_signals() { return facility_signals_; }
    const std::vector<FacilitySignals>& facility_signals() const { return facility_signals_; }

    // --- Static utility functions (exposed for testing) ---

    // Compute base signal composite from four dimensions and weights.
    static float compute_signal_composite(float power, float chemical, float traffic,
                                           float olfactory, const FacilityTypeSignalWeights& weights);

    // Compute net signal after mitigation.
    static float compute_net_signal(float base_composite, float scrutiny_mitigation);

    // Compute LE fill rate from aggregate regional criminal signal.
    static float compute_le_fill_rate(float regional_signal, float detection_scale,
                                       float fill_rate_max);

    // Determine investigator meter status from current level.
    static InvestigatorMeterStatus evaluate_investigator_status(float current_level);

    // Determine regulator meter status from current level.
    static RegulatorMeterStatus evaluate_regulator_status(float current_level);

    // Apply corruption to fill rate.
    static float apply_corruption_to_fill_rate(float fill_rate, float corruption_susceptibility,
                                                float regional_corruption_coverage);

    // --- Constants ---
    struct Constants {
        static constexpr float default_weight = 0.25f;
        static constexpr float karst_mitigation_bonus = 0.10f;
        static constexpr float facility_count_normalizer = 5.0f;
        static constexpr float detection_to_fill_rate_scale = 0.005f;
        static constexpr float fill_rate_max = 0.01f;
        static constexpr float surveillance_threshold = 0.30f;
        static constexpr float formal_inquiry_threshold = 0.60f;
        static constexpr float raid_threshold = 0.80f;
        static constexpr float notice_threshold = 0.25f;
        static constexpr float audit_threshold = 0.50f;
        static constexpr float enforcement_threshold = 0.75f;
        static constexpr float meter_decay_rate = 0.001f;
        static constexpr float personnel_violence_multiplier = 3.0f;
    };

private:
    std::vector<FacilitySignals> facility_signals_;
};

}  // namespace econlife
