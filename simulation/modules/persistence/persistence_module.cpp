#include "persistence_module.h"
#include "core/world_state/world_state.h"
#include "core/world_state/player.h"
#include "core/world_state/delta_buffer.h"
#include <algorithm>
#include <cstring>
#include <lz4.h>

namespace econlife {

// ── Flat binary serialization helpers ────────────────────────────────────────
// All multi-byte values are little-endian. Floats are stored as raw IEEE 754
// binary via memcpy (zero precision loss per spec invariant 8).

namespace {

class ByteWriter {
public:
    void write_u8(uint8_t v) { buf_.push_back(v); }

    void write_u32(uint32_t v) {
        buf_.push_back(static_cast<uint8_t>(v & 0xFF));
        buf_.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        buf_.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        buf_.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    }

    void write_u64(uint64_t v) {
        write_u32(static_cast<uint32_t>(v & 0xFFFFFFFF));
        write_u32(static_cast<uint32_t>((v >> 32) & 0xFFFFFFFF));
    }

    void write_float(float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        write_u32(bits);
    }

    void write_bool(bool v) { write_u8(v ? 1 : 0); }

    void write_string(const std::string& s) {
        write_u32(static_cast<uint32_t>(s.size()));
        for (char c : s) buf_.push_back(static_cast<uint8_t>(c));
    }

    std::vector<uint8_t>& data() { return buf_; }
    const std::vector<uint8_t>& data() const { return buf_; }

private:
    std::vector<uint8_t> buf_;
};

class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size)
        : data_(data), size_(size), pos_(0) {}

    bool ok() const { return pos_ <= size_; }

    uint8_t read_u8() {
        if (pos_ + 1 > size_) { error_ = true; return 0; }
        return data_[pos_++];
    }

    uint32_t read_u32() {
        if (pos_ + 4 > size_) { error_ = true; return 0; }
        uint32_t v = static_cast<uint32_t>(data_[pos_])
                   | (static_cast<uint32_t>(data_[pos_+1]) << 8)
                   | (static_cast<uint32_t>(data_[pos_+2]) << 16)
                   | (static_cast<uint32_t>(data_[pos_+3]) << 24);
        pos_ += 4;
        return v;
    }

    uint64_t read_u64() {
        uint32_t lo = read_u32();
        uint32_t hi = read_u32();
        return static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
    }

    float read_float() {
        uint32_t bits = read_u32();
        float v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }

    bool read_bool() { return read_u8() != 0; }

    std::string read_string() {
        uint32_t len = read_u32();
        if (pos_ + len > size_) { error_ = true; return ""; }
        std::string s(reinterpret_cast<const char*>(data_ + pos_), len);
        pos_ += len;
        return s;
    }

