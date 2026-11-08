/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch walker-4: the rules the review found in walker_combat.cpp, walker_movement.cpp, walker_pathing.cpp, walker_specials.cpp, weap.cpp.
// Each test is named after the rule it asserts.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "../test_sim_random_scope.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/sound_ids.h>
#include <openglad/gameplay/event.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/pathfinding_grid.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

// A sim-RNG spy: records every bound drawn, answers `answer_for_two` to a
// draw of bound 2 (the death cry's coin) and 0 to every other draw.
class CoinSpy : public IRandom
{
public:
    explicit CoinSpy(std::uint32_t answer_for_two) : answer_(answer_for_two) {}
    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        bounds.push_back(max_exclusive);
        if (max_exclusive == 0)
            return 0;
        return max_exclusive == 2 ? answer_ % 2 : 0;
    }
    std::vector<std::uint32_t> bounds;

private:
    std::uint32_t answer_;
};

// The A* path state of grid cell (cx, cy) on `floor` (walker_pathing.cpp's
// MAKE_STATE, in cells).
PathState cell_state(int cx, int cy, int floor = 0)
{
    return reinterpret_cast<PathState>(static_cast<intptr_t>(
        static_cast<intptr_t>(floor) * FLOOR_STRIDE + cy * MAP_WIDTH + cx));
}

// A sim-RNG spy for turn undead: answers `roll_bound - 1` to the roll of
// bound `roll_bound` (so every resistance draw loses) and 0 to the rest,
// and records every bound.
class RollSpy : public IRandom
{
public:
    explicit RollSpy(std::uint32_t roll_bound) : roll_(roll_bound) {}
    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        bounds.push_back(max_exclusive);
        if (max_exclusive == 0)
            return 0;
        return max_exclusive == roll_ ? roll_ - 1 : 0;
    }
    std::vector<std::uint32_t> bounds;

private:
    std::uint32_t roll_;
};

// A walker whose walk() answers from a script and records every attempt,
// and whose facing() can be forced (walkstep's fallback switch reads it).
class ScriptedWalkWalker : public walker
{
public:
    ScriptedWalkWalker() = default;
    void set_walk_results(std::initializer_list<bool> vals) { results_.assign(vals.begin(), vals.end()); }
    bool walk(float x, float y) override
    {
        calls_.emplace_back(x, y);
        if (results_.empty())
            return false;
        const bool r = results_.front();
        results_.pop_front();
        return r;
    }
    short facing(short x, short y) override
    {
        return forced_ ? forced_facing_ : walker::facing(x, y);
    }
    void force_facing(short f) { forced_ = true; forced_facing_ = f; }
    const std::vector<std::pair<float, float>>& calls() const { return calls_; }

private:
    std::deque<bool> results_;
    std::vector<std::pair<float, float>> calls_;
    bool forced_ = false;
    short forced_facing_ = FACE_UP;
};

class SemWalker4 : public ::testing::Test {
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
        if (current_game != nullptr)
        {
            current_game->positional_sound_visible = nullptr;
            current_game->positional_sound_visible_user = nullptr;
        }
        EXPECT_EQ(0u, guard_.count()) << guard_.message();
        tw_.reset();
    }

    // An open field of grass, 64x64 cells, with nobody on it.
    void fresh_world(int floors = 1)
    {
        tw_.reset();
        tw_ = std::make_unique<TestGameWorld>();
        GameWorld& w = world();
        if (floors > 1)
            w.set_floor_count(floors);
        w.resize_grid(64, 64);
        paint_all(PIX_GRASS1);
        w.my_team = 0;
        w.type = 0;
        w.rng_.state_ = 12345u;
    }

    GameWorld& world() { return tw_->world(); }
    og::sim::SimEventLog& events() { return tw_->events; }

    void paint_all(unsigned char pix)
    {
        std::fill_n(world().grid.data.get(),
                    static_cast<std::size_t>(world().grid.w) * world().grid.h,
                    static_cast<std::uint8_t>(pix));
    }

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

    // A level-4 bot (ACT_SIT, no seat), 400 hp, 500 MP, no regeneration.
    walker* living_at(int family, unsigned char team, short x, short y)
    {
        walker* w = world().add_ob(Order::Living, family);
        if (w == nullptr)
            return nullptr;
        w->set_team_num(team);
        w->set_real_team_num(255);
        w->set_act_type(ACT_SIT);
        w->stats()->set_level(4);
        w->stats()->set_max_hitpoints(400.0f);
        w->stats()->set_hitpoints(400.0f);
        w->stats()->set_max_magicpoints(500.0f);
        w->stats()->set_magicpoints(500.0f);
        w->stats()->set_magic_per_round(0.0f);
        w->set_busy(0.0f);
        return place(w, x, y);
    }

    int count_notifications(const std::string& text) const
    {
        int n = 0;
        for (const auto& e : tw_->events.events())
            if (e.kind == og::sim::EventKind::Notification && e.text == text)
                ++n;
        return n;
    }

    int count_all_notifications() const
    {
        int n = 0;
        for (const auto& e : tw_->events.events())
            if (e.kind == og::sim::EventKind::Notification)
                ++n;
        return n;
    }

    int count_sounds(int id) const
    {
        int n = 0;
        for (const auto& e : tw_->events.events())
            if (e.kind == og::sim::EventKind::PlaySound && e.a == static_cast<std::uint32_t>(id))
                ++n;
        return n;
    }

    int count_weapons(int family) const
    {
        int n = 0;
        for (const auto& u : tw_->world().weaplist)
            if (u && u->family() == family && !u->dead())
                ++n;
        return n;
    }

    // The live weapons of `family` (by pointer, read before the next tick).
    std::vector<walker*> weapons(int family)
    {
        std::vector<walker*> out;
        for (const auto& u : world().weaplist)
            if (u && u->family() == family && !u->dead())
                out.push_back(u.get());
        return out;
    }

    // Ready to die at the next blow.
    static void frail(walker* w)
    {
        w->stats()->set_max_hitpoints(1.0f);
        w->stats()->set_hitpoints(1.0f);
    }
};

// ---------------------------------------------------------------------------
// walker_combat.cpp: walker::attack's kill
// ---------------------------------------------------------------------------

// walker_combat.cpp:452. The classic toast: only the LAST foe's death says
// "All foes defeated!"; a kill that leaves foes standing says nothing of it.
TEST_F(SemWalker4, all_foes_defeated_is_announced_only_on_the_last_foe)
{
    walker* red = living_at(FAMILY_SOLDIER, 0, 96, 96);
    walker* g1 = living_at(FAMILY_ORC, 1, 128, 96);
    walker* g2 = living_at(FAMILY_ORC, 1, 160, 96);
    ASSERT_TRUE(red && g1 && g2);
    red->set_damage(5000.0f);
    frail(g1);
    frail(g2);

    ASSERT_EQ(1, static_cast<int>(red->attack(g1)));
    ASSERT_EQ(1, static_cast<int>(g1->dead()));
    EXPECT_EQ(0, count_notifications("All foes defeated!"))
        << "foes remain: a kill that leaves one standing announces nothing";

    ASSERT_EQ(1, static_cast<int>(red->attack(g2)));
    ASSERT_EQ(1, static_cast<int>(g2->dead()));
    EXPECT_EQ(1, count_notifications("All foes defeated!"))
        << "classic level, last foe: the toast, once";
}

// walker_combat.cpp:462 (triage: quirk). A living killed by a blow whose
// credit climb (no dead-owner stop) reaches a head of the victim's own colour,
// while is_friendly (which stops at a dead owner) judged the blow hostile, is
// announced without credit, in the five classic wordings. Staged the one way
// it is reachable: the killer's owner is dead and of the victim's colour.
class SameTeamDeath : public SemWalker4 {
protected:
    struct Kill
    {
        int notifications = 0;
        int owner_kills = 0;
        int owner_level_kills = 0;
        bool victim_dead = false;
    };

    // `shape` dresses the victim (team 0) before the blow.
    template <typename Shape>
    Kill kill_same_team(int victim_family, const std::string& expect, Shape shape)
    {
        fresh_world();
        walker* owner = living_at(FAMILY_SOLDIER, 0, 400, 400);
        walker* killer = living_at(FAMILY_SOLDIER, 1, 96, 96);
        walker* victim = living_at(victim_family, 0, 128, 96);
        EXPECT_TRUE(owner && killer && victim);
        if (!(owner && killer && victim))
            return {};
        auto record = std::make_unique<guy>(FAMILY_SOLDIER);
        record->name.clear();
        owner->set_owned_myguy(std::move(record));
        killer->set_owner(owner);
        owner->set_dead(1);
        killer->set_damage(5000.0f);
        frail(victim);
        victim->stats()->name.clear();
        shape(victim);
        EXPECT_EQ(0, static_cast<int>(killer->is_friendly(victim)))
            << "the blow is judged hostile (the killer heads its own chain)";
        events().clear();
        EXPECT_EQ(1, static_cast<int>(killer->attack(victim)));
        Kill k;
        k.victim_dead = victim->dead() != 0;
        k.notifications = count_notifications(expect);
        k.owner_kills = owner->myguy->kills;
        k.owner_level_kills = owner->myguy->level_kills;
        // A hero's hurt cry ("ANNA yells for help!") is hit_response's, not
        // the death announcement's.
        int announcements = 0;
        for (const auto& e : tw_->events.events())
            if (e.kind == og::sim::EventKind::Notification &&
                e.text.find("yells for help") == std::string::npos)
                ++announcements;
        EXPECT_EQ(1, announcements)
            << "exactly one death announcement for '" << expect << "'";
        return k;
    }
};

