#include "random_events_module.h"

#include <algorithm>
#include <cmath>

#include "core/world_state/player.h"  // PlayerCharacter (complete type for state.player->)

namespace econlife {

// =============================================================================
// Constants — fixed time conversions (not tunable)
// =============================================================================

static constexpr float TICKS_PER_MONTH = 30.0f;

// =============================================================================
// Default templates — used when template registry is not loaded.
// =============================================================================

static std::vector<RandomEventTemplate> get_default_templates() {
    std::vector<RandomEventTemplate> templates;

    templates.push_back({"drought_mild", "drought_mild", EventCategory::natural, "Mild Drought",
                         1.0f, 0.10f, 0.30f, 10, 30, 2.0f, 1.0f, 1.0f, false});
    templates.push_back({"drought_severe", "drought_severe", EventCategory::natural,
                         "Severe Drought", 0.5f, 0.40f, 0.70f, 20, 60, 3.0f, 1.0f, 1.0f, false});
    templates.push_back({"flood", "flood", EventCategory::natural, "Flood", 0.8f, 0.20f, 0.60f, 5,
                         15, 1.5f, 1.0f, 1.0f, false});
    templates.push_back({"earthquake", "earthquake", EventCategory::natural, "Earthquake", 0.3f,
                         0.30f, 0.90f, 1, 5, 1.0f, 1.0f, 1.0f, false});

    templates.push_back({"industrial_fire", "industrial_fire", EventCategory::accident,
                         "Industrial Fire", 1.0f, 0.20f, 0.70f, 3, 10, 1.0f, 1.0f, 2.0f, true});
    templates.push_back({"chemical_spill", "chemical_spill", EventCategory::accident,
                         "Chemical Spill", 0.6f, 0.30f, 0.80f, 5, 20, 1.0f, 1.0f, 2.5f, true});
    templates.push_back({"transport_failure", "transport_failure", EventCategory::accident,
                         "Transport Failure", 0.8f, 0.10f, 0.40f, 2, 7, 1.0f, 1.0f, 1.5f, true});

    templates.push_back({"market_shock", "market_shock", EventCategory::economic, "Market Shock",
                         1.0f, 0.20f, 0.60f, 5, 15, 1.0f, 1.0f, 1.0f, false});
    templates.push_back({"commodity_spike", "commodity_spike", EventCategory::economic,
                         "Commodity Price Spike", 0.7f, 0.15f, 0.50f, 3, 10, 1.0f, 1.0f, 1.0f,
                         false});
    templates.push_back({"credit_tightening", "credit_tightening", EventCategory::economic,
                         "Credit Tightening", 0.5f, 0.30f, 0.70f, 10, 30, 1.0f, 1.5f, 1.0f, false});

    templates.push_back({"political_crisis", "political_crisis", EventCategory::human,
                         "Political Crisis", 0.8f, 0.25f, 0.65f, 5, 20, 1.0f, 2.5f, 1.0f, false});
    templates.push_back({"labor_strike", "labor_strike", EventCategory::human, "Labor Strike", 0.7f,
                         0.20f, 0.50f, 5, 15, 1.0f, 2.0f, 1.0f, false});
    templates.push_back({"community_unrest", "community_unrest", EventCategory::human,
                         "Community Unrest", 0.6f, 0.30f, 0.70f, 3, 10, 1.0f, 3.0f, 1.0f, false});

    return templates;
}

// =============================================================================
// RandomEventsModule — out-of-line method implementations
// =============================================================================

RandomEventsModule::RandomEventsModule(const RandomEventsConfig& cfg)
    : cfg_(cfg), templates_(get_default_templates()), next_event_id_(1) {}

std::string_view RandomEventsModule::name() const noexcept {
    return "random_events";
}
std::string_view RandomEventsModule::package_id() const noexcept {
    return "base_game";
}
ModuleScope RandomEventsModule::scope() const noexcept {
    return ModuleScope::v1;
}

std::vector<std::string_view> RandomEventsModule::runs_after() const {
    return {"calendar"};
}

bool RandomEventsModule::is_province_parallel() const noexcept {
    return false;
}

void RandomEventsModule::execute_province(uint32_t province_idx, const WorldState& state,
                                          DeltaBuffer& province_delta) {
    if (province_idx >= state.provinces.size())
        return;
    const Province& province = state.provinces[province_idx];

    if (province.lod_level != SimulationLOD::full)
        return;

    uint64_t rng_seed = state.world_seed ^ static_cast<uint64_t>(state.current_tick) ^
                        static_cast<uint64_t>(province.id);
    DeterministicRNG rng(rng_seed);

    process_active_events(state, province, province_delta);
    roll_for_new_event(state, province, rng, province_delta);
}

void RandomEventsModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Prune expired events (end_tick == 0) so active_events_ does not grow without bound.
    // This runs single-threaded, so the erase is safe.
    active_events_.erase(
        std::remove_if(active_events_.begin(), active_events_.end(),
                       [](const ActiveRandomEvent& e) { return e.end_tick == 0; }),
        active_events_.end());

