# EconLife — MVP Targets & Solutions
*Audit dated 2026-09-01. Measured against the build at `ee5589b`.*
*Scope reference: GDD §23 (V1 Vertical Slice), TDD v29 milestone table, Feature Tier List.*

---

## What "MVP" means here

The project's own documents already define it, in two places that agree:

- **TDD v29 §milestones:** *"V1 vertical slice (core economy + 1 career path) — Month 12 — Playable build: one region, business path, basic NPC simulation."*
- **99% Experiment §Month 12:** *"Human plays a playable build of the core loop for one to two days. This is the first real felt-correctness check."*

So the MVP target is not the V1 feature list. It is one sentence:

> **A human can play the business path for two days and the game answers back.**

Everything below is measured against that sentence, not against the Feature Tier List.

---

## The headline

**The world simulation is alive. The game is not connected to it.**

52 modules run a living economy and a 13,000-year emergent history with rise-and-fall
civilisational cycles. But a player who starts a business and plays a full simulated
year ends the year **10,000 poorer than they started**, exactly as old as their own
birthday, with an **empty calendar** and **13 blank scene cards they cannot read,
answer, or dismiss.**

That is not an inference from the code. It is a run.

### The measurement

`econlife_cli --interactive --seed 42 --npcs 500 --provinces 6`, one `start_business`
action (retail, province 0), then 365 ticks:

| | tick 1 | tick 365 |
|---|---|---|
| player wealth | 50,000 | **40,000** |
| player age | 30.0 | **30.0** |
| player health | 1.0 | **1.0** |
| reputation (business/political/social/street) | 0/0/0/0 | **0/0/0/0** |
| exhaustion | 0.0 | **0.0** |
| player business revenue_per_tick | — | **0.0** |
| player business cost_per_tick | — | **0.0** |
| player business cash | 10,000 | **10,000** |
| calendar entries | 0 | **0** |
| pending scene cards | 0 | **13, all empty** |

Meanwhile the world around them worked fine: 62 businesses, average NPC capital 29,367,
average spot price 135.18. The economy is running. The player is standing outside it
with their face against the glass.

A representative pending card, verbatim from the run:

```json
{"id": 0, "npc_id": 0, "npc_name": "NPC #0", "type": "news_notification",
 "setting": "other", "dialogue": [], "choices": [], "chosen_choice_id": 0}
```

---

## The targets, in three tiers

**Tier 0 — the loop does not close.** Without these there is nothing to play for two days.
**Tier 1 — the player does not change.** Playable, but the character is a spreadsheet row.
**Tier 2 — the world is unreachable and invisible.** Built, tested, and behind glass.

---

# Tier 0 — the loop does not close

## T0.1 — The player's business cannot produce anything

**Evidence.** After a full year the player's business shows `revenue_per_tick 0.0`,
`cost_per_tick 0.0`, and cash unchanged at its 10,000 seed. `production_module.cpp`
dispatches from `facilities_by_business_`, populated from `state.facilities`. The
business created by `handle_start_business` (`player_actions_module.cpp:207-238`) has no
`Facility`. Production therefore never visits it, and `financial_distribution` — which
*does* correctly route owner draws to `player_delta.wealth_delta` — has nothing to
distribute. `start_business` takes 10,000 and silently hands back an object that cannot
earn.

The only path from "player" to "facility" is the Phase 11 construction pipeline: own a
correctly-zoned property → `RequestConstructionBidsAction` → `AwardConstructionBidAction`
→ facility delivered ~90 ticks later. That pipeline is fully implemented and tested. It
is also entirely unreachable — none of its actions, nor the property purchase that must
precede them, are exposed by the UI bridge.

**This is the single blocking defect for the MVP.** The one career path the vertical
slice ships cannot generate a single unit of anything.

**Solutions, cheapest first:**

**(a) Expose `AcquireBusinessAction` (already built, ~1 day).** Phase 10 of the real
estate arc implemented business acquisition end to end: price from revenue, an owner
accept-roll, mortgage underwriting, a 60-tick due-diligence close, mid-deal-sale
protection — and facilities follow the business automatically because `Facility.business_id`
is unchanged by the transfer. The player buys a going concern and *immediately owns a
producing business.* The mechanism exists, is persisted, and has tests. Only the bridge
parser and a UI panel are missing. This is the fastest route from "no game" to "a game".

**(b) Add a commercial lease (`LeaseFacilityAction`, the designed entry).** Most real
businesses start by renting premises, not building them. Give `Facility` a vacancy/tenant
notion, let the player rent an idle facility in their province for a per-tick cost, and
set `business_id` on lease. This is the honest founding path and it is mechanically
defensible in real units (rent per square metre per tick). It also gives the existing
`real_estate` rental-yield model a second consumer.