    bool has_error() const { return error_; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_;
    bool error_ = false;
};

// ── Write helpers for compound types ─────────────────────────────────────────

void write_geography(ByteWriter& w, const GeographyProfile& g) {
    w.write_float(g.latitude);
    w.write_float(g.longitude);
    w.write_float(g.elevation_avg_m);
    w.write_float(g.terrain_roughness);
    w.write_float(g.forest_coverage);
    w.write_float(g.arable_land_fraction);
    w.write_float(g.coastal_length_km);
    w.write_bool(g.is_landlocked);
    w.write_float(g.port_capacity);
    w.write_float(g.river_access);
    w.write_float(g.area_km2);
}

void write_climate(ByteWriter& w, const ClimateProfile& c) {
    w.write_u8(static_cast<uint8_t>(c.koppen_zone));
    w.write_float(c.temperature_avg_c);
    w.write_float(c.temperature_min_c);
    w.write_float(c.temperature_max_c);
    w.write_float(c.precipitation_mm);
    w.write_float(c.precipitation_seasonality);
    w.write_float(c.drought_vulnerability);
    w.write_float(c.flood_vulnerability);
    w.write_float(c.wildfire_vulnerability);
    w.write_float(c.climate_stress_current);
}

void write_demographics(ByteWriter& w, const RegionDemographics& d) {
    w.write_u32(d.total_population);
    w.write_float(d.median_age);
    w.write_float(d.education_level);
    w.write_float(d.income_low_fraction);
    w.write_float(d.income_middle_fraction);
    w.write_float(d.income_high_fraction);
    w.write_float(d.political_lean);
}

void write_community(ByteWriter& w, const CommunityState& c) {
    w.write_float(c.cohesion);
    w.write_float(c.grievance_level);
    w.write_float(c.institutional_trust);
    w.write_float(c.resource_access);
    w.write_u8(c.response_stage);
}

void write_political(ByteWriter& w, const RegionalPoliticalState& p) {
    w.write_u32(p.governing_office_id);
    w.write_float(p.approval_rating);
    w.write_u32(p.election_due_tick);
    w.write_float(p.corruption_index);
}

void write_conditions(ByteWriter& w, const RegionConditions& c) {
    w.write_float(c.stability_score);
    w.write_float(c.inequality_index);
    w.write_float(c.crime_rate);
    w.write_float(c.addiction_rate);
    w.write_float(c.criminal_dominance_index);
    w.write_float(c.formal_employment_rate);
    w.write_float(c.regulatory_compliance_index);
    w.write_float(c.drought_modifier);
    w.write_float(c.flood_modifier);
}

void write_province_link(ByteWriter& w, const ProvinceLink& l) {
    w.write_u64(l.neighbor_h3);
    w.write_u8(static_cast<uint8_t>(l.type));
    w.write_float(l.shared_border_km);
    w.write_float(l.transit_terrain_cost);
    w.write_float(l.infrastructure_bonus);
}

void write_resource_deposit(ByteWriter& w, const ResourceDeposit& r) {
    w.write_u32(r.id);
    w.write_u8(static_cast<uint8_t>(r.type));
    w.write_float(r.quantity);
    w.write_float(r.quality);
    w.write_float(r.depth);
    w.write_float(r.accessibility);
    w.write_float(r.depletion_rate);
    w.write_float(r.quantity_remaining);
}

void write_memory_entry(ByteWriter& w, const MemoryEntry& m) {
    w.write_u32(m.tick_timestamp);
    w.write_u8(static_cast<uint8_t>(m.type));
    w.write_u32(m.subject_id);
    w.write_float(m.emotional_weight);
    w.write_float(m.decay);
    w.write_bool(m.is_actionable);
}

void write_knowledge_entry(ByteWriter& w, const KnowledgeEntry& k) {
    w.write_u32(k.subject_id);
    w.write_u32(k.secondary_subject_id);
    w.write_u8(static_cast<uint8_t>(k.type));
    w.write_float(k.confidence);
    w.write_u32(k.acquired_at_tick);
    w.write_u32(k.source_npc_id);
    w.write_u8(static_cast<uint8_t>(k.original_scope));
}

void write_relationship(ByteWriter& w, const Relationship& r) {
    w.write_u32(r.target_npc_id);
    w.write_float(r.trust);
    w.write_float(r.fear);
    w.write_float(r.obligation_balance);
    w.write_u32(static_cast<uint32_t>(r.shared_secrets.size()));
    for (uint32_t s : r.shared_secrets) w.write_u32(s);
    w.write_u32(r.last_interaction_tick);
    w.write_bool(r.is_movement_ally);
    w.write_float(r.recovery_ceiling);
}

void write_motivation(ByteWriter& w, const MotivationVector& m) {
    for (float wt : m.weights) w.write_float(wt);
}

void write_npc(ByteWriter& w, const NPC& npc) {
    w.write_u32(npc.id);
    w.write_u8(static_cast<uint8_t>(npc.role));
    write_motivation(w, npc.motivations);
    w.write_float(npc.risk_tolerance);

    // Memory log
    w.write_u32(static_cast<uint32_t>(npc.memory_log.size()));
    for (const auto& m : npc.memory_log) write_memory_entry(w, m);

    // Knowledge
    w.write_u32(static_cast<uint32_t>(npc.known_evidence.size()));
    for (const auto& k : npc.known_evidence) write_knowledge_entry(w, k);
    w.write_u32(static_cast<uint32_t>(npc.known_relationships.size()));
    for (const auto& k : npc.known_relationships) write_knowledge_entry(w, k);

    // Relationships
    w.write_u32(static_cast<uint32_t>(npc.relationships.size()));
    for (const auto& r : npc.relationships) write_relationship(w, r);

    // Resources
    w.write_float(npc.capital);
    w.write_float(npc.social_capital);
    w.write_u32(static_cast<uint32_t>(npc.contact_ids.size()));
    for (uint32_t c : npc.contact_ids) w.write_u32(c);

    w.write_u32(npc.movement_follower_count);
    w.write_u32(npc.home_province_id);
    w.write_u32(npc.current_province_id);
    w.write_u8(static_cast<uint8_t>(npc.travel_status));
    w.write_u8(static_cast<uint8_t>(npc.status));
}

void write_evidence_token(ByteWriter& w, const EvidenceToken& e) {
    w.write_u32(e.id);
    w.write_u8(static_cast<uint8_t>(e.type));
    w.write_u32(e.source_npc_id);
    w.write_u32(e.target_npc_id);
    w.write_float(e.actionability);
    w.write_float(e.decay_rate);
    w.write_u32(e.created_tick);
    w.write_u32(e.province_id);
    w.write_bool(e.is_active);
}

void write_obligation(ByteWriter& w, const ObligationNode& o) {
    w.write_u32(o.id);
    w.write_u32(o.creditor_npc_id);
    w.write_u32(o.debtor_npc_id);
    w.write_u8(static_cast<uint8_t>(o.favor_type));
    w.write_float(o.weight);
    w.write_u32(o.created_tick);
    w.write_bool(o.is_active);
}

void write_market(ByteWriter& w, const RegionalMarket& m) {
    w.write_u32(m.good_id);
    w.write_u32(m.province_id);
    w.write_float(m.spot_price);
    w.write_float(m.equilibrium_price);
    w.write_float(m.adjustment_rate);
    w.write_float(m.supply);
    w.write_float(m.demand_buffer);
    w.write_float(m.import_price_ceiling);
    w.write_float(m.export_price_floor);
}

void write_business(ByteWriter& w, const NPCBusiness& b) {
    w.write_u32(b.id);
    w.write_u8(static_cast<uint8_t>(b.sector));
    w.write_u8(static_cast<uint8_t>(b.profile));
    w.write_float(b.cash);
    w.write_float(b.revenue_per_tick);
    w.write_float(b.cost_per_tick);
    w.write_float(b.market_share);
    w.write_u32(b.strategic_decision_tick);
    w.write_u8(b.dispatch_day_offset);
    w.write_float(b.actor_tech_state.effective_tech_tier);
    w.write_bool(b.criminal_sector);
    w.write_u32(b.province_id);
    w.write_float(b.regulatory_violation_severity);
    w.write_u8(static_cast<uint8_t>(b.default_activity_scope));
    w.write_u32(b.owner_id);
    w.write_float(b.deferred_salary_liability);
    w.write_float(b.accounts_payable_float);
}

void write_province(ByteWriter& w, const Province& p) {
    w.write_u64(p.h3_index);
    w.write_u32(p.id);
    w.write_string(p.fictional_name);
    w.write_string(p.real_world_reference);

    write_geography(w, p.geography);
    write_climate(w, p.climate);

    // Deposits
    w.write_u32(static_cast<uint32_t>(p.deposits.size()));
    for (const auto& d : p.deposits) write_resource_deposit(w, d);

    write_demographics(w, p.demographics);
    w.write_float(p.infrastructure_rating);
    w.write_float(p.agricultural_productivity);
    w.write_float(p.energy_cost_baseline);
    w.write_float(p.trade_openness);

    w.write_u8(static_cast<uint8_t>(p.lod_level));
    write_community(w, p.community);
    write_political(w, p.political);
    write_conditions(w, p.conditions);

    // NPC ids
    w.write_u32(static_cast<uint32_t>(p.significant_npc_ids.size()));
    for (uint32_t id : p.significant_npc_ids) w.write_u32(id);

    // cohort_stats presence flag + data
    bool has_cohort = (p.cohort_stats != nullptr);
    w.write_bool(has_cohort);
    if (has_cohort) {
        w.write_u32(p.cohort_stats->total_population);
        w.write_float(p.cohort_stats->median_age);
        w.write_float(p.cohort_stats->working_age_fraction);
        w.write_float(p.cohort_stats->dependency_ratio);
    }

    w.write_bool(p.has_karst);
    w.write_float(p.historical_trauma_index);
    w.write_u32(p.region_id);
    w.write_u32(p.nation_id);

    // Links
    w.write_u32(static_cast<uint32_t>(p.links.size()));
    for (const auto& l : p.links) write_province_link(w, l);

    // Market ids
    w.write_u32(static_cast<uint32_t>(p.market_ids.size()));
    for (uint32_t mid : p.market_ids) w.write_u32(mid);
}

void write_nation_political(ByteWriter& w, const NationPoliticalCycleState& p) {
    w.write_u32(p.current_administration_tick);
    w.write_float(p.national_approval);
    w.write_bool(p.election_campaign_active);
    w.write_u32(p.next_election_tick);
}

void write_nation(ByteWriter& w, const Nation& n) {
    w.write_u32(n.id);
    w.write_string(n.name);
    w.write_string(n.currency_code);
    w.write_u8(static_cast<uint8_t>(n.government_type));
    write_nation_political(w, n.political_cycle);

    w.write_u32(static_cast<uint32_t>(n.province_ids.size()));
    for (uint32_t pid : n.province_ids) w.write_u32(pid);

    w.write_float(n.corporate_tax_rate);
    w.write_float(n.income_tax_rate_top_bracket);

    // diplomatic_relations (sorted by key for determinism)
    w.write_u32(static_cast<uint32_t>(n.diplomatic_relations.size()));
    for (const auto& [k, v] : n.diplomatic_relations) {
        w.write_u32(k);
        w.write_float(v);
    }

    // tariff_schedule pointer — skip (indexed separately in tariff_schedules)
    // lod1_profile
    w.write_bool(n.lod1_profile.has_value());
    if (n.lod1_profile.has_value()) {
        const auto& lp = *n.lod1_profile;
        w.write_float(lp.export_margin);
        w.write_float(lp.import_premium);
        w.write_float(lp.trade_openness);
        w.write_float(lp.tech_tier_modifier);
        w.write_float(lp.population_modifier);
        w.write_float(lp.research_investment);
        w.write_u8(lp.current_tier);
        w.write_float(lp.stability_delta_this_month);
        w.write_float(lp.climate_stress_aggregate);
        w.write_float(lp.climate_vulnerability);
        w.write_float(lp.geographic_centroid_lat);
        w.write_float(lp.geographic_centroid_lon);
        w.write_float(lp.lod1_transit_variability_multiplier);
        w.write_string(lp.archetype);
    }
}

void write_calendar_entry(ByteWriter& w, const CalendarEntry& e) {
    w.write_u32(e.id);
    w.write_u32(e.start_tick);
    w.write_u32(e.duration_ticks);
    w.write_u8(static_cast<uint8_t>(e.type));
    w.write_u32(e.npc_id);
    w.write_bool(e.player_committed);
    w.write_bool(e.mandatory);
    // deadline_consequence
    w.write_float(e.deadline_consequence.relationship_penalty);
    w.write_bool(e.deadline_consequence.npc_initiative);
    w.write_u8(static_cast<uint8_t>(e.deadline_consequence.consequence_type));
    w.write_float(e.deadline_consequence.consequence_severity);
    w.write_u32(e.deadline_consequence.consequence_delay_ticks);
    w.write_string(e.deadline_consequence.default_outcome_description);
    w.write_u32(e.scene_card_id);
}

void write_dialogue_line(ByteWriter& w, const DialogueLine& d) {
    w.write_u32(d.speaker_npc_id);
    w.write_string(d.text);
    w.write_float(d.emotional_tone);
}

void write_player_choice(ByteWriter& w, const PlayerChoice& c) {
    w.write_u32(c.id);
    w.write_string(c.label);
    w.write_string(c.description);
    w.write_u32(c.consequence_id);
}

void write_scene_card(ByteWriter& w, const SceneCard& s) {
    w.write_u32(s.id);
    w.write_u8(static_cast<uint8_t>(s.type));
    w.write_u8(static_cast<uint8_t>(s.setting));
    w.write_u32(s.npc_id);
    w.write_u32(static_cast<uint32_t>(s.dialogue.size()));
    for (const auto& d : s.dialogue) write_dialogue_line(w, d);
    w.write_u32(static_cast<uint32_t>(s.choices.size()));
    for (const auto& c : s.choices) write_player_choice(w, c);
    w.write_float(s.npc_presentation_state);
    w.write_bool(s.is_authored);
    w.write_u32(s.chosen_choice_id);
}

void write_player(ByteWriter& w, const PlayerCharacter& p) {
    w.write_u32(p.id);
    w.write_u8(static_cast<uint8_t>(p.background));
    for (auto t : p.traits) w.write_u8(static_cast<uint8_t>(t));
    w.write_u32(p.starting_province_id);

    // Health
    w.write_float(p.health.current_health);
    w.write_float(p.health.lifespan_projection);
    w.write_float(p.health.base_lifespan);
    w.write_float(p.health.exhaustion_accumulator);
    w.write_float(p.health.degradation_rate);

    w.write_float(p.age);

    // Reputation
    w.write_float(p.reputation.public_business);
    w.write_float(p.reputation.public_political);
    w.write_float(p.reputation.public_social);
    w.write_float(p.reputation.street);

    w.write_float(p.wealth);
    w.write_float(p.net_assets);

    // Skills
    w.write_u32(static_cast<uint32_t>(p.skills.size()));
    for (const auto& s : p.skills) {
        w.write_u8(static_cast<uint8_t>(s.domain));
        w.write_float(s.level);
        w.write_float(s.decay_rate);
        w.write_u32(s.last_exercise_tick);
    }

    // Evidence awareness
    w.write_u32(static_cast<uint32_t>(p.evidence_awareness_map.size()));
    for (const auto& e : p.evidence_awareness_map) {
        w.write_u32(e.token_id);
        w.write_u32(e.discovery_tick);
        w.write_u32(e.source_npc_id);
    }

    // Obligation node ids
    w.write_u32(static_cast<uint32_t>(p.obligation_node_ids.size()));
    for (uint32_t id : p.obligation_node_ids) w.write_u32(id);

    // Calendar entry ids
    w.write_u32(static_cast<uint32_t>(p.calendar_entry_ids.size()));
    for (uint32_t id : p.calendar_entry_ids) w.write_u32(id);

    // Personal
    w.write_u32(p.residence_id);
    w.write_u32(p.partner_npc_id);
    w.write_u32(static_cast<uint32_t>(p.children_npc_ids.size()));
    for (uint32_t id : p.children_npc_ids) w.write_u32(id);
    w.write_u32(p.designated_heir_npc_id);

    // Relationships
    w.write_u32(static_cast<uint32_t>(p.relationships.size()));
    for (const auto& r : p.relationships) write_relationship(w, r);

    // Network health
    w.write_float(p.network_health.overall_score);
    w.write_float(p.network_health.network_reach);
    w.write_float(p.network_health.network_density);
    w.write_float(p.network_health.vulnerability);
    w.write_u32(p.movement_follower_count);

    // Milestones
    w.write_u32(static_cast<uint32_t>(p.milestone_log.size()));
    for (const auto& m : p.milestone_log) {
        w.write_u8(static_cast<uint8_t>(m.type));
        w.write_u32(m.achieved_tick);
        w.write_string(m.context_summary);
    }

    // Location
    w.write_u32(p.home_province_id);
    w.write_u32(p.current_province_id);
    w.write_u8(static_cast<uint8_t>(p.travel_status));

    // Restoration history
    w.write_u32(p.restoration_history.restoration_count);
    w.write_u32(static_cast<uint32_t>(p.restoration_history.records.size()));
    for (const auto& r : p.restoration_history.records) {
        w.write_u32(r.restoration_index);
        w.write_u32(r.restored_to_tick);
        w.write_u32(r.restoration_real_tick);
        w.write_u32(r.ticks_erased);
        w.write_u8(r.tier_applied);
    }

    // Calendar capacity modifiers
    w.write_u32(static_cast<uint32_t>(p.calendar_capacity_modifiers.size()));
    for (const auto& m : p.calendar_capacity_modifiers) {
        w.write_float(m.delta);
        w.write_u32(m.expires_tick);
        w.write_u8(static_cast<uint8_t>(m.source));
    }

    w.write_bool(p.ironman_eligible);
}

void write_deferred_work_queue(ByteWriter& w, DeferredWorkQueue queue_copy) {
    // Extract all items from queue (copy is intentional)
    std::vector<DeferredWorkItem> items;
    while (!queue_copy.empty()) {
        items.push_back(queue_copy.top());
        queue_copy.pop();
    }
    // Sort by (due_tick, type, subject_id) for determinism
    std::sort(items.begin(), items.end(),
              [](const DeferredWorkItem& a, const DeferredWorkItem& b) {
                  if (a.due_tick != b.due_tick) return a.due_tick < b.due_tick;
                  if (a.type != b.type)
                      return static_cast<uint8_t>(a.type) < static_cast<uint8_t>(b.type);
                  return a.subject_id < b.subject_id;
              });

    w.write_u32(static_cast<uint32_t>(items.size()));
    for (const auto& item : items) {
        w.write_u32(item.due_tick);
        w.write_u8(static_cast<uint8_t>(item.type));
        w.write_u32(item.subject_id);
        // Payload variant index + fields
        w.write_u8(static_cast<uint8_t>(item.payload.index()));
        std::visit([&w](const auto& p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, EmptyPayload>) {
                // nothing
            } else if constexpr (std::is_same_v<T, ConsequencePayload>) {
                w.write_u32(p.consequence_id);
            } else if constexpr (std::is_same_v<T, TransitPayload>) {
                w.write_u32(p.shipment_id);
                w.write_u32(p.destination_province_id);
            } else if constexpr (std::is_same_v<T, NPCRelationshipDecayPayload>) {
                w.write_u32(p.npc_id);
            } else if constexpr (std::is_same_v<T, EvidenceDecayPayload>) {
                w.write_u32(p.evidence_token_id);
            } else if constexpr (std::is_same_v<T, NPCBusinessDecisionPayload>) {
                w.write_u32(p.business_id);
            } else if constexpr (std::is_same_v<T, MarketRecomputePayload>) {
                w.write_u32(p.good_id);
                w.write_u32(p.region_id);
            } else if constexpr (std::is_same_v<T, InvestigatorMeterPayload>) {
                w.write_u32(p.npc_id);
            } else if constexpr (std::is_same_v<T, MaturationPayload>) {
                w.write_u32(p.business_id);
                w.write_u32(p.node_key);
            } else if constexpr (std::is_same_v<T, CommercializePayload>) {
                w.write_u32(p.business_id);
                w.write_u32(p.node_key);
                w.write_u8(p.decision);
            }
        }, item.payload);
    }
}

