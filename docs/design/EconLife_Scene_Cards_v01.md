# EconLife — Scene Card Rulebook
*Version 0.1 — Companion document to GDD v1.7 and TDD v29*
*Companion document to: EconLife_Technical_Design_v29.md §9 (SceneCard struct), §8 (Calendar)*

---

> **Document Purpose**
> This document is the human-readable design specification for the `SceneCard` system defined in TDD §9. Every field in the `SceneCard` struct has a corresponding rule here. Every player-facing behaviour of the scene card queue is specified here. The TDD defines the data; this document defines what the data means for players, artists, and writers.

---

## 1. Card Classification

Every scene card is assigned one of three classes at generation time. Class determines interruption behaviour, fast-forward rules, queue priority, and default outcomes. Class is set by the module that generates the card and stored as `SceneCard.card_class`.

```
enum CardClass {
    mandatory,       // must be engaged before normal play resumes
    timed_optional,  // pauses fast-forward; default outcome fires on expiry
    ambient          // never interrupts; queued for player pickup
};
```

### 1.1 Mandatory

Fast-forward stops at end of current tick. Simulation continues (the world does not pause), but the player's interaction scope is restricted to this card until it is engaged. Multiple Mandatory cards queue and are delivered sequentially. All must be resolved before fast-forward can resume.

**When a card is Mandatory:**

| Event | Notes |
|---|---|
| Legal summons / court appearance | Cannot be deferred; a default outcome of contempt fires if ignored via any means |
| ObligationNode status → `critical` or `hostile` (creditor demand delivery) | The demand arrives as a scene card. It is not optional. |
| Player character death notification (NPC attack, health failure) | Session-ending or succession-triggering card |
| Serious injury to player character (from violence or accident) | Requires medical decision |
| Criminal territorial war declaration targeting the player | Rival organization opens formal conflict |
| Legal verdict delivery | Charges resolved; sentence or acquittal rendered |

**What the player can do:** Engage the card (make a choice from available options) or, for non-legal Mandatory cards, defer it once (pushes the card to end of the Mandatory queue; available one time per card). Deferring a legal Mandatory card is not available — it fires its contempt consequence and clears automatically.

### 1.2 Timed-Optional

Fast-forward pauses for **8 real-time seconds** when a Timed-Optional card arrives. The card is presented with a visible countdown. The player can engage it (make a choice), dismiss it (card clears; default outcome fires immediately), or allow the countdown to expire (default outcome fires; card clears). Fast-forward resumes automatically when the card resolves.

**When a card is Timed-Optional:**

| Event | Default Outcome if Dismissed / Expired |
|---|---|
| Inbound NPC meeting request | NPC treats request as declined; relationship impact scaled to NPC sensitivity (0.0–1.0 range; mean: −0.08) |
| Personal life event (partner scene, child milestone, old friend) | Scene is skipped; relationship investment counter for that NPC is not incremented; accumulated skips degrade relationship over time |
| Whistleblower emergence notification (contact warning) | NPC proceeds without player intervention; contacts an investigator on their own timeline (within 30–90 ticks) |
| ObligationNode status → `escalated` (first escalation demand) | NPC registers no response; node remains `escalated`; `current_demand` continues growing |
| Manager escalation requiring player decision | Manager acts on their autonomous default (defined in delegation settings); player notified via Ambient card of the outcome |
| Criminal contact approach (first or new contact) | Contact does not approach again for 60–180 ticks; opportunity may not recur |

**8-second countdown rationale:** Long enough to register visually and create a real decision moment. Short enough that mass dismissal of low-priority cards does not require extended attention. The countdown is not punitive — it is a forcing function that gives the simulation-running fast-forward state a way to deliver time-sensitive information without fully stopping the game.

### 1.3 Ambient

Never interrupts the player. Never affects fast-forward. Card is added to the Communications layer queue silently. Badge count increments. The player reads it when they choose to. Ambient cards do not expire — they accumulate until read or manually cleared.

**When a card is Ambient:**

| Event |
|---|
| General news from media outlets |
| Market price signals and commodity reports |
| Contact intelligence reports and check-ins |
| ObligationNode → `open` (initial favour receipt; brief confirmation) |
| Operational updates from delegated managers (non-escalation) |
| First-time overlay confirmations (system introductions after first trigger) |
| Financial notifications (payment received, expense cleared) |
| Skill gain notifications |

**Queue cap:** Maximum 50 Ambient cards in queue. When cap is exceeded, the oldest unread Ambient card is silently cleared and logged to the device reference log (accessible to the player at any time). No content is permanently lost — only the active notification. The reference log is the player's permanent record of everything that was sent.

