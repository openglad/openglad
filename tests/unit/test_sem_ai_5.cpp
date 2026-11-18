/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch ai-5: how a bot turns toward a foe it cannot strike
// yet (Gameplay Fixes BE, #513), and how a melee-only bot fights in its
// ATTACK order (Gameplay Fixes BC, #447). Each test is named after the rule
// it asserts.
//
// Every scene is headless: an open field of grass 1024 px a side, bodies
// placed in pixels (each with its family's own box), the sim stream
// (GameWorld::rng_) pinned to a state whose draws are known.
//
// The engine facts the tests lean on (the old game has the same):
//  * walker::turn moves curdir one notch the short way round (a half turn
//    goes clockwise) and never writes enddir; a mobile walker's heading
//    follows the new facing, a stationary living's (a tower's) stays where
//    it was. Asked for the facing it has, it steps a notch counter-clockwise
//    with the setting off and does nothing with it on (Gameplay Fixes AT).
//  * living::act_round begins, after its timers, with "not yet facing
//    enddir: turn one notch toward it, and that is the act"; only an act
//    that faces its enddir reaches the queued orders.
//  * fire_check builds its test weapon and draws its waver once, next(h + 1)
//    with h the weapon's stepsize / 2 clamped to 0..127
//    (walker::set_weapon_heading), before any of its gates.
//  * The ATTACK order (statistics::do_command) aims at its foe along the
//    snapped delta (an axis when one delta is more than three times the
//    other, else a diagonal) and asks fire_check along it. A melee-only bot
//    is refused NoRanged whenever its foe is inside its weapon's reach (the
//    Manhattan distance between the boxes' corners), whatever it faces: the
//    reach gate and the melee-only gate come before the facing gate.
//  * init_fire charges busy with the family's fire delay and starts the
//    attack animation, whose first frame draws nothing; the blow leaves when
//    the animation's row ends (walker::animate calls fire()). busy drains by
//    one on each act that does not animate; init_fire starts nothing while
//    it is above 0.
//  * walkstep tries the full step, then a one-unit step, then for a bot one
//    stepsize sideways by its table (RIGHT turns to UP). A loud step into a
//    hostile body swings at it (living::collide calls init_fire); a refused
//    step shoves an ally two times in three (living::shove, one rng(3)).
//  * The obmap's overlap test (::collide) shrinks each box by one pixel, so
//    boxes that only touch do not meet; walkers on different floors never
//    meet.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/specials_view.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace {

// A draw and the value a pinned state must give (kAny: any value, kNonZero:
// anything but 0).
constexpr std::uint32_t kAny = 0xFFFFFFFFu;
constexpr std::uint32_t kNonZero = 0xFFFFFFFEu;
struct Draw
{
    std::uint32_t bound;
    std::uint32_t want;
};

// The first RNG state from which the draws come out as `draws` says.
std::uint32_t state_where(std::initializer_list<Draw> draws)
{
    for (std::uint32_t seed = 1;; ++seed) {
        og::sim::SimRandom probe(seed);
        bool all = true;
        for (const Draw& d : draws) {
            const std::uint32_t got = probe.next(d.bound);
            if (d.want == kNonZero)
                all = all && got != 0;
            else
                all = all && (d.want == kAny || got == d.want);
        }
        if (all)
            return seed;
    }
}

// The state after the draws `bounds` from `state`.
std::uint32_t state_after(std::uint32_t state, std::initializer_list<std::uint32_t> bounds)
{
    og::sim::SimRandom probe(state);
    for (std::uint32_t b : bounds)
        (void)probe.next(b);
    return probe.state_;
}

// The front of a walker's command queue.
struct Cmd
{
    std::size_t size = 0;
    int type = -1;
    int count = -1;
    int com1 = 0;
    int com2 = 0;
};

Cmd front_of(const walker* w)
{
    Cmd f;
    const auto& c = w->stats()->commands;
    f.size = c.size();
    if (!c.empty()) {
        f.type = c.front().commandtype;
        f.count = c.front().commandcount;
        f.com1 = c.front().com1;
        f.com2 = c.front().com2;
    }
    return f;
}

// The unit step of each facing (UP, UR, R, DR, D, DL, L, UL).
constexpr int kStep[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};

const char* setting(short fixes)
{
    return fixes ? "Gameplay Fixes on" : "Gameplay Fixes off";
}

class SemAi5 : public ::testing::Test {
protected:
    og::test::ScopedPackStoreState pack_store_restore_;
    og::test::ScopedHookFailureGuard guard_;
    std::unique_ptr<TestGameWorld> tw_;

    void SetUp() override
    {
        og::test::mount_core_pack();
        fresh_world();
    }

    void TearDown() override
    {
        EXPECT_EQ(0u, guard_.count()) << guard_.message();
        tw_.reset();
    }

    // An open field of grass, 1024 px a side, with nobody on it.
    void fresh_world(short fixes = 0)
    {
        tw_.reset();
        tw_ = std::make_unique<TestGameWorld>();
        GameWorld& w = world();
        w.resize_grid(64, 64);
        std::fill_n(w.grid.data.get(),
                    static_cast<std::size_t>(w.grid.w) * w.grid.h,
                    static_cast<std::uint8_t>(PIX_GRASS1));
        w.my_team = 0;
        w.gameplay_fixes = fixes;
        w.rng_.state_ = 12345u;
    }

    GameWorld& world() { return tw_->world(); }

    // A body (with its family's box) moved to (x, y): the obmap registers it
    // in every cell its box covers there.
    static walker* place(walker* w, short x, short y)
    {
        if (w == nullptr)
            return nullptr;
        w->setxy(static_cast<short>(x + 1), y);  // force a real move
        w->setxy(x, y);
        return w;
    }

