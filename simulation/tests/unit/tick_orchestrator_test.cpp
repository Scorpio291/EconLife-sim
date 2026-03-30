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

// Test module that supports province-parallel + global post-pass.
class TestModuleWithPostPass : public ITickModule {
   public:
    TestModuleWithPostPass(std::string name, std::vector<std::string_view> after = {},
                           std::vector<std::string_view> before = {})
        : name_(std::move(name)), after_(std::move(after)), before_(std::move(before)) {}

    std::string_view name() const noexcept override { return name_; }
    std::string_view package_id() const noexcept override { return "test"; }

    std::vector<std::string_view> runs_after() const override { return after_; }
    std::vector<std::string_view> runs_before() const override { return before_; }
    bool is_province_parallel() const noexcept override { return true; }
    bool has_global_post_pass() const noexcept override { return true; }

    void execute(const WorldState& state, DeltaBuffer& delta) override {
        TestModule::execution_order().push_back(name_ + "_global");
    }

    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override {
        TestModule::execution_order().push_back(name_ + "_p" + std::to_string(province_idx));
    }

   private:
    std::string name_;
    std::vector<std::string_view> after_;
    std::vector<std::string_view> before_;
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

TEST_CASE("province-parallel module with global post-pass executes provinces then global",
          "[orchestrator][tier0]") {
    TestModule::reset_order();
    TickOrchestrator orch;

    // "alpha" is province-parallel with global post-pass.
    orch.register_module(std::make_unique<TestModuleWithPostPass>("alpha"));
    orch.finalize_registration();

    // Create WorldState with 3 provinces to verify province dispatch.
    WorldState state{};
    state.current_tick = 0;
    state.world_seed = 42;
    state.player = nullptr;
    state.lod2_price_index = nullptr;
    state.provinces.resize(3);
    for (auto& p : state.provinces) {
        p.lod_level = SimulationLOD::full;
    }

    ThreadPool pool(1);
    orch.execute_tick(state, pool);

    auto& order = TestModule::execution_order();
    // Should execute: alpha_p0, alpha_p1, alpha_p2, alpha_global
    REQUIRE(order.size() == 4);
    CHECK(order[0] == "alpha_p0");
    CHECK(order[1] == "alpha_p1");
    CHECK(order[2] == "alpha_p2");
    CHECK(order[3] == "alpha_global");
}

TEST_CASE("mixed sequential and province-parallel-with-post-pass modules execute correctly",
          "[orchestrator][tier0]") {
    TestModule::reset_order();
    TickOrchestrator orch;

    // "first" sequential, "middle" province-parallel with post-pass, "last" sequential after middle.
    orch.register_module(std::make_unique<TestModule>("last", std::vector<std::string_view>{"middle"}));
    orch.register_module(std::make_unique<TestModuleWithPostPass>("middle",
                         std::vector<std::string_view>{"first"}));
    orch.register_module(std::make_unique<TestModule>("first"));
    orch.finalize_registration();

    WorldState state{};
    state.current_tick = 0;
    state.world_seed = 42;
    state.player = nullptr;
    state.lod2_price_index = nullptr;
    state.provinces.resize(2);
    for (auto& p : state.provinces) {
        p.lod_level = SimulationLOD::full;
    }

    ThreadPool pool(1);
    orch.execute_tick(state, pool);

    auto& order = TestModule::execution_order();
    // first (sequential), middle_p0, middle_p1, middle_global, last (sequential)
    REQUIRE(order.size() == 5);
    CHECK(order[0] == "first");
    CHECK(order[1] == "middle_p0");
    CHECK(order[2] == "middle_p1");
    CHECK(order[3] == "middle_global");
    CHECK(order[4] == "last");
}

TEST_CASE("province-parallel module without post-pass does not call execute",
          "[orchestrator][tier0]") {
    TestModule::reset_order();
    TickOrchestrator orch;

    // "par" is province-parallel but does NOT have global post-pass.
    orch.register_module(std::make_unique<TestModule>("par", std::vector<std::string_view>{},
                                                      std::vector<std::string_view>{}, true));
    orch.finalize_registration();

    WorldState state{};
    state.current_tick = 0;
    state.world_seed = 42;
    state.player = nullptr;
    state.lod2_price_index = nullptr;
    state.provinces.resize(2);
    for (auto& p : state.provinces) {
        p.lod_level = SimulationLOD::full;
    }

    ThreadPool pool(1);
    orch.execute_tick(state, pool);

    auto& order = TestModule::execution_order();
    // Should only execute province calls, no global execute call.
    REQUIRE(order.size() == 2);
    CHECK(order[0] == "par_p0");
    CHECK(order[1] == "par_p1");
}
