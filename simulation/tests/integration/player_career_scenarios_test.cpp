// Career entry scenarios (Review R1 F-04).
//
// The player-loop ratchets buy a firm in one world, and that world was chosen
// because it had a firm the opening balance could reach. These scenarios test
// the path INTO a business independently of which world happens to offer one:
// each sets up its own precondition (an affordable firm, an unaffordable one,
// an offer the owner should refuse, no firm nearby) in several generated worlds
// and checks what the acquisition mechanism does with it.
//
// Setting the player's balance here is a test fixture that establishes the
// scenario's precondition. It is not a rebalance: nothing in the game's
// starting capital or firm valuations changes. How those actually compare
// across worlds is reported, not asserted, by "[.career_report]" below.

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <limits>
#include <vector>

#include "core/world_state/pending_transaction.h"
#include "player_loop_harness.h"

using namespace econlife;
using namespace econlife::player_loop;

namespace {

// Months of revenue per unit of offer multiple, and the fair multiple, as the
// acquisition mechanism prices them (PackageConfig defaults; the scenarios
// read the loaded config so they follow it if it changes).
const PackageConfig& package() {
    static const PackageConfig pkg = load_package_config(find_package_dir("config"));
    return pkg;
}

float price_at(const NPCBusiness& biz, float multiple) {
    return biz.revenue_per_tick *
           static_cast<float>(package().real_estate.acquisition_ticks_per_month) * multiple;
}

// The cheapest going concern in the player's province: earning, legitimate,
// not already theirs. nullptr when there is none.
const NPCBusiness* cheapest_local_firm(const WorldState& w) {
    const NPCBusiness* best = nullptr;
    for (const auto& biz : w.npc_businesses) {
        if (biz.owner_id == w.player->id || biz.province_id != w.player->current_province_id ||
            biz.revenue_per_tick <= 0.0f || biz.criminal_sector)
            continue;
        if (best == nullptr || biz.revenue_per_tick < best->revenue_per_tick)
            best = &biz;
    }
    return best;
}

std::size_t pending_offers_by_player(const WorldState& w) {
    std::size_t n = 0;
    for (const auto& a : w.pending_business_acquisitions)
        if (a.buyer_id == w.player->id && a.stage == PendingTxStage::pending)
            ++n;
    return n;
}

std::size_t businesses_owned(const WorldState& w) {
    std::size_t n = 0;
    for (const auto& biz : w.npc_businesses)
        if (biz.owner_id == w.player->id)
            ++n;
    return n;
}

void offer(WorldState& w, uint32_t business_id, float multiple) {
    AcquireBusinessAction a{};
    a.business_id = business_id;
    a.offer_multiple = multiple;
    a.payment_method = PaymentMethod::cash;
    a.down_payment_fraction = 1.0f;
    enqueue_player_action(w, PlayerActionType::acquire_business, a);
}

// The worlds every scenario runs in. Several, so no scenario rests on one.
constexpr uint64_t kWorlds[] = {3, 42, 47, 101};
constexpr uint32_t kNpcs = 200;
constexpr uint32_t kOfferTick = 7;

}  // namespace