    // Two floors: floor 1 a second field of grass of the same size.
    void two_floors()
    {
        GameWorld& w = world();
        w.set_floor_count(2);
        const int gw = w.grid.w;
        const int gh = w.grid.h;
        auto* buf = new unsigned char[static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh)];
        std::fill(buf, buf + static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh),
                  static_cast<unsigned char>(PIX_GRASS1));
        w.grid_for_floor(1) = PixieData(1, static_cast<unsigned char>(gw), static_cast<unsigned char>(gh), buf);
        w.smoother_for_floor(1).set_target(w.grid_for_floor(1));
    }

    // One tile of floor 0's grid.
    void tile(int tx, int ty, unsigned char pix)
    {
        world().grid.data[static_cast<std::size_t>(tx + world().grid.w * ty)] = pix;
    }

    // A level-4 bot (ACT_RANDOM, no seat) with its family's box, 400 hp,
    // 500 MP, no regeneration, walking, facing right, on `floor` (set before
    // the body is placed: the obmap files a body under its floor).
    walker* living_at(int family, unsigned char team, short x, short y, short floor = 0)
    {
        walker* w = world().add_ob(Order::Living, family);
        if (w == nullptr)
            return nullptr;
        w->set_floor(floor);
        w->set_team_num(team);
        w->set_real_team_num(255);
        w->set_act_type(ACT_RANDOM);
        w->stats()->set_level(4);
        w->stats()->set_max_hitpoints(400.0f);
        w->stats()->set_hitpoints(400.0f);
        w->stats()->set_max_magicpoints(500.0f);
        w->stats()->set_magicpoints(500.0f);
        w->stats()->set_magic_per_round(0.0f);
        w->stats()->set_heal_per_round(0.0f);
        w->set_busy(0.0f);
        w->set_ani_type(ANI_WALK);
        face(w, FACE_RIGHT);
        return place(w, x, y);
    }

    // The game's own snap-face (walker::face_delta): curdir, enddir and the
    // heading fire_check aims along (the facing's unit step times stepsize,
    // or the unit step itself for a tower, whose stepsize is 0).
    static void face(walker* w, int dir)
    {
        w->face_delta(static_cast<short>(kStep[dir][0]), static_cast<short>(kStep[dir][1]));
    }

    // The bound of the waver draw of the test weapon `w` builds for
    // fire_check, read from a weapon it builds and then drops (no draw).
    std::uint32_t waver_bound_of(walker* w)
    {
        walker* k = w->create_weapon();
        if (k == nullptr)
            return 0;
        const auto h = static_cast<signed char>(std::clamp(k->stepsize() / 2, 0.0f, 127.0f));
        world().remove_ob(k);
        return static_cast<std::uint32_t>(h + 1);
    }

    // Do the box of `a` moved to (ax, ay) and the box of `b` where it stands
    // meet, by the obmap's own test (each box shrunk by a pixel)?
    static bool meets(const walker* a, int ax, int ay, const walker* b)
    {
        return ::collide(static_cast<short>(ax), static_cast<short>(ay), a->sizex(), a->sizey(),
                         b->xpos(), b->ypos(), b->sizex(), b->sizey()) != 0;
    }

    // The box of the weapon `w` would put on its pad the way it faces now
    // (walker::weapon_pad, the table fire() reads), read from a weapon it
    // builds and then drops (no draw).
    struct Box
    {
        int x = 0;
        int y = 0;
        short sx = 0;
        short sy = 0;
    };
    Box pad_box(walker* w)
    {
        Box b;
        walker* k = w->create_weapon();
        if (k == nullptr)
            return b;
        short px = 0, py = 0;
        w->weapon_pad(*k, px, py);
        b = Box{px, py, k->sizex(), k->sizey()};
        world().remove_ob(k);
        return b;
    }
    static bool box_meets(const Box& b, const walker* body)
    {
        return ::collide(static_cast<short>(b.x), static_cast<short>(b.y), b.sx, b.sy,
                         body->xpos(), body->ypos(), body->sizex(), body->sizey()) != 0;
    }

    // The live weapons in the world (a shot started, or a probe left alive).
    int live_weapons()
    {
        int n = 0;
        for (const auto& u : world().weaplist)
            if (u && !u->dead())
                ++n;
        return n;
    }

    // A bot, its foe (a team-0 soldier) and the delta from the bot to it.
    struct Scene
    {
        walker* bot = nullptr;
        walker* soldier = nullptr;
        short dx = 0;
        short dy = 0;
    };

    // The orc scenes' path-check counter: more than the rounds of chase any
    // of them runs, so none re-plans.
    static constexpr int kCounter = 50;

    // A melee-only orc (team 1) at (320,320) facing `start`, holding its foe,
    // a team-0 soldier `gap` px off the orc's right side, their boxes level;
    // the orc's path-check counter is kCounter.
    Scene orc_scene(short fixes, int start, short gap)
    {
        Scene sc;
        fresh_world(fixes);
        sc.bot = living_at(FAMILY_ORC, 1, 320, 320);
        if (sc.bot == nullptr)
            return sc;
        const short sx = static_cast<short>(320 + sc.bot->sizex() + gap);
        sc.soldier = living_at(FAMILY_SOLDIER, 0, sx, 320);
        if (sc.soldier == nullptr)
            return sc;
        face(sc.bot, start);
        sc.bot->set_foe(sc.soldier);
        sc.bot->set_path_check_counter(kCounter);
        sc.dx = static_cast<short>(sc.soldier->xpos() - sc.bot->xpos());
        sc.dy = static_cast<short>(sc.soldier->ypos() - sc.bot->ypos());
        return sc;
    }

    // A team-1 tower at (320,320) facing UP, its heading the unit step up (a
    // tower's stepsize is 0, so face_delta gives it a unit heading, #350),
    // holding its foe, a team-0 soldier 16 px off the tower's right side with
    // the soldier's centre on the tower's centre line.
    Scene tower_scene(short fixes)
    {
        Scene sc;
        fresh_world(fixes);
        sc.bot = living_at(FAMILY_TOWER1, 1, 320, 320);
        if (sc.bot == nullptr)
            return sc;
        const short sx = static_cast<short>(320 + sc.bot->sizex() + 16);
        sc.soldier = living_at(FAMILY_SOLDIER, 0, sx, 320);
        if (sc.soldier == nullptr)
            return sc;
        place(sc.soldier, sx, static_cast<short>(320 + (sc.bot->sizey() - sc.soldier->sizey()) / 2));
        face(sc.bot, FACE_UP);
        sc.bot->set_foe(sc.soldier);
        sc.dx = static_cast<short>(sc.soldier->xpos() - sc.bot->xpos());
        sc.dy = static_cast<short>(sc.soldier->ypos() - sc.bot->ypos());
        return sc;
    }

    // The rounds of the ATTACK order the BC tests queue.
    static constexpr int kOrder = 30;

    // The ATTACK order as the bot's only order: kOrder rounds, com1 and com2
    // `com` (1 from walk_to_foe's near branch, 0 from a mode's script; the
    // order reads neither).
    static void hold_attack(walker* w, int com = 1)
    {
        w->stats()->clear_command();
        w->stats()->add_command(COMMAND_ATTACK, kOrder, com, com);
    }

    // The line check's answer along (dx, dy): None when it passes, else why
    // it refuses. Asking builds a test weapon and draws its waver, so a test
    // asks it in a twin world built the same way.
    static walker::FireCheckDenial refusal(walker* w, short dx, short dy)
    {
        walker::FireCheckDenial why = walker::FireCheckDenial::None;
        if (w->fire_check(dx, dy, &why))
            return walker::FireCheckDenial::None;
        return why;
    }

    // The ATTACK order's aim at `foe` (stats.cpp COMMAND_ATTACK): an axis when
    // one delta is more than three times the other, else a diagonal; each
    // half -1, 0 or 1.
    struct Aim
    {
        short dx = 0;
        short dy = 0;
    };
    static Aim order_aim(const walker* bot, const walker* foe)
    {
        short dx = static_cast<short>(foe->xpos() - bot->xpos());
        short dy = static_cast<short>(foe->ypos() - bot->ypos());
        if (std::abs(dx) > std::abs(3 * dy))
            dy = 0;
        if (std::abs(dy) > std::abs(3 * dx))
            dx = 0;
        return Aim{static_cast<short>((dx > 0) - (dx < 0)), static_cast<short>((dy > 0) - (dy < 0))};
    }

    // The stream that takes act() into act_random with the held foe kept:
    // the special roll not 0, the second 1-in-5 0, the 1-in-80 not 0; then
    // the waver of the line check's probe; then, on the first act that runs
    // a SEARCH, walk_to_foe's 1-in-300 not 0 (no reset: the chase goes on and
    // counts the path-check counter down by one).
    static std::uint32_t into_act_random(std::uint32_t waver)
    {
        return state_where({{5, kNonZero}, {5, 0}, {80, kNonZero}, {waver, kAny}, {300, kNonZero}});
    }

    // One body for a_faced_melee_only_attacker_swings_at_a_body_on_its_pad_with_the_setting_on
    // and an_attack_order_queued_by_a_script_fights_the_same_way: the orc of
    // orc_scene facing RIGHT, its foe 1 px off its right side (the soldier's
    // body on the orc's pad), not busy, holding the ATTACK order with com1
    // and com2 `com`; one round. With `stale_heading` the orc's heading
    // (lastx, lasty) then points DOWN while curdir and enddir stay RIGHT, so
    // its pad along that heading holds nothing; the round's snap points the
    // heading back at the foe before the pad is asked.
    void faced_swing_round(int com, bool stale_heading = false)
    {
        const auto point_heading_down = [](walker* w) {
            w->set_lastx(0.0f);
            w->set_lasty(w->stepsize());
        };
        for (short fixes : {short{0}, short{1}}) {
            SCOPED_TRACE(setting(fixes));
            Scene twin = orc_scene(fixes, FACE_RIGHT, 1);
            ASSERT_TRUE(twin.bot && twin.soldier);
            if (stale_heading)
                point_heading_down(twin.bot);
            ASSERT_TRUE(refusal(twin.bot, 1, 0) == walker::FireCheckDenial::NoRanged)
                << "the staging: refused as melee only, the foe in reach";

            Scene sc = orc_scene(fixes, FACE_RIGHT, 1);
            ASSERT_TRUE(sc.bot && sc.soldier);
            walker* orc = sc.bot;
            const Aim aim = order_aim(orc, sc.soldier);
            ASSERT_EQ(1, aim.dx) << "the staging: the order aims RIGHT";
            ASSERT_EQ(0, aim.dy);
            ASSERT_EQ(FACE_RIGHT, orc->curdir()) << "the staging: the orc already faces its foe";
            ASSERT_TRUE(orc->fire_pad_answer() == walker::PadAnswer::Body)
                << "the staging: the soldier's body is on the orc's pad";
            ASSERT_FLOAT_EQ(0.0f, orc->busy()) << "the staging: not busy";
            ASSERT_EQ(ANI_WALK, orc->ani_type());
            if (stale_heading) {
                point_heading_down(orc);
                ASSERT_EQ(FACE_DOWN, orc->facing(static_cast<short>(orc->lastx()), static_cast<short>(orc->lasty())))
                    << "the staging: the heading points DOWN";
                ASSERT_EQ(FACE_RIGHT, orc->curdir()) << "the staging: the orc still faces its foe";
                ASSERT_EQ(FACE_RIGHT, orc->enddir());
                ASSERT_TRUE(orc->fire_pad_answer() == walker::PadAnswer::Clear)
                    << "the staging: the pad along that heading holds nothing";
            }
            hold_attack(orc, com);
            const std::uint32_t waver = waver_bound_of(orc);
            ASSERT_GT(waver, 0u);
            const std::uint32_t s = world().rng_.state_;

            EXPECT_EQ(1, orc->stats()->do_command());

            EXPECT_EQ(state_after(s, {waver}), world().rng_.state_) << "the line check's waver; the round draws nothing else";
            EXPECT_EQ(320, orc->xpos()) << "not moved";
            EXPECT_EQ(320, orc->ypos()) << "not moved";
            EXPECT_EQ(FACE_RIGHT, orc->curdir());
            EXPECT_EQ(FACE_RIGHT, orc->enddir());
            EXPECT_FLOAT_EQ(orc->stepsize(), orc->lastx()) << "the heading points at the foe";
            EXPECT_FLOAT_EQ(0.0f, orc->lasty()) << "the heading points at the foe";
            if (fixes) {
                EXPECT_FLOAT_EQ(orc->fire_frequency(), orc->busy()) << "the swing started: the fire delay is charged";
                EXPECT_EQ(ANI_ATTACK, orc->ani_type()) << "the swing started: the attack animation runs";
            } else {
                EXPECT_FLOAT_EQ(0.0f, orc->busy()) << "the classic round only faces: no swing";
                EXPECT_EQ(ANI_WALK, orc->ani_type()) << "the classic round only faces: no swing";
            }
            const Cmd f = front_of(orc);
            EXPECT_EQ(COMMAND_ATTACK, f.type);
            EXPECT_EQ(kOrder - 1, f.count) << "the order spent one round";
            EXPECT_EQ(com, f.com1);
            EXPECT_EQ(com, f.com2);
            EXPECT_EQ(sc.soldier, orc->foe());
        }
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// The turn of living::act_random's refused shot (living.cpp, through
// statistics::turn_to_foe and statistics::keep_turn, stats.cpp)
// ---------------------------------------------------------------------------

// Gameplay Fixes BE (#513), living.cpp living::act_random through
// statistics::turn_to_foe; both settings. A team-1 orc (melee only) at
// (320,320) facing UP, holding its foe, a team-0 soldier 16 px off its right
// side (the aim is RIGHT, two notches off) and inside its sight box. Asserted
// first: the aim is RIGHT and the orc does not face it; the line check
// refuses (asked in a twin world built the same way, since asking builds a
// test weapon); the orc's path-check counter is 50, more than the rounds of
// chase this scene runs, so no round re-plans (walk_to_foe re-plans when it
// reaches 0, and a re-plan within 100 px of a foe wipes the orders for an
// ATTACK).
//  Act 1 (act_random): the shot is refused and the orc turns a notch toward
//    the soldier, UP to UP_RIGHT, then queues SEARCH(200). The stream is the
//    two 1-in-5s, the 1-in-80 and the probe's waver. Setting on: enddir
//    becomes RIGHT (the turn is kept). Off: enddir stays UP.
//  Act 2: the orc does not face its enddir, so the act is one notch toward
//    it and nothing else: no draw, the SEARCH not run (count 200, counter 50).
//    On: to RIGHT, its foe's direction. Off: back to UP, the facing it had.
//  Act 3: the orc faces its enddir, so the SEARCH runs: walk_to_foe counts
//    the path-check counter down (49) and the order spends a round (199).
// A second start: the orc faces UP_RIGHT, ONE notch off its foe, so act 1's
// notch reaches RIGHT. Setting on: act 1 leaves curdir and enddir RIGHT, on
// the foe, and act 2 runs the SEARCH. Off: act 1 leaves enddir UP_RIGHT, act
// 2 turns the orc back to it with nothing drawn and the SEARCH not run, and
// act 3 runs it.
TEST_F(SemAi5, a_bot_whose_shot_is_refused_keeps_its_turn_toward_the_foe_with_the_setting_on)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(setting(fixes));
        Scene twin = orc_scene(fixes, FACE_UP, 16);
        ASSERT_TRUE(twin.bot && twin.soldier);
        ASSERT_FALSE(twin.bot->fire_check(twin.dx, twin.dy)) << "the staging: the line check refuses";

        Scene sc = orc_scene(fixes, FACE_UP, 16);
        ASSERT_TRUE(sc.bot && sc.soldier);
        walker* orc = sc.bot;
        ASSERT_EQ(FACE_RIGHT, orc->facing(sc.dx, sc.dy)) << "the staging: the aim is RIGHT";
        ASSERT_EQ(FACE_UP, orc->curdir()) << "the staging: the orc faces UP, away from its foe";
        ASSERT_EQ(FACE_UP, orc->enddir());
        ASSERT_LT(std::abs(sc.dx), orc->lineofsight() * GRID_SIZE) << "the staging: the foe is in the sight box";
        ASSERT_GT(orc->path_check_counter(), 3) << "the staging: no round of the chase re-plans";
        ASSERT_TRUE(orc->stats()->commands.empty());
        const std::uint32_t waver = waver_bound_of(orc);
        ASSERT_GT(waver, 0u);
        const std::uint32_t s = into_act_random(waver);
        world().rng_.state_ = s;

        (void)orc->act();

        EXPECT_EQ(state_after(s, {5, 5, 80, waver}), world().rng_.state_) << "act 1: the two 1-in-5s, the 1-in-80, the waver";
        EXPECT_EQ(FACE_UP_RIGHT, orc->curdir()) << "act 1: a notch toward the foe";
        if (fixes) {
            EXPECT_EQ(FACE_RIGHT, orc->enddir()) << "act 1: the foe's direction is kept as the facing it turns to";
        } else {
            EXPECT_EQ(FACE_UP, orc->enddir()) << "act 1: the classic turn leaves enddir as it was";
        }
        Cmd f = front_of(orc);
        EXPECT_EQ(1u, f.size);
        EXPECT_EQ(COMMAND_SEARCH, f.type) << "act 1: the SEARCH is queued";
        EXPECT_EQ(200, f.count);
        EXPECT_EQ(sc.soldier, orc->foe());
        EXPECT_FLOAT_EQ(0.0f, orc->busy()) << "act 1: no swing";
        EXPECT_EQ(ANI_WALK, orc->ani_type()) << "act 1: no swing";
        EXPECT_EQ(320, orc->xpos());
        EXPECT_EQ(320, orc->ypos());

        const std::uint32_t after1 = world().rng_.state_;
        (void)orc->act();

        if (fixes) {
            EXPECT_EQ(FACE_RIGHT, orc->curdir()) << "act 2: the kept turn is finished";
            EXPECT_EQ(FACE_RIGHT, orc->enddir());
        } else {
            EXPECT_EQ(FACE_UP, orc->curdir()) << "act 2: turned back to the facing it had";
            EXPECT_EQ(FACE_UP, orc->enddir());
        }
        EXPECT_EQ(after1, world().rng_.state_) << "act 2: the turn is the act; nothing is drawn";
        f = front_of(orc);
        EXPECT_EQ(COMMAND_SEARCH, f.type);
        EXPECT_EQ(200, f.count) << "act 2: the SEARCH did not run";
        EXPECT_EQ(kCounter, orc->path_check_counter()) << "act 2: no chase";

        (void)orc->act();

        f = front_of(orc);
        EXPECT_EQ(COMMAND_SEARCH, f.type);
        EXPECT_EQ(199, f.count) << "act 3: the SEARCH ran one round";
        EXPECT_EQ(kCounter - 1, orc->path_check_counter()) << "act 3: the SEARCH's chase ran";
    }
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(std::string(setting(fixes)) + ", one notch off");
        Scene twin = orc_scene(fixes, FACE_UP_RIGHT, 16);
        ASSERT_TRUE(twin.bot && twin.soldier);
        ASSERT_FALSE(twin.bot->fire_check(twin.dx, twin.dy)) << "the staging: the line check refuses";

        Scene sc = orc_scene(fixes, FACE_UP_RIGHT, 16);
        ASSERT_TRUE(sc.bot && sc.soldier);
        walker* orc = sc.bot;
        ASSERT_EQ(FACE_RIGHT, orc->facing(sc.dx, sc.dy)) << "the staging: the aim is RIGHT";
        ASSERT_EQ(FACE_UP_RIGHT, orc->curdir()) << "the staging: the orc faces UP_RIGHT, one notch off its foe";
        ASSERT_EQ(FACE_UP_RIGHT, orc->enddir());
        ASSERT_GT(orc->path_check_counter(), 3) << "the staging: no round of the chase re-plans";
        const std::uint32_t waver = waver_bound_of(orc);
        ASSERT_GT(waver, 0u);
        const std::uint32_t s = into_act_random(waver);
        world().rng_.state_ = s;

        (void)orc->act();

        EXPECT_EQ(state_after(s, {5, 5, 80, waver}), world().rng_.state_) << "act 1: the two 1-in-5s, the 1-in-80, the waver";
        EXPECT_EQ(FACE_RIGHT, orc->curdir()) << "act 1: the notch reaches the foe's direction";
        EXPECT_EQ(fixes ? FACE_RIGHT : FACE_UP_RIGHT, orc->enddir())
            << (fixes ? "act 1: the foe's direction is kept as the facing it turns to" : "act 1: the classic turn leaves enddir as it was");
        EXPECT_EQ(COMMAND_SEARCH, front_of(orc).type) << "act 1: the SEARCH is queued";
        EXPECT_EQ(200, front_of(orc).count);

        if (!fixes) {
            const std::uint32_t after1 = world().rng_.state_;
            (void)orc->act();
            EXPECT_EQ(FACE_UP_RIGHT, orc->curdir()) << "act 2: turned back to the facing it had";
            EXPECT_EQ(after1, world().rng_.state_) << "act 2: the turn is the act; nothing is drawn";
            EXPECT_EQ(200, front_of(orc).count) << "act 2: the SEARCH did not run";
            EXPECT_EQ(kCounter, orc->path_check_counter()) << "act 2: no chase";
        }

        (void)orc->act();

        EXPECT_EQ(COMMAND_SEARCH, front_of(orc).type);
        EXPECT_EQ(199, front_of(orc).count) << (fixes ? "act 2 runs the SEARCH" : "act 3 runs the SEARCH");
        EXPECT_EQ(kCounter - 1, orc->path_check_counter()) << "the SEARCH's chase ran";
    }
}