**(c) Wire the construction pipeline through the bridge (the growth path).** Correct
long-term and already built; the ~90-tick lead time makes it the *expansion* verb, not
the *starting* verb. Do it after (a) or (b), never instead of them.

Whichever lands, `start_business` must stop silently charging for nothing: either it
fails loudly with a reason the UI can show, or it bundles a premises choice.

---

## T0.2 — Scene cards are structurally empty

**Evidence.** `random_events_module.cpp:346-359` constructs cards with a hardcoded
`sc.id = 0`, no dialogue and no choices. **No module anywhere in the codebase writes
`SceneCard::dialogue`** — the only mentions are persistence reading and writing an
always-empty vector. Only `real_estate` ever authors `choices` (its two negotiation
cards). The scene card system is the game's *primary interface* — the GDD cut the 3D
world and put scene cards in its place — and it currently renders nothing.

Two failures compound. Because `id == 0`, the player cannot address the card with
`scene_card_choice` even in principle. Because `choices` is empty, no choice can ever be
made, so nothing retires the card. They accumulate: 13 by the end of the year, capped
only by `max_scene_cards_per_tick`.

**Solutions:**

**(a) A data-driven template catalog.** `packages/base_game/scene_cards/*.csv`, keyed by
`card_key`, each row carrying dialogue lines with parameter placeholders
(`{npc_name}`, `{amount}`, `{province}`) and 2–3 choices with `consequence_id`s. This is
exactly how goods, recipes, facility types and the 502-node technology tree already work,
so it inherits the moddability doctrine for free and requires no recompilation to author.

**(b) A `SceneCardSeedDelta` channel.** Any module says *"raise card `landlord_raises_rent`
about NPC 412 with amount=1,800"*; `scene_cards` owns id assignment, template lookup,
parameter injection, presentation state and the per-tick cap. This is the established
pattern in this codebase (`LegalCaseSeedDelta`, `RacketSeedDelta`, `LaunderingSeedDelta`,
`RandomEventTriggerDelta`) and it fixes the `id = 0` defect at its source by giving card
identity a single owner.

**(c) Hygiene, immediately.** Every card gets a real id and at least a dismiss choice, so
a card can never wedge the queue. This is a one-hour change and it stops the pile-up
regardless of when (a) and (b) land.

**(d) Content, sized for the MVP not for V1.** The Feature Tier List asks for ~200
templates. That is the V1 number. **Twenty to thirty templates covering the business path
alone** is enough for a two-day felt-correctness check — supplier negotiation, a hire, a
poor performance review, a landlord, a regulator visit, a rival's offer, a journalist's
call. Author those, measure whether the loop is fun, then scale.

---

## T0.3 — The calendar is empty; time is not a constraint

**Evidence.** Zero calendar entries across 365 ticks. The only creators of `CalendarEntry`
in the entire codebase are two player-action handlers — `calendar_schedule` and
`initiate_contact` (`player_actions_module.cpp:138`, `:531`). **No NPC, business,
institution, or investigation ever asks for the player's time.** The calendar is a
notepad the player writes to themselves.

This matters more than it looks. The GDD deliberately cut the energy/action-token budget
and replaced it with time-as-constraint; the calendar is named "the primary
self-management tool" in V1. And `scene_cards` Phase 3 — the module's *only* non-placeholder
card pathway — triggers exclusively from calendar entries. An empty calendar means the
one designed card source never fires, which is why every card in the run came from the
`random_events` placeholder.

**Solution: give the calendar producers.** One `CalendarEntrySeedDelta` channel plus a
handful of emitters, each grounded in something that already exists in the world:

- NPCs with a relationship to the player request meetings (`influence_network`,
  `trust_updates` already hold the relationship graph);
- an owned business generates owner obligations — a supplier negotiation, a hiring
  decision, a quarterly review (`npc_business` already runs a strategic-decision cadence);
- `legal_process` and `investigator_engine` issue summons and interviews at the case
  stages they already model;
- `media_system` schedules an interview when a story about the player is running.

**T0.3 and T0.2 unblock each other**, which makes this pair the highest-leverage work in
the audit: calendar entries produce scene cards, scene cards resolve into consequences and
relationship changes, and the interaction loop closes for the first time.

---

## T0.4 — Nothing is ever saved

**Evidence.** `PersistenceModule::execute` (`persistence_module.cpp:2820-2826`) is an
empty stub whose entire body is a comment: *"In full implementation: serialize(state) to
background thread, write to disk... For now, snapshot serialization is invoked externally
by the game loop."* **No game loop invokes it.** There is no save-file I/O in the CLI at
all, and the interactive IPC protocol accepts only `tick`, `action` and `quit`.