    // Process each province sequentially. Previously this module was incorrectly
    // marked province-parallel, which caused a data race: concurrent execute_province
    // calls both read and wrote active_events_ (via push_back in roll_for_new_event
    // and next_event_id_++ in allocate_event_id). A push_back reallocation in one
    // thread invalidated iterators held by another thread, corrupting the heap.
    for (uint32_t p = 0; p < static_cast<uint32_t>(state.provinces.size()); ++p) {
        execute_province(p, state, delta);
    }
}

const std::vector<RandomEventTemplate>& RandomEventsModule::templates() const {
    return templates_;
}
void RandomEventsModule::set_templates(std::vector<RandomEventTemplate> t) {
    templates_ = std::move(t);
}

const std::vector<ActiveRandomEvent>& RandomEventsModule::active_events() const {
    return active_events_;
}
std::vector<ActiveRandomEvent>& RandomEventsModule::active_events_mut() {
    return active_events_;
}
void RandomEventsModule::add_active_event(ActiveRandomEvent event) {
    active_events_.push_back(std::move(event));
}

void RandomEventsModule::set_base_rate(float rate) {
    base_rate_override_ = rate;
}
void RandomEventsModule::clear_base_rate_override() {
    base_rate_override_ = -1.0f;
}
uint32_t RandomEventsModule::allocate_event_id() {
    return next_event_id_++;
}

float RandomEventsModule::effective_base_rate() const {
    return (base_rate_override_ >= 0.0f) ? base_rate_override_ : cfg_.base_rate;
}

void RandomEventsModule::process_active_events(const WorldState& state, const Province& province,
                                               DeltaBuffer& province_delta) {
    for (auto& event : active_events_) {
        if (event.province_id != province.id)
            continue;

        if (event.end_tick != 0 && state.current_tick >= event.end_tick) {
            event.end_tick = 0;
            continue;
        }

        if (event.end_tick == 0)
            continue;

        apply_per_tick_effects(state, province, event, province_delta);
    }
}

void RandomEventsModule::apply_per_tick_effects(const WorldState& state, const Province& province,
                                                ActiveRandomEvent& event,
                                                DeltaBuffer& province_delta) {
    switch (event.category) {
        case EventCategory::natural:
            apply_natural_per_tick(state, province, event, province_delta);
            break;
        case EventCategory::accident:
            apply_accident_per_tick(state, province, event, province_delta);
            break;
        case EventCategory::economic:
            apply_economic_per_tick(state, province, event, province_delta);
            break;
        case EventCategory::human:
            apply_human_per_tick(state, province, event, province_delta);
            break;
    }
}

void RandomEventsModule::apply_natural_per_tick(const WorldState& /*state*/,
                                                const Province& province,
                                                const ActiveRandomEvent& event,
                                                DeltaBuffer& province_delta) {
    float agri_impact = cfg_.natural_agri_mod_min +
                        (cfg_.natural_agri_mod_max - cfg_.natural_agri_mod_min) * (1.0f - event.severity);
    (void)agri_impact;

    RegionDelta rd{};
    rd.region_id = province.region_id;
    rd.stability_delta = -0.01f * event.severity;
    province_delta.region_deltas.push_back(rd);
}