void write_tariff_schedule(ByteWriter& w, const TariffSchedule& ts) {
    w.write_u32(ts.nation_id);
    w.write_u32(static_cast<uint32_t>(ts.good_tariff_rates.size()));
    for (const auto& [k, v] : ts.good_tariff_rates) {
        w.write_u32(k);
        w.write_float(v);
    }
    w.write_float(ts.default_tariff_rate);
    w.write_u32(static_cast<uint32_t>(ts.trade_agreements.size()));
    for (const auto& ta : ts.trade_agreements) {
        w.write_u32(ta.partner_nation_id);
        w.write_float(ta.tariff_reduction);
        w.write_u32(ta.signed_tick);
        w.write_u32(ta.expires_tick);
        w.write_bool(ta.is_active);
    }
}

void write_good_offer(ByteWriter& w, const GoodOffer& g) {
    w.write_u32(g.good_id);
    w.write_float(g.quantity_available);
    w.write_float(g.offer_price);
}

void write_trade_offer(ByteWriter& w, const NationalTradeOffer& o) {
    w.write_u32(o.nation_id);
    w.write_u32(o.tick_generated);
    w.write_u32(static_cast<uint32_t>(o.exports.size()));
    for (const auto& e : o.exports) write_good_offer(w, e);
    w.write_u32(static_cast<uint32_t>(o.imports.size()));
    for (const auto& i : o.imports) write_good_offer(w, i);
}

