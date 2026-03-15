#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

// Forward declarations
struct WorldState;
struct DeltaBuffer;

namespace econlife {

enum class ModuleScope : uint8_t {
    core = 0,   // Always runs. Architectural foundation.
    v1   = 1,   // Base game. Registered by base_game package at startup.
    ex   = 2,   // Expansion or mod. Registered by its package during load_all().
};

// Interface for all tick modules.
// Each module reads WorldState (const) and writes to DeltaBuffer (mutable).
// Modules declare ordering constraints via runs_after()/runs_before().
class ITickModule {
public:
    virtual ~ITickModule() = default;

    virtual std::string_view name()       const noexcept = 0;
    virtual std::string_view package_id() const noexcept = 0;  // must match package.json
    virtual ModuleScope      scope()      const noexcept { return ModuleScope::v1; }

    // Fine-grained ordering within the resolved package graph.
    // Reference module names from any already-loaded package.
    // PackageManager validates all named dependencies exist after load_all().
    virtual std::vector<std::string_view> runs_after()  const { return {}; }
    virtual std::vector<std::string_view> runs_before() const { return {}; }

    // Province-parallel capability. Override both to enable parallel dispatch.
    virtual bool is_province_parallel() const noexcept { return false; }
    virtual void execute_province(uint32_t province_idx,
                                  const WorldState& state,
                                  DeltaBuffer& province_delta) {}

    // Sequential execution. Called if is_province_parallel() returns false.
    virtual void execute(const WorldState& state, DeltaBuffer& delta) = 0;
};

}  // namespace econlife
