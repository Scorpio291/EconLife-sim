#pragma once

#include <algorithm>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "regional_conditions_types.h"

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class RegionalConditionsModule : public ITickModule {
   public:
    explicit RegionalConditionsModule(const RegionalConditionsConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "regional_conditions"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    std::vector<std::string_view> runs_after() const override {
        return {"political_cycle", "influence_network"};
    }
    bool is_province_parallel() const noexcept override { return true; }
    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override;
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Static utilities for testing ---
    static float compute_stability_recovery(float current_stability, uint32_t instability_events);
    static float compute_criminal_dominance(float criminal_revenue, float total_revenue);
    static float compute_drought_recovery(float current_modifier, float recovery_rate);
    static float compute_inequality_from_gini(float gini_coefficient);
    // Significant-NPC wealth concentration: top-decile capital share, normalized
    // so an equal distribution -> 0 and full concentration in the top decile -> 1.
    // Captures the wealth gap the cohort *income* gini is blind to (a single owner
    // accumulating capital does not move income gini, but does move this). Mutates
    // the input vector (sorts it) for in-place efficiency.
    static float compute_wealth_concentration(std::vector<float>& capitals);
    // Per-capita rate from a count and a population (0 if population is 0).
    static float compute_population_rate(uint32_t count, uint32_t population);
    // Size-weighted mean employment_rate across cohorts (0 if no population).
    static float compute_formal_employment_rate(float weighted_employment, uint32_t total_size);
    // Mean (1 - violation) across non-criminal facilities; 1.0 if none (vacuously clean).
    static float compute_regulatory_compliance(float compliance_sum, uint32_t facility_count);
    // Quarterly EMA smoothing of criminal dominance: (1-alpha)*prev + alpha*ratio.
    static float compute_dominance_ema(float prev, float ratio, float alpha);

   private:
    RegionalConditionsConfig cfg_;
};

}  // namespace econlife
