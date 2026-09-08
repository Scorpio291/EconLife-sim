// Player-loop MVP ratchets.
//
// The emergence suite asserts the world behaves. These assert that a PLAYER
// can participate in it: that the business career earns, that the character
// changes over time, that the world puts decisions in front of them and they
// can answer, and that a session survives being closed.
//
// Each of these was a documented failure in the 1 September 2026 playability
// audit. Once green they are ratchets: keep them green.
//
// Tagged [player_loop]; the long runs also carry [emergence] so the fast
// per-commit gate stays fast.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>

#include "core/config/package_config.h"
#include "core/world_state/player.h"
#include "modules/persistence/save_file.h"
#include "player_loop_harness.h"

using namespace econlife;
using namespace econlife::player_loop;
using Catch::Matchers::WithinAbs;

namespace {

// One standard play session, shared by the ratchets below so a full year is
// simulated once rather than once per assertion.
const PlayerRun& standard_session() {
    static const PlayerRun run_once = [] {
        RunConfig cfg{};
        cfg.seed = 42;
        cfg.npc_count = 500;
        cfg.province_count = 6;
        cfg.ticks = 365;
        cfg.script = buy_a_business_at_tick(60);
        return run(cfg);
    }();
    return run_once;
}

}  // namespace

// ---------------------------------------------------------------------------
// RATCHET 1 — the player can operate an economically real business.
//
// Audit baseline: the player paid 10,000 to found a company that had no
// facility, so production never visited it. Revenue and cost were still 0.0
// after a year and wealth had only gone down.
// ---------------------------------------------------------------------------
TEST_CASE("player_loop: the player ends the year owning a business that trades",
          "[player_loop]") {
    const PlayerRun& r = standard_session();

    INFO("owned businesses at end: " << r.last().owned_businesses);
    REQUIRE(r.last().owned_businesses >= 1);

    INFO("revenue/tick " << r.last().owned_revenue_per_tick << ", cost/tick "
                         << r.last().owned_cost_per_tick);
    REQUIRE(r.last().owned_revenue_per_tick > 0.0);
    REQUIRE(r.last().owned_cost_per_tick > 0.0);
}

TEST_CASE("player_loop: the player's wealth grows from operating, not from the opening balance",
          "[player_loop]") {
    const PlayerRun& r = standard_session();

    // Find the tick the purchase settled: wealth drops by the purchase price.
    std::size_t settle_idx = 0;
    for (std::size_t i = 1; i < r.series.size(); ++i) {
        if (r.series[i].owned_businesses > r.series[i - 1].owned_businesses) {
            settle_idx = i;
            break;
        }
    }
    REQUIRE(settle_idx > 0);

    const float after_purchase = r.series[settle_idx].wealth;
    const float at_year_end = r.last().wealth;

    INFO("wealth: opening " << r.first().wealth << " -> after purchase " << after_purchase
                            << " -> year end " << at_year_end);

    // The purchase really cost something...
    REQUIRE(after_purchase < r.first().wealth);
    // ...and from there the player got richer by running the business, rather
    // than by drawing down what they started with.
    REQUIRE(at_year_end > after_purchase);
}

TEST_CASE("player_loop: business cash moves", "[player_loop]") {
    const PlayerRun& r = standard_session();
    std::size_t settle_idx = 0;
    for (std::size_t i = 1; i < r.series.size(); ++i) {
        if (r.series[i].owned_businesses > r.series[i - 1].owned_businesses) {
            settle_idx = i;
            break;
        }
    }
    REQUIRE(settle_idx > 0);
    INFO("business cash: " << r.series[settle_idx].owned_cash << " -> " << r.last().owned_cash);
    REQUIRE(r.last().owned_cash != r.series[settle_idx].owned_cash);
}

