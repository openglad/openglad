/* Real-layout safety proofs for the modes campaign's two water fields.
 *
 * MUDBOWL (821) and THE CAUSEWAY (829) both put standable ground on
 * opposite sides of open water. The hunt AI does not pathfind, so their
 * automatic squads exclude teleporting families and their unattended runs
 * must not accumulate a near-stationary unproductive window at a shoreline
 * (including an empty command queue, not only a stuck far command).
 *
 * Copyright (C) 1995-2002 FSGames. Ported by Sean Ford and Yan Shosh.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/terrain_types.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include "../modes_pack_fixture.h"
#include "../test_save_state_guard.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace og::modes_test;

namespace {

constexpr int kMudbowl = 821;
constexpr int kCauseway = 829;
constexpr int kModeIdSlot = 0;
constexpr int kSoccerModeId = 4;
constexpr int kBasketballModeId = 6;
constexpr int kScoreLimitSlot = 8;
constexpr int kTimeLimitSlot = 12;
constexpr int kSquadSeedSlot = 6;
constexpr int kSoccerBallEntitySlot = 14;
constexpr int kSoccerBallPxSlot = 15;
constexpr int kSoccerBallPySlot = 16;
constexpr int kSoccerBallVxSlot = 17;
constexpr int kSoccerBallVySlot = 18;
constexpr int kSoccerStallSinceSlot = 43;
constexpr int kBasketballBallEntitySlot = 14;
constexpr int kBasketballBallPxSlot = 16;
constexpr int kBasketballBallPySlot = 17;
constexpr int kBasketballBallPzSlot = 18;
constexpr int kBasketballBallVxSlot = 19;
constexpr int kBasketballBallVySlot = 20;
constexpr int kBasketballBallVzSlot = 21;
constexpr int kBasketballBallStateSlot = 22;
constexpr int kBasketballCarrierSlot = 23;
constexpr int kBasketballLastToucherSlot = 28;
constexpr int kBasketballJumpUntilSlot = 38;
constexpr int kBasketballStallSinceSlot = 40;
constexpr int kBasketballFree = 0;
constexpr int kBasketballRebound = 4;
constexpr int kDeadBallTicks = 600;
constexpr int kSquadSeedMageFirst = 4; // code 3 picks source member four
constexpr int kWindowTicks = 300;
constexpr int kWindowCount = 6;
constexpr int kFarDistance = 32;
constexpr int kContactDistance = 24;
constexpr int kStationaryAxisSpan = 8;
constexpr int kStationaryNet = 8;
constexpr int kUnproductiveTicks = 240;

struct LoadedModesWaterLevel
{
    LevelRuntimeData level;
    SaveData save;
    og::sim::SimEventLog events;
    FixedRandom rng;
    GameContext gc;
    ScopedGameplayContext gameplay;
    bool loaded = false;

    explicit LoadedModesWaterLevel(int id, std::uint32_t rng_value = 0)
        : level(id, true, &modes_test_level_hooks())
        , rng(rng_value)
        , gameplay(level, save, events, cfg)
    {
        level.set_sim_context(&save, &level.world().enemy_freeze, &events,
                              &rng, &cfg);
        gc.rng = &rng;
        push_test_context(&gc);
        loaded = level.load();
    }

    ~LoadedModesWaterLevel() { pop_test_context(); }

    GameWorld& world() { return level.world(); }
};

class ModesWaterRealCampaign : public ::testing::Test
{
protected:
    void SetUp() override
    {
        restore_default_campaigns();
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error("modes"));
    }

private:
    og::test::ScopedCampaignMountState mount_restore_;
};

std::vector<int> sorted_water_squad()
{
    std::vector<int> out = {FAMILY_SOLDIER, FAMILY_ARCHER, FAMILY_ELF,
                            FAMILY_ORC, FAMILY_THIEF};
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<int> team_bot_families(const GameWorld& world, int team)
{
    std::vector<int> out;
    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr || w->dead() || w->query_order() != Order::Living ||
            w->team_num() != static_cast<unsigned char>(team) ||
            w->myguy != nullptr)
        {
            continue;
        }
        out.push_back(static_cast<int>(w->family()));
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::uint32_t> automatic_living_ids(const GameWorld& world)
{
    std::vector<std::uint32_t> out;
    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr || w->dead() || w->query_order() != Order::Living ||
            w->team_num() >= SCORE_TEAM_COUNT || w->myguy != nullptr)
        {
            continue;
        }
        out.push_back(w->entity_id());
    }
    std::sort(out.begin(), out.end());
    return out;
}

void request_two_automatic_squads(GameWorld& world)
{
    for (auto& fill : world.ctf_requested_fill)
        fill = og::sim::kFillFair;
    world.mode.vars[static_cast<std::size_t>(kSquadSeedSlot)] =
        kSquadSeedMageFirst;
}

walker* add_roster_hero(GameWorld& world, int family, int team, int x, int y)
{
    walker* hero = world.add_ob(Order::Living, family);
    if (hero == nullptr)
        return nullptr;
    hero->setxy(static_cast<short>(x), static_cast<short>(y));
    hero->set_team_num(static_cast<unsigned char>(team));
    hero->set_real_team_num(255);
    hero->set_act_type(ACT_CONTROL);
    hero->set_owned_myguy(std::make_unique<guy>(family));
    hero->myguy->id = 1;
    return hero;
}

void keep_match_running(GameWorld& world)
{
    world.mode.vars[static_cast<std::size_t>(kScoreLimitSlot)] = 100000;
    world.mode.vars[static_cast<std::size_t>(kTimeLimitSlot)] = 100000;
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w == nullptr || w->dead() || w->query_order() != Order::Living ||
            w->team_num() >= SCORE_TEAM_COUNT || w->stats() == nullptr)
        {
            continue;
        }
        w->stats()->set_bit_flags(BIT_INVINCIBLE, 1);
    }
}

bool has_live_owned_weapon(const GameWorld& world, const walker* owner)
{
    for (const auto& uptr : world.weaplist)
    {
        const walker* weapon = uptr.get();
        if (weapon != nullptr && !weapon->dead() && weapon->owner() == owner)
            return true;
    }
    return false;
}

walker* team_family(GameWorld& world, int team, int family)
{
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w != nullptr && !w->dead() &&
            w->query_order() == Order::Living &&
            w->team_num() == static_cast<unsigned char>(team) &&
            w->family() == family)
        {
            return w;
        }
    }
    return nullptr;
}

walker* stage_causeway_water_ball(GameWorld& world, int cx, int cy)
{
    auto& vars = world.mode.vars;
    walker* ball = world.find_by_id(static_cast<std::uint32_t>(
        vars[static_cast<std::size_t>(kBasketballBallEntitySlot)]));
    if (ball == nullptr)
        return nullptr;
    vars[static_cast<std::size_t>(kBasketballBallStateSlot)] =
        kBasketballFree;
    vars[static_cast<std::size_t>(kBasketballCarrierSlot)] = 0;
    vars[static_cast<std::size_t>(kBasketballBallPxSlot)] = cx * 256;
    vars[static_cast<std::size_t>(kBasketballBallPySlot)] = cy * 256;
    vars[static_cast<std::size_t>(kBasketballBallPzSlot)] = 0;
    vars[static_cast<std::size_t>(kBasketballBallVxSlot)] = 0;
    vars[static_cast<std::size_t>(kBasketballBallVySlot)] = 0;
    vars[static_cast<std::size_t>(kBasketballBallVzSlot)] = 0;
    vars[static_cast<std::size_t>(kBasketballLastToucherSlot)] = 0;
    vars[static_cast<std::size_t>(kBasketballJumpUntilSlot)] = 0;
    ball->set_team_num(static_cast<unsigned char>(SCORE_TEAM_COUNT));
    ball->setxy(static_cast<short>(cx - ball->sizex() / 2),
                static_cast<short>(cy - ball->sizey() / 2));
    return ball;
}

int front_command_type(const walker& w)
{
    const statistics* stats = w.stats();
    if (stats == nullptr || stats->commands.empty())
        return -1;
    return stats->commands.front().commandtype;
}

bool enemy_in_contact(const GameWorld& world, const walker& subject)
{
    for (const auto& uptr : world.oblist)
    {
        const walker* other = uptr.get();
        if (other == nullptr || other->dead() ||
            other->query_order() != Order::Living ||
            other->team_num() >= SCORE_TEAM_COUNT ||
            other->team_num() == subject.team_num())
        {
            continue;
        }
        const int distance =
            std::abs(static_cast<int>(other->xpos()) - subject.xpos()) +
            std::abs(static_cast<int>(other->ypos()) - subject.ypos());
        if (distance <= kContactDistance)
            return true;
    }
    return false;
}

struct IntentSample
{
    bool present = false;
    bool far = false;
    std::uint64_t signature = 0;
    int distance = 0;
};

IntentSample current_intent(const walker& w)
{
    const statistics* stats = w.stats();
    if (stats != nullptr && !stats->commands.empty())
    {
        const command& front = stats->commands.front();
        if (front.commandtype == COMMAND_WALK)
        {
            // Deterministic mutation probes use the engine's fixed-direction
            // walk command. Project position onto that direction and negate
            // it, so a successful commanded step is the same strictly
            // decreasing progress metric as distance-to-target below.
            const int dx = std::clamp(static_cast<int>(front.com1), -1, 1);
            const int dy = std::clamp(static_cast<int>(front.com2), -1, 1);
            if (dx != 0 || dy != 0)
            {
                const std::uint64_t signature =
                    (UINT64_C(4) << 61) |
                    (static_cast<std::uint64_t>(dx + 1) << 2) |
                    static_cast<std::uint64_t>(dy + 1);
                const int projection = dx * static_cast<int>(w.xpos()) +
                                       dy * static_cast<int>(w.ypos());
                return {true, true, signature, -projection};
            }
        }
        if (front.commandtype == COMMAND_GOTO)
        {
            const int tx = static_cast<int>(front.com1);
            const int ty = static_cast<int>(front.com2);
            const int distance = std::abs(tx - w.xpos()) +
                                 std::abs(ty - w.ypos());
            const std::uint64_t signature =
                (UINT64_C(1) << 63) |
                (static_cast<std::uint64_t>(static_cast<std::uint16_t>(tx))
                 << 16) |
                static_cast<std::uint16_t>(ty);
            return {true, distance > kFarDistance, signature, distance};
        }
        if (front.commandtype == COMMAND_ATTACK)
        {
            const walker* foe = w.foe();
            if (foe != nullptr && !foe->dead())
            {
                const int distance =
                    std::abs(static_cast<int>(foe->xpos()) - w.xpos()) +
                    std::abs(static_cast<int>(foe->ypos()) - w.ypos());
                return {true, distance > kFarDistance,
                        (UINT64_C(2) << 61) | foe->entity_id(), distance};
            }
        }
    }

    const walker* foe = w.foe();
    if (w.act_type() == ACT_RANDOM && foe != nullptr && !foe->dead())
    {
        const int distance =
            std::abs(static_cast<int>(foe->xpos()) - w.xpos()) +
            std::abs(static_cast<int>(foe->ypos()) - w.ypos());
        return {true, distance > kFarDistance,
                (UINT64_C(3) << 61) | foe->entity_id(), distance};
    }
    return {};
}

struct MotionWindow
{
    explicit MotionWindow(const walker& w)
        : id(w.entity_id())
        , team(w.team_num())
        , family(w.family())
        , start_x(w.xpos())
        , start_y(w.ypos())
        , last_x(w.xpos())
        , last_y(w.ypos())
        , min_x(w.xpos())
        , max_x(w.xpos())
        , min_y(w.ypos())
        , max_y(w.ypos())
    {
    }

    std::uint32_t id;
    int team;
    int family;
    int start_x;
    int start_y;
    int last_x;
    int last_y;
    int min_x;
    int max_x;
    int min_y;
    int max_y;
    int productive_ticks = 0;
    int unproductive_ticks = 0;
    int exempt_ticks = 0;
    std::uint64_t intent_signature = 0;
    int intent_best = std::numeric_limits<int>::max();
    bool missing = false;
};

void sample_motion(GameWorld& world, MotionWindow& track)
{
    walker* w = world.find_by_id(track.id);
    if (w == nullptr || w->dead())
    {
        track.missing = true;
        return;
    }
    track.last_x = w->xpos();
    track.last_y = w->ypos();
    track.min_x = std::min(track.min_x, track.last_x);
    track.max_x = std::max(track.max_x, track.last_x);
    track.min_y = std::min(track.min_y, track.last_y);
    track.max_y = std::max(track.max_y, track.last_y);
    const IntentSample intent = current_intent(*w);
    const statistics* stats = w->stats();
    const bool posted = w->act_type() == ACT_GUARD;
    const bool frozen = stats != nullptr && stats->frozen_delay() > 0;
    const bool engaged = enemy_in_contact(world, *w);
    const bool cooldown = w->busy() > 0.0f ||
                          has_live_owned_weapon(world, w);
    if (posted || frozen || engaged || cooldown)
    {
        ++track.exempt_ticks;
        track.intent_signature = 0;
        track.intent_best = std::numeric_limits<int>::max();
        return;
    }

    // An empty queue/foe is itself unproductive. For a command, only a
    // new best distance on the SAME target is progress; an early advance
    // cannot pardon the remaining idle ticks in this window, and a target
    // refresh cannot manufacture progress by resetting the baseline.
    if (!intent.present)
    {
        ++track.unproductive_ticks;
        track.intent_signature = 0;
        track.intent_best = std::numeric_limits<int>::max();
        return;
    }
    if (track.intent_signature != intent.signature)
    {
        track.intent_signature = intent.signature;
        track.intent_best = intent.distance;
        ++track.unproductive_ticks;
        return;
    }
    if (intent.distance < track.intent_best)
    {
        track.intent_best = intent.distance;
        ++track.productive_ticks;
    }
    else
    {
        ++track.unproductive_ticks;
    }
}

bool is_stalled(const MotionWindow& track)
{
    const int span_x = track.max_x - track.min_x;
    const int span_y = track.max_y - track.min_y;
    const int net = std::abs(track.last_x - track.start_x) +
                    std::abs(track.last_y - track.start_y);
    return !track.missing &&
           track.unproductive_ticks >= kUnproductiveTicks &&
           span_x <= kStationaryAxisSpan &&
           span_y <= kStationaryAxisSpan && net <= kStationaryNet;
}

struct StationarityResult
{
    int windows = 0;
    int sample_attempts = 0;
    int missing_samples = 0;
    int stalled_windows = 0;
    std::set<std::uint32_t> stalled_ids;
};

StationarityResult run_stationarity(
    GameWorld& world, const std::vector<std::uint32_t>& ids,
    std::function<void()> before_tick = {})
{
    StationarityResult result;
    // Six adjacent windows cover 1,800 uninterrupted ticks. A persistent
    // jam that begins too late to earn 240 samples in one window is measured
    // from a clean origin in the next; only a jam beginning in the final 60
    // ticks lacks the evidence duration the predicate deliberately requires.
    for (int window = 0; window < kWindowCount; ++window)
    {
        std::vector<MotionWindow> tracks;
        tracks.reserve(ids.size());
        for (std::uint32_t id : ids)
        {
            walker* w = world.find_by_id(id);
            if (w == nullptr || w->dead())
            {
                ++result.missing_samples;
                continue;
            }
            tracks.emplace_back(*w);
        }

        for (int tick = 0; tick < kWindowTicks; ++tick)
        {
            if (before_tick)
                before_tick();
            world.tick();
            for (MotionWindow& track : tracks)
            {
                sample_motion(world, track);
                ++result.sample_attempts;
            }
        }
        for (const MotionWindow& track : tracks)
        {
            if (track.missing)
                ++result.missing_samples;
            if (is_stalled(track))
            {
                ++result.stalled_windows;
                result.stalled_ids.insert(track.id);
            }
        }
        ++result.windows;
    }
    return result;
}

std::string stalled_description(const GameWorld& world,
                                const std::set<std::uint32_t>& ids)
{
    std::string out;
    for (std::uint32_t id : ids)
    {
        const walker* w = world.find_by_id(id);
        out += " id=" + std::to_string(id);
        if (w != nullptr)
        {
            out += " team=" + std::to_string(w->team_num());
            out += " family=" + std::to_string(w->family());
            out += " at=(" + std::to_string(w->xpos()) + "," +
                   std::to_string(w->ypos()) + ")";
        }
    }
    return out;
}

void expect_no_script_errors(GameWorld& world, int level)
{
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count) << level;
    for (const auto& error : world.scripts().host().errors())
        ADD_FAILURE() << "scen" << level << " script error: "
                      << error.message;
}

void retire_score_team_livings(GameWorld& world)
{
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w == nullptr || w->dead() || w->query_order() != Order::Living ||
            w->team_num() >= SCORE_TEAM_COUNT)
        {
            continue;
        }
        w->set_team_num(static_cast<unsigned char>(SCORE_TEAM_COUNT));
        w->set_real_team_num(255);
        w->set_act_type(ACT_SIT);
        w->set_dead(1);
    }
}

bool force_wipe_reset(GameWorld& world, int level)
{
    const std::int32_t expired_since =
        static_cast<std::int32_t>(world.tick_count_) - kDeadBallTicks;
    if (level == kCauseway)
    {
        // Basketball's wipe watchdog ignores ball state and attendance.
        world.mode.vars[static_cast<std::size_t>(
            kBasketballStallSinceSlot)] = expired_since;
        world.tick();
        return true;
    }

    // Soccer's reset is the dead-ball path. Put the objective in the middle
    // of Mudbowl's north pool so waterlogging (not nearby survivors) owns
    // the reset, then expire its clock. This is the shipped recovery path,
    // only with the 600-tick wait banked directly in deterministic state.
    const int cx = 21 * GRID_SIZE + GRID_SIZE / 2;
    const int cy = 6 * GRID_SIZE + GRID_SIZE / 2;
    auto& vars = world.mode.vars;
    walker* ball = world.find_by_id(static_cast<std::uint32_t>(
        vars[static_cast<std::size_t>(kSoccerBallEntitySlot)]));
    if (ball == nullptr)
        return false;
    vars[static_cast<std::size_t>(kSoccerBallPxSlot)] = cx * 256;
    vars[static_cast<std::size_t>(kSoccerBallPySlot)] = cy * 256;
    vars[static_cast<std::size_t>(kSoccerBallVxSlot)] = 0;
    vars[static_cast<std::size_t>(kSoccerBallVySlot)] = 0;
    vars[static_cast<std::size_t>(kSoccerStallSinceSlot)] = expired_since;
    ball->setxy(static_cast<short>(cx - ball->sizex() / 2),
                static_cast<short>(cy - ball->sizey() / 2));
    world.tick();
    return true;
}

void paint_full_water_band(GameWorld& world)
{
    for (int y = 0; y < world.grid.h; ++y)
        for (int x = 21; x <= 28; ++x)
            world.grid.data[static_cast<std::size_t>(x + y * world.grid.w)] =
                PIX_WATER1;
}

walker* add_jam_probe(GameWorld& world, int team, int tx, int ty)
{
    walker* w = world.add_ob(Order::Living, FAMILY_SOLDIER);
    if (w == nullptr)
        return nullptr;
    w->setxy(static_cast<short>(tx * GRID_SIZE),
             static_cast<short>(ty * GRID_SIZE));
    w->set_team_num(static_cast<unsigned char>(team));
    w->set_real_team_num(255);
    w->set_act_type(ACT_RANDOM);
    w->set_current_weapon(FAMILY_KNIFE);
    w->stats()->set_bit_flags(BIT_NO_RANGED, 1);
    w->stats()->set_bit_flags(BIT_INVINCIBLE, 1);
    return w;
}

} // namespace

TEST_F(ModesWaterRealCampaign,
       automatic_water_squads_are_nonteleporting_on_full_init_and_wipe_fill)
{
    const std::vector<int> expected = sorted_water_squad();
    for (int level : {kMudbowl, kCauseway})
    {
        LoadedModesWaterLevel fx(level);
        ASSERT_TRUE(fx.loaded) << level;
        request_two_automatic_squads(fx.world());
        fx.world().tick();
        ASSERT_TRUE(fx.world().mode.active) << level;
        EXPECT_EQ(level == kMudbowl ? kSoccerModeId : kBasketballModeId,
                  fx.world().mode.vars[static_cast<std::size_t>(kModeIdSlot)])
            << level;
        EXPECT_EQ(expected, team_bot_families(fx.world(), 0)) << level;
        EXPECT_EQ(expected, team_bot_families(fx.world(), 1)) << level;

        // No-corpse fallback is the other family-selecting path. Move the
        // original red squad outside the scoring domain; the shared wipe
        // seam must construct the same safe five, not fall back to the core
        // mage-bearing roster.
        for (const auto& uptr : fx.world().oblist)
        {
            walker* w = uptr.get();
            if (w != nullptr && !w->dead() &&
                w->query_order() == Order::Living && w->team_num() == 0)
            {
                w->set_team_num(
                    static_cast<unsigned char>(SCORE_TEAM_COUNT));
                w->set_real_team_num(255);
                w->set_act_type(ACT_SIT);
                w->set_dead(1);
            }
            else if (w != nullptr && !w->dead() &&
                     w->query_order() == Order::Living &&
                     w->team_num() < SCORE_TEAM_COUNT)
            {
                // Keep the surviving side from launching an incidental
                // tick-2 projectile through the forced soccer reset point.
                w->set_act_type(ACT_SIT);
            }
        }
        ASSERT_TRUE(force_wipe_reset(fx.world(), level)) << level;
        EXPECT_EQ(expected, team_bot_families(fx.world(), 0)) << level;
        expect_no_script_errors(fx.world(), level);
    }
}

TEST_F(ModesWaterRealCampaign,
       matched_water_fill_keeps_the_human_mage_and_builds_an_orc_bot)
{
    for (int level : {kMudbowl, kCauseway})
    {
        LoadedModesWaterLevel fx(level);
        ASSERT_TRUE(fx.loaded) << level;
        GameWorld& world = fx.world();
        world.ctf_requested_strip_scenario_troops = og::sim::kTroopsMatched;
        world.ctf_requested_fill[0] = og::sim::kFillNone;
        world.ctf_requested_fill[1] = og::sim::kFillFair;
        world.mode.vars[static_cast<std::size_t>(kSquadSeedSlot)] =
            kSquadSeedMageFirst;
        walker* hero = add_roster_hero(world, FAMILY_MAGE, 0,
                                       20 * GRID_SIZE, 14 * GRID_SIZE);
        ASSERT_NE(nullptr, hero) << level;
        const std::uint32_t hero_id = hero->entity_id();

        world.tick();
        ASSERT_TRUE(world.mode.active) << level;
        EXPECT_EQ(level == kMudbowl ? kSoccerModeId : kBasketballModeId,
                  world.mode.vars[static_cast<std::size_t>(kModeIdSlot)])
            << level;
        const walker* surviving_hero = world.find_by_id(hero_id);
        ASSERT_NE(nullptr, surviving_hero) << level;
        EXPECT_EQ(FAMILY_MAGE, surviving_hero->family()) << level;
        EXPECT_NE(nullptr, surviving_hero->myguy) << level;
        EXPECT_EQ(std::vector<int>{FAMILY_ORC}, team_bot_families(world, 1))
            << "seed code 3 selects safe-roster member four on scen" << level;
        expect_no_script_errors(world, level);
    }
}

TEST_F(ModesWaterRealCampaign,
       causeway_recovery_selects_reachable_archer_and_shoots_from_dry_land)
{
    LoadedModesWaterLevel fx(kCauseway);
    ASSERT_TRUE(fx.loaded);
    request_two_automatic_squads(fx.world());
    fx.world().tick();
    ASSERT_TRUE(fx.world().mode.active);
    ASSERT_EQ(kBasketballModeId,
              fx.world().mode.vars[static_cast<std::size_t>(kModeIdSlot)]);

    walker* knife = team_family(fx.world(), 0, FAMILY_THIEF);
    walker* archer = team_family(fx.world(), 0, FAMILY_ARCHER);
    ASSERT_NE(nullptr, knife);
    ASSERT_NE(nullptr, archer);
    for (const auto& uptr : fx.world().oblist)
    {
        walker* w = uptr.get();
        if (w == nullptr || w->dead() ||
            w->query_order() != Order::Living ||
            w->team_num() >= SCORE_TEAM_COUNT)
        {
            continue;
        }
        w->stats()->set_bit_flags(BIT_NO_RANGED, 1);
        w->stats()->set_bit_flags(BIT_INVINCIBLE, 1);
    }
    knife->stats()->set_bit_flags(BIT_NO_RANGED, 0);
    knife->stats()->set_weapon_cost(0);
    knife->set_current_weapon(FAMILY_KNIFE);
    archer->stats()->set_bit_flags(BIT_NO_RANGED, 0);
    archer->stats()->set_weapon_cost(0);
    archer->set_current_weapon(FAMILY_ARROW);

    // The center of Causeway's committed northwest 11x8 bay. Both shooters
    // retain their generated marker positions; only the objective is staged.
    constexpr int kBallX = 13 * GRID_SIZE + GRID_SIZE / 2;
    constexpr int kBallY = 4 * GRID_SIZE + GRID_SIZE / 2;
    ASSERT_EQ(TYPE_WATER,
              fx.world().smoother_for_floor(0).query_genre_x_y(13, 4));
    walker* ball = stage_causeway_water_ball(fx.world(), kBallX, kBallY);
    ASSERT_NE(nullptr, ball);
    EXPECT_FALSE(knife->can_approach_weapon_range(ball));
    EXPECT_TRUE(archer->can_approach_weapon_range(ball));
    const int archer_reach = archer->prospective_weapon_reach(0, -1);
    ASSERT_GT(archer_reach, 0);
    const int initial_distance =
        std::abs(static_cast<int>(ball->xpos()) - archer->xpos()) +
        std::abs(static_cast<int>(ball->ypos()) - archer->ypos());
    ASSERT_GT(initial_distance, archer_reach)
        << "the generated archer must approach, not begin in weapon range";

    const std::uint32_t next_cadence =
        ((fx.world().tick_count_ / 15) + 1) * 15;
    fx.world().tick_count_ = next_cadence - 1;
    fx.world().tick();
    EXPECT_NE(COMMAND_ATTACK, front_command_type(*knife));
    EXPECT_EQ(COMMAND_ATTACK, front_command_type(*archer));
    EXPECT_EQ(ball, archer->foe());

    bool saw_live_projectile = false;
    int previous_x = archer->xpos();
    int previous_y = archer->ypos();
    int max_tick_step = 0;
    for (int tick = 0; tick < 240 &&
                       fx.world().mode.vars[static_cast<std::size_t>(
                           kBasketballLastToucherSlot)] !=
                           static_cast<std::int32_t>(archer->entity_id());
         ++tick)
    {
        fx.world().tick();
        saw_live_projectile =
            saw_live_projectile || has_live_owned_weapon(fx.world(), archer);
        const int step = std::abs(archer->xpos() - previous_x) +
                         std::abs(archer->ypos() - previous_y);
        max_tick_step = std::max(max_tick_step, step);
        previous_x = archer->xpos();
        previous_y = archer->ypos();
    }

    EXPECT_TRUE(saw_live_projectile)
        << "ordinary COMMAND_ATTACK must create the archer's live arrow";
    EXPECT_GT(max_tick_step, 0)
        << "the generated archer walks into range under its command";
    EXPECT_LE(max_tick_step,
              2 * static_cast<int>(archer->stepsize()))
        << "the shooter advances only by ordinary diagonal walk steps";
    const int archer_tx =
        (archer->xpos() + archer->sizex() / 2) / GRID_SIZE;
    const int archer_ty =
        (archer->ypos() + archer->sizey() / 2) / GRID_SIZE;
    EXPECT_NE(TYPE_WATER,
              fx.world().smoother_for_floor(archer->floor())
                  .query_genre_x_y(archer_tx, archer_ty));
    EXPECT_EQ(static_cast<std::int32_t>(archer->entity_id()),
              fx.world().mode.vars[static_cast<std::size_t>(
                  kBasketballLastToucherSlot)]);
    EXPECT_EQ(kBasketballRebound,
              fx.world().mode.vars[static_cast<std::size_t>(
                  kBasketballBallStateSlot)]);
    EXPECT_EQ(704,
              std::abs(fx.world().mode.vars[static_cast<std::size_t>(
                           kBasketballBallVxSlot)]) +
                  std::abs(fx.world().mode.vars[static_cast<std::size_t>(
                           kBasketballBallVySlot)]));
    EXPECT_EQ(416,
              fx.world().mode.vars[static_cast<std::size_t>(
                  kBasketballBallVzSlot)]);
    expect_no_script_errors(fx.world(), kCauseway);
}

TEST_F(ModesWaterRealCampaign,
       shipped_water_fields_have_no_near_stationary_unproductive_window)
{
    for (int level : {kMudbowl, kCauseway})
    {
        LoadedModesWaterLevel fx(level);
        ASSERT_TRUE(fx.loaded) << level;
        request_two_automatic_squads(fx.world());
        fx.world().tick();
        ASSERT_TRUE(fx.world().mode.active) << level;
        EXPECT_EQ(level == kMudbowl ? kSoccerModeId : kBasketballModeId,
                  fx.world().mode.vars[static_cast<std::size_t>(kModeIdSlot)])
            << level;
        keep_match_running(fx.world());
        const std::vector<std::uint32_t> ids =
            automatic_living_ids(fx.world());
        ASSERT_EQ(10u, ids.size()) << level;

        const StationarityResult result = run_stationarity(fx.world(), ids);
        EXPECT_EQ(kWindowCount, result.windows) << level;
        EXPECT_EQ(kWindowCount * kWindowTicks * 10, result.sample_attempts)
            << level;
        EXPECT_EQ(0, result.missing_samples) << level;
        EXPECT_EQ(0, result.stalled_windows) << level;
        EXPECT_EQ(std::set<std::uint32_t>{}, result.stalled_ids)
            << "scen" << level
            << " accumulated a shoreline stall: "
            << stalled_description(fx.world(), result.stalled_ids);
        expect_no_script_errors(fx.world(), level);
    }
}

TEST_F(ModesWaterRealCampaign,
       stationarity_detector_catches_a_full_water_band_on_each_real_field)
{
    for (int level : {kMudbowl, kCauseway})
    {
        LoadedModesWaterLevel fx(level);
        ASSERT_TRUE(fx.loaded) << level;
        GameWorld& world = fx.world();
        retire_score_team_livings(world);
        paint_full_water_band(world);

        // Keep the loaded field geometry but suppress match resets: the tooth
        // is the real engine movement command against the mutated terrain,
        // observed by the same 1,800-tick stationarity runner as the shipped
        // simulations. Fixed COMMAND_WALK cannot path around the band, and a
        // lease one tick longer than the run keeps intent present throughout.
        world.type &= ~GameWorld::TYPE_SCRIPTED;
        walker* west = add_jam_probe(world, SCORE_TEAM_COUNT, 0, 0);
        walker* east = add_jam_probe(world, SCORE_TEAM_COUNT, 0, 0);
        ASSERT_NE(nullptr, west) << level;
        ASSERT_NE(nullptr, east) << level;
        constexpr int kBandLeft = 21;
        constexpr int kBandRight = 28;
        constexpr int kProbeTy = 14;
        // Close the two one-tile shoreline pockets vertically with the same
        // mutated water. walker::walkstep deliberately wall-slides along a
        // straight bank; these teeth make both fixed east/west attempts a
        // true zero-motion jam without introducing a second blocker kind.
        for (const std::array<int, 2>& cell :
             {std::array<int, 2>{kBandLeft - 1, kProbeTy - 1},
              {kBandLeft - 1, kProbeTy + 1},
              {kBandRight + 1, kProbeTy - 1},
              {kBandRight + 1, kProbeTy + 1}})
        {
            world.grid.data[static_cast<std::size_t>(
                cell[0] + cell[1] * world.grid.w)] = PIX_WATER1;
        }
        const int west_x = kBandLeft * GRID_SIZE - west->sizex();
        const int east_x = (kBandRight + 1) * GRID_SIZE;
        const int probe_y = kProbeTy * GRID_SIZE;
        west->setxy(static_cast<short>(west_x), static_cast<short>(probe_y));
        east->setxy(static_cast<short>(east_x), static_cast<short>(probe_y));
        for (walker* probe : {west, east})
        {
            probe->set_act_type(ACT_CONTROL);
            probe->stats()->clear_command();
        }
        constexpr int kLeaseTicks = kWindowCount * kWindowTicks + 1;
        west->stats()->force_command(COMMAND_WALK, kLeaseTicks, 1, 0);
        east->stats()->force_command(COMMAND_WALK, kLeaseTicks, -1, 0);

        EXPECT_EQ(PIX_WATER1,
                  world.grid.data[static_cast<std::size_t>(
                      kBandLeft + kProbeTy * world.grid.w)])
            << level;
        EXPECT_EQ(PIX_WATER1,
                  world.grid.data[static_cast<std::size_t>(
                      kBandRight + kProbeTy * world.grid.w)])
            << level;
        EXPECT_TRUE(world.query_grid_passable(static_cast<float>(west_x),
                                              static_cast<float>(probe_y), west))
            << level;
        EXPECT_FALSE(world.query_grid_passable(
            static_cast<float>(west_x + 1), static_cast<float>(probe_y), west))
            << level;
        EXPECT_TRUE(world.query_grid_passable(static_cast<float>(east_x),
                                              static_cast<float>(probe_y), east))
            << level;
        EXPECT_FALSE(world.query_grid_passable(
            static_cast<float>(east_x - 1), static_cast<float>(probe_y), east))
            << level;

        const std::vector<std::uint32_t> ids = {
            west->entity_id(), east->entity_id()};
        const std::set<std::uint32_t> expected(ids.begin(), ids.end());
        const StationarityResult result = run_stationarity(world, ids);
        EXPECT_EQ(kWindowCount, result.windows) << level;
        EXPECT_EQ(kWindowCount * kWindowTicks * 2, result.sample_attempts)
            << level;
        EXPECT_EQ(0, result.missing_samples) << level;
        EXPECT_EQ(kWindowCount * 2, result.stalled_windows) << level;
        EXPECT_EQ(expected, result.stalled_ids) << level;
        EXPECT_EQ(west_x, west->xpos()) << level;
        EXPECT_EQ(probe_y, west->ypos()) << level;
        EXPECT_EQ(east_x, east->xpos()) << level;
        EXPECT_EQ(probe_y, east->ypos()) << level;
        expect_no_script_errors(world, level);
    }
}

TEST_F(ModesWaterRealCampaign,
       stationarity_detector_catches_one_water_cell_near_goal_outside_contact)
{
    // rng=1 avoids COMMAND_GOTO's legacy 1/300 path-reset branch while the
    // per-tick rearm below keeps this movement canary off pathfinding.
    LoadedModesWaterLevel fx(kCauseway, 1);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    retire_score_team_livings(world);
    world.type &= ~GameWorld::TYPE_SCRIPTED;

    // Put one water cell immediately against the probe's right edge and its
    // fixed foe on the opposite shore. The 32 px intent is outside the real
    // contact allowance but exactly on the former blanket-exemption edge.
    // COMMAND_GOTO attempts a real east walk every tick: water must keep that
    // movement at zero for all six detector windows, while passable water
    // moves it more than the 8 px stationarity allowance in the first window.
    constexpr int kWaterTx = 25;
    constexpr int kWaterTy = 14;
    world.grid.data[static_cast<std::size_t>(
        kWaterTx + kWaterTy * world.grid.w)] = PIX_WATER1;
    walker* probe = add_jam_probe(world, SCORE_TEAM_COUNT, 0, 0);
    ASSERT_NE(nullptr, probe);
    const int start_x = kWaterTx * GRID_SIZE - probe->sizex();
    const int start_y = kWaterTy * GRID_SIZE;
    constexpr int kGoalDistance = kFarDistance;
    const int goal_x = start_x + kGoalDistance;
    probe->setxy(static_cast<short>(start_x), static_cast<short>(start_y));
    probe->set_act_type(ACT_CONTROL);
    probe->stats()->clear_command();
    constexpr int kLeaseTicks = kWindowCount * kWindowTicks + 1;
    probe->stats()->force_command(COMMAND_GOTO, kLeaseTicks, goal_x, start_y);

    EXPECT_EQ(PIX_WATER1,
              world.grid.data[static_cast<std::size_t>(
                  kWaterTx + kWaterTy * world.grid.w)]);
    EXPECT_TRUE(world.query_grid_passable(static_cast<float>(start_x),
                                          static_cast<float>(start_y), probe));
    EXPECT_FALSE(world.query_grid_passable(static_cast<float>(start_x + 1),
                                           static_cast<float>(start_y), probe));
    const IntentSample intent = current_intent(*probe);
    ASSERT_TRUE(intent.present);
    EXPECT_FALSE(intent.far);
    EXPECT_EQ(kGoalDistance, intent.distance);

    const std::vector<std::uint32_t> ids = {probe->entity_id()};
    const StationarityResult result = run_stationarity(world, ids, [probe]() {
        // Suppress COMMAND_GOTO's right-hand/path fallback so every tick is
        // the same ordinary eastward grid-movement attempt. This is state
        // preparation, not sampling: world.tick still owns the attempted move.
        probe->set_curdir(FACE_RIGHT);
        probe->set_enddir(FACE_RIGHT);
        probe->set_path_check_counter(kWindowCount * kWindowTicks + 1);
        probe->stats()->set_last_distance(15000);
        probe->path_to_foe.clear();
    });
    EXPECT_EQ(kWindowCount, result.windows);
    EXPECT_EQ(kWindowCount * kWindowTicks, result.sample_attempts);
    EXPECT_EQ(0, result.missing_samples);
    EXPECT_EQ(kWindowCount, result.stalled_windows);
    EXPECT_EQ(std::set<std::uint32_t>{probe->entity_id()}, result.stalled_ids);
    EXPECT_EQ(start_x, probe->xpos());
    EXPECT_EQ(start_y, probe->ypos());
    expect_no_script_errors(world, kCauseway);
}
