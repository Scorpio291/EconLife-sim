#pragma once

#include <vector>

#include "core/tick/tick_module.h"
#include "legal_process_types.h"

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class LegalProcessModule : public ITickModule {
   public:
    std::string_view name() const noexcept override { return "legal_process"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    std::vector<std::string_view> runs_after() const override { return {"investigator_engine"}; }
    std::vector<std::string_view> runs_before() const override { return {"informant_system"}; }
    bool is_province_parallel() const noexcept override { return false; }
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Static utilities ---
    static float compute_conviction_probability(float evidence_weight, float defense_quality,
                                                float judge_bias, float witness_reliability);
    static uint32_t compute_sentence_ticks(CaseSeverity severity,
                                           uint32_t ticks_per_severity_level);
    static bool is_double_jeopardy_active(uint32_t current_tick, uint32_t cooldown_until);
    static LegalCaseStage advance_stage(LegalCaseStage current, bool conviction);
    static float compute_evidence_weight(const std::vector<float>& token_actionabilities);

    static constexpr float DEFENSE_QUALITY_FACTOR = 0.40f;
    static constexpr uint32_t TICKS_PER_SEVERITY = 365;
    static constexpr uint32_t DOUBLE_JEOPARDY_COOLDOWN = 1825;
    static constexpr float CONVICTION_THRESHOLD = 0.50f;

   private:
    std::vector<LegalCase> cases_;
};

}  // namespace econlife