void write_lod1_stats(ByteWriter& w, const std::map<uint32_t, Lod1NationStats>& stats) {
    w.write_u32(static_cast<uint32_t>(stats.size()));
    for (const auto& [nation_id, ns] : stats) {
        w.write_u32(nation_id);
        w.write_u32(static_cast<uint32_t>(ns.production_by_good.size()));
        for (const auto& [k, v] : ns.production_by_good) {
            w.write_u32(k);
            w.write_float(v);
        }
        w.write_u32(static_cast<uint32_t>(ns.consumption_by_good.size()));
        for (const auto& [k, v] : ns.consumption_by_good) {
            w.write_u32(k);
            w.write_float(v);
        }
    }
}

void write_route_table(ByteWriter& w,
                       const std::map<std::pair<uint32_t, uint32_t>,
                                      std::array<RouteProfile, 5>>& table) {
    w.write_u32(static_cast<uint32_t>(table.size()));
    for (const auto& [key, routes] : table) {
        w.write_u32(key.first);
        w.write_u32(key.second);
        for (const auto& rp : routes) {
            w.write_float(rp.distance_km);
            w.write_float(rp.route_terrain_roughness);
            w.write_float(rp.min_infrastructure);
            w.write_u8(rp.hop_count);
            w.write_bool(rp.requires_sea_leg);
            w.write_bool(rp.requires_rail);
            w.write_float(rp.concealment_bonus);
            w.write_u32(static_cast<uint32_t>(rp.province_path.size()));
            for (uint32_t p : rp.province_path) w.write_u32(p);
        }
    }
}

// ── Read helpers for compound types ──────────────────────────────────────────

GeographyProfile read_geography(ByteReader& r) {
    GeographyProfile g{};
    g.latitude = r.read_float();
    g.longitude = r.read_float();
    g.elevation_avg_m = r.read_float();
    g.terrain_roughness = r.read_float();
    g.forest_coverage = r.read_float();
    g.arable_land_fraction = r.read_float();
    g.coastal_length_km = r.read_float();
    g.is_landlocked = r.read_bool();
    g.port_capacity = r.read_float();
    g.river_access = r.read_float();
    g.area_km2 = r.read_float();
    return g;
}

ClimateProfile read_climate(ByteReader& r) {
    ClimateProfile c{};
    c.koppen_zone = static_cast<KoppenZone>(r.read_u8());
    c.temperature_avg_c = r.read_float();
    c.temperature_min_c = r.read_float();
    c.temperature_max_c = r.read_float();
    c.precipitation_mm = r.read_float();
    c.precipitation_seasonality = r.read_float();
    c.drought_vulnerability = r.read_float();
    c.flood_vulnerability = r.read_float();
    c.wildfire_vulnerability = r.read_float();
    c.climate_stress_current = r.read_float();
    return c;
}

RegionDemographics read_demographics(ByteReader& r) {
    RegionDemographics d{};
    d.total_population = r.read_u32();
    d.median_age = r.read_float();
    d.education_level = r.read_float();
    d.income_low_fraction = r.read_float();
    d.income_middle_fraction = r.read_float();
    d.income_high_fraction = r.read_float();
    d.political_lean = r.read_float();
    return d;
}

CommunityState read_community(ByteReader& r) {
    CommunityState c{};
    c.cohesion = r.read_float();
    c.grievance_level = r.read_float();
    c.institutional_trust = r.read_float();
    c.resource_access = r.read_float();
    c.response_stage = r.read_u8();
    return c;
}

RegionalPoliticalState read_political(ByteReader& r) {
    RegionalPoliticalState p{};
    p.governing_office_id = r.read_u32();
    p.approval_rating = r.read_float();
    p.election_due_tick = r.read_u32();
    p.corruption_index = r.read_float();
    return p;
}

RegionConditions read_conditions(ByteReader& r) {
    RegionConditions c{};
    c.stability_score = r.read_float();
    c.inequality_index = r.read_float();
    c.crime_rate = r.read_float();
    c.addiction_rate = r.read_float();
    c.criminal_dominance_index = r.read_float();
    c.formal_employment_rate = r.read_float();
    c.regulatory_compliance_index = r.read_float();
    c.drought_modifier = r.read_float();
    c.flood_modifier = r.read_float();
    return c;
}

ProvinceLink read_province_link(ByteReader& r) {
    ProvinceLink l{};
    l.neighbor_h3 = r.read_u64();
    l.type = static_cast<LinkType>(r.read_u8());
    l.shared_border_km = r.read_float();
    l.transit_terrain_cost = r.read_float();
    l.infrastructure_bonus = r.read_float();
    return l;
}

ResourceDeposit read_resource_deposit(ByteReader& r) {
    ResourceDeposit rd{};
    rd.id = r.read_u32();
    rd.type = static_cast<ResourceType>(r.read_u8());
    rd.quantity = r.read_float();
    rd.quality = r.read_float();
    rd.depth = r.read_float();
    rd.accessibility = r.read_float();
    rd.depletion_rate = r.read_float();
    rd.quantity_remaining = r.read_float();
    return rd;
}

MemoryEntry read_memory_entry(ByteReader& r) {
    MemoryEntry m{};
    m.tick_timestamp = r.read_u32();
    m.type = static_cast<MemoryType>(r.read_u8());
    m.subject_id = r.read_u32();
    m.emotional_weight = r.read_float();
    m.decay = r.read_float();
    m.is_actionable = r.read_bool();
    return m;
}

KnowledgeEntry read_knowledge_entry(ByteReader& r) {
    KnowledgeEntry k{};
    k.subject_id = r.read_u32();
    k.secondary_subject_id = r.read_u32();
    k.type = static_cast<KnowledgeType>(r.read_u8());
    k.confidence = r.read_float();
    k.acquired_at_tick = r.read_u32();
    k.source_npc_id = r.read_u32();
    k.original_scope = static_cast<VisibilityScope>(r.read_u8());
    return k;
}

Relationship read_relationship(ByteReader& r) {
    Relationship rel{};
    rel.target_npc_id = r.read_u32();
    rel.trust = r.read_float();
    rel.fear = r.read_float();
    rel.obligation_balance = r.read_float();
    uint32_t secrets_count = r.read_u32();
    rel.shared_secrets.resize(secrets_count);
    for (uint32_t i = 0; i < secrets_count; ++i) rel.shared_secrets[i] = r.read_u32();
    rel.last_interaction_tick = r.read_u32();
    rel.is_movement_ally = r.read_bool();
    rel.recovery_ceiling = r.read_float();
    return rel;
}

MotivationVector read_motivation(ByteReader& r) {
    MotivationVector m{};
    for (float& w : m.weights) w = r.read_float();
    return m;
}

NPC read_npc(ByteReader& r) {
    NPC npc{};
    npc.id = r.read_u32();
    npc.role = static_cast<NPCRole>(r.read_u8());
    npc.motivations = read_motivation(r);
    npc.risk_tolerance = r.read_float();

    uint32_t mem_count = r.read_u32();
    npc.memory_log.resize(mem_count);
    for (uint32_t i = 0; i < mem_count; ++i) npc.memory_log[i] = read_memory_entry(r);

    uint32_t ke_count = r.read_u32();
    npc.known_evidence.resize(ke_count);
    for (uint32_t i = 0; i < ke_count; ++i) npc.known_evidence[i] = read_knowledge_entry(r);

    uint32_t kr_count = r.read_u32();
    npc.known_relationships.resize(kr_count);
    for (uint32_t i = 0; i < kr_count; ++i) npc.known_relationships[i] = read_knowledge_entry(r);

    uint32_t rel_count = r.read_u32();
    npc.relationships.resize(rel_count);
    for (uint32_t i = 0; i < rel_count; ++i) npc.relationships[i] = read_relationship(r);

    npc.capital = r.read_float();
    npc.social_capital = r.read_float();
    uint32_t contact_count = r.read_u32();
    npc.contact_ids.resize(contact_count);
    for (uint32_t i = 0; i < contact_count; ++i) npc.contact_ids[i] = r.read_u32();

    npc.movement_follower_count = r.read_u32();
    npc.home_province_id = r.read_u32();
    npc.current_province_id = r.read_u32();
    npc.travel_status = static_cast<NPCTravelStatus>(r.read_u8());
    npc.status = static_cast<NPCStatus>(r.read_u8());
    return npc;
}