// Gameplay Fixes BE (#513), the same scene with the orc facing LEFT: the aim
// (RIGHT) is four notches off, and a half turn goes clockwise
// (walker_movement.cpp walker::turn). Both settings.
//  Setting on: act 1 (act_random) turns LEFT to UP_LEFT and keeps RIGHT as
//    enddir; acts 2, 3 and 4 finish the turn, UP, UP_RIGHT, RIGHT, one notch
//    an act with nothing drawn and the SEARCH not run (count 200, counter
//    50); act 5 faces RIGHT and runs it (199, counter 49).
//  Setting off: act 1 turns LEFT to UP_LEFT; act 2 turns it back to LEFT,
//    the facing it had; act 3 runs the SEARCH (199, counter 49).
TEST_F(SemAi5, a_half_turn_takes_four_acts_and_then_the_search_runs)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(setting(fixes));
        Scene twin = orc_scene(fixes, FACE_LEFT, 16);
        ASSERT_TRUE(twin.bot && twin.soldier);
        ASSERT_FALSE(twin.bot->fire_check(twin.dx, twin.dy)) << "the staging: the line check refuses";

        Scene sc = orc_scene(fixes, FACE_LEFT, 16);
        ASSERT_TRUE(sc.bot && sc.soldier);
        walker* orc = sc.bot;
        ASSERT_EQ(FACE_RIGHT, orc->facing(sc.dx, sc.dy)) << "the staging: the aim is RIGHT";
        ASSERT_EQ(FACE_LEFT, orc->curdir()) << "the staging: the orc faces LEFT, four notches off";
        ASSERT_GT(orc->path_check_counter(), 5) << "the staging: no round of the chase re-plans";
        const std::uint32_t waver = waver_bound_of(orc);
        ASSERT_GT(waver, 0u);
        const std::uint32_t s = into_act_random(waver);
        world().rng_.state_ = s;

        (void)orc->act();

        EXPECT_EQ(state_after(s, {5, 5, 80, waver}), world().rng_.state_) << "act 1: the two 1-in-5s, the 1-in-80, the waver";
        EXPECT_EQ(FACE_UP_LEFT, orc->curdir()) << "act 1: a half turn starts clockwise";
        EXPECT_EQ(fixes ? FACE_RIGHT : FACE_LEFT, orc->enddir());
        EXPECT_EQ(COMMAND_SEARCH, front_of(orc).type) << "act 1: the SEARCH is queued";
        EXPECT_EQ(200, front_of(orc).count);

        const std::uint32_t after1 = world().rng_.state_;
        constexpr int kKeptTurn[3] = {FACE_UP, FACE_UP_RIGHT, FACE_RIGHT};
        const int acts_turning = fixes ? 3 : 1;
        for (int a = 0; a < acts_turning; ++a) {
            SCOPED_TRACE("act " + std::to_string(a + 2));
            (void)orc->act();
            const int want = fixes ? kKeptTurn[a] : FACE_LEFT;
            EXPECT_EQ(want, orc->curdir()) << (fixes ? "one notch of the kept turn" : "turned back to the facing it had");
            EXPECT_EQ(after1, world().rng_.state_) << "the turn is the act; nothing is drawn";
            EXPECT_EQ(COMMAND_SEARCH, front_of(orc).type);
            EXPECT_EQ(200, front_of(orc).count) << "the SEARCH did not run";
            EXPECT_EQ(kCounter, orc->path_check_counter()) << "no chase";
        }

        SCOPED_TRACE(fixes ? "act 5" : "act 3");
        (void)orc->act();

        EXPECT_EQ(COMMAND_SEARCH, front_of(orc).type);
        EXPECT_EQ(199, front_of(orc).count) << "the first act that faces its enddir runs the SEARCH";
        EXPECT_EQ(kCounter - 1, orc->path_check_counter()) << "the SEARCH's chase ran";
    }
}

// Gameplay Fixes BE (#513), a control: a bot that already faces its foe.
// Both settings. The orc faces RIGHT with the soldier 1 px off its right side,
// inside its weapon's reach, so the line check refuses it for being melee
// only (NoRanged, asserted in a twin world). act_random's turn is asked for
// the facing the orc has.
//  Setting on: nothing changes (Gameplay Fixes AT: a walker asked to face the
//    way it faces stays put): act 1 leaves curdir and enddir RIGHT and queues
//    SEARCH(200); act 2 runs the SEARCH (199, counter 49).
//  Setting off: act 1 turns the orc the classic notch counter-clockwise, RIGHT
//    to UP_RIGHT, enddir left RIGHT; act 2 turns it back to RIGHT with nothing
//    drawn and the SEARCH not run; act 3 runs it (199, counter 49).
// Asking keep_turn for a bot that faces its foe would change nothing this
// test can read; what the "not faced" term of turn_to_foe decides shows in
// the near branch's swing, held by
// SemAi4.a_chaser_facing_its_foe_within_100_px_swings_only_with_the_setting_on.
TEST_F(SemAi5, a_bot_already_facing_its_foe_is_left_as_it_is)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(setting(fixes));
        Scene twin = orc_scene(fixes, FACE_RIGHT, 1);
        ASSERT_TRUE(twin.bot && twin.soldier);
        walker::FireCheckDenial why = walker::FireCheckDenial::None;
        ASSERT_FALSE(twin.bot->fire_check(twin.dx, twin.dy, &why)) << "the staging: the line check refuses";
        ASSERT_TRUE(why == walker::FireCheckDenial::NoRanged) << "the staging: refused as melee only, the foe in reach";

        Scene sc = orc_scene(fixes, FACE_RIGHT, 1);
        ASSERT_TRUE(sc.bot && sc.soldier);
        walker* orc = sc.bot;
        ASSERT_EQ(FACE_RIGHT, orc->facing(sc.dx, sc.dy)) << "the staging: the aim is RIGHT";
        ASSERT_EQ(FACE_RIGHT, orc->curdir()) << "the staging: the orc faces its foe";
        const std::uint32_t waver = waver_bound_of(orc);
        ASSERT_GT(waver, 0u);
        const std::uint32_t s = into_act_random(waver);
        world().rng_.state_ = s;

        (void)orc->act();

        EXPECT_EQ(state_after(s, {5, 5, 80, waver}), world().rng_.state_) << "act 1: the two 1-in-5s, the 1-in-80, the waver";
        EXPECT_EQ(fixes ? FACE_RIGHT : FACE_UP_RIGHT, orc->curdir())
            << (fixes ? "act 1: asked for the facing it has, it stays" : "act 1: the classic notch counter-clockwise");
        EXPECT_EQ(FACE_RIGHT, orc->enddir()) << "act 1: enddir is left as it was";
        EXPECT_EQ(COMMAND_SEARCH, front_of(orc).type) << "act 1: the SEARCH is queued";
        EXPECT_EQ(200, front_of(orc).count);

        if (!fixes) {
            const std::uint32_t after1 = world().rng_.state_;
            (void)orc->act();
            EXPECT_EQ(FACE_RIGHT, orc->curdir()) << "act 2: turned back";
            EXPECT_EQ(after1, world().rng_.state_) << "act 2: the turn is the act; nothing is drawn";
            EXPECT_EQ(200, front_of(orc).count) << "act 2: the SEARCH did not run";
            EXPECT_EQ(kCounter, orc->path_check_counter());
        }

        (void)orc->act();

        EXPECT_EQ(COMMAND_SEARCH, front_of(orc).type);
        EXPECT_EQ(199, front_of(orc).count) << (fixes ? "act 2 runs the SEARCH" : "act 3 runs the SEARCH");
        EXPECT_EQ(kCounter - 1, orc->path_check_counter()) << "the SEARCH's chase ran";
    }
}

