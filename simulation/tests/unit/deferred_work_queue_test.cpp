// DeferredWorkQueue unit tests — verify min-heap ordering and payload handling.
// All tests tagged [deferred_work][tier0].

#include <catch2/catch_test_macros.hpp>
#include <cstdint>

#include "core/tick/deferred_work.h"
#include "core/tick/drain_deferred_work.h"
#include "core/world_state/apply_deltas.h"
#include "core/world_state/consequence.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"

using namespace econlife;

TEST_CASE("empty queue has zero size", "[deferred_work][tier0]") {
    DeferredWorkQueue queue;
    REQUIRE(queue.empty());
    REQUIRE(queue.size() == 0);
}

TEST_CASE("items drain in ascending due_tick order", "[deferred_work][tier0]") {
    DeferredWorkQueue queue;

    // Push in arbitrary order.
    queue.push({10, WorkType::consequence, 1, EmptyPayload{}});
    queue.push({5, WorkType::transit_arrival, 2, EmptyPayload{}});
    queue.push({15, WorkType::evidence_decay_batch, 3, EmptyPayload{}});
    queue.push({5, WorkType::npc_business_decision, 4, EmptyPayload{}});
    queue.push({1, WorkType::market_recompute, 5, EmptyPayload{}});

    REQUIRE(queue.size() == 5);

    // Drain and verify ascending order.
    uint32_t prev_tick = 0;
    while (!queue.empty()) {
        auto item = queue.top();
        queue.pop();
        REQUIRE(item.due_tick >= prev_tick);
        prev_tick = item.due_tick;
    }
}

TEST_CASE("consequence payload is preserved", "[deferred_work][tier0]") {
    DeferredWorkQueue queue;

    ConsequencePayload payload{42};
    queue.push({10, WorkType::consequence, 100, payload});

    auto item = queue.top();
    queue.pop();

    REQUIRE(item.type == WorkType::consequence);
    REQUIRE(item.subject_id == 100);
    REQUIRE(item.due_tick == 10);

    auto* p = std::get_if<ConsequencePayload>(&item.payload);
    REQUIRE(p != nullptr);
    REQUIRE(p->consequence_id == 42);
}

TEST_CASE("transit payload is preserved", "[deferred_work][tier0]") {
    DeferredWorkQueue queue;

    TransitPayload payload{7, 3};
    queue.push({20, WorkType::transit_arrival, 200, payload});

    auto item = queue.top();
    queue.pop();

    REQUIRE(item.type == WorkType::transit_arrival);
    auto* p = std::get_if<TransitPayload>(&item.payload);
    REQUIRE(p != nullptr);
    REQUIRE(p->shipment_id == 7);
    REQUIRE(p->destination_province_id == 3);
}

TEST_CASE("drain items due at current tick", "[deferred_work][tier0]") {
    DeferredWorkQueue queue;

    queue.push({5, WorkType::consequence, 1, EmptyPayload{}});
    queue.push({5, WorkType::transit_arrival, 2, EmptyPayload{}});
    queue.push({10, WorkType::evidence_decay_batch, 3, EmptyPayload{}});
    queue.push({15, WorkType::market_recompute, 4, EmptyPayload{}});

    // Drain all items due at tick 5.
    uint32_t current_tick = 5;
    int drained = 0;
    while (!queue.empty() && queue.top().due_tick <= current_tick) {
        queue.pop();
        drained++;
    }

    REQUIRE(drained == 2);
    REQUIRE(queue.size() == 2);

    // Next item should be due at tick 10.
    REQUIRE(queue.top().due_tick == 10);
}

TEST_CASE("all work types can be pushed and retrieved", "[deferred_work][tier0]") {
    DeferredWorkQueue queue;

    queue.push({1, WorkType::consequence, 0, ConsequencePayload{1}});
    queue.push({2, WorkType::transit_arrival, 0, TransitPayload{1, 2}});
    queue.push({3, WorkType::npc_relationship_decay, 0, NPCRelationshipDecayPayload{5}});
    queue.push({4, WorkType::evidence_decay_batch, 0, EvidenceDecayPayload{10}});
    queue.push({5, WorkType::npc_business_decision, 0, NPCBusinessDecisionPayload{20}});
    queue.push({6, WorkType::market_recompute, 0, MarketRecomputePayload{1, 2}});
    queue.push({7, WorkType::investigator_meter_update, 0, InvestigatorMeterPayload{30}});
    queue.push({8, WorkType::maturation_project_advance, 0, MaturationPayload{40, 50}});
    queue.push({9, WorkType::commercialize_technology, 0, CommercializePayload{60, 70, 1}});
    queue.push({10, WorkType::background_work, 0, EmptyPayload{}});

    REQUIRE(queue.size() == 10);

    // Drain all and verify they come out in tick order.
    uint32_t expected_tick = 1;
    while (!queue.empty()) {
        REQUIRE(queue.top().due_tick == expected_tick);
        queue.pop();
        expected_tick++;
    }
}

