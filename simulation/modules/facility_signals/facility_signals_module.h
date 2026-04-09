#pragma once

// Facility Signals Module — province-parallel tick module that computes
// observable signal composites for facilities, applies scrutiny mitigation,
// and feeds results into investigator and regulator meters.
//
// See docs/interfaces/facility_signals/INTERFACE.md for the canonical specification.

#include <cstdint>
#include <string_view>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "modules/facility_signals/facility_signals_types.h"

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
    explicit FacilitySignalsModule(FacilitySignalsConfig cfg = {}) : cfg_(std::move(cfg)) {}

    std::string_view name() const noexcept override { return "facility_signals"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    bool is_province_parallel() const noexcept override { return true; }

    std::vector<std::string_view> runs_after() const override { return {"evidence"}; }

    std::vector<std::string_view> runs_before() const override { return {"investigator_engine"}; }

    void init_for_tick(const WorldState& state) override;
    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override;
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Module-internal facility signal storage ---
    std::vector<FacilitySignals>& facility_signals() { return facility_signals_; }
    const std::vector<FacilitySignals>& facility_signals() const { return facility_signals_; }

    // --- Static utility functions (exposed for testing) ---

    // Compute base signal composite from four dimensions and weights.
    static float compute_signal_composite(float power, float chemical, float traffic,
                                          float olfactory,
                                          const FacilityTypeSignalWeights& weights);

    // Compute net signal after mitigation.
    static float compute_net_signal(float base_composite, float scrutiny_mitigation);

    // Compute LE fill rate from aggregate regional criminal signal.
    static float compute_le_fill_rate(float regional_signal, float detection_scale,
                                      float fill_rate_max);

    // Determine investigator meter status from current level.
    InvestigatorMeterStatus evaluate_investigator_status(float current_level) const;

    // Determine regulator meter status from current level.
    RegulatorMeterStatus evaluate_regulator_status(float current_level) const;

    // Apply corruption to fill rate.
    static float apply_corruption_to_fill_rate(float fill_rate, float corruption_susceptibility,
                                               float regional_corruption_coverage);

   private:
    FacilitySignalsConfig cfg_;
    std::vector<FacilitySignals> facility_signals_;
};

}  // namespace econlife