// living.cpp living::act_random, a living whose stats have lost their owner
// (the stats' controller): statistics::turn_to_foe turns the stats' owner,
// so such a living turns itself the classic notch when its shot is refused,
// on both settings, as the statement before Gameplay Fixes BE did. The game
// keeps every live walker's stats owned by it (statistics::do_command
// guards the same state). The orc of orc_scene facing UP, its foe 16 px off
// its right side (the aim RIGHT, two notches off); act_random reached
// through act() as the tests above reach it, its stream the two 1-in-5s, the
// 1-in-80 and the probe's waver. First the orc's stats name another walker,
// a team-1 soldier standing apart facing DOWN; then they name nobody. Both:
// the orc turns UP to UP_RIGHT, its heading one stepsize along UP_RIGHT,
// enddir left UP; a SEARCH(200) is queued and nothing is fired; the other
// walker is not turned; and nothing reads the missing owner (no crash).
TEST_F(SemAi5, a_living_whose_stats_have_lost_their_owner_turns_the_classic_notch_on_both_settings)
{
    for (short fixes : {short{0}, short{1}}) {
        for (bool other_owner : {true, false}) {
            SCOPED_TRACE(std::string(setting(fixes)) +
                         (other_owner ? ", the stats name another walker" : ", the stats name nobody"));
            Scene twin = orc_scene(fixes, FACE_UP, 16);
            ASSERT_TRUE(twin.bot && twin.soldier);
            ASSERT_FALSE(twin.bot->fire_check(twin.dx, twin.dy)) << "the staging: the line check refuses";

            Scene sc = orc_scene(fixes, FACE_UP, 16);
            ASSERT_TRUE(sc.bot && sc.soldier);
            walker* orc = sc.bot;
            walker* other = living_at(FAMILY_SOLDIER, 1, 600, 600);
            ASSERT_NE(nullptr, other);
            face(other, FACE_DOWN);
            ASSERT_EQ(orc, orc->stats()->controller()) << "the staging: the game gives a walker's stats their owner";
            walker* const owner = other_owner ? other : nullptr;
            orc->stats()->set_controller(owner);
            ASSERT_EQ(owner, orc->stats()->controller()) << "the staging: the orc's stats have lost their owner";
            ASSERT_EQ(FACE_RIGHT, orc->facing(sc.dx, sc.dy)) << "the staging: the aim is RIGHT";
            ASSERT_EQ(FACE_UP, orc->curdir()) << "the staging: the orc faces UP, away from its foe";
            ASSERT_EQ(FACE_UP, orc->enddir());
            ASSERT_TRUE(orc->stats()->commands.empty()) << "the staging: no order runs before the think";
            const std::uint32_t waver = waver_bound_of(orc);
            ASSERT_GT(waver, 0u);
            const std::uint32_t s = state_where({{5, kNonZero}, {5, 0}, {80, kNonZero}, {waver, kAny}});
            world().rng_.state_ = s;

            (void)orc->act();

            EXPECT_EQ(state_after(s, {5, 5, 80, waver}), world().rng_.state_) << "the two 1-in-5s, the 1-in-80, the waver";
            EXPECT_EQ(FACE_UP_RIGHT, orc->curdir()) << "the classic notch toward the foe";
            EXPECT_EQ(FACE_UP, orc->enddir()) << "the classic turn leaves enddir as it was";
            EXPECT_FLOAT_EQ(orc->stepsize(), orc->lastx()) << "the heading follows the new facing";
            EXPECT_FLOAT_EQ(-orc->stepsize(), orc->lasty()) << "the heading follows the new facing";
            const Cmd f = front_of(orc);
            EXPECT_EQ(1u, f.size);
            EXPECT_EQ(COMMAND_SEARCH, f.type) << "the SEARCH is queued";
            EXPECT_EQ(200, f.count);
            EXPECT_EQ(0, live_weapons()) << "nothing fired";
            EXPECT_EQ(320, orc->xpos());
            EXPECT_EQ(320, orc->ypos());
            EXPECT_EQ(FACE_DOWN, other->curdir()) << "the other walker is not turned";
            EXPECT_EQ(FACE_DOWN, other->enddir()) << "the other walker is not turned";
            EXPECT_FLOAT_EQ(0.0f, other->lastx()) << "the other walker's heading is not touched";
            EXPECT_FLOAT_EQ(other->stepsize(), other->lasty()) << "the other walker's heading is not touched";
            orc->stats()->set_controller(orc);
        }
    }
}

// ---------------------------------------------------------------------------
// A tower (stats.cpp statistics::keep_turn's stationary arm)
// ---------------------------------------------------------------------------

// Gameplay Fixes BE (#513), living::act_random's refused shot for a tower,
// through statistics::keep_turn's stationary arm; both settings. The tower of
// tower_scene. Asserted first: the tower is stationary (its family's
// is_stationary) and the soldier is not, nor is the bone wall, a weapon that
// shares the tower's family number (walker::stationary asks only a living);
// the tower's heading points UP (stepsize 0: a unit step); the aim is RIGHT;
// the line check refuses for the facing alone (Facing: the reach and magic
// gates come first, so the soldier is in reach), asked in a twin world.
// act_random is reached through act() as the orc's is, and its stream is the
// two 1-in-5s, the 1-in-80 and the waver of the tower's arrow.
//  Setting on: the tower faces its foe at once: curdir and enddir RIGHT and
//    its heading along RIGHT (face_delta's unit step); its line check, asked
//    again, passes, since the heading it flies along now points at the
//    soldier.
//  Setting off: the classic notch, UP to UP_RIGHT, enddir left UP, and the
//    heading left as it was (walker::turn does not move a stationary
//    living's heading); the line check, asked again, refuses (Facing).
// Both: a SEARCH(200) is queued, nothing is fired, the tower does not move.
TEST_F(SemAi5, a_tower_whose_shot_is_refused_faces_its_foe_at_once_with_its_heading)
{
    {
        fresh_world();
        walker* tower = living_at(FAMILY_TOWER1, 1, 320, 320);
        walker* soldier = living_at(FAMILY_SOLDIER, 0, 400, 320);
        walker* bone_wall = world().add_weap_ob(Order::Weapon, FAMILY_TOWER1);
        ASSERT_TRUE(tower && soldier && bone_wall);
        const FamilyDescriptor* fd = get_family_descriptor(FAMILY_TOWER1);
        ASSERT_TRUE(fd != nullptr && fd->is_stationary) << "the staging: the tower's family never moves";
        ASSERT_TRUE(tower->stationary()) << "the staging: the tower is stationary";
        ASSERT_FALSE(soldier->stationary()) << "the staging: the soldier is not";
        ASSERT_EQ(FAMILY_TOWER1, static_cast<int>(bone_wall->family())) << "the staging: the bone wall shares the tower's number";
        ASSERT_FALSE(bone_wall->stationary()) << "only a living's family is asked";
    }
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(setting(fixes));
        Scene twin = tower_scene(fixes);
        ASSERT_TRUE(twin.bot && twin.soldier);
        walker::FireCheckDenial why = walker::FireCheckDenial::None;
        ASSERT_FALSE(twin.bot->fire_check(twin.dx, twin.dy, &why)) << "the staging: the line check refuses";
        ASSERT_TRUE(why == walker::FireCheckDenial::Facing) << "the staging: refused for the facing alone";

        Scene sc = tower_scene(fixes);
        ASSERT_TRUE(sc.bot && sc.soldier);
        walker* tower = sc.bot;
        ASSERT_FLOAT_EQ(0.0f, tower->stepsize()) << "the staging: a tower never steps";
        ASSERT_FLOAT_EQ(0.0f, tower->lastx()) << "the staging: its heading points UP";
        ASSERT_FLOAT_EQ(-1.0f, tower->lasty()) << "the staging: its heading points UP";
        ASSERT_EQ(FACE_UP, tower->curdir());
        ASSERT_EQ(FACE_UP, tower->enddir());
        ASSERT_EQ(FACE_RIGHT, tower->facing(sc.dx, sc.dy)) << "the staging: the aim is RIGHT";
        ASSERT_LT(std::abs(sc.dx), tower->lineofsight() * GRID_SIZE) << "the staging: the foe is in the sight box";
        ASSERT_LT(std::abs(sc.dy), tower->lineofsight() * GRID_SIZE);
        const std::uint32_t waver = waver_bound_of(tower);
        ASSERT_GT(waver, 0u);
        const std::uint32_t s = state_where({{5, kNonZero}, {5, 0}, {80, kNonZero}, {waver, kAny}});
        world().rng_.state_ = s;

        (void)tower->act();

        EXPECT_EQ(state_after(s, {5, 5, 80, waver}), world().rng_.state_) << "the two 1-in-5s, the 1-in-80, the waver";
        if (fixes) {
            EXPECT_EQ(FACE_RIGHT, tower->curdir()) << "the tower faces its foe at once";
            EXPECT_EQ(FACE_RIGHT, tower->enddir());
            EXPECT_EQ(FACE_RIGHT, tower->facing(tower->lastx(), tower->lasty())) << "its heading points at its foe";
            EXPECT_FLOAT_EQ(1.0f, tower->lastx()) << "face_delta's unit step";
            EXPECT_FLOAT_EQ(0.0f, tower->lasty());
        } else {
            EXPECT_EQ(FACE_UP_RIGHT, tower->curdir()) << "the classic notch";
            EXPECT_EQ(FACE_UP, tower->enddir()) << "the classic turn leaves enddir as it was";
            EXPECT_FLOAT_EQ(0.0f, tower->lastx()) << "a stationary living's heading stays where it was";
            EXPECT_FLOAT_EQ(-1.0f, tower->lasty());
        }
        const Cmd f = front_of(tower);
        EXPECT_EQ(1u, f.size);
        EXPECT_EQ(COMMAND_SEARCH, f.type) << "the SEARCH is queued";
        EXPECT_EQ(200, f.count);
        EXPECT_FLOAT_EQ(0.0f, tower->busy()) << "nothing fired";
        EXPECT_EQ(ANI_WALK, tower->ani_type()) << "nothing fired";
        EXPECT_EQ(0, live_weapons()) << "nothing fired";
        EXPECT_EQ(320, tower->xpos());
        EXPECT_EQ(320, tower->ypos());

        // This second check's waver is not pinned: the staging lands for any sideways waver.
        why = walker::FireCheckDenial::None;
        const bool passes = tower->fire_check(sc.dx, sc.dy, &why);
        if (fixes) {
            EXPECT_TRUE(passes) << "the line check, asked again, passes along the new heading";
            EXPECT_TRUE(why == walker::FireCheckDenial::None);
        } else {
            EXPECT_FALSE(passes) << "the line check, asked again, still refuses";
            EXPECT_TRUE(why == walker::FireCheckDenial::Facing);
        }
    }
}