TEST_F(SameTeamDeath, a_same_team_death_is_announced_without_credit_classic)
{
    struct Case
    {
        const char* arm;
        int family;
        std::string text;
        void (*shape)(walker*);
    };
    const Case cases[] = {
        {"named summon", FAMILY_ORC, "BRAK Dispelled!",
         [](walker* v) { v->stats()->name = "BRAK"; v->set_lifetime(50); }},
        {"named NPC", FAMILY_ORC, "BRAK DIED!",
         [](walker* v) { v->stats()->name = "BRAK"; }},
        {"hero", FAMILY_ORC, "ANNA Died!",
         [](walker* v) {
             auto g = std::make_unique<guy>(FAMILY_ORC);
             g->name = "ANNA";
             v->set_owned_myguy(std::move(g));
             v->stats()->name.clear();
         }},
        {"registered family (orc)", FAMILY_ORC, "ORC DIED", [](walker*) {}},
        {"registered family (soldier)", FAMILY_SOLDIER, "SOLDIER SLAIN", [](walker*) {}},
        {"unknown family", FAMILY_ORC, "SOMEONE DIED",
         [](walker* v) { v->set_order_family(Order::Living, static_cast<char>(120)); }},
    };
    for (const Case& c : cases)
    {
        const Kill k = kill_same_team(c.family, c.text, c.shape);
        EXPECT_TRUE(k.victim_dead) << c.arm << ": the victim dies";
        EXPECT_EQ(1, k.notifications) << c.arm << ": announced as '" << c.text << "'";
        EXPECT_EQ(0, k.owner_kills) << c.arm << ": no kill on the head's record";
        EXPECT_EQ(0, k.owner_level_kills) << c.arm << ": no level kills either";
    }
}

// walker_combat.cpp:481. A living's death leaves a growing walk-over stain
// of its team on its spot and floor; when the stain cannot be made, the
// kill still happens with no stain.
TEST_F(SemWalker4, a_living_death_leaves_a_blood_stain_unless_none_can_be_made)
{
    walker* red = living_at(FAMILY_SOLDIER, 0, 96, 96);
    walker* orc = living_at(FAMILY_ORC, 1, 130, 98);
    ASSERT_TRUE(red && orc);
    red->set_damage(5000.0f);
    frail(orc);
    ASSERT_EQ(0, count_weapons(FAMILY_BLOOD));
    ASSERT_EQ(1, static_cast<int>(red->attack(orc)));
    const auto stains = weapons(FAMILY_BLOOD);
    ASSERT_EQ(1u, stains.size()) << "living killed: one stain";
    EXPECT_EQ(130, stains[0]->xpos());
    EXPECT_EQ(98, stains[0]->ypos());
    EXPECT_EQ(orc->floor(), stains[0]->floor());
    EXPECT_EQ(1, static_cast<int>(stains[0]->team_num())) << "the victim's team";
    EXPECT_EQ(1, static_cast<int>(stains[0]->ignore())) << "walked over";
    EXPECT_EQ(ANI_GROW, static_cast<int>(stains[0]->ani_type())) << "it grows";

    // add_ob failed: a factory that cannot make blood.
    fresh_world();
    auto real = world().entity_factory;
    world().entity_factory = [real](Order o, std::int32_t f) -> std::unique_ptr<walker> {
        if (o == Order::Weapon && f == FAMILY_BLOOD)
            return nullptr;
        return real(o, f);
    };
    red = living_at(FAMILY_SOLDIER, 0, 96, 96);
    orc = living_at(FAMILY_ORC, 1, 130, 98);
    ASSERT_TRUE(red && orc);
    red->set_damage(5000.0f);
    frail(orc);
    EXPECT_EQ(1, static_cast<int>(red->attack(orc))) << "add_ob failed: attack still lands";
    EXPECT_EQ(1, static_cast<int>(orc->dead())) << "add_ob failed: the victim still dies";
    EXPECT_EQ(0, count_weapons(FAMILY_BLOOD)) << "add_ob failed: no stain";
}

bool never_visible(const walker*, std::uint32_t, void*) { return false; }

// walker_combat.cpp:490. A living's death cries DIE1 on a coin of 1 and DIE2
// on 0; when the harness says the cry is not heard, there is no cry and no
// coin.
TEST_F(SemWalker4, a_death_cries_die1_or_die2_by_a_coin_unless_unheard)
{
    struct Run
    {
        int die1 = 0;
        int die2 = 0;
        std::vector<std::uint32_t> bounds;
    };
    auto run = [this](std::uint32_t coin, bool heard) {
        fresh_world();
        walker* red = living_at(FAMILY_SOLDIER, 0, 96, 96);
        walker* orc = living_at(FAMILY_ORC, 1, 128, 96);
        EXPECT_TRUE(red && orc);
        red->set_damage(5000.0f);
        frail(orc);
        if (!heard)
            current_game->positional_sound_visible = &never_visible;
        events().clear();
        CoinSpy spy(coin);
        Run r;
        {
            ScopedSimRandom scope(&spy);
            red->attack(orc);
        }
        current_game->positional_sound_visible = nullptr;
        EXPECT_EQ(1, static_cast<int>(orc->dead()));
        r.die1 = count_sounds(SOUND_DIE1);
        r.die2 = count_sounds(SOUND_DIE2);
        r.bounds = spy.bounds;
        return r;
    };
    const Run one = run(1, true);
    EXPECT_EQ(1, one.die1) << "rng 1: DIE1";
    EXPECT_EQ(0, one.die2) << "rng 1: not DIE2";
    const Run zero = run(0, true);
    EXPECT_EQ(0, zero.die1) << "rng 0: not DIE1";
    EXPECT_EQ(1, zero.die2) << "rng 0: DIE2";
    const Run quiet = run(1, false);
    EXPECT_EQ(0, quiet.die1) << "not visible: no DIE1";
    EXPECT_EQ(0, quiet.die2) << "not visible: no DIE2";
    // The heard run draws exactly one coin more than the unheard one.
    ASSERT_EQ(one.bounds.size(), quiet.bounds.size() + 1)
        << "not visible: the cry's coin is not drawn";
    EXPECT_EQ(std::count(one.bounds.begin(), one.bounds.end(), 2u),
              std::count(quiet.bounds.begin(), quiet.bounds.end(), 2u) + 1)
        << "the missing draw is the bound-2 coin";
}

// ---------------------------------------------------------------------------
// walker_movement.cpp
// ---------------------------------------------------------------------------

// walker_movement.cpp:51. With no world (so no collision map), setxy only
// repositions the walker.
TEST_F(SemWalker4, setxy_without_a_world_map_only_repositions)
{
    auto w = std::make_unique<walker>();
    w->set_sizex(16);
    w->set_sizey(16);
    GameWorld* const saved = current_game->world;
    current_game->world = nullptr;
    const bool ok = w->setxy(40, 50);
    current_game->world = saved;
    EXPECT_TRUE(ok) << "no world map: setxy succeeds";
    EXPECT_EQ(40, w->xpos()) << "no world map: xpos";
    EXPECT_EQ(50, w->ypos()) << "no world map: ypos";
    EXPECT_FLOAT_EQ(40.0f, w->worldx()) << "no world map: worldx";
    EXPECT_FLOAT_EQ(50.0f, w->worldy()) << "no world map: worldy";
    EXPECT_EQ(0u, world().myobmap->walker_to_pos.count(w.get()))
        << "no world map: the world's map never heard of it";
}

// walker_movement.cpp:73. A tangible walker moved by setworldxy is
// re-bucketed at the TRUNCATED pixel position.
TEST_F(SemWalker4, setworldxy_rebuckets_a_tangible_walker_at_the_truncated_pixel)
{
    walker* w = living_at(FAMILY_SOLDIER, 0, 96, 96);
    ASSERT_NE(nullptr, w);
    // (111.9, 47.9) truncates to (111, 47): a 16x16 body spans only
    // bucket (3, 1); rounded to (112, 48) it would span four buckets.
    w->setworldxy(111.9f, 47.9f);
    EXPECT_EQ(111, w->xpos());
    EXPECT_EQ(47, w->ypos());
    auto it = world().myobmap->walker_to_pos.find(w);
    ASSERT_NE(world().myobmap->walker_to_pos.end(), it) << "tangible: registered";
    const std::list<std::pair<short, short>> want{{3, 1}};
    EXPECT_EQ(want, it->second) << "tangible: exactly the truncated position's bucket";
    const std::list<walker*>* old_pile = world().myobmap->find_list(96, 96, 0);
    const bool still_at_old = old_pile != nullptr &&
        std::find(old_pile->begin(), old_pile->end(), w) != old_pile->end();
    EXPECT_FALSE(still_at_old) << "tangible: gone from the old bucket";
    const std::list<walker*>* new_pile = world().myobmap->find_list(111, 47, 0);
    ASSERT_NE(nullptr, new_pile);
    EXPECT_EQ(1, std::count(new_pile->begin(), new_pile->end(), w));
}

// walker_movement.cpp:176. A blocked full step is retried as a one-unit
// baby step before any fallback.
TEST_F(SemWalker4, a_blocked_full_step_is_retried_as_a_baby_step)
{
    ScriptedWalkWalker sw;
    sw.set_stepsize(3.0f);
    sw.set_user(-1);
    sw.set_curdir(static_cast<signed char>(FACE_RIGHT));
    sw.set_walk_results({false, true});
    EXPECT_TRUE(sw.walkstep(1, 0)) << "full blocked, baby passes: true";
    ASSERT_EQ(2u, sw.calls().size()) << "full blocked, baby passes: no fallback attempt";
    EXPECT_EQ(std::make_pair(3.0f, 0.0f), sw.calls()[0]) << "the full step first";
    EXPECT_EQ(std::make_pair(1.0f, 0.0f), sw.calls()[1]) << "then the one-unit baby step";

    // On real terrain (16 px cells): three pixels east is a boulder, one
    // is not.
    tile(10, 5, PIX_BOULDER_1);
    walker* w = living_at(FAMILY_SOLDIER, 1, 142, 80);
    ASSERT_NE(nullptr, w);
    w->set_stepsize(3.0f);
    w->set_user(-1);
    w->set_curdir(static_cast<signed char>(FACE_RIGHT));
    EXPECT_TRUE(w->walkstep(1, 0)) << "full blocked, baby passes: true";
    EXPECT_EQ(143, w->xpos()) << "full blocked, baby passes: moved by exactly one pixel";
    EXPECT_EQ(80, w->ypos()) << "full blocked, baby passes: no sidestep";
}

