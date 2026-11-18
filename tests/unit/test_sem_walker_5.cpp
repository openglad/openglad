/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch walker-5: the line check a shooter asks before it
// fires (walker::fire_check), and the one step of a flying weapon it shares
// with the shot (walker::flight_step). Each test is named after the rule it
// asserts.
//
// Every scene is headless: an open field of grass 1024 px a side (GRID_SIZE
// 16), bodies placed in pixels, the sim stream (GameWorld::rng_) pinned to a
// state whose draws are known. A weapon's numbers (its step, its line of
// sight, its box) are read from a weapon the game builds, never typed.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/event.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
[[maybe_unused]] std::uint32_t state_where(std::initializer_list<Draw> draws)
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
[[maybe_unused]] std::uint32_t state_after(std::uint32_t state, std::initializer_list<std::uint32_t> bounds)
{
    og::sim::SimRandom probe(state);
    for (std::uint32_t b : bounds)
        (void)probe.next(b);
    return probe.state_;
}

using Denial = walker::FireCheckDenial;

// The first RNG state whose first draw (of `first_bound`) answers
// `first_want` and whose next draws, all of `bound`, answer as `keep` says.
template <typename Keep>
std::uint32_t state_where_then(std::uint32_t first_bound, std::uint32_t first_want, std::uint32_t bound,
                               int n, Keep keep)
{
    for (std::uint32_t seed = 1;; ++seed) {
        og::sim::SimRandom probe(seed);
        bool all = probe.next(first_bound) == first_want;
        for (int i = 0; all && i < n; ++i)
            all = keep(probe.next(bound));
        if (all)
            return seed;
    }
}

// The unit step of each facing (UP, UR, R, DR, D, DL, L, UL).
constexpr int kStep[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};

const char* dir_name(int dir)
{
    static const char* const kNames[8] = {"UP", "UP_RIGHT", "RIGHT", "DOWN_RIGHT",
                                          "DOWN", "DOWN_LEFT", "LEFT", "UP_LEFT"};
    return (dir >= 0 && dir < 8) ? kNames[dir] : "?";
}

class SemLineCheck : public ::testing::Test {
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

    // An open field of grass, `tiles_w` x `tiles_h` tiles (1024 px a side by
    // default), with nobody on it.
    void fresh_world(short fixes = 0, int tiles_w = 64, int tiles_h = 64)
    {
        tw_.reset();
        tw_ = std::make_unique<TestGameWorld>();
        GameWorld& w = world();
        w.resize_grid(tiles_w, tiles_h);
        std::fill_n(w.grid.data.get(),
                    static_cast<std::size_t>(w.grid.w) * w.grid.h,
                    static_cast<std::uint8_t>(PIX_GRASS1));
        w.my_team = 0;
        w.gameplay_fixes = fixes;
        w.rng_.state_ = 12345u;
    }

    GameWorld& world() { return tw_->world(); }

    void tile(int tx, int ty, unsigned char pix)
    {
        world().grid.data[static_cast<std::size_t>(tx + world().grid.w * ty)] = pix;
    }

    // Sizes first, then the position: the obmap registers a body in every
    // cell its box covers at the moment it is placed.
    walker* place(walker* w, short x, short y, short sx = 16, short sy = 16)
    {
        if (w == nullptr)
            return nullptr;
        w->set_sizex(sx);
        w->set_sizey(sy);
        w->setxy(static_cast<short>(x + 1), y);  // force a real move
        w->setxy(x, y);
        return w;
    }