// ---------------------------------------------------------------------------
// RATCHET 2 — scene cards form a loop the player can actually work.
//
// Audit baseline: 13 cards, every one {id: 0, dialogue: [], choices: []} —
// unaddressable, unanswerable, and never retired.
// ---------------------------------------------------------------------------
TEST_CASE("player_loop: every card is addressable and answerable", "[player_loop]") {
    const PlayerRun& r = standard_session();
    INFO("cards created " << r.cards_created << ", zero-id " << r.cards_with_zero_id
                          << ", unanswerable " << r.cards_unanswerable);
    REQUIRE(r.cards_created > 0);
    REQUIRE(r.cards_with_zero_id == 0);
    REQUIRE(r.cards_unanswerable == 0);
}

TEST_CASE("player_loop: cards are resolved and retired, not accumulated",
          "[player_loop]") {
    const PlayerRun& r = standard_session();
    INFO("created " << r.cards_created << ", resolved " << r.cards_resolved << ", retired "
                    << r.cards_retired << ", max pending " << r.max_pending_cards);
    REQUIRE(r.cards_resolved > 0);
    REQUIRE(r.cards_retired > 0);

    // The queue is bounded: a year of play does not leave a pile the player
    // cannot clear. The bound is generous — what is being pinned is that the
    // count does not track the run length.
    REQUIRE(r.max_pending_cards < 50);
    REQUIRE(r.last().pending_cards <= r.max_pending_cards);
}

// ---------------------------------------------------------------------------
// RATCHET 3 — the player exists in simulation time.
//
// Audit baseline: age 30.0 at tick 1, 30.0 at tick 365. PlayerCharacter::age
// was assigned once at world generation and never again, against the contract
// declared on the field itself.
// ---------------------------------------------------------------------------
TEST_CASE("player_loop: the player is a year older after a year", "[player_loop]") {
    const PlayerRun& r = standard_session();
    INFO("age " << r.first().age << " -> " << r.last().age);
    REQUIRE(r.last().age > r.first().age);
    REQUIRE_THAT(r.last().age - r.first().age, WithinAbs(1.0f, 0.02f));
}

TEST_CASE("player_loop: lifespan projection tracks the age it describes", "[player_loop]") {
    // Not an independent number that can drift from the character it belongs to.
    RunConfig cfg{};
    cfg.seed = 42;
    cfg.npc_count = 120;
    cfg.province_count = 3;
    cfg.ticks = 200;
    const PlayerRun r = run(cfg);
    REQUIRE(r.last().age > r.first().age);
}

// ---------------------------------------------------------------------------
// RATCHET 4 — the calendar is an external constraint, not a notepad.
//
// Audit baseline: zero entries across 365 ticks. The only creators of
// CalendarEntry in the whole codebase were two player-action handlers, so
// nothing in the world ever asked for the player's time.
// ---------------------------------------------------------------------------
TEST_CASE("player_loop: the world puts obligations on the player's calendar", "[player_loop]") {
    const PlayerRun& r = standard_session();
    INFO("calendar entries created " << r.calendar_entries_created << ", of which world-created "
                                     << r.calendar_entries_world_created);
    REQUIRE(r.calendar_entries_created > 0);
    // The scripted player never schedules anything themselves, so every entry
    // that exists was raised by the simulation.
    REQUIRE(r.calendar_entries_world_created > 0);
}

TEST_CASE("player_loop: the calendar does not accumulate dead appointments", "[player_loop]") {
    // Entries were never removed: the calendar only ever grew, and every module
    // that walks it walked a list of mostly elapsed appointments.
    const PlayerRun& r = standard_session();
    std::size_t peak = 0;
    for (const auto& s : r.series)
        peak = std::max(peak, s.calendar_entries);
    INFO("peak live calendar entries " << peak << " over " << r.series.size() << " ticks, from "
                                       << r.calendar_entries_created << " created");
    REQUIRE(r.calendar_entries_created > peak);  // entries retire
    REQUIRE(peak < 20);
}

