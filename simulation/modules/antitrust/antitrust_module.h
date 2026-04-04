#pragma once

// Antitrust Module — sequential tick module that monitors market concentration
// per good per province on a monthly schedule, triggering regulatory responses
// when actors exceed antitrust thresholds.
//
// See docs/interfaces/antitrust/INTERFACE.md for the canonical specification.

#include <cstdint>
#include <map>
#include <string_view>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "modules/antitrust/antitrust_types.h"

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;
struct NPC;
struct RegionalMarket;
struct NPCBusiness;

// ---------------------------------------------------------------------------
// AntitrustModule — ITickModule implementation
// ---------------------------------------------------------------------------
class AntitrustModule : public ITickModule {
   public:
    explicit AntitrustModule(AntitrustConfig cfg = {}) : cfg_(std::move(cfg)) {}

    std::string_view name() const noexcept override { return "antitrust"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    bool is_province_parallel() const noexcept override { return false; }

    std::vector<std::string_view> runs_after() const override {
        return {"evidence", "facility_signals", "production", "price_engine"};
    }

    std::vector<std::string_view> runs_before() const override {
        return {"political_cycle", "legal_process"};
    }

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Module-internal state ---
    // Antitrust proposal pressure per province
    std::map<uint32_t, float>& proposal_pressure() { return proposal_pressure_; }
    const std::map<uint32_t, float>& proposal_pressure() const { return proposal_pressure_; }

    // Track when next monthly check fires
    uint32_t& next_check_tick() { return next_check_tick_; }

    // Generated proposals
    std::vector<AntitrustProposal>& proposals() { return proposals_; }
    const std::vector<AntitrustProposal>& proposals() const { return proposals_; }

    // --- Static utility functions (exposed for testing) ---

    // Compute actor supply share for a good in a province.
    // Returns 0.0 if total_supply <= 0.0.
    static float compute_supply_share(float actor_output, float total_supply);

    // Check if supply share triggers Tier 1 (preliminary inquiry).
    bool is_tier1_triggered(float supply_share) const;

    // Check if supply share triggers Tier 2 (dominant price-mover).
    bool is_tier2_triggered(float supply_share) const;

    // Compute meter fill increment for Tier 1 actors.
    float compute_meter_fill_increment() const;

    // Compute proposal pressure increment for Tier 2 actors.
    float compute_pressure_increment() const;

    // Compute proposal pressure decay when no dominant actor.
    float compute_pressure_decay() const;

    // Check if proposal pressure has crossed the auto-generation threshold.
    bool should_generate_proposal(float pressure) const;

   private:
    AntitrustConfig cfg_;
    std::map<uint32_t, float> proposal_pressure_;
    uint32_t next_check_tick_ = 30;
    std::vector<AntitrustProposal> proposals_;
    uint32_t next_proposal_id_ = 1000;

    // Run the monthly antitrust scan.
    void run_monthly_check(const WorldState& state, DeltaBuffer& delta);
};

}  // namespace econlife