    // A level-4 bot (ACT_RANDOM, no seat), 400 hp, 500 MP, no regeneration,
    // walking, facing right. Its box is the family's unless (sx, sy) is given.
    walker* living_at(int family, unsigned char team, short x, short y, short sx = -1, short sy = -1)
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
        return place(w, x, y, sx < 0 ? w->sizex() : sx, sy < 0 ? w->sizey() : sy);
    }

    // The game's own snap-face (walker::face_delta): curdir, enddir and the
    // heading fire_check aims along (the facing's unit step times stepsize,
    // or the unit step itself for a tower, whose stepsize is 0).
    static void face(walker* w, int dir)
    {
        w->face_delta(static_cast<short>(kStep[dir][0]), static_cast<short>(kStep[dir][1]));
    }

    // The shot `shooter` builds for the way it faces, pointed straight along
    // that facing at its own step (no waver) and already facing it, so its
    // first walk() steps; placed at (x, y), nothing met yet.
    walker* aimed_shot(walker* shooter, int dir, short x, short y)
    {
        walker* shot = shooter->create_weapon();
        if (shot == nullptr)
            return nullptr;
        const float s = shot->stepsize();
        shot->set_lastx(static_cast<float>(kStep[dir][0]) * s);
        shot->set_lasty(static_cast<float>(kStep[dir][1]) * s);
        shot->set_curdir(static_cast<signed char>(dir));
        place(shot, x, y, shot->sizex(), shot->sizey());
        shot->set_collide_ob(nullptr);
        return shot;
    }

    // Do the boxes of `a` at (ax, ay) and `b` where it stands meet, by the
    // obmap's own test (each box shrunk by a pixel)?
    static bool meets(const walker* a, float ax, float ay, const walker* b)
    {
        return ::collide(static_cast<short>(ax), static_cast<short>(ay), a->sizex(), a->sizey(),
                         b->xpos(), b->ypos(), b->sizex(), b->sizey()) != 0;
    }

    // A box of (sx, sy) at `b` meets `body` where it stands (the obmap's test).
    struct Box
    {
        int x = 0;
        int y = 0;
    };
    static bool box_meets(Box b, short sx, short sy, const walker* body)
    {
        return ::collide(static_cast<short>(b.x), static_cast<short>(b.y), sx, sy,
                         body->xpos(), body->ypos(), body->sizex(), body->sizey()) != 0;
    }

    // The shot a shooter would take the way it faces, read from a weapon the
    // game builds for it and then drops (no draw): its step and line of sight
    // after the shooter's profile, its box, the pad fire() and the line check
    // put it on (walker::weapon_pad), its heading with a waver of 0, the
    // waver draw (walker::set_weapon_heading: next(h + 1) - h / 2, h the
    // step / 2 clamped to 0..127) and the reach gate (step x line of sight).
    struct ShotPlan
    {
        float step = 0.0f;
        int los = 0;
        short sx = 0;
        short sy = 0;
        short pad_x = 0;
        short pad_y = 0;
        float lx = 0.0f;
        float ly = 0.0f;
        std::uint32_t waver_bound = 0;
        std::uint32_t waver_zero = 0;
        std::int32_t reach = 0;
    };
    ShotPlan plan_of(walker* shooter, int dir)
    {
        ShotPlan p;
        walker* w = shooter->create_weapon();
        if (w == nullptr)
            return p;
        p.step = w->stepsize();
        p.los = w->lineofsight();
        p.sx = w->sizex();
        p.sy = w->sizey();
        shooter->weapon_pad(*w, p.pad_x, p.pad_y);
        const auto h = static_cast<signed char>(std::clamp(p.step / 2, 0.0f, 127.0f));
        p.waver_bound = static_cast<std::uint32_t>(h + 1);
        p.waver_zero = static_cast<std::uint32_t>(h / 2);
        p.lx = static_cast<float>(kStep[dir][0]) * p.step;
        p.ly = static_cast<float>(kStep[dir][1]) * p.step;
        p.reach = static_cast<std::int32_t>(p.step) * static_cast<std::int32_t>(p.los);
        world().remove_ob(w);
        return p;
    }

    // The spots fire_check's classic loop tests: the pad, then each spot i
    // steps on from the last, cut to a whole pixel (setxy(xpos + i * lastx)):
    // 0, 1, 3, 6, 10 ... steps out.
    static std::vector<Box> classic_spots(const ShotPlan& p)
    {
        std::vector<Box> spots;
        std::int32_t x = p.pad_x;
        std::int32_t y = p.pad_y;
        for (int i = 0; i < p.los; ++i) {
            x = static_cast<short>(static_cast<std::int32_t>(static_cast<float>(x) + static_cast<float>(i) * p.lx));
            y = static_cast<short>(static_cast<std::int32_t>(static_cast<float>(y) + static_cast<float>(i) * p.ly));
            spots.push_back({x, y});
        }
        return spots;
    }

    // The boxes the shot's own steps test (walker::walk: the box at the
    // whole-pixel position plus one step), steps 1 .. lineofsight - 1, when
    // nothing stops them; the shot keeps its fraction between steps.
    static std::vector<Box> flown_boxes(const ShotPlan& p)
    {
        std::vector<Box> boxes;
        float wx = p.pad_x;
        float wy = p.pad_y;
        for (int k = 1; k < p.los; ++k) {
            const short xp = static_cast<short>(wx);
            const short yp = static_cast<short>(wy);
            boxes.push_back({static_cast<std::int32_t>(static_cast<float>(xp) + p.lx),
                             static_cast<std::int32_t>(static_cast<float>(yp) + p.ly)});
            wx += p.lx;
            wy += p.ly;
        }
        return boxes;
    }

    // Where the shot stands when each of those steps is asked (its whole-pixel
    // position: what the arrow slit's gamble measures from its shooter).
    static std::vector<Box> flown_from(const ShotPlan& p)
    {
        std::vector<Box> from;
        float wx = p.pad_x;
        float wy = p.pad_y;
        for (int k = 1; k < p.los; ++k) {
            from.push_back({static_cast<short>(wx), static_cast<short>(wy)});
            wx += p.lx;
            wy += p.ly;
        }
        return from;
    }

    // Every weapon and effect leaves the world (the probes fire_check left
    // dead, a shot that ended, what its death made).
    void drop_weapons()
    {
        std::vector<walker*> gone;
        for (const auto& u : world().weaplist)
            gone.push_back(u.get());
        for (const auto& u : world().fxlist)
            gone.push_back(u.get());
        for (walker* w : gone)
            world().remove_ob(w);
    }

    // The live weapons in the world.
    int live_weapons()
    {
        int n = 0;
        for (const auto& u : world().weaplist)
            if (u && !u->dead())
                ++n;
        return n;
    }

    // fire_check as a bot asks it, and what its answer leaves behind,
    // whichever way it ends (a wall or a body on the pad, a step that strikes
    // or is stopped, the mode ball after a step, the steps run out): the test
    // weapon it built is dead, so asking launched no shot, and no walker is
    // still named as the line check's probe (GameWorld::slit_open_for, which
    // would open arrow slits for whatever weapon later sits at that address).
    bool ask(walker* shooter, short dx, short dy, Denial* why)
    {
        const int before = live_weapons();
        const bool passes = shooter->fire_check(dx, dy, why);
        EXPECT_EQ(nullptr, world().slit_open_for) << "the check's answer leaves no probe marked at the slits";
        EXPECT_EQ(before, live_weapons()) << "the check's answer leaves its test weapon dead";
        return passes;
    }

    // fire() from the pinned stream, then the shot acts until it ends (as
    // weap::act does each tick). True when the shot ended.
    bool fire_and_fly(walker* shooter, int max_acts)
    {
        walker* shot = shooter->fire();
        for (int a = 0; shot != nullptr && !shot->dead() && a < max_acts; ++a)
            shot->act();
        return shot == nullptr || shot->dead();
    }

    // ---- Gameplay Fixes BF: doors on the line ----

    // The collision table's cell (obmap.cpp OBRES): a box of (sx, sy) at b is filed under
    // the cells x / 32 to (x + sx) / 32 and y / 32 to (y + sy) / 32, and a weapon meeting an
    // enemy weapon-order body draws its clash roll once for each such cell that holds it.
    static constexpr int kObCell = 32;
    static bool covers_cell(Box b, short sx, short sy, int c, int r)
    {
        return b.x / kObCell <= c && c <= (b.x + sx) / kObCell && b.y / kObCell <= r &&
               r <= (b.y + sy) / kObCell;
    }
    // A box of (sx, sy) at b and `body`'s box share a collision cell.
    static bool shares_a_cell(Box b, short sx, short sy, const walker* body)
    {
        for (int c = body->xpos() / kObCell; c <= (body->xpos() + body->sizex()) / kObCell; ++c)
            for (int r = body->ypos() / kObCell; r <= (body->ypos() + body->sizey()) / kObCell; ++r)
                if (covers_cell(b, sx, sy, c, r))
                    return true;
        return false;
    }

    // A door of `team` at (x, y), in the family's own box, sitting as a level's door sits.
    walker* door_at(unsigned char team, short x, short y)
    {
        walker* d = world().add_ob(Order::Weapon, FAMILY_DOOR);
        if (d == nullptr)
            return nullptr;
        d->set_team_num(team);
        d->set_act_type(ACT_SIT);
        return place(d, x, y, d->sizex(), d->sizey());
    }

    // Where a body of (bw, bh) stands across the flight of a shooter facing UP (the heading
    // straight up, waver 0): its box lies in one collision cell, no box before the k-th
    // flight box covers that cell (nor the pad's), and the k-th box meets the body (the
    // body's bottom row on the box's bottom row). The first such k from `from` on; k is -1
    // when there is none.
    struct LineSpot
    {
        int k = -1;
        short x = 0;
        short y = 0;
    };
    static LineSpot up_line_spot(const ShotPlan& p, short bw, short bh, int from)
    {
        LineSpot s;
        int x = -1;
        for (int cx = p.pad_x - bw + 2; cx <= p.pad_x + p.sx - 2 && x < 0; ++cx)
            if (cx / kObCell == (cx + bw) / kObCell &&
                ::collide(static_cast<short>(cx), 0, bw, 4, p.pad_x, 0, p.sx, 4))
                x = cx;
        if (x < 0 || p.lx != 0.0f || p.ly >= 0.0f)
            return s;
        const std::vector<Box> boxes = flown_boxes(p);
        for (int k = from; k < static_cast<int>(boxes.size()); ++k) {
            const Box b = boxes[static_cast<std::size_t>(k)];
            const int r = b.y / kObCell;
            const int above = k == 0 ? p.pad_y / kObCell : boxes[static_cast<std::size_t>(k - 1)].y / kObCell;
            const int y = b.y + p.sy - bh;
            if (above > r && y >= 0 && y / kObCell == r && (y + bh) / kObCell == r) {
                s.k = k;
                s.x = static_cast<short>(x);
                s.y = static_cast<short>(y);
                return s;
            }
        }
        return s;
    }

    // fire_check's classic loop (the setting off) on open grass with an enemy `door` in one
    // collision cell and a living `body` (or none) on the line, the two in cells that no
    // classic spot's box covers together: the waver is drawn; at each spot (the pad, then 1,
    // 3, 6 ... steps out) a box that covers the door's cell draws one clash roll before its
    // box test (obmap.cpp), and a lost roll (3 or less) stops the test weapon on the door
    // when their boxes meet, or goes on when they do not; a box that meets the body stops it
    // there; past the last spot the check refuses (RayMiss). What it answers, the body it
    // stops on, and the stream after it.
    struct ClassicRun
    {
        bool passes = false;
        Denial why = Denial::RayMiss;
        const walker* met = nullptr;
        std::uint32_t after = 0;
    };
    static ClassicRun classic_run(const ShotPlan& p, std::uint32_t state, const walker* door, const walker* body)
    {
        ClassicRun out;
        og::sim::SimRandom r(state);
        (void)r.next(p.waver_bound);
        const int dc = door->xpos() / kObCell;
        const int dr = door->ypos() / kObCell;
        for (const Box& b : classic_spots(p)) {
            if (covers_cell(b, p.sx, p.sy, dc, dr) && r.next(10) <= 3 && box_meets(b, p.sx, p.sy, door)) {
                out.passes = true;
                out.why = Denial::None;
                out.met = door;
                break;
            }
            if (body != nullptr && box_meets(b, p.sx, p.sy, body)) {
                out.passes = true;
                out.why = Denial::None;
                out.met = body;
                break;
            }
        }
        out.after = r.state_;
        return out;
    }

    // Every dead weapon and every effect leaves the world (the test weapons earlier checks
    // left); a live door, tree or shot stays.
    void drop_dead_weapons()
    {
        std::vector<walker*> gone;
        for (const auto& u : world().weaplist)
            if (u && u->dead())
                gone.push_back(u.get());
        for (const auto& u : world().fxlist)
            gone.push_back(u.get());
        for (walker* w : gone)
            world().remove_ob(w);
    }

    // The test weapon `shooter`'s last check left (dead, still listed), or null.
    walker* last_probe(const walker* shooter)
    {
        walker* found = nullptr;
        for (const auto& u : world().weaplist)
            if (u && u->dead() && u->owner() == shooter)
                found = u.get();
        return found;
    }

    // The notifications posted so far.
    std::size_t notifications()
    {
        std::size_t n = 0;
        for (const auto& e : tw_->events.events())
            if (e.kind == og::sim::EventKind::Notification)
                ++n;
        return n;
    }

    // The shooter of the door tests: a level-4 archer bot facing UP at (512, 2304) on a field
    // 64 x 160 tiles, so that every classic spot stays on the map.
    walker* up_archer(short fixes)
    {
        fresh_world(fixes, 64, 160);
        walker* a = living_at(FAMILY_ARCHER, 0, 512, 2304);
        if (a != nullptr)
            face(a, FACE_UP);
        return a;
    }

    // The door tests' line, for a shooter facing UP with plan p: a door of `door_team` across
    // the flight at up_line_spot's first spot (in one collision cell, beyond the pad's cells,
    // met by the first flight box over its cell), and, when body_family is not -1, a sitting
    // 16 x 16 hostile of that family on the line beyond the door's cell: at the first height
    // where a flight box and a classic spot's box meet it and no classic spot's box covers
    // the door's cell and one of its cells. Each fact is asserted here; the caller asserts
    // the reach.
    void stage_door_line(walker* shooter, const ShotPlan& p, unsigned char door_team, int body_family,
                         walker*& door, walker*& body)
    {
        door = door_at(door_team, 8, 8);
        body = nullptr;
        ASSERT_NE(nullptr, door);
        ASSERT_EQ(Order::Weapon, door->query_order()) << "the staging: a door is a weapon-order body";
        const LineSpot s = up_line_spot(p, door->sizex(), door->sizey(), 0);
        ASSERT_GE(s.k, 0) << "the staging: a spot for the door on the flight";
        place(door, s.x, s.y, door->sizex(), door->sizey());
        const int dc = door->xpos() / kObCell;
        const int dr = door->ypos() / kObCell;
        ASSERT_EQ(dc, (door->xpos() + door->sizex()) / kObCell) << "the staging: the door is in one cell";
        ASSERT_EQ(dr, (door->ypos() + door->sizey()) / kObCell) << "the staging: the door is in one cell";
        ASSERT_FALSE(covers_cell(Box{p.pad_x, p.pad_y}, p.sx, p.sy, dc, dr)) << "the staging: beyond the pad's cells";
        const std::vector<Box> boxes = flown_boxes(p);
        for (int k = 0; k < s.k; ++k)
            ASSERT_FALSE(covers_cell(boxes[static_cast<std::size_t>(k)], p.sx, p.sy, dc, dr))
                << "the staging: no earlier flight box covers the door's cell (box " << k + 1 << ")";
        ASSERT_TRUE(box_meets(boxes[static_cast<std::size_t>(s.k)], p.sx, p.sy, door))
            << "the staging: the first flight box over the door's cell meets it";
        if (body_family < 0)
            return;
        const std::vector<Box> spots = classic_spots(p);
        const short bx = static_cast<short>(shooter->xpos() + (shooter->sizex() - 16) / 2);
        const auto box_on = [&](Box b, int y) {
            return ::collide(static_cast<short>(b.x), static_cast<short>(b.y), p.sx, p.sy, bx,
                             static_cast<short>(y), 16, 16) != 0;
        };
        int by = -1;
        for (int y = dr * kObCell - 16 - 1; y >= 0 && by < 0; --y) {
            bool classic = false;
            bool shared = false;
            for (const Box& b : spots) {
                classic = classic || box_on(b, y);
                if (covers_cell(b, p.sx, p.sy, dc, dr))
                    for (int c = bx / kObCell; c <= (bx + 16) / kObCell; ++c)
                        for (int r = y / kObCell; r <= (y + 16) / kObCell; ++r)
                            shared = shared || covers_cell(b, p.sx, p.sy, c, r);
            }
            bool flown = false;
            for (const Box& b : boxes)
                flown = flown || box_on(b, y);
            if (classic && flown && !shared)
                by = y;
        }
        ASSERT_GE(by, 0) << "the staging: a spot for the body beyond the door";
        body = living_at(body_family, 1, bx, static_cast<short>(by), 16, 16);
        ASSERT_NE(nullptr, body);
        body->set_act_type(ACT_SIT);
        body->stats()->set_armor(0.0f);
        ASSERT_LT(body->ypos() + body->sizey(), door->ypos()) << "the staging: the body stands beyond the door";
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// One step of a weapon in flight (walker_movement.cpp, walker::flight_step)
// ---------------------------------------------------------------------------

// walker::flight_step, both settings (it is not gated). An archer's arrow
// pointed right along its own step (a cardinal arrow's step, which is not a
// whole number of pixels):
//  (a) open ground: Flew, twice; the arrow moved exactly two steps and kept
//      the fraction (its world position is two steps on, where a move cut to
//      a whole pixel each step would have dropped it);
//  (b) a live hostile orc whose box the next step's box meets: Struck, the
//      arrow did not move and its collision partner is the orc;
//  (c) a wall tile under the next step's box only: Stopped, not moved,
//      nothing met;
//  (d) a wave (NO_COLLIDE and IMMORTAL) whose next box meets an orc: Struck,
//      and it moved its step (a NO_COLLIDE weapon passes the body it strikes).
TEST_F(SemLineCheck, one_flight_step_tells_a_strike_from_a_stop_and_a_free_step)
{
    constexpr short kX = 400;
    constexpr short kY = 320;
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        {
            SCOPED_TRACE("(a) open ground");
            fresh_world(fixes);
            walker* archer = living_at(FAMILY_ARCHER, 0, 300, kY);
            ASSERT_NE(nullptr, archer);
            walker* arrow = aimed_shot(archer, FACE_RIGHT, kX, kY);
            ASSERT_NE(nullptr, arrow);
            const float step = arrow->stepsize();
            ASSERT_NE(std::floor(2.0f * step), 2.0f * step)
                << "the staging: two steps are not a whole number of pixels";
            ASSERT_NE(static_cast<float>(2 * static_cast<int>(step)), 2.0f * step)
                << "the staging: two moves cut to a whole pixel each would land elsewhere";
            EXPECT_EQ(walker::FlightStep::Flew, arrow->flight_step()) << "first step";
            EXPECT_EQ(walker::FlightStep::Flew, arrow->flight_step()) << "second step";
            EXPECT_FLOAT_EQ(static_cast<float>(kX) + 2.0f * step, arrow->worldx())
                << "two steps on, the fraction kept";
            EXPECT_EQ(static_cast<short>(static_cast<float>(kX) + 2.0f * step), arrow->xpos());
            EXPECT_FLOAT_EQ(static_cast<float>(kY), arrow->worldy());
            EXPECT_EQ(nullptr, arrow->collide_ob()) << "nothing met";
        }
        {
            SCOPED_TRACE("(b) a live hostile body ahead");
            fresh_world(fixes);
            walker* archer = living_at(FAMILY_ARCHER, 0, 300, kY);
            ASSERT_NE(nullptr, archer);
            walker* arrow = aimed_shot(archer, FACE_RIGHT, kX, kY);
            ASSERT_NE(nullptr, arrow);
            const float step = arrow->stepsize();
            walker* orc = living_at(FAMILY_ORC, 1, static_cast<short>(kX + arrow->sizex()),
                                    static_cast<short>(kY + (arrow->sizey() - 16) / 2));
            ASSERT_NE(nullptr, orc);
            ASSERT_FALSE(meets(arrow, kX, kY, orc)) << "the staging: the orc is not on the arrow's box";
            ASSERT_TRUE(meets(arrow, static_cast<float>(kX) + step, kY, orc))
                << "the staging: the next step's box meets the orc";
            EXPECT_EQ(walker::FlightStep::Struck, arrow->flight_step());
            EXPECT_EQ(orc, arrow->collide_ob()) << "the body it struck";
            EXPECT_FLOAT_EQ(static_cast<float>(kX), arrow->worldx()) << "a struck step is not taken";
            EXPECT_FLOAT_EQ(400.0f, orc->stats()->hitpoints()) << "the step only tells; act_fire strikes";
        }
        {
            SCOPED_TRACE("(c) a wall ahead");
            fresh_world(fixes);
            walker* archer = living_at(FAMILY_ARCHER, 0, 300, kY);
            ASSERT_NE(nullptr, archer);
            walker* arrow = aimed_shot(archer, FACE_RIGHT, kX, kY);
            ASSERT_NE(nullptr, arrow);
            const float step = arrow->stepsize();
            const int wall_tx = (static_cast<int>(static_cast<float>(kX) + step) + arrow->sizex()) / GRID_SIZE;
            tile(wall_tx, kY / GRID_SIZE, PIX_H_WALL1);
            ASSERT_TRUE(world().query_grid_passable(kX, kY, arrow)) << "the staging: open where it stands";
            ASSERT_FALSE(world().query_grid_passable(static_cast<float>(kX) + step, kY, arrow))
                << "the staging: the wall is under the next step's box";
            EXPECT_EQ(walker::FlightStep::Stopped, arrow->flight_step());
            EXPECT_EQ(nullptr, arrow->collide_ob()) << "a wall is no body";
            EXPECT_FLOAT_EQ(static_cast<float>(kX), arrow->worldx()) << "a stopped step is not taken";
        }
        {
            SCOPED_TRACE("(d) a NO_COLLIDE, IMMORTAL wave over a body");
            fresh_world(fixes);
            walker* mage = living_at(FAMILY_MAGE, 0, 300, kY);
            ASSERT_NE(nullptr, mage);
            mage->set_current_weapon(FAMILY_WAVE);
            walker* wave = aimed_shot(mage, FACE_RIGHT, kX, kY);
            ASSERT_NE(nullptr, wave);
            ASSERT_EQ(FAMILY_WAVE, wave->family());
            ASSERT_TRUE(wave->stats()->query_bit_flags(BIT_NO_COLLIDE)) << "the staging: the wave is NO_COLLIDE";
            ASSERT_TRUE(wave->stats()->query_bit_flags(BIT_IMMORTAL)) << "the staging: the wave is IMMORTAL";
            const float step = wave->stepsize();
            walker* orc = living_at(FAMILY_ORC, 1, static_cast<short>(kX + wave->sizex()),
                                    static_cast<short>(kY + (wave->sizey() - 16) / 2));
            ASSERT_NE(nullptr, orc);
            ASSERT_FALSE(meets(wave, kX, kY, orc)) << "the staging: the orc is not on the wave's box";
            ASSERT_TRUE(meets(wave, static_cast<float>(kX) + step, kY, orc))
                << "the staging: the next step's box meets the orc";
            EXPECT_EQ(walker::FlightStep::Struck, wave->flight_step());
            EXPECT_EQ(orc, wave->collide_ob());
            EXPECT_FLOAT_EQ(static_cast<float>(kX) + step, wave->worldx()) << "a NO_COLLIDE step is taken";
        }
    }
}

// walker::act_fire, both settings (it is not gated): a NO_COLLIDE shot that
// is not IMMORTAL is spent by every step, the free ones too. No stock weapon
// is NO_COLLIDE without being IMMORTAL (the wave family is both); a pack's
// weapon can be. An archer's arrow pointed right along its own step, in open
// ground with nothing under its next box, is given the flag and acts once:
// it takes its step (flight_step's Flew), meets nothing, and is dead. The
// control in the same body, the same arrow without the flag: the same step,
// nothing met, and it flies on.
TEST_F(SemLineCheck, a_no_collide_shot_that_is_not_immortal_is_spent_by_its_first_act)
{
    constexpr short kX = 400;
    constexpr short kY = 320;
    for (short fixes : {short{0}, short{1}}) {
        for (bool no_collide : {true, false}) {
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, " : "Gameplay Fixes off, ") +
                         (no_collide ? "a NO_COLLIDE arrow" : "the control: an ordinary arrow"));
            fresh_world(fixes);
            walker* archer = living_at(FAMILY_ARCHER, 0, 300, kY);
            ASSERT_NE(nullptr, archer);
            walker* arrow = aimed_shot(archer, FACE_RIGHT, kX, kY);
            ASSERT_NE(nullptr, arrow);
            ASSERT_EQ(ACT_FIRE, static_cast<int>(arrow->act_type())) << "the staging: the arrow flies when it acts";
            ASSERT_EQ(ANI_WALK, static_cast<int>(arrow->ani_type())) << "the staging: no animation to finish first";
            ASSERT_GT(arrow->lineofsight(), 1) << "the staging: range is left after this act";
            const float step = arrow->stepsize();
            ASSERT_TRUE(world().query_grid_passable(static_cast<float>(kX) + step, kY, arrow))
                << "the staging: no wall under the next step's box";
            ASSERT_TRUE(world().query_object_passable(static_cast<float>(kX) + step, kY, arrow, arrow->floor(), true))
                << "the staging: no body under the next step's box";
            arrow->stats()->set_bit_flags(BIT_NO_COLLIDE, no_collide ? 1 : 0);
            ASSERT_EQ(no_collide, arrow->stats()->query_bit_flags(BIT_NO_COLLIDE) != 0) << "the staging: the flag";
            ASSERT_FALSE(arrow->stats()->query_bit_flags(BIT_IMMORTAL)) << "the staging: the arrow is not IMMORTAL";
            arrow->act();
            EXPECT_FLOAT_EQ(static_cast<float>(kX) + step, arrow->worldx()) << "the step was taken";
            EXPECT_EQ(nullptr, arrow->collide_ob()) << "nothing met";
            EXPECT_EQ(no_collide, arrow->dead() != 0)
                << (no_collide ? "a NO_COLLIDE shot that is not IMMORTAL is spent by its free step"
                               : "an ordinary shot flies on after a free step");
        }
    }
}