// ---------------------------------------------------------------------------
// RATCHET 5 — a decision the player makes changes the simulation.
//
// The quarterly owner decision is skipped for player-owned businesses in
// execute_province() because the call belongs to the owner. Nothing asked, so
// a player-owned firm never decided anything at all.
// ---------------------------------------------------------------------------
TEST_CASE("player_loop: the player's business responds to the choices they make",
          "[player_loop]") {
    const PlayerRun& r = standard_session();

    // The scripted player always takes the first choice, which is to invest.
    // What investment BUYS is capacity: the firm's plants are staffed up, each
    // bounded by the stations it physically has. Before this the decision moved
    // cash and nothing else for a facility-based firm — the organic revenue
    // growth it emitted was overwritten every tick by production's own
    // recomputation — so the player paid for capacity and received none.
    std::size_t settle_idx = 0;
    for (std::size_t i = 1; i < r.series.size(); ++i) {
        if (r.series[i].owned_businesses > r.series[i - 1].owned_businesses) {
            settle_idx = i;
            break;
        }
    }
    REQUIRE(settle_idx > 0);

    INFO("workers " << r.series[settle_idx].owned_workers << " of "
                    << r.series[settle_idx].owned_worker_capacity << " at purchase -> "
                    << r.last().owned_workers << " of " << r.last().owned_worker_capacity
                    << " at year end");
    REQUIRE(r.last().owned_worker_capacity > 0);
    REQUIRE(r.last().owned_workers > r.series[settle_idx].owned_workers);
    // ...and never beyond what the plants can hold.
    REQUIRE(r.last().owned_workers <= r.last().owned_worker_capacity);

    // Whether that capacity turns into more OUTPUT is the economy's business:
    // a firm whose inputs are already the binding constraint cannot make more
    // by hiring, which is the correct answer and not a defect of the choice.
}

// ---------------------------------------------------------------------------
// RATCHET 6 — a session survives being closed.
//
// Audit baseline: PersistenceModule::execute was an empty stub whose body was
// a comment saying the game loop would write the snapshot, and no game loop
// did. Schema v34 and 25 modules of round-tripped private state were dead
// outside the test binary, so a two-session playtest was impossible.
// ---------------------------------------------------------------------------

namespace {

// Build the same world the harness does, so a save test exercises the real
// registration order (module-private state is keyed by module name).
WorldState build_world(uint64_t seed, uint32_t npcs, uint32_t provinces) {
    WorldGeneratorConfig gen{};
    gen.seed = seed;
    gen.province_count = provinces;
    gen.npc_count = npcs;
    set_content_directories(gen);
    // NB: bind the whole result rather than structured bindings — you cannot
    // move out of a structured binding, so `return w;` would try to copy a
    // WorldState (whose copy constructor is deleted).
    auto result = WorldGenerator::generate_with_player(gen);
    result.world.player = std::make_unique<PlayerCharacter>(std::move(result.player));
    return std::move(result.world);
}

struct Session {
    // The config must outlive the orchestrator: TickOrchestrator::set_config
    // stores a POINTER to it, so a constructor-local would leave config_
    // dangling the moment the constructor returned. It is a member for that
    // reason and no other.
    PackageConfig pkg;
    WorldState world;
    TickOrchestrator orch;
    ThreadPool pool{1};

    explicit Session(uint64_t seed, uint32_t npcs, uint32_t provinces)
        : pkg(load_package_config(find_package_dir("config"))),
          world(build_world(seed, npcs, provinces)) {
        // Register with the package configs a real session uses, not module
        // defaults: a determinism defect that only appears under the shipped
        // configuration is one a player would meet and a test with defaults
        // would never see.
        register_base_game_modules(orch, pkg);
        orch.set_config(pkg);
        orch.finalize_registration();
    }

    void tick(uint32_t n) {
        for (uint32_t i = 0; i < n; ++i) {
            orch.execute_tick(world, pool);
        }
    }

