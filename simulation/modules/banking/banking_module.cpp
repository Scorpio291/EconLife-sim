#include "core/tick/tick_module.h"

namespace econlife {

class BankingModule : public ITickModule {
public:
    std::string_view name() const noexcept override { return "banking"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    std::vector<std::string_view> runs_after() const override {
        return {"financial_distribution"};
    }

    void execute(const WorldState& state, DeltaBuffer& delta) override {
        // Stub: no-op implementation. Will be replaced during Phase 1 prototype.
    }
};

}  // namespace econlife
