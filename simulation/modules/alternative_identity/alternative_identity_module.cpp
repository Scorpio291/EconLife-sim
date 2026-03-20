#include "alternative_identity_module.h"
#include "core/world_state/world_state.h"
#include "core/world_state/player.h"
#include <algorithm>
#include <cmath>

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
        if (identity.status == IdentityStatus::burned ||
            identity.status == IdentityStatus::retired) continue;

        if (state.current_tick > identity.last_maintenance_tick) {
            uint32_t ticks_since = state.current_tick - identity.last_maintenance_tick;
            identity.documentation_quality = decay_documentation_quality(
                identity.documentation_quality, DOCUMENTATION_DECAY_RATE * ticks_since);
        }

        if (identity.status == IdentityStatus::active &&
            should_burn_identity(identity.documentation_quality, BURN_THRESHOLD)) {
            identity.status = IdentityStatus::burned;
            identity.burn_tick = state.current_tick;

            EvidenceDelta ev;
            ev.new_token = EvidenceToken{
                0, EvidenceType::documentary,
                identity.real_actor_id, identity.alias_id,
                0.40f, 0.002f,
                state.current_tick, 0, true
            };
            delta.evidence_deltas.push_back(ev);
        }
    }
}

}  // namespace econlife