EvidenceToken read_evidence_token(ByteReader& r) {
    EvidenceToken e{};
    e.id = r.read_u32();
    e.type = static_cast<EvidenceType>(r.read_u8());
    e.source_npc_id = r.read_u32();
    e.target_npc_id = r.read_u32();
    e.actionability = r.read_float();
    e.decay_rate = r.read_float();
    e.created_tick = r.read_u32();
    e.province_id = r.read_u32();
    e.is_active = r.read_bool();
    return e;
}

ObligationNode read_obligation(ByteReader& r) {
    ObligationNode o{};
    o.id = r.read_u32();
    o.creditor_npc_id = r.read_u32();
    o.debtor_npc_id = r.read_u32();
    o.favor_type = static_cast<FavorType>(r.read_u8());
    o.weight = r.read_float();
    o.created_tick = r.read_u32();
    o.is_active = r.read_bool();
    return o;
}

RegionalMarket read_market(ByteReader& r) {
    RegionalMarket m{};
    m.good_id = r.read_u32();
    m.province_id = r.read_u32();
    m.spot_price = r.read_float();
    m.equilibrium_price = r.read_float();
    m.adjustment_rate = r.read_float();
    m.supply = r.read_float();
    m.demand_buffer = r.read_float();
    m.import_price_ceiling = r.read_float();
    m.export_price_floor = r.read_float();
    return m;
}

NPCBusiness read_business(ByteReader& r) {
    NPCBusiness b{};
    b.id = r.read_u32();
    b.sector = static_cast<BusinessSector>(r.read_u8());
    b.profile = static_cast<BusinessProfile>(r.read_u8());
    b.cash = r.read_float();
    b.revenue_per_tick = r.read_float();
    b.cost_per_tick = r.read_float();
    b.market_share = r.read_float();
    b.strategic_decision_tick = r.read_u32();
    b.dispatch_day_offset = r.read_u8();
    b.actor_tech_state.effective_tech_tier = r.read_float();
    b.criminal_sector = r.read_bool();
    b.province_id = r.read_u32();
    b.regulatory_violation_severity = r.read_float();
    b.default_activity_scope = static_cast<VisibilityScope>(r.read_u8());
    b.owner_id = r.read_u32();
    b.deferred_salary_liability = r.read_float();
    b.accounts_payable_float = r.read_float();
    return b;
}

Province read_province(ByteReader& r) {
    Province p{};
    p.h3_index = r.read_u64();
    p.id = r.read_u32();
    p.fictional_name = r.read_string();
    p.real_world_reference = r.read_string();

    p.geography = read_geography(r);
    p.climate = read_climate(r);

    uint32_t dep_count = r.read_u32();
    p.deposits.resize(dep_count);
    for (uint32_t i = 0; i < dep_count; ++i) p.deposits[i] = read_resource_deposit(r);

    p.demographics = read_demographics(r);
    p.infrastructure_rating = r.read_float();
    p.agricultural_productivity = r.read_float();
    p.energy_cost_baseline = r.read_float();
    p.trade_openness = r.read_float();

    p.lod_level = static_cast<SimulationLOD>(r.read_u8());
    p.community = read_community(r);
    p.political = read_political(r);
    p.conditions = read_conditions(r);

    uint32_t npc_id_count = r.read_u32();
    p.significant_npc_ids.resize(npc_id_count);
    for (uint32_t i = 0; i < npc_id_count; ++i) p.significant_npc_ids[i] = r.read_u32();

    bool has_cohort = r.read_bool();
    if (has_cohort) {
        p.cohort_stats = new RegionCohortStats{};
        p.cohort_stats->total_population = r.read_u32();
        p.cohort_stats->median_age = r.read_float();
        p.cohort_stats->working_age_fraction = r.read_float();
        p.cohort_stats->dependency_ratio = r.read_float();
    } else {
        p.cohort_stats = nullptr;
    }

    p.has_karst = r.read_bool();
    p.historical_trauma_index = r.read_float();
    p.region_id = r.read_u32();
    p.nation_id = r.read_u32();

    uint32_t link_count = r.read_u32();
    p.links.resize(link_count);
    for (uint32_t i = 0; i < link_count; ++i) p.links[i] = read_province_link(r);

    uint32_t market_id_count = r.read_u32();
    p.market_ids.resize(market_id_count);
    for (uint32_t i = 0; i < market_id_count; ++i) p.market_ids[i] = r.read_u32();

    return p;
}

NationPoliticalCycleState read_nation_political(ByteReader& r) {
    NationPoliticalCycleState p{};
    p.current_administration_tick = r.read_u32();
    p.national_approval = r.read_float();
    p.election_campaign_active = r.read_bool();
    p.next_election_tick = r.read_u32();
    return p;
}

Nation read_nation(ByteReader& r) {
    Nation n{};
    n.id = r.read_u32();
    n.name = r.read_string();
    n.currency_code = r.read_string();
    n.government_type = static_cast<GovernmentType>(r.read_u8());
    n.political_cycle = read_nation_political(r);

    uint32_t prov_count = r.read_u32();
    n.province_ids.resize(prov_count);
    for (uint32_t i = 0; i < prov_count; ++i) n.province_ids[i] = r.read_u32();

    n.corporate_tax_rate = r.read_float();
    n.income_tax_rate_top_bracket = r.read_float();

    uint32_t diplo_count = r.read_u32();
    for (uint32_t i = 0; i < diplo_count; ++i) {
        uint32_t k = r.read_u32();
        float v = r.read_float();
        n.diplomatic_relations[k] = v;
    }

    n.tariff_schedule = nullptr;

    bool has_lod1 = r.read_bool();
    if (has_lod1) {
        Lod1NationProfile lp{};
        lp.export_margin = r.read_float();
        lp.import_premium = r.read_float();
        lp.trade_openness = r.read_float();
        lp.tech_tier_modifier = r.read_float();
        lp.population_modifier = r.read_float();
        lp.research_investment = r.read_float();
        lp.current_tier = r.read_u8();
        lp.stability_delta_this_month = r.read_float();
        lp.climate_stress_aggregate = r.read_float();
        lp.climate_vulnerability = r.read_float();
        lp.geographic_centroid_lat = r.read_float();
        lp.geographic_centroid_lon = r.read_float();
        lp.lod1_transit_variability_multiplier = r.read_float();
        lp.archetype = r.read_string();
        n.lod1_profile = lp;
    } else {
        n.lod1_profile = std::nullopt;
    }

    return n;
}

CalendarEntry read_calendar_entry(ByteReader& r) {
    CalendarEntry e{};
    e.id = r.read_u32();
    e.start_tick = r.read_u32();
    e.duration_ticks = r.read_u32();
    e.type = static_cast<CalendarEntryType>(r.read_u8());
    e.npc_id = r.read_u32();
    e.player_committed = r.read_bool();
    e.mandatory = r.read_bool();
    e.deadline_consequence.relationship_penalty = r.read_float();
    e.deadline_consequence.npc_initiative = r.read_bool();
    e.deadline_consequence.consequence_type = static_cast<ConsequenceType>(r.read_u8());
    e.deadline_consequence.consequence_severity = r.read_float();
    e.deadline_consequence.consequence_delay_ticks = r.read_u32();
    e.deadline_consequence.default_outcome_description = r.read_string();
    e.scene_card_id = r.read_u32();
    return e;
}

