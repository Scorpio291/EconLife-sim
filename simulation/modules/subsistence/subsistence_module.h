#pragma once

// Subsistence Module — the commons food economy of the dawn.
//
// Before money and markets, the founding population feeds itself by working the
// land directly: foraging, subsistence farming, hunting, fishing, herding. There
// are no firms and no facilities — production is the whole province population
// applied to the province's natural capital, capped by a carrying ceiling (fixed
// land feeds only so many). This module models that directly, outside the
// firm/facility/market machinery, and records the food surplus over need —
// the master variable that later frees labour for specialists and trade.
//
// It is regime-gated: active only in eras whose economic_regime is in the
// configured set (subsistence/barter by default). In market eras it is inert, so
// the modern economy is untouched. See docs/design/
// EconLife_Origin_Economy_and_Early_Jobs_v01.md.

#include <cstdint>
#include <string_view>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class SubsistenceModule : public ITickModule {
   public:
    explicit SubsistenceModule(const SubsistenceConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "subsistence"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    // Needs the current era resolved (technology sets it); produces a signal other
    // province-level consumers can read, so run it early.
    std::vector<std::string_view> runs_after() const override { return {"technology"}; }

    bool is_province_parallel() const noexcept override { return true; }

    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override;
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Pure, testable production model ---

    // Food the province's natural capital yields when worked by `labor` people.
    // Rises with labour toward a ceiling (carrying capacity) set by natural
    // capital — diminishing returns, so fixed land caps the head count it feeds.
    static float subsistence_output(float natural_capital, float labor,
                                    const SubsistenceConfig& cfg);

    // produced / needed for `population` people. 1.0 = exactly fed.
    static float surplus_ratio(float output, uint32_t population, const SubsistenceConfig& cfg);

    // Is the commons food path active in `regime`? (regime ∈ cfg.active_regimes)
    bool regime_active(std::string_view regime) const;

    // How many of `residents` a surplus can free into Layer-2 specialists.
    // 0 when surplus <= 1; rises with surplus toward max_specialist_fraction.
    static uint32_t specialist_count(uint32_t residents, float surplus,
                                     const SubsistenceConfig& cfg);

   private:
    SubsistenceConfig cfg_;
};

}  // namespace econlife