// walker_movement.cpp:183. An NPC whose facing is none of the eight makes
// no fallback move, fails, and keeps its facing.
TEST_F(SemWalker4, an_npc_with_an_invalid_facing_makes_no_fallback_move)
{
    ScriptedWalkWalker sw;
    sw.set_stepsize(2.0f);
    sw.set_user(-1);
    sw.setxy(64, 64);
    sw.set_curdir(static_cast<signed char>(FACE_LEFT));
    sw.force_facing(99);
    sw.set_walk_results({false, false});
    EXPECT_FALSE(sw.walkstep(0, -1)) << "invalid facing: false";
    EXPECT_EQ(2u, sw.calls().size()) << "invalid facing: the full and baby steps only";
    EXPECT_EQ(FACE_LEFT, static_cast<int>(sw.curdir())) << "invalid facing: curdir kept";
    EXPECT_EQ(64, sw.xpos());
    EXPECT_EQ(64, sw.ypos());
}

// walker_movement.cpp:286 (triage: quirk). A player's wall slide advances
// the walk frame one cycle past the stored cycle (not one per pixel), drawn
// facing the axis that last moved alone; curdir is put back afterwards.
TEST_F(SemWalker4, a_player_slide_draws_the_moved_axis_one_cycle_on_classic)
{
    struct Slide
    {
        const char* arm;
        short x, y;
        int dir;      // the diagonal pushed
        float dx, dy;
        int drawn;    // the facing whose row the frame comes from
        short end_x, end_y;
        bool blockers;
    };
    const Slide slides[] = {
        // Off the top edge: only the horizontal axis moves.
        {"only horizontal moved", 32, 0, FACE_UP_RIGHT, 1, -1, FACE_RIGHT, 34, 0, false},
        // Off the left edge: only the vertical axis moves.
        {"only vertical moved", 0, 32, FACE_UP_LEFT, -1, -1, FACE_UP, 0, 30, false},
        // Two 1x1 posts: A blocks the full step, B the baby step and the
        // first pixel's horizontal; the second pixel moves both axes.
        {"both moved", 200, 200, FACE_UP_RIGHT, 1, -1, FACE_UP, 201, 198, true},
    };
    for (const Slide& s : slides)
    {
        fresh_world();
        walker* w = living_at(FAMILY_SOLDIER, 0, s.x, s.y);
        ASSERT_NE(nullptr, w);
        if (s.blockers)
        {
            // Friendly tents: a hostile body would make the slider turn to
            // fight it (init_fire), a friendly living would be shoved.
            walker* a = world().add_ob(Order::Generator, FAMILY_TENT);
            walker* b = world().add_ob(Order::Generator, FAMILY_TENT);
            ASSERT_TRUE(a && b);
            a->set_team_num(0);
            b->set_team_num(0);
            ASSERT_TRUE(a && b);
            // collide() shrinks every box by a pixel a side, so a 16 px body
            // at x meets a 1x1 body only at columns x+1..x+14.
            place(a, static_cast<short>(s.x + 16), s.y, 1, 1);
            place(b, static_cast<short>(s.x + 15), static_cast<short>(s.y + 13), 1, 1);
        }
        w->set_stepsize(2.0f);
        w->set_user(0);
        w->set_curdir(static_cast<signed char>(s.dir));
        w->set_cycle(0);
        ASSERT_NE(nullptr, w->ani);
        const signed char want = w->ani[s.drawn][1];
        ASSERT_NE(want, w->ani[s.dir][1]) << s.arm << ": the rows must differ to tell them apart";
        EXPECT_FALSE(w->walkstep(s.dx, s.dy)) << s.arm << ": the slide reports failure";
        EXPECT_EQ(s.end_x, w->xpos()) << s.arm;
        EXPECT_EQ(s.end_y, w->ypos()) << s.arm;
        EXPECT_EQ(1, static_cast<int>(w->cycle()))
            << s.arm << ": moved anything: cycle = stored cycle + 1 (not one per pixel)";
        EXPECT_EQ(static_cast<int>(want), static_cast<int>(w->frame()))
            << s.arm << ": the frame of facing " << s.drawn << "'s row";
        EXPECT_EQ(s.dir, static_cast<int>(w->curdir())) << s.arm << ": after loop: curdir restored";
    }
}

// walker_movement.cpp:426 (#470). A player's wall slide tries one pixel per
// STARTED unit of stepsize, as the old game's `for (i = 0; i < step; i++)`
// does with an int counter and a float step: ceil(step) pixels, not
// floor(step). A wall row above the slider blocks the full step, the baby
// step and every pixel up; the cells along the wall are open.
TEST_F(SemWalker4, a_wall_slide_moves_one_pixel_per_started_unit_of_stepsize)
{
    struct Slide
    {
        const char* arm;
        float step;
        int dir;
        float dx;
        short end_x;
    };
    const Slide slides[] = {
        {"up-right, stepsize 2.2: three pixels", 2.2f, FACE_UP_RIGHT, 1.0f, 67},
        {"up-right, stepsize 2.0: two pixels", 2.0f, FACE_UP_RIGHT, 1.0f, 66},
        {"up-right, stepsize 1.5: two pixels", 1.5f, FACE_UP_RIGHT, 1.0f, 66},
        {"up-left, stepsize 2.2: three pixels", 2.2f, FACE_UP_LEFT, -1.0f, 61},
    };
    for (const Slide& s : slides)
    {
        fresh_world();
        for (int tx = 0; tx < 64; ++tx)
            tile(tx, 4, PIX_H_WALL1);  // y 64..79: the wall above the slider
        walker* w = living_at(FAMILY_SOLDIER, 0, 64, 80);
        ASSERT_NE(nullptr, w) << s.arm;
        w->set_stepsize(s.step);
        w->set_user(0);
        w->set_curdir(static_cast<signed char>(s.dir));
        EXPECT_FALSE(w->walkstep(s.dx, -1.0f)) << s.arm << ": the slide reports failure";
        EXPECT_EQ(s.end_x, w->xpos()) << s.arm;
        EXPECT_EQ(80, w->ypos()) << s.arm << ": the wall keeps it from moving up";
    }
}

// walker_movement.cpp:326. A stationary living's walk only turns it.
TEST_F(SemWalker4, a_stationary_living_walk_only_sets_its_facing)
{
    walker* t = world().add_ob(Order::Living, FAMILY_TOWER1);
    ASSERT_NE(nullptr, t);
    place(t, 64, 64, t->sizex(), t->sizey());
    t->set_curdir(static_cast<signed char>(FACE_UP));
    t->set_cycle(0);
    t->set_stepsize(2.0f);
    EXPECT_TRUE(t->walker::walk(1.0f, 0.0f)) << "stationary: true";
    EXPECT_EQ(FACE_RIGHT, static_cast<int>(t->curdir())) << "stationary: faces the walk";
    EXPECT_EQ(64, t->xpos()) << "stationary: no move";
    EXPECT_EQ(64, t->ypos()) << "stationary: no move";
    EXPECT_EQ(0, static_cast<int>(t->cycle())) << "stationary: no frame step";

    // The production route: walk() repeats lastx/lasty through walker::walk.
    t->set_lastx(1.0f);
    t->set_lasty(1.0f);
    EXPECT_TRUE(t->walk());
    EXPECT_EQ(FACE_DOWN_RIGHT, static_cast<int>(t->curdir()));
    EXPECT_EQ(64, t->xpos());
    EXPECT_EQ(64, t->ypos());
}

// walker_movement.cpp:436. After a turn a mobile walker heads one stepsize
// along its new facing, for each of the eight facings.
TEST_F(SemWalker4, a_turn_heads_a_mobile_walker_one_stepsize_along_each_facing)
{
    walker* w = living_at(FAMILY_SOLDIER, 0, 200, 200);
    ASSERT_NE(nullptr, w);
    w->set_stepsize(2.0f);
    const float unit[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1},
                              {0, 1},  {-1, 1}, {-1, 0}, {-1, -1}};
    for (int f = 0; f < 8; ++f)
    {
        w->set_curdir(static_cast<signed char>((f + 7) % 8));
        w->set_lastx(9.0f);
        w->set_lasty(9.0f);
        EXPECT_TRUE(w->turn(static_cast<short>(f)));
        EXPECT_EQ(f, static_cast<int>(w->curdir())) << "facing " << f;
        EXPECT_FLOAT_EQ(2.0f * unit[f][0], w->lastx()) << "mobile, facing " << f << ": lastx";
        EXPECT_FLOAT_EQ(2.0f * unit[f][1], w->lasty()) << "mobile, facing " << f << ": lasty";
    }
}