Meanwhile the serializer is at **schema v15**, round-trips the private state of 11
modules, has backward-compatible readers for every version from v4, canonicalises
unordered-map order for byte-determinism, and is exercised by a large body of tests. All
of it is dead outside the test binary.

"Continuous autosave, no reload" is a V1 non-negotiable, and the GDD is explicit about
*why*: it is the architectural expression of causality — *"without it the consequence
queue is meaningless and the obligation network has no teeth."* A two-day playtest is
also not a two-day process; the session has to survive being closed.

**Solution — small, because the hard part is finished.** Give the CLI a save directory;
have `execute()` call `serialize()` on `is_snapshot_tick` and write to a rotating pair of
files (write-to-temp, fsync, rename, so a crash mid-write cannot destroy the previous
save); add `save`/`load` to the IPC protocol and `--load <path>` to the CLI. The existing
`is_save_allowed(cross_province_buffer_empty)` guard already handles the one-tick
cross-province propagation delay, which is the only subtle correctness question here.

---

# Tier 1 — the player does not change

## T1.1 — The player has no life clock

**Evidence.** Age 30.0 at tick 1, 30.0 at tick 365. `PlayerCharacter::age` is assigned
exactly once in the entire codebase, at `world_generator.cpp:553`, and never again. Health
is 1.0 flat. `lifespan_projection` and `base_lifespan` are set at generation
(`world_generator.cpp:549-550`), serialized, and never read by any mechanism.

The field's own declaration in `player.h:287` says: *"in-game years; increments each tick
by (1.0 / 365.0)"*. It does not. Per the Module Interface Contract, **the spec wins and
the implementation is the divergence.**

Everything downstream is therefore inert: aging and lifespan, the health degradation
model, terminal illness, sudden death, succession, the heir system, "death → what you set
up is what happens", and playing as the heir. That is the entire Progression & Legacy
pillar, and all of it is V1.

**Solution.** Extend `population_aging` — it already owns NPC aging and already emits
`age_delta` (`population_aging_module.cpp:587`) — or add a small `player_lifecycle`
module beside it. Advance the player's age, degrade health from age and accumulated
exhaustion, recompute `lifespan_projection`, and fire the terminal phase and death. The
model is already fully specified in `player.h`; **only the producer is missing.** Ground
mortality in the cohort tables `structural_demography` already maintains rather than a
fitted decay curve — a flat "health falls 0.02/yr" constant is precisely the kind of rail
`docs/design/EconLife_No_Rails_Rule.md` forbids.

---

## T1.2 — Nine of eleven player channels have no producer

`PlayerDelta` defines eleven channels. This is what writes them:

| channel | producers |
|---|---|
| `wealth_delta` | player_actions, financial_distribution, banking, real_estate, money_laundering, antitrust |
| `reputation_business_delta` | media_system only |
| `reputation_social_delta` | media_system only |
| `health_delta` | calendar only (scheduling load) |
| `exhaustion_delta` | calendar only |
| `new_travel_status` | player_actions |
| `skill_delta` | **none** |
| `new_evidence_awareness` | **none** |
| `relationship_delta` | **none** |
| `reputation_political_delta` | **none** |
| `new_province_id` | none — travel arrival writes the field directly in the core drain |

The player is a wallet. Money moves; nothing else about them does.

Three of these are worth calling out individually.

**`new_evidence_awareness` — zero producers.** The GDD names the player's evidence
awareness gap *"the primary late-game tension; must ship."* The evidence pool, four token
types, actionability scoring, investigator meters, the raid pipeline and media propagation
all exist and all work. The player is simply never told any of it. The channel is declared
in `delta_buffer.h:62`, merged in `delta_buffer.cpp:55`, and applied in
`apply_deltas.cpp:245` — **it has an applier and no writer.**

> *Solution:* one module owns an awareness rule, and every path is a real information
> channel rather than a stat readout — which is what the device-mediated doctrine asks
> for. The player learns of a token when (i) an NPC who knows it trusts the player enough
> to warn them, (ii) it surfaces in media coverage they can see, or (iii) it reaches
> institutional visibility — a subpoena, a public arrest record, a regulator's letter.
> Each of those already exists as a modelled event; each just needs to emit
> `new_evidence_awareness`.

**`skill_delta` — zero producers.** "Skill leveling (by doing) and skill rust (by
neglect)" is V1, and outside of money it is the player's *only* progression mechanic.

> *Solution:* `player_actions` is the natural owner — it already dispatches every action
> the player takes. Each handler names the `SkillDomain` it exercises and emits a
> `skill_delta`. Rust needs no new state at all: `PlayerSkill` already carries
> `last_exercise_tick` and `decay_rate` for exactly this purpose.

**`relationship_delta` — zero producers.** The whole influence architecture — trust,
obligation, fear — is defined as a relationship network rather than a stat, and the
player's side of that network never moves. `trust_updates` owns NPC↔NPC trust; it should
own player↔NPC too, driven by scene-card outcomes, kept promises, and obligation
settlement.

