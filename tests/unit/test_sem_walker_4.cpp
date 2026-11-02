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
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/obmap.h>
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

} // namespace