// walker::fire (walker.cpp: a fresh shot's facing is (frame + 1) % 2, "so
// first walk will just be draw") and walker::act_fire's countdown, both
// settings. An archer at the middle of the field fires an arrow along each
// facing and the arrow acts until it dies; the acts on which its position
// changed are counted. A shot whose fresh facing is not its heading spends
// its first act turning, so it moves lineofsight - 1 times; FACE_UP is 0 and
// FACE_UP_RIGHT is 1, so an UP shot from an odd shooter frame and an UP_RIGHT
// shot from an even one already face their heading and move lineofsight
// times. Each facing is fired from an even and an odd frame, and every count
// is asserted exactly. The line check (Gameplay Fixes BD) flies the steps
// every shot moves: lineofsight - 1.
TEST_F(SemLineCheck, a_fresh_shot_moves_one_step_fewer_than_its_line_of_sight)
{
    ASSERT_EQ(0, FACE_UP);
    ASSERT_EQ(1, FACE_UP_RIGHT);
    for (short fixes : {short{0}, short{1}}) {
        for (int dir = 0; dir < 8; ++dir) {
            for (short frame : {short{0}, short{1}}) {
                SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, " : "Gameplay Fixes off, ") +
                             dir_name(dir) + ", shooter frame " + std::to_string(frame));
                fresh_world(fixes);
                walker* archer = living_at(FAMILY_ARCHER, 0, 504, 504);
                ASSERT_NE(nullptr, archer);
                face(archer, dir);
                archer->set_frame(frame);
                ASSERT_EQ(frame, archer->frame()) << "the staging: the shooter's frame";
                walker* shot = archer->fire();
                ASSERT_NE(nullptr, shot) << "the staging: the arrow flies";
                // The stream is not pinned: an arrow's waver (a few pixels across a step of
                // about eleven) never takes its heading out of the facing's 45-degree sector.
                ASSERT_EQ(dir, shot->facing(shot->lastx(), shot->lasty()))
                    << "the staging: the arrow's heading faces " << dir_name(dir);
                const int los = shot->lineofsight();
                ASSERT_GT(los, 1) << "the staging: a line of sight read from the arrow";
                const bool faces_heading = ((frame + 1) % 2) == dir;
                int moves = 0;
                int acts = 0;
                while (!shot->dead() && acts < los + 4) {
                    const float x = shot->worldx();
                    const float y = shot->worldy();
                    shot->act();
                    ++acts;
                    if (shot->worldx() != x || shot->worldy() != y)
                        ++moves;
                }
                ASSERT_TRUE(shot->dead()) << "the staging: the arrow's range ran out on open ground";
                EXPECT_EQ(faces_heading ? los : los - 1, moves)
                    << (faces_heading ? "already facing its heading: lineofsight moves"
                                      : "the first act turns: lineofsight - 1 moves");
            }
        }
    }
}

// ---------------------------------------------------------------------------
// The line check flies the shot (walker.cpp fire_check, walker_movement.cpp
// walker::line_check_flight: Gameplay Fixes BD, #538)
// ---------------------------------------------------------------------------

// Gameplay Fixes BD, setting on: the check answers whether the shot lands.
// For each shooter a bot can be, each of the facings UP, RIGHT and
// DOWN_RIGHT, each sideways offset 0, 6, 12 and 18 px and every gap from 0
// px to the weapon's flight plus 16 px, one hostile soldier with a 16 px box
// stands at that gap from the shooter's face. fire_check is asked along the
// facing, then from the same staging and the same stream fire() is called
// and the shot acts until it ends; the soldier was struck when it lost hit
// points. For every gap the reach gate does not refuse (OutOfRange), the check
// passes if and only if the soldier was struck. Per shooter and facing the
// test counts what it compared and requires both outcomes (the sideways
// offsets give the misses). One world per shooter and facing serves every gap.
//
// The staging is open grass on one floor, and nothing else: no tree or shrub
// (a shot loses range there and the check does not count it), no arrow slit
// (the check takes a slit as open and the shot gambles), no air cell (a rock
// or a knife would drop a floor), and a soldier shooter is handed its knives
// before each throw (its family can refuse a throw with none left, which the
// check does not ask). The waver is pinned to the draw that gives 0, for the
// check and for the shot. A tower's own blow does nothing (its melee damage
// is 0), so every shooter is given a blow of at least 10, or a body struck on
// the pad would not show in its hit points; the check never reads it. The
// soldier target wears no armour, so every strike shows.
//
// What this test cannot tell: a check whose test weapon moves by whole pixels
// (setxy(xpos() + lastx(), ...), #82's form) answers like the shot's own
// sub-pixel flight at every gap for one body in open ground, inside the reach
// gate. That the check takes the shot's own step is kept by the one function
// they share (walker::flight_step) and by
// one_flight_step_tells_a_strike_from_a_stop_and_a_free_step. With the setting
// off this test compares nothing: the classic check's rule is
// classic_the_check_steps_over_a_foe_between_two_of_its_spots.
TEST_F(SemLineCheck, the_check_passes_exactly_where_the_shot_lands_with_the_setting_on)
{
    struct Shooter
    {
        int family;
        const char* name;
    };
    const Shooter kShooters[] = {{FAMILY_TOWER1, "tower"},      {FAMILY_ARCHER, "archer"},
                                 {FAMILY_DRUID, "druid"},       {FAMILY_GOLEM, "golem"},
                                 {FAMILY_FIREELEMENTAL, "fire elemental"},
                                 {FAMILY_FAERIE, "faerie"},     {FAMILY_MAGE, "mage"},
                                 {FAMILY_SKELETON, "skeleton"}, {FAMILY_SOLDIER, "soldier"},
                                 {FAMILY_ELF, "elf"}};
    const int kFacings[] = {FACE_UP, FACE_RIGHT, FACE_DOWN_RIGHT};
    const int kOffsets[] = {0, 6, 12, 18};
    constexpr short kX = 480;
    constexpr short kY = 480;
    for (const Shooter& sh : kShooters) {
        for (int dir : kFacings) {
            SCOPED_TRACE(std::string(sh.name) + " facing " + dir_name(dir));
            fresh_world(1);
            walker* shooter = living_at(sh.family, 0, kX, kY);
            ASSERT_NE(nullptr, shooter);
            face(shooter, dir);
            ASSERT_EQ(dir, shooter->curdir()) << "the staging: the shooter faces " << dir_name(dir);
            ASSERT_FALSE(shooter->stats()->query_bit_flags(BIT_NO_RANGED)) << "the staging: a shooter";
            if (shooter->damage() < 10.0f)
                shooter->set_damage(10.0f);
            walker* target = living_at(FAMILY_SOLDIER, 1, 64, 64, 16, 16);
            ASSERT_NE(nullptr, target);
            target->set_act_type(ACT_SIT);
            target->stats()->set_armor(0.0f);
            shooter->set_foe(target);
            const ShotPlan p = plan_of(shooter, dir);
            ASSERT_GT(p.los, 1) << "the staging: a weapon the shooter builds";
            const std::uint32_t straight = state_where({{p.waver_bound, p.waver_zero}});
            const int last_gap = static_cast<int>(p.step * static_cast<float>(p.los - 1)) + 16;
            const int w = shooter->sizex();
            const int h = shooter->sizey();
            int compared = 0;
            int hits = 0;
            int misses = 0;
            int wrong = 0;
            for (int off : kOffsets) {
                for (int gap = 0; gap <= last_gap; ++gap) {
                    int tx = 0;
                    int ty = 0;
                    if (dir == FACE_UP) {
                        tx = kX + (w - 16) / 2 + off;
                        ty = kY - gap - 16;
                    } else if (dir == FACE_RIGHT) {
                        tx = kX + w + gap;
                        ty = kY + (h - 16) / 2 + off;
                    } else {
                        const int d = off * 7 / 10;  // off px across the diagonal
                        tx = kX + w + gap + d;
                        ty = kY + h + gap - d;
                    }
                    ASSERT_GE(tx, 0) << "the staging: at gap " << gap << " px the soldier stands on the field";
                    ASSERT_GE(ty, 0) << "the staging: at gap " << gap << " px the soldier stands on the field";
                    ASSERT_LE(tx + 16, world().grid.w * GRID_SIZE)
                        << "the staging: at gap " << gap << " px the soldier stands on the field";
                    ASSERT_LE(ty + 16, world().grid.h * GRID_SIZE)
                        << "the staging: at gap " << gap << " px the soldier stands on the field";
                    place(target, static_cast<short>(tx), static_cast<short>(ty), 16, 16);
                    target->stats()->set_hitpoints(400.0f);
                    shooter->stats()->set_magicpoints(500.0f);
                    shooter->set_weapons_left(4);
                    world().rng_.state_ = straight;
                    Denial why = Denial::NoFoe;
                    const bool passes =
                        shooter->fire_check(static_cast<short>(kStep[dir][0]), static_cast<short>(kStep[dir][1]), &why);
                    drop_weapons();
                    if (why == Denial::OutOfRange)
                        continue;
                    world().rng_.state_ = straight;
                    shooter->stats()->set_magicpoints(500.0f);
                    const bool ended = fire_and_fly(shooter, 4 * p.los);
                    const bool struck = target->stats()->hitpoints() < 400.0f;
                    drop_weapons();
                    ASSERT_TRUE(ended) << "the staging: the shot ends inside its range";
                    ++compared;
                    (struck ? hits : misses) += 1;
                    if (struck != passes && ++wrong <= 5)
                        ADD_FAILURE() << "gap " << gap << " px, offset " << off << " px: the check "
                                      << (passes ? "passes" : "refuses") << " (denial "
                                      << static_cast<int>(why) << ") but the shot "
                                      << (struck ? "strikes" : "misses");
                }
            }
            EXPECT_EQ(0, wrong) << "gaps where the check and the shot disagree, of " << compared;
            EXPECT_GT(hits, 0) << "the staging: some gaps where the shot strikes";
            EXPECT_GT(misses, 0) << "the staging: some gaps where the shot misses";
        }
    }
}

// Gameplay Fixes BD, setting on: the check flies as many steps as a fresh
// shot moves, and not one more. An archer facing right: a fresh shot's facing
// is 0 or 1 (UP or UP_RIGHT) and FACE_RIGHT is 2, so its first act always
// turns it and it moves lineofsight - 1 steps. Its foe, a sitting soldier,
// stands inside the reach gate and off every box the flight tests (and off
// the box of one step more), so the foe decides nothing. A second hostile
// soldier sits on the line far out, where only the end of the flight reaches
// (the reach gate asks about the foe alone, so a body that is not the foe can
// stand there in play). Each spot is searched from the arrow's numbers and
// asserted:
//  (i) only the box of the last step a fresh shot takes meets it, no earlier
//      box: the check passes, and a fired arrow (waver 0) strikes it;
//  (ii) further out, where no box of the flight meets it but the box of one
//      step more would: the check refuses (RayMiss), and a fired arrow leaves
//      its hit points as they were.
// A check that flew one step more would pass in (ii); one that flew one step
// fewer would refuse in (i). With the setting off the classic loop runs; its
// rule is classic_the_check_steps_over_a_foe_between_two_of_its_spots.
TEST_F(SemLineCheck, the_check_flies_as_far_as_a_fresh_shot_and_no_further_with_the_setting_on)
{
    constexpr short kX = 480;
    constexpr short kY = 480;
    ASSERT_GT(static_cast<int>(FACE_RIGHT), 1) << "the staging: no fresh shot already faces right";
    for (bool last : {true, false}) {
        SCOPED_TRACE(last ? "(i) met by the last step's box only" : "(ii) met by the box of one step more only");
        fresh_world(1);
        walker* archer = living_at(FAMILY_ARCHER, 0, kX, kY);
        ASSERT_NE(nullptr, archer);
        face(archer, FACE_RIGHT);
        const ShotPlan p = plan_of(archer, FACE_RIGHT);
        ASSERT_GT(p.los, 2);
        const std::vector<Box> flown = flown_boxes(p);
        ShotPlan one_more = p;
        one_more.los = p.los + 1;
        const std::vector<Box> flown_one_more = flown_boxes(one_more);
        ASSERT_EQ(flown.size() + 1, flown_one_more.size());
        const Box further = flown_one_more.back();  // the box the step after the last one tests
        // The far soldier's spot: centred on the line, the first x past the pad's side where
        // the case's boxes meet it.
        const short fy = static_cast<short>(p.pad_y + p.sy / 2 - 8);
        const auto box_hits = [&](const Box& b, int x) {
            return ::collide(static_cast<short>(b.x), static_cast<short>(b.y), p.sx, p.sy, static_cast<short>(x), fy,
                             16, 16) != 0;
        };
        int far_x = -1;
        for (int x = flown.front().x; x <= further.x + p.sx && far_x < 0; ++x) {
            bool earlier = false;
            for (std::size_t k = 0; k + 1 < flown.size(); ++k)
                earlier = earlier || box_hits(flown[k], x);
            const bool at_last = box_hits(flown.back(), x);
            if (last ? (at_last && !earlier) : (!at_last && !earlier && box_hits(further, x)))
                far_x = x;
        }
        ASSERT_GE(far_x, 0) << "the staging: a spot for the far soldier";
        walker* far = living_at(FAMILY_SOLDIER, 1, static_cast<short>(far_x), fy, 16, 16);
        ASSERT_NE(nullptr, far);
        far->set_act_type(ACT_SIT);
        far->stats()->set_armor(0.0f);
        for (std::size_t k = 0; k + 1 < flown.size(); ++k)
            ASSERT_FALSE(box_meets(flown[k], p.sx, p.sy, far)) << "the staging: no earlier box meets it (step " << k + 1 << ")";
        if (last) {
            ASSERT_TRUE(box_meets(flown.back(), p.sx, p.sy, far)) << "the staging: the last step's box meets it";
        } else {
            ASSERT_FALSE(box_meets(flown.back(), p.sx, p.sy, far)) << "the staging: the last step's box does not meet it";
            ASSERT_TRUE(box_meets(further, p.sx, p.sy, far)) << "the staging: the box of one step more would";
        }
        walker* foe = living_at(FAMILY_SOLDIER, 1, static_cast<short>(p.pad_x + 10),
                                static_cast<short>(kY + archer->sizey() + 8), 16, 16);
        ASSERT_NE(nullptr, foe);
        foe->set_act_type(ACT_SIT);
        archer->set_foe(foe);
        ASSERT_LE(archer->distance_to_ob(foe), p.reach) << "the staging: the foe is inside the reach gate";
        for (const Box& b : flown_one_more)
            ASSERT_FALSE(box_meets(b, p.sx, p.sy, foe)) << "the staging: the foe is off the flight's line";
        const std::uint32_t straight = state_where({{p.waver_bound, p.waver_zero}});
        world().rng_.state_ = straight;
        Denial why = Denial::NoFoe;
        const bool passes = ask(archer, 1, 0, &why);
        drop_weapons();
        if (last) {
            EXPECT_TRUE(passes) << "the last step a fresh shot takes meets the far soldier";
            EXPECT_EQ(Denial::None, why);
        } else {
            EXPECT_FALSE(passes) << "only a step no fresh shot takes would meet the far soldier";
            EXPECT_EQ(Denial::RayMiss, why);
        }
        world().rng_.state_ = straight;
        ASSERT_TRUE(fire_and_fly(archer, 4 * p.los)) << "the staging: the arrow ends inside its range";
        drop_weapons();
        if (last)
            EXPECT_LT(far->stats()->hitpoints(), 400.0f) << "the arrow strikes it on its last step";
        else
            EXPECT_FLOAT_EQ(400.0f, far->stats()->hitpoints()) << "the arrow ends one step short of it";
        EXPECT_FLOAT_EQ(400.0f, foe->stats()->hitpoints()) << "the staging: the foe is off the line";
    }
}