// walker_movement.cpp:629 (#446, fix AT). With the Gameplay Fixes setting
// on, a walker asked to face the way it already faces does nothing: from
// FACE_RIGHT, turn(FACE_RIGHT) leaves the facing, the heading (a sentinel
// 9,9 that a real turn would rewrite) and the walk frame (cycle 3, which a
// real turn resets to 0) as they were, and reports done. The same holds for
// all eight facings. Control, the setting still on: only distance 0 is
// gated; turn(FACE_DOWN) from FACE_RIGHT still steps one notch clockwise to
// FACE_DOWN_RIGHT and heads one stepsize along it.
TEST_F(SemWalker4, a_walker_asked_to_face_the_way_it_faces_stays_put_with_the_setting_on)
{
    world().gameplay_fixes = 1;
    walker* w = living_at(FAMILY_SOLDIER, 0, 200, 200);
    ASSERT_NE(nullptr, w);
    w->set_stepsize(2.0f);

    w->set_curdir(static_cast<signed char>(FACE_RIGHT));
    w->set_lastx(9.0f);
    w->set_lasty(9.0f);
    w->set_cycle(3);
    EXPECT_TRUE(w->turn(static_cast<short>(FACE_RIGHT))) << "the turn reports done";
    EXPECT_EQ(FACE_RIGHT, static_cast<int>(w->curdir())) << "the facing stays";
    EXPECT_FLOAT_EQ(9.0f, w->lastx()) << "the heading is not rewritten";
    EXPECT_FLOAT_EQ(9.0f, w->lasty()) << "the heading is not rewritten";
    EXPECT_EQ(3, static_cast<int>(w->cycle())) << "the walk frame is not reset";
    EXPECT_EQ(200, w->xpos());
    EXPECT_EQ(200, w->ypos());

    for (int f = 0; f < 8; ++f)
    {
        w->set_curdir(static_cast<signed char>(f));
        EXPECT_TRUE(w->turn(static_cast<short>(f)));
        EXPECT_EQ(f, static_cast<int>(w->curdir())) << "facing " << f << " stays";
    }

    w->set_curdir(static_cast<signed char>(FACE_RIGHT));
    w->set_cycle(3);
    EXPECT_TRUE(w->turn(static_cast<short>(FACE_DOWN)));
    EXPECT_EQ(FACE_DOWN_RIGHT, static_cast<int>(w->curdir())) << "control: a real turn still steps one notch";
    EXPECT_FLOAT_EQ(2.0f, w->lastx()) << "control: heading along DOWN_RIGHT";
    EXPECT_FLOAT_EQ(2.0f, w->lasty()) << "control: heading along DOWN_RIGHT";
    EXPECT_EQ(0, static_cast<int>(w->cycle())) << "control: a real turn resets the walk frame";
}

// walker_movement.cpp:649 (#446, fix AT's off column). With the setting off
// (the classic game and the old game), turn(d) from a walker already facing
// d takes the +7 arm: from FACE_RIGHT, turn(FACE_RIGHT) steps one notch
// counter-clockwise to FACE_UP_RIGHT, heads one stepsize along it (+2, -2)
// and resets the walk frame; every facing f goes to (f + 7) % 8.
TEST_F(SemWalker4, classic_a_walker_asked_to_face_the_way_it_faces_turns_a_notch_counter_clockwise)
{
    world().gameplay_fixes = 0;
    walker* w = living_at(FAMILY_SOLDIER, 0, 200, 200);
    ASSERT_NE(nullptr, w);
    w->set_stepsize(2.0f);

    w->set_curdir(static_cast<signed char>(FACE_RIGHT));
    w->set_lastx(9.0f);
    w->set_lasty(9.0f);
    w->set_cycle(3);
    EXPECT_TRUE(w->turn(static_cast<short>(FACE_RIGHT))) << "the turn reports done";
    EXPECT_EQ(FACE_UP_RIGHT, static_cast<int>(w->curdir())) << "one notch counter-clockwise";
    EXPECT_FLOAT_EQ(2.0f, w->lastx()) << "heading along UP_RIGHT";
    EXPECT_FLOAT_EQ(-2.0f, w->lasty()) << "heading along UP_RIGHT";
    EXPECT_EQ(0, static_cast<int>(w->cycle())) << "the walk frame is reset";

    for (int f = 0; f < 8; ++f)
    {
        w->set_curdir(static_cast<signed char>(f));
        EXPECT_TRUE(w->turn(static_cast<short>(f)));
        EXPECT_EQ((f + 7) % 8, static_cast<int>(w->curdir())) << "facing " << f << " steps counter-clockwise";
    }
}

// ---------------------------------------------------------------------------
// walker_pathing.cpp: follow_path_to_foe
// ---------------------------------------------------------------------------

// walker_pathing.cpp:76. With no node left, a path-follower does not move;
// a node it already stands on is dropped without a step.
TEST_F(SemWalker4, a_path_follower_with_no_node_left_stands_still)
{
    walker* w = living_at(FAMILY_SOLDIER, 0, 32, 32);
    ASSERT_NE(nullptr, w);
    w->set_stepsize(2.0f);
    w->set_curdir(static_cast<signed char>(FACE_RIGHT));
    w->path_to_foe.clear();
    w->follow_path_to_foe();
    EXPECT_EQ(32, w->xpos()) << "path empty: no move";
    EXPECT_EQ(32, w->ypos()) << "path empty: no move";
    EXPECT_TRUE(w->path_to_foe.empty());

    w->path_to_foe = {cell_state(2, 2)};  // its own cell
    w->follow_path_to_foe();
    EXPECT_TRUE(w->path_to_foe.empty()) << "the reached node is erased";
    EXPECT_EQ(32, w->xpos()) << "then the path is empty: no move";
    EXPECT_EQ(32, w->ypos()) << "then the path is empty: no move";
}

// walker_pathing.cpp:109. The convex-corner alignment assist.
class CornerAssist : public SemWalker4 {
protected:
    // Walls on row 9 everywhere and on row 11 except column 10: the one-lane
    // gap at (10, 11) beside the corridor row 10.
    void corridor()
    {
        for (int x = 0; x < world().grid.w; ++x)
        {
            tile(x, 9, PIX_H_WALL1);
            if (x != 10)
                tile(x, 11, PIX_H_WALL1);
        }
    }

    walker* chaser(short x, short y, int dir)
    {
        walker* w = living_at(FAMILY_ORC, 1, x, y);
        if (w == nullptr)
            return nullptr;
        w->set_stepsize(2.0f);
        w->set_curdir(static_cast<signed char>(dir));
        return w;
    }
};

TEST_F(CornerAssist, a_misaligned_walker_slides_into_alignment_at_a_blocked_corner)
{
    corridor();
    walker* w = chaser(160, 174, FACE_LEFT);  // 14 px below row 10's top
    ASSERT_NE(nullptr, w);
    w->path_to_foe = {cell_state(9, 10)};  // cardinal west; direct step hits (9, 11)
    ASSERT_FALSE(world().query_passable(158.0f, 174.0f, w)) << "the direct step is blocked";
    w->follow_path_to_foe();
    EXPECT_EQ(160, w->xpos()) << "slide possible: no walkstep this tick";
    EXPECT_EQ(172, w->ypos()) << "slide possible: min(stepsize 2, mis 14) px up";
    EXPECT_EQ(1u, w->path_to_foe.size()) << "slide possible: the node is kept";
    for (int i = 0; i < 6; ++i)
        w->follow_path_to_foe();
    EXPECT_EQ(160, w->xpos());
    EXPECT_EQ(160, w->ypos()) << "seven slides of 2 px: aligned";
    w->follow_path_to_foe();
    EXPECT_EQ(158, w->xpos()) << "aligned: the normal step west";
    EXPECT_EQ(160, w->ypos());
}

TEST_F(CornerAssist, a_slide_blocked_at_its_first_pixel_falls_back_to_the_normal_step)
{
    corridor();
    walker* w = chaser(160, 174, FACE_LEFT);
    walker* post = world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_TRUE(w && post);
    post->set_team_num(1);
    place(post, 160, 159);  // meets the chaser one pixel up, not where it stands
    ASSERT_TRUE(world().query_passable(160.0f, 174.0f, w));
    ASSERT_FALSE(world().query_passable(160.0f, 173.0f, w));
    w->path_to_foe = {cell_state(9, 10)};
    w->follow_path_to_foe();
    // walkstep(-1, 0): the full and baby steps west hit the wall at (9, 11),
    // so the NPC fallback for LEFT steps DOWN one stepsize.
    EXPECT_EQ(160, w->xpos()) << "slide blocked at first pixel: normal walkstep fallback";
    EXPECT_EQ(176, w->ypos()) << "slide blocked at first pixel: not 173 (no slide), the NPC sidestep";
    EXPECT_EQ(FACE_LEFT, static_cast<int>(w->curdir()));
    EXPECT_EQ(1u, w->path_to_foe.size());
}

TEST_F(CornerAssist, an_aligned_or_open_or_diagonal_hop_takes_the_normal_step)
{
    corridor();
    walker* w = chaser(160, 160, FACE_LEFT);
    ASSERT_NE(nullptr, w);
    w->path_to_foe = {cell_state(9, 10)};
    w->follow_path_to_foe();
    EXPECT_EQ(158, w->xpos()) << "aligned (mis 0): walkstep(-1, 0)";
    EXPECT_EQ(160, w->ypos());

    // Open grass: misaligned, but the direct step passes.
    fresh_world();
    w = chaser(160, 165, FACE_LEFT);
    ASSERT_NE(nullptr, w);
    w->path_to_foe = {cell_state(9, 10)};
    w->follow_path_to_foe();
    EXPECT_EQ(158, w->xpos()) << "direct step passable: walkstep(-1, 0)";
    EXPECT_EQ(165, w->ypos()) << "direct step passable: no slide";

    // A diagonal hop is never assisted.
    fresh_world();
    w = chaser(160, 165, FACE_UP_LEFT);
    ASSERT_NE(nullptr, w);
    w->path_to_foe = {cell_state(9, 9)};
    w->follow_path_to_foe();
    EXPECT_EQ(158, w->xpos()) << "diagonal hop: walkstep(-1, -1)";
    EXPECT_EQ(163, w->ypos()) << "diagonal hop: walkstep(-1, -1)";
}

