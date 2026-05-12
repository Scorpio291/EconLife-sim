#pragma once

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "addiction_types.h"
#include "core/config/package_config.h"
#include "core/tick/tick_module.h"

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class AddictionModule : public ITickModule {
   public:
    explicit AddictionModule(const AddictionConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "addiction"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    std::vector<std::string_view> runs_after() const override { return {"community_response"}; }
    bool is_province_parallel() const noexcept override { return true; }
    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override;
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // Seed (or replace) the per-NPC addiction state. Used by tests and by
    // the future cross-module hookup that introduces an NPC into the
    // addiction state machine (drug_economy → addiction). The state machine
    // in execute_province advances any NPC whose seeded stage != none, so
    // until this seeder is called for an NPC, that NPC is invisible to the
    // module. See docs/session_logs/flagged_issues.md for the open
    // architectural question on whether per-NPC state should live on the
    // NPC struct directly (per addiction/INTERFACE.md) rather than this
    // module-private map.
    void set_addiction_state(uint32_t npc_id, AddictionState state) {
        addiction_states_[npc_id] = std::move(state);
    }
    const AddictionState* find_addiction_state(uint32_t npc_id) const {
        auto it = addiction_states_.find(npc_id);
        return it == addiction_states_.end() ? nullptr : &it->second;
    }

    // --- Static utilities for testing ---
    static AddictionStage compute_next_stage(const AddictionState& state,
                                             const AddictionConfig& cfg = {});
    static float craving_increment(AddictionStage stage, const AddictionConfig& cfg = {});
    static float compute_withdrawal_damage(AddictionStage stage, uint32_t supply_gap_ticks,
                                           const AddictionConfig& cfg = {});
    static float compute_work_efficiency(AddictionStage stage, const AddictionConfig& cfg = {});
    static float compute_addiction_rate_delta(AddictionStage old_stage, AddictionStage new_stage,
                                              const AddictionConfig& cfg = {});
    static bool is_recovery_complete(uint32_t clean_ticks, float relapse_probability,
                                     const AddictionConfig& cfg = {});

   private:
    AddictionConfig cfg_;
    // Module-internal addiction state per NPC (not in WorldState)
    std::unordered_map<uint32_t, AddictionState> addiction_states_;
};

}  // namespace econlife