// fire_check, the classic loop (walker.cpp), and line_check_flight's gate.
// A tower facing up on a field 160 tiles tall (every spot of the classic loop
// stays on the map, asserted spot by spot) and a 16 px soldier on the shot's
// line. The test computes the classic spots itself, from a probe's pad and
// step (p[0] the pad, p[i] = trunc(p[i-1] + i * step)), and picks the first
// gap inside the reach gate at which no spot's box meets the soldier; a fired
// arrow (waver 0) strikes it there. Setting off: refused, RayMiss. Setting
// on: passes. Control, the first gap at which a classic spot's box meets the
// soldier: passes on both settings.
TEST_F(SemLineCheck, classic_the_check_steps_over_a_foe_between_two_of_its_spots)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes, 64, 160);
        walker* tower = living_at(FAMILY_TOWER1, 0, 496, 2304);
        ASSERT_NE(nullptr, tower);
        face(tower, FACE_UP);
        walker* body = living_at(FAMILY_SOLDIER, 1, 64, 64, 16, 16);
        ASSERT_NE(nullptr, body);
        body->set_act_type(ACT_SIT);
        tower->set_foe(body);
        const ShotPlan p = plan_of(tower, FACE_UP);
        ASSERT_GT(p.los, 1);
        const std::vector<Box> spots = classic_spots(p);
        const int map_h = world().grid.h * GRID_SIZE;
        for (const Box& b : spots)
            ASSERT_TRUE(b.y >= 0 && b.y + p.sy < map_h) << "the staging: every classic spot is on the map (y " << b.y << ")";
        const short body_x = static_cast<short>(p.pad_x + p.sx / 2 - 8);
        const auto at_gap = [&](int gap) {
            place(body, body_x, static_cast<short>(tower->ypos() - gap - 16), 16, 16);
        };
        const auto classic_meets = [&] {
            for (const Box& b : spots)
                if (box_meets(b, p.sx, p.sy, body))
                    return true;
            return false;
        };
        // The first gap (from the tower's face to the soldier's near edge) inside the reach gate
        // at which no classic spot's box meets the soldier, and the first at which one does.
        struct
        {
            int blind = -1;
            int seen = -1;
        } g;
        for (int gap = 0; g.blind < 0 || g.seen < 0; ++gap) {
            at_gap(gap);
            if (tower->distance_to_ob(body) > p.reach)
                break;
            const bool met = classic_meets();
            if (!met && g.blind < 0)
                g.blind = gap;
            if (met && g.seen < 0)
                g.seen = gap;
        }
        ASSERT_GE(g.blind, 0) << "the staging: a gap inside the reach that no classic spot meets";
        ASSERT_GE(g.seen, 0) << "the staging: a gap a classic spot meets";
        const std::uint32_t straight = state_where({{p.waver_bound, p.waver_zero}});

        at_gap(g.blind);
        ASSERT_LE(tower->distance_to_ob(body), p.reach) << "the staging: inside the reach gate";
        ASSERT_FALSE(classic_meets()) << "the staging: between two classic spots";
        world().rng_.state_ = straight;
        Denial why = Denial::None;
        const bool passes = ask(tower, 0, -1, &why);
        drop_weapons();
        if (fixes) {
            EXPECT_TRUE(passes) << "gap " << g.blind << ": the flown check meets the soldier";
            EXPECT_EQ(Denial::None, why);
        } else {
            EXPECT_FALSE(passes) << "gap " << g.blind << ": the classic check steps over the soldier";
            EXPECT_EQ(Denial::RayMiss, why);
        }
        world().rng_.state_ = straight;
        ASSERT_TRUE(fire_and_fly(tower, 4 * p.los));
        drop_weapons();
        EXPECT_LT(body->stats()->hitpoints(), 400.0f) << "the staging: a fired arrow strikes the soldier there";

        at_gap(g.seen);
        body->stats()->set_hitpoints(400.0f);
        world().rng_.state_ = straight;
        why = Denial::NoFoe;
        EXPECT_TRUE(ask(tower, 0, -1, &why)) << "control, gap " << g.seen << ": a classic spot meets it";
        EXPECT_EQ(Denial::None, why);
        drop_weapons();
    }
}

// line_check_flight's pad questions, on a DIAGONAL pad, where the pad
// question decides alone: for a cardinal facing the first flight step meets
// the same tile or body as the pad. An archer facing up and right; the pad
// fire() uses and the box of the shot's first step are computed from the
// arrow's numbers, and the tile and body below are placed under the pad's box
// and clear of the first step's box (asserted). (i) A wall tile under the pad:
// WallBlocked on both settings, and a fired shot is wasted (no weapon flies,
// its magic is spent). (ii) A hostile body under the pad: passes on both
// settings, and fire() lands the blow. The foe of (i) stands in reach but off
// the flight's line.
TEST_F(SemLineCheck, the_pad_passes_on_a_body_and_refuses_on_a_wall_on_both_settings)
{
    constexpr short kX = 320;
    constexpr short kY = 329;
    for (short fixes : {short{0}, short{1}}) {
        for (bool wall : {true, false}) {
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, " : "Gameplay Fixes off, ") +
                         (wall ? "(i) a wall under the pad" : "(ii) a body under the pad"));
            fresh_world(fixes);
            walker* archer = living_at(FAMILY_ARCHER, 0, kX, kY);
            ASSERT_NE(nullptr, archer);
            face(archer, FACE_UP_RIGHT);
            const ShotPlan p = plan_of(archer, FACE_UP_RIGHT);
            ASSERT_GT(p.los, 1);
            const Box pad{p.pad_x, p.pad_y};
            const Box first = flown_boxes(p).front();
            walker* foe = nullptr;
            if (wall) {
                // The tile under the pad box's bottom-left pixel; the first step's box lies a
                // whole box up and right of the pad's.
                const int ttx = pad.x / GRID_SIZE;
                const int tty = (pad.y + p.sy - 1) / GRID_SIZE;
                tile(ttx, tty, PIX_H_WALL1);
                walker* probe = archer->create_weapon();
                ASSERT_NE(nullptr, probe);
                ASSERT_FALSE(world().query_grid_passable(static_cast<float>(pad.x), static_cast<float>(pad.y), probe)) << "the staging: the wall is under the pad";
                ASSERT_TRUE(world().query_grid_passable(static_cast<float>(first.x), static_cast<float>(first.y), probe))
                    << "the staging: the first step's box is clear of it";
                world().remove_ob(probe);
                foe = living_at(FAMILY_SOLDIER, 1, static_cast<short>(kX + 40), static_cast<short>(kY + 40), 16, 16);
                ASSERT_NE(nullptr, foe);
                for (const Box& b : flown_boxes(p))
                    ASSERT_FALSE(box_meets(b, p.sx, p.sy, foe)) << "the staging: the foe is off the flight's line";
            } else {
                foe = living_at(FAMILY_SOLDIER, 1, static_cast<short>(pad.x + 3), static_cast<short>(pad.y + 3), 16, 16);
                ASSERT_NE(nullptr, foe);
                ASSERT_TRUE(box_meets(pad, p.sx, p.sy, foe)) << "the staging: the body is under the pad's box";
                for (const Box& b : flown_boxes(p))
                    ASSERT_FALSE(box_meets(b, p.sx, p.sy, foe)) << "the staging: and under no step's box";
                ASSERT_FALSE(meets(archer, archer->xpos(), archer->ypos(), foe)) << "the staging: clear of the archer";
            }
            foe->set_act_type(ACT_SIT);
            archer->set_foe(foe);
            ASSERT_LE(archer->distance_to_ob(foe), p.reach) << "the staging: inside the reach gate";
            const std::uint32_t straight = state_where({{p.waver_bound, p.waver_zero}});
            world().rng_.state_ = straight;
            Denial why = Denial::NoFoe;
            const bool passes = ask(archer, 1, -1, &why);
            drop_weapons();
            if (wall) {
                EXPECT_FALSE(passes) << "a wall on the pad: the shot would be wasted";
                EXPECT_EQ(Denial::WallBlocked, why);
            } else {
                EXPECT_TRUE(passes) << "a body on the pad: the blow lands";
                EXPECT_EQ(Denial::None, why);
            }
            world().rng_.state_ = straight;
            const float mp = archer->stats()->magicpoints();
            walker* shot = archer->fire();
            EXPECT_EQ(nullptr, shot) << "no shot leaves a pad that is held";
            EXPECT_EQ(0, live_weapons()) << "nothing flies";
            EXPECT_FLOAT_EQ(mp - archer->stats()->weapon_cost(), archer->stats()->magicpoints()) << "the cost is paid";
            if (wall)
                EXPECT_FLOAT_EQ(400.0f, foe->stats()->hitpoints()) << "a wall on the pad: the shot is wasted";
            else
                EXPECT_LT(foe->stats()->hitpoints(), 400.0f) << "a body on the pad: fire() lands the blow";
            drop_weapons();
        }
    }
}

// The issue's case (#538): a tower whose top row is y 560 facing up, a wall
// tile on rows 448 to 463 across the shot's line, a 16 px soldier on rows 423
// to 438. The soldier is inside the reach gate, the classic spots (computed
// as above) jump the wall and one of them meets the soldier, and a flown step
// meets the wall before any flown step meets the soldier (all asserted).
// Setting on: WallBlocked. Setting off: passes. Both: a fired arrow (waver 0)
// stops on the wall and leaves the soldier's hit points as they were.
TEST_F(SemLineCheck, a_wall_the_shot_stops_on_refuses_with_the_setting_on)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes);
        walker* tower = living_at(FAMILY_TOWER1, 0, 500, 560);
        ASSERT_NE(nullptr, tower);
        face(tower, FACE_UP);
        const ShotPlan p = plan_of(tower, FACE_UP);
        ASSERT_GT(p.los, 1);
        for (int tx = p.pad_x / GRID_SIZE; tx <= (p.pad_x + p.sx - 1) / GRID_SIZE; ++tx)
            tile(tx, 448 / GRID_SIZE, PIX_H_WALL1);
        walker* body = living_at(FAMILY_SOLDIER, 1, static_cast<short>(p.pad_x + p.sx / 2 - 8), 423, 16, 16);
        ASSERT_NE(nullptr, body);
        body->set_act_type(ACT_SIT);
        tower->set_foe(body);
        ASSERT_LE(tower->distance_to_ob(body), p.reach) << "the staging: inside the reach gate";
        walker* probe = tower->create_weapon();
        ASSERT_NE(nullptr, probe);
        bool classic_meets = false;
        for (const Box& b : classic_spots(p)) {
            if (box_meets(b, p.sx, p.sy, body)) {
                classic_meets = true;
                break;
            }
            ASSERT_TRUE(world().query_grid_passable(static_cast<float>(b.x), static_cast<float>(b.y), probe))
                << "the staging: the classic spot at y " << b.y << " is clear of the wall";
        }
        ASSERT_TRUE(classic_meets) << "the staging: a classic spot past the wall meets the soldier";
        bool walled_first = false;
        for (const Box& b : flown_boxes(p)) {
            ASSERT_FALSE(box_meets(b, p.sx, p.sy, body)) << "the staging: no flown step reaches the soldier first";
            if (!world().query_grid_passable(static_cast<float>(b.x), static_cast<float>(b.y), probe)) {
                walled_first = true;
                break;
            }
        }
        world().remove_ob(probe);
        ASSERT_TRUE(walled_first) << "the staging: a flown step meets the wall";
        const std::uint32_t straight = state_where({{p.waver_bound, p.waver_zero}});
        world().rng_.state_ = straight;
        Denial why = Denial::NoFoe;
        const bool passes = ask(tower, 0, -1, &why);
        drop_weapons();
        if (fixes) {
            EXPECT_FALSE(passes) << "the shot would stop on the wall";
            EXPECT_EQ(Denial::WallBlocked, why);
        } else {
            EXPECT_TRUE(passes) << "the classic check steps over the wall";
            EXPECT_EQ(Denial::None, why);
        }
        world().rng_.state_ = straight;
        ASSERT_TRUE(fire_and_fly(tower, 4 * p.los));
        drop_weapons();
        EXPECT_FLOAT_EQ(400.0f, body->stats()->hitpoints()) << "the arrow stops on the wall";
    }
}

