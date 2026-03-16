// DeferredWorkQueue unit tests — verify min-heap ordering and payload handling.
// All tests tagged [deferred_work][tier0].

#include <catch2/catch_test_macros.hpp>
#include <cstdint>

#include "core/tick/deferred_work.h"

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
    queue.push({5,  WorkType::transit_arrival, 2, EmptyPayload{}});
    queue.push({15, WorkType::evidence_decay_batch, 3, EmptyPayload{}});
    queue.push({5,  WorkType::npc_business_decision, 4, EmptyPayload{}});
    queue.push({1,  WorkType::market_recompute, 5, EmptyPayload{}});

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