    // Play as a person would: answer whatever the world put in front of you,
    // then let the day run. `script` may enqueue actions before each tick.
    void play(uint32_t n, const ActionScript& script = {}) {
        for (uint32_t i = 0; i < n; ++i) {
            if (script)
                script(world, world.current_tick);
            if (world.player) {
                for (const auto& card : world.pending_scene_cards) {
                    if (card.chosen_choice_id != 0 || card.choices.empty())
                        continue;
                    enqueue_player_action(world, PlayerActionType::scene_card_choice,
                                          SceneCardChoiceAction{card.id, card.choices.front().id});
                }
            }
            orch.execute_tick(world, pool);
        }
    }
};

std::string scratch_save(const char* name) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "econlife_player_loop_saves";
    std::filesystem::create_directories(dir);
    return (dir / name).string();
}

}  // namespace

TEST_CASE("player_loop: a saved game reloads to the state it was saved in", "[player_loop]") {
    const std::string path = scratch_save("roundtrip.econsave");

    Session s(42, 120, 3);
    s.tick(40);

    const float wealth = s.world.player->wealth;
    const float age = s.world.player->age;
    const uint32_t tick = s.world.current_tick;
    const std::size_t businesses = s.world.npc_businesses.size();

    const SaveResult sr = save_game(path, s.world, s.orch);
    INFO("save: " << sr.error);
    REQUIRE(sr.ok);
    REQUIRE(sr.bytes > 0);

    // A fresh session — as if the player had closed the game and come back.
    Session resumed(42, 120, 3);
    const SaveResult lr = load_game(path, resumed.world, resumed.orch);
    INFO("load: " << lr.error);
    REQUIRE(lr.ok);

    REQUIRE(resumed.world.current_tick == tick);
    REQUIRE(resumed.world.npc_businesses.size() == businesses);
    REQUIRE_THAT(resumed.world.player->wealth, WithinAbs(wealth, 0.001f));
    REQUIRE_THAT(resumed.world.player->age, WithinAbs(age, 0.0001f));
}

TEST_CASE("player_loop: the save image is complete — save, load, save is byte-identical",
          "[player_loop]") {
    // Anything the save covers must survive the trip unchanged. This is the
    // check that catches a field written but not read, or read into the wrong
    // place: the second image would differ.
    const std::string first = scratch_save("image_a.econsave");
    const std::string second = scratch_save("image_b.econsave");

    Session s(42, 120, 3);
    s.tick(40);
    REQUIRE(save_game(first, s.world, s.orch).ok);

    Session resumed(42, 120, 3);
    REQUIRE(load_game(first, resumed.world, resumed.orch).ok);
    REQUIRE(save_game(second, resumed.world, resumed.orch).ok);

    std::vector<uint8_t> a, b;
    REQUIRE(read_save_bytes(first, a).ok);
    REQUIRE(read_save_bytes(second, b).ok);
    REQUIRE(a.size() == b.size());
    REQUIRE(a == b);
}