// The barrage arena of the parity rows (#538's worked case): a tower at
// (173, 60) facing up at an archer at (176, 2) on the map's top row. Setting
// on: passes, the flight meets the archer before the edge. Setting off:
// WallBlocked, a classic spot is off the map before any spot meets the archer
// (asserted). And the case the name promises: the archer moved sideways out
// of every box the flight tests, still inside the reach gate (both asserted).
// Setting on: WallBlocked, because the flight leaves the map before its steps
// run out (the step at which it does is computed and asserted to be under
// the arrow's line of sight).
TEST_F(SemLineCheck, the_map_edge_refuses_only_when_the_shot_would_leave_the_map)
{
    for (short fixes : {short{0}, short{1}}) {
        for (bool moved : {false, true}) {
            if (moved && !fixes)
                continue;
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off") +
                         (moved ? ", the archer moved off the line" : ", the barrage arena"));
            fresh_world(fixes);
            walker* tower = living_at(FAMILY_TOWER1, 0, 173, 60);
            ASSERT_NE(nullptr, tower);
            face(tower, FACE_UP);
            const ShotPlan p = plan_of(tower, FACE_UP);
            ASSERT_GT(p.los, 1);
            const short archer_x = moved ? static_cast<short>(p.pad_x + p.sx + 12) : short{176};
            walker* archer = living_at(FAMILY_ARCHER, 1, archer_x, 2, 16, 16);
            ASSERT_NE(nullptr, archer);
            archer->set_act_type(ACT_SIT);
            tower->set_foe(archer);
            ASSERT_LE(tower->distance_to_ob(archer), p.reach) << "the staging: inside the reach gate";
            // The step at which the flight's box would leave the map (walker::walk: a step whose
            // top-left would go below 0 is not taken).
            int leaves_at = -1;
            float wy = p.pad_y;
            bool met_first = false;
            for (int k = 1; k < p.los; ++k) {
                const short yp = static_cast<short>(wy);
                if (static_cast<float>(yp) + p.ly < 0.0f) {
                    leaves_at = k;
                    break;
                }
                const Box b{static_cast<std::int32_t>(static_cast<float>(p.pad_x) + p.lx),
                            static_cast<std::int32_t>(static_cast<float>(yp) + p.ly)};
                if (box_meets(b, p.sx, p.sy, archer)) {
                    met_first = true;
                    break;
                }
                wy += p.ly;
            }
            if (moved) {
                ASSERT_FALSE(met_first) << "the staging: no box the flight tests meets the archer";
                ASSERT_GT(leaves_at, 0) << "the staging: the flight leaves the map";
                ASSERT_LT(leaves_at, p.los) << "the staging: before its steps run out";
            } else {
                ASSERT_TRUE(met_first) << "the staging: the flight meets the archer before the edge";
                bool off_first = false;
                for (const Box& b : classic_spots(p)) {
                    ASSERT_FALSE(box_meets(b, p.sx, p.sy, archer)) << "the staging: no classic spot meets the archer";
                    if (b.y < 0) {
                        off_first = true;
                        break;
                    }
                }
                ASSERT_TRUE(off_first) << "the staging: a classic spot leaves the map";
            }
            world().rng_.state_ = state_where({{p.waver_bound, p.waver_zero}});
            Denial why = Denial::NoFoe;
            const bool passes = ask(tower, 0, -1, &why);
            if (fixes && !moved) {
                EXPECT_TRUE(passes) << "the flight meets the archer before the edge";
                EXPECT_EQ(Denial::None, why);
            } else {
                EXPECT_FALSE(passes) << (moved ? "the flight leaves the map at step " + std::to_string(leaves_at)
                                               : std::string("a classic spot is off the map"));
                EXPECT_EQ(Denial::WallBlocked, why);
            }
        }
    }
}

// fire_check's random stream in open ground, both settings: the waver it
// draws for its probe (walker::set_weapon_heading) and nothing else. A
// level-4 soldier's knife (a short weapon: the classic loop's last spot is
// 36 steps of about 7 px past the pad, still on the 1024 px field, so a
// RayMiss is reachable on both settings). A foe straight ahead (passes) and a
// foe in reach but off every box both loops test (RayMiss); the stream after
// each equals the waver's one draw, its bound derived from the knife.
TEST_F(SemLineCheck, the_check_draws_its_waver_and_nothing_else_in_open_ground)
{
    for (short fixes : {short{0}, short{1}}) {
        for (bool on_line : {true, false}) {
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, " : "Gameplay Fixes off, ") +
                         (on_line ? "a foe on the line" : "a foe off the line"));
            fresh_world(fixes);
            walker* soldier = living_at(FAMILY_SOLDIER, 0, 480, 480);
            ASSERT_NE(nullptr, soldier);
            face(soldier, FACE_RIGHT);
            const ShotPlan p = plan_of(soldier, FACE_RIGHT);
            ASSERT_GT(p.los, 1);
            const short fy = on_line ? static_cast<short>(p.pad_y + p.sy / 2 - 8) : static_cast<short>(480 + 24);
            walker* foe = living_at(FAMILY_ORC, 1, static_cast<short>(p.pad_x + 10), fy, 16, 16);
            ASSERT_NE(nullptr, foe);
            foe->set_act_type(ACT_SIT);
            soldier->set_foe(foe);
            ASSERT_LE(soldier->distance_to_ob(foe), p.reach) << "the staging: inside the reach gate";
            const std::vector<Box> spots = classic_spots(p);
            for (const Box& b : spots)
                ASSERT_LT(b.x + p.sx, world().grid.w * GRID_SIZE) << "the staging: every classic spot is on the map";
            if (!on_line) {
                for (const Box& b : spots)
                    ASSERT_FALSE(box_meets(b, p.sx, p.sy, foe)) << "the staging: off every classic spot";
                for (const Box& b : flown_boxes(p))
                    ASSERT_FALSE(box_meets(b, p.sx, p.sy, foe)) << "the staging: off every flown box";
            }
            const std::uint32_t s = state_where({{p.waver_bound, p.waver_zero}});
            world().rng_.state_ = s;
            Denial why = Denial::NoFoe;
            const bool passes = ask(soldier, 1, 0, &why);
            EXPECT_EQ(on_line, passes);
            EXPECT_EQ(on_line ? Denial::None : Denial::RayMiss, why);
            EXPECT_EQ(state_after(s, {p.waver_bound}), world().rng_.state_) << "the waver, and nothing else";
        }
    }
}

// Gameplay Fixes BD, setting on, a deliberate rule: the check meets on its
// line what the shot would meet, so an enemy weapon-order body on the line, a
// weapon in flight or a grown tree, is met as the shot would meet it; Gameplay
// Fixes BF leaves both so (only a door is met without the roll). No living on
// the line; one such body parked in a 32 px collision cell the flight crosses
// beyond the pad, where the first flight box that covers the cell also meets
// it: an enemy arrow in the line check's arrow box at that box's spot, or an
// enemy grown tree (FAMILY_TREE, weapon order) in the family's own box with
// its bottom row on that box's (each place computed from the arrow's step and
// asserted). A weapon meeting an enemy weapon draws rng(10) for each covered
// cell holding it and is stopped by it only on a roll of 3 or less
// (obmap.cpp). Stream pinned so that roll is lost at the first box: the check
// passes ("we hit an enemy") on that body, and the stream after is the waver
// and that one roll. Pinned so every roll is won: RayMiss, and the stream
// after is the waver and exactly one rng(10) per flight box whose cells hold
// the body, a count the test derives. The foe stands in reach, off the
// flight's line.
TEST_F(SemLineCheck, an_enemy_weapon_on_the_line_is_met_on_its_clash_roll_with_the_setting_on)
{
    constexpr int kCell = 32;  // obmap.cpp OBRES: the collision table's cell
    struct Parked
    {
        int family;
        const char* name;
    };
    for (const Parked parked_as : {Parked{FAMILY_ARROW, "an enemy arrow"}, Parked{FAMILY_TREE, "an enemy grown tree"}}) {
        for (bool lost : {true, false}) {
            SCOPED_TRACE(std::string(parked_as.name) +
                         (lost ? ", the clash roll lost at the first box" : ", every clash roll won"));
            fresh_world(1);
            walker* archer = living_at(FAMILY_ARCHER, 0, 512, 640);
            ASSERT_NE(nullptr, archer);
            face(archer, FACE_UP);
            const ShotPlan p = plan_of(archer, FACE_UP);
            ASSERT_GT(p.los, 1);
            const std::vector<Box> boxes = flown_boxes(p);
            const auto rows = [&](int y) { return std::pair<int, int>{y / kCell, (y + p.sy) / kCell}; };
            const auto cols = [&](int x) { return std::pair<int, int>{x / kCell, (x + p.sx) / kCell}; };
            // The first flight box that enters a cell row no earlier box (nor the pad) covers, with
            // room in that row for the parked body's box.
            int k = -1;
            int seen_row = rows(p.pad_y).first;
            for (int i = 0; i < static_cast<int>(boxes.size()); ++i) {
                const int r = rows(boxes[static_cast<std::size_t>(i)].y).first;
                if (r < seen_row && rows(boxes[static_cast<std::size_t>(i)].y).second == r) {
                    k = i;
                    break;
                }
                seen_row = std::min(seen_row, r);
            }
            ASSERT_GE(k, 0) << "the staging: a flight box that enters a fresh cell row whole";
            const Box at = boxes[static_cast<std::size_t>(k)];
            walker* parked = world().add_ob(Order::Weapon, parked_as.family);
            ASSERT_NE(nullptr, parked);
            ASSERT_EQ(Order::Weapon, parked->query_order()) << "the staging: a weapon-order body";
            parked->set_team_num(1);
            parked->set_act_type(ACT_SIT);
            const short bw = parked_as.family == FAMILY_ARROW ? p.sx : parked->sizex();
            const short bh = parked_as.family == FAMILY_ARROW ? p.sy : parked->sizey();
            ASSERT_GT(bw, 0) << "the staging: the family's own box";
            ASSERT_GT(bh, 0);
            place(parked, static_cast<short>(at.x), static_cast<short>(at.y + p.sy - bh), bw, bh);
            ASSERT_EQ(parked->xpos() / kCell, (parked->xpos() + bw) / kCell) << "the staging: the parked body is in one cell";
            ASSERT_EQ(parked->ypos() / kCell, (parked->ypos() + bh) / kCell);
            const int cell_c = parked->xpos() / kCell;
            const int cell_r = parked->ypos() / kCell;
            ASSERT_EQ(rows(at.y).first, cell_r) << "the staging: in the row that flight box enters";
            ASSERT_TRUE(box_meets(at, p.sx, p.sy, parked)) << "the staging: the first box over its cell meets it";
            ASSERT_FALSE(rows(p.pad_y).first <= cell_r && cell_r <= rows(p.pad_y).second) << "the staging: beyond the pad's cells";
            int covering = 0;
            for (const Box& b : boxes)
                if (cols(b.x).first <= cell_c && cell_c <= cols(b.x).second && rows(b.y).first <= cell_r &&
                    cell_r <= rows(b.y).second)
                    ++covering;
            ASSERT_GE(covering, 1);
            walker* foe = living_at(FAMILY_ORC, 1, static_cast<short>(512 + 60), 640, 16, 16);
            ASSERT_NE(nullptr, foe);
            foe->set_act_type(ACT_SIT);
            archer->set_foe(foe);
            ASSERT_LE(archer->distance_to_ob(foe), p.reach) << "the staging: inside the reach gate";
            for (const Box& b : boxes)
                ASSERT_FALSE(box_meets(b, p.sx, p.sy, foe)) << "the staging: the foe is off the line";
            std::uint32_t s = 0;
            if (lost)
                s = state_where_then(p.waver_bound, p.waver_zero, 10, 1, [](std::uint32_t r) { return r <= 3; });
            else
                s = state_where_then(p.waver_bound, p.waver_zero, 10, covering, [](std::uint32_t r) { return r > 3; });
            world().rng_.state_ = s;
            Denial why = Denial::NoFoe;
            const bool passes = ask(archer, 0, -1, &why);  // the parked body stays live: compared before and after
            og::sim::SimRandom expect(s);
            (void)expect.next(p.waver_bound);
            for (int i = 0; i < (lost ? 1 : covering); ++i)
                (void)expect.next(10);
            if (lost) {
                EXPECT_TRUE(passes) << "the probe is stopped by " << parked_as.name << ": the shot would strike it";
                EXPECT_EQ(Denial::None, why);
                const walker* probe = last_probe(archer);
                ASSERT_NE(nullptr, probe);
                EXPECT_EQ(parked, probe->collide_ob()) << "stopped by the parked body";
            } else {
                EXPECT_FALSE(passes) << "every roll won: the probe flies on and finds nobody";
                EXPECT_EQ(Denial::RayMiss, why);
            }
            EXPECT_EQ(expect.state_, world().rng_.state_)
                << "the waver and " << (lost ? 1 : covering) << " clash roll(s), one per flight box over the cell";
        }
    }
}

// Gameplay Fixes BD: the mode ball (an ignored, live, hostile foe that is
// not in the collision table) on the line, as an archer's foe. (a) On a
// DIAGONAL pad, where the pad's question decides alone (for a cardinal
// facing the first step would meet the same ball): the archer faces up and
// right, and the ball's box meets the pad's and neither any box a step tests
// nor any place the flight stands after a step (asserted). (b) Facing up,
// three steps out: its box meets the flight's position after its third step
// and none before (asserted). Setting on: passes in both. Setting off: passes
// exactly when a classic spot's box meets it (computed from the classic
// spots), else RayMiss.
TEST_F(SemLineCheck, a_mode_ball_on_the_line_passes_the_flown_check)
{
    for (short fixes : {short{0}, short{1}}) {
        for (bool on_pad : {true, false}) {
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, " : "Gameplay Fixes off, ") +
                         (on_pad ? "(a) the ball on a diagonal pad" : "(b) the ball three steps out"));
            fresh_world(fixes);
            const int dir = on_pad ? FACE_UP_RIGHT : FACE_UP;
            walker* archer = living_at(FAMILY_ARCHER, 0, 512, 640);
            ASSERT_NE(nullptr, archer);
            face(archer, dir);
            const ShotPlan p = plan_of(archer, dir);
            ASSERT_GT(p.los, 4);
            // Where the flight stands after each step (its whole-pixel position).
            std::vector<Box> at{{p.pad_x, p.pad_y}};
            float wx = p.pad_x;
            float wy = p.pad_y;
            for (int k = 1; k < p.los; ++k) {
                wx += p.lx;
                wy += p.ly;
                at.push_back({static_cast<short>(wx), static_cast<short>(wy)});
            }
            const short bx = on_pad ? static_cast<short>(p.pad_x + 3) : static_cast<short>(p.pad_x + p.sx / 2 - 8);
            const short by = on_pad ? static_cast<short>(p.pad_y + 3) : static_cast<short>(at[2].y - 15);
            walker* ball = living_at(FAMILY_ORC, 1, bx, by, 16, 16);
            ASSERT_NE(nullptr, ball);
            ball->set_act_type(ACT_SIT);
            ball->set_ignore(1);
            world().myobmap->remove(ball);
            ASSERT_TRUE(world().myobmap->walker_to_pos.find(ball) == world().myobmap->walker_to_pos.end())
                << "the staging: the ball is not in the collision table";
            if (on_pad) {
                ASSERT_TRUE(box_meets(at[0], p.sx, p.sy, ball)) << "the staging: the ball is on the pad";
                for (std::size_t k = 1; k < at.size(); ++k)
                    ASSERT_FALSE(box_meets(at[k], p.sx, p.sy, ball))
                        << "the staging: and not where the flight stands after step " << k;
                for (const Box& b : flown_boxes(p))
                    ASSERT_FALSE(box_meets(b, p.sx, p.sy, ball)) << "the staging: and under no step's box";
                ASSERT_FALSE(meets(archer, archer->xpos(), archer->ypos(), ball)) << "the staging: clear of the archer";
            } else {
                for (int k = 0; k <= 2; ++k)
                    ASSERT_FALSE(box_meets(at[static_cast<std::size_t>(k)], p.sx, p.sy, ball))
                        << "the staging: not met after step " << k;
                ASSERT_TRUE(box_meets(at[3], p.sx, p.sy, ball)) << "the staging: met after the third step";
            }
            archer->set_foe(ball);
            ASSERT_LE(archer->distance_to_ob(ball), p.reach);
            bool classic_meets = false;
            for (const Box& b : classic_spots(p))
                classic_meets = classic_meets || box_meets(b, p.sx, p.sy, ball);
            world().rng_.state_ = state_where({{p.waver_bound, p.waver_zero}});
            Denial why = Denial::NoFoe;
            const bool passes = ask(archer, static_cast<short>(kStep[dir][0]), static_cast<short>(kStep[dir][1]), &why);
            const bool expected = fixes ? true : classic_meets;
            EXPECT_EQ(expected, passes) << (fixes ? "the flown check meets the ball"
                                                  : (classic_meets ? "a classic spot meets the ball"
                                                                   : "no classic spot meets the ball"));
            EXPECT_EQ(expected ? Denial::None : Denial::RayMiss, why);
        }
    }
}

