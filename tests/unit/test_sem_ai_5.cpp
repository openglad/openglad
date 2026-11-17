/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch ai-5: how a bot turns toward a foe it cannot strike
// yet (Gameplay Fixes BE, #513). Each test is named after the rule it asserts.
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

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <string>

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

    // A level-4 bot (ACT_RANDOM, no seat) with its family's box, 400 hp,
    // 500 MP, no regeneration, walking, facing right.
    walker* living_at(int family, unsigned char team, short x, short y)
    {
        walker* w = world().add_ob(Order::Living, family);
        if (w == nullptr)
            return nullptr;
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

    // The stream that takes act() into act_random with the held foe kept:
    // the special roll not 0, the second 1-in-5 0, the 1-in-80 not 0; then
    // the waver of the line check's probe; then, on the first act that runs
    // a SEARCH, walk_to_foe's 1-in-300 not 0 (no reset: the chase goes on and
    // counts the path-check counter down by one).
    static std::uint32_t into_act_random(std::uint32_t waver)
    {
        return state_where({{5, kNonZero}, {5, 0}, {80, kNonZero}, {waver, kAny}, {300, kNonZero}});
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