TEST_CASE("career: an affordable firm and a willing owner become the player's",
          "[player_loop][career]") {
    // At 12x against a fair 6x the owner accepts with p > 0.99 at any trust, and
    // the player re-offers weekly after a refusal. An accepted deal then either
    // closes (the player owns the firm and paid for it) or is lost because the
    // firm closed or changed hands during due diligence, which the world is
    // entitled to do. Any other end of an accepted deal is a defect.
    const float kMultiple = 12.0f;
    const uint32_t diligence = package().real_estate.acquisition_due_diligence_ticks;
    int closed_total = 0, lost_total = 0;
    for (uint64_t seed : kWorlds) {
        INFO("world seed " << seed);
        float funded = 0.0f;
        int accepted = 0, closed = 0, lost = 0, unexplained = 0;
        float wealth_at_close_before = 0.0f;
        float price_paid = 0.0f;
        bool paid_ok = true;
        std::vector<PendingBusinessAcquisition> open;  // the player's deals last tick
        RunConfig cfg{};
        cfg.seed = seed;
        cfg.npc_count = kNpcs;
        cfg.ticks = kOfferTick + 2 * (diligence + 7) + 2;
        cfg.script = [&](WorldState& w, uint32_t tick) {
            if (!w.player)
                return;
            // How did last tick's open deals end?
            for (const auto& was : open) {
                bool still = false;
                for (const auto& a : w.pending_business_acquisitions)
                    still |= (a.id == was.id && a.stage == PendingTxStage::pending);
                if (still)
                    continue;
                const NPCBusiness* biz = nullptr;
                for (const auto& b : w.npc_businesses)
                    if (b.id == was.business_id)
                        biz = &b;
                if (biz && biz->owner_id == w.player->id) {
                    ++closed;
                    price_paid = was.price;
                    paid_ok = w.player->wealth <= wealth_at_close_before - was.price * 0.99f +
                                                      std::abs(wealth_at_close_before) * 0.05f;
                } else if (!biz || biz->owner_id != was.seller_id) {
                    ++lost;
                } else {
                    ++unexplained;
                }
            }
            open.clear();
            for (const auto& a : w.pending_business_acquisitions)
                if (a.buyer_id == w.player->id && a.stage == PendingTxStage::pending)
                    open.push_back(a);
            wealth_at_close_before = w.player->wealth;

            if (tick < kOfferTick || (tick - kOfferTick) % 7 != 0)
                return;
            if (businesses_owned(w) > 0 || !open.empty())
                return;
            const NPCBusiness* firm = cheapest_local_firm(w);
            if (firm == nullptr)
                return;
            if (w.player->wealth < price_at(*firm, kMultiple)) {
                // Precondition: the player can afford this firm in cash.
                funded = price_at(*firm, kMultiple) * 1.25f;
                w.player->wealth = funded;
            }
            offer(w, firm->id, kMultiple);
            ++accepted;  // counted as an offer here; acceptance shows as an open deal
        };
        const PlayerRun r = run(cfg);
        INFO("offers " << accepted << ", closed " << closed << ", lost " << lost << ", unexplained "
                       << unexplained << ", price " << price_paid);
        REQUIRE(funded > 0.0f);     // every world here has a local going concern
        CHECK(closed + lost >= 1);  // at least one offer was accepted and resolved
        CHECK(unexplained == 0);
        CHECK(paid_ok);
        if (closed > 0)
            CHECK(r.last().owned_businesses == 1);
        closed_total += closed;
        lost_total += lost;
    }
    std::printf(
        "[career] willing owner: %d deals closed, %d lost to the firm closing or "
        "changing hands, across %zu worlds\n",
        closed_total, lost_total, std::size(kWorlds));
    // The positive path is not one world's luck: it completes in several.
    CHECK(closed_total >= 2);
}

TEST_CASE("career: an offer the player cannot pay for creates nothing", "[player_loop][career]") {
    for (uint64_t seed : kWorlds) {
        INFO("world seed " << seed);
        bool offered = false;
        float wealth_before = 0.0f;
        std::size_t pending_after = 0;
        RunConfig cfg{};
        cfg.seed = seed;
        cfg.npc_count = kNpcs;
        cfg.ticks = kOfferTick + 3;
        cfg.script = [&](WorldState& w, uint32_t tick) {
            if (!w.player)
                return;
            if (offered && tick == kOfferTick + 1) {
                pending_after = pending_offers_by_player(w);
                return;
            }
            if (tick != kOfferTick)
                return;
            const NPCBusiness* firm = cheapest_local_firm(w);
            if (firm == nullptr)
                return;
            // Precondition: half the cash price.
            w.player->wealth = price_at(*firm, 7.0f) * 0.5f;
            wealth_before = w.player->wealth;
            offer(w, firm->id, 7.0f);
            offered = true;
        };
        const PlayerRun r = run(cfg);
        REQUIRE(offered);
        CHECK(pending_after == 0);
        CHECK(r.last().owned_businesses == 0);
        // No purchase price left the account (operating flows aside, the
        // player owns nothing that could pay or charge them).
        CHECK(r.series[kOfferTick + 1].wealth >= wealth_before * 0.99f);
    }
}

