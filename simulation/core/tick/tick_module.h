#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace econlife {

// Forward declarations — in econlife namespace to match type definitions
struct WorldState;
struct DeltaBuffer;

enum class ModuleScope : uint8_t {
    core = 0,  // Always runs. Architectural foundation.
    v1 = 1,    // Base game. Registered by base_game package at startup.
    ex = 2,    // Expansion or mod. Registered by its package during load_all().
};

// Interface for all tick modules.
// Each module reads WorldState (const) and writes to DeltaBuffer (mutable).
// Modules declare ordering constraints via runs_after()/runs_before().
class ITickModule {
   public:
    virtual ~ITickModule() = default;

    virtual std::string_view name() const noexcept = 0;
    virtual std::string_view package_id() const noexcept = 0;  // must match package.json
    virtual ModuleScope scope() const noexcept { return ModuleScope::v1; }

    // Fine-grained ordering within the resolved package graph.
    // Reference module names from any already-loaded package.
    // PackageManager validates all named dependencies exist after load_all().
    virtual std::vector<std::string_view> runs_after() const { return {}; }
    virtual std::vector<std::string_view> runs_before() const { return {}; }

    // Province-parallel capability. Override both to enable parallel dispatch.
    virtual bool is_province_parallel() const noexcept { return false; }
    virtual void execute_province(uint32_t province_idx, const WorldState& state,
                                  DeltaBuffer& province_delta) {}

    // Global post-pass for province-parallel modules. When true, execute()
    // is called after all province deltas are merged and applied, allowing
    // the module to perform global coordination (e.g., transit arrivals,
    // wage equilibration). Only meaningful when is_province_parallel() is true.
    virtual bool has_global_post_pass() const noexcept { return false; }

    // Sequential execution. Called if is_province_parallel() returns false.
    // Also called as a global post-pass if both is_province_parallel() and
    // has_global_post_pass() return true (after province deltas are applied).
    virtual void execute(const WorldState& state, DeltaBuffer& delta) = 0;
};

}  // namespace econlife