// walker_pathing.cpp:153. On a multi-floor map a reached node on another
// floor is the stair: the walker nudges its CENTER over the stair cell and
// then waits with the node kept; a single-floor map consumes the node.
TEST_F(SemWalker4, a_walker_centers_on_a_stair_node_and_waits_on_a_multifloor_map)
{
    fresh_world(2);
    ASSERT_EQ(2, world().floor_count());
    tile(10, 8, PIX_ZSTAIR_UP);
    // A 32x32 body whose corner is on the stair cell but whose center is not.
    walker* big = living_at(FAMILY_ORC, 1, 160, 128);
    ASSERT_NE(nullptr, big);
    place(big, 160, 128, 32, 32);
    ASSERT_EQ(32, big->sizex());
    big->set_stepsize(2.0f);
    big->set_curdir(static_cast<signed char>(FACE_UP_LEFT));
    big->path_to_foe = {cell_state(10, 8, 1)};
    big->follow_path_to_foe();
    EXPECT_EQ(158, big->xpos()) << "center off the stair cell: walkstep toward its center";
    EXPECT_EQ(126, big->ypos()) << "center off the stair cell: walkstep(-1, -1)";
    EXPECT_EQ(1u, big->path_to_foe.size()) << "center off the stair cell: node kept";
    EXPECT_EQ(0, static_cast<int>(big->floor()));

    fresh_world(2);
    tile(10, 8, PIX_ZSTAIR_UP);
    walker* orc = living_at(FAMILY_ORC, 1, 160, 128);
    ASSERT_NE(nullptr, orc);
    orc->set_stepsize(2.0f);
    orc->set_curdir(static_cast<signed char>(FACE_UP_LEFT));
    orc->path_to_foe = {cell_state(10, 8, 1)};
    orc->follow_path_to_foe();
    EXPECT_EQ(160, orc->xpos()) << "center on stair cell: no move";
    EXPECT_EQ(128, orc->ypos()) << "center on stair cell: no move";
    EXPECT_EQ(1u, orc->path_to_foe.size()) << "center on stair cell: node kept for the floor change";

    fresh_world(1);
    orc = living_at(FAMILY_ORC, 1, 160, 128);
    ASSERT_NE(nullptr, orc);
    orc->set_stepsize(2.0f);
    orc->set_curdir(static_cast<signed char>(FACE_UP_LEFT));
    orc->path_to_foe = {cell_state(10, 8, 1)};
    orc->follow_path_to_foe();
    EXPECT_TRUE(orc->path_to_foe.empty()) << "single-floor map: node erased";
    EXPECT_EQ(160, orc->xpos()) << "single-floor map: no move";
    EXPECT_EQ(128, orc->ypos());
}

// ---------------------------------------------------------------------------
// walker_specials.cpp: the blink and its landing probe
// ---------------------------------------------------------------------------

class Blink : public SemWalker4 {
protected:
    // Boulders everywhere but the grass cell (20, 20) = px (320, 320).
    void boulder_arena(int floor_count = 1)
    {
        fresh_world(floor_count);
        paint_all(PIX_BOULDER_1);
        tile(20, 20, PIX_GRASS1);
    }

    // Floor 1's grid: all `pix`, with `hole` at cell (20, 20).
    void floor1_grid(unsigned char pix, unsigned char hole)
    {
        const int gw = world().grid.w;
        const int gh = world().grid.h;
        const std::size_t n = static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh);
        auto* buf = new unsigned char[n];
        std::fill(buf, buf + n, pix);
        buf[static_cast<std::size_t>(20 + 20 * gw)] = hole;
        world().grid_for_floor(1) = PixieData(1, static_cast<unsigned char>(gw),
                                              static_cast<unsigned char>(gh), buf);
        world().smoother_for_floor(1).set_target(world().grid_for_floor(1));
    }

    walker* mage_at(short x, short y)
    {
        walker* m = living_at(FAMILY_MAGE, 0, x, y);
        if (m != nullptr)
            m->set_user(-1);
        return m;
    }

    walker* marker_for(walker* owner, short x, short y, short life)
    {
        walker* m = world().add_ob(Order::FX, FAMILY_MARKER);
        if (m == nullptr)
            return nullptr;
        m->set_owner(owner);
        m->set_dead(0);
        place(m, x, y);
        m->set_lifetime(life);
        return m;
    }

    walker* thing_at(Order order, int family, short x, short y)
    {
        walker* t = world().add_ob(order, family);
        if (t == nullptr)
            return nullptr;
        t->set_team_num(1);
        return place(t, x, y);
    }

    int count_text(const std::string& text) const { return count_notifications(text); }
};

// walker_specials.cpp:60. What a body on the landing spot does to a blink.
TEST_F(Blink, a_blink_lands_only_where_no_solid_body_stands)
{
    struct Case
    {
        const char* arm;
        bool lands;
    };
    // (b) a potion alone: allowed, and not eaten by the probe.
    {
        boulder_arena();
        walker* mage = mage_at(96, 96);
        walker* mark = marker_for(mage, 320, 320, 5);
        walker* loot = thing_at(Order::Treasure, FAMILY_FLIGHT_POTION, 320, 320);
        ASSERT_TRUE(mage && mark && loot);
        world().rng_.state_ = 42u;
        EXPECT_TRUE(mage->teleport()) << "a treasure overlaps: landing allowed";
        EXPECT_EQ(320, mage->xpos()) << "a treasure overlaps: lands on the marker";
        EXPECT_EQ(320, mage->ypos());
        EXPECT_EQ(4, static_cast<int>(mark->lifetime()));
        EXPECT_EQ(0, static_cast<int>(loot->dead())) << "a treasure overlaps: the probe eats nothing";
        EXPECT_EQ(0, static_cast<int>(mage->flight_left())) << "a treasure overlaps: no flight drunk";
    }
    // (c) generators and solid scenery refuse the landing.
    const int bone_wall = og::families::resolve_family_string_id(Order::Weapon, "core:bone_wall");
    ASSERT_GE(bone_wall, 0);
    const struct { const char* arm; Order order; int family; } blockers[] = {
        {"a generator (tent)", Order::Generator, FAMILY_TENT},
        {"a door weapon", Order::Weapon, FAMILY_DOOR},
        {"a tree weapon", Order::Weapon, FAMILY_TREE},
        {"a boulder weapon", Order::Weapon, FAMILY_BOULDER},
        {"a weapon declaring blocks_placement (bone wall)", Order::Weapon, bone_wall},
    };
    for (const auto& b : blockers)
    {
        boulder_arena();
        walker* mage = mage_at(96, 96);
        walker* mark = marker_for(mage, 320, 320, 5);
        walker* body = thing_at(b.order, b.family, 320, 320);
        ASSERT_TRUE(mage && mark && body) << b.arm;
        world().rng_.state_ = 42u;
        EXPECT_FALSE(mage->teleport()) << b.arm << " overlaps: landing refused";
        EXPECT_EQ(96, mage->xpos()) << b.arm << " overlaps: caster stays";
        EXPECT_EQ(96, mage->ypos()) << b.arm;
        EXPECT_EQ(5, static_cast<int>(mark->lifetime())) << b.arm << ": no use spent";
    }
    // (d) a corpse in the pile, and a hidden walker forced into it.
    for (int hidden = 0; hidden < 2; ++hidden)
    {
        const char* arm = hidden ? "a hidden walker overlaps" : "a dead walker overlaps";
        boulder_arena();
        walker* mage = mage_at(96, 96);
        walker* mark = marker_for(mage, 320, 320, 5);
        walker* body = living_at(hidden ? FAMILY_SKELETON : FAMILY_SOLDIER, 1, 320, 320);
        ASSERT_TRUE(mage && mark && body) << arm;
        if (hidden)
            body->set_hidden(true);
        else
            body->set_dead(1);
        world().myobmap->add(body, 320, 320);
        const std::list<walker*>* pile = world().myobmap->find_list(320, 320, 0);
        ASSERT_NE(nullptr, pile);
        ASSERT_EQ(1, std::count(pile->begin(), pile->end(), body)) << arm << ": in the pile";
        world().rng_.state_ = 42u;
        EXPECT_TRUE(mage->teleport()) << arm << ": landing allowed";
        EXPECT_EQ(320, mage->xpos()) << arm << ": the caster lands on it";
        EXPECT_EQ(320, mage->ypos()) << arm;
        EXPECT_EQ(4, static_cast<int>(mark->lifetime())) << arm;
    }
}

// walker_specials.cpp:74. Two height-bounded bodies whose heights overlap
// block the landing.
TEST_F(Blink, a_height_overlapping_body_blocks_a_blink)
{
    boulder_arena();
    walker* mage = mage_at(96, 96);
    walker* mark = marker_for(mage, 320, 320, 10);
    walker* body = living_at(FAMILY_SOLDIER, 1, 320, 320);
    ASSERT_TRUE(mage && mark && body);
    body->set_sizez(8);
    body->set_worldz(4.0f);
    mage->set_sizez(8);
    mage->set_worldz(0.0f);
    world().rng_.state_ = 5u;
    EXPECT_FALSE(mage->teleport()) << "both bounded and height-overlapping: refused";
    EXPECT_EQ(96, mage->xpos()) << "both bounded and height-overlapping: caster stays";
    EXPECT_EQ(96, mage->ypos());
    EXPECT_EQ(10, static_cast<int>(mark->lifetime()));
}