TEST_CASE("career: an owner refuses a lowball offer and the player is told",
          "[player_loop][career]") {
    // At 1x against a fair 6x the owner accepts with p <= 0.022 at any trust
    // (sigmoid((1/6 - 1 + 0.2 trust) x 6)). The player offers 1x on EVERY local
    // going concern at once, so each world rolls many independent owners. The
    // assertions are on the mechanism: every refusal leaves no pending deal and
    // no charge, refusals are announced, and acceptances are the rare case.
    std::size_t offers_total = 0, accepted_total = 0;
    for (uint64_t seed : kWorlds) {
        INFO("world seed " << seed);
        std::size_t offers = 0;
        std::size_t accepted = 0;
        float wealth_before = 0.0f;
        RunConfig cfg{};
        cfg.seed = seed;
        cfg.npc_count = kNpcs;
        cfg.ticks = kOfferTick + 3;     // a declined notice is materialised the tick after
        cfg.auto_answer_cards = false;  // keep the notices in the queue to count them
        cfg.script = [&](WorldState& w, uint32_t tick) {
            if (!w.player)
                return;
            if (tick == kOfferTick + 1) {
                accepted = pending_offers_by_player(w);
                return;
            }
            if (tick != kOfferTick)
                return;
            float total = 0.0f;
            for (const auto& biz : w.npc_businesses) {
                if (biz.province_id != w.player->current_province_id ||
                    biz.revenue_per_tick <= 0.0f || biz.criminal_sector ||
                    biz.owner_id == w.player->id)
                    continue;
                total += price_at(biz, 1.0f);
                offer(w, biz.id, 1.0f);
                ++offers;
            }
            // Precondition: every offer is affordable at once, so no offer
            // fails for want of cash.
            w.player->wealth = total * 1.1f + 1.0f;
            wealth_before = w.player->wealth;
        };
        const PlayerRun r = run(cfg);
        REQUIRE(offers >= 1);
        INFO("offers " << offers << ", accepted " << accepted);
        // A deal in due diligence has not yet charged the player.
        CHECK(r.series[kOfferTick + 1].wealth >= wealth_before * 0.99f);
        if (accepted < offers)
            CHECK(r.last().pending_cards >= 1);  // the "offer_declined" notice
        offers_total += offers;
        accepted_total += accepted;
    }
    INFO("offers " << offers_total << ", accepted " << accepted_total);
    REQUIRE(offers_total >= 8);
    // Refusal is the expected outcome for nearly every owner (p <= 0.022 each).
    CHECK(accepted_total * 4 < offers_total);
}

TEST_CASE("career: with no going concern nearby the player makes no offer",
          "[player_loop][career]") {
    // The scripted player's own choice, in a constructed world: the only firms
    // are in another province, idle, or criminal.
    WorldState w{};
    w.current_tick = 60;
    w.player = std::make_unique<PlayerCharacter>();
    w.player->id = 1;
    w.player->current_province_id = 0;
    w.player->wealth = 1.0e9f;
    auto add_firm = [&](uint32_t id, uint32_t province, float revenue, bool criminal) {
        NPCBusiness b{};
        b.id = id;
        b.owner_id = 900 + id;
        b.province_id = province;
        b.revenue_per_tick = revenue;
        b.criminal_sector = criminal;
        w.npc_businesses.push_back(b);
    };
    add_firm(10, 1, 500.0f, false);  // elsewhere
    add_firm(11, 0, 0.0f, false);    // here, but not trading
    add_firm(12, 0, 500.0f, true);   // here, but criminal
    buy_a_business_at_tick(60)(w, 60);
    CHECK(w.player_action_queue.empty());

    // Add a local going concern and the same player makes an offer.
    add_firm(13, 0, 100.0f, false);
    buy_a_business_at_tick(60)(w, 60);
    CHECK(w.player_action_queue.size() == 1);
}

