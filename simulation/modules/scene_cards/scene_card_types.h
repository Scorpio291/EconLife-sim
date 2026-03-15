#pragma once

#include <cstdint>
#include <vector>

namespace econlife {

// Forward declarations for types defined in other domain headers
struct DialogueLine;   // NPC dialogue line; defined in dialogue module
struct PlayerChoice;   // player choice option with consequence payload; defined in dialogue module

// ---------------------------------------------------------------------------
// SceneSetting
// ---------------------------------------------------------------------------
// Complete enum from TDD Section 9. Each setting implies a visual atmosphere,
// privacy level, and constraints on available interactions.
enum class SceneSetting : uint8_t {
    // --- Business Interiors ---
    boardroom          = 0,   // Corporate meeting room. Formal, observed, professional.
    private_office     = 1,   // One-on-one in a private office. More candid than boardroom.
    open_plan_office   = 2,   // Visible to coworkers. Limits conversation sensitivity.
    factory_floor      = 3,   // Industrial facility operations area. Loud, physical.
    warehouse          = 4,   // Storage facility, distribution point. Liminal, private.
    construction_site  = 5,   // Active construction. Casual, physically demanding.
    laboratory         = 6,   // Research or production lab. Clinical, specialized.
                              //   Includes criminal lab settings.

    // --- Food and Hospitality ---
    restaurant         = 7,   // Semi-public dining. Moderate privacy. Good for
                              //   relationship-building meetings.
    cafe               = 8,   // Casual, public. Low sensitivity. Good for initial
                              //   contacts and low-stakes exchanges.
    hotel_lobby        = 9,   // Public but transitional. Deniable meeting location.
    nightclub          = 10,  // Loud, crowded. Privacy through noise. Criminal
                              //   contact venue. High visual atmosphere.

    // --- Government and Legal ---
    government_office  = 11,  // Official setting. Formal, on-record. Regulator,
                              //   politician, or civil servant interaction.
    courthouse         = 12,  // Legal proceedings location. High stakes. Mandatory
                              //   attendance possible.
    courtroom          = 13,  // Active trial or hearing. Structured, adversarial.
    police_station     = 14,  // Voluntary or compelled visit. Investigator interaction.
    prison_visiting    = 15,  // Contact with incarcerated NPC or player. Limited
                              //   communication, observed.
    prison_cell        = 16,  // Player character incarcerated. Constrained operational
                              //   state; scene cards limited to prison-available actions.

    // --- Public and Outdoor ---
    street_corner      = 17,  // Informal, urban. Criminal network access. Visible.
    public_park        = 18,  // Semi-private outdoor meeting. Surveillance-difficult
                              //   in low-tech jurisdictions.
    parking_garage     = 19,  // Classic discreet meeting location. Poor lighting,
                              //   private, physically exposed.
    political_rally    = 20,  // Public political event. Campaign setting.
                              //   Crowd interaction implied.

    // --- Residential and Personal ---
    home_dining        = 21,  // Dinner at player or NPC home. High trust intimacy.
                              //   Personal relationship signal.
    home_office        = 22,  // Player's personal working space. Personal, unobserved.
    hospital           = 23,  // Health event or visiting ill NPC. Emotionally weighted.

    // --- Remote Communication ---
    phone_call         = 24,  // Voice call. No visual. Deniable. Routine contact.
    video_call         = 25,  // Video link. Remote formal or semi-formal. Business
                              //   and political use.

    // --- Transit and Transitional ---
    moving_vehicle     = 26,  // Car, train, plane. Private but transitional.
                              //   Classic secure conversation setting.
};

// ---------------------------------------------------------------------------
// SceneCardType
// ---------------------------------------------------------------------------
// Derived from TDD Section 9 inline comment on SceneCard.type:
//   "meeting, call, personal_event, news_notification"
enum class SceneCardType : uint8_t {
    meeting            = 0,  // face-to-face NPC meeting
    call               = 1,  // phone or video call
    personal_event     = 2,  // personal / lifestyle scene
    news_notification  = 3   // news event delivered as a scene card
};

// ---------------------------------------------------------------------------
// SceneCard
// ---------------------------------------------------------------------------
// Scene cards are the primary interface through which the player experiences
// NPC interactions. They are 2D illustrated or rendered scenes with atmospheric
// framing, NPC portrait, dialogue, and player choice options.
//
// npc_presentation_state is derived from the NPC's relationship score with the
// player and their current risk tolerance -- a hostile NPC shows differently
// from a cooperative one. This is the visual feedback for relationship quality
// without any explicit relationship meter.
//
// Some scene cards are procedurally generated from NPC state; some are authored
// for high-significance interactions.
struct SceneCard {
    uint32_t                  id;
    SceneCardType             type;                    // meeting, call, personal_event, news_notification
    SceneSetting              setting;                 // see complete SceneSetting enum above
    uint32_t                  npc_id;                  // primary NPC
    std::vector<DialogueLine> dialogue;                // NPC says, driven by their state (forward-declared)
    std::vector<PlayerChoice> choices;                 // player options, each with consequence payload
                                                       // (forward-declared)
    float                     npc_presentation_state;  // 0.0 (hostile/closed) to 1.0 (open/cooperative)
                                                       // drives visual state of NPC portrait
};

}  // namespace econlife