---

## 2. Queue Ordering

Cards are held in a three-tier priority queue. Delivery order within each tier is FIFO (first generated, first delivered). The queue is evaluated each tick after all tick modules have run and the DeltaBuffer has been applied.

```
Queue priority (highest to lowest):
  Tier 1: mandatory
  Tier 2: timed_optional
  Tier 3: ambient
```

**Tier 2 cap (Timed-Optional):** Maximum 12 cards active at once. If the 13th Timed-Optional card is generated while 12 are already queued, the incoming card is demoted to Ambient. Its `card_class` is updated; its default outcome no longer fires on a timer — it must be manually engaged or cleared. The player receives no notification of demotion. This prevents extreme event density from creating an unplayable attention demand.

**Multiple Mandatory cards:** All Mandatory cards must be resolved before fast-forward can resume. They are delivered one at a time in generation order. The player cannot skip forward through them — each requires its own engagement or deferral.

**Cross-tier delivery:** After all Mandatory cards are resolved, if any Timed-Optional cards are pending, they are delivered in FIFO order, each with its own 8-second window. Ambient cards are never delivered with countdown behavior, regardless of queue state.

---

## 3. Default Outcomes

Every Timed-Optional card has a specified default outcome. The default is a real simulation consequence, not a null event. Dismissing a card is a decision with a result.

**Default outcome principle:** The default should be the most conservative available option — the outcome that preserves the status quo most closely while acknowledging the player's non-engagement. It is never the worst possible outcome. The worst outcomes come from bad active choices, not from card dismissal. This prevents the system from feeling punitive about non-engagement.

| Card Type | Default Outcome | Severity |
|---|---|---|
| NPC meeting request | Request declined; relationship trust −0.05 to −0.15 depending on NPC sensitivity | Low |
| Personal life event | Scene skipped; relationship not invested; accumulated skip count +1 | Low (accumulates) |
| Whistleblower notification | NPC takes independent action; player loses window to intervene | Medium |
| ObligationNode escalation demand | No response registered; node continues escalating | Medium (deferred consequence) |
| Manager escalation | Manager autonomous default fires; Ambient notification sent | Low to Medium |
| Criminal contact approach | Contact withdraws for 60–180 ticks | Low to Medium |

---

## 4. Fast-Forward Interruption Rules

**Complete specification for all fast-forward states:**

| State | Condition | Player Action Required | Resume Condition |
|---|---|---|---|
| Fast-forward stops | Mandatory card in queue at tick end | Engage or defer all Mandatory cards | All Mandatory cards resolved |
| Fast-forward pauses | Timed-Optional card arrives | Engage, dismiss, or wait | Card resolved (by any means) |
| Fast-forward unaffected | Ambient card arrives | None | N/A — fast-forward continues |
| Fast-forward suppressed | Player in committed calendar entry (CalendarEntry.is_mandatory = true) | Engage scene card linked to entry | Entry's scene card resolved |

**HUD indicator during stop:** A badge reading "ATTENTION REQUIRED" appears in the top-center HUD position with a count of pending Mandatory cards. The fast-forward control is greyed and non-interactive. The badge clears when count reaches zero.

**HUD indicator during pause:** A card preview appears at the bottom of the active layer view (non-obstructive; upper portion of card visible with a countdown timer overlay). Player can tap/click to bring it to full view or dismiss directly from preview.

**Resuming fast-forward:** When all Mandatory cards are resolved and no Timed-Optional card is actively presenting, the fast-forward control returns to its last set speed automatically. The player does not need to re-engage the control.

---

## 5. NPC Presentation State

`SceneCard.npc_presentation_state` is a float (0.0–1.0) generated from the presenting NPC's `relationship_score` with the player, their `risk_tolerance`, and their `motivation` weights at the time the card is generated. It is read-only by the UI — never modified after generation.

The float communicates the NPC's emotional state through visual and dialogue design. It is the primary readable signal in a scene card — before any dialogue is read, the player should be able to sense the quality of the interaction from the NPC's presentation.

**Artist and writer guide:**

