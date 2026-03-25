// TickOrchestrator unit tests — verify topological sort, registration, and execution.
// All tests tagged [orchestrator][tier0].

#include "core/tick/tick_orchestrator.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/tick/thread_pool.h"
#include "core/tick/tick_module.h"
#include "core/world_state/world_state.h"

using namespace econlife;

// --- Test module helpers ---

class TestModule : public ITickModule {
   public:
    TestModule(std::string name, std::vector<std::string_view> after = {},
               std::vector<std::string_view> before = {}, bool parallel = false)
        : name_(std::move(name)),
          after_(std::move(after)),
          before_(std::move(before)),
          parallel_(parallel) {}

    std::string_view name() const noexcept override { return name_; }
    std::string_view package_id() const noexcept override { return "test"; }

    std::vector<std::string_view> runs_after() const override { return after_; }
    std::vector<std::string_view> runs_before() const override { return before_; }
    bool is_province_parallel() const noexcept override { return parallel_; }

    void execute(const WorldState& state, DeltaBuffer& delta) override {
        execution_order_.push_back(name_);
    }

    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override {
        execution_order_.push_back(name_ + "_p" + std::to_string(province_idx));
    }

    static std::vector<std::string>& execution_order() { return execution_order_; }
    static void reset_order() { execution_order_.clear(); }

   private:
    std::string name_;
    std::vector<std::string_view> after_;
    std::vector<std::string_view> before_;
    bool parallel_;
    static inline std::vector<std::string> execution_order_;
};

// --- Tests ---

TEST_CASE("empty orchestrator finalizes without error", "[orchestrator][tier0]") {
    TickOrchestrator orch;
    REQUIRE_FALSE(orch.is_finalized());
    orch.finalize_registration();
    REQUIRE(orch.is_finalized());
    REQUIRE(orch.modules().empty());
}

TEST_CASE("single module registers and finalizes", "[orchestrator][tier0]") {
    TickOrchestrator orch;
    orch.register_module(std::make_unique<TestModule>("alpha"));
    orch.finalize_registration();
    REQUIRE(orch.modules().size() == 1);
    REQUIRE(orch.modules()[0]->name() == "alpha");
}

TEST_CASE("modules sorted by runs_after dependency", "[orchestrator][tier0]") {
    TickOrchestrator orch;

    // Register in reverse dependency order.
    orch.register_module(std::make_unique<TestModule>("c", std::vector<std::string_view>{"b"}));
    orch.register_module(std::make_unique<TestModule>("a"));
    orch.register_module(std::make_unique<TestModule>("b", std::vector<std::string_view>{"a"}));

    orch.finalize_registration();

    // Should be sorted: a, b, c
    REQUIRE(orch.modules()[0]->name() == "a");
    REQUIRE(orch.modules()[1]->name() == "b");
    REQUIRE(orch.modules()[2]->name() == "c");
}

TEST_CASE("modules sorted by runs_before dependency", "[orchestrator][tier0]") {
    TickOrchestrator orch;

    // "a" declares it runs_before "b"
    orch.register_module(std::make_unique<TestModule>("b"));
    orch.register_module(std::make_unique<TestModule>("a", std::vector<std::string_view>{},
                                                      std::vector<std::string_view>{"b"}));

    orch.finalize_registration();

    REQUIRE(orch.modules()[0]->name() == "a");
    REQUIRE(orch.modules()[1]->name() == "b");
}

TEST_CASE("cycle detection throws", "[orchestrator][tier0]") {
    TickOrchestrator orch;

    orch.register_module(std::make_unique<TestModule>("x", std::vector<std::string_view>{"y"}));
    orch.register_module(std::make_unique<TestModule>("y", std::vector<std::string_view>{"x"}));

    REQUIRE_THROWS_WITH(orch.finalize_registration(), Catch::Matchers::ContainsSubstring("cycle"));
}

TEST_CASE("duplicate module name throws", "[orchestrator][tier0]") {
    TickOrchestrator orch;

    orch.register_module(std::make_unique<TestModule>("dup"));
    orch.register_module(std::make_unique<TestModule>("dup"));

    REQUIRE_THROWS_WITH(orch.finalize_registration(),
                        Catch::Matchers::ContainsSubstring("Duplicate"));
}

TEST_CASE("unknown runs_after dependency throws", "[orchestrator][tier0]") {
    TickOrchestrator orch;

    orch.register_module(
        std::make_unique<TestModule>("orphan", std::vector<std::string_view>{"nonexistent"}));

    REQUIRE_THROWS_WITH(orch.finalize_registration(),
                        Catch::Matchers::ContainsSubstring("nonexistent"));
}

TEST_CASE("unknown runs_before dependency is silently ignored", "[orchestrator][tier0]") {
    TickOrchestrator orch;

    // "a" declares runs_before "missing" — should be ignored (soft reference).
    orch.register_module(std::make_unique<TestModule>("a", std::vector<std::string_view>{},
                                                      std::vector<std::string_view>{"missing"}));

    REQUIRE_NOTHROW(orch.finalize_registration());
    REQUIRE(orch.modules().size() == 1);
}

TEST_CASE("register after finalize throws", "[orchestrator][tier0]") {
    TickOrchestrator orch;
    orch.finalize_registration();

    REQUIRE_THROWS_WITH(orch.register_module(std::make_unique<TestModule>("late")),
                        Catch::Matchers::ContainsSubstring("after finalize"));
}

TEST_CASE("double finalize throws", "[orchestrator][tier0]") {
    TickOrchestrator orch;
    orch.finalize_registration();
    REQUIRE_THROWS(orch.finalize_registration());
}

TEST_CASE("sequential modules execute in sorted order", "[orchestrator][tier0]") {
    TestModule::reset_order();
    TickOrchestrator orch;

    orch.register_module(std::make_unique<TestModule>("c", std::vector<std::string_view>{"b"}));
    orch.register_module(std::make_unique<TestModule>("a"));
    orch.register_module(std::make_unique<TestModule>("b", std::vector<std::string_view>{"a"}));
    orch.finalize_registration();

    // Create minimal WorldState for execution.
    WorldState state{};
    state.current_tick = 0;
    state.world_seed = 42;
    state.player = nullptr;
    state.lod2_price_index = nullptr;

    ThreadPool pool(1);
    orch.execute_tick(state, pool);

    auto& order = TestModule::execution_order();
    REQUIRE(order.size() == 3);
    REQUIRE(order[0] == "a");
    REQUIRE(order[1] == "b");
    REQUIRE(order[2] == "c");
    REQUIRE(state.current_tick == 1);
}