// walker_specials.cpp:98. A permanent flyer may land over water; a walker
// on a flight potion may not, and keeps its flight ticks.
TEST_F(Blink, a_permanent_flyer_lands_over_water_but_transient_flight_does_not)
{
    fresh_world();
    paint_all(PIX_WATER1);
    walker* mage = mage_at(32, 32);
    walker* mark = marker_for(mage, 320, 320, 5);
    ASSERT_TRUE(mage && mark);
    mage->stats()->set_bit_flags(BIT_FLYING, 1);
    mage->set_flight_left(0);
    world().rng_.state_ = 42u;
    EXPECT_TRUE(mage->teleport()) << "permanently BIT_FLYING over water: landing allowed";
    EXPECT_EQ(320, mage->xpos());
    EXPECT_EQ(320, mage->ypos());
    EXPECT_EQ(4, static_cast<int>(mark->lifetime()));

    fresh_world();
    paint_all(PIX_WATER1);
    mage = mage_at(32, 32);
    mark = marker_for(mage, 320, 320, 5);
    ASSERT_TRUE(mage && mark);
    mage->stats()->set_bit_flags(BIT_FLYING, 0);
    mage->set_flight_left(200);
    world().rng_.state_ = 42u;
    EXPECT_FALSE(mage->teleport()) << "transient flight over water: refused everywhere";
    EXPECT_EQ(32, mage->xpos());
    EXPECT_EQ(32, mage->ypos());
    EXPECT_EQ(5, static_cast<int>(mark->lifetime()));
    EXPECT_EQ(200, static_cast<int>(mage->flight_left())) << "the probe restores flight_left";
}

// walker_specials.cpp:108. A same-floor landing is probed at the caster's
// own height; another floor's at that floor's ground.
TEST_F(Blink, a_landing_is_probed_at_the_casters_height_on_its_floor_and_at_ground_elsewhere)
{
    boulder_arena();
    walker* mage = mage_at(96, 96);
    walker* mark = marker_for(mage, 320, 320, 5);
    walker* body = living_at(FAMILY_SOLDIER, 1, 320, 320);
    ASSERT_TRUE(mage && mark && body);
    body->set_sizez(8);
    body->set_worldz(0.0f);
    mage->set_sizez(8);
    mage->set_worldz(40.0f);
    world().rng_.state_ = 5u;
    EXPECT_TRUE(mage->teleport()) << "same floor, caster hovering at 40: the grounded body does not block";
    EXPECT_EQ(320, mage->xpos());
    EXPECT_EQ(320, mage->ypos());
    EXPECT_EQ(4, static_cast<int>(mark->lifetime()));

    boulder_arena(2);
    ASSERT_EQ(2, world().floor_count());
    floor1_grid(PIX_BOULDER_1, PIX_GRASS1);
    tile(20, 20, PIX_BOULDER_1);  // no clear cell on floor 0: the random fallback cannot land
    mage = mage_at(96, 96);
    mark = marker_for(mage, 0, 0, 5);
    body = living_at(FAMILY_SOLDIER, 1, 0, 0);
    ASSERT_TRUE(mage && mark && body);
    body->change_floor(1);
    body->setxy(320, 320);
    mark->change_floor(1);
    mark->setxy(320, 320);
    body->set_sizez(8);
    body->set_worldz(0.0f);
    mage->set_sizez(8);
    mage->set_worldz(40.0f);
    ASSERT_EQ(1, static_cast<int>(body->floor()));
    ASSERT_EQ(1, static_cast<int>(mark->floor()));
    world().rng_.state_ = 42u;
    EXPECT_FALSE(mage->teleport()) << "other floor: probed at z 0, the grounded body blocks";
    EXPECT_EQ(0, static_cast<int>(mage->floor()));
    EXPECT_EQ(96, mage->xpos());
    EXPECT_EQ(96, mage->ypos());
    EXPECT_EQ(5, static_cast<int>(mark->lifetime()));
}

// walker_specials.cpp:254. A self-teleport stamps the tick before probing,
// whether the blink lands or not.
TEST_F(Blink, a_blink_stamps_the_self_teleport_tick_even_when_it_fails)
{
    boulder_arena();
    tile(20, 20, PIX_BOULDER_1);  // nowhere to land
    walker* mage = mage_at(96, 96);
    ASSERT_NE(nullptr, mage);
    world().tick_count_ = 77;
    world().rng_.state_ = 42u;
    EXPECT_FALSE(mage->teleport());
    EXPECT_EQ(77u, mage->last_self_teleport_tick()) << "teleport() called: stamped though it failed";

    fresh_world();
    mage = mage_at(96, 96);
    ASSERT_NE(nullptr, mage);
    world().tick_count_ = 78;
    world().rng_.state_ = 42u;
    EXPECT_TRUE(mage->teleport());
    EXPECT_EQ(78u, mage->last_self_teleport_tick()) << "teleport() called: stamped";
}

// walker_specials.cpp:262. A blink ignores markers owned by others.
TEST_F(Blink, a_blink_ignores_another_casters_marker)
{
    walker* a = mage_at(96, 96);
    walker* b = mage_at(400, 400);
    ASSERT_TRUE(a && b);
    walker* mark = marker_for(b, 320, 320, 5);
    ASSERT_NE(nullptr, mark);
    world().rng_.state_ = state_where_draws_are({64u, 64u}, {5u, 7u});
    EXPECT_TRUE(a->teleport());
    EXPECT_EQ(5 * GRID_SIZE, a->xpos()) << "only another mage's marker: the random blink's cell";
    EXPECT_EQ(7 * GRID_SIZE, a->ypos());
    EXPECT_EQ(5, static_cast<int>(mark->lifetime())) << "only another mage's marker: its uses untouched";
    EXPECT_EQ(0, static_cast<int>(mark->dead()));
}

// walker_specials.cpp:290. A marker trip spends one use; the last use kills
// the marker through death().
TEST_F(Blink, a_marker_trip_spends_one_use_and_the_last_use_kills_it)
{
    walker* mage = mage_at(20, 20);
    walker* mark = marker_for(mage, 140, 140, 5);
    ASSERT_TRUE(mage && mark);
    EXPECT_TRUE(mage->teleport());
    EXPECT_EQ(4, static_cast<int>(mark->lifetime())) << "uses left: lifetime - 1";
    EXPECT_EQ(0, static_cast<int>(mark->dead())) << "uses left: alive";
    EXPECT_EQ(0, static_cast<int>(mark->death_called())) << "uses left: no death()";

    fresh_world();
    mage = mage_at(20, 20);
    mark = marker_for(mage, 140, 140, 1);
    ASSERT_TRUE(mage && mark);
    EXPECT_TRUE(mage->teleport());
    EXPECT_EQ(1, static_cast<int>(mark->dead())) << "lifetime reaches 0: dead";
    EXPECT_EQ(1, static_cast<int>(mark->death_called())) << "lifetime reaches 0: death() run";
}

// walker_specials.cpp:299. Only a far blocked marker tells the player.
TEST_F(Blink, a_near_blocked_marker_tells_the_player_nothing)
{
    walker* mage = mage_at(96, 96);
    walker* mark = marker_for(mage, 100, 100, 5);
    walker* body = living_at(FAMILY_SOLDIER, 1, 100, 100);
    ASSERT_TRUE(mage && mark && body);
    mage->set_user(2);
    events().clear();
    world().rng_.state_ = 7u;
    mage->teleport();
    EXPECT_EQ(0, count_text("Marker is Blocked!")) << "near marker: no notification";
    EXPECT_EQ(5, static_cast<int>(mark->lifetime())) << "near marker: never used";

    // The far twin of the same blocked marker does tell the player.
    fresh_world();
    mage = mage_at(96, 96);
    mark = marker_for(mage, 320, 320, 5);
    body = living_at(FAMILY_SOLDIER, 1, 320, 320);
    ASSERT_TRUE(mage && mark && body);
    mage->set_user(2);
    events().clear();
    world().rng_.state_ = 7u;
    mage->teleport();
    EXPECT_EQ(1, count_text("Marker is Blocked!")) << "player caster, far blocked marker";
}

// walker_specials.cpp:352. The short escape hop stamps the tick too.
TEST_F(Blink, a_ranged_hop_stamps_the_self_teleport_tick_even_when_it_fails)
{
    walker* sk = living_at(FAMILY_SKELETON, 1, 200, 200);
    ASSERT_NE(nullptr, sk);
    world().tick_count_ = 55;
    world().rng_.state_ = 42u;
    EXPECT_TRUE(sk->teleport_ranged(40));
    EXPECT_EQ(55u, sk->last_self_teleport_tick()) << "teleport_ranged called: stamped";

    fresh_world();
    paint_all(PIX_BOULDER_1);
    sk = living_at(FAMILY_SKELETON, 1, 200, 200);
    ASSERT_NE(nullptr, sk);
    world().tick_count_ = 56;
    world().rng_.state_ = 42u;
    EXPECT_FALSE(sk->teleport_ranged(40));
    EXPECT_EQ(56u, sk->last_self_teleport_tick()) << "teleport_ranged called: stamped though it failed";
}

// The world LCG's next draw of bound `max` (SimRandom::next), stepping `s`.
static int lcg_draw(std::uint32_t& s, int max)
{
    s = s * 1103515245u + 12345u;
    return static_cast<int>((s >> 16) % static_cast<std::uint32_t>(max));
}