TEST_CASE("large queue maintains heap property", "[deferred_work][tier0]") {
    DeferredWorkQueue queue;

    // Push 1000 items in reverse order.
    for (uint32_t i = 1000; i > 0; --i) {
        queue.push({i, WorkType::background_work, i, EmptyPayload{}});
    }

    REQUIRE(queue.size() == 1000);

    // Verify drain order is ascending.
    uint32_t prev = 0;
    while (!queue.empty()) {
        auto item = queue.top();
        queue.pop();
        REQUIRE(item.due_tick > prev);
        prev = item.due_tick;
    }
}

// ===========================================================================
// Consequence queue (GDD §21 delayed-consequence system)
// ===========================================================================

TEST_CASE("Consequence: base delay table + scheduling formula", "[consequence][tier0]") {
    REQUIRE(consequence_base_delay(ConsequenceCategory::legal_proceeding) == 180u);
    REQUIRE(consequence_base_delay(ConsequenceCategory::criminal_investigation) == 120u);
    REQUIRE(consequence_base_delay(ConsequenceCategory::social_consequence) == 30u);
    // variance01=0.5 -> 0 variance; awareness 1.0 -> delay == base.
    REQUIRE(compute_consequence_delay(ConsequenceCategory::media_exposure, 0.5f, 1.0f) == 45u);
    // awareness 2.0 doubles the delay.
    REQUIRE(compute_consequence_delay(ConsequenceCategory::media_exposure, 0.5f, 2.0f) == 90u);
    // variance01=0 -> -0.2 multiplier: 45 * 0.8 = 36.
    REQUIRE(compute_consequence_delay(ConsequenceCategory::media_exposure, 0.0f, 1.0f) == 36u);
}

TEST_CASE("Consequence: due investigation fires a legal case seed", "[consequence][tier0]") {
    WorldState w{};
    w.current_tick = 100;
    ConsequenceEntry e{};
    e.id = 1;
    e.category = ConsequenceCategory::criminal_investigation;
    e.source_npc_id = 7;
    e.target_id = 9;
    e.province_id = 2;
    e.scheduled_tick = 100;  // due now
    w.consequence_queue.push_back(e);

    DeltaBuffer d{};
    DrainConfig cfg{};
    drain_deferred_work(w, d, cfg);

    REQUIRE(d.new_legal_case_seeds.size() == 1);
    REQUIRE(d.new_legal_case_seeds[0].defendant_npc_id == 9);
    REQUIRE(d.new_legal_case_seeds[0].lead_investigator_id == 7);
    REQUIRE(d.new_legal_case_seeds[0].province_id == 2);
    REQUIRE(w.consequence_queue.empty());  // pruned after firing
}

TEST_CASE("Consequence: does not fire before scheduled tick", "[consequence][tier0]") {
    WorldState w{};
    w.current_tick = 100;
    ConsequenceEntry e{};
    e.category = ConsequenceCategory::criminal_investigation;
    e.scheduled_tick = 200;  // future
    w.consequence_queue.push_back(e);

    DeltaBuffer d{};
    DrainConfig cfg{};
    drain_deferred_work(w, d, cfg);

    REQUIRE(d.new_legal_case_seeds.empty());
    REQUIRE(w.consequence_queue.size() == 1);  // still pending
}

TEST_CASE("Consequence: cancelled entry does not fire", "[consequence][tier0]") {
    WorldState w{};
    w.current_tick = 100;
    ConsequenceEntry e{};
    e.category = ConsequenceCategory::criminal_investigation;
    e.scheduled_tick = 100;
    e.cancelled = true;
    w.consequence_queue.push_back(e);

    DeltaBuffer d{};
    DrainConfig cfg{};
    drain_deferred_work(w, d, cfg);

    REQUIRE(d.new_legal_case_seeds.empty());
    REQUIRE(w.consequence_queue.empty());  // cancelled entries are pruned
}

TEST_CASE("Consequence: fires even when the source NPC is absent/dead", "[consequence][tier0]") {
    WorldState w{};
    w.current_tick = 50;  // no significant_npcs at all
    ConsequenceEntry e{};
    e.category = ConsequenceCategory::media_exposure;
    e.source_npc_id = 999;  // does not exist
    e.province_id = 0;
    e.scheduled_tick = 50;
    w.consequence_queue.push_back(e);

    DeltaBuffer d{};
    DrainConfig cfg{};
    drain_deferred_work(w, d, cfg);

    REQUIRE(d.region_deltas.size() == 1);  // media_exposure -> institutional_trust hit
    REQUIRE(w.consequence_queue.empty());
}

TEST_CASE("Consequence: ConsequenceDelta schedules and cancels via apply_deltas",
          "[consequence][tier0]") {
    WorldState w{};
    w.current_tick = 10;

    DeltaBuffer d{};
    ConsequenceDelta cd{};
    ConsequenceEntry e{};
    e.id = 5;
    e.category = ConsequenceCategory::social_consequence;
    e.scheduled_tick = 40;
    cd.new_consequence = e;
    d.consequence_deltas.push_back(cd);
    apply_deltas(w, d);
    REQUIRE(w.consequence_queue.size() == 1);
    REQUIRE(w.consequence_queue[0].id == 5);

    DeltaBuffer d2{};
    ConsequenceDelta cancel{};
    cancel.cancelled_entry_id = 5;
    d2.consequence_deltas.push_back(cancel);
    apply_deltas(w, d2);
    REQUIRE(w.consequence_queue[0].cancelled == true);
}