void RandomEventsModule::apply_accident_per_tick(const WorldState& state, const Province& province,
                                                 ActiveRandomEvent& event,
                                                 DeltaBuffer& province_delta) {
    if (!event.evidence_generated && event.severity >= cfg_.evidence_severity_threshold) {
        uint32_t target_owner_id = 0;
        for (const auto& biz : state.npc_businesses) {
            if (biz.province_id == province.id) {
                target_owner_id = biz.owner_id;
                break;
            }
        }

        EvidenceToken token{};
        token.id = 0;
        token.type = EvidenceType::physical;
        token.source_npc_id = 0;
        token.target_npc_id = target_owner_id;
        token.actionability = event.severity;
        token.decay_rate = 0.001f;
        token.created_tick = state.current_tick;
        token.province_id = province.id;
        token.is_active = true;

        EvidenceDelta ed{};
        ed.new_token = token;
        province_delta.evidence_deltas.push_back(ed);

        event.evidence_generated = true;
    }

    RegionDelta rd{};
    rd.region_id = province.region_id;
    rd.stability_delta = -0.005f * event.severity;
    province_delta.region_deltas.push_back(rd);

    for (uint32_t npc_id : province.significant_npc_ids) {
        for (const auto& npc : state.significant_npcs) {
            if (npc.id == npc_id && npc.status == NPCStatus::active) {
                NPCDelta nd{};
                nd.npc_id = npc.id;
                MemoryEntry mem{};
                mem.tick_timestamp = state.current_tick;
                mem.type = MemoryType::physical_hazard;
                mem.subject_id = province.id;
                mem.emotional_weight = -0.3f * event.severity;
                mem.decay = 1.0f;
                mem.is_actionable = (event.severity >= cfg_.evidence_severity_threshold);
                nd.new_memory_entry = mem;
                province_delta.npc_deltas.push_back(nd);
                break;
            }
        }
    }
}

void RandomEventsModule::apply_economic_per_tick(const WorldState& state, const Province& province,
                                                 const ActiveRandomEvent& event,
                                                 DeltaBuffer& province_delta) {
    if (!province.market_ids.empty()) {
        float shift = cfg_.economic_price_shift_min +
                      (cfg_.economic_price_shift_max - cfg_.economic_price_shift_min) * event.severity;
        if (event.id % 2 == 0)
            shift = -shift;

        for (const auto& market : state.regional_markets) {
            if (market.province_id == province.id) {
                MarketDelta md{};
                md.good_id = market.good_id;
                md.region_id = province.region_id;
                md.spot_price_override = market.spot_price + shift;
                province_delta.market_deltas.push_back(md);
                break;
            }
        }
    }
}

void RandomEventsModule::apply_human_per_tick(const WorldState& state, const Province& province,
                                              const ActiveRandomEvent& event,
                                              DeltaBuffer& province_delta) {
    RegionDelta rd{};
    rd.region_id = province.region_id;
    rd.grievance_delta = 0.01f * event.severity;
    rd.cohesion_delta = -0.005f * event.severity;
    province_delta.region_deltas.push_back(rd);

    if (state.player != nullptr && state.player->current_province_id == province.id) {
        SceneCard sc{};
        sc.id = 0;
        sc.type = SceneCardType::news_notification;
        sc.setting = SceneSetting::street_corner;
        sc.npc_id = 0;
        sc.npc_presentation_state = 0.5f;
        province_delta.new_scene_cards.push_back(sc);
    }
}

void RandomEventsModule::roll_for_new_event(const WorldState& state, const Province& province,
                                            DeterministicRNG& rng, DeltaBuffer& province_delta) {
    float climate_stress = province.climate.climate_stress_current;
    float instability = 1.0f - province.conditions.stability_score;
    float infra_rating = province.infrastructure_rating;

    float economic_volatility = compute_economic_volatility(state, province);

    float adjusted_rate = effective_base_rate() *
                          (1.0f + climate_stress * cfg_.climate_event_amplifier) *
                          (1.0f + instability * cfg_.instability_event_amplifier) * economic_volatility;

    float p = 1.0f - std::exp(-adjusted_rate / TICKS_PER_MONTH);

    float roll = rng.next_float();
    if (roll >= p)
        return;

    EventCategory category =
        select_category(climate_stress, instability, infra_rating, economic_volatility, rng);

    const RandomEventTemplate* selected = select_template(category, province, rng);
    if (selected == nullptr)
        return;

    float severity = selected->severity_min +
                     rng.next_float() * (selected->severity_max - selected->severity_min);
    severity = std::clamp(severity, selected->severity_min, selected->severity_max);

    uint32_t duration =
        static_cast<uint32_t>(selected->duration_ticks_min) +
        rng.next_uint(selected->duration_ticks_max - selected->duration_ticks_min + 1);
    duration = std::clamp(duration, selected->duration_ticks_min, selected->duration_ticks_max);

    ActiveRandomEvent event{};
    event.id = allocate_event_id();
    event.template_id = selected->id;
    event.template_key = selected->template_key;
    event.province_id = province.id;
    event.category = category;
    event.severity = severity;
    event.started_tick = state.current_tick;
    event.end_tick = state.current_tick + duration;
    event.evidence_generated = false;
    event.effects_applied_this_tick = false;

    active_events_.push_back(event);

    apply_immediate_effects(state, province, event, province_delta);
}

