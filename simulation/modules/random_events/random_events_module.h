#pragma once

#include "event_types.h"
#include "core/tick/tick_module.h"
#include "core/world_state/world_state.h"
#include "core/world_state/delta_buffer.h"
#include "core/rng/deterministic_rng.h"

#include <vector>

namespace econlife {

// Forward declaration — full class defined in random_events_module.cpp
class RandomEventsModule : public ITickModule {
public:
    RandomEventsModule();

    std::string_view name() const noexcept override;
    std::string_view package_id() const noexcept override;
    ModuleScope scope() const noexcept override;
    std::vector<std::string_view> runs_after() const override;
    bool is_province_parallel() const noexcept override;

    void execute_province(uint32_t province_idx,
                          const WorldState& state,
                          DeltaBuffer& province_delta) override;
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // Template access for testing
    const std::vector<RandomEventTemplate>& templates() const;
    void set_templates(std::vector<RandomEventTemplate> t);

    // Active event management for testing
    const std::vector<ActiveRandomEvent>& active_events() const;
    std::vector<ActiveRandomEvent>& active_events_mut();
    void add_active_event(ActiveRandomEvent event);

    // Control for testing
    void set_base_rate(float rate);
    void clear_base_rate_override();
    uint32_t allocate_event_id();

private:
    std::vector<RandomEventTemplate> templates_;
    std::vector<ActiveRandomEvent>   active_events_;
    uint32_t                         next_event_id_;
    float                            base_rate_override_ = -1.0f;

    float effective_base_rate() const;
    void process_active_events(const WorldState& state,
                               const Province& province,
                               DeltaBuffer& province_delta);
    void apply_per_tick_effects(const WorldState& state,
                                const Province& province,
                                ActiveRandomEvent& event,
                                DeltaBuffer& province_delta);
    void apply_natural_per_tick(const WorldState& state,
                                const Province& province,
                                const ActiveRandomEvent& event,
                                DeltaBuffer& province_delta);
    void apply_accident_per_tick(const WorldState& state,
                                 const Province& province,
                                 ActiveRandomEvent& event,
                                 DeltaBuffer& province_delta);
    void apply_economic_per_tick(const WorldState& state,
                                 const Province& province,
                                 const ActiveRandomEvent& event,
                                 DeltaBuffer& province_delta);
    void apply_human_per_tick(const WorldState& state,
                              const Province& province,
                              const ActiveRandomEvent& event,
                              DeltaBuffer& province_delta);
    float compute_economic_volatility(const WorldState& state,
                                      const Province& province) const;
    EventCategory select_category(float climate_stress,
                                   float instability,
                                   float infra_rating,
                                   float economic_volatility,
                                   DeterministicRNG& rng) const;
    const RandomEventTemplate* select_template(EventCategory category,
                                                const Province& province,
                                                DeterministicRNG& rng) const;
    void roll_for_new_event(const WorldState& state,
                            const Province& province,
                            DeterministicRNG& rng,
                            DeltaBuffer& province_delta);
    void apply_immediate_effects(const WorldState& state,
                                 const Province& province,
                                 ActiveRandomEvent& event,
                                 DeltaBuffer& province_delta);
};

}  // namespace econlife