// ---------------------------------------------------------------------------
// Observation, not a ratchet: how starting capital, firm prices and financing
// decide whether a new player can get into business at all. Prints one line per
// world. Run with:  ./econlife_player_loop_tests "[.career_report]"
// ---------------------------------------------------------------------------
TEST_CASE("career report: accessibility of a first business across worlds", "[.career_report]") {
    const auto& re = package().real_estate;
    const float fair = re.acquisition_fair_multiple;
    const double p_accept =
        1.0 / (1.0 + std::exp(-static_cast<double>((7.0f / fair - 1.0f) * re.sigmoid_steepness)));
    std::printf(
        "Columns per day: local going concerns / cheapest at 7x / cash buy / financed buy.\n"
        "Financed: %.0f%% down, loan <= %.0fx wealth. Owner acceptance at 7x, trust 0: %.2f.\n"
        "seed | start wealth | day 0: n / cheapest / cash / fin | day 60: n / cheapest / cash / "
        "fin\n",
        static_cast<double>(re.acquisition_min_down_payment * 100.0f),
        static_cast<double>(re.player_max_loan_multiplier_of_wealth), p_accept);
    struct Row {
        std::size_t n = 0;
        float cheapest = 0.0f;
        bool cash = false, fin = false;
    };
    auto observe = [&](const WorldState& w) {
        Row row;
        for (const auto& biz : w.npc_businesses)
            if (biz.province_id == w.player->current_province_id && biz.revenue_per_tick > 0.0f &&
                !biz.criminal_sector)
                ++row.n;
        const NPCBusiness* firm = cheapest_local_firm(w);
        if (firm == nullptr)
            return row;
        const float wealth = w.player->wealth;
        row.cheapest = price_at(*firm, 7.0f);
        row.cash = row.cheapest <= wealth;
        const float down = row.cheapest * re.acquisition_min_down_payment;
        row.fin = down <= wealth &&
                  row.cheapest - down <= wealth * re.player_max_loan_multiplier_of_wealth;
        return row;
    };
    int cash0 = 0, fin0 = 0, cash60 = 0, fin60 = 0, worlds = 0;
    for (uint64_t seed = 40; seed <= 59; ++seed) {
        Row day0, day60;
        float start_wealth = 0.0f;
        RunConfig cfg{};
        cfg.seed = seed;
        cfg.npc_count = 500;
        cfg.ticks = 61;
        cfg.script = [&](WorldState& w, uint32_t tick) {
            if (!w.player)
                return;
            if (tick == 0) {
                start_wealth = w.player->wealth;
                day0 = observe(w);
            } else if (tick == 60) {
                day60 = observe(w);
            }
        };
        run(cfg);
        std::printf("%4llu | %12.0f | %3zu / %8.0f / %3s / %3s | %3zu / %8.0f / %3s / %3s\n",
                    static_cast<unsigned long long>(seed), static_cast<double>(start_wealth),
                    day0.n, static_cast<double>(day0.cheapest), day0.cash ? "yes" : "no",
                    day0.fin ? "yes" : "no", day60.n, static_cast<double>(day60.cheapest),
                    day60.cash ? "yes" : "no", day60.fin ? "yes" : "no");
        cash0 += day0.cash;
        fin0 += day0.fin;
        cash60 += day60.cash;
        fin60 += day60.fin;
        ++worlds;
    }
    std::printf("worlds %d: day 0 cash %d, financed %d | day 60 cash %d, financed %d\n", worlds,
                cash0, fin0, cash60, fin60);
}