// walker::act_guard and the FIRE order through fire_check, in a ticking
// world. The geometry of classic_the_check_steps_over_a_foe_between_two_of_its_spots:
// a hold-post tower (a guard that keeps its post asks a FIRE order every act)
// and a soldier that does not act, standing between two classic spots. The
// world ticks; after each tick the weapon list is searched for a live arrow
// whose owner is the tower (found again by its entity id, never by a pointer
// kept across a tick). Setting on: the tower has started a shot within its
// fire delay plus three acts. Setting off: sixty ticks, no shot.
TEST_F(SemLineCheck, a_tower_shoots_a_still_foe_between_two_classic_spots_only_with_the_setting_on)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes, 64, 160);
        walker* tower = living_at(FAMILY_TOWER1, 0, 496, 2304);
        ASSERT_NE(nullptr, tower);
        face(tower, FACE_UP);
        walker* body = living_at(FAMILY_SOLDIER, 1, 64, 64, 16, 16);
        ASSERT_NE(nullptr, body);
        body->set_act_type(ACT_SIT);
        const ShotPlan p = plan_of(tower, FACE_UP);
        ASSERT_GT(p.los, 1);
        const std::vector<Box> spots = classic_spots(p);
        const short body_x = static_cast<short>(p.pad_x + p.sx / 2 - 8);
        int blind = -1;
        for (int gap = 0; blind < 0; ++gap) {
            place(body, body_x, static_cast<short>(tower->ypos() - gap - 16), 16, 16);
            ASSERT_LE(tower->distance_to_ob(body), p.reach) << "the staging: a blind gap inside the reach";
            bool met = false;
            for (const Box& b : spots)
                met = met || box_meets(b, p.sx, p.sy, body);
            if (!met)
                blind = gap;
        }
        tower->set_act_type(ACT_GUARD);
        tower->set_guard_hold_post(true);
        ASSERT_TRUE(tower->guard_hold_post());
        const std::uint32_t tower_id = tower->entity_id();
        const std::uint32_t body_id = body->entity_id();
        const int budget = fixes ? static_cast<int>(std::ceil(tower->fire_frequency())) + 3 : 60;
        const auto tower_shot_alive = [&] {
            const walker* t = world().find_by_id(tower_id);
            if (t == nullptr)
                return false;
            for (const auto& u : world().weaplist)
                if (u && !u->dead() && u->owner() == t && u->family() == FAMILY_ARROW)
                    return true;
            return false;
        };
        int shot_at = -1;
        for (int tick = 1; tick <= budget && shot_at < 0; ++tick) {
            world().tick();
            if (tower_shot_alive())
                shot_at = tick;
        }
        const walker* still = world().find_by_id(body_id);
        ASSERT_NE(nullptr, still) << "the staging: the soldier is still there";
        if (fixes)
            EXPECT_GT(shot_at, 0) << "gap " << blind << ": the tower shoots within " << budget << " ticks";
        else
            EXPECT_EQ(-1, shot_at) << "gap " << blind << ": the classic check never passes (shot on tick " << shot_at << ")";
    }
}

