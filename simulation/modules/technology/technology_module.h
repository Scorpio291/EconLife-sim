#pragma once

// Technology Module — tick module that manages era transitions, technology
// maturation advancement, domain knowledge decay, and research project progress.
//
// Runs after calendar (needs current tick/date) and before production
// (production reads maturation_of() for quality ceiling).
//
// See docs/design/EconLife_RnD_and_Technology_v22.md for canonical spec.

#include <string>
#include <vector>

#include "core/config/package_config.h"  // RndConfig
#include "core/tick/tick_module.h"
#include "core/world_gen/technology_catalog.h"
#include "modules/technology/technology_types.h"

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;

class TechnologyModule : public ITickModule {
   public:
    // Default constructor: uses TechnologyConfig spec-correct defaults.
    TechnologyModule() = default;

    // Config constructor: maps RndConfig fields to TechnologyConfig.
    explicit TechnologyModule(const RndConfig& rnd) {
        config_.maturation_rate_coeff         = rnd.maturation_rate_coeff;
        config_.maturation_difficulty_per_level = rnd.maturation_difficulty_per_level;
        config_.base_research_success_rate    = rnd.base_research_success_rate;
        config_.domain_knowledge_bonus_coeff  = rnd.domain_knowledge_bonus_coeff;
        config_.unexpected_discovery_probability = rnd.unexpected_discovery_probability;
        config_.patent_preemption_check_rate  = rnd.patent_preemption_check_rate;
        config_.knowledge_decay_rate          = rnd.knowledge_decay_rate;
        config_.era_transition_threshold      = rnd.era_transition_threshold;
        config_.patent_duration_ticks         = rnd.patent_duration_ticks;
    }

    std::string_view name() const noexcept override { return "technology"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    std::vector<std::string_view> runs_after() const override { return {"calendar"}; }
    std::vector<std::string_view> runs_before() const override { return {"production"}; }

    bool is_province_parallel() const noexcept override { return false; }

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // Initialize from WorldState on first tick (lazy init like ProductionModule).
    void init_from_world_state(const WorldState& state);

    // --- Configuration access ---
    TechnologyConfig& config() { return config_; }
    const TechnologyConfig& config() const { return config_; }

    // --- Catalog access (for test injection) ---
    TechnologyCatalog& catalog() { return catalog_; }
    const TechnologyCatalog& catalog() const { return catalog_; }

   private:
    TechnologyConfig config_;
    TechnologyCatalog catalog_;
    bool initialized_ = false;

    // --- Per-tick processing ---

    // Evaluate era transition conditions and advance if threshold met.
    void check_era_transition(const WorldState& state, DeltaBuffer& delta);

    // Advance maturation for all active maturation projects.
    void advance_maturation(const WorldState& state, DeltaBuffer& delta);

    // Decay domain knowledge levels.
    void decay_domain_knowledge(const WorldState& state, DeltaBuffer& delta);

    // Update maturation ceilings for all tech holdings based on current era.
    void update_maturation_ceilings(const WorldState& state, DeltaBuffer& delta);

    // --- Helpers ---
    float compute_calendar_year(uint32_t tick, uint32_t base_year) const;
    float compute_era_transition_score(const WorldState& state, uint8_t target_era) const;
};

}  // namespace econlife