float RandomEventsModule::compute_economic_volatility(const WorldState& state,
                                                      const Province& province) const {
    float total_deviation = 0.0f;
    uint32_t count = 0;
    for (const auto& market : state.regional_markets) {
        if (market.province_id == province.id && market.equilibrium_price > 0.0f) {
            float deviation =
                std::abs(market.spot_price - market.equilibrium_price) / market.equilibrium_price;
            total_deviation += deviation;
            ++count;
        }
    }
    if (count == 0)
        return 1.0f;
    float avg_deviation = total_deviation / static_cast<float>(count);
    return 1.0f + avg_deviation;
}

EventCategory RandomEventsModule::select_category(float climate_stress, float instability,
                                                  float infra_rating, float economic_volatility,
                                                  DeterministicRNG& rng) const {
    float w_natural = cfg_.weight_natural * (1.0f + climate_stress);
    float w_accident = cfg_.weight_accident * (1.0f + (1.0f - infra_rating));
    float w_economic = cfg_.weight_economic * economic_volatility;
    float w_human = cfg_.weight_human * (1.0f + instability);

    float total = w_natural + w_accident + w_economic + w_human;
    if (total <= 0.0f)
        return EventCategory::natural;

    float roll = rng.next_float() * total;

    if (roll < w_natural)
        return EventCategory::natural;
    roll -= w_natural;
    if (roll < w_accident)
        return EventCategory::accident;
    roll -= w_accident;
    if (roll < w_economic)
        return EventCategory::economic;
    return EventCategory::human;
}

const RandomEventTemplate* RandomEventsModule::select_template(EventCategory category,
                                                               const Province& province,
                                                               DeterministicRNG& rng) const {
    float climate_stress = province.climate.climate_stress_current;
    float instability = 1.0f - province.conditions.stability_score;
    float infra_rating = province.infrastructure_rating;

    std::vector<std::pair<const RandomEventTemplate*, float>> candidates;
    float total_weight = 0.0f;

    for (const auto& tmpl : templates_) {
        if (tmpl.category != category)
            continue;

        float w = tmpl.base_weight;
        w *= (1.0f + climate_stress * (tmpl.climate_stress_weight_scale - 1.0f));
        w *= (1.0f + instability * (tmpl.instability_weight_scale - 1.0f));
        w *= (1.0f + (1.0f - infra_rating) * (tmpl.infrastructure_weight_scale - 1.0f));

        if (w > 0.0f) {
            candidates.push_back({&tmpl, w});
            total_weight += w;
        }
    }

    if (candidates.empty() || total_weight <= 0.0f)
        return nullptr;

    float roll = rng.next_float() * total_weight;
    for (const auto& [tmpl, w] : candidates) {
        if (roll < w)
            return tmpl;
        roll -= w;
    }

    return candidates.back().first;
}

