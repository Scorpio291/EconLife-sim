#include "interactive_json.h"

#include <chrono>
#include <cmath>
#include <ctime>

#include "core/world_state/player.h"
#include "core/world_state/player_action_queue.h"
#include "modules/calendar/calendar_types.h"
#include "modules/economy/economy_types.h"
#include "modules/scene_cards/scene_card_types.h"
#include "modules/trade_infrastructure/trade_types.h"

namespace econlife {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string tick_to_date(uint32_t tick) {
    // Base date: January 1, 2000. One tick = one day.
    struct std::tm base{};
    base.tm_year = 100;  // 2000
    base.tm_mon = 0;
    base.tm_mday = 1 + static_cast<int>(tick);
    base.tm_hour = 12;
    std::mktime(&base);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", base.tm_year + 1900,
                  base.tm_mon + 1, base.tm_mday);
    return std::string(buf);
}

static const char* npc_role_str(NPCRole role) {
    switch (role) {
        case NPCRole::politician:          return "Politician";
        case NPCRole::candidate:           return "Candidate";
        case NPCRole::regulator:           return "Regulator";
        case NPCRole::law_enforcement:     return "Law Enforcement";
        case NPCRole::prosecutor:          return "Prosecutor";
        case NPCRole::judge:               return "Judge";
        case NPCRole::appointed_official:  return "Official";
        case NPCRole::corporate_executive: return "Executive";
        case NPCRole::middle_manager:      return "Manager";
        case NPCRole::worker:              return "Worker";
        case NPCRole::accountant:          return "Accountant";
        case NPCRole::banker:              return "Banker";
        case NPCRole::lawyer:              return "Lawyer";
        case NPCRole::media_editor:        return "Media Editor";
        case NPCRole::journalist:          return "Journalist";
        case NPCRole::ngo_investigator:    return "NGO Investigator";
        case NPCRole::community_leader:    return "Community Leader";
        case NPCRole::union_organizer:     return "Union Organizer";
        case NPCRole::criminal_operator:   return "Criminal Operator";
        case NPCRole::criminal_enforcer:   return "Enforcer";
        case NPCRole::fixer:               return "Fixer";
        case NPCRole::bodyguard:           return "Bodyguard";
        case NPCRole::family_member:       return "Family Member";
        default:                           return "Unknown";
    }
}

static const char* travel_status_str(NPCTravelStatus s) {
    switch (s) {
        case NPCTravelStatus::resident:   return "resident";
        case NPCTravelStatus::in_transit: return "in_transit";
        case NPCTravelStatus::visiting:   return "visiting";
        default:                          return "unknown";
    }
}

static const char* scene_card_type_str(SceneCardType t) {
    switch (t) {
        case SceneCardType::meeting:            return "meeting";
        case SceneCardType::call:               return "call";
        case SceneCardType::personal_event:     return "personal_event";
        case SceneCardType::news_notification:  return "news_notification";
        default:                                return "unknown";
    }
}

static const char* scene_setting_str(SceneSetting s) {
    switch (s) {
        case SceneSetting::boardroom:         return "boardroom";
        case SceneSetting::private_office:    return "private_office";
        case SceneSetting::open_plan_office:  return "open_plan_office";
        case SceneSetting::factory_floor:     return "factory_floor";
        case SceneSetting::warehouse:         return "warehouse";
        case SceneSetting::construction_site: return "construction_site";
        case SceneSetting::laboratory:        return "laboratory";
        default:                              return "other";
    }
}

static const char* calendar_type_str(CalendarEntryType t) {
    switch (t) {
        case CalendarEntryType::meeting:   return "meeting";
        case CalendarEntryType::event:     return "event";
        case CalendarEntryType::operation: return "operation";
        case CalendarEntryType::deadline:  return "deadline";
        case CalendarEntryType::personal:  return "personal";
        default:                           return "unknown";
    }
}