| Range | NPC State | Visual Signals | Dialogue Signals |
|---|---|---|---|
| 0.0–0.25 | Hostile or deeply fearful | Figure turned away or positioned at angle; tight framing; dark, cool palette; low ambient light; tense body posture | Short, clipped exchanges; avoids direct answers; deflects; doesn't meet eye line in portrait |
| 0.26–0.50 | Guarded or uncertain | Neutral body language; standard framing; desaturated palette; measured posture | Measured, careful language; answers questions without elaborating; visible hesitation |
| 0.51–0.75 | Comfortable and cooperative | Open body language; standard-to-wide framing; warm-neutral palette; relaxed posture | Volunteers context; answers fully; may offer information not directly asked for |
| 0.76–1.0 | Deeply trusting or loyal | Leaning toward viewer; widest framing; warmest palette; no defensive body language | Speaks freely; shares sensitive information; proactively raises concerns the player hasn't asked about |

**Setting interaction:** The `SceneSetting` (boardroom, parking_garage, home_dining, etc.) establishes the base visual environment. The `npc_presentation_state` determines the palette and emotional register applied to that setting. The same boardroom at 0.2 (hostile) reads very differently from a boardroom at 0.9 (loyal meeting with a trusted ally). Settings are not neutral — they carry social weight that interacts with the NPC's state. See setting notes in §6.

---

## 6. Visual Style Specification

**Illustration style:** Painted. Oil or gouache-adjacent. Visible brushwork. Atmosphere over fine detail.

**What this means in practice:**
- Readable at screen scale before close inspection — the emotional content of a scene is legible at a glance; fine detail is a reward for looking, not a requirement for understanding
- Same setting, multiple registers — the same `boardroom` setting should support a 0.1-state card (tense, grey, threatening) and a 0.9-state card (open, warm, collegial) through palette and stroke weight, not through recomposition. The environment is the same; the feeling is entirely different.
- Roughness is consistent — the brushwork texture is intentional and uniform across all cards, not a marker of lower production quality. Lower-tier NPCs do not receive more "rushed" illustrations. The roughness is a style choice, not a signal of importance.
- Scene cards are the only painted element in the interface — all UI chrome (dashboards, calendars, maps, HUD) is typographic and clean. The contrast marks scene cards as the "happening now" layer — the lived moment inside the management system. This distinction must be maintained: no illustrated elements should appear in dashboards or maps.

**Palette architecture:**
- Each of the 27 `SceneSetting` environments has a base palette: dominant hues, lighting temperature, and shadow density that establish the space.
- `npc_presentation_state` modulates the base palette: low values (hostile) push the palette toward cooler, darker, higher-contrast reads; high values (trusting) push toward warmer, lighter, lower-contrast reads. The base palette is never fully overridden — only modulated.
- This means a `home_dining` setting at 0.1 (hostile family confrontation) and 0.9 (warm, connected dinner) are still recognizably the same space — painted in different emotional light.

**NPC portrait placement:** The primary NPC occupies a consistent position in the card (left or right foreground depending on the setting convention established for their role type). Secondary figures, when present, are background. No more than three figures in any scene card. Crowd scenes are environmental — no individual portraits.

**Authored vs. templated card investment priority:**

| Priority | Card Types | Target |
|---|---|---|
| 1 — Fully authored | Path-opening scene cards (first session); ObligationNode `hostile` action delivery; player character death/injury; criminal war declaration | Unique illustrations + authored dialogue per NPC archetype |
| 2 — Authored variations + template remainder | ObligationNode escalation demands (per NPC archetype × status tier); personal life milestones (partner, children); legal verdicts; first-encounter cards for new NPC roles | Authored dialogue; templated illustration variants |
| 3 — Templated with NPC state injection | Routine meeting cards; manager escalations; contact check-ins; news delivery | Template illustrations with palette modulation by `npc_presentation_state`; templated dialogue filled from NPC state |

---

## 7. Cross-Reference Index

| Rule | TDD Reference |
|---|---|
| `SceneCard` struct definition | TDD v29 §9 |
| `SceneSetting` enum (27 values) | TDD v29 §9 |
| `npc_presentation_state` field | TDD v29 §9 |
| `pending_scene_cards` queue in WorldState | TDD v29 §10 |
| DeltaBuffer scene card delivery | TDD v29 §3 (tick orchestrator) |
| Fast-forward suppression during mandatory calendar entries | TDD v29 §8 (CalendarEntry) |
| ObligationNode status transitions that generate scene cards | TDD v29 §7 |
| Obligation legibility framework (what is readable vs. opaque) | GDD §10.4a [PATCH] |
| Intentional opacity directive for obligation escalation | GDD §22.1 |
| First-time overlay system (contextual reveal) | GDD §22.1 |

---

*EconLife Scene Card Rulebook v0.1 — Companion to GDD v1.7 / TDD v29 — Living Document*
