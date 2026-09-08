# MVP vertical slice — completion report (2026-09-08)

**Goal.** A human can play the BUSINESS career across multiple sessions: make
meaningful decisions, experience consequences, progress as a character,
save/exit/reload, and get feedback from the living simulation. The world already
worked; the task was to connect the player to it, not to expand simulation scope.

**Branch.** `claude/mvp-targets-solutions-vsjor8`, nine commits from `e08c9b0`
(the audit) through `9de8dcf` (the card catalog).

**Gates at the final commit.**

| gate | result |
|---|---|
| `ctest -LE emergence` (fast per-commit) | **1899/1899 pass** |
| `econlife_player_loop_tests "[player_loop]"` | **17 pass, 1 failing-as-expected ratchet** |
| `ctest -L emergence` (behavioural) | **pass** (1/1, 670 s) |

---

## The measurement, before and after

Same seed, same scale, same script as the 1 Sep audit: seed 42, 500 NPCs,
6 provinces, 365 ticks, a player who tries to enter the business path.

| | before (build ee5589b) | after (`9de8dcf`) |
|---|---|---|
| player wealth | 50,000 → **40,000** | 50,000 → **27,010** (bought a going concern for ~31k, then earned) |
| player business revenue/tick | **0.0** | **148.2** |
| player business cost/tick | **0.0** | **76.0** |
| player business cash | 10,000 → **10,000** | **35,396**, moving every tick |
| facilities owned | — | **3**, with **95 of 95** worker stations filled |
| player age | 30.0 → **30.0** | 30.00 → **31.00** |
| max skill | — | 0.050 → **0.057** (exercised, and rusts when not) |
| calendar entries | **0** | **3, none of them scheduled by the player** |
| scene cards | **13, all empty, none answerable** | **7 created, 7 answered, 7 retired; 0 unanswerable, 0 unaddressable; peak queue 1** |
| save / reload | no save file existed | autosave + save/load, deterministic continuation |

The player's wealth *falls* on the headline number and that is the honest
reading: they spent capital buying an operating business, and the business now
earns. Takings run at 148.2/day against 76.0/day of costs against real
facilities running real recipes with real workers. Nothing was conjured: the
income traces to plants, workers, inputs and a market.

Reproduce the trace:
`./build/simulation/tests/integration/econlife_player_loop_tests "[.player-loop-observe]"`

---

## The ratchets, and what each one proves

All in `simulation/tests/integration/player_loop_test.cpp`. Every one is a
regression ratchet: it asserts a thing a player would notice.

Green (17):

- the player ends the year owning a business that trades
- wealth grows from operating, not from the opening balance
- business cash moves
- every card is addressable and answerable
- cards are resolved and retired, not accumulated
- the player is a year older after a year
- lifespan projection tracks the age it describes
- the world puts obligations on the player's calendar
- the calendar does not accumulate dead appointments
- the player's business responds to the choices they make
- a saved game reloads to the state it was saved in
- the save image is complete — save, load, save is byte-identical
- play continues deterministically after a reload
- a save keeps the previous one until the new one is committed
- a character starts with every domain and levels the ones they use
- something about the character other than money changed
- a career survives being put down and picked up again

Failing as designed (1): **the world's finances continue exactly after a
reload.** The player's own position, the identity and ownership of every firm,
the card queue and the calendar all continue exactly. A minority of NPC-side
balances do not — a worker paid in one run and not the other. Held as a visible
`[!shouldfail]` ratchet rather than deleted or weakened. Ruled out so far: the
save image itself, reference data, deposit era gates, the deferred queue's
ordering, module-private state in three modules, threading, and any difference
in shipped config or content.

---

## What the business path can now do

Reuse before building was the rule; almost all of this exposes machinery that
already existed and had no route to the player.

- **Acquire an operating business.** `real_estate`'s acquisition path — offer,
  owner accept/decline, due-diligence window, close, financing — reaches the
  player. The refusal to found a company from nothing stays: a firm with no
  premises and no book of business cannot earn, and inventing revenue for it
  would be the rail the doctrine forbids. The action now says which routes to an
  operating business actually exist instead of taking 10,000 in silence.
- **Run it.** The quarterly owner decision (invest / hold / cut) is the player's
  when they own the firm, using the same magnitudes `npc_business` already
  applies to NPC owners. Investing buys capacity and hires; hiring fills real
  worker stations on real plants.
