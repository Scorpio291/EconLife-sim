#include "alternative_identity_module.h"

#include <algorithm>
#include <cmath>

#include "core/world_state/player.h"
#include "core/world_state/world_state.h"

namespace econlife {

float AlternativeIdentityModule::decay_documentation_quality(float current, float decay_rate) {
    return std::max(0.0f, current - decay_rate);
}

float AlternativeIdentityModule::build_documentation_quality(float current, float build_rate) {
    return std::min(1.0f, current + build_rate);
}

bool AlternativeIdentityModule::should_burn_identity(float quality, float threshold) {
    return quality < threshold;
}

float AlternativeIdentityModule::compute_witness_discovery_confidence() {
    return WITNESS_CONFIDENCE;
}

float AlternativeIdentityModule::compute_forensic_discovery_confidence() {
    return FORENSIC_CONFIDENCE;
}

void AlternativeIdentityModule::execute(const WorldState& state, DeltaBuffer& delta) {
    std::sort(identities_.begin(), identities_.end(),
              [](const AlternativeIdentity& a, const AlternativeIdentity& b) {
                  return a.alias_id < b.alias_id;
              });

    for (auto& identity : identities_) {
        if (identity.status == IdentityStatus::burned || identity.status == IdentityStatus::retired)
            continue;

        if (state.current_tick > identity.last_maintenance_tick) {
            uint32_t ticks_since = state.current_tick - identity.last_maintenance_tick;
            identity.documentation_quality = decay_documentation_quality(
                identity.documentation_quality, DOCUMENTATION_DECAY_RATE * ticks_since);
        }

        if (identity.status == IdentityStatus::active) {
            // EvidenceDelta: redirect evidence tokens that target real_actor_id to the
            // cover alias instead. For each attributed evidence token, reduce its
            // actionability toward the real actor (cover absorbs investigative pressure).
            for (const auto& token : state.evidence_pool) {
                if (token.is_active && token.target_npc_id == identity.real_actor_id &&
                    identity.documentation_quality > BURN_THRESHOLD) {
                    // Reduce actionability in proportion to documentation quality
                    EvidenceDelta redirect_ev;
                    redirect_ev.retired_token_id = token.id;  // retire original
                    // Re-emit token pointing at alias_id with quality-scaled actionability
                    redirect_ev.new_token =
                        EvidenceToken{0,
                                      token.type,
                                      token.source_npc_id,
                                      identity.alias_id,
                                      token.actionability * identity.documentation_quality,
                                      token.decay_rate,
                                      state.current_tick,
                                      token.province_id,
                                      true};
                    delta.evidence_deltas.push_back(redirect_ev);
                }
            }

            // NPCDelta: identity maintenance costs (documents, bribes, upkeep)
            // Per-tick cost scales with documentation quality (higher quality = more upkeep)
            constexpr float MAINTENANCE_COST_PER_TICK = 50.0f;
            NPCDelta cost_delta;
            cost_delta.npc_id = identity.real_actor_id;
            cost_delta.capital_delta =
                -(MAINTENANCE_COST_PER_TICK * identity.documentation_quality);
            delta.npc_deltas.push_back(cost_delta);
        }

        if (identity.status == IdentityStatus::active &&
            should_burn_identity(identity.documentation_quality, BURN_THRESHOLD)) {
            identity.status = IdentityStatus::burned;
            identity.burn_tick = state.current_tick;

            EvidenceDelta ev;
            ev.new_token = EvidenceToken{0,
                                         EvidenceType::documentary,
                                         identity.real_actor_id,
                                         identity.alias_id,
                                         0.40f,
                                         0.002f,
                                         state.current_tick,
                                         0,
                                         true};
            delta.evidence_deltas.push_back(ev);
        }
    }
}

}  // namespace econlife