// walker_specials.cpp:466 (#471). With no usable marker the blink draws
// random cells until one is clear and tests every draw, as the old game's
// unbounded loop does. This game used to stop after 200 draws and never
// tested the 200th. Only the cell of the nth draw is clear: the caster lands
// there, and the world stream has advanced exactly n (x, y) pairs.
TEST_F(Blink, a_random_blink_tests_every_draw_and_lands_on_a_cell_first_drawn_after_two_hundred_tries)
{
    for (const int nth : {200, 250})
    {
        fresh_world();
        const int gw = world().grid.w;
        const int gh = world().grid.h;
        // The first seed whose nth draw names a cell no earlier draw named
        // (and not the caster's own cell, (6, 6)).
        std::uint32_t seed = 0;
        std::uint32_t after = 0;
        int cell = -1;
        for (std::uint32_t cand = 1; cell < 0; ++cand)
        {
            std::uint32_t s = cand;
            std::vector<bool> seen(static_cast<std::size_t>(gw * gh), false);
            for (int i = 1; i <= nth; ++i)
            {
                const int x = lcg_draw(s, gw);
                const int c = x + gw * lcg_draw(s, gh);
                if (i == nth && !seen[static_cast<std::size_t>(c)] && c != 6 + gw * 6)
                {
                    seed = cand;
                    after = s;
                    cell = c;
                }
                seen[static_cast<std::size_t>(c)] = true;
            }
        }
        paint_all(PIX_BOULDER_1);
        tile(cell % gw, cell / gw, PIX_GRASS1);
        walker* mage = mage_at(96, 96);
        ASSERT_NE(nullptr, mage);
        world().rng_.state_ = seed;
        EXPECT_TRUE(mage->teleport()) << "the clear cell is draw " << nth << ": it lands";
        EXPECT_EQ((cell % gw) * GRID_SIZE, mage->xpos()) << "draw " << nth;
        EXPECT_EQ((cell / gw) * GRID_SIZE, mage->ypos()) << "draw " << nth;
        EXPECT_EQ(after, world().rng_.state_) << "exactly " << nth << " (x, y) pairs drawn";
    }
}

// walker_specials.cpp:490 (#471). With no clear cell the old game loops
// forever; this game gives up after 64 draws per cell per floor (here 64 x
// 64 x 64 pairs) and leaves the caster where it was.
TEST_F(Blink, a_random_blink_with_no_clear_cell_gives_up_and_stays_put)
{
    paint_all(PIX_BOULDER_1);
    walker* mage = mage_at(96, 96);
    ASSERT_NE(nullptr, mage);
    const int gw = world().grid.w;
    const int gh = world().grid.h;
    std::uint32_t s = 42u;
    for (int i = 0; i < 64 * gw * gh; ++i)
    {
        lcg_draw(s, gw);
        lcg_draw(s, gh);
    }
    world().rng_.state_ = 42u;
    EXPECT_FALSE(mage->teleport()) << "no clear cell: the cast gives up";
    EXPECT_EQ(96, mage->xpos()) << "no clear cell: the caster stays";
    EXPECT_EQ(96, mage->ypos());
    EXPECT_EQ(s, world().rng_.state_) << "exactly 64 x cells pairs drawn before giving up";
}

// ---------------------------------------------------------------------------
// walker_specials.cpp: walker::special
// ---------------------------------------------------------------------------

// walker_specials.cpp:210 and :228. A family with no special hook is
// refused for nothing, with the default reason, traced.
TEST_F(SemWalker4, a_family_with_no_special_hook_is_refused_free_with_the_default_reason)
{
    walker* w = living_at(FAMILY_SOLDIER, 0, 96, 96);
    ASSERT_NE(nullptr, w);
    w->set_order_family(Order::Living, static_cast<char>(120));
    w->set_current_special(1);
    w->set_shifter_down(0);
    w->stats()->set_special_cost(1, 10);
    w->stats()->set_magicpoints(100.0f);
    walker::SpecialFailure why = walker::SpecialFailure::None;
    std::string reason;
    EXPECT_FALSE(w->special(&why, &reason));
    EXPECT_EQ(walker::SpecialFailure::ScriptDeclined, why) << "no hook for the family: declined";
    EXPECT_EQ("NO SPECIAL AVAILABLE", reason) << "no hook / empty reason: the default reason";
    EXPECT_FLOAT_EQ(100.0f, w->stats()->magicpoints()) << "no hook for the family: MP unchanged";
    // (The census TRACE line is a no-op in og_gameplay as unit groups link
    // it, without -DTESTING; the returned reason is the same expression.)
}

// ---------------------------------------------------------------------------
// walker_specials.cpp: walker::turn_undead
// ---------------------------------------------------------------------------

class TurnUndead : public SemWalker4 {
protected:
    static constexpr std::int32_t kRange = 64;
    static constexpr std::uint32_t kRoll = static_cast<std::uint32_t>(kRange * 40);
};

// walker_specials.cpp:619 (#474, fix AH). A warded undead (BIT_INVINCIBLE,
// or an invulnerability potion running) is turned the old game's way with
// the Gameplay Fixes setting off: the kill loop asks only the family, so the
// skeleton draws its roll and dies. With the setting on it is spared before
// any roll: nothing else in reach is turnable, so the cast is refused (-2)
// and draws nothing. An unwarded skeleton is still turned with it on.
TEST_F(TurnUndead, a_warded_undead_is_turned_with_the_fixes_off_and_spared_with_them_on)
{
    constexpr std::uint32_t kFixesRoll = 4u * 160u;  // power 4: the fixes' roll bound
    struct Ward
    {
        const char* arm;
        bool invincible;
        short potion;
    };
    const Ward wards[] = {{"BIT_INVINCIBLE", true, 0}, {"an invulnerability potion", false, 50}};
    for (const Ward& ward : wards)
    {
        for (short fixes = 0; fixes < 2; ++fixes)
        {
            fresh_world();
            world().gameplay_fixes = fixes;
            walker* cleric = living_at(FAMILY_CLERIC, 0, 200, 264);
            walker* sk = living_at(FAMILY_SKELETON, 1, 216, 264);
            ASSERT_TRUE(cleric && sk);
            sk->stats()->set_level(1);
            if (ward.invincible)
                sk->stats()->set_bit_flags(BIT_INVINCIBLE, 1);
            sk->set_invulnerable_left(ward.potion);
            RollSpy spy(fixes != 0 ? kFixesRoll : kRoll);
            std::int32_t killed = 0;
            {
                ScopedSimRandom scope(&spy);
                killed = cleric->turn_undead(kRange, 4);
            }
            if (fixes == 0)
            {
                EXPECT_EQ(1, killed) << ward.arm << ", fixes off: the old game turns it";
                EXPECT_EQ(1, static_cast<int>(sk->dead())) << ward.arm << ", fixes off";
                EXPECT_EQ((std::vector<std::uint32_t>{kRoll, 10u}), spy.bounds)
                    << ward.arm << ", fixes off: the caster's roll, then the level-1 victim's";
            }
            else
            {
                EXPECT_EQ(-2, killed) << ward.arm << ", fixes on: nothing turnable in reach";
                EXPECT_EQ(0, static_cast<int>(sk->dead())) << ward.arm << ", fixes on: spared";
                EXPECT_FLOAT_EQ(400.0f, sk->stats()->hitpoints()) << ward.arm << ", fixes on";
                EXPECT_EQ(0u, spy.bounds.size()) << ward.arm << ", fixes on: no roll drawn";
            }
        }
    }

    fresh_world();
    world().gameplay_fixes = 1;
    walker* cleric = living_at(FAMILY_CLERIC, 0, 200, 264);
    walker* sk = living_at(FAMILY_SKELETON, 1, 216, 264);
    ASSERT_TRUE(cleric && sk);
    sk->stats()->set_level(1);
    RollSpy spy(kFixesRoll);
    std::int32_t killed = 0;
    {
        ScopedSimRandom scope(&spy);
        killed = cleric->turn_undead(kRange, 4);
    }
    EXPECT_EQ(1, killed) << "unwarded, fixes on: turned";
    EXPECT_EQ(1, static_cast<int>(sk->dead())) << "unwarded, fixes on";
    EXPECT_EQ((std::vector<std::uint32_t>{kFixesRoll, 10u}), spy.bounds) << "unwarded, fixes on";
}

// walker_specials.cpp:418. A scripted level whose mode failed to start
// turns undead the classic way: a pre-kill with no attribution and no
// death(); an active mode routes the kill through death().
TEST_F(TurnUndead, a_scripted_level_whose_mode_failed_turns_undead_the_classic_way)
{
    for (int active = 0; active < 2; ++active)
    {
        fresh_world();
        world().gameplay_fixes = 0;
        world().type = GameWorld::TYPE_SCRIPTED;
        world().mode.init_attempted = true;
        world().mode.active = active != 0;
        walker* cleric = living_at(FAMILY_CLERIC, 0, 200, 264);
        walker* sk = living_at(FAMILY_SKELETON, 1, 216, 264);
        ASSERT_TRUE(cleric && sk);
        sk->stats()->set_level(1);
        RollSpy spy(kRoll);
        std::int32_t killed = 0;
        {
            ScopedSimRandom scope(&spy);
            killed = cleric->turn_undead(kRange, 4);
        }
        EXPECT_EQ(1, killed);
        EXPECT_EQ(1, static_cast<int>(sk->dead()));
        if (active == 0)
        {
            EXPECT_EQ(0, static_cast<int>(sk->death_called()))
                << "scripted level whose mode init failed: classic pre-kill, no death()";
            EXPECT_EQ(0u, sk->last_attacker_id())
                << "scripted level whose mode init failed: no attribution stamp";
        }
        else
        {
            EXPECT_EQ(1, static_cast<int>(sk->death_called())) << "scripted, mode active: death() run";
            EXPECT_EQ(cleric->entity_id(), sk->last_attacker_id()) << "scripted, mode active: stamped";
        }
    }
}

// ---------------------------------------------------------------------------
// weap.cpp
// ---------------------------------------------------------------------------