// Gameplay Fixes BB (#497) amended by BE (#513): the near branch of
// statistics::walk_to_foe for a tower; both settings. The tower of
// tower_scene (its foe 16 px off its right side, within the branch's 100 px),
// a SEARCH(9) queued, re-plan due (path-check counter 0). The branch wipes the
// order and queues ATTACK(30 + rng(25), 1, 1) (the draws: rng(300), the
// cadence rng(10), the duration pinned to 7: 37), and the tower keeps its
// foe and does not move.
//  Setting on: turn_to_foe finds the tower not facing its foe and keeps the
//    turn, which for a tower is facing it at once: curdir and enddir RIGHT
//    and the heading along RIGHT. The turn is the act: nothing is fired on
//    it (no line check, no draw beyond the three, no weapon, busy 0).
//  Setting off: the classic notch, UP to UP_RIGHT, with the heading left UP;
//    init_fire() along that heading finds the facing off it and only turns
//    the tower back to UP: nothing is fired.
TEST_F(SemAi5, a_tower_turning_toward_its_near_foe_faces_it_at_once_with_the_setting_on)
{
    const std::uint32_t s = state_where({{300, kNonZero}, {10, kAny}, {25, 7}});
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(setting(fixes));
        Scene sc = tower_scene(fixes);
        ASSERT_TRUE(sc.bot && sc.soldier);
        walker* tower = sc.bot;
        ASSERT_TRUE(tower->stationary()) << "the staging: the tower is stationary";
        ASSERT_EQ(FACE_RIGHT, tower->facing(sc.dx, sc.dy)) << "the staging: the aim is RIGHT";
        ASSERT_EQ(FACE_UP, tower->curdir()) << "the staging: the tower faces UP";
        ASSERT_LT(static_cast<std::uint32_t>(tower->distance_to_ob(sc.soldier)), 100u)
            << "the staging: the foe is within the near branch's 100 px";
        tower->stats()->add_command(COMMAND_SEARCH, 9, 0, 0);
        tower->set_path_check_counter(0);
        world().rng_.state_ = s;

        EXPECT_TRUE(tower->stats()->walk_to_foe());

        const Cmd f = front_of(tower);
        EXPECT_EQ(1u, f.size) << "the SEARCH was wiped";
        EXPECT_EQ(COMMAND_ATTACK, f.type);
        EXPECT_EQ(37, f.count) << "30 + rng(25), the draw pinned to 7";
        EXPECT_EQ(1, f.com1);
        EXPECT_EQ(1, f.com2);
        EXPECT_EQ(state_after(s, {300, 10, 25}), world().rng_.state_) << "not facing its foe: nothing asked";
        EXPECT_EQ(sc.soldier, tower->foe());
        EXPECT_EQ(320, tower->xpos());
        EXPECT_EQ(320, tower->ypos());
        EXPECT_FLOAT_EQ(0.0f, tower->busy()) << "nothing fired";
        EXPECT_EQ(ANI_WALK, tower->ani_type()) << "nothing fired";
        EXPECT_EQ(0, static_cast<int>(world().weaplist.size())) << "no weapon was built";
        if (fixes) {
            EXPECT_EQ(FACE_RIGHT, tower->curdir()) << "the tower faces its foe at once";
            EXPECT_EQ(FACE_RIGHT, tower->enddir());
            EXPECT_EQ(FACE_RIGHT, tower->facing(tower->lastx(), tower->lasty())) << "its heading points at its foe";
        } else {
            EXPECT_EQ(FACE_UP, tower->curdir()) << "a notch toward the foe, then init_fire turns it back";
            EXPECT_EQ(FACE_UP, tower->enddir());
            EXPECT_FLOAT_EQ(0.0f, tower->lastx()) << "the heading stays UP";
            EXPECT_FLOAT_EQ(-1.0f, tower->lasty());
        }
    }
}

// ---------------------------------------------------------------------------
// A melee-only bot's ATTACK order (stats.cpp COMMAND_ATTACK, through
// statistics::attack_face_or_fight and statistics::foe_on_pad)
// ---------------------------------------------------------------------------

// Gameplay Fixes BC (#447), stats.cpp COMMAND_ATTACK through
// statistics::attack_face_or_fight; both settings. A team-1 orc (melee only)
// at (320,320) facing RIGHT, holding its foe, a team-0 soldier 1 px off its
// right side, their boxes level; the orc holds the ATTACK order the near
// branch queues (30 rounds, 1, 1) and is not busy. Asserted first: the order
// aims RIGHT and the orc already faces that way; the soldier's body stands on
// the orc's pad (fire_pad_answer: quiet, no draw); the line check refuses the
// orc as melee only (NoRanged, asked in a twin world). One round:
//  Setting on: the orc swings: busy is charged its fire delay and the attack
//    animation runs; it does not move.
//  Setting off: it only faces its foe (the guard-standoff fix): busy 0, the
//    walk animation, not moved.
// Both: the stream after is the line check's waver alone (the decision draws
// nothing) and the order has spent one round.
TEST_F(SemAi5, a_faced_melee_only_attacker_swings_at_a_body_on_its_pad_with_the_setting_on)
{
    faced_swing_round(1);
}

// Gameplay Fixes BC (#447), statistics::attack_face_or_fight's step; both
// settings. The orc of the test above with the soldier two tiles (32 px) off
// its right side, inside the rock's reach, so its pad is clear (asserted, with
// the NoRanged refusal). One round:
//  Setting on: the orc takes one step toward its foe: moved exactly its
//    stepsize along the order's aim (RIGHT), busy 0, the walk animation.
//  Setting off: it only faces: not moved.
// Both: only the line check's waver is drawn.
TEST_F(SemAi5, a_faced_melee_only_attacker_steps_toward_a_foe_off_its_pad_with_the_setting_on)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(setting(fixes));
        const short gap = 2 * GRID_SIZE;
        Scene twin = orc_scene(fixes, FACE_RIGHT, gap);
        ASSERT_TRUE(twin.bot && twin.soldier);
        ASSERT_TRUE(refusal(twin.bot, 1, 0) == walker::FireCheckDenial::NoRanged)
            << "the staging: refused as melee only, the foe in reach";

        Scene sc = orc_scene(fixes, FACE_RIGHT, gap);
        ASSERT_TRUE(sc.bot && sc.soldier);
        walker* orc = sc.bot;
        const Aim aim = order_aim(orc, sc.soldier);
        ASSERT_EQ(1, aim.dx) << "the staging: the order aims RIGHT";
        ASSERT_EQ(0, aim.dy);
        ASSERT_EQ(FACE_RIGHT, orc->curdir()) << "the staging: the orc already faces its foe";
        ASSERT_TRUE(orc->fire_pad_answer() == walker::PadAnswer::Clear) << "the staging: the orc's pad is clear";
        const int step = static_cast<int>(orc->stepsize());
        ASSERT_GT(step, 0);
        ASSERT_FLOAT_EQ(static_cast<float>(step), orc->stepsize()) << "the staging: a whole step";
        hold_attack(orc);
        const std::uint32_t waver = waver_bound_of(orc);
        ASSERT_GT(waver, 0u);
        const std::uint32_t s = world().rng_.state_;

        EXPECT_EQ(1, orc->stats()->do_command());

        EXPECT_EQ(state_after(s, {waver}), world().rng_.state_) << "the line check's waver; the round draws nothing else";
        if (fixes) {
            EXPECT_EQ(320 + aim.dx * step, orc->xpos()) << "one step toward the foe";
            EXPECT_EQ(320 + aim.dy * step, orc->ypos()) << "one step toward the foe";
        } else {
            EXPECT_EQ(320, orc->xpos()) << "the classic round only faces: not moved";
            EXPECT_EQ(320, orc->ypos()) << "the classic round only faces: not moved";
        }
        EXPECT_EQ(FACE_RIGHT, orc->curdir());
        EXPECT_FLOAT_EQ(0.0f, orc->busy()) << "no swing";
        EXPECT_EQ(ANI_WALK, orc->ani_type()) << "no swing";
        EXPECT_EQ(kOrder - 1, front_of(orc).count) << "the order spent one round";
    }
}

// Gameplay Fixes BC (#447), the "already faced" term of
// statistics::attack_face_or_fight; both settings. The orc faces UP, the
// soldier touching its right side (no gap), boxes level, so the order aims
// RIGHT, two notches off its facing; refused NoRanged (twin world).
//  Round 1, both settings: the orc snaps to face its foe (curdir and enddir
//    RIGHT) and that is the round: no step, no swing. Its pad then holds the
//    soldier's body (asserted).
//  Round 2: setting on, the swing starts (busy charged, attack animation);
//    off, it only faces again. Neither round moves the orc.
TEST_F(SemAi5, a_melee_only_attacker_that_must_turn_only_turns_on_both_settings)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(setting(fixes));
        Scene twin = orc_scene(fixes, FACE_UP, 0);
        ASSERT_TRUE(twin.bot && twin.soldier);
        ASSERT_TRUE(refusal(twin.bot, 1, 0) == walker::FireCheckDenial::NoRanged)
            << "the staging: refused as melee only, the foe in reach";

        Scene sc = orc_scene(fixes, FACE_UP, 0);
        ASSERT_TRUE(sc.bot && sc.soldier);
        walker* orc = sc.bot;
        const Aim aim = order_aim(orc, sc.soldier);
        ASSERT_EQ(1, aim.dx) << "the staging: the order aims RIGHT";
        ASSERT_EQ(0, aim.dy);
        ASSERT_EQ(FACE_UP, orc->curdir()) << "the staging: the orc faces UP, off its foe";
        hold_attack(orc);
        const std::uint32_t waver = waver_bound_of(orc);
        ASSERT_GT(waver, 0u);
        std::uint32_t s = world().rng_.state_;

        EXPECT_EQ(1, orc->stats()->do_command());

        EXPECT_EQ(state_after(s, {waver}), world().rng_.state_) << "round 1: the waver alone";
        EXPECT_EQ(FACE_RIGHT, orc->curdir()) << "round 1: it faces its foe";
        EXPECT_EQ(FACE_RIGHT, orc->enddir());
        EXPECT_EQ(320, orc->xpos()) << "round 1: the turn is the round: no step";
        EXPECT_EQ(320, orc->ypos());
        EXPECT_FLOAT_EQ(0.0f, orc->busy()) << "round 1: the turn is the round: no swing";
        EXPECT_EQ(ANI_WALK, orc->ani_type()) << "round 1: the turn is the round: no swing";
        ASSERT_TRUE(orc->fire_pad_answer() == walker::PadAnswer::Body)
            << "the staging: facing its foe, the orc has the soldier's body on its pad";

        s = world().rng_.state_;
        EXPECT_EQ(1, orc->stats()->do_command());

        EXPECT_EQ(state_after(s, {waver}), world().rng_.state_) << "round 2: the waver alone";
        EXPECT_EQ(320, orc->xpos()) << "round 2: not moved";
        EXPECT_EQ(320, orc->ypos());
        EXPECT_EQ(FACE_RIGHT, orc->curdir());
        if (fixes) {
            EXPECT_FLOAT_EQ(orc->fire_frequency(), orc->busy()) << "round 2: the swing started";
            EXPECT_EQ(ANI_ATTACK, orc->ani_type()) << "round 2: the swing started";
        } else {
            EXPECT_FLOAT_EQ(0.0f, orc->busy()) << "round 2: the classic round only faces";
            EXPECT_EQ(ANI_WALK, orc->ani_type()) << "round 2: the classic round only faces";
        }
        EXPECT_EQ(kOrder - 2, front_of(orc).count) << "two rounds spent";
    }
}

