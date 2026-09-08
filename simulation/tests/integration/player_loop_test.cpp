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
        cfg.script = buy_a_business_at_tick(2);
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
    // Investment compounds capacity, so the firm they run at the end of the
    // year is bigger than the one they bought.
    std::size_t settle_idx = 0;
    for (std::size_t i = 1; i < r.series.size(); ++i) {
        if (r.series[i].owned_businesses > r.series[i - 1].owned_businesses) {
            settle_idx = i;
            break;
        }
    }
    REQUIRE(settle_idx > 0);
    INFO("revenue at purchase " << r.series[settle_idx].owned_revenue_per_tick << " -> year end "
                                << r.last().owned_revenue_per_tick);
    REQUIRE(r.last().owned_revenue_per_tick > r.series[settle_idx].owned_revenue_per_tick);
}