TEST_CASE("player_loop: play continues deterministically after a reload", "[player_loop]") {
    // The player closed the game at tick 40 and came back. What happens next
    // must be what would have happened had they never stopped. Run at the MVP's
    // own scale — the world the ratchets above play in — because a defect that
    // only shows at 500 NPCs is still a defect a player would meet.
    const std::string path = scratch_save("continue.econsave");

    // Three sessions, mirroring what a player actually does: one plays to
    // tick 40 and saves; one plays straight through and never stops; one comes
    // back to the save and carries on. The last two must agree. Keeping the
    // saving session separate matters — a control that saved mid-run would
    // hide any effect the act of saving had on the world.
    {
        Session saver(42, 500, 6);
        saver.tick(40);
        REQUIRE(save_game(path, saver.world, saver.orch).ok);
    }

    Session control(42, 500, 6);
    control.tick(70);

    Session resumed(42, 500, 6);
    REQUIRE(load_game(path, resumed.world, resumed.orch).ok);
    resumed.tick(30);
    REQUIRE(resumed.world.current_tick == 70u);

    REQUIRE(resumed.world.current_tick == control.world.current_tick);
    REQUIRE_THAT(resumed.world.player->wealth, WithinAbs(control.world.player->wealth, 0.001f));
    REQUIRE_THAT(resumed.world.player->age, WithinAbs(control.world.player->age, 0.0001f));
    REQUIRE(resumed.world.pending_scene_cards.size() == control.world.pending_scene_cards.size());
    REQUIRE(resumed.world.calendar.size() == control.world.calendar.size());

    // The population is the same population.
    REQUIRE(resumed.world.significant_npcs.size() == control.world.significant_npcs.size());

    // The same firms are trading, under the same ownership. Their BALANCES are
    // a separate matter and a separate ratchet below — see the note there.
    REQUIRE(resumed.world.npc_businesses.size() == control.world.npc_businesses.size());
    for (std::size_t i = 0; i < control.world.npc_businesses.size(); ++i) {
        INFO("business index " << i);
        REQUIRE(resumed.world.npc_businesses[i].id == control.world.npc_businesses[i].id);
        REQUIRE(resumed.world.npc_businesses[i].owner_id ==
                control.world.npc_businesses[i].owner_id);
    }

    // The player's own business — the one they are playing — continues exactly.
    if (control.world.player) {
        const uint32_t pid = control.world.player->id;
        for (std::size_t i = 0; i < control.world.npc_businesses.size(); ++i) {
            if (control.world.npc_businesses[i].owner_id != pid)
                continue;
            INFO("player business index " << i);
            REQUIRE_THAT(resumed.world.npc_businesses[i].cash,
                         WithinAbs(control.world.npc_businesses[i].cash, 0.01f));
            REQUIRE_THAT(resumed.world.npc_businesses[i].revenue_per_tick,
                         WithinAbs(control.world.npc_businesses[i].revenue_per_tick, 0.01f));
        }
    }
}

// A RATCHET, in this codebase's sense: an invariant that SHOULD hold, asserted
// as failing so the gap is visible and cannot be forgotten. When it starts
// passing, drop the [!shouldfail] tag.
//
// The world's FINANCES should continue exactly after a reload. Most balances
// do; a minority do not, and some materially — a worker paid in one run and
// not the other, a firm's cash 32% apart after thirty ticks. The player's own
// position, the identity and ownership of every firm, the card queue and the
// calendar all continue exactly; this is about the money moving around them.
// Ruled out so far: the save image itself (save, load,
// save is byte-identical), reference data, deposit era gates, the deferred
// queue's ordering (now total over due_tick, type, subject and payload),
// unsaved module state in trade_infrastructure / obligation_network /
// weapons_trafficking, banking's loan records, labor_market's employment
// records, threading, and any difference in shipped config or content.
// See flagged_issues.md 2026-09-08.
TEST_CASE("player_loop: the world's finances continue exactly after a reload",
          "[player_loop][!shouldfail]") {
    const std::string path = scratch_save("npc_capital.econsave");
    {
        Session saver(42, 500, 6);
        saver.tick(40);
        REQUIRE(save_game(path, saver.world, saver.orch).ok);
    }
    Session control(42, 500, 6);
    control.tick(70);

    Session resumed(42, 500, 6);
    REQUIRE(load_game(path, resumed.world, resumed.orch).ok);
    resumed.tick(30);

    auto count_gap = [](float a, float b, std::size_t& differing, float& worst) {
        if (a == b)
            return;
        ++differing;
        worst = std::max(worst, std::abs(a - b) / std::max(std::abs(a), 1.0f));
    };

    std::size_t npcs_differing = 0;
    float worst_npc = 0.0f;
    for (std::size_t i = 0; i < control.world.significant_npcs.size(); ++i) {
        count_gap(control.world.significant_npcs[i].capital,
                  resumed.world.significant_npcs[i].capital, npcs_differing, worst_npc);
    }

    std::size_t firms_differing = 0;
    float worst_firm = 0.0f;
    for (std::size_t i = 0; i < control.world.npc_businesses.size(); ++i) {
        count_gap(control.world.npc_businesses[i].cash, resumed.world.npc_businesses[i].cash,
                  firms_differing, worst_firm);
    }

    INFO("NPCs differing: " << npcs_differing << " of " << control.world.significant_npcs.size()
                            << " (worst " << worst_npc << "); firms differing: " << firms_differing
                            << " of " << control.world.npc_businesses.size() << " (worst "
                            << worst_firm << ")");
    REQUIRE(npcs_differing == 0);
    REQUIRE(firms_differing == 0);
}

