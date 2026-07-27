#pragma once

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "political_cycle_types.h"

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class PoliticalCycleModule : public ITickModule {
   public:
    explicit PoliticalCycleModule(const PoliticalCycleConfig& cfg = {});

    std::string_view name() const noexcept override { return "political_cycle"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    std::vector<std::string_view> runs_after() const override { return {"community_response"}; }
    bool is_province_parallel() const noexcept override { return false; }
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // Module-private state persistence (schema v25 participant). Covers the
    // whole of political_state_ — offices, campaigns, proposals, nation_unrest —
    // plus the one-shot formation guard. Without these the office topology
    // re-seeded from world-gen data on every load (killing elections outright,
    // see the self-healing due-tick logic in execute) and the autocratic
    // repression ratchet reset, which restarted the FailedState collapse clock
    // and flipped suppression back to grievance-reducing.
    // cfg_ is deliberately excluded: it is package-config reference data,
    // reloaded on startup like the technology module's era_triggers.
    void serialize_state(std::vector<uint8_t>& out) const override;
    bool deserialize_state(const uint8_t* data, size_t size) override;

    // --- Static utilities for testing ---
    static float compute_raw_vote_share(
        const std::unordered_map<std::string, float>& approval_by_demographic,
        const std::vector<DemographicWeight>& demographics);
    static float compute_resource_modifier(float resource_deployment, float resource_scale,
                                           float resource_max_effect);
    static float compute_event_modifier_total(const std::vector<float>& event_modifiers,
                                              float event_modifier_cap);
    static float compute_final_vote_share(float raw_share, float resource_modifier,
                                          float event_total);
    static float compute_legislator_support(float motivation_alignment, float obligation_bonus,
                                            float constituency_pressure);
    static bool compute_vote_passed(float votes_for, float votes_against, float majority_threshold);

    // National legitimacy target from one province's conditions (the
    // population-weighted mean of this across a nation's provinces, EMA-smoothed,
    // is the nation's legitimacy). Clamped to [0,1].
    static float compute_legitimacy_target(float institutional_trust, float stability,
                                           float grievance, float unemployment,
                                           const PoliticalCycleConfig& cfg);

    // Net per-province grievance change from one autocratic crackdown: the
    // accumulated martyr floor minus the short-term dispersal cut. Negative on
    // early crackdowns (force quells), turns positive once the floor exceeds the
    // dispersal — the "tighten your grip" ratchet.
    static float compute_suppression_net_grievance(float repression_grievance_floor,
                                                   float suppression_immediate);

    // Add each endorsement's approval_bonus to its demographic in `approval`,
    // clamped to [0,1]. (Endorsements come from NPC endorsers; producer is a
    // future extension, but the application logic is exercised here.)
    static void apply_endorsement_bonuses(std::unordered_map<std::string, float>& approval,
                                          const std::vector<Endorsement>& endorsements);

    // Test/inspection accessors for the module-private election state.
    PoliticalCycleModuleState& state() { return political_state_; }
    const PoliticalCycleModuleState& state() const { return political_state_; }

   private:
    // National legitimacy roll-up + regime-differentiated unrest response.
    // Aggregates each nation's provinces into national_legitimacy and, when a
    // nation is in legitimacy crisis, fires the regime-appropriate monthly
    // response (democratic concession+turnover / autocratic suppression /
    // failed-state fragmentation). See docs/design/EconLife_Unrest_*.md.
    void process_national_unrest(const WorldState& state, DeltaBuffer& delta);

    // Get-or-create the per-nation unrest state (suppression history).
    NationUnrestState& unrest_state_for(uint32_t nation_id);

    // Seed one governor office per province from world-gen data on first execute
    // (idempotent; skipped if offices already present). Deterministic.
    void form_offices(const WorldState& state);

    // Auto-activate a campaign for any office whose election is within the
    // campaign lead-time window and has no active campaign yet (candidate =
    // incumbent, approval copied from the office baseline + endorsements).
    void activate_campaigns(const WorldState& state);

    PoliticalCycleConfig cfg_;
    PoliticalCycleModuleState political_state_;
    // One-shot formation guard. Persisted since schema v25 along with the
    // offices themselves: a run that had already formed its offices must not
    // re-form them after a load, or a world whose offices were legitimately
    // emptied would silently grow a fresh set and diverge from the
    // uninterrupted run.
    bool formed_ = false;
};

}  // namespace econlife