// Gameplay Fixes BD and the arrow slit (GameWorld::slit_open_for,
// game_world.cpp). A slit lets an owned weapon through on rng(dist / 16) == 0,
// dist the weapon's distance from its shooter on the long axis minus 8, plus
// 16 when under 16, measured from where the weapon stands when it asks (the
// classic loop moves its probe onto a spot and then asks there; a flying shot
// asks for the box one step ahead of where it stands); the roll is made for
// each slit tile under each box a weapon tests. An archer facing up, one slit tile on its line, a soldier
// beyond the slit inside the reach and under a flight box and a classic spot's
// box. Three stagings, each slit tile chosen from the arrow's numbers and
// asserted: (a) every flight box over the slit has a bound of 0 or 1; (b) no
// classic spot's box covers the slit, and the first flight box over it has a
// bound of 2 or more; (c) exactly one classic spot's box covers it, with a
// bound of 2 or more. Setting on, all three: the check passes and the stream
// after is the waver alone (no slit roll was drawn). The shot, setting on: in
// (a) it strikes the soldier; in (b), the shot's first slit roll lost, the
// arrow dies at the slit and the soldier keeps its hit points; every slit roll
// won, the soldier is struck. Setting off: (b) passes with no slit draw; (c)
// draws exactly the one roll of the computed bound, refused WallBlocked when
// it is lost, passing when it is won.
TEST_F(SemLineCheck, an_arrow_slit_is_open_to_the_check_and_the_shot_takes_its_gamble)
{
    constexpr short kX = 512;
    constexpr short kY = 640;
    enum class Slit { Near, Unspotted, Spotted };
    for (Slit slit : {Slit::Near, Slit::Unspotted, Slit::Spotted}) {
        const char* name = slit == Slit::Near        ? "(a) a near slit"
                           : slit == Slit::Unspotted ? "(b) a slit no classic spot covers"
                                                     : "(c) a slit one classic spot covers";
        SCOPED_TRACE(name);
        // The numbers, from an archer in a world of its own.
        fresh_world(1);
        walker* a0 = living_at(FAMILY_ARCHER, 0, kX, kY);
        ASSERT_NE(nullptr, a0);
        face(a0, FACE_UP);
        const ShotPlan p = plan_of(a0, FACE_UP);
        ASSERT_GT(p.los, 1);
        const std::vector<Box> flown = flown_boxes(p);
        const std::vector<Box> from = flown_from(p);
        const std::vector<Box> spots = classic_spots(p);
        ASSERT_EQ(p.pad_x / GRID_SIZE, (p.pad_x + p.sx - 1) / GRID_SIZE) << "the staging: the line is one tile wide";
        const int col = p.pad_x / GRID_SIZE;
        const auto covers = [&](const Box& b, int row) {
            return b.y / GRID_SIZE <= row && row <= (b.y + p.sy - 1) / GRID_SIZE;
        };
        const auto bound_at = [&](const Box& b) {
            std::int32_t dist = std::abs(b.y - kY) - GRID_SIZE / 2;
            if (dist < GRID_SIZE)
                dist += GRID_SIZE;
            return static_cast<std::uint32_t>(dist / GRID_SIZE);
        };
        // The soldier: under the first classic spot's box past the pad that is also met by a
        // flight box, inside the reach.
        int body_y = -1;
        for (std::size_t i = 1; i < spots.size() && body_y < 0; ++i) {
            const int y = spots[i].y - 7;
            if (std::abs(kY - y) + std::abs(kX - (p.pad_x + p.sx / 2 - 8)) > p.reach)
                break;
            if (kY - y < 100)
                continue;  // room for the three slits in front of it
            body_y = y;
        }
        ASSERT_GE(body_y, 0) << "the staging: a soldier spot";
        const int body_row_top = body_y / GRID_SIZE;
        const int pad_row_bottom = (p.pad_y + p.sy - 1) / GRID_SIZE;
        int row = -1;
        std::vector<std::uint32_t> flown_bounds;
        std::uint32_t classic_bound = 0;
        for (int r = pad_row_bottom - 1; r > body_row_top + 1 && row < 0; --r) {
            std::vector<std::uint32_t> fb;
            for (std::size_t k = 0; k < flown.size(); ++k)
                if (covers(flown[k], r))
                    fb.push_back(bound_at(from[k]));  // measured from where the shot stands
            int nspots = 0;
            std::uint32_t cb = 0;
            for (const Box& b : spots)
                if (covers(b, r)) {
                    ++nspots;
                    cb = bound_at(b);
                }
            if (fb.empty() || covers(Box{p.pad_x, p.pad_y}, r))
                continue;
            const bool near = std::all_of(fb.begin(), fb.end(), [](std::uint32_t v) { return v <= 1; });
            if ((slit == Slit::Near && near) || (slit == Slit::Unspotted && nspots == 0 && fb.front() >= 2) ||
                (slit == Slit::Spotted && nspots == 1 && cb >= 2)) {
                row = r;
                flown_bounds = fb;
                classic_bound = cb;
            }
        }
        ASSERT_GE(row, 0) << "the staging: a slit row for " << name;
        for (short fixes : {short{1}, short{0}}) {
            SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
            if (!fixes && slit == Slit::Near)
                continue;
            const auto stage = [&](walker*& archer, walker*& body) {
                fresh_world(fixes);
                tile(col, row, PIX_WALL_ARROW_GRASS);
                archer = living_at(FAMILY_ARCHER, 0, kX, kY);
                body = living_at(FAMILY_SOLDIER, 1, static_cast<short>(p.pad_x + p.sx / 2 - 8), static_cast<short>(body_y), 16, 16);
                if (archer == nullptr || body == nullptr)
                    return;
                face(archer, FACE_UP);
                body->set_act_type(ACT_SIT);
                body->stats()->set_armor(0.0f);
                archer->set_foe(body);
            };
            walker* archer = nullptr;
            walker* body = nullptr;
            stage(archer, body);
            ASSERT_TRUE(archer && body);
            ASSERT_LE(archer->distance_to_ob(body), p.reach) << "the staging: inside the reach";
            bool flown_meets = false;
            for (const Box& b : flown)
                flown_meets = flown_meets || box_meets(b, p.sx, p.sy, body);
            ASSERT_TRUE(flown_meets) << "the staging: under a flight box";
            if (fixes) {
                const std::uint32_t s = state_where({{p.waver_bound, p.waver_zero}});
                world().rng_.state_ = s;
                Denial why = Denial::NoFoe;
                EXPECT_TRUE(ask(archer, 0, -1, &why)) << "the check takes the slit as open";
                EXPECT_EQ(Denial::None, why);
                EXPECT_EQ(state_after(s, {p.waver_bound}), world().rng_.state_) << "no slit roll drawn by the check";
                drop_weapons();
                if (slit == Slit::Near) {
                    world().rng_.state_ = s;
                    ASSERT_TRUE(fire_and_fly(archer, 4 * p.los));
                    EXPECT_LT(body->stats()->hitpoints(), 400.0f) << "every slit roll has a bound of 0 or 1: struck";
                } else if (slit == Slit::Unspotted) {
                    world().rng_.state_ =
                        state_where_then(p.waver_bound, p.waver_zero, flown_bounds.front(), 1,
                                         [](std::uint32_t r) { return r != 0; });
                    ASSERT_TRUE(fire_and_fly(archer, 4 * p.los));
                    EXPECT_FLOAT_EQ(400.0f, body->stats()->hitpoints()) << "the first slit roll lost: the arrow breaks";
                    drop_weapons();
                    stage(archer, body);
                    ASSERT_TRUE(archer && body);
                    std::uint32_t won = 0;
                    for (std::uint32_t seed = 1; won == 0; ++seed) {
                        og::sim::SimRandom probe(seed);
                        bool all = probe.next(p.waver_bound) == p.waver_zero;
                        for (std::uint32_t b : flown_bounds)
                            all = all && probe.next(b) == 0;
                        if (all)
                            won = seed;
                    }
                    world().rng_.state_ = won;
                    ASSERT_TRUE(fire_and_fly(archer, 4 * p.los));
                    EXPECT_LT(body->stats()->hitpoints(), 400.0f) << "every slit roll won: struck";
                }
            } else if (slit == Slit::Unspotted) {
                const std::uint32_t s = state_where({{p.waver_bound, p.waver_zero}});
                world().rng_.state_ = s;
                Denial why = Denial::NoFoe;
                EXPECT_TRUE(ask(archer, 0, -1, &why)) << "no classic spot is on the slit";
                EXPECT_EQ(Denial::None, why);
                EXPECT_EQ(state_after(s, {p.waver_bound}), world().rng_.state_) << "no slit roll drawn";
            } else {
                ASSERT_GE(classic_bound, 2u);
                for (bool lost : {true, false}) {
                    SCOPED_TRACE(lost ? "the classic spot's roll lost" : "the classic spot's roll won");
                    if (!lost)
                        stage(archer, body);
                    ASSERT_TRUE(archer && body);
                    const std::uint32_t s = state_where({{p.waver_bound, p.waver_zero}, {classic_bound, lost ? kNonZero : 0u}});
                    world().rng_.state_ = s;
                    Denial why = Denial::NoFoe;
                    const bool passes = ask(archer, 0, -1, &why);
                    EXPECT_EQ(!lost, passes);
                    EXPECT_EQ(lost ? Denial::WallBlocked : Denial::None, why);
                    EXPECT_EQ(state_after(s, {p.waver_bound, classic_bound}), world().rng_.state_)
                        << "exactly one slit roll, of bound " << classic_bound;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// A shooter does not fire at a door (walker_movement.cpp
// walker::line_check_flight, obmap.cpp's weapon-against-weapon arm,
// GameWorld::is_line_check_probe: Gameplay Fixes BF)
// ---------------------------------------------------------------------------

// Gameplay Fixes BF, the flight. An archer facing up on a field 160 tiles
// tall; an enemy door across the line in one collision cell, the first flight
// box over that cell meeting it, and a hostile soldier on the line beyond the
// door, inside the reach gate, met by a flight box and by a classic spot's
// box, in cells no classic spot's box shares with the door's (all computed
// from the arrow's numbers and asserted: stage_door_line). Two streams: the
// first clash roll lost, and every roll won. Setting on, both: the check is
// refused, WallBlocked, its test weapon stopped by the door, and the stream
// after is the waver alone (the door draws no roll); with the door gone the
// check passes on the soldier, so the door is what refused it. Setting off,
// both: the classic loop as computed from its spots (classic_run): it rolls
// at the door's cell, passes on the door when that roll is lost and on the
// soldier when it is won (the staging asserts both, so the classic check does
// fire at the door here).
TEST_F(SemLineCheck, a_door_on_the_shots_path_refuses_the_check_with_the_setting_on)
{
    for (short fixes : {short{0}, short{1}}) {
        for (bool lost : {true, false}) {
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, " : "Gameplay Fixes off, ") +
                         (lost ? "the first clash roll lost" : "every clash roll won"));
            walker* archer = up_archer(fixes);
            ASSERT_NE(nullptr, archer);
            const ShotPlan p = plan_of(archer, FACE_UP);
            ASSERT_GT(p.los, 1);
            walker* door = nullptr;
            walker* soldier = nullptr;
            ASSERT_NO_FATAL_FAILURE(stage_door_line(archer, p, 1, FAMILY_SOLDIER, door, soldier));
            archer->set_foe(soldier);
            ASSERT_LE(archer->distance_to_ob(soldier), p.reach) << "the staging: inside the reach gate";
            const std::uint32_t s =
                lost ? state_where_then(p.waver_bound, p.waver_zero, 10, 1, [](std::uint32_t r) { return r <= 3; })
                     : state_where_then(p.waver_bound, p.waver_zero, 10, 8, [](std::uint32_t r) { return r > 3; });
            const ClassicRun classic = classic_run(p, s, door, soldier);
            ASSERT_EQ(lost ? static_cast<const walker*>(door) : static_cast<const walker*>(soldier), classic.met)
                << "the staging: the classic loop stops on the door when its roll is lost, else on the soldier";
            world().rng_.state_ = s;
            Denial why = Denial::NoFoe;
            const bool passes = ask(archer, 0, -1, &why);
            const walker* probe = last_probe(archer);
            ASSERT_NE(nullptr, probe);
            if (fixes) {
                EXPECT_FALSE(passes) << "an enemy door on the shot's path refuses the check";
                EXPECT_EQ(Denial::WallBlocked, why) << "the refusal a wall gives";
                EXPECT_EQ(door, probe->collide_ob()) << "the test weapon was stopped by the door";
                EXPECT_EQ(state_after(s, {p.waver_bound}), world().rng_.state_) << "the waver alone: no roll at the door";
            } else {
                EXPECT_EQ(classic.passes, passes);
                EXPECT_EQ(classic.why, why);
                EXPECT_EQ(classic.met, probe->collide_ob()) << (lost ? "the classic check fires at the door" : "it flies through and meets the soldier");
                EXPECT_EQ(classic.after, world().rng_.state_) << "the waver and the classic loop's rolls at the door's cell";
            }
            EXPECT_EQ(0, door->dead()) << "the door is not opened";
            // Control: the door gone, the same check passes on the soldier.
            world().remove_ob(door);
            drop_dead_weapons();
            world().rng_.state_ = s;
            why = Denial::NoFoe;
            EXPECT_TRUE(ask(archer, 0, -1, &why)) << "control: with no door the check reaches the soldier";
            EXPECT_EQ(Denial::None, why);
            const walker* control = last_probe(archer);
            ASSERT_NE(nullptr, control);
            EXPECT_EQ(soldier, control->collide_ob());
        }
    }
}

// Gameplay Fixes BF, the pad. On a DIAGONAL pad, where the pad's question
// decides alone (the geometry of the_pad_passes_on_a_body_and_refuses_on_a_wall_on_both_settings):
// an archer facing up and right, an enemy door whose box meets the pad's box,
// shares a collision cell with it and meets no flight box (asserted); the
// archer's foe stands in reach off the line. Setting on, the first clash roll
// lost and every roll won: refused, WallBlocked, the test weapon stopped by the
// door, the stream after the waver alone. Setting off, the first roll lost: the
// classic pad passes on the door as on any body, its one roll drawn.
TEST_F(SemLineCheck, a_door_on_the_pad_refuses_the_check_with_the_setting_on)
{
    constexpr short kX = 320;
    constexpr short kY = 329;
    for (short fixes : {short{0}, short{1}}) {
        for (bool lost : {true, false}) {
            if (!fixes && !lost)
                continue;  // the classic loop's later spots are not this rule's
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, " : "Gameplay Fixes off, ") +
                         (lost ? "the first clash roll lost" : "every clash roll won"));
            fresh_world(fixes);
            walker* archer = living_at(FAMILY_ARCHER, 0, kX, kY);
            ASSERT_NE(nullptr, archer);
            face(archer, FACE_UP_RIGHT);
            const ShotPlan p = plan_of(archer, FACE_UP_RIGHT);
            ASSERT_GT(p.los, 1);
            const Box pad{p.pad_x, p.pad_y};
            walker* door = door_at(1, static_cast<short>(pad.x + 3), static_cast<short>(pad.y + 3));
            ASSERT_NE(nullptr, door);
            ASSERT_TRUE(box_meets(pad, p.sx, p.sy, door)) << "the staging: the door is under the pad's box";
            ASSERT_TRUE(shares_a_cell(pad, p.sx, p.sy, door)) << "the staging: in a cell the pad's box covers";
            for (const Box& b : flown_boxes(p))
                ASSERT_FALSE(box_meets(b, p.sx, p.sy, door)) << "the staging: and under no step's box";
            ASSERT_FALSE(meets(archer, archer->xpos(), archer->ypos(), door)) << "the staging: clear of the archer";
            walker* foe = living_at(FAMILY_SOLDIER, 1, static_cast<short>(kX + 40), static_cast<short>(kY + 40), 16, 16);
            ASSERT_NE(nullptr, foe);
            foe->set_act_type(ACT_SIT);
            ASSERT_FALSE(box_meets(pad, p.sx, p.sy, foe)) << "the staging: the foe is off the pad";
            for (const Box& b : flown_boxes(p))
                ASSERT_FALSE(box_meets(b, p.sx, p.sy, foe)) << "the staging: the foe is off the flight's line";
            archer->set_foe(foe);
            ASSERT_LE(archer->distance_to_ob(foe), p.reach) << "the staging: inside the reach gate";
            const std::uint32_t s =
                lost ? state_where_then(p.waver_bound, p.waver_zero, 10, 1, [](std::uint32_t r) { return r <= 3; })
                     : state_where_then(p.waver_bound, p.waver_zero, 10, 8, [](std::uint32_t r) { return r > 3; });
            world().rng_.state_ = s;
            Denial why = Denial::NoFoe;
            const bool passes = ask(archer, 1, -1, &why);
            const walker* probe = last_probe(archer);
            ASSERT_NE(nullptr, probe);
            EXPECT_EQ(door, probe->collide_ob()) << "the test weapon was stopped by the door on the pad";
            if (fixes) {
                EXPECT_FALSE(passes) << "an enemy door on the pad refuses the check";
                EXPECT_EQ(Denial::WallBlocked, why);
                EXPECT_EQ(state_after(s, {p.waver_bound}), world().rng_.state_) << "the waver alone: no roll at the door";
            } else {
                EXPECT_TRUE(passes) << "the classic pad passes on the door, a body";
                EXPECT_EQ(Denial::None, why);
                EXPECT_EQ(state_after(s, {p.waver_bound, 10}), world().rng_.state_) << "the waver and the door's one roll";
            }
            EXPECT_EQ(0, door->dead());
        }
    }
}

// Gameplay Fixes BF: a door that is the shooter's own foe (a level script or a
// mod can name one, set_foe) passes the check, as every foe does. (a) On the
// flight, at the door spot of stage_door_line, nothing else on the line; (b)
// on a diagonal pad, as in a_door_on_the_pad_refuses_the_check_with_the_setting_on.
// The door is inside the reach gate. Setting on, the first clash roll lost and
// every roll won: passes, the test weapon stopped by the door, the stream after
// the waver alone. Setting off: (a) the classic loop as computed from its spots
// (it passes on the door on a lost roll and runs out, RayMiss, when every roll
// is won, both asserted on the computation); (b) the first roll lost: passes,
// its one roll drawn.
TEST_F(SemLineCheck, a_door_that_is_the_shooters_foe_passes_the_check)
{
    for (bool on_pad : {false, true}) {
        for (short fixes : {short{0}, short{1}}) {
            for (bool lost : {true, false}) {
                if (on_pad && !fixes && !lost)
                    continue;  // the classic loop's later spots are not this rule's
                SCOPED_TRACE(std::string(on_pad ? "(b) on the pad, " : "(a) on the flight, ") +
                             (fixes ? "Gameplay Fixes on, " : "Gameplay Fixes off, ") +
                             (lost ? "the first clash roll lost" : "every clash roll won"));
                walker* archer = nullptr;
                walker* door = nullptr;
                ShotPlan p;
                short dx = 0;
                short dy = -1;
                if (on_pad) {
                    fresh_world(fixes);
                    archer = living_at(FAMILY_ARCHER, 0, 320, 329);
                    ASSERT_NE(nullptr, archer);
                    face(archer, FACE_UP_RIGHT);
                    p = plan_of(archer, FACE_UP_RIGHT);
                    door = door_at(1, static_cast<short>(p.pad_x + 3), static_cast<short>(p.pad_y + 3));
                    ASSERT_NE(nullptr, door);
                    ASSERT_TRUE(box_meets(Box{p.pad_x, p.pad_y}, p.sx, p.sy, door)) << "the staging: on the pad";
                    for (const Box& b : flown_boxes(p))
                        ASSERT_FALSE(box_meets(b, p.sx, p.sy, door)) << "the staging: and under no step's box";
                    dx = 1;
                } else {
                    archer = up_archer(fixes);
                    ASSERT_NE(nullptr, archer);
                    p = plan_of(archer, FACE_UP);
                    walker* none = nullptr;
                    ASSERT_NO_FATAL_FAILURE(stage_door_line(archer, p, 1, -1, door, none));
                }
                ASSERT_GT(p.los, 1);
                archer->set_foe(door);
                ASSERT_EQ(door, archer->foe());
                ASSERT_LE(archer->distance_to_ob(door), p.reach) << "the staging: inside the reach gate";
                const std::uint32_t s =
                    lost ? state_where_then(p.waver_bound, p.waver_zero, 10, 1, [](std::uint32_t r) { return r <= 3; })
                         : state_where_then(p.waver_bound, p.waver_zero, 10, 8, [](std::uint32_t r) { return r > 3; });
                ClassicRun classic;
                if (!on_pad && !fixes) {
                    classic = classic_run(p, s, door, nullptr);
                    ASSERT_EQ(lost, classic.passes) << "the staging: the classic loop passes on a lost roll, else runs out";
                }
                world().rng_.state_ = s;
                Denial why = Denial::NoFoe;
                const bool passes = ask(archer, dx, dy, &why);
                const walker* probe = last_probe(archer);
                ASSERT_NE(nullptr, probe);
                if (fixes) {
                    EXPECT_TRUE(passes) << "a door that is the foe passes, as every foe does";
                    EXPECT_EQ(Denial::None, why);
                    EXPECT_EQ(door, probe->collide_ob()) << "the test weapon was stopped by the door";
                    EXPECT_EQ(state_after(s, {p.waver_bound}), world().rng_.state_) << "the waver alone: no roll at the door";
                } else if (on_pad) {
                    EXPECT_TRUE(passes) << "the classic pad passes on the door";
                    EXPECT_EQ(Denial::None, why);
                    EXPECT_EQ(state_after(s, {p.waver_bound, 10}), world().rng_.state_) << "the waver and the door's one roll";
                } else {
                    EXPECT_EQ(classic.passes, passes);
                    EXPECT_EQ(classic.why, why);
                    EXPECT_EQ(classic.after, world().rng_.state_) << "the waver and the classic loop's rolls at the door's cell";
                }
            }
        }
    }
}

// Gameplay Fixes BF changes nothing for a door the shot never reaches. An
// archer facing up on a field 160 tiles tall; a hostile soldier on the line at
// the spot of up_line_spot (met by the first flight box over its cell, and by a
// classic spot's box), and an enemy door beyond it in one collision cell that a
// later flight box would meet, in a row no flight box and no classic spot
// covers up to the first that meets the soldier (all asserted). Both settings,
// the first clash roll lost and every roll won: the check passes on the
// soldier (its test weapon stopped by the soldier) and the stream after is the
// waver alone.
TEST_F(SemLineCheck, a_door_behind_the_foe_changes_nothing)
{
    for (short fixes : {short{0}, short{1}}) {
        for (bool lost : {true, false}) {
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, " : "Gameplay Fixes off, ") +
                         (lost ? "the first clash roll lost" : "every clash roll won"));
            walker* archer = up_archer(fixes);
            ASSERT_NE(nullptr, archer);
            const ShotPlan p = plan_of(archer, FACE_UP);
            ASSERT_GT(p.los, 1);
            const LineSpot at = up_line_spot(p, 16, 16, 0);
            ASSERT_GE(at.k, 0) << "the staging: a spot for the soldier";
            walker* soldier = living_at(FAMILY_SOLDIER, 1, at.x, at.y, 16, 16);
            ASSERT_NE(nullptr, soldier);
            soldier->set_act_type(ACT_SIT);
            const std::vector<Box> boxes = flown_boxes(p);
            const std::vector<Box> spots = classic_spots(p);
            int first_flown = -1;
            for (int k = 0; k < static_cast<int>(boxes.size()) && first_flown < 0; ++k)
                if (box_meets(boxes[static_cast<std::size_t>(k)], p.sx, p.sy, soldier))
                    first_flown = k;
            int first_spot = -1;
            for (int i = 0; i < static_cast<int>(spots.size()) && first_spot < 0; ++i)
                if (box_meets(spots[static_cast<std::size_t>(i)], p.sx, p.sy, soldier))
                    first_spot = i;
            ASSERT_EQ(at.k, first_flown) << "the staging: the flight meets the soldier at its spot";
            ASSERT_GE(first_spot, 0) << "the staging: a classic spot meets the soldier";
            walker* door = door_at(1, 8, 8);
            ASSERT_NE(nullptr, door);
            // The door: the lowest single-cell place above the soldier's row, across the line.
            const int row = soldier->ypos() / kObCell - 1;
            const short door_y = static_cast<short>(row * kObCell + kObCell - 1 - door->sizey());
            place(door, at.x, door_y, door->sizex(), door->sizey());
            const int dc = door->xpos() / kObCell;
            const int dr = door->ypos() / kObCell;
            ASSERT_EQ(dc, (door->xpos() + door->sizex()) / kObCell) << "the staging: the door is in one cell";
            ASSERT_EQ(dr, (door->ypos() + door->sizey()) / kObCell);
            ASSERT_EQ(row, dr);
            bool behind = false;
            for (int k = first_flown + 1; k < static_cast<int>(boxes.size()); ++k)
                behind = behind || box_meets(boxes[static_cast<std::size_t>(k)], p.sx, p.sy, door);
            ASSERT_TRUE(behind) << "the staging: the door stands on the shot's path, behind the soldier";
            for (int k = 0; k <= first_flown; ++k)
                ASSERT_FALSE(covers_cell(boxes[static_cast<std::size_t>(k)], p.sx, p.sy, dc, dr))
                    << "the staging: the flight meets the soldier before the door's cell";
            for (int i = 0; i <= first_spot; ++i)
                ASSERT_FALSE(covers_cell(spots[static_cast<std::size_t>(i)], p.sx, p.sy, dc, dr))
                    << "the staging: so do the classic spots";
            archer->set_foe(soldier);
            ASSERT_LE(archer->distance_to_ob(soldier), p.reach) << "the staging: inside the reach gate";
            const std::uint32_t s =
                lost ? state_where_then(p.waver_bound, p.waver_zero, 10, 1, [](std::uint32_t r) { return r <= 3; })
                     : state_where_then(p.waver_bound, p.waver_zero, 10, 8, [](std::uint32_t r) { return r > 3; });
            world().rng_.state_ = s;
            Denial why = Denial::NoFoe;
            EXPECT_TRUE(ask(archer, 0, -1, &why)) << "the check passes on the soldier";
            EXPECT_EQ(Denial::None, why);
            const walker* probe = last_probe(archer);
            ASSERT_NE(nullptr, probe);
            EXPECT_EQ(soldier, probe->collide_ob()) << "its test weapon was stopped by the soldier";
            EXPECT_EQ(state_after(s, {p.waver_bound}), world().rng_.state_) << "the waver alone";
        }
    }
}

// A door of the shooter's own side on the line is flown through, on both
// settings: obmap.cpp lets a weapon pass its own side before any clash roll,
// and Gameplay Fixes BF changes nothing there. An archer facing up, its own
// team's door at the door spot of stage_door_line and a hostile soldier beyond
// it, met by a flight box and by a classic spot's box (asserted there). The
// first clash roll lost and every roll won: the check passes on the soldier
// (its test weapon stopped by the soldier) and the stream after is the waver
// alone.
TEST_F(SemLineCheck, an_own_team_door_is_flown_through)
{
    for (short fixes : {short{0}, short{1}}) {
        for (bool lost : {true, false}) {
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, " : "Gameplay Fixes off, ") +
                         (lost ? "the first clash roll lost" : "every clash roll won"));
            walker* archer = up_archer(fixes);
            ASSERT_NE(nullptr, archer);
            const ShotPlan p = plan_of(archer, FACE_UP);
            ASSERT_GT(p.los, 1);
            walker* door = nullptr;
            walker* soldier = nullptr;
            ASSERT_NO_FATAL_FAILURE(stage_door_line(archer, p, 0, FAMILY_SOLDIER, door, soldier));
            ASSERT_EQ(archer->team_num(), door->team_num()) << "the staging: the archer's own side's door";
            archer->set_foe(soldier);
            ASSERT_LE(archer->distance_to_ob(soldier), p.reach) << "the staging: inside the reach gate";
            const std::uint32_t s =
                lost ? state_where_then(p.waver_bound, p.waver_zero, 10, 1, [](std::uint32_t r) { return r <= 3; })
                     : state_where_then(p.waver_bound, p.waver_zero, 10, 8, [](std::uint32_t r) { return r > 3; });
            world().rng_.state_ = s;
            Denial why = Denial::NoFoe;
            EXPECT_TRUE(ask(archer, 0, -1, &why)) << "the test weapon flies through its own side's door";
            EXPECT_EQ(Denial::None, why);
            const walker* probe = last_probe(archer);
            ASSERT_NE(nullptr, probe);
            EXPECT_EQ(soldier, probe->collide_ob()) << "and is stopped by the soldier";
            EXPECT_EQ(state_after(s, {p.waver_bound}), world().rng_.state_) << "the waver alone: no roll at an own door";
        }
    }
}

// Gameplay Fixes BF changes the check and not the shot. An archer facing up
// fires a real arrow (waver 0) at an enemy door on the line, at the door spot
// of stage_door_line, nothing behind it; the arrow acts until it ends (as
// weap::act does each tick). The arrow takes the clash roll at each step whose
// box covers the door's cell (the count is derived from its boxes). On both
// settings: the first roll lost, the arrow is stopped by the door and the door
// loses hit points; every roll won, the arrow flies through it, ends beyond the
// door, and the door's hit points are whole.
TEST_F(SemLineCheck, the_shot_itself_still_takes_the_clash_roll_at_a_door)
{
    for (short fixes : {short{0}, short{1}}) {
        for (bool lost : {true, false}) {
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, " : "Gameplay Fixes off, ") +
                         (lost ? "the first clash roll lost" : "every clash roll won"));
            walker* archer = up_archer(fixes);
            ASSERT_NE(nullptr, archer);
            const ShotPlan p = plan_of(archer, FACE_UP);
            ASSERT_GT(p.los, 1);
            walker* door = nullptr;
            walker* none = nullptr;
            ASSERT_NO_FATAL_FAILURE(stage_door_line(archer, p, 1, -1, door, none));
            const int dc = door->xpos() / kObCell;
            const int dr = door->ypos() / kObCell;
            int covering = 0;
            for (const Box& b : flown_boxes(p))
                if (covers_cell(b, p.sx, p.sy, dc, dr))
                    ++covering;
            ASSERT_GE(covering, 1);
            const float whole = door->stats()->hitpoints();
            ASSERT_GT(whole, 0.0f) << "the staging: the door's hit points";
            world().rng_.state_ =
                lost ? state_where_then(p.waver_bound, p.waver_zero, 10, 1, [](std::uint32_t r) { return r <= 3; })
                     : state_where_then(p.waver_bound, p.waver_zero, 10, covering, [](std::uint32_t r) { return r > 3; });
            walker* shot = archer->fire();
            ASSERT_NE(nullptr, shot) << "the staging: the arrow leaves the pad";
            for (int a = 0; !shot->dead() && a < 4 * p.los; ++a)
                shot->act();
            ASSERT_TRUE(shot->dead()) << "the staging: the arrow ends inside its range";
            if (lost) {
                EXPECT_LT(door->stats()->hitpoints(), whole) << "the lost roll: the door takes the arrow";
                EXPECT_GT(shot->ypos(), door->ypos()) << "the arrow ended at the door";
            } else {
                EXPECT_FLOAT_EQ(whole, door->stats()->hitpoints()) << "every roll won: the arrow flies through the door";
                EXPECT_LT(shot->ypos() + shot->sizey(), door->ypos()) << "and ends beyond it";
            }
            EXPECT_EQ(0, door->dead());
        }
    }
}