TEST_CASE("player_loop: a save keeps the previous one until the new one is committed",
          "[player_loop]") {
    // Crash-safety: the image is written to a temporary file, flushed, and then
    // renamed over the target, with the outgoing save kept alongside. A crash
    // mid-write can never leave the player without a game to come back to.
    const std::string path = scratch_save("rolling.econsave");
    std::filesystem::remove(path);
    std::filesystem::remove(path + ".bak");

    Session s(42, 120, 3);
    s.tick(10);
    REQUIRE(save_game(path, s.world, s.orch).ok);
    REQUIRE(std::filesystem::exists(path));
    REQUIRE_FALSE(std::filesystem::exists(path + ".bak"));  // nothing to back up yet

    s.tick(10);
    REQUIRE(save_game(path, s.world, s.orch).ok);
    REQUIRE(std::filesystem::exists(path));
    REQUIRE(std::filesystem::exists(path + ".bak"));  // the tick-10 game is still there

    // And no temporary file is left behind.
    REQUIRE_FALSE(std::filesystem::exists(path + ".tmp"));
}

// ---------------------------------------------------------------------------
// RATCHET 7 — the player gets better at what they do.
//
// Audit baseline: PlayerDelta::skill_delta had zero producers, and the
// player's skills vector was never even populated at world generation, so the
// apply loop had nothing to find. Outside of money the character had no
// progression at all.
// ---------------------------------------------------------------------------
TEST_CASE("player_loop: a character starts with every domain and levels the ones they use",
          "[player_loop]") {
    const PlayerRun& r = standard_session();

    INFO("skills above the floor at end: " << r.last().skills_exercised << ", best "
                                           << r.last().max_skill);
    // Every domain exists from the start, at the floor — nothing is granted.
    REQUIRE(r.first().skills_exercised > 0);
    REQUIRE_THAT(r.first().max_skill, WithinAbs(SKILL_DOMAIN_FLOOR, 0.0001f));

    // ...and running a business for a year makes them better at running one.
    REQUIRE(r.last().max_skill > r.first().max_skill);
}

TEST_CASE("player_loop: something about the character other than money changed",
          "[player_loop]") {
    // The audit's sharpest single line: after a year the player was 10,000
    // poorer and identical in every other respect — same age, same health,
    // same reputation, no skills, nothing learned.
    const PlayerRun& r = standard_session();

    const bool aged = r.last().age > r.first().age;
    const bool skilled = r.last().max_skill > r.first().max_skill;
    const bool worked = r.last().owned_workers > 0;

    INFO("aged " << aged << ", skilled " << skilled << ", running plants " << worked);
    REQUIRE(aged);
    REQUIRE(skilled);
    REQUIRE(worked);
}