// Gameplay Fixes BC (#447) and init_fire's busy gate (walker.cpp); the setting
// on only (off, the round only faces, as the swing test above shows). The
// scene of a_faced_melee_only_attacker_swings_at_a_body_on_its_pad_with_the_setting_on
// with the orc busy 3: the order's round neither starts a swing (busy stays 3,
// the walk animation) nor steps (not moved); the round itself drains nothing
// (busy drains in living::act_round, not in the order).
TEST_F(SemAi5, a_busy_melee_only_attacker_neither_swings_nor_steps)
{
    Scene twin = orc_scene(1, FACE_RIGHT, 1);
    ASSERT_TRUE(twin.bot && twin.soldier);
    ASSERT_TRUE(refusal(twin.bot, 1, 0) == walker::FireCheckDenial::NoRanged)
        << "the staging: refused as melee only, the foe in reach";

    Scene sc = orc_scene(1, FACE_RIGHT, 1);
    ASSERT_TRUE(sc.bot && sc.soldier);
    walker* orc = sc.bot;
    ASSERT_EQ(FACE_RIGHT, orc->curdir()) << "the staging: the orc already faces its foe";
    ASSERT_TRUE(orc->fire_pad_answer() == walker::PadAnswer::Body) << "the staging: the soldier's body is on the orc's pad";
    orc->set_busy(3.0f);
    hold_attack(orc);
    const std::uint32_t waver = waver_bound_of(orc);
    ASSERT_GT(waver, 0u);
    const std::uint32_t s = world().rng_.state_;

    EXPECT_EQ(1, orc->stats()->do_command());

    EXPECT_EQ(state_after(s, {waver}), world().rng_.state_) << "the waver alone";
    EXPECT_FLOAT_EQ(3.0f, orc->busy()) << "busy: no swing started";
    EXPECT_EQ(ANI_WALK, orc->ani_type()) << "busy: no swing started";
    EXPECT_EQ(320, orc->xpos()) << "a body on the pad is never stepped at";
    EXPECT_EQ(320, orc->ypos());
    EXPECT_EQ(kOrder - 1, front_of(orc).count);
}

// Gameplay Fixes BC (#447), statistics::foe_on_pad's "no" for a wall and for
// an ally; both settings. The orc faces RIGHT with its foe beyond the thing on
// its pad, inside the rock's reach (NoRanged, twin world).
//  A wall: the orc stands with its right edge on a tile boundary and the
//    column of tiles beyond it walled over its rows, so its pad answers
//    Terrain; there is no pixel of room, so walkstep's one-unit step is
//    refused by the wall too and its table's sidestep for RIGHT, one stepsize
//    UP, is the step that moves (that spot is open: asserted). On: no swing,
//    moved one stepsize up, still facing RIGHT. Off: not moved.
//  An ally: a team-1 orc 1 px off the orc's right side, on its pad, which
//    answers Clear (an own-team body passes a weapon probe); the full step's
//    box meets the ally and the one-unit step's does not (asserted). The
//    stream is pinned so the shove's 2-in-3 roll shoves. On: no swing; the
//    refused full step shoves the ally, which holds the WALK order
//    living::shove gives it (4 rounds along the step, which
//    statistics::force_command clamps to the unit (1, 0) as it does every
//    WALK order), and the one-unit step moves the orc 1 px; the stream is
//    the waver and the shove's rng(3). Off: not moved, the ally holds no
//    order, the waver alone.
TEST_F(SemAi5, a_wall_or_an_ally_on_the_pad_is_stepped_at_not_swung_at)
{
    const auto wall_scene = [this](short fixes, int* wall_tx) {
        Scene sc;
        fresh_world(fixes);
        sc.bot = living_at(FAMILY_ORC, 1, 320, 320);
        if (sc.bot == nullptr)
            return sc;
        const short ox = static_cast<short>(21 * GRID_SIZE - sc.bot->sizex());
        place(sc.bot, ox, 320);
        *wall_tx = (ox + sc.bot->sizex()) / GRID_SIZE;
        for (int ty = 320 / GRID_SIZE; ty <= (320 + sc.bot->sizey() - 1) / GRID_SIZE; ++ty)
            tile(*wall_tx, ty, PIX_H_WALL1);
        sc.soldier = living_at(FAMILY_SOLDIER, 0, static_cast<short>((*wall_tx + 1) * GRID_SIZE), 320);
        if (sc.soldier == nullptr)
            return sc;
        face(sc.bot, FACE_RIGHT);
        sc.bot->set_foe(sc.soldier);
        return sc;
    };
    const auto ally_scene = [this](short fixes, walker** ally) {
        Scene sc;
        fresh_world(fixes);
        sc.bot = living_at(FAMILY_ORC, 1, 320, 320);
        if (sc.bot == nullptr)
            return sc;
        *ally = living_at(FAMILY_ORC, 1, static_cast<short>(320 + sc.bot->sizex() + 1), 320);
        if (*ally == nullptr)
            return sc;
        sc.soldier = living_at(FAMILY_SOLDIER, 0, static_cast<short>((*ally)->xpos() + (*ally)->sizex() + 1), 320);
        if (sc.soldier == nullptr)
            return sc;
        face(sc.bot, FACE_RIGHT);
        sc.bot->set_foe(sc.soldier);
        return sc;
    };
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(std::string(setting(fixes)) + ", a wall on the pad");
        int wall_tx = -1;
        Scene twin = wall_scene(fixes, &wall_tx);
        ASSERT_TRUE(twin.bot && twin.soldier);
        ASSERT_TRUE(refusal(twin.bot, 1, 0) == walker::FireCheckDenial::NoRanged)
            << "the staging: refused as melee only, the foe in reach";

        Scene sc = wall_scene(fixes, &wall_tx);
        ASSERT_TRUE(sc.bot && sc.soldier);
        walker* orc = sc.bot;
        const short ox = orc->xpos();
        const short oy = orc->ypos();
        const int step = static_cast<int>(orc->stepsize());
        ASSERT_GT(step, 0);
        const Aim aim = order_aim(orc, sc.soldier);
        ASSERT_EQ(1, aim.dx) << "the staging: the order aims RIGHT";
        ASSERT_EQ(0, aim.dy);
        ASSERT_EQ(FACE_RIGHT, orc->curdir()) << "the staging: the orc already faces its foe";
        ASSERT_TRUE(orc->fire_pad_answer() == walker::PadAnswer::Terrain) << "the staging: a wall on the orc's pad";
        ASSERT_EQ(wall_tx * GRID_SIZE, ox + orc->sizex()) << "the staging: no pixel of room before the wall";
        ASSERT_FALSE(world().query_grid_passable(static_cast<float>(ox + 1), static_cast<float>(oy), orc))
            << "the staging: the one-unit step meets the wall";
        ASSERT_TRUE(world().query_passable(static_cast<float>(ox), static_cast<float>(oy - step), orc, orc->floor(), true))
            << "the staging: the sidestep's spot, one stepsize UP, is open";
        hold_attack(orc);
        const std::uint32_t waver = waver_bound_of(orc);
        ASSERT_GT(waver, 0u);
        const std::uint32_t s = world().rng_.state_;

        EXPECT_EQ(1, orc->stats()->do_command());

        EXPECT_EQ(state_after(s, {waver}), world().rng_.state_) << "the waver alone";
        EXPECT_FLOAT_EQ(0.0f, orc->busy()) << "a wall is never swung at";
        EXPECT_EQ(ANI_WALK, orc->ani_type()) << "a wall is never swung at";
        EXPECT_EQ(ox, orc->xpos());
        EXPECT_EQ(fixes ? oy - step : oy, orc->ypos())
            << (fixes ? "the refused step's sidestep: one stepsize UP" : "the classic round only faces: not moved");
        EXPECT_EQ(FACE_RIGHT, orc->curdir()) << "walkstep keeps the facing it had";
    }
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(std::string(setting(fixes)) + ", an ally on the pad");
        walker* twin_ally = nullptr;
        Scene twin = ally_scene(fixes, &twin_ally);
        ASSERT_TRUE(twin.bot && twin.soldier && twin_ally);
        ASSERT_TRUE(refusal(twin.bot, 1, 0) == walker::FireCheckDenial::NoRanged)
            << "the staging: refused as melee only, the foe in reach";

        walker* ally = nullptr;
        Scene sc = ally_scene(fixes, &ally);
        ASSERT_TRUE(sc.bot && sc.soldier && ally);
        walker* orc = sc.bot;
        const short ox = orc->xpos();
        const short oy = orc->ypos();
        const int step = static_cast<int>(orc->stepsize());
        ASSERT_GT(step, 0);
        const Aim aim = order_aim(orc, sc.soldier);
        ASSERT_EQ(1, aim.dx) << "the staging: the order aims RIGHT";
        ASSERT_EQ(0, aim.dy);
        ASSERT_EQ(FACE_RIGHT, orc->curdir()) << "the staging: the orc already faces its foe";
        ASSERT_NE(0, orc->is_friendly(ally)) << "the staging: the ally is on the orc's side";
        ASSERT_TRUE(box_meets(pad_box(orc), ally)) << "the staging: the ally stands on the orc's pad";
        ASSERT_TRUE(orc->fire_pad_answer() == walker::PadAnswer::Clear)
            << "the staging: an own-team body passes the weapon probe";
        ASSERT_TRUE(meets(orc, ox + step, oy, ally)) << "the staging: the full step meets the ally";
        ASSERT_FALSE(meets(orc, ox + 1, oy, ally)) << "the staging: the one-unit step does not";
        ASSERT_TRUE(ally->stats()->commands.empty()) << "the staging: the ally holds no order";
        hold_attack(orc);
        const std::uint32_t waver = waver_bound_of(orc);
        ASSERT_GT(waver, 0u);
        const std::uint32_t s = state_where({{waver, kAny}, {3, kNonZero}});
        world().rng_.state_ = s;

        EXPECT_EQ(1, orc->stats()->do_command());

        EXPECT_FLOAT_EQ(0.0f, orc->busy()) << "an ally is never swung at";
        EXPECT_EQ(ANI_WALK, orc->ani_type()) << "an ally is never swung at";
        EXPECT_EQ(oy, orc->ypos());
        if (fixes) {
            EXPECT_EQ(state_after(s, {waver, 3}), world().rng_.state_) << "the waver and the shove's 2-in-3 roll";
            EXPECT_EQ(ox + 1, orc->xpos()) << "the full step is refused by the ally; the one-unit step moves";
            const Cmd a = front_of(ally);
            EXPECT_EQ(1u, a.size);
            EXPECT_EQ(COMMAND_WALK, a.type) << "the refused step shoved the ally";
            EXPECT_EQ(4, a.count) << "living::shove's WALK of 4 rounds";
            EXPECT_EQ(aim.dx, a.com1) << "along the orc's step, clamped to one unit as every WALK order is";
            EXPECT_EQ(aim.dy, a.com2);
        } else {
            EXPECT_EQ(state_after(s, {waver}), world().rng_.state_) << "the waver alone";
            EXPECT_EQ(ox, orc->xpos()) << "the classic round only faces: not moved";
            EXPECT_TRUE(ally->stats()->commands.empty()) << "the classic round shoves nobody";
        }
    }
}