void RandomEventsModule::apply_immediate_effects(const WorldState& state, const Province& province,
                                                 ActiveRandomEvent& event,
                                                 DeltaBuffer& province_delta) {
    switch (event.category) {
        case EventCategory::natural: {
            float agri_mod = cfg_.natural_agri_mod_min + (cfg_.natural_agri_mod_max - cfg_.natural_agri_mod_min) *
                                                        (1.0f - event.severity);
            float infra_dmg = cfg_.natural_infra_dmg_min +
                              (cfg_.natural_infra_dmg_max - cfg_.natural_infra_dmg_min) * event.severity;
            (void)agri_mod;
            (void)infra_dmg;

            RegionDelta rd{};
            rd.region_id = province.region_id;
            rd.stability_delta = -0.02f * event.severity;
            province_delta.region_deltas.push_back(rd);
            break;
        }

        case EventCategory::accident: {
            float output_impact =
                cfg_.accident_output_rate_min +
                (cfg_.accident_output_rate_max - cfg_.accident_output_rate_min) * (1.0f - event.severity);
            float infra_dmg = cfg_.accident_infra_dmg_min +
                              (cfg_.accident_infra_dmg_max - cfg_.accident_infra_dmg_min) * event.severity;
            (void)output_impact;
            (void)infra_dmg;

            RegionDelta rd{};
            rd.region_id = province.region_id;
            rd.stability_delta = -0.01f * event.severity;
            province_delta.region_deltas.push_back(rd);

            if (event.severity >= cfg_.evidence_severity_threshold) {
                uint32_t target_owner_id = 0;
                for (const auto& biz : state.npc_businesses) {
                    if (biz.province_id == province.id) {
                        target_owner_id = biz.owner_id;
                        break;
                    }
                }

                EvidenceToken token{};
                token.id = 0;
                token.type = EvidenceType::physical;
                token.source_npc_id = 0;
                token.target_npc_id = target_owner_id;
                token.actionability = event.severity;
                token.decay_rate = 0.001f;
                token.created_tick = state.current_tick;
                token.province_id = province.id;
                token.is_active = true;

                EvidenceDelta ed{};
                ed.new_token = token;
                province_delta.evidence_deltas.push_back(ed);

                event.evidence_generated = true;
            }

            for (uint32_t npc_id : province.significant_npc_ids) {
                for (const auto& npc : state.significant_npcs) {
                    if (npc.id == npc_id && npc.status == NPCStatus::active) {
                        NPCDelta nd{};
                        nd.npc_id = npc.id;
                        MemoryEntry mem{};
                        mem.tick_timestamp = state.current_tick;
                        mem.type = MemoryType::physical_hazard;
                        mem.subject_id = province.id;
                        mem.emotional_weight = -0.5f * event.severity;
                        mem.decay = 1.0f;
                        mem.is_actionable = (event.severity >= cfg_.evidence_severity_threshold);
                        nd.new_memory_entry = mem;
                        province_delta.npc_deltas.push_back(nd);
                        break;
                    }
                }
            }
            break;
        }

        case EventCategory::economic: {
            if (!province.market_ids.empty()) {
                float shift =
                    cfg_.economic_price_shift_min +
                    (cfg_.economic_price_shift_max - cfg_.economic_price_shift_min) * event.severity;
                if (event.id % 2 == 0)
                    shift = -shift;

                for (const auto& market : state.regional_markets) {
                    if (market.province_id == province.id) {
                        MarketDelta md{};
                        md.good_id = market.good_id;
                        md.region_id = province.region_id;
                        md.spot_price_override = market.spot_price + shift;
                        province_delta.market_deltas.push_back(md);
                        break;
                    }
                }
            }

            for (uint32_t npc_id : province.significant_npc_ids) {
                for (const auto& npc : state.significant_npcs) {
                    if (npc.id == npc_id && npc.status == NPCStatus::active) {
                        NPCDelta nd{};
                        nd.npc_id = npc.id;
                        MemoryEntry mem{};
                        mem.tick_timestamp = state.current_tick;
                        mem.type = MemoryType::event;
                        mem.subject_id = province.id;
                        mem.emotional_weight = -0.2f * event.severity;
                        mem.decay = 1.0f;
                        mem.is_actionable = false;
                        nd.new_memory_entry = mem;
                        province_delta.npc_deltas.push_back(nd);
                        break;
                    }
                }
            }
            break;
        }

        case EventCategory::human: {
            RegionDelta rd{};
            rd.region_id = province.region_id;
            rd.grievance_delta = 0.02f * event.severity;
            rd.cohesion_delta = -0.01f * event.severity;
            province_delta.region_deltas.push_back(rd);

            if (state.player != nullptr && state.player->current_province_id == province.id) {
                SceneCard sc{};
                sc.id = 0;
                sc.type = SceneCardType::news_notification;
                sc.setting = SceneSetting::street_corner;
                sc.npc_id = 0;
                sc.npc_presentation_state = 0.5f;
                province_delta.new_scene_cards.push_back(sc);
            }
            break;
        }
    }
}

}  // namespace econlife
