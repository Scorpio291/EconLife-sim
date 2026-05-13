// Smoke tests for TechnologyModule. The module runs sequentially and is
// responsible for era transitions, maturation advance, and domain-knowledge
// decay. The catalog-loading path is tested separately in
// technology_catalog_test.cpp; here we exercise execute().

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/apply_deltas.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"
#include "modules/technology/technology_module.h"
#include "modules/technology/technology_types.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

namespace {

WorldState make_world_with_tech(SimulationEra era, uint32_t current_tick) {
    WorldState w{};
    w.current_tick = current_tick;
    w.world_seed = 1;
    w.game_mode = GameMode::standard;
    w.technology.current_era = era;
    w.technology.era_started_tick = 0;

    Province p{};
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    p.id = 0;
    w.provinces.push_back(p);
    rebuild_npc_indices(w);
    return w;
}

}  // namespace

TEST_CASE("Technology: domain_knowledge decay emits TechnologyDelta with negative delta",
          "[technology][tier1]") {
    auto state = make_world_with_tech(SimulationEra::era_1_turn_of_millennium, /*tick=*/1);
    // Seed domain_knowledge high enough that the per-tick decay
    // (current * knowledge_decay_rate; default 0.0001) clears the
    // skip-negligible-decay threshold (|decay| > 0.0001, strictly).
    // 1.5 * 0.0001 = 0.00015 > 0.0001 ✓.
    state.technology.domain_knowledge[0] = 1.5f;
    state.technology.domain_knowledge[1] = 1.5f;

    TechnologyModule module;
    DeltaBuffer delta{};
    module.execute(state, delta);

    // At least one TechnologyDelta with a negative domain_knowledge_delta.
    bool found_negative_decay = false;
    for (const auto& td : delta.technology_deltas) {
        if (td.domain_index.has_value() && td.domain_knowledge_delta.has_value() &&
            *td.domain_knowledge_delta < 0.0f) {
            found_negative_decay = true;
            break;
        }
    }
    REQUIRE(found_negative_decay);
}

TEST_CASE("Technology: zero domain_knowledge produces no decay delta", "[technology][tier1]") {
    auto state = make_world_with_tech(SimulationEra::era_1_turn_of_millennium, /*tick=*/1);
    // All domain_knowledge entries default to 0.0 — module should emit no
    // decay deltas for them.

    TechnologyModule module;
    DeltaBuffer delta{};
    module.execute(state, delta);

    for (const auto& td : delta.technology_deltas) {
        if (td.domain_index.has_value() && td.domain_knowledge_delta.has_value()) {
            // The only non-zero deltas allowed at this point are era-related
            // (new_era replacements), not domain decay. Decay deltas for
            // domains we never seeded must not appear.
            INFO("Unexpected decay delta for domain_index " << static_cast<int>(*td.domain_index));
            REQUIRE_FALSE(td.domain_knowledge_delta.value() < 0.0f);
        }
    }
}

TEST_CASE("Technology: module name and dependencies match spec", "[technology][tier1]") {
    TechnologyModule module;
    REQUIRE(module.name() == "technology");
    REQUIRE(module.package_id() == "base_game");
    REQUIRE_FALSE(module.is_province_parallel());

    auto after = module.runs_after();
    REQUIRE(after.size() == 1);
    REQUIRE(after[0] == "calendar");
    auto before = module.runs_before();
    REQUIRE(before.size() == 1);
    REQUIRE(before[0] == "production");
}
