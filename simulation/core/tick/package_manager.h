#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// Forward declarations
struct WorldState;

namespace econlife {

class TickOrchestrator;
class ScriptEngine;

enum class PackageType : uint8_t {
    base_game = 0,  // Engine core. Always loaded first. Ships with the binary.
    expansion = 1,  // Studio-authored. Officially tested. Compatibility guaranteed.
    mod       = 2,  // Community-authored. May cause instability. Player accepts risk.
};

// Forward declarations for content registries (defined in data layer)
class GoodRegistry;
class RecipeRegistry;
class FacilityTypeRegistry;

// Manages package discovery, load ordering, and content loading.
// Initialization sequence: discover_packages() → resolve_load_order() → load_all()
class PackageManager {
public:
    // Scans /packages/ and /mods/ directories. Reads all package.json manifests.
    void discover_packages();

    // Topological sort on load_after/load_before constraints.
    // Panics at startup with named cycle on failure.
    std::vector<std::string> resolve_load_order();

    // Loads all packages in resolved order.
    void load_all(TickOrchestrator& orchestrator,
                  ScriptEngine&     script_engine,
                  MigrationRegistry& migrations);

    bool        is_loaded(std::string_view package_id) const;
    PackageType package_type(std::string_view package_id) const;
    std::vector<std::string> loaded_package_ids() const;

    const GoodRegistry&          goods()      const;
    const RecipeRegistry&        recipes()    const;
    const FacilityTypeRegistry&  facilities() const;
};

// Schema versioning for save compatibility.
constexpr uint32_t CURRENT_SCHEMA_VERSION = 1;

using MigrationFn = std::function<void(WorldState&)>;

// Manages schema migrations for world state versioning.
// Migrations are additive only: add new fields with defaults; never delete data.
class MigrationRegistry {
public:
    void register_migration(uint32_t from_version, MigrationFn fn);

    // Applies migrations in version order from loaded_version to CURRENT_SCHEMA_VERSION.
    void migrate(WorldState& ws, uint32_t loaded_version) const;

private:
    std::map<uint32_t, MigrationFn> migrations_;
};

// Lua 5.4 scripting layer for behavior hooks.
// Scripts receive WorldState as read-only; writes go through sandboxed DeltaBuffer.
// Per-hook CPU budget: normal = 0.1ms, extended = 0.5ms.
// Three consecutive budget overruns: hook disabled for session.
class ScriptEngine {
public:
    void register_hook(const std::string& script_path, const std::string& hook_type,
                       const std::string& budget);
    void execute_hooks(uint32_t province_id, uint32_t tick, const WorldState& state);
};

}  // namespace econlife
