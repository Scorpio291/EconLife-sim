# EconLife — First Session Design
*Version 0.1 — Companion document to GDD v1.7*
*Cross-reference: GDD §4 (Character Creation), §5 (Three Challenges), §6 (Career Paths), §20 (UI/UX Philosophy), §22.1 (Development Philosophy — Contextual Reveal), EconLife_Scene_Cards_v01.md*

---

> **Document Purpose**
> This document specifies the design of the player's first 90 minutes in EconLife. It translates the philosophical commitments of GDD §22.1 (contextual reveal, intentional opacity, no tutorial) into a concrete, executable beat sequence for each of the three primary V1 career path starts. It also specifies the opening scene card that appears before any system is explained — the emotional stakes-setter that gives the player something to care about before they engage a dashboard.
>
> **What this document is not:** A scripted narrative. Every beat described here is a condition that the simulation must be capable of producing, not authored content the game plays at the player. The employee's first manager escalation is a real simulation event — specified here in terms of what conditions produce it and what the player can do about it, not as a cutscene.

---

## 1. Design Goals for Session 1

### What the player must understand by the end of session 1:
- What the calendar is for and how to use it
- How to initiate or respond to contact with an NPC
- What their character's current situation is (background, starting position, immediate constraints)
- That the world continues running when they are not acting — the simulation does not wait
- That their decisions have consequences they may not see immediately

### What must remain mysterious at the end of session 1:
- How fast obligations escalate (the node is visible; its nature is not explained)
- What their exposure level is (evidence tokens exist; the player has no window into them)
- What is happening in regions they have no contacts in (the map is blank there)
- How NPC knowledge will eventually affect them (workers and contacts have memory logs the player cannot see)

### Emotional tone:
The opening of EconLife is not triumphant. It is not the story of someone who has already succeeded. It is January 2000, and the player is at the beginning of a decade they cannot fully see. The appropriate emotional register is **weight of choice** — the sense that the decisions being made now matter, and that mattering is not immediately visible.

The player should finish session 1 feeling they are in motion, not feeling they have accomplished something. The accomplishments come later and are not announced.

---

## 2. The Opening Scene Card

Before the Communications layer is revealed. Before any system is explained. Before the player has touched a dashboard.

A single Mandatory scene card appears. It is the only authored card in session 1 that serves no system introduction purpose. Its only job is to establish the character in time and place — to give the player a person to inhabit before they start managing one.

**Card class:** Mandatory (dismiss only; no choices; no system consequence)
**Card type:** `personal_event`
**Setting:** Determined by starting background tier
**`npc_presentation_state`:** N/A — no NPC present; this is a solo establishing scene

The card has no choices. It has no system information. The player reads it and dismisses it. The simulation begins.

### Opening Card Variants

**POOR / WORKING CLASS** (`background: born_poor` or `background: working_class`)

*Setting:* `home_dining` — a kitchen in a modest apartment. Dawn light. Low light, warm from a single window. A stack of envelopes on the counter — utilities, mostly. The character is at the table with coffee. The mug is real; everything else feels provisional.

