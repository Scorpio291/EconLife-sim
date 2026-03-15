#include "core/tick/tick_module.h"

namespace econlife {

class LegalProcessModule : public ITickModule {
public:
    std::string_view name() const noexcept override { return "legal_process"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    std::vector<std::string_view> runs_after() const override {
        return {"investigator_engine"};
    }

    bool is_province_parallel() const noexcept override { return false; }

    void execute(const WorldState& state, DeltaBuffer& delta) override {
        // Stub: no-op implementation. Will be replaced during Orchestrator implementation.
    }
};

}  // namespace econlife