// Gameplay Fixes BF, through ticks. A hold-post archer guard (it asks a FIRE
// order every act) facing up, an enemy door on the line at the door spot of
// stage_door_line, and a sitting hostile orc beyond it inside the reach gate
// (asserted there). The guard's check draws its waver every act, so the
// staging asserts that, for every waver the draw can give, the flight's first
// box over the door's cell meets the door and no earlier box meets the orc.
// The world ticks 200 times; every live arrow the archer owns is counted once
// by its entity id. Setting on: no shot is started, and the door's and the
// orc's hit points are whole; after the ticks the archer holds the orc as its
// foe, faces it, and its check is refused at the door (WallBlocked), so every
// gate but the door's lets it shoot. Setting off: the classic check fires: at
// least one shot is started and the door loses hit points (each shot meets the
// door's clash roll and is stopped by it on four rolls in ten; over the shots
// of 200 ticks the door is missed every time with odds under one in a
// million).
TEST_F(SemLineCheck, a_bot_whose_foe_is_behind_a_door_does_not_shoot_with_the_setting_on)
{
    constexpr int kTicks = 200;
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        walker* archer = up_archer(fixes);
        ASSERT_NE(nullptr, archer);
        const ShotPlan p = plan_of(archer, FACE_UP);
        ASSERT_GT(p.los, 1);
        walker* door = nullptr;
        walker* orc = nullptr;
        ASSERT_NO_FATAL_FAILURE(stage_door_line(archer, p, 1, FAMILY_ORC, door, orc));
        ASSERT_LE(archer->distance_to_ob(orc), p.reach) << "the staging: inside the reach gate";
        ASSERT_EQ(FACE_UP, archer->facing(static_cast<short>(orc->xpos() - archer->xpos()),
                                          static_cast<short>(orc->ypos() - archer->ypos())))
            << "the staging: the orc is straight up";
        const int dc = door->xpos() / kObCell;
        const int dr = door->ypos() / kObCell;
        for (std::uint32_t d = 0; d < p.waver_bound; ++d) {
            ShotPlan aim = p;
            aim.lx = static_cast<float>(static_cast<std::int32_t>(d) - static_cast<std::int32_t>(p.waver_zero));
            const std::vector<Box> boxes = flown_boxes(aim);
            bool met = false;
            for (const Box& b : boxes) {
                ASSERT_FALSE(box_meets(b, aim.sx, aim.sy, orc)) << "the staging: waver " << aim.lx << ", the orc before the door";
                if (covers_cell(b, aim.sx, aim.sy, dc, dr)) {
                    met = box_meets(b, aim.sx, aim.sy, door);
                    break;
                }
            }
            ASSERT_TRUE(met) << "the staging: waver " << aim.lx << ", the first box over the door's cell meets it";
        }
        const float door_whole = door->stats()->hitpoints();
        const float orc_whole = orc->stats()->hitpoints();
        archer->set_act_type(ACT_GUARD);
        archer->set_guard_hold_post(true);
        ASSERT_TRUE(archer->guard_hold_post());
        const std::uint32_t archer_id = archer->entity_id();
        const std::uint32_t door_id = door->entity_id();
        const std::uint32_t orc_id = orc->entity_id();
        std::vector<std::uint32_t> shots;
        for (int tick = 1; tick <= kTicks; ++tick) {
            world().tick();
            const walker* a = world().find_by_id(archer_id);
            ASSERT_NE(nullptr, a) << "the archer stands, tick " << tick;
            for (const auto& u : world().weaplist)
                if (u && !u->dead() && u->owner() == a && u->family() == FAMILY_ARROW &&
                    std::find(shots.begin(), shots.end(), u->entity_id()) == shots.end())
                    shots.push_back(u->entity_id());
        }
        walker* const a = world().find_by_id(archer_id);
        walker* const d = world().find_by_id(door_id);
        walker* const o = world().find_by_id(orc_id);
        ASSERT_NE(nullptr, a);
        ASSERT_NE(nullptr, d) << "the door still stands";
        if (fixes) {
            EXPECT_EQ(0u, shots.size()) << "no shot is started at a foe behind a door";
            EXPECT_FLOAT_EQ(door_whole, d->stats()->hitpoints()) << "the door's hit points are whole";
            ASSERT_NE(nullptr, o);
            EXPECT_FLOAT_EQ(orc_whole, o->stats()->hitpoints()) << "the orc's too";
            EXPECT_EQ(o, a->foe()) << "the archer holds the orc as its foe";
            EXPECT_EQ(FACE_UP, a->curdir()) << "and faces it";
            drop_dead_weapons();
            Denial why = Denial::NoFoe;
            EXPECT_FALSE(ask(a, 0, static_cast<short>(o->ypos() - a->ypos()), &why));
            EXPECT_EQ(Denial::WallBlocked, why) << "only the door refuses the shot";
        } else {
            EXPECT_GE(shots.size(), 1u) << "the classic check fires at the door";
            EXPECT_LT(d->stats()->hitpoints(), door_whole) << "and the door loses hit points";
        }
    }
}

// Gameplay Fixes BF: the door arm of obmap.cpp now runs for the line check's
// test weapon at every door it meets, and that weapon is nobody's seat and
// holds no key. The shooter is a seat's archer (user 0), so its own step into a
// locked door is told "Key N needed!"; the staging of
// a_door_on_the_shots_path_refuses_the_check_with_the_setting_on, the setting
// on. After the refused check (WallBlocked, the test weapon stopped by the
// door): no notification was posted, the test weapon is no seat's and started
// no notice cooldown, and the door is alive, shut and whole. Control: the
// archer's own step onto the door posts one notice.
TEST_F(SemLineCheck, a_refused_check_at_a_door_tells_no_one_a_key_is_needed)
{
    walker* archer = up_archer(1);
    ASSERT_NE(nullptr, archer);
    const ShotPlan p = plan_of(archer, FACE_UP);
    ASSERT_GT(p.los, 1);
    walker* door = nullptr;
    walker* soldier = nullptr;
    ASSERT_NO_FATAL_FAILURE(stage_door_line(archer, p, 1, FAMILY_SOLDIER, door, soldier));
    archer->set_user(0);
    archer->set_skip_exit(0);
    archer->set_foe(soldier);
    ASSERT_LE(archer->distance_to_ob(soldier), p.reach) << "the staging: inside the reach gate";
    ASSERT_EQ(0u, archer->keys()) << "the staging: the archer holds no key";
    const float whole = door->stats()->hitpoints();
    ASSERT_EQ(0u, notifications()) << "the staging: nothing posted yet";
    world().rng_.state_ = state_where({{p.waver_bound, p.waver_zero}});
    Denial why = Denial::NoFoe;
    EXPECT_FALSE(ask(archer, 0, -1, &why));
    EXPECT_EQ(Denial::WallBlocked, why);
    const walker* probe = last_probe(archer);
    ASSERT_NE(nullptr, probe);
    EXPECT_EQ(door, probe->collide_ob()) << "the door arm ran: the door stopped the test weapon";
    EXPECT_EQ(0u, notifications()) << "no one is told a key is needed";
    EXPECT_EQ(-1, static_cast<int>(probe->user())) << "the test weapon is no seat's";
    EXPECT_EQ(0, probe->skip_exit()) << "and started no notice cooldown";
    EXPECT_EQ(0, door->dead()) << "the door is not opened";
    EXPECT_FLOAT_EQ(whole, door->stats()->hitpoints()) << "and is whole";
    // Control: the seat's own step onto the locked door is told.
    EXPECT_FALSE(world().query_passable(static_cast<float>(door->xpos()), static_cast<float>(door->ypos()), archer));
    EXPECT_EQ(1u, notifications()) << "control: a seat bumping the locked door is told";
}
