#pragma once

// Evidence Module — sequential tick module that manages evidence token lifecycle:
// actionability decay, creation from observable actions, and propagation through
// the NPC knowledge graph.
//
// See docs/interfaces/evidence/INTERFACE.md for the canonical specification.

#include <cstdint>
#include <string_view>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "modules/evidence/evidence_types.h"

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;
struct NPC;
struct EvidenceToken;
struct NPCBusiness;

// ---------------------------------------------------------------------------
// EvidenceModule — ITickModule implementation for evidence lifecycle
// ---------------------------------------------------------------------------
class EvidenceModule : public ITickModule {
   public:
    explicit EvidenceModule(const EvidenceConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "evidence"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    bool is_province_parallel() const noexcept override { return false; }

    std::vector<std::string_view> runs_after() const override { return {"npc_behavior"}; }

    std::vector<std::string_view> runs_before() const override {
        return {"facility_signals", "investigator_engine", "media_system"};
    }

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Static utility functions (exposed for testing) ---

    // Compute the decay amount for one batch interval.
    // credible: decay = base_decay_rate * batch_interval
    // discredited: decay = base_decay_rate * discredit_multiplier * batch_interval
    static float compute_decay_amount(float base_decay_rate, bool is_credible,
                                      float discredit_multiplier, uint32_t batch_interval);

    // Apply decay to current actionability, clamping to [floor, 1.0].
    static float apply_actionability_decay(float current_actionability, float decay_amount,
                                           float actionability_floor);

    // Evaluate whether an NPC is a credible evidence holder.
    // is_credible = (public_credibility >= threshold)
    static bool evaluate_holder_credibility(float public_credibility, float credibility_threshold);

    // Compute propagation confidence when evidence is shared.
    // received_confidence = sharer_confidence * trust_factor
    static float compute_propagation_confidence(float sharer_confidence, float relationship_trust,
                                                float trust_factor_min, float trust_factor_max);

    // Normalize relationship trust [-1.0, 1.0] to trust factor [0.1, 1.0].
    static float normalize_trust_to_factor(float trust, float trust_factor_min,
                                           float trust_factor_max);

    // Check if NPC can share evidence with player based on trust threshold.
    static bool can_share_with_player(float trust, float share_threshold);

    // --- Internal evidence tracking ---
    // Module-internal next_token_id counter for new evidence creation.
    uint32_t& next_token_id() { return next_token_id_; }

   private:
    EvidenceConfig cfg_;
    uint32_t next_token_id_ = 1000;  // start above any pre-seeded tokens

    // Process actionability decay for tokens due this tick.
    void process_decay_batches(const WorldState& state, DeltaBuffer& delta);

    // Create new evidence from criminal businesses and regulatory violations.
    void create_evidence_from_businesses(const WorldState& state, DeltaBuffer& delta);
};

}  // namespace econlife
