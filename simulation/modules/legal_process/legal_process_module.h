#pragma once

#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "legal_process_types.h"

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class LegalProcessModule : public ITickModule {
   public:
    explicit LegalProcessModule(const LegalProcessConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "legal_process"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    std::vector<std::string_view> runs_after() const override { return {"investigator_engine"}; }
    std::vector<std::string_view> runs_before() const override { return {"informant_system"}; }
    bool is_province_parallel() const noexcept override { return false; }
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // Persistence (schema v7): cases_ holds genuine run state — open
    // proceedings mid-trial, paroled-but-not-released NPCs, double-jeopardy
    // cooldowns. See ITickModule.
    void serialize_state(std::vector<uint8_t>& out) const override;
    bool deserialize_state(const uint8_t* data, size_t size) override;

    // Test helpers.
    const std::vector<LegalCase>& cases() const { return cases_; }
    std::vector<LegalCase>& cases_mut() { return cases_; }

    // --- Static utilities ---
    static float compute_conviction_probability(float evidence_weight, float defense_quality,
                                                float judge_bias, float witness_reliability,
                                                float defense_quality_factor);
    static uint32_t compute_sentence_ticks(CaseSeverity severity,
                                           uint32_t ticks_per_severity_level);
    static bool is_double_jeopardy_active(uint32_t current_tick, uint32_t cooldown_until);
    static LegalCaseStage advance_stage(LegalCaseStage current, bool conviction);
    static float compute_evidence_weight(const std::vector<float>& token_actionabilities);

    // --- v7 state-machine transition gates (legal_process INTERFACE.md) ---

    // investigation -> arrested: evidence has reached the threshold for an
    // arrest. Spec also calls for the lead investigator's meter to be at
    // raid_imminent ("critical"); that check lives at the call site since
    // InvestigatorMeter records are not on WorldState yet (deferred — see
    // flagged_issues).
    static bool should_arrest(float evidence_weight, float arrest_evidence_threshold);

    // arrested/charged -> acquitted: evidence has decayed below the dismissal
    // threshold. Closes the case without trial.
    static bool should_dismiss(float evidence_weight, float dismissal_evidence_threshold);

    // arrested -> charged: enough time has elapsed since arrest AND evidence
    // has held above the charge threshold. Acquitted via should_dismiss
    // before this triggers.
    static bool should_charge(uint32_t current_tick, uint32_t stage_entered_tick,
                              uint32_t investigation_to_charge_ticks, float evidence_weight,
                              float charge_evidence_threshold);

    // charged -> trial: enough time has elapsed since charges were filed.
    static bool should_proceed_to_trial(uint32_t current_tick, uint32_t stage_entered_tick,
                                        uint32_t charge_to_trial_ticks);

    // convicted -> imprisoned (true) or convicted -> fined (false). Custodial
    // floor is in CaseSeverity terms: severity_value (= enum + 1) >= floor.
    static bool is_custodial(CaseSeverity severity, uint32_t custodial_sentence_severity_floor);

    // imprisoned -> paroled eligibility based on fraction of sentence served.
    static bool is_parole_eligible(uint32_t current_tick, uint32_t release_tick,
                                   uint32_t sentence_ticks, float parole_eligibility_fraction);

    // Fine outcome amount for severity < floor. Linear in severity (severity 1
    // = base, severity 2 = 2x base, etc.).
    static float compute_fine_amount(CaseSeverity severity, float fine_amount_per_severity);

   private:
    LegalProcessConfig cfg_;
    std::vector<LegalCase> cases_;
};

}  // namespace econlife