SceneCard read_scene_card(ByteReader& r) {
    SceneCard s{};
    s.id = r.read_u32();
    s.type = static_cast<SceneCardType>(r.read_u8());
    s.setting = static_cast<SceneSetting>(r.read_u8());
    s.npc_id = r.read_u32();
    uint32_t dlg_count = r.read_u32();
    s.dialogue.resize(dlg_count);
    for (uint32_t i = 0; i < dlg_count; ++i) {
        s.dialogue[i].speaker_npc_id = r.read_u32();
        s.dialogue[i].text = r.read_string();
        s.dialogue[i].emotional_tone = r.read_float();
    }
    uint32_t choice_count = r.read_u32();
    s.choices.resize(choice_count);
    for (uint32_t i = 0; i < choice_count; ++i) {
        s.choices[i].id = r.read_u32();
        s.choices[i].label = r.read_string();
        s.choices[i].description = r.read_string();
        s.choices[i].consequence_id = r.read_u32();
    }
    s.npc_presentation_state = r.read_float();
    s.is_authored = r.read_bool();
    s.chosen_choice_id = r.read_u32();
    return s;
}

PlayerCharacter read_player(ByteReader& r) {
    PlayerCharacter p{};
    p.id = r.read_u32();
    p.background = static_cast<Background>(r.read_u8());
    for (auto& t : p.traits) t = static_cast<Trait>(r.read_u8());
    p.starting_province_id = r.read_u32();

    p.health.current_health = r.read_float();
    p.health.lifespan_projection = r.read_float();
    p.health.base_lifespan = r.read_float();
    p.health.exhaustion_accumulator = r.read_float();
    p.health.degradation_rate = r.read_float();

    p.age = r.read_float();

    p.reputation.public_business = r.read_float();
    p.reputation.public_political = r.read_float();
    p.reputation.public_social = r.read_float();
    p.reputation.street = r.read_float();

    p.wealth = r.read_float();
    p.net_assets = r.read_float();

    uint32_t skill_count = r.read_u32();
    p.skills.resize(skill_count);
    for (uint32_t i = 0; i < skill_count; ++i) {
        p.skills[i].domain = static_cast<SkillDomain>(r.read_u8());
        p.skills[i].level = r.read_float();
        p.skills[i].decay_rate = r.read_float();
        p.skills[i].last_exercise_tick = r.read_u32();
    }

    uint32_t ea_count = r.read_u32();
    p.evidence_awareness_map.resize(ea_count);
    for (uint32_t i = 0; i < ea_count; ++i) {
        p.evidence_awareness_map[i].token_id = r.read_u32();
        p.evidence_awareness_map[i].discovery_tick = r.read_u32();
        p.evidence_awareness_map[i].source_npc_id = r.read_u32();
    }

    uint32_t on_count = r.read_u32();
    p.obligation_node_ids.resize(on_count);
    for (uint32_t i = 0; i < on_count; ++i) p.obligation_node_ids[i] = r.read_u32();

    uint32_t ce_count = r.read_u32();
    p.calendar_entry_ids.resize(ce_count);
    for (uint32_t i = 0; i < ce_count; ++i) p.calendar_entry_ids[i] = r.read_u32();

    p.residence_id = r.read_u32();
    p.partner_npc_id = r.read_u32();
    uint32_t child_count = r.read_u32();
    p.children_npc_ids.resize(child_count);
    for (uint32_t i = 0; i < child_count; ++i) p.children_npc_ids[i] = r.read_u32();
    p.designated_heir_npc_id = r.read_u32();

    uint32_t rel_count = r.read_u32();
    p.relationships.resize(rel_count);
    for (uint32_t i = 0; i < rel_count; ++i) p.relationships[i] = read_relationship(r);

    p.network_health.overall_score = r.read_float();
    p.network_health.network_reach = r.read_float();
    p.network_health.network_density = r.read_float();
    p.network_health.vulnerability = r.read_float();
    p.movement_follower_count = r.read_u32();

    uint32_t ms_count = r.read_u32();
    p.milestone_log.resize(ms_count);
    for (uint32_t i = 0; i < ms_count; ++i) {
        p.milestone_log[i].type = static_cast<MilestoneType>(r.read_u8());
        p.milestone_log[i].achieved_tick = r.read_u32();
        p.milestone_log[i].context_summary = r.read_string();
        p.achieved_milestones.insert(p.milestone_log[i].type);
    }

    p.home_province_id = r.read_u32();
    p.current_province_id = r.read_u32();
    p.travel_status = static_cast<NPCTravelStatus>(r.read_u8());

    p.restoration_history.restoration_count = r.read_u32();
    uint32_t rr_count = r.read_u32();
    p.restoration_history.records.resize(rr_count);
    for (uint32_t i = 0; i < rr_count; ++i) {
        p.restoration_history.records[i].restoration_index = r.read_u32();
        p.restoration_history.records[i].restored_to_tick = r.read_u32();
        p.restoration_history.records[i].restoration_real_tick = r.read_u32();
        p.restoration_history.records[i].ticks_erased = r.read_u32();
        p.restoration_history.records[i].tier_applied = r.read_u8();
    }

    uint32_t mod_count = r.read_u32();
    p.calendar_capacity_modifiers.resize(mod_count);
    for (uint32_t i = 0; i < mod_count; ++i) {
        p.calendar_capacity_modifiers[i].delta = r.read_float();
        p.calendar_capacity_modifiers[i].expires_tick = r.read_u32();
        p.calendar_capacity_modifiers[i].source = static_cast<ModifierSource>(r.read_u8());
    }

    p.ironman_eligible = r.read_bool();
    return p;
}

DeferredWorkQueue read_deferred_work_queue(ByteReader& r) {
    DeferredWorkQueue queue;
    uint32_t count = r.read_u32();
    for (uint32_t i = 0; i < count; ++i) {
        DeferredWorkItem item{};
        item.due_tick = r.read_u32();
        item.type = static_cast<WorkType>(r.read_u8());
        item.subject_id = r.read_u32();
        uint8_t variant_idx = r.read_u8();
        switch (variant_idx) {
            case 0: item.payload = EmptyPayload{}; break;
            case 1: {
                ConsequencePayload cp{};
                cp.consequence_id = r.read_u32();
                item.payload = cp;
                break;
            }
            case 2: {
                TransitPayload tp{};
                tp.shipment_id = r.read_u32();
                tp.destination_province_id = r.read_u32();
                item.payload = tp;
                break;
            }
            case 3: {
                NPCRelationshipDecayPayload np{};
                np.npc_id = r.read_u32();
                item.payload = np;
                break;
            }
            case 4: {
                EvidenceDecayPayload ep{};
                ep.evidence_token_id = r.read_u32();
                item.payload = ep;
                break;
            }
            case 5: {
                NPCBusinessDecisionPayload bp{};
                bp.business_id = r.read_u32();
                item.payload = bp;
                break;
            }
            case 6: {
                MarketRecomputePayload mp{};
                mp.good_id = r.read_u32();
                mp.region_id = r.read_u32();
                item.payload = mp;
                break;
            }
            case 7: {
                InvestigatorMeterPayload ip{};
                ip.npc_id = r.read_u32();
                item.payload = ip;
                break;
            }
            case 8: {
                MaturationPayload mp{};
                mp.business_id = r.read_u32();
                mp.node_key = r.read_u32();
                item.payload = mp;
                break;
            }
            case 9: {
                CommercializePayload cp{};
                cp.business_id = r.read_u32();
                cp.node_key = r.read_u32();
                cp.decision = r.read_u8();
                item.payload = cp;
                break;
            }
            default:
                item.payload = EmptyPayload{};
                break;
        }
        queue.push(item);
    }
    return queue;
}

TariffSchedule read_tariff_schedule(ByteReader& r) {
    TariffSchedule ts{};
    ts.nation_id = r.read_u32();
    uint32_t rate_count = r.read_u32();
    for (uint32_t i = 0; i < rate_count; ++i) {
        uint32_t k = r.read_u32();
        float v = r.read_float();
        ts.good_tariff_rates[k] = v;
    }
    ts.default_tariff_rate = r.read_float();
    uint32_t ta_count = r.read_u32();
    ts.trade_agreements.resize(ta_count);
    for (uint32_t i = 0; i < ta_count; ++i) {
        ts.trade_agreements[i].partner_nation_id = r.read_u32();
        ts.trade_agreements[i].tariff_reduction = r.read_float();
        ts.trade_agreements[i].signed_tick = r.read_u32();
        ts.trade_agreements[i].expires_tick = r.read_u32();
        ts.trade_agreements[i].is_active = r.read_bool();
    }
    return ts;
}

