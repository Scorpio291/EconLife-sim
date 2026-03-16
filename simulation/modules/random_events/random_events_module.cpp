#include "event_types.h"
#include "core/tick/tick_module.h"
#include "core/world_state/world_state.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"        // PlayerCharacter (complete type for state.player->)
#include "core/rng/deterministic_rng.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace econlife {

// =============================================================================
// Constants — from INTERFACE.md §19 invariants
// =============================================================================

static constexpr float    BASE_RATE                    = 0.15f;  // events/province/month
static constexpr float    TICKS_PER_MONTH              = 30.0f;
static constexpr float    CLIMATE_EVENT_AMPLIFIER      = 1.5f;
static constexpr float    INSTABILITY_EVENT_AMPLIFIER  = 1.0f;
static constexpr float    EVIDENCE_SEVERITY_THRESHOLD  = 0.3f;

// Category base weights (before conditioning on province state)
static constexpr float    WEIGHT_NATURAL               = 0.25f;
static constexpr float    WEIGHT_ACCIDENT              = 0.20f;
static constexpr float    WEIGHT_ECONOMIC              = 0.30f;
static constexpr float    WEIGHT_HUMAN                 = 0.25f;

// Natural event effect ranges
static constexpr float    NATURAL_AGRI_MOD_MIN         = -0.40f;
static constexpr float    NATURAL_AGRI_MOD_MAX         = -0.05f;
static constexpr float    NATURAL_INFRA_DMG_MIN        =  0.01f;
static constexpr float    NATURAL_INFRA_DMG_MAX        =  0.15f;

// Accident event effect ranges
static constexpr float    ACCIDENT_OUTPUT_RATE_MIN     = -1.0f;
static constexpr float    ACCIDENT_OUTPUT_RATE_MAX     = -0.10f;
static constexpr float    ACCIDENT_INFRA_DMG_MIN       =  0.01f;
static constexpr float    ACCIDENT_INFRA_DMG_MAX       =  0.05f;

// Economic event effect ranges
static constexpr float    ECONOMIC_PRICE_SHIFT_MIN     =  0.10f;
static constexpr float    ECONOMIC_PRICE_SHIFT_MAX     =  0.40f;

// =============================================================================
// Default templates — used when template registry is not loaded.
// In production, templates are loaded from /data/events/event_templates.json.
// =============================================================================

static std::vector<RandomEventTemplate> get_default_templates() {
    std::vector<RandomEventTemplate> templates;

    // --- Natural events ---
    templates.push_back({"drought_mild", "drought_mild", EventCategory::natural,
        "Mild Drought", 1.0f, 0.10f, 0.30f, 10, 30,
        2.0f, 1.0f, 1.0f, false});
    templates.push_back({"drought_severe", "drought_severe", EventCategory::natural,
        "Severe Drought", 0.5f, 0.40f, 0.70f, 20, 60,
        3.0f, 1.0f, 1.0f, false});
    templates.push_back({"flood", "flood", EventCategory::natural,
        "Flood", 0.8f, 0.20f, 0.60f, 5, 15,
        1.5f, 1.0f, 1.0f, false});
    templates.push_back({"earthquake", "earthquake", EventCategory::natural,
        "Earthquake", 0.3f, 0.30f, 0.90f, 1, 5,
        1.0f, 1.0f, 1.0f, false});

    // --- Accident events ---
    templates.push_back({"industrial_fire", "industrial_fire", EventCategory::accident,
        "Industrial Fire", 1.0f, 0.20f, 0.70f, 3, 10,
        1.0f, 1.0f, 2.0f, true});
    templates.push_back({"chemical_spill", "chemical_spill", EventCategory::accident,
        "Chemical Spill", 0.6f, 0.30f, 0.80f, 5, 20,
        1.0f, 1.0f, 2.5f, true});
    templates.push_back({"transport_failure", "transport_failure", EventCategory::accident,
        "Transport Failure", 0.8f, 0.10f, 0.40f, 2, 7,
        1.0f, 1.0f, 1.5f, true});

    // --- Economic events ---
    templates.push_back({"market_shock", "market_shock", EventCategory::economic,
        "Market Shock", 1.0f, 0.20f, 0.60f, 5, 15,
        1.0f, 1.0f, 1.0f, false});
    templates.push_back({"commodity_spike", "commodity_spike", EventCategory::economic,
        "Commodity Price Spike", 0.7f, 0.15f, 0.50f, 3, 10,
        1.0f, 1.0f, 1.0f, false});
    templates.push_back({"credit_tightening", "credit_tightening", EventCategory::economic,
        "Credit Tightening", 0.5f, 0.30f, 0.70f, 10, 30,
        1.0f, 1.5f, 1.0f, false});

    // --- Human events ---
    templates.push_back({"political_crisis", "political_crisis", EventCategory::human,
        "Political Crisis", 0.8f, 0.25f, 0.65f, 5, 20,
        1.0f, 2.5f, 1.0f, false});
    templates.push_back({"labor_strike", "labor_strike", EventCategory::human,
        "Labor Strike", 0.7f, 0.20f, 0.50f, 5, 15,
        1.0f, 2.0f, 1.0f, false});
    templates.push_back({"community_unrest", "community_unrest", EventCategory::human,
        "Community Unrest", 0.6f, 0.30f, 0.70f, 3, 10,
        1.0f, 3.0f, 1.0f, false});

    return templates;
}

