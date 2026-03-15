#include "core/tick/tick_module.h"

namespace econlife {

class LodSystemModule : public ITickModule {
public:
    std::string_view name() const noexcept override { return "lod_system"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    std::vector<std::string_view> runs_after() const override {
        return {"regional_conditions"};
    }

    bool is_province_parallel() const noexcept override { return false; }

    void execute(const WorldState& state, DeltaBuffer& delta) override {
        // Stub: no-op implementation. Will be replaced during Orchestrator implementation.
    }
};

}  // namespace econlife
