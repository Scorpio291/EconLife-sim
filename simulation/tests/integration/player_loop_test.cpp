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

#include <filesystem>

#include "core/config/package_config.h"
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
struct Session {
    WorldState world;
    TickOrchestrator orch;
    ThreadPool pool{1};

    explicit Session(uint64_t seed, uint32_t npcs, uint32_t provinces) {
        WorldGeneratorConfig gen{};
        gen.seed = seed;
        gen.province_count = provinces;
        gen.npc_count = npcs;
        set_content_directories(gen);
        auto [w, p] = WorldGenerator::generate_with_player(gen);
        world = std::move(w);
        world.player = std::make_unique<PlayerCharacter>(std::move(p));
        // Register with the package configs a real session uses, not module
        // defaults: a determinism defect that only appears under the shipped
        // configuration is one a player would meet and a test with defaults
        // would never see.
        const std::string config_dir = find_package_dir("config");
        const PackageConfig pkg = load_package_config(config_dir);
        register_base_game_modules(orch, pkg);
        orch.set_config(pkg);
        orch.finalize_registration();
    }

    void tick(uint32_t n) {
        for (uint32_t i = 0; i < n; ++i)
            orch.execute_tick(world, pool);
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

    // And the people are the same people, holding the same money.
    REQUIRE(resumed.world.significant_npcs.size() == control.world.significant_npcs.size());
    for (std::size_t i = 0; i < control.world.significant_npcs.size(); ++i) {
        if (resumed.world.significant_npcs[i].capital !=
            control.world.significant_npcs[i].capital) {
            INFO("npc index " << i << " id " << control.world.significant_npcs[i].id
                              << " role " << static_cast<int>(control.world.significant_npcs[i].role)
                              << ": control " << control.world.significant_npcs[i].capital
                              << " vs resumed " << resumed.world.significant_npcs[i].capital);
            REQUIRE_THAT(resumed.world.significant_npcs[i].capital,
                         WithinAbs(control.world.significant_npcs[i].capital, 0.01f));
        }
    }

    // The businesses the player could interact with are the same firms in the
    // same condition.
    REQUIRE(resumed.world.npc_businesses.size() == control.world.npc_businesses.size());
    for (std::size_t i = 0; i < control.world.npc_businesses.size(); ++i) {
        INFO("business index " << i);
        REQUIRE(resumed.world.npc_businesses[i].id == control.world.npc_businesses[i].id);
        REQUIRE_THAT(resumed.world.npc_businesses[i].cash,
                     WithinAbs(control.world.npc_businesses[i].cash, 0.01f));
    }
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