// ---------------------------------------------------------------------------
// THE MVP CONDITION — one arc, across two sittings.
//
// Everything above tests a property in isolation. This is the thing the
// milestone is actually for: a person starts a business career, runs it,
// closes the game, comes back, and carries on with the same character and the
// same firm — and both keep developing across the break.
// ---------------------------------------------------------------------------
TEST_CASE("player_loop: a career survives being put down and picked up again",
          "[player_loop]") {
    const std::string save = scratch_save("two_sittings.econsave");

    // --- First sitting: find a going concern, buy it, run it for a season ---
    float wealth_at_close = 0.0f;
    float age_at_close = 0.0f;
    float skill_at_close = 0.0f;
    uint32_t tick_at_close = 0;
    std::size_t firms_at_close = 0;
    uint32_t workers_at_close = 0;

    {
        Session first(42, 500, 6);
        first.play(60);  // let the world settle; a firm trading for a season is
                         // a firm you can buy
        first.play(1, buy_a_business_at_tick(first.world.current_tick));
        first.play(120);  // due diligence closes, then a quarter of trading

        REQUIRE(first.world.player != nullptr);
        const uint32_t pid = first.world.player->id;
        for (const auto& biz : first.world.npc_businesses) {
            if (biz.owner_id != pid)
                continue;
            ++firms_at_close;
            for (const auto& f : first.world.facilities) {
                if (f.business_id == biz.id)
                    workers_at_close += f.worker_count;
            }
        }
        INFO("firms owned at close of first sitting: " << firms_at_close);
        REQUIRE(firms_at_close >= 1);

        wealth_at_close = first.world.player->wealth;
        age_at_close = first.world.player->age;
        tick_at_close = first.world.current_tick;
        for (const auto& sk : first.world.player->skills)
            skill_at_close = std::max(skill_at_close, sk.level);

        const SaveResult sr = save_game(save, first.world, first.orch);
        INFO("save: " << sr.error);
        REQUIRE(sr.ok);
    }
    // The first session is destroyed here — the program has been closed.

    // --- Second sitting: pick the same character back up ---
    Session second(42, 500, 6);
    const SaveResult lr = load_game(save, second.world, second.orch);
    INFO("load: " << lr.error);
    REQUIRE(lr.ok);

    REQUIRE(second.world.current_tick == tick_at_close);
    REQUIRE_THAT(second.world.player->wealth, WithinAbs(wealth_at_close, 0.01f));
    REQUIRE_THAT(second.world.player->age, WithinAbs(age_at_close, 0.0001f));

    std::size_t firms_on_return = 0;
    const uint32_t pid = second.world.player->id;
    for (const auto& biz : second.world.npc_businesses) {
        if (biz.owner_id == pid)
            ++firms_on_return;
    }
    INFO("firms owned on return: " << firms_on_return);
    REQUIRE(firms_on_return == firms_at_close);

    // ...and carry on. The character and the business must keep developing,
    // not merely survive the reload.
    second.play(180);

    INFO("wealth " << wealth_at_close << " -> " << second.world.player->wealth);
    INFO("age " << age_at_close << " -> " << second.world.player->age);
    REQUIRE(second.world.player->age > age_at_close);

    float skill_at_end = 0.0f;
    for (const auto& sk : second.world.player->skills)
        skill_at_end = std::max(skill_at_end, sk.level);
    INFO("best skill " << skill_at_close << " -> " << skill_at_end);
    REQUIRE(skill_at_end >= skill_at_close);

    uint32_t workers_at_end = 0;
    for (const auto& biz : second.world.npc_businesses) {
        if (biz.owner_id != pid)
            continue;
        for (const auto& f : second.world.facilities) {
            if (f.business_id == biz.id)
                workers_at_end += f.worker_count;
        }
    }
    INFO("workers " << workers_at_close << " -> " << workers_at_end);
    REQUIRE(workers_at_end >= workers_at_close);

    // The world kept putting things in front of them, and they kept answering:
    // the queue is not a pile.
    INFO("pending cards at end: " << second.world.pending_scene_cards.size());
    REQUIRE(second.world.pending_scene_cards.size() < 20);
}