// =============================================================================
// RandomEventsModule — province-parallel tick module
// =============================================================================

class RandomEventsModule : public ITickModule {
public:
    RandomEventsModule()
        : templates_(get_default_templates())
        , next_event_id_(1)
    {}

    std::string_view name() const noexcept override { return "random_events"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    std::vector<std::string_view> runs_after() const override {
        return {"calendar"};
    }

    bool is_province_parallel() const noexcept override { return true; }

    void execute_province(uint32_t province_idx,
                          const WorldState& state,
                          DeltaBuffer& province_delta) override {
        if (province_idx >= state.provinces.size()) return;
        const Province& province = state.provinces[province_idx];

        // Only process LOD 0 provinces.
        if (province.lod_level != SimulationLOD::full) return;

        // Fork RNG deterministically for this province and tick.
        uint64_t rng_seed = state.world_seed ^ static_cast<uint64_t>(state.current_tick)
                            ^ static_cast<uint64_t>(province.id);
        DeterministicRNG rng(rng_seed);

        // --- Phase 1: Process active events (per-tick effects and expiry) ---
        process_active_events(state, province, province_delta);

        // --- Phase 2: Roll for new event ---
        roll_for_new_event(state, province, rng, province_delta);
    }

    void execute(const WorldState& state, DeltaBuffer& delta) override {
        // Province-parallel module; execute() is unused.
        // Provided as required by ITickModule interface.
    }

    // --- Template access for testing ---
    const std::vector<RandomEventTemplate>& templates() const { return templates_; }
    void set_templates(std::vector<RandomEventTemplate> t) { templates_ = std::move(t); }

    // --- Active event management for testing ---
    const std::vector<ActiveRandomEvent>& active_events() const { return active_events_; }
    std::vector<ActiveRandomEvent>& active_events_mut() { return active_events_; }
    void add_active_event(ActiveRandomEvent event) { active_events_.push_back(std::move(event)); }

    // --- Control for testing ---
    void set_base_rate(float rate) { base_rate_override_ = rate; }
    void clear_base_rate_override() { base_rate_override_ = -1.0f; }

    uint32_t allocate_event_id() { return next_event_id_++; }

private:
    std::vector<RandomEventTemplate> templates_;
    std::vector<ActiveRandomEvent>   active_events_;
    uint32_t                         next_event_id_;
    float                            base_rate_override_ = -1.0f;

    float effective_base_rate() const {
        return (base_rate_override_ >= 0.0f) ? base_rate_override_ : BASE_RATE;
    }

    // =========================================================================
    // Process active events: apply per-tick effects and expire completed events
    // =========================================================================
    void process_active_events(const WorldState& state,
                               const Province& province,
                               DeltaBuffer& province_delta) {
        for (auto& event : active_events_) {
            if (event.province_id != province.id) continue;

            // Check for expiry.
            if (event.end_tick != 0 && state.current_tick >= event.end_tick) {
                // Mark as resolved.
                event.end_tick = 0;
                continue;
            }

            // Skip already-resolved events.
            if (event.end_tick == 0) continue;

            // Apply per-tick effects based on category.
            apply_per_tick_effects(state, province, event, province_delta);
        }
    }

    // =========================================================================
    // Apply per-tick effects for an active event
    // =========================================================================
    void apply_per_tick_effects(const WorldState& state,
                                const Province& province,
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
                // Economic per-tick effects are price overrides; re-applied each tick.
                apply_economic_per_tick(state, province, event, province_delta);
                break;
            case EventCategory::human:
                // Human per-tick effects are community modifiers.
                apply_human_per_tick(state, province, event, province_delta);
                break;
        }
    }