static const char* business_sector_str(BusinessSector s) {
    switch (s) {
        case BusinessSector::manufacturing:       return "manufacturing";
        case BusinessSector::food_beverage:       return "food_beverage";
        case BusinessSector::retail:              return "retail";
        case BusinessSector::services:            return "services";
        case BusinessSector::real_estate:         return "real_estate";
        case BusinessSector::agriculture:         return "agriculture";
        case BusinessSector::energy:              return "energy";
        case BusinessSector::technology:          return "technology";
        case BusinessSector::finance:             return "finance";
        case BusinessSector::transport_logistics: return "transport_logistics";
        case BusinessSector::media:               return "media";
        case BusinessSector::security:            return "security";
        case BusinessSector::research:            return "research";
        case BusinessSector::criminal:            return "criminal";
        default:                                  return "unknown";
    }
}

// Look up NPC role string by ID (for scene card / calendar display).
static std::string npc_display_name(const WorldState& world, uint32_t npc_id) {
    for (const auto& npc : world.significant_npcs) {
        if (npc.id == npc_id) {
            return std::string(npc_role_str(npc.role)) + " #" + std::to_string(npc_id);
        }
    }
    return "NPC #" + std::to_string(npc_id);
}

// ---------------------------------------------------------------------------
// Shared metric helpers
// ---------------------------------------------------------------------------

float compute_avg_npc_capital(const WorldState& world) {
    if (world.significant_npcs.empty()) return 0.0f;
    float total = 0.0f;
    for (const auto& npc : world.significant_npcs) total += npc.capital;
    return total / static_cast<float>(world.significant_npcs.size());
}

float compute_avg_spot_price(const WorldState& world) {
    if (world.regional_markets.empty()) return 0.0f;
    float total = 0.0f;
    for (const auto& m : world.regional_markets) total += m.spot_price;
    return total / static_cast<float>(world.regional_markets.size());
}

// ---------------------------------------------------------------------------
// State serialization
// ---------------------------------------------------------------------------

nlohmann::json serialize_ui_state(const WorldState& world) {
    using json = nlohmann::json;
    json state;

    state["tick"] = world.current_tick;
    state["date"] = tick_to_date(world.current_tick);

    // Player
    if (world.player) {
        const auto& p = *world.player;
        state["player"] = {
            {"id", p.id},
            {"wealth", p.wealth},
            {"health", p.health.current_health},
            {"exhaustion", p.health.exhaustion_accumulator},
            {"age", p.age},
            {"province_id", p.current_province_id},
            {"home_province_id", p.home_province_id},
            {"travel_status", travel_status_str(p.travel_status)},
            {"reputation", {
                {"business", p.reputation.public_business},
                {"political", p.reputation.public_political},
                {"social", p.reputation.public_social},
                {"street", p.reputation.street}
            }}
        };
    }

    // Pending scene cards
    json cards = json::array();
    for (const auto& card : world.pending_scene_cards) {
        json c;
        c["id"] = card.id;
        c["type"] = scene_card_type_str(card.type);
        c["setting"] = scene_setting_str(card.setting);
        c["npc_id"] = card.npc_id;
        c["npc_name"] = npc_display_name(world, card.npc_id);
        c["npc_presentation_state"] = card.npc_presentation_state;
        c["chosen_choice_id"] = card.chosen_choice_id;

        json dialogue = json::array();
        for (const auto& line : card.dialogue) {
            dialogue.push_back({
                {"speaker", npc_display_name(world, line.speaker_npc_id)},
                {"text", line.text},
                {"tone", line.emotional_tone}
            });
        }
        c["dialogue"] = dialogue;

        json choices = json::array();
        for (const auto& ch : card.choices) {
            choices.push_back({
                {"id", ch.id},
                {"label", ch.label},
                {"description", ch.description}
            });
        }
        c["choices"] = choices;
        cards.push_back(c);
    }
    state["pending_scene_cards"] = cards;

    // Calendar (player's entries)
    json calendar = json::array();
    for (const auto& entry : world.calendar) {
        // Include all entries — the UI can filter by relevance.
        calendar.push_back({
            {"id", entry.id},
            {"start_tick", entry.start_tick},
            {"start_date", tick_to_date(entry.start_tick)},
            {"duration_ticks", entry.duration_ticks},
            {"type", calendar_type_str(entry.type)},
            {"npc_id", entry.npc_id},
            {"npc_name", npc_display_name(world, entry.npc_id)},
            {"player_committed", entry.player_committed},
            {"mandatory", entry.mandatory},
            {"scene_card_id", entry.scene_card_id}
        });
    }
    state["calendar"] = calendar;

    // Provinces
    json provinces = json::array();
    for (const auto& prov : world.provinces) {
        provinces.push_back({
            {"id", prov.id},
            {"name", prov.fictional_name},
            {"population", prov.demographics.total_population},
            {"infrastructure", prov.infrastructure_rating},
            {"stability", prov.conditions.stability_score},
            {"crime", prov.conditions.crime_rate},
            {"grievance", prov.community.grievance_level},
            {"cohesion", prov.community.cohesion}
        });
    }
    state["provinces"] = provinces;

    // Player-owned businesses
    json businesses = json::array();
    if (world.player) {
        for (const auto& biz : world.npc_businesses) {
            if (biz.owner_id == world.player->id) {
                businesses.push_back({
                    {"id", biz.id},
                    {"sector", business_sector_str(biz.sector)},
                    {"province_id", biz.province_id},
                    {"cash", biz.cash},
                    {"revenue_per_tick", biz.revenue_per_tick},
                    {"cost_per_tick", biz.cost_per_tick},
                    {"output_quality", biz.output_quality}
                });
            }
        }
    }
    state["businesses"] = businesses;

    // Aggregate metrics
    state["metrics"] = {
        {"npc_count", world.significant_npcs.size()},
        {"business_count", world.npc_businesses.size()},
        {"avg_npc_capital", compute_avg_npc_capital(world)},
        {"avg_spot_price", compute_avg_spot_price(world)}
    };

    return state;
}