// Gameplay Fixes BC (#447), the floor term of statistics::attack_face_or_fight
// (#447's rule of floors: a flyer never changes floors); both settings. Two
// floors; a team-1 ghost (melee only) at (320,320) on floor 0 facing RIGHT,
// holding its foe, a team-0 soldier 1 px off its right side on floor 1. The
// reach does not see floors, so the line check refuses NoRanged (twin world);
// the soldier's body is on another floor, so the ghost's pad holds nothing
// (Clear). One round, both settings: it only faces its foe: no step (a step
// would carry it over the soldier's spot), no swing.
TEST_F(SemAi5, a_melee_only_attacker_whose_foe_is_on_another_floor_only_faces_it)
{
    const auto ghost_scene = [this](short fixes) {
        Scene sc;
        fresh_world(fixes);
        two_floors();
        sc.bot = living_at(FAMILY_GHOST, 1, 320, 320, 0);
        if (sc.bot == nullptr)
            return sc;
        sc.soldier = living_at(FAMILY_SOLDIER, 0, static_cast<short>(320 + sc.bot->sizex() + 1), 320, 1);
        if (sc.soldier == nullptr)
            return sc;
        face(sc.bot, FACE_RIGHT);
        sc.bot->set_foe(sc.soldier);
        return sc;
    };
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(setting(fixes));
        Scene twin = ghost_scene(fixes);
        ASSERT_TRUE(twin.bot && twin.soldier);
        ASSERT_TRUE(refusal(twin.bot, 1, 0) == walker::FireCheckDenial::NoRanged)
            << "the staging: refused as melee only, the foe in reach (the reach does not see floors)";

        Scene sc = ghost_scene(fixes);
        ASSERT_TRUE(sc.bot && sc.soldier);
        walker* ghost = sc.bot;
        ASSERT_EQ(2, world().floor_count()) << "the staging: two floors";
        ASSERT_EQ(0, ghost->floor());
        ASSERT_EQ(1, sc.soldier->floor()) << "the staging: the soldier is one floor up";
        const Aim aim = order_aim(ghost, sc.soldier);
        ASSERT_EQ(1, aim.dx) << "the staging: the order aims RIGHT";
        ASSERT_EQ(0, aim.dy);
        ASSERT_EQ(FACE_RIGHT, ghost->curdir()) << "the staging: the ghost already faces its foe";
        ASSERT_TRUE(ghost->fire_pad_answer() == walker::PadAnswer::Clear)
            << "the staging: the ghost's pad, on its own floor, holds nothing";
        hold_attack(ghost);
        const std::uint32_t waver = waver_bound_of(ghost);
        ASSERT_GT(waver, 0u);
        const std::uint32_t s = world().rng_.state_;

        EXPECT_EQ(1, ghost->stats()->do_command());

        EXPECT_EQ(state_after(s, {waver}), world().rng_.state_) << "the waver alone";
        EXPECT_EQ(320, ghost->xpos()) << "a foe on another floor is only faced: no step";
        EXPECT_EQ(320, ghost->ypos());
        EXPECT_FLOAT_EQ(0.0f, ghost->busy()) << "no swing";
        EXPECT_EQ(ANI_WALK, ghost->ani_type()) << "no swing";
        EXPECT_EQ(FACE_RIGHT, ghost->curdir());
        EXPECT_EQ(kOrder - 1, front_of(ghost).count);
    }
}

// Gameplay Fixes BC (#447) in a ticking world; both settings. The orc of
// a_faced_melee_only_attacker_swings_at_a_body_on_its_pad_with_the_setting_on
// (the soldier's body on its pad, not busy, the ATTACK order of 30 rounds)
// with magic for three swings at its weapon's cost, which is less than HOWL
// costs (asserted), so no special can ever fire. No magic returns and no hit
// point heals during the test: each regen delay starts at 0 and is longer
// than the test (asserted). The soldier is a seat with nobody at it
// (ACT_CONTROL: it does not act, and a seat's hit response does nothing) with
// 400 hp, so it cannot die here. Asserted first, in a twin world: the line
// check refuses the orc NoRanged with that magic and with none (the
// melee-only gate comes before the magic gate, so a dry orc stays in this arm
// of the order). Walkers are read by entity id after each world.tick().
// P, the acts from one swing's start to the next, is the fire delay (a whole
// number, asserted; busy drains one an act once the animation is over) plus
// the attack animation's further acts; a blow lands when the animation's row
// ends, its length minus one acts after the swing starts. Both are counted
// from the orc's own animation row. The order spends a round only on an act
// that does not animate, so a fire delay's worth of rounds each period.
//  Setting on, 4P ticks: swings start on ticks 1, 1 + P, 1 + 2P and 1 + 3P,
//    each charging busy its fire delay. The first three blows land on their
//    start plus the row's further acts, and the third spends the orc's last
//    magic. The fourth swing is not paid for and still starts (the weapon's
//    magic is not asked; that an unpaid swing still costs its animation and
//    its pause is #574): it lands nothing, so the soldier's hit points do not
//    change after the third blow. The orc never moves (a body on its pad is
//    never stepped at) and the order still holds, four fire delays of rounds
//    spent.
//  Setting off, the order's 30 ticks: no swing starts and no blow lands; the
//    order is spent, one round an act.
TEST_F(SemAi5, a_melee_only_attacker_lands_one_blow_per_weapon_pause_in_its_order)
{
    const int blows = 3;
    const int periods = blows + 1;
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(setting(fixes));
        Scene twin = orc_scene(fixes, FACE_RIGHT, 1);
        ASSERT_TRUE(twin.bot && twin.soldier);
        twin.bot->stats()->set_magicpoints(static_cast<float>(blows * twin.bot->stats()->weapon_cost()));
        ASSERT_TRUE(refusal(twin.bot, 1, 0) == walker::FireCheckDenial::NoRanged)
            << "the staging: refused as melee only, the foe in reach, with magic for three swings";
        twin.bot->stats()->set_magicpoints(0.0f);
        ASSERT_TRUE(refusal(twin.bot, 1, 0) == walker::FireCheckDenial::NoRanged)
            << "the staging: with no magic still refused as melee only (that gate comes before the magic gate)";

        Scene sc = orc_scene(fixes, FACE_RIGHT, 1);
        ASSERT_TRUE(sc.bot && sc.soldier);
        walker* orc = sc.bot;
        sc.soldier->set_act_type(ACT_CONTROL);
        ASSERT_EQ(FACE_RIGHT, orc->curdir()) << "the staging: the orc already faces its foe";
        ASSERT_TRUE(orc->fire_pad_answer() == walker::PadAnswer::Body) << "the staging: the soldier's body is on the orc's pad";
        ASSERT_FLOAT_EQ(0.0f, orc->busy());
        const int cost = orc->stats()->weapon_cost();
        ASSERT_GT(cost, 0) << "the staging: a swing costs magic";
        ASSERT_LT(blows * cost, static_cast<int>(og::sim::cast_cost(*orc, 1, false)))
            << "the staging: less magic than HOWL costs";
        orc->stats()->set_magicpoints(static_cast<float>(blows * cost));
        ASSERT_GE(orc->curdir(), 0);
        const int row_index = orc->curdir() + ANI_ATTACK * NUM_FACINGS;
        ASSERT_LT(row_index, orc->ani_count) << "the staging: the orc has an attack row";
        const signed char* row = orc->ani[row_index];
        ASSERT_NE(nullptr, row);
        int row_len = 0;
        while (row_len < 128 && row[row_len] != -1)
            ++row_len;
        ASSERT_GE(row_len, 1) << "the staging: the attack row has frames";
        const int fire_delay = static_cast<int>(std::ceil(orc->fire_frequency()));
        ASSERT_GE(fire_delay, 1);
        ASSERT_FLOAT_EQ(static_cast<float>(fire_delay), orc->fire_frequency()) << "the staging: a whole fire delay";
        const int lands_after = row_len - 1;
        const int period = fire_delay + lands_after;
        const int ticks = fixes ? periods * period : kOrder;
        ASSERT_LT(periods * fire_delay, kOrder) << "the staging: the order outlasts the four periods";
        orc->stats()->set_current_magic_delay(0);
        ASSERT_FLOAT_EQ(0.0f, orc->stats()->magic_per_round()) << "the staging: no magic returns";
        ASSERT_GT(orc->stats()->max_magic_delay(), ticks) << "the staging: no magic returns during the test";
        sc.soldier->stats()->set_current_heal_delay(0);
        ASSERT_FLOAT_EQ(0.0f, sc.soldier->stats()->heal_per_round()) << "the staging: no hit point heals";
        ASSERT_GT(sc.soldier->stats()->max_heal_delay(), ticks) << "the staging: no hit point heals during the test";
        hold_attack(orc);
        const std::uint32_t orc_id = orc->entity_id();
        const std::uint32_t soldier_id = sc.soldier->entity_id();
        const float hp0 = sc.soldier->stats()->hitpoints();

        std::vector<int> starts;
        std::vector<float> busy_at_start;
        std::vector<int> landed;
        float busy_before = 0.0f;
        float hp_before = hp0;
        float hp_after_third = -1.0f;
        float magic_after_third = -1.0f;
        for (int t = 1; t <= ticks; ++t) {
            world().tick();
            const walker* o = world().find_by_id(orc_id);
            const walker* foe = world().find_by_id(soldier_id);
            ASSERT_TRUE(o != nullptr && foe != nullptr) << "tick " << t;
            if (o->busy() > busy_before) {
                starts.push_back(t);
                busy_at_start.push_back(o->busy());
            }
            if (foe->stats()->hitpoints() < hp_before) {
                landed.push_back(t);
                if (landed.size() == static_cast<std::size_t>(blows)) {
                    hp_after_third = foe->stats()->hitpoints();
                    magic_after_third = o->stats()->magicpoints();
                }
            }
            busy_before = o->busy();
            hp_before = foe->stats()->hitpoints();
            EXPECT_EQ(320, o->xpos()) << "tick " << t << ": the orc never moves";
            EXPECT_EQ(320, o->ypos()) << "tick " << t;
        }
        const walker* o = world().find_by_id(orc_id);
        const walker* foe = world().find_by_id(soldier_id);
        ASSERT_TRUE(o != nullptr && foe != nullptr);
        ASSERT_FALSE(foe->dead()) << "the staging: the soldier cannot die in this test";
        if (fixes) {
            ASSERT_EQ((std::vector<int>{1, 1 + period, 1 + 2 * period, 1 + 3 * period}), starts)
                << "one swing per weapon pause, P = " << period << "; the fourth is not paid for and still starts";
            for (std::size_t i = 0; i < busy_at_start.size(); ++i)
                EXPECT_FLOAT_EQ(o->fire_frequency(), busy_at_start[i]) << "swing " << i + 1 << " charges busy its fire delay";
            EXPECT_EQ((std::vector<int>{1 + lands_after, 1 + period + lands_after, 1 + 2 * period + lands_after}), landed)
                << "each paid blow lands when the attack row ends; the unpaid fourth lands nothing";
            EXPECT_FLOAT_EQ(0.0f, magic_after_third) << "the third blow spends the orc's last magic";
            EXPECT_FLOAT_EQ(0.0f, o->stats()->magicpoints()) << "the fourth swing spends nothing";
            EXPECT_FLOAT_EQ(hp_after_third, foe->stats()->hitpoints())
                << "the soldier's hit points do not change after the third blow";
            EXPECT_EQ(COMMAND_ATTACK, front_of(o).type) << "the order still holds";
            EXPECT_EQ(kOrder - periods * fire_delay, front_of(o).count)
                << "a round on each act that does not animate: a fire delay's worth each period";
        } else {
            EXPECT_TRUE(starts.empty()) << "the classic order starts no swing";
            EXPECT_TRUE(landed.empty()) << "the classic order lands no blow";
            EXPECT_FLOAT_EQ(hp0, foe->stats()->hitpoints());
            EXPECT_TRUE(o->stats()->commands.empty()) << "the order is spent, one round an act";
        }
    }
}