class Weap : public SemWalker4 {
protected:
    walker* knife_at(short x, short y)
    {
        walker* k = world().add_ob(Order::Weapon, FAMILY_KNIFE);
        if (k == nullptr)
            return nullptr;
        k->set_team_num(0);
        k->set_dead(0);
        k->set_death_called(0);
        k->set_ani_type(ANI_WALK);  // else act() goes straight to animate()
        return place(k, x, y, 8, 8);
    }

    int count_fx(int family) const
    {
        int n = 0;
        for (const auto& u : tw_->world().oblist)
            if (u && u->query_order() == Order::FX && u->family() == family && !u->dead())
                ++n;
        for (const auto& u : tw_->world().fxlist)
            if (u && u->query_order() == Order::FX && u->family() == family && !u->dead())
                ++n;
        return n;
    }
};

// weap.cpp:85. A shot over trees loses range per tick, never below zero.
TEST_F(Weap, a_shot_over_trees_spends_range_but_never_below_zero)
{
    tile(0, 0, PIX_TREE_M1);
    walker* k = knife_at(0, 0);
    ASSERT_NE(nullptr, k);
    k->set_act_type(ACT_SIT);
    k->set_lineofsight(3);
    k->act();
    EXPECT_EQ(2, static_cast<int>(k->lineofsight())) << "over a tree cell, range left: -1";
    k->set_ani_type(ANI_WALK);
    k->set_lineofsight(0);
    k->act();
    EXPECT_EQ(0, static_cast<int>(k->lineofsight())) << "range 0: unchanged";
}

// weap.cpp:100 (#459). The range decay over trees reads the weapon's own
// floor, keeping the classic quirk of passing pixel coordinates as grid
// coordinates. On a two-floor map a shot upstairs at pixel (10, 10) keeps its
// range over floor 0's tree cell (10, 10) and spends one over floor 1's own
// tree cell (20, 20); a shot on floor 0 over the floor-0 tree spends one.
TEST_F(Weap, a_shot_upstairs_spends_range_only_over_its_own_floors_trees)
{
    auto two_floor_world = [&]() {
        fresh_world(2);
        tile(10, 10, PIX_TREE_M1);  // floor 0
        GameWorld& w = world();
        const std::size_t cells = static_cast<std::size_t>(w.grid.w) * w.grid.h;
        auto* buf = new unsigned char[cells];
        std::fill(buf, buf + cells, static_cast<unsigned char>(PIX_GRASS1));
        buf[static_cast<std::size_t>(20 + w.grid.w * 20)] = PIX_TREE_M1;  // floor 1
        w.grid_for_floor(1) = PixieData(1, w.grid.w, w.grid.h, buf);
        w.smoother_for_floor(1).set_target(w.grid_for_floor(1));
    };
    auto shot = [&](short floor, short x, short y) {
        walker* k = knife_at(x, y);
        if (k == nullptr)
            return k;
        k->change_floor(floor);
        k->set_act_type(ACT_SIT);
        k->set_lineofsight(3);
        return k;
    };

    two_floor_world();
    walker* over = shot(1, 10, 10);
    ASSERT_NE(nullptr, over);
    ASSERT_EQ(1, static_cast<int>(over->floor()));
    over->act();
    EXPECT_EQ(3, static_cast<int>(over->lineofsight())) << "floor 1 over floor 0's tree: range kept";

    two_floor_world();
    walker* in = shot(1, 20, 20);
    ASSERT_NE(nullptr, in);
    in->act();
    EXPECT_EQ(2, static_cast<int>(in->lineofsight())) << "floor 1 over its own tree: range left -1";

    two_floor_world();
    walker* ground = shot(0, 10, 10);
    ASSERT_NE(nullptr, ground);
    ASSERT_EQ(0, static_cast<int>(ground->floor()));
    ground->act();
    EXPECT_EQ(2, static_cast<int>(ground->lineofsight())) << "floor 0 over its own tree: range left -1";
}

// weap.cpp:90. A weapon in a living-only act mode does nothing.
TEST_F(Weap, a_weapon_in_control_or_an_unknown_act_does_nothing)
{
    walker* k = knife_at(100, 100);
    ASSERT_NE(nullptr, k);
    k->set_act_type(ACT_CONTROL);
    k->set_lastx(1.0f);
    k->set_lasty(0.0f);
    k->set_curdir(static_cast<signed char>(FACE_RIGHT));
    EXPECT_TRUE(k->act()) << "ACT_CONTROL: returns at once";
    EXPECT_EQ(100, k->xpos()) << "ACT_CONTROL: no move";
    EXPECT_EQ(100, k->ypos()) << "ACT_CONTROL: no move";

    events().clear();
    k->set_act_type(123);
    EXPECT_FALSE(k->act()) << "unknown act: false";
    EXPECT_EQ(1, count_notifications("No act type set for weapon")) << "unknown act: notification";
    EXPECT_EQ(1, count_all_notifications()) << "unknown act: nothing else said";
    EXPECT_EQ(100, k->xpos());
    EXPECT_EQ(100, k->ypos());
}

// weap.cpp:119. A fired weapon with range left flies one step along its
// heading.
TEST_F(Weap, a_fired_weapon_with_range_left_moves_along_its_heading)
{
    walker* k = knife_at(100, 100);
    ASSERT_NE(nullptr, k);
    k->set_act_type(ACT_FIRE);
    const float step = k->stepsize();
    ASSERT_GE(step, 1.0f);
    k->set_lastx(step);
    k->set_lasty(0.0f);
    k->set_curdir(static_cast<signed char>(FACE_RIGHT));
    k->set_lineofsight(5);
    EXPECT_TRUE(k->act());
    EXPECT_EQ(4, static_cast<int>(k->lineofsight()));
    EXPECT_EQ(100 + static_cast<int>(step), k->xpos()) << "range left: moves one stepsize";
    EXPECT_EQ(100, k->ypos()) << "range left: along the heading only";
    EXPECT_EQ(0, static_cast<int>(k->dead()));
}

// weap.cpp:170. A weapon's death effects run once: the second death()
// runs nothing.
TEST_F(Weap, a_second_weapon_death_runs_no_effect)
{
    walker* archer = living_at(FAMILY_ARCHER, 0, 400, 400);
    walker* arrow = world().add_ob(Order::Weapon, FAMILY_FIRE_ARROW);
    ASSERT_TRUE(archer && arrow);
    place(arrow, 200, 200, 8, 8);
    arrow->set_owner(archer);
    arrow->set_skip_exit(1);  // armed: explodes on death
    arrow->set_damage(7.0f);
    arrow->set_dead(1);
    events().clear();
    const int before = count_fx(FAMILY_EXPLOSION);
    EXPECT_TRUE(arrow->death()) << "first death() runs the hook";
    EXPECT_EQ(before + 1, count_fx(FAMILY_EXPLOSION)) << "first death(): one explosion";
    EXPECT_EQ(1, count_sounds(SOUND_EXPLODE));
    EXPECT_FALSE(arrow->death()) << "second: returns 0";
    EXPECT_EQ(before + 1, count_fx(FAMILY_EXPLOSION)) << "second: nothing runs (no explosion)";
    EXPECT_EQ(1, count_sounds(SOUND_EXPLODE)) << "second: nothing runs (no sound)";
}

// weap.cpp:194. A weapon whose animation script says it is finished dies.
TEST_F(Weap, a_weapon_whose_animation_script_finishes_dies)
{
    walker* c = world().add_ob(Order::Weapon, FAMILY_CIRCLE_PROTECTION);
    ASSERT_NE(nullptr, c);
    place(c, 200, 200);
    c->set_owner(nullptr);
    c->set_dead(0);
    c->set_death_called(0);
    EXPECT_TRUE(c->animate()) << "hook returns false: death() ran and returned 1";
    EXPECT_EQ(1, static_cast<int>(c->death_called())) << "hook returns false: death() run";
    EXPECT_EQ(1, static_cast<int>(c->dead()));
    EXPECT_FALSE(c->animate()) << "a second finish: death() already ran, returns 0";
}

// weap.cpp:206. The default animation leaves the frame alone when its facing
// has no row, or the row is empty.
TEST_F(Weap, the_default_animation_leaves_the_frame_on_a_missing_or_empty_row)
{
    walker* a = world().add_ob(Order::Weapon, FAMILY_ARROW);
    ASSERT_NE(nullptr, a);
    place(a, 200, 200);
    static const signed char empty_row[] = {-1};
    static const signed char one_row[] = {2, -1};
    static const signed char* rows[8] = {empty_row, one_row, one_row, one_row,
                                         one_row, one_row, one_row, one_row};
    a->ani = rows;
    a->ani_count = 8;
    a->set_frame(1);
    ASSERT_EQ(1, static_cast<int>(a->frame()));
    a->set_curdir(0);
    a->set_cycle(0);
    EXPECT_TRUE(a->animate());
    EXPECT_EQ(1, static_cast<int>(a->frame())) << "empty row: frame unchanged";
    EXPECT_EQ(0, static_cast<int>(a->cycle())) << "empty row: cycle unchanged";

    a->ani_count = 2;  // facing 5 is a real facing past the table
    a->set_curdir(5);
    a->set_cycle(0);
    EXPECT_TRUE(a->animate());
    EXPECT_EQ(1, static_cast<int>(a->frame())) << "no row: frame unchanged";
    EXPECT_EQ(0, static_cast<int>(a->cycle())) << "no row: cycle unchanged";

    a->set_curdir(1);  // control: a real one-frame row is played
    a->set_cycle(0);
    EXPECT_TRUE(a->animate());
    EXPECT_EQ(2, static_cast<int>(a->frame())) << "valid facing: the row's frame";
}

} // namespace