NationalTradeOffer read_trade_offer(ByteReader& r) {
    NationalTradeOffer o{};
    o.nation_id = r.read_u32();
    o.tick_generated = r.read_u32();
    uint32_t exp_count = r.read_u32();
    o.exports.resize(exp_count);
    for (uint32_t i = 0; i < exp_count; ++i) {
        o.exports[i].good_id = r.read_u32();
        o.exports[i].quantity_available = r.read_float();
        o.exports[i].offer_price = r.read_float();
    }
    uint32_t imp_count = r.read_u32();
    o.imports.resize(imp_count);
    for (uint32_t i = 0; i < imp_count; ++i) {
        o.imports[i].good_id = r.read_u32();
        o.imports[i].quantity_available = r.read_float();
        o.imports[i].offer_price = r.read_float();
    }
    return o;
}

}  // anonymous namespace

// ═════════════════════════════════════════════════════════════════════════════
// PersistenceModule — static method implementations
// ═════════════════════════════════════════════════════════════════════════════

uint32_t PersistenceModule::compute_checksum(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

bool PersistenceModule::is_schema_compatible(uint32_t saved_version, uint32_t current_version) {
    return saved_version <= current_version;
}

bool PersistenceModule::needs_migration(uint32_t saved_version, uint32_t current_version) {
    return saved_version < current_version;
}

bool PersistenceModule::is_save_allowed(bool cross_province_buffer_empty) {
    return cross_province_buffer_empty;
}

RestoreResult PersistenceModule::check_restore_preconditions(bool is_ironman, bool is_restoring) {
    if (is_ironman) return RestoreResult::locked_ironman_mode;
    if (is_restoring) return RestoreResult::already_restoring;
    return RestoreResult::success;
}

bool PersistenceModule::is_snapshot_tick(uint32_t current_tick) {
    return (current_tick % SNAPSHOT_INTERVAL) == 0;
}

uint8_t PersistenceModule::compute_disruption_tier(uint32_t restoration_count) {
    if (restoration_count == 0) return 0;
    if (restoration_count <= 2) return 1;
    if (restoration_count <= 5) return 2;
    return 3;
}

// ═════════════════════════════════════════════════════════════════════════════
// serialize — WorldState -> LZ4-compressed flat binary
// ═════════════════════════════════════════════════════════════════════════════

std::vector<uint8_t> PersistenceModule::serialize(const WorldState& state) {
    ByteWriter w;

    // --- Global scalars ---
    w.write_u32(state.current_tick);
    w.write_u64(state.world_seed);
    w.write_u32(state.ticks_this_session);
    w.write_u8(static_cast<uint8_t>(state.game_mode));
    w.write_u32(state.current_schema_version);
    w.write_bool(state.network_health_dirty);

    // --- Nations ---
    w.write_u32(static_cast<uint32_t>(state.nations.size()));
    for (const auto& n : state.nations) write_nation(w, n);

    // --- Provinces ---
    w.write_u32(static_cast<uint32_t>(state.provinces.size()));
    for (const auto& p : state.provinces) write_province(w, p);

    // --- Regions ---
    w.write_u32(static_cast<uint32_t>(state.region_groups.size()));
    for (const auto& r : state.region_groups) {
        w.write_u32(r.id);
        w.write_string(r.fictional_name);
        w.write_u32(r.nation_id);
        w.write_u32(static_cast<uint32_t>(r.province_ids.size()));
        for (uint32_t pid : r.province_ids) w.write_u32(pid);
    }

    // --- NPCs ---
    w.write_u32(static_cast<uint32_t>(state.significant_npcs.size()));
    for (const auto& npc : state.significant_npcs) write_npc(w, npc);

    w.write_u32(static_cast<uint32_t>(state.named_background_npcs.size()));
    for (const auto& npc : state.named_background_npcs) write_npc(w, npc);

    // --- Player ---
    bool has_player = (state.player != nullptr);
    w.write_bool(has_player);
    if (has_player) write_player(w, *state.player);

    // --- Economy ---
    w.write_u32(static_cast<uint32_t>(state.regional_markets.size()));
    for (const auto& m : state.regional_markets) write_market(w, m);

    w.write_u32(static_cast<uint32_t>(state.npc_businesses.size()));
    for (const auto& b : state.npc_businesses) write_business(w, b);

    // --- Evidence ---
    w.write_u32(static_cast<uint32_t>(state.evidence_pool.size()));
    for (const auto& e : state.evidence_pool) write_evidence_token(w, e);

    // --- DeferredWorkQueue ---
    write_deferred_work_queue(w, state.deferred_work_queue);

    // --- Obligation network ---
    w.write_u32(static_cast<uint32_t>(state.obligation_network.size()));
    for (const auto& o : state.obligation_network) write_obligation(w, o);

    // --- Calendar ---
    w.write_u32(static_cast<uint32_t>(state.calendar.size()));
    for (const auto& c : state.calendar) write_calendar_entry(w, c);

    // --- Scene cards ---
    w.write_u32(static_cast<uint32_t>(state.pending_scene_cards.size()));
    for (const auto& s : state.pending_scene_cards) write_scene_card(w, s);

    // --- Trade infrastructure ---
    w.write_u32(static_cast<uint32_t>(state.tariff_schedules.size()));
    for (const auto& ts : state.tariff_schedules) write_tariff_schedule(w, ts);

    w.write_u32(static_cast<uint32_t>(state.lod1_trade_offers.size()));
    for (const auto& o : state.lod1_trade_offers) write_trade_offer(w, o);

    // lod2_price_index
    bool has_lod2 = (state.lod2_price_index != nullptr);
    w.write_bool(has_lod2);
    if (has_lod2) {
        w.write_u32(state.lod2_price_index->last_updated_tick);
        w.write_u32(static_cast<uint32_t>(state.lod2_price_index->lod2_price_modifier.size()));
        for (const auto& [k, v] : state.lod2_price_index->lod2_price_modifier) {
            w.write_u32(k);
            w.write_float(v);
        }
    }

    write_lod1_stats(w, state.lod1_national_stats);
    write_route_table(w, state.province_route_table);

    // --- Uncompressed data ready ---
    const auto& raw = w.data();
    uint32_t raw_size = static_cast<uint32_t>(raw.size());
    uint32_t checksum = compute_checksum(raw.data(), raw.size());

    // --- LZ4 compression ---
    int max_compressed = LZ4_compressBound(static_cast<int>(raw_size));
    std::vector<uint8_t> result(HEADER_SIZE + static_cast<size_t>(max_compressed));

    // Write header
    auto write_le32 = [&](size_t offset, uint32_t v) {
        result[offset]     = static_cast<uint8_t>(v & 0xFF);
        result[offset + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
        result[offset + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
        result[offset + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
    };
    write_le32(0, MAGIC_BYTES);
    write_le32(4, CURRENT_SCHEMA_VERSION);
    write_le32(8, raw_size);
    write_le32(12, checksum);

    int compressed_size = LZ4_compress_default(
        reinterpret_cast<const char*>(raw.data()),
        reinterpret_cast<char*>(result.data() + HEADER_SIZE),
        static_cast<int>(raw_size),
        max_compressed);

    result.resize(HEADER_SIZE + static_cast<size_t>(compressed_size));
    return result;
}

// ═════════════════════════════════════════════════════════════════════════════
// deserialize — LZ4-compressed flat binary -> WorldState
// ═════════════════════════════════════════════════════════════════════════════

RestoreResult PersistenceModule::deserialize(const std::vector<uint8_t>& data,
                                              WorldState& out_state) {
    if (data.size() < HEADER_SIZE) return RestoreResult::io_error;

    auto read_le32 = [&](size_t offset) -> uint32_t {
        return static_cast<uint32_t>(data[offset])
             | (static_cast<uint32_t>(data[offset+1]) << 8)
             | (static_cast<uint32_t>(data[offset+2]) << 16)
             | (static_cast<uint32_t>(data[offset+3]) << 24);
    };

    uint32_t magic = read_le32(0);
    if (magic != MAGIC_BYTES) return RestoreResult::io_error;

    uint32_t schema_ver = read_le32(4);
    if (!is_schema_compatible(schema_ver, CURRENT_SCHEMA_VERSION))
        return RestoreResult::schema_too_new;

    uint32_t raw_size = read_le32(8);
    uint32_t expected_checksum = read_le32(12);

    // Decompress
    std::vector<uint8_t> raw(raw_size);
    int decompressed = LZ4_decompress_safe(
        reinterpret_cast<const char*>(data.data() + HEADER_SIZE),
        reinterpret_cast<char*>(raw.data()),
        static_cast<int>(data.size() - HEADER_SIZE),
        static_cast<int>(raw_size));

    if (decompressed < 0 || static_cast<uint32_t>(decompressed) != raw_size)
        return RestoreResult::io_error;

    // Verify checksum
    uint32_t actual_checksum = compute_checksum(raw.data(), raw.size());
    if (actual_checksum != expected_checksum)
        return RestoreResult::checksum_mismatch;

    // Parse flat binary
    ByteReader r(raw.data(), raw.size());

    out_state.current_tick = r.read_u32();
    out_state.world_seed = r.read_u64();
    out_state.ticks_this_session = r.read_u32();
    out_state.game_mode = static_cast<GameMode>(r.read_u8());
    out_state.current_schema_version = r.read_u32();
    out_state.network_health_dirty = r.read_bool();

    // Nations
    uint32_t nation_count = r.read_u32();
    out_state.nations.resize(nation_count);
    for (uint32_t i = 0; i < nation_count; ++i) out_state.nations[i] = read_nation(r);

    // Provinces
    uint32_t prov_count = r.read_u32();
    out_state.provinces.resize(prov_count);
    for (uint32_t i = 0; i < prov_count; ++i) out_state.provinces[i] = read_province(r);

    // Regions
    uint32_t region_count = r.read_u32();
    out_state.region_groups.resize(region_count);
    for (uint32_t i = 0; i < region_count; ++i) {
        out_state.region_groups[i].id = r.read_u32();
        out_state.region_groups[i].fictional_name = r.read_string();
        out_state.region_groups[i].nation_id = r.read_u32();
        uint32_t pc = r.read_u32();
        out_state.region_groups[i].province_ids.resize(pc);
        for (uint32_t j = 0; j < pc; ++j) out_state.region_groups[i].province_ids[j] = r.read_u32();
    }

    // NPCs
    uint32_t sig_count = r.read_u32();
    out_state.significant_npcs.resize(sig_count);
    for (uint32_t i = 0; i < sig_count; ++i) out_state.significant_npcs[i] = read_npc(r);

    uint32_t bg_count = r.read_u32();
    out_state.named_background_npcs.resize(bg_count);
    for (uint32_t i = 0; i < bg_count; ++i) out_state.named_background_npcs[i] = read_npc(r);

    // Player
    bool has_player = r.read_bool();
    if (has_player) {
        out_state.player = new PlayerCharacter(read_player(r));
    } else {
        out_state.player = nullptr;
    }

    // Markets
    uint32_t market_count = r.read_u32();
    out_state.regional_markets.resize(market_count);
    for (uint32_t i = 0; i < market_count; ++i) out_state.regional_markets[i] = read_market(r);

    // Businesses
    uint32_t biz_count = r.read_u32();
    out_state.npc_businesses.resize(biz_count);
    for (uint32_t i = 0; i < biz_count; ++i) out_state.npc_businesses[i] = read_business(r);

    // Evidence
    uint32_t ev_count = r.read_u32();
    out_state.evidence_pool.resize(ev_count);
    for (uint32_t i = 0; i < ev_count; ++i) out_state.evidence_pool[i] = read_evidence_token(r);

    // DeferredWorkQueue
    out_state.deferred_work_queue = read_deferred_work_queue(r);

    // Obligations
    uint32_t ob_count = r.read_u32();
    out_state.obligation_network.resize(ob_count);
    for (uint32_t i = 0; i < ob_count; ++i) out_state.obligation_network[i] = read_obligation(r);

    // Calendar
    uint32_t cal_count = r.read_u32();
    out_state.calendar.resize(cal_count);
    for (uint32_t i = 0; i < cal_count; ++i) out_state.calendar[i] = read_calendar_entry(r);

    // Scene cards
    uint32_t sc_count = r.read_u32();
    out_state.pending_scene_cards.resize(sc_count);
    for (uint32_t i = 0; i < sc_count; ++i) out_state.pending_scene_cards[i] = read_scene_card(r);

    // Tariff schedules
    uint32_t ts_count = r.read_u32();
    out_state.tariff_schedules.resize(ts_count);
    for (uint32_t i = 0; i < ts_count; ++i) out_state.tariff_schedules[i] = read_tariff_schedule(r);

    // Trade offers
    uint32_t to_count = r.read_u32();
    out_state.lod1_trade_offers.resize(to_count);
    for (uint32_t i = 0; i < to_count; ++i) out_state.lod1_trade_offers[i] = read_trade_offer(r);

    // LOD2 price index
    bool has_lod2 = r.read_bool();
    if (has_lod2) {
        out_state.lod2_price_index = new GlobalCommodityPriceIndex{};
        out_state.lod2_price_index->last_updated_tick = r.read_u32();
        uint32_t lod2_count = r.read_u32();
        for (uint32_t i = 0; i < lod2_count; ++i) {
            uint32_t k = r.read_u32();
            float v = r.read_float();
            out_state.lod2_price_index->lod2_price_modifier[k] = v;
        }
    } else {
        out_state.lod2_price_index = nullptr;
    }

    // LOD1 stats
    uint32_t l1_count = r.read_u32();
    out_state.lod1_national_stats.clear();
    for (uint32_t i = 0; i < l1_count; ++i) {
        uint32_t nid = r.read_u32();
        Lod1NationStats ns{};
        uint32_t prod_count = r.read_u32();
        for (uint32_t j = 0; j < prod_count; ++j) {
            uint32_t k = r.read_u32();
            float v = r.read_float();
            ns.production_by_good[k] = v;
        }
        uint32_t cons_count = r.read_u32();
        for (uint32_t j = 0; j < cons_count; ++j) {
            uint32_t k = r.read_u32();
            float v = r.read_float();
            ns.consumption_by_good[k] = v;
        }
        out_state.lod1_national_stats[nid] = ns;
    }

    // Route table
    uint32_t rt_count = r.read_u32();
    out_state.province_route_table.clear();
    for (uint32_t i = 0; i < rt_count; ++i) {
        uint32_t from = r.read_u32();
        uint32_t to = r.read_u32();
        std::array<RouteProfile, 5> routes{};
        for (auto& rp : routes) {
            rp.distance_km = r.read_float();
            rp.route_terrain_roughness = r.read_float();
            rp.min_infrastructure = r.read_float();
            rp.hop_count = r.read_u8();
            rp.requires_sea_leg = r.read_bool();
            rp.requires_rail = r.read_bool();
            rp.concealment_bonus = r.read_float();
            uint32_t path_len = r.read_u32();
            rp.province_path.resize(path_len);
            for (uint32_t j = 0; j < path_len; ++j) rp.province_path[j] = r.read_u32();
        }
        out_state.province_route_table[{from, to}] = routes;
    }

    // CrossProvinceDeltaBuffer is always empty at save/load time
    out_state.cross_province_delta_buffer.entries.clear();

    if (r.has_error()) return RestoreResult::io_error;
    return RestoreResult::success;
}

// ═════════════════════════════════════════════════════════════════════════════
// execute — tick-time autosave check
// ═════════════════════════════════════════════════════════════════════════════

void PersistenceModule::execute(const WorldState& state, DeltaBuffer& /*delta*/) {
    if (is_snapshot_tick(state.current_tick)) {
        // In full implementation: serialize(state) to background thread,
        // write to disk, generate SnapshotSummary companion file.
        // For now, snapshot serialization is invoked externally by the game loop.
    }
}

}  // namespace econlife