// ---------------------------------------------------------------------------
// Action parsing
// ---------------------------------------------------------------------------

bool parse_and_enqueue_action(const nlohmann::json& cmd, WorldState& world) {
    if (!cmd.contains("action_type") || !cmd.contains("payload"))
        return false;

    std::string action_type = cmd["action_type"].get<std::string>();
    const auto& payload = cmd["payload"];

    if (action_type == "travel") {
        uint32_t dest = payload.value("destination_province_id", 0u);
        enqueue_player_action(world, PlayerActionType::travel, TravelAction{dest});
        return true;
    }
    if (action_type == "scene_card_choice") {
        uint32_t card_id = payload.value("scene_card_id", 0u);
        uint32_t choice_id = payload.value("choice_id", 0u);
        enqueue_player_action(world, PlayerActionType::scene_card_choice,
                              SceneCardChoiceAction{card_id, choice_id});
        return true;
    }
    if (action_type == "calendar_commit") {
        uint32_t entry_id = payload.value("calendar_entry_id", 0u);
        bool accept = payload.value("accept", true);
        enqueue_player_action(world, PlayerActionType::calendar_commit,
                              CalendarCommitAction{entry_id, accept});
        return true;
    }
    if (action_type == "start_business") {
        std::string sector_str = payload.value("sector", "retail");
        uint32_t province_id = payload.value("province_id", 0u);
        // Map string to enum — default to retail.
        BusinessSector sector = BusinessSector::retail;
        if (sector_str == "manufacturing") sector = BusinessSector::manufacturing;
        else if (sector_str == "food_beverage") sector = BusinessSector::food_beverage;
        else if (sector_str == "services") sector = BusinessSector::services;
        else if (sector_str == "real_estate") sector = BusinessSector::real_estate;
        else if (sector_str == "agriculture") sector = BusinessSector::agriculture;
        else if (sector_str == "energy") sector = BusinessSector::energy;
        else if (sector_str == "technology") sector = BusinessSector::technology;
        else if (sector_str == "finance") sector = BusinessSector::finance;
        else if (sector_str == "transport_logistics") sector = BusinessSector::transport_logistics;
        else if (sector_str == "media") sector = BusinessSector::media;
        else if (sector_str == "security") sector = BusinessSector::security;
        else if (sector_str == "research") sector = BusinessSector::research;
        else if (sector_str == "criminal") sector = BusinessSector::criminal;
        enqueue_player_action(world, PlayerActionType::start_business,
                              StartBusinessAction{sector, province_id});
        return true;
    }
    if (action_type == "initiate_contact") {
        uint32_t npc_id = payload.value("target_npc_id", 0u);
        enqueue_player_action(world, PlayerActionType::initiate_contact,
                              InitiateContactAction{npc_id});
        return true;
    }

    return false;
}

}  // namespace econlife