    void apply_natural_per_tick(const WorldState& /*state*/,
                                const Province& province,
                                const ActiveRandomEvent& event,
                                DeltaBuffer& province_delta) {
        // Agricultural output modifier scaled by severity.
        float agri_impact = NATURAL_AGRI_MOD_MIN +
            (NATURAL_AGRI_MOD_MAX - NATURAL_AGRI_MOD_MIN) * (1.0f - event.severity);

        RegionDelta rd{};
        rd.region_id = province.region_id;
        rd.stability_delta = -0.01f * event.severity;
        province_delta.region_deltas.push_back(rd);
    }

    void apply_accident_per_tick(const WorldState& state,
                                 const Province& province,
                                 ActiveRandomEvent& event,
                                 DeltaBuffer& province_delta) {
        // Generate evidence token once if above threshold.
        if (!event.evidence_generated && event.severity >= EVIDENCE_SEVERITY_THRESHOLD) {
            // Find a facility in this province to target.
            uint32_t target_owner_id = 0;
            for (const auto& biz : state.npc_businesses) {
                if (biz.province_id == province.id) {
                    target_owner_id = biz.owner_id;
                    break;
                }
            }

            EvidenceToken token{};
            token.id = 0; // Will be assigned by evidence system
            token.type = EvidenceType::physical;
            token.source_npc_id = 0; // System-generated
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

        // Infrastructure damage.
        RegionDelta rd{};
        rd.region_id = province.region_id;
        rd.stability_delta = -0.005f * event.severity;
        province_delta.region_deltas.push_back(rd);

        // Create NPC memory entries for workers witnessing the accident.
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
                    mem.is_actionable = (event.severity >= EVIDENCE_SEVERITY_THRESHOLD);
                    nd.new_memory_entry = mem;
                    province_delta.npc_deltas.push_back(nd);
                    break; // Only first matching NPC per id
                }
            }
        }
    }

    void apply_economic_per_tick(const WorldState& state,
                                 const Province& province,
                                 const ActiveRandomEvent& event,
                                 DeltaBuffer& province_delta) {
        // Apply spot price shift to first market in province.
        if (!province.market_ids.empty()) {
            // Determine price shift direction from template severity.
            // Higher severity = larger price disruption.
            float shift = ECONOMIC_PRICE_SHIFT_MIN +
                (ECONOMIC_PRICE_SHIFT_MAX - ECONOMIC_PRICE_SHIFT_MIN) * event.severity;
            // Negative or positive based on whether event id is odd/even (deterministic).
            if (event.id % 2 == 0) shift = -shift;

            for (const auto& market : state.regional_markets) {
                if (market.province_id == province.id) {
                    MarketDelta md{};
                    md.good_id = market.good_id;
                    md.region_id = province.region_id;
                    md.spot_price_override = market.spot_price + shift;
                    province_delta.market_deltas.push_back(md);
                    break; // Affect first matching market
                }
            }
        }
    }

    void apply_human_per_tick(const WorldState& state,
                              const Province& province,
                              const ActiveRandomEvent& event,
                              DeltaBuffer& province_delta) {
        // Community deltas: grievance and cohesion modifiers.
        RegionDelta rd{};
        rd.region_id = province.region_id;
        rd.grievance_delta = 0.01f * event.severity;
        rd.cohesion_delta = -0.005f * event.severity;
        province_delta.region_deltas.push_back(rd);

        // Scene card for player if in this province.
        if (state.player != nullptr &&
            state.player->current_province_id == province.id) {
            SceneCard sc{};
            sc.id = 0; // Will be assigned by scene card system
            sc.type = SceneCardType::news_notification;
            sc.setting = SceneSetting::street_corner;
            sc.npc_id = 0; // System event, no primary NPC
            sc.npc_presentation_state = 0.5f;
            province_delta.new_scene_cards.push_back(sc);
        }
    }

    // =========================================================================
    // Roll for a new event in this province
    // =========================================================================
    void roll_for_new_event(const WorldState& state,
                            const Province& province,
                            DeterministicRNG& rng,
                            DeltaBuffer& province_delta) {
        // Read province conditions.
        float climate_stress = province.climate.climate_stress_current;
        float instability    = 1.0f - province.conditions.stability_score;
        float infra_rating   = province.infrastructure_rating;

        // Economic volatility modifier: derived from market price deviation.
        float economic_volatility = compute_economic_volatility(state, province);

        // Compute adjusted event rate (INTERFACE.md invariant).
        float adjusted_rate = effective_base_rate()
            * (1.0f + climate_stress * CLIMATE_EVENT_AMPLIFIER)
            * (1.0f + instability * INSTABILITY_EVENT_AMPLIFIER)
            * economic_volatility;

        // Poisson probability for this tick.
        float p = 1.0f - std::exp(-adjusted_rate / TICKS_PER_MONTH);

        // Roll.
        float roll = rng.next_float();
        if (roll >= p) return; // No event fires this tick.

        // --- Event fires: select category ---
        EventCategory category = select_category(climate_stress, instability,
                                                  infra_rating, economic_volatility,
                                                  rng);

        // --- Select template from category ---
        const RandomEventTemplate* selected = select_template(category, province, rng);
        if (selected == nullptr) return; // No matching template (failure mode: log + skip).

        // --- Determine severity and duration ---
        float severity = selected->severity_min +
            rng.next_float() * (selected->severity_max - selected->severity_min);
        severity = std::clamp(severity, selected->severity_min, selected->severity_max);

        uint32_t duration = static_cast<uint32_t>(selected->duration_ticks_min) +
            rng.next_uint(selected->duration_ticks_max - selected->duration_ticks_min + 1);
        duration = std::clamp(duration, selected->duration_ticks_min, selected->duration_ticks_max);

        // --- Create ActiveRandomEvent ---
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

        // --- Apply immediate first-tick effects ---
        apply_immediate_effects(state, province, event, province_delta);
    }

    // =========================================================================
    // Compute economic volatility modifier
    // =========================================================================
    float compute_economic_volatility(const WorldState& state,
                                      const Province& province) const {
        // Measure volatility as deviation of spot prices from equilibrium.
        float total_deviation = 0.0f;
        uint32_t count = 0;
        for (const auto& market : state.regional_markets) {
            if (market.province_id == province.id && market.equilibrium_price > 0.0f) {
                float deviation = std::abs(market.spot_price - market.equilibrium_price)
                                  / market.equilibrium_price;
                total_deviation += deviation;
                ++count;
            }
        }
        // Base modifier of 1.0; scaled up by average price deviation.
        if (count == 0) return 1.0f;
        float avg_deviation = total_deviation / static_cast<float>(count);
        return 1.0f + avg_deviation;
    }

    // =========================================================================
    // Select event category by conditioned weighted draw
    // =========================================================================
    EventCategory select_category(float climate_stress,
                                   float instability,
                                   float infra_rating,
                                   float economic_volatility,
                                   DeterministicRNG& rng) const {
        // Compute conditioned weights per INTERFACE.md.
        float w_natural  = WEIGHT_NATURAL  * (1.0f + climate_stress);
        float w_accident = WEIGHT_ACCIDENT * (1.0f + (1.0f - infra_rating));
        float w_economic = WEIGHT_ECONOMIC * economic_volatility;
        float w_human    = WEIGHT_HUMAN    * (1.0f + instability);

        // Normalize.
        float total = w_natural + w_accident + w_economic + w_human;
        if (total <= 0.0f) return EventCategory::natural; // Fallback.

        float roll = rng.next_float() * total;

        if (roll < w_natural) return EventCategory::natural;
        roll -= w_natural;
        if (roll < w_accident) return EventCategory::accident;
        roll -= w_accident;
        if (roll < w_economic) return EventCategory::economic;
        return EventCategory::human;
    }

    // =========================================================================
    // Select template within a category by conditioned weight
    // =========================================================================
    const RandomEventTemplate* select_template(EventCategory category,
                                                const Province& province,
                                                DeterministicRNG& rng) const {
        float climate_stress = province.climate.climate_stress_current;
        float instability    = 1.0f - province.conditions.stability_score;
        float infra_rating   = province.infrastructure_rating;

        // Collect templates matching category, compute conditioned weights.
        std::vector<std::pair<const RandomEventTemplate*, float>> candidates;
        float total_weight = 0.0f;

        for (const auto& tmpl : templates_) {
            if (tmpl.category != category) continue;

            // Conditioned weight per INTERFACE.md:
            // base_weight * climate_stress_weight_scale * instability_weight_scale
            //   * infrastructure_weight_scale
            // The scales are applied as multipliers conditioned on province state.
            float w = tmpl.base_weight;
            w *= (1.0f + climate_stress * (tmpl.climate_stress_weight_scale - 1.0f));
            w *= (1.0f + instability * (tmpl.instability_weight_scale - 1.0f));
            w *= (1.0f + (1.0f - infra_rating) * (tmpl.infrastructure_weight_scale - 1.0f));

            if (w > 0.0f) {
                candidates.push_back({&tmpl, w});
                total_weight += w;
            }
        }

        if (candidates.empty() || total_weight <= 0.0f) return nullptr;

        // Weighted draw.
        float roll = rng.next_float() * total_weight;
        for (const auto& [tmpl, w] : candidates) {
            if (roll < w) return tmpl;
            roll -= w;
        }

        // Fallback to last candidate (rounding).
        return candidates.back().first;
    }

    // =========================================================================
    // Apply immediate effects when event first fires
    // =========================================================================
    void apply_immediate_effects(const WorldState& state,
                                 const Province& province,
                                 ActiveRandomEvent& event,
                                 DeltaBuffer& province_delta) {
        switch (event.category) {
            case EventCategory::natural: {
                // Agricultural output modifier and infrastructure damage.
                float agri_mod = NATURAL_AGRI_MOD_MIN +
                    (NATURAL_AGRI_MOD_MAX - NATURAL_AGRI_MOD_MIN) * (1.0f - event.severity);
                float infra_dmg = NATURAL_INFRA_DMG_MIN +
                    (NATURAL_INFRA_DMG_MAX - NATURAL_INFRA_DMG_MIN) * event.severity;

                RegionDelta rd{};
                rd.region_id = province.region_id;
                rd.stability_delta = -0.02f * event.severity;
                province_delta.region_deltas.push_back(rd);
                break;
            }

            case EventCategory::accident: {
                // Facility disruption.
                float output_impact = ACCIDENT_OUTPUT_RATE_MIN +
                    (ACCIDENT_OUTPUT_RATE_MAX - ACCIDENT_OUTPUT_RATE_MIN) * (1.0f - event.severity);
                float infra_dmg = ACCIDENT_INFRA_DMG_MIN +
                    (ACCIDENT_INFRA_DMG_MAX - ACCIDENT_INFRA_DMG_MIN) * event.severity;

                RegionDelta rd{};
                rd.region_id = province.region_id;
                rd.stability_delta = -0.01f * event.severity;
                province_delta.region_deltas.push_back(rd);

                // Evidence token if above threshold.
                if (event.severity >= EVIDENCE_SEVERITY_THRESHOLD) {
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

                // NPC memory entries.
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
                            mem.is_actionable = (event.severity >= EVIDENCE_SEVERITY_THRESHOLD);
                            nd.new_memory_entry = mem;
                            province_delta.npc_deltas.push_back(nd);
                            break;
                        }
                    }
                }
                break;
            }

            case EventCategory::economic: {
                // Spot price override on affected markets.
                if (!province.market_ids.empty()) {
                    float shift = ECONOMIC_PRICE_SHIFT_MIN +
                        (ECONOMIC_PRICE_SHIFT_MAX - ECONOMIC_PRICE_SHIFT_MIN) * event.severity;
                    if (event.id % 2 == 0) shift = -shift;

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

                // NPC memory for economic stress.
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
                // Community deltas.
                RegionDelta rd{};
                rd.region_id = province.region_id;
                rd.grievance_delta = 0.02f * event.severity;
                rd.cohesion_delta = -0.01f * event.severity;
                province_delta.region_deltas.push_back(rd);

                // Scene card for player if in this province.
                if (state.player != nullptr &&
                    state.player->current_province_id == province.id) {
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
};

}  // namespace econlife