*Text (trait-variable):*
> "January 2000. [Starting region].
>
> You've been here [X] years. Long enough that the neighbourhood knows your face, not long enough that it knows your name.
>
> [TRAIT REFERENCE — one sentence placing the character's dominant trait in context. Examples: *You notice things other people miss. That's gotten you into trouble before.* / *You've always been better with people than with plans.* / *You've been thinking about this for a long time. You just haven't told anyone yet.*]
>
> The rent went up again. The decade turned over three weeks ago and nobody around here noticed. You noticed. You've been waiting for a reason to move.
>
> Today is a working day."

**MIDDLE CLASS** (`background: middle_class`)

*Setting:* `home_office` or `home_dining` depending on path intent (entrepreneur: home_office desk with papers; employee: dining table, work bag by the door). Morning. The news is on somewhere in the background — something about the economy, optimistic in the way morning news always is.

*Text (trait-variable):*
> "January 2000. [Starting region].
>
> You've been thinking about this for a while. Not just since New Year's — longer.
>
> [TRAIT REFERENCE. Examples: *You've always been more careful than you look.* / *Most people in your position would call this stable. You call it slow.* / *You've spent a long time watching how things work. Now you want to be on the other side of how they work.*]
>
> The question has never been whether you can do something. It's been what 'something' should be.
>
> The decade is new. That's as good a reason as any to find out."

**WEALTHY** (`background: wealthy`)

*Setting:* A well-appointed office or living room. Something valuable in the background — not ostentatious, just present. Art, or a city view, or both. The character is reviewing something. A document, an asset statement, something with numbers.

*Text (trait-variable):*
> "January 2000. [Starting region].
>
> You have more than most people will see in their lifetimes. This is a fact about your situation, not a conclusion about your character.
>
> [TRAIT REFERENCE. Examples: *The advantage you were given is real. What you do with it is the part that belongs to you.* / *You're harder to read than people expect. That's intentional.* / *The people around you know what you have. Very few of them know what you want.*]
>
> The world is about to change in ways nobody has fully mapped yet. The internet. Globalization. Things that don't have names yet. You intend to be ahead of it.
>
> The question, as always, is which version of ahead."

---

## 3. Contextual Reveal Architecture

Contextual overlays are brief, non-interrupting text panels that appear exactly once per system, the first time the player encounters it personally. They are dismissable immediately. They are stored in the device reference log and can be reviewed at any time.

**Overlay format:** Single sentence or two-sentence explanation. No jargon. No numerical values. Written in second person, present tense. Explains what the thing is and what the player can do about it — nothing else.

**Explicitly withheld in session 1 (no overlay fires):**

| System | Reason |
|---|---|
| ObligationNode creation | Intentionally opaque — favour is legible; obligation nature is not explained (see GDD §22.1 and §10.4a) |
| Evidence token generation | Player cannot see their own evidence tokens; no overlay would be accurate |
| NPC memory log | The player cannot see what NPCs know; no overlay would be accurate |
| Surveillance infrastructure | Player has no reason to know it exists yet |
| Escalation mechanics (any) | Opacity is the design; revealing the escalation path removes the experience of discovering it |

**Session 1 contextual reveal trigger map:**

| System | Trigger Condition | Overlay Text |
|---|---|---|
| Calendar | First CalendarEntry created | *"Your calendar shows what you've committed to and what's waiting for your attention. The world keeps moving whether you engage or not."* |
| Operational layer | First business or operation acquired | *"Your operations dashboard. Each business or operation you run appears here."* |
| Revenue / expense | First financial transaction | *"Money in and out of your operations appears here. Watch the margins."* |
| Skill gain | First `skill_level` increment | *"Skills improve through use. They rust without it."* |
| Fast-forward | First time player activates fast-forward | *"Time passes at whatever rate you set. The simulation doesn't pause — it keeps running."* |
| Scene card (Timed-Optional) | First Timed-Optional card with countdown | *"Some messages need a response before the moment passes. The world moves on either way."* |
| Map layer contact data | First contact established in a region | *"Your map shows what your contacts can see. No contacts in a region means no picture."* |

---

## 4. Path-Specific Beat Sheets

### 4.1 Employee Path

*Entry condition: player selected employee start in character creation. Opening scene card variant determined by background tier.*

**Beat sequence (first 90 in-game minutes / ~90 real-time minutes at 1× speed / shorter at higher speeds):**

**Beat 1 — Opening scene card** (tick 0)
Mandatory, dismiss only. See §2. Sets character in time and place.

**Beat 2 — Communications layer revealed** (tick 0, immediately after card dismisses)
The inbox is empty. One Ambient card is already present: a job listings notification for the starting region. A contextual overlay fires: "This is your communications layer. Messages and requests from the world arrive here." — dismissable.

**Beat 3 — First job application** (ticks 1–5)
Player opens the job listings Ambient card. Two or three entry-level positions are listed, matched to the player's starting skills and background. No overlay fires — the interaction is self-explanatory. Player initiates an application. A Timed-Optional scene card fires within 1–3 ticks: the hiring NPC. The card is a brief interview scene. `npc_presentation_state` = 0.55–0.70 (cooperative, slightly formal). Two choices: direct/confident or deferential/careful. No mechanically significant difference at this stage — both result in a hire if the player's skills meet the basic threshold. The purpose of the choice is to establish that choices exist and have a textural effect on the interaction.

**Beat 4 — First calendar entry** (immediately after hire)
A CalendarEntry appears for the first work shift, 2–3 ticks from now. Contextual overlay fires: calendar explanation. Player sees the calendar for the first time in context, with something already in it.

**Beat 5 — First work shift** (ticks 3–7)
The CalendarEntry triggers as Mandatory when its tick arrives. A brief scene card: a moment from the first day. The setting is workplace-appropriate for the industry (factory_floor, open_plan_office, restaurant, etc.). A colleague says something in passing — a piece of flavour that reflects a province condition (a mention of the local economy, something in the news, a workplace gripe). This dialogue is templated from province state — it is not authored content, but it is the first time the player experiences the simulation speaking through an NPC naturally. Skill gain: +0.05 to the relevant skill. Contextual overlay fires: skill explanation.

**Beat 6 — First pay** (ticks 5–10)
An Ambient card: first wage received. Brief, informational. No overlay. The first money the player has earned is not announced — it arrives as it would in life. A contextual overlay fires on the financial transaction system: revenue/expense explanation.

**Beat 7 — First manager escalation** (ticks 10–20)
A Timed-Optional scene card arrives: the player's manager wants something. Either a task slightly outside the player's formal role (cover someone's shift, handle a minor problem, make a judgment call). This is the first real choice with a visible downstream consequence. `npc_presentation_state` = 0.45–0.60 (comfortable but slightly pressured). Default outcome if dismissed: manager registers the non-response as unreliability; relationship trust −0.07.

**Beat 8 — First ObligationNode (path-specific, intentionally opaque)** (ticks 15–30)
A colleague or a contact from the player's background network approaches with a small personal favour — nothing to do with the job. The nature of the favour matches the background tier and starting traits. If the player accepts: first ObligationNode created. **No contextual overlay fires.** The node appears in the obligation network view. The HUD badge does not activate (count is 0 for an `open` node). The player has taken on an obligation. They do not know what they have taken on.

> [SCENARIO: scenario_employee_path_session1_obligation_opaque]
> Test: When a player on the employee path accepts a first favour (generating ObligationNode status=`open`) in session 1, no contextual overlay fires and no HUD badge appears.
> Seed setup: Employee path start; player accepts first favour from peer NPC at tick 20; ObligationNode created with status=`open`.
> Run length: 5 ticks after acceptance.
> Assertion: contextual_overlay_fired(system=obligation) = false; HUD_obligation_badge_count = 0.

---

### 4.2 Entrepreneur Path

*Entry condition: player selected entrepreneur start with appropriate starting capital from background tier.*

**Beat 1 — Opening scene card** (tick 0)
Mandatory, dismiss only. Home office variant. The character is looking at options.

**Beat 2 — First real decision** (ticks 1–3)
A Timed-Optional scene card fires before the Communications layer is introduced. A contact has a lead — a property, a deal, an introduction. The card presents the first material decision: how to deploy starting capital, or which sector to enter, or which region to anchor in. This choice happens before any system is explained. The point is that the game trusts the player to make decisions before they're tutorialized. Default outcome if dismissed: the lead disappears; the player starts with a direct property search instead (lower-quality options, more expensive).

**Beat 3 — Operational layer unlocks** (immediately after first acquisition)
The player acquires a business space, signs a lease, or makes a founding decision. The Operational layer populates with a first tile. Contextual overlay fires: operations dashboard explanation.

**Beat 4 — First hire** (ticks 3–8)
A Timed-Optional scene card: hiring. The player can hire or skip. If skipped: the business opens understaffed; production is slow; a note in the Operational layer shows reduced output. If hired: a worker NPC enters the simulation with their own motivation and memory log. The player does not see this — they see a staff count increment. The worker has already begun accumulating a memory of Day 1.

**Beat 5 — First operational dashboard** (ticks 3–10)
The Operational overview tile for the business shows a visual status: low activity. Nothing is moving yet. The visual feedback principle (§20.4) fires before any metric does: the tile looks quiet before any number confirms it.

**Beat 6 — First revenue or shortfall** (ticks 5–15)
An Ambient card: first financial result. Either a small revenue or a negative cash flow (depending on setup choices). Contextual overlay fires: revenue/expense explanation.

**Beat 7 — First supplier negotiation** (ticks 8–20)
A Timed-Optional scene card: an NPC supplier approaches with a price for an input the business needs. Player can accept at market rate, counter-offer (requires a Trade skill check; low-level skill means moderate success odds), or pass (alternative supplier at slightly worse terms). `npc_presentation_state` = 0.50–0.65.

**Beat 8 — First ObligationNode (intentionally opaque)** (ticks 10–25)
The supplier — or a new contact from the business network — offers better terms in exchange for a favour to be named later. The offer is presented in natural language: "Do this for me and I'll make it worth your while." No system label is attached. If accepted: first ObligationNode created. No overlay. No badge (status=`open`). The node is in the network. The player has made a deal.

> [SCENARIO: scenario_entrepreneur_path_session1_obligation_opaque]
> Test: When a player on the entrepreneur path accepts a "favour to be named later" offer (ObligationNode status=`open`), no contextual overlay fires.
> Seed setup: Entrepreneur path start; NPC offers `open` obligation at tick 18; player accepts.
> Run length: 5 ticks.
> Assertion: contextual_overlay_fired(system=obligation) = false.

---

### 4.3 Criminal Path

*Entry condition: player selected street-smart trait and/or poor/working class background with no business start. The criminal path has no explicit menu selection — it is entered by the player's response to encounter conditions.*

**Beat 1 — Opening scene card** (tick 0)
Mandatory, dismiss only. Poor/working class variant. The weight of the opening is the same as any other path. The character is not introduced as a criminal — they are introduced as a person at the beginning of something.

**Beat 2 — Communications layer** (tick 0)
Inbox is empty. No job board card fires automatically. A note about a low-wage job is present as ambient, but it is not foregrounded. The criminal path begins through encounter, not through system navigation.

**Beat 3 — First grey-area contact** (ticks 3–8)
Within the first week, a Timed-Optional scene card arrives: someone approaches through a mutual. The setting is appropriate — `street_corner`, `cafe`, or `moving_vehicle`. They don't introduce themselves by role. They say they heard the player handles things quietly. The `npc_presentation_state` = 0.35–0.50 (cautious, measuring). The player has three options: engage (ask what they mean), deflect (non-committal acknowledgement), or walk away. **No contextual overlay fires.** This is not the criminal system being introduced — it is a person, behaving like a person, seeing if the player is interested.

If player walks away: the contact does not approach again for 60–120 ticks. An alternative, lower-value contact may eventually appear. The criminal path is still available; the specific on-ramp changes.

**Beat 4 — First offer** (ticks 8–15, if player engaged Beat 3)
A second Timed-Optional scene card: the contact makes an offer. A protection arrangement, or a courier job, or a small logistical favour for someone who prefers not to ask through official channels. The nature of the work is vague. The pay is specified (a concrete number that meaningfully exceeds what the entry-level job board was offering). Player can accept, decline, or ask questions. Asking questions produces deflection — the NPC doesn't explain their operation to someone they've just met. Default if dismissed: deal is offered once more at lower pay; declined again = contact withdraws for 90 ticks.

**Beat 5 — First payment** (ticks 12–20)
An Ambient card: payment received. No special flagging. It is money, arriving like any other money. The only difference is there is no corresponding record in any legitimate operation — a gap in the paper trail that the player may not yet know matters.

**Beat 6 — First OPSEC moment** (ticks 15–25)
An Ambient card: a police patrol is active in the area. The card provides no instructions. It is a piece of information. `street_reputation` = 0.0 at this point — the patrol is not specifically looking for the player. What the player does with this information (change behaviour, note the patrol timing, ignore it) is their choice. No overlay fires. No system is introduced. This is the world, running.

**Beat 7 — First ObligationNode (intentionally opaque)** (ticks 20–35)
The contact asks a small favour in return: relay a message to a named NPC. Or hold something for a day. Or provide information about a route they take regularly. If accepted: first ObligationNode created. No overlay. No badge. The player has done a favour. What it becomes is for them to discover.

> [SCENARIO: scenario_criminal_path_session1_no_system_intro]
> Test: A player on the criminal path who has completed all three criminal beats (contact approach, offer acceptance, first payment) has not received any contextual overlay about the criminal economy, OPSEC, or street reputation systems.
> Seed setup: Criminal path conditions; player accepts all three beats through tick 25.
> Run length: 30 ticks.
> Assertion: contextual_overlay_fired(system=criminal_economy) = false AND contextual_overlay_fired(system=opsec) = false AND contextual_overlay_fired(system=street_reputation) = false.

---

## 5. Session 1 Success Criteria

The following scenario tags define what a successful session 1 looks like from a simulation and testing perspective. These are not goals the player is shown — they are evaluation criteria for the design.

> [SCENARIO: scenario_session1_calendar_engagement]
> Test: By the end of session 1 (tick 100, approximately 3 in-game months), the player has at least 1 completed CalendarEntry.
> Seed setup: Any path; normal play.
> Run length: 100 ticks.
> Assertion: PlayerCharacter.calendar.completed_entries.count ≥ 1.

> [SCENARIO: scenario_session1_npc_relationship_established]
> Test: By tick 100, the player has at least 1 non-zero Relationship record with any NPC (any type, any direction).
> Seed setup: Any path; normal play.
> Run length: 100 ticks.
> Assertion: WorldState.relationships WHERE player_is_party.count ≥ 1.

> [SCENARIO: scenario_session1_unread_ambient_card]
> Test: By tick 100, the player's Communications queue contains at least 1 Ambient card they have not read.
> Rationale: The world should be generating more information than the player can fully attend to, even in session 1. An unread card signals that the simulation is already outpacing the player's attention — establishing the pressure that grows throughout the game.
> Seed setup: Any path; normal play.
> Run length: 100 ticks.
> Assertion: WorldState.pending_scene_cards WHERE card_class=ambient AND player_has_read=false.count ≥ 1.

> [SCENARIO: scenario_session1_real_consequence_from_player_action]
> Test: By tick 100, at least one NPC has a non-zero emotional_weight memory entry attributable to a player action.
> Rationale: Session 1 must establish that player decisions have real effects on the simulation, even if those effects are not yet visible to the player.
> Seed setup: Any path; player has taken at least one action that interacts with an NPC (hired, met, accepted or declined a request).
> Run length: 100 ticks.
> Assertion: Any Significant or Named NPC has a memory_log entry WHERE emotional_weight > 0.0 AND source = player_action.

---

## 6. V1 Feature Compliance Check

All beats in this document reference only V1-tier features from the Feature Tier List. The following items are confirmed V1:

- Employee path (§ Feature Tier List: Career Paths — Employee, V1)
- Entrepreneur path (§ Feature Tier List: Career Paths — Entrepreneur/Business Owner, V1)
- Criminal Operator path (§ Feature Tier List: Career Paths — Criminal Operator, V1)
- Scene cards (§ Feature Tier List: UI/UX — Scene cards for NPC interactions, V1)
- Calendar interface (§ Feature Tier List: UI/UX — Calendar interface, V1)
- ObligationNode system (§ Feature Tier List: Power/Exposure — Obligation network, V1)
- Contextual system introduction (§ Feature Tier List: UI/UX — Contextual system introduction, V1)
- Skill leveling by doing (§ Feature Tier List: Player Character — Skill leveling, V1)
- NPC memory log (§ Feature Tier List: Simulation Foundation — NPC memory log, V1)

**No EX-tier features are introduced in session 1.** The Union Leader path (EX), Infiltrator path (EX), Substance use/addiction for player (EX), and all expansion criminal content are not referenced or triggered in any session 1 beat.

---

*EconLife First Session Design v0.1 — Companion to GDD v1.7 — Living Document*