// Gameplay Fixes BC (#447): the rule reads nothing from the order and snaps
// the bot to face its foe, heading included, before it asks the pad, so an
// ATTACK order a mode's script or a pack queues (com1 and com2 0) fights the
// same way, also when it finds the bot's heading pointing away from where
// the bot faces. The scene and asserts of
// a_faced_melee_only_attacker_swings_at_a_body_on_its_pad_with_the_setting_on
// with the order queued as (30, 0, 0) and the orc's heading pointing DOWN
// while it faces RIGHT (asserted: along that heading its pad holds nothing).
// One round, both settings: the heading points at the foe again and the orc
// has not moved; setting on, the swing starts; off, it only faces. Without
// the snap the orc would ask the pad below it, find it clear and step: the
// step bumps the soldier (a swing all the same) and moves the orc a pixel, so
// "not moved" is the assert that tells the two apart.
TEST_F(SemAi5, an_attack_order_queued_by_a_script_fights_the_same_way)
{
    faced_swing_round(0, /*stale_heading=*/true);
}

// The guard-standoff fix, kept on both settings by
// statistics::attack_face_or_fight: a shooter refused for its facing only
// snaps to face its foe. A team-0 soldier bot (a knife thrower) at (320,320)
// facing UP, holding its foe, a team-1 orc 1 px off its right side, boxes
// level; the order aims RIGHT and the line check refuses Facing (the reach,
// the melee-only and the magic gates pass first; twin world). One round, both
// settings: curdir, enddir and the heading point RIGHT, at the foe; no step,
// no throw; only the waver is drawn. Fix BC's "already faced" term is what
// keeps it out of this case (a bot refused Facing never already faced its
// foe; with that term gone the thrower would swing at the orc on its pad);
// the test pins the snap-face that stays here on both settings.
TEST_F(SemAi5, a_shooter_turned_off_its_foe_still_only_faces)
{
    const auto stage = [this](short fixes) {
        Scene sc;
        fresh_world(fixes);
        sc.bot = living_at(FAMILY_SOLDIER, 0, 320, 320);
        if (sc.bot == nullptr)
            return sc;
        sc.soldier = living_at(FAMILY_ORC, 1, static_cast<short>(320 + sc.bot->sizex() + 1), 320);
        if (sc.soldier == nullptr)
            return sc;
        face(sc.bot, FACE_UP);
        sc.bot->set_foe(sc.soldier);
        return sc;
    };
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(setting(fixes));
        Scene twin = stage(fixes);
        ASSERT_TRUE(twin.bot && twin.soldier);
        ASSERT_TRUE(refusal(twin.bot, 1, 0) == walker::FireCheckDenial::Facing)
            << "the staging: refused for the facing alone";

        Scene sc = stage(fixes);
        ASSERT_TRUE(sc.bot && sc.soldier);
        walker* thrower = sc.bot;
        const Aim aim = order_aim(thrower, sc.soldier);
        ASSERT_EQ(1, aim.dx) << "the staging: the order aims RIGHT";
        ASSERT_EQ(0, aim.dy);
        ASSERT_EQ(FACE_UP, thrower->curdir()) << "the staging: the thrower faces UP, off its foe";
        hold_attack(thrower);
        const std::uint32_t waver = waver_bound_of(thrower);
        ASSERT_GT(waver, 0u);
        const std::uint32_t s = world().rng_.state_;

        EXPECT_EQ(1, thrower->stats()->do_command());

        EXPECT_EQ(state_after(s, {waver}), world().rng_.state_) << "the waver alone";
        EXPECT_EQ(FACE_RIGHT, thrower->curdir()) << "it snaps to face its foe";
        EXPECT_EQ(FACE_RIGHT, thrower->enddir());
        EXPECT_EQ(FACE_RIGHT, thrower->facing(static_cast<short>(thrower->lastx()), static_cast<short>(thrower->lasty())))
            << "its heading points at its foe";
        EXPECT_EQ(320, thrower->xpos()) << "no step";
        EXPECT_EQ(320, thrower->ypos());
        EXPECT_FLOAT_EQ(0.0f, thrower->busy()) << "no throw";
        EXPECT_EQ(ANI_WALK, thrower->ani_type()) << "no throw";
        EXPECT_EQ(kOrder - 1, front_of(thrower).count);
    }
}

// Gameplay Fixes BC (#447), a step that meets the foe's own body beside the
// pad; both settings. A team-1 orc at (320,320) facing DOWN_RIGHT, holding its
// foe, a team-0 elf (a smaller box) just right of the orc and as low as its
// box can stand while staying off the orc's corner pad. Asserted first: the
// order aims DOWN_RIGHT (both deltas inside 3:1) and the orc faces that way;
// the pad is clear; the line check refuses NoRanged (twin world); the full
// step's box meets the elf and the one-unit step's box does not (the obmap's
// test). One round:
//  Setting on: the pad is clear, so the orc steps: the full step is refused by
//    the elf's own body, and that loud step into a hostile body is a bump
//    (living::collide: init_fire, busy charged the fire delay, the attack
//    animation); walkstep's one-unit step is then taken: moved one pixel on
//    each axis.
//  Setting off: it only faces: not moved, busy 0.
// Both: only the waver is drawn.
TEST_F(SemAi5, a_small_foe_beside_the_pad_is_bumped_by_the_step)
{
    const auto stage = [this](short fixes) {
        Scene sc;
        fresh_world(fixes);
        sc.bot = living_at(FAMILY_ORC, 1, 320, 320);
        if (sc.bot == nullptr)
            return sc;
        sc.soldier = living_at(FAMILY_ELF, 0, 480, 480);
        if (sc.soldier == nullptr)
            return sc;
        face(sc.bot, FACE_DOWN_RIGHT);
        // The elf's lowest spot whose box stays off the pad: the pad's box is
        // one pixel below the orc's, and the obmap shrinks each box by one.
        place(sc.soldier, static_cast<short>(320 + sc.bot->sizex() + 1),
              static_cast<short>(320 + sc.bot->sizey() + 2 - sc.soldier->sizey()));
        sc.bot->set_foe(sc.soldier);
        return sc;
    };
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(setting(fixes));
        Scene twin = stage(fixes);
        ASSERT_TRUE(twin.bot && twin.soldier);
        ASSERT_TRUE(refusal(twin.bot, 1, 1) == walker::FireCheckDenial::NoRanged)
            << "the staging: refused as melee only, the foe in reach";

        Scene sc = stage(fixes);
        ASSERT_TRUE(sc.bot && sc.soldier);
        walker* orc = sc.bot;
        walker* elf = sc.soldier;
        ASSERT_LT(elf->sizex(), orc->sizex()) << "the staging: a smaller foe";
        const Aim aim = order_aim(orc, elf);
        ASSERT_EQ(1, aim.dx) << "the staging: the order aims DOWN_RIGHT";
        ASSERT_EQ(1, aim.dy);
        ASSERT_EQ(FACE_DOWN_RIGHT, orc->curdir()) << "the staging: the orc already faces that way";
        ASSERT_TRUE(orc->fire_pad_answer() == walker::PadAnswer::Clear) << "the staging: the pad misses the elf";
        ASSERT_FALSE(box_meets(pad_box(orc), elf));
        const int step = static_cast<int>(orc->stepsize());
        ASSERT_GT(step, 1);
        ASSERT_TRUE(meets(orc, 320 + step, 320 + step, elf)) << "the staging: the full step meets the elf";
        ASSERT_FALSE(meets(orc, 321, 321, elf)) << "the staging: the one-unit step does not";
        hold_attack(orc);
        const std::uint32_t waver = waver_bound_of(orc);
        ASSERT_GT(waver, 0u);
        const std::uint32_t s = world().rng_.state_;

        EXPECT_EQ(1, orc->stats()->do_command());

        EXPECT_EQ(state_after(s, {waver}), world().rng_.state_) << "the waver alone";
        if (fixes) {
            EXPECT_FLOAT_EQ(orc->fire_frequency(), orc->busy()) << "the step into the elf's body is a bump";
            EXPECT_EQ(ANI_ATTACK, orc->ani_type()) << "the step into the elf's body is a bump";
            EXPECT_EQ(321, orc->xpos()) << "the one-unit step is taken";
            EXPECT_EQ(321, orc->ypos()) << "the one-unit step is taken";
        } else {
            EXPECT_FLOAT_EQ(0.0f, orc->busy()) << "the classic round only faces";
            EXPECT_EQ(ANI_WALK, orc->ani_type());
            EXPECT_EQ(320, orc->xpos()) << "the classic round only faces: not moved";
            EXPECT_EQ(320, orc->ypos());
        }
        EXPECT_EQ(FACE_DOWN_RIGHT, orc->curdir());
        EXPECT_EQ(kOrder - 1, front_of(orc).count);
    }
}