- **Be asked for their time.** The world creates calendar entries; the calendar
  raises cards; unanswered timed cards fire their stated default and retire.
- **Change.** Age advances daily, health follows the province's cohort health,
  skills are exercised by the things the player actually does and rust when they
  are not, evidence reaches awareness through published stories.
- **Stop and come back.** Crash-safe atomic saves (tmp → fsync → .bak →
  rename), autosave, `--load` at startup, IPC `save`/`load`.

---

## Persistence

Schema **v33 → v37** across this branch, every step backward-compatible behind
a version gate. v37 adds `pending_scene_card_seeds`, because that queue crosses
tick boundaries and a save taken between the emit and the drain would otherwise
swallow whatever the world was about to say.

Five distinct save-lossiness defects were found and fixed by the round-trip
ratchets, not by inspection: missing reference data on load, an unserialised
`era_unlock`, a non-total comparator in the deferred-work queue (equal keys
ordered by heap accident), a missing `PlayerTravelPayload` reader/writer, and
unserialised module-private state in three modules.

---

## Architectural decisions worth knowing

- **Card copy is content.** `SceneCardCatalog` loads
  `packages/base_game/scene_cards/*.csv`; producers emit a `SceneCardSeedDelta`
  naming a template and `scene_cards` owns ids, lookup, injection, class rules
  and caps. A seed naming a template nobody wrote raises **no** card — a missing
  card is a visible content bug, an invented one is not. That failure is silent
  by nature, so a unit test asserts every key any producer names against the
  shipped CSV.
- **Monotonic card and calendar ids.** Cards retire now, so the old max-scan
  would re-issue a dead card's id and a stale `NegotiationContext.scene_card_id`
  would silently match an unrelated new card.
- **The seed queue is cross-tick, not same-tick.** Producers run on both sides
  of `scene_cards` in the tick order. Modelling it on the same-tick seed queues
  would have dropped every seed emitted after it.
- **A card always carries a choice.** A card the player cannot answer wedges the
  queue; `finalize_new_cards` drops choice-less cards on admission and the
  catalog refuses them at load.
- **No fake gameplay.** No arbitrary income, no scripted progression, no
  player-only economic shortcut. Where a mechanism did not exist, the action
  refuses and says so.

---

## Known defects and deferred work

Open, recorded in `docs/session_logs/flagged_issues.md`:

1. **Most facility firms never produce.** 8 of 47 firms with a facility earn;
   all 15 without one do. This reaches the player's own farm (148.2 → 5.0
   revenue in a later sitting). The player path works around it by buying a firm
   that does produce. This is the largest open item and it is a simulation
   defect, not a player-path one.
2. **World finances are not yet exactly deterministic after reload** (the
   `[!shouldfail]` ratchet above).
3. **Three producers still compose card copy inline** — `real_estate`'s counter
   and inbound-offer cards, `npc_business`'s quarterly owner decision. Each needs
   the card id at emit time to bind it to a `NegotiationContext` or a calendar
   entry; the seed channel allocates ids at drain time. Closing it needs an
   id-reservation step in the channel (a `card_id` on the seed, a shared claim
   helper that scans the seed queue as well as `new_scene_cards`, and
   `apply_deltas` advancing the allocator past claimed-but-unmaterialised ids).
4. **`process_quarterly_dividend` is unreachable for non-micro firms.**
5. **NPC business cash runaway** observed once at −8.4e13.
6. **News cards lost their emotional tone** in the move to the seed channel
   (nothing mechanical reads it; presentation only).
7. **The producer-key list in `scene_card_catalog_test` is maintained by hand.**
   A registry generated from the producers would close the silent-failure gap
   properly.

Deferred by scope (the goal's own scope call, MVP = business path only):
politician and criminal paths; the V1 target of ~200 card templates (18 shipped,
each with a live producer — padding the file with copy nothing can raise would
be content in name only); UI work beyond the CLI/IPC bridge.

---

## Content

18 authored templates in `packages/base_game/scene_cards/business_path.csv`
covering: founding refusals (wrong province, in transit, no capital, no
premises), acquisition outcomes (declined, accepted, closed, lapsed, lost), a
story that names the player, local unrest, and the six kinds of calendar
commitment plus a summons. Every one has a live producer. Card copy no longer
requires a recompile.