---

# Tier 2 — built, tested, and behind glass

## T2.1 — The bridge exposes 5 of 21 player actions

`parse_and_enqueue_action` (`interactive_json.cpp:343`) handles `travel`,
`scene_card_choice`, `calendar_commit`, `start_business` and `initiate_contact`. The other
sixteen are unreachable — including the **entire thirteen-phase real-estate arc** (listing,
offers, counter-offers, mortgages, foreclosure, auctions, zoning, subdivision, business
acquisition, construction contracts, property tax, liens, tax sales), which is implemented,
persisted across seven schema revisions, and covered by roughly ninety tests.

This is the best effort-to-visible-capability ratio in the project. The action payloads are
plain structs; one table-driven parser plus UI panels exposes all of it. **Do the two that
unblock T0.1 first** (`acquire_business`, `make_property_offer`).

## T2.2 — The UI sees seven fields

`serialize_ui_state` emits `{tick, date, player, provinces, businesses, calendar, metrics,
pending_scene_cards}`. Not exposed: evidence awareness, obligations, relationships and
contacts, facilities and production, market prices, legal cases, technology and R&D,
employment, media and news. The device-mediated information model says the player sees what
their contact network gives them. Right now it gives them almost nothing — and the
Operational, Communications and Map layers named in V1 have no data to render even if they
were built.

---

# The scope call that should be made explicitly

**Two of the three V1 career paths have no player hooks whatsoever.**

- **Politician.** `political_cycle_module.cpp` contains **zero references to the player.**
  No candidacy, no campaign, no coalition, no vote. V1 wants city council → head of state.
- **Criminal.** `drug_economy`, `criminal_operations` and `money_laundering` all
  `#include "core/world_state/player.h"` and then never reference `state.player`. The
  player cannot run a lab, a route, or a laundering front. (`money_laundering:166` carries
  a comment reading "Direct to player wealth" — but no player-owned operation ever exists
  to create that flow.)

The vertical slice is defined as "core economy + **1 career path**", so deferring these is
almost certainly *correct* — but right now it is an accident rather than a decision.

**Recommendation: write it down.** MVP is the business path only. The other two paths are
post-MVP, and the wiring pattern the business path establishes (an action set, a calendar
producer, a scene-card template family, a reputation channel) becomes the template for
both. Not deciding this is how a two-day playtest turns into a six-month one.

---

# Recommended order

Ordered by what unblocks what, not by size.

1. **Scene-card hygiene** (T0.2c) — real ids, a dismiss choice. Hours. Stops the queue wedging.
2. **Expose `acquire_business` + `make_property_offer`** (T2.1 → T0.1a). The player can own something that earns.
3. **Calendar producers + scene-card templates** (T0.3 + T0.2a/b). The pair that closes the interaction loop.
4. **Autosave and load** (T0.4). The playtest has to survive being closed.
5. **The life clock** (T1.1). The character starts existing in time.
6. **`skill_delta` and `new_evidence_awareness`** (T1.2). Progression, and the tension the GDD is built around.
7. **Widen the action surface and the state surface** (T2.1, T2.2). Everything else already built becomes reachable.

---

# The instrument this work needs

This project's method is: **measure it, record the negative result, encode it as a gate.**
That is how the emergence baseline retired six broken loops, and it is why the history arc
is trustworthy. The player-facing layer has no such instrument — which is precisely why a
year-long run in which the player earns nothing, ages not at all and accumulates thirteen
unreadable cards was not caught by 1,600 passing tests. **Every one of those tests passes
today.** They assert that modules work in isolation; nothing asserts that a player can
play.

**Build `simulation/tests/integration/player_loop_observe`, parallel to
`emergence_observe`.** Script a player-year and assert the things a human would notice on
day one:

- the player's wealth rises from *working*, not from the starting balance;
- the player is one year older, and their health has moved;
- at least one skill leveled and one rusted;
- scene cards were *resolved*, not accumulated — pending count is bounded and falls;
- the calendar was non-empty and something the player did not schedule appeared on it;
- at least one evidence token entered the awareness map;
- the save round-trips and the loaded world ticks identically.

Each assertion that fails today is a ratchet, in exactly the sense this codebase already
uses the word. Each one flipping green is an MVP target closed — and the same
human-light validation loop the 99% experiment is built around, pointed for the first time
at the player instead of the world.

---

*Audit method: static trace of every `PlayerDelta` channel, `SceneCard` field and
`CalendarEntry` producer across `simulation/modules/`, plus one instrumented 365-tick
interactive run at seed 42 with 500 NPCs and 6 provinces. Every number in this document
came from that run or from a named file and line.*
