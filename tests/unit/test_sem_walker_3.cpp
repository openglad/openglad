/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch walker-3: the rules the review found in walker.cpp, walker_combat.cpp.
// Each test is named after the rule it asserts.
//
// Two halves. The vertical rules (falls, stairs, their cooldowns) exist only
// in the current game: the old game has one floor and no z motion, so those
// tests have no classic oracle and pin the design (docs/z-axis-design.md).
// The combat rules (attack, do_combat_damage, do_hit_effects) were read
// against the old game's walker.cpp at 7ff1654a and agree arm by arm, except
// the kill experience (#440): the old game pays a killing blow's experience
// again with the kill bonus, this game pays the bonus alone, on purpose
// (a_kill_adds_the_kill_bonus_alone_to_its_heros_experience).
//
// Randomness. TestGameWorld installs FixedRandom{0} as the combat RNG, so a
// blow of damage d lands exactly floor(d - sqrt(d)/2 + 0.5) on armour 0; the
// world's own rng_ (the cosmetic stream, the death cry) is pinned to 12345.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/combat_math.h>
#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/sound_ids.h>
#include <openglad/gameplay/event.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/kit_state.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// walker_combat.cpp's adapter; the sim declares it where it calls it.
short exp_from_action(ExpAction action, walker* w, walker* target, short value);

namespace {

bool on_screen(const walker*, std::uint32_t, void*) { return true; }

class SemWalker3 : public ::testing::Test {
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
        if (current_game != nullptr)
            current_game->positional_sound_visible = nullptr;
        tw_.reset();
    }

    // An open field of grass, 1024 px a side; every positional sound counts
    // as on screen.
    void fresh_world()
    {
        tw_.reset();
        tw_ = std::make_unique<TestGameWorld>();
        GameWorld& w = world();
        w.resize_grid(64, 64);
        std::fill_n(w.grid.data.get(),
                    static_cast<std::size_t>(w.grid.w) * w.grid.h,
                    static_cast<std::uint8_t>(PIX_GRASS1));
        w.my_team = 0;
        w.gameplay_fixes = 0;
        w.new_specials = 0;
        w.rng_.state_ = 12345u;
        current_game->positional_sound_visible = &on_screen;
    }

    GameWorld& world() { return tw_->world(); }
    og::sim::SimEventLog& events() { return tw_->events; }

    static walker* place(walker* w, short x, short y)
    {
        if (w == nullptr)
            return nullptr;
        w->setxy(static_cast<short>(x + 1), y);  // force a real move
        w->setxy(x, y);
        return w;
    }

    // A living with 400 hp, armour 0, no magic (so hit_response never casts),
    // no character record unless the test gives it one.
    walker* living_at(int family, unsigned char team, short x, short y)
    {
        walker* w = world().add_ob(Order::Living, family);
        if (w == nullptr)
            return nullptr;
        w->clear_myguy();
        w->set_team_num(team);
        w->set_real_team_num(255);
        w->set_act_type(ACT_RANDOM);
        w->stats()->set_level(4);
        w->stats()->set_max_hitpoints(400.0f);
        w->stats()->set_hitpoints(400.0f);
        w->stats()->set_armor(0.0f);
        w->stats()->set_max_magicpoints(0.0f);
        w->stats()->set_magicpoints(0.0f);
        w->stats()->set_magic_per_round(0.0f);
        w->stats()->name.clear();
        w->set_busy(0.0f);
        return place(w, x, y);
    }

    walker* hero_at(int family, unsigned char team, short x, short y)
    {
        walker* w = living_at(family, team, x, y);
        if (w != nullptr)
            w->set_owned_myguy(std::make_unique<guy>(family));
        return w;
    }

    walker* generator_at(unsigned char team, short x, short y)
    {
        walker* g = world().add_ob(Order::Generator, FAMILY_TENT);
        if (g == nullptr)
            return nullptr;
        g->set_team_num(team);
        g->stats()->set_level(3);
        g->stats()->set_armor(0.0f);
        g->stats()->set_max_hitpoints(100.0f);
        g->stats()->set_hitpoints(100.0f);
        return place(g, x, y);
    }

    walker* knife_of(walker* thrower, float damage)
    {
        walker* k = world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
        if (k == nullptr)
            return nullptr;
        k->set_team_num(thrower ? thrower->team_num() : 0);
        k->set_owner(thrower);
        k->set_damage(damage);
        k->stats()->set_level(4);  // the attack xp reads the hitter's (the knife's) level
        k->stats()->set_hitpoints(1000.0f);
        return k;
    }

    std::size_t count_events(og::sim::EventKind kind) const
    {
        std::size_t n = 0;
        for (const og::sim::Event& e : tw_->events.events())
            if (e.kind == kind)
                ++n;
        return n;
    }

    std::size_t count_sound(std::uint32_t id) const
    {
        std::size_t n = 0;
        for (const og::sim::Event& e : tw_->events.events())
            if (e.kind == og::sim::EventKind::PlaySound && e.a == id)
                ++n;
        return n;
    }

    std::array<std::uint32_t, 4> scores() const
    {
        const GameWorld& w = tw_->world();
        return {w.m_score[0], w.m_score[1], w.m_score[2], w.m_score[3]};
    }

    std::size_t count_family(const GameWorld::EntityList& list, Order order,
                             int family) const
    {
        std::size_t n = 0;
        for (const auto& w : list)
            if (w && w->query_order() == order && w->family() == family)
                ++n;
        return n;
    }

    std::vector<walker*> of_family(const GameWorld::EntityList& list,
                                   Order order, int family) const
    {
        std::vector<walker*> out;
        for (const auto& w : list)
            if (w && w->query_order() == order && w->family() == family)
                out.push_back(w.get());
        return out;
    }

    // ---- floors ----------------------------------------------------------

    // Replace floor f's (f >= 1) grid with a uniform fill; returns the buffer.
    unsigned char* fill_floor(int f, unsigned char tile)
    {
        GameWorld& w = world();
        const int gw = w.grid.w;
        const int gh = w.grid.h;
        auto* buf = new unsigned char[static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh)];
        std::fill(buf, buf + static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh), tile);
        w.grid_for_floor(f) = PixieData(1, static_cast<unsigned char>(gw),
                                        static_cast<unsigned char>(gh), buf);
        w.smoother_for_floor(f).set_target(w.grid_for_floor(f));
        return w.grid_for_floor(f).data.get();
    }

    void paint(int f, int tx, int ty, unsigned char tile)
    {
        GameWorld& w = world();
        w.grid_for_floor(f).data[static_cast<std::size_t>(tx + ty * w.grid.w)] = tile;
        if (f > 0)
            w.smoother_for_floor(f).set_target(w.grid_for_floor(f));
    }

    // `floors` floors, all grass, with a stacked air column at (sx, sy) on
    // floors lowest_air..floors-1.
    void build_shaft(int floors, int sx, int sy, int lowest_air)
    {
        world().set_floor_count(floors);
        for (int f = 1; f < floors; ++f) {
            unsigned char* buf = fill_floor(f, static_cast<unsigned char>(PIX_GRASS1));
            if (f >= lowest_air)
                buf[sx + sy * world().grid.w] = static_cast<unsigned char>(PIX_AIR);
        }
    }

    // A soldier with its centre on cell (sx, sy) of `floor`, 100 hp.
    walker* soldier_on_cell(int floor, int sx, int sy)
    {
        walker* a = world().add_ob(Order::Living, FAMILY_SOLDIER);
        if (a == nullptr)
            return nullptr;
        a->clear_myguy();
        a->set_floor(static_cast<short>(floor));
        a->stats()->set_max_hitpoints(100.0f);
        a->stats()->set_hitpoints(100.0f);
        a->setxy(sx * GRID_SIZE + GRID_SIZE / 2 - a->sizex() / 2,
                 sy * GRID_SIZE + GRID_SIZE / 2 - a->sizey() / 2);
        return a;
    }

    static void run_z(walker* a, int probes = 60)
    {
        for (int i = 0; i < probes; ++i)
            a->apply_z_motion();
    }

    static int centre_cx(const walker* w) { return (w->xpos() + w->sizex() / 2) / GRID_SIZE; }
    static int centre_cy(const walker* w) { return (w->ypos() + w->sizey() / 2) / GRID_SIZE; }

    std::size_t in_obmap(walker* w) { return world().myobmap->walker_to_pos.count(w); }
};

}  // namespace

// ---------------------------------------------------------------------------
// walker.cpp:2215 walker::resolve_fall_landing (the damage curve, the clang)
// ---------------------------------------------------------------------------

// A fall of n stories costs min(15% x (n-1), 50%) of max hp at the settle:
// 3 stories 30%, 4 stories 45%. Every damaging settle clangs once.
TEST_F(SemWalker3, a_fall_costs_fifteen_percent_of_max_hp_per_story_past_the_first)
{
    struct Case { int stories; float hp_after; };
    for (const Case c : {Case{3, 70.0f}, Case{4, 55.0f}}) {
        fresh_world();
        build_shaft(c.stories + 1, 7, 7, 1);
        walker* a = soldier_on_cell(c.stories, 7, 7);
        ASSERT_NE(nullptr, a);
        events().clear();
        run_z(a);
        EXPECT_EQ(0, a->floor()) << c.stories << " stories: the cascade reaches floor 0";
        EXPECT_FLOAT_EQ(c.hp_after, a->stats()->hitpoints())
            << c.stories << " stories: " << (c.stories - 1) * 15 << "% of 100 hp";
        ASSERT_EQ(1u, a->damage_numbers.size()) << c.stories << " stories: one settle";
        EXPECT_FLOAT_EQ(100.0f - c.hp_after, a->damage_numbers.back().value);
        EXPECT_EQ(1u, count_sound(SOUND_CLANG)) << c.stories << " stories: one clang at the settle";
    }

    // Two stories: the 15% settle sounds the clang exactly once too.
    fresh_world();
    build_shaft(3, 7, 7, 1);
    walker* two = soldier_on_cell(2, 7, 7);
    ASSERT_NE(nullptr, two);
    events().clear();
    run_z(two);
    EXPECT_FLOAT_EQ(85.0f, two->stats()->hitpoints());
    EXPECT_EQ(1u, count_sound(SOUND_CLANG)) << "2 stories: the clang";
}

// ---------------------------------------------------------------------------
// walker.cpp:2223 walker::resolve_fall_landing (the character's record)
// ---------------------------------------------------------------------------

// A character's fall counts in its scenario damage-taken tally and lowers its
// lowest-hp mark; a second fall from higher hp adds to the tally but leaves
// the lower mark. A walker with no record takes the same damage and gains
// no record.
TEST_F(SemWalker3, a_characters_fall_damage_counts_in_its_scenario_record)
{
    build_shaft(3, 7, 7, 1);
    walker* a = soldier_on_cell(2, 7, 7);
    ASSERT_NE(nullptr, a);
    a->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    a->myguy->scen_damage_taken = 0.0f;
    a->myguy->scen_min_hp = 100.0f;
    run_z(a);
    ASSERT_EQ(0, a->floor());
    EXPECT_FLOAT_EQ(15.0f, a->myguy->scen_damage_taken) << "character: the 15 hp counted";
    EXPECT_FLOAT_EQ(85.0f, a->myguy->scen_min_hp) << "character: the low mark is 85";

    // Back to full on the top floor: the same fall again.
    a->stats()->set_hitpoints(100.0f);
    a->set_floor(2);
    a->setxy(static_cast<std::int32_t>(a->xpos() + 1), static_cast<std::int32_t>(a->ypos()));
    a->setxy(static_cast<std::int32_t>(a->xpos() - 1), static_cast<std::int32_t>(a->ypos()));
    run_z(a);
    ASSERT_EQ(0, a->floor());
    EXPECT_FLOAT_EQ(30.0f, a->myguy->scen_damage_taken) << "character: the tally grows";
    EXPECT_FLOAT_EQ(85.0f, a->myguy->scen_min_hp) << "character: 85 again does not lower the mark";

    walker* b = soldier_on_cell(2, 7, 7);
    ASSERT_NE(nullptr, b);
    a->set_floor(0);
    run_z(b);
    EXPECT_EQ(0, b->floor());
    EXPECT_FLOAT_EQ(85.0f, b->stats()->hitpoints()) << "no record: the same damage";
    EXPECT_EQ(nullptr, b->myguy) << "no record: none is made";
}

// ---------------------------------------------------------------------------
// walker.cpp:2232 walker::resolve_fall_landing (a lethal fall)
// ---------------------------------------------------------------------------

// A fall that kills credits nobody: no team score, no ScoreChange, no kill on
// any record, no attacker stamp. (Its "fall lethal" TRACE is not linked in
// unit binaries; the death itself is pinned by ZAxis.lethal_when_wounded.)
TEST_F(SemWalker3, a_lethal_fall_credits_no_score_and_no_kill)
{
    build_shaft(3, 7, 7, 1);
    walker* a = soldier_on_cell(2, 7, 7);
    walker* bystander = hero_at(FAMILY_SOLDIER, 0, 600, 600);
    ASSERT_NE(nullptr, a);
    ASSERT_NE(nullptr, bystander);
    a->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    a->myguy->kills = 0;
    a->myguy->scen_kills = 0;
    bystander->myguy->kills = 0;
    bystander->myguy->scen_kills = 0;
    bystander->myguy->exp = 0;
    a->stats()->set_hitpoints(10.0f);
    const auto before = scores();
    events().clear();

    run_z(a);
    ASSERT_EQ(1, a->dead()) << "10 hp, 15 hp fall: dead";
    EXPECT_EQ(before, scores()) << "attribution: no team score";
    EXPECT_EQ(0u, count_events(og::sim::EventKind::ScoreChange)) << "attribution: no ScoreChange";
    EXPECT_EQ(0, a->myguy->kills) << "attribution: no kill on the faller";
    EXPECT_EQ(0, bystander->myguy->kills) << "attribution: no kill on anyone";
    EXPECT_EQ(0, bystander->myguy->scen_kills);
    EXPECT_EQ(0u, bystander->myguy->exp);
    EXPECT_EQ(0u, a->last_attacker_id()) << "attribution: no attacker stamp";
}

// ---------------------------------------------------------------------------
// walker.cpp:2250 walker::apply_z_motion (single floor)
// ---------------------------------------------------------------------------

// On a one-floor level nothing vertical happens: a soldier standing on air or
// on a stair keeps floor 0, its hp and its life. The same air on floor 1 of a
// two-floor world drops it (the contrast).
TEST_F(SemWalker3, a_single_floor_level_has_no_vertical_motion)
{
    ASSERT_EQ(1, world().floor_count());
    walker* a = soldier_on_cell(0, 7, 7);
    ASSERT_NE(nullptr, a);
    paint(0, 7, 7, PIX_AIR);
    run_z(a, 50);
    EXPECT_EQ(0, a->dead()) << "one floor, air: alive (no pit death)";
    EXPECT_EQ(0, a->floor());
    EXPECT_FLOAT_EQ(100.0f, a->stats()->hitpoints()) << "one floor, air: no damage";
    EXPECT_EQ(0, a->fall_stories_for_test());

    paint(0, 7, 7, PIX_ZSTAIR_UP);
    run_z(a, 50);
    EXPECT_EQ(0, a->floor()) << "one floor, stair: no climb";
    EXPECT_EQ(0, a->dead());

    fresh_world();
    build_shaft(2, 7, 7, 1);
    walker* b = soldier_on_cell(1, 7, 7);
    ASSERT_NE(nullptr, b);
    b->apply_z_motion();
    EXPECT_EQ(0, b->floor()) << "two floors: the air rule runs";
}

// ---------------------------------------------------------------------------
// walker.cpp:2252 walker::apply_z_motion (dead, ignored)
// ---------------------------------------------------------------------------

// A corpse or an ignored walker over air keeps its floor.
TEST_F(SemWalker3, a_corpse_or_an_ignored_walker_never_falls)
{
    build_shaft(2, 7, 7, 1);
    walker* corpse = soldier_on_cell(1, 7, 7);
    ASSERT_NE(nullptr, corpse);
    corpse->set_dead(1);
    run_z(corpse, 10);
    EXPECT_EQ(1, corpse->floor()) << "dead over air: floor kept";

    walker* ignored = soldier_on_cell(1, 7, 7);
    ASSERT_NE(nullptr, ignored);
    ignored->set_ignore(1);
    run_z(ignored, 10);
    EXPECT_EQ(1, ignored->floor()) << "ignored over air: floor kept";

    ignored->set_ignore(0);
    ignored->apply_z_motion();
    EXPECT_EQ(0, ignored->floor()) << "alive and not ignored: it falls";
}

// ---------------------------------------------------------------------------
// walker.cpp:2255 walker::apply_z_motion (the cooldown gate)
// ---------------------------------------------------------------------------

// After a story fallen, the next two probes change nothing even over air;
// the third falls on.
TEST_F(SemWalker3, a_running_z_cooldown_holds_the_walker_on_its_floor)
{
    build_shaft(3, 7, 7, 1);
    walker* a = soldier_on_cell(2, 7, 7);
    ASSERT_NE(nullptr, a);
    a->apply_z_motion();
    ASSERT_EQ(1, a->floor()) << "the first story";
    a->apply_z_motion();
    EXPECT_EQ(1, a->floor()) << "cooldown running (2 -> 1): no change over air";
    a->apply_z_motion();
    EXPECT_EQ(1, a->floor()) << "cooldown running (1 -> 0): no change over air";
    a->apply_z_motion();
    EXPECT_EQ(0, a->floor()) << "cooldown expired: the second story";
}

// ---------------------------------------------------------------------------
// walker.cpp:2284 walker::apply_z_motion (stairs)
// ---------------------------------------------------------------------------

// An UP stair on the top floor and a DOWN stair on floor 0 do nothing and
// start no cooldown (the very next probe elsewhere acts at once). A flyer on
// an UP stair never climbs.
TEST_F(SemWalker3, a_stair_with_no_floor_beyond_it_does_nothing_and_flyers_ignore_stairs)
{
    world().set_floor_count(2);
    unsigned char* f1 = fill_floor(1, PIX_GRASS1);
    f1[7 + 7 * world().grid.w] = PIX_ZSTAIR_UP;
    f1[8 + 7 * world().grid.w] = PIX_AIR;
    walker* top = soldier_on_cell(1, 7, 7);
    ASSERT_NE(nullptr, top);
    top->apply_z_motion();
    EXPECT_EQ(1, top->floor()) << "UP on the top floor: no change";
    top->setxy(static_cast<std::int32_t>(top->xpos() + GRID_SIZE), static_cast<std::int32_t>(top->ypos()));
    ASSERT_EQ(8, centre_cx(top));
    top->apply_z_motion();
    EXPECT_EQ(0, top->floor()) << "UP on the top floor started no cooldown: the air drops it at once";

    paint(0, 9, 9, PIX_ZSTAIR_DOWN);
    paint(0, 10, 9, PIX_ZSTAIR_UP);
    walker* bottom = soldier_on_cell(0, 9, 9);
    ASSERT_NE(nullptr, bottom);
    bottom->apply_z_motion();
    EXPECT_EQ(0, bottom->floor()) << "DOWN on floor 0: no change";
    bottom->setxy(static_cast<std::int32_t>(bottom->xpos() + GRID_SIZE), static_cast<std::int32_t>(bottom->ypos()));
    ASSERT_EQ(10, centre_cx(bottom));
    bottom->apply_z_motion();
    EXPECT_EQ(1, bottom->floor()) << "DOWN on floor 0 started no cooldown: the UP takes it at once";

    paint(0, 20, 20, PIX_ZSTAIR_UP);
    walker* flyer = soldier_on_cell(0, 20, 20);
    ASSERT_NE(nullptr, flyer);
    flyer->stats()->set_bit_flags(BIT_FLYING, 1);
    ASSERT_EQ(20, centre_cx(flyer));
    ASSERT_EQ(20, centre_cy(flyer));
    run_z(flyer, 10);
    EXPECT_EQ(0, flyer->floor()) << "flyer on an UP stair: never climbs";
}

// ---------------------------------------------------------------------------
// walker.cpp:2313 walker::apply_z_motion (a denied stair's cooldown)
// ---------------------------------------------------------------------------

// A climb denied for want of any clear arrival waits exactly 6 probes: with
// the far side cleared right after the denial, probes 1..6 keep the floor and
// the 7th climbs, aligned.
TEST_F(SemWalker3, a_denied_stair_climb_retries_after_six_ticks)
{
    world().set_floor_count(2);
    walker* a = soldier_on_cell(0, 8, 8);
    walker* b = soldier_on_cell(1, 8, 8);
    ASSERT_NE(nullptr, a);
    ASSERT_NE(nullptr, b);
    const short ax = static_cast<short>(a->xpos());
    const short ay = static_cast<short>(a->ypos());
    paint(0, 8, 8, PIX_ZSTAIR_UP);
    unsigned char* f1 = fill_floor(1, PIX_GRASS1);
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx)
            f1[(8 + dx) + (8 + dy) * world().grid.w] = PIX_H_WALL1;
    f1[8 + 8 * world().grid.w] = PIX_ZSTAIR_DOWN;
    world().smoother_for_floor(1).set_target(world().grid_for_floor(1));

    a->apply_z_motion();
    ASSERT_EQ(0, a->floor()) << "nothing clear: denied";
    b->setxy(14 * GRID_SIZE, 14 * GRID_SIZE);
    for (int t = 1; t <= 6; ++t) {
        a->apply_z_motion();
        ASSERT_EQ(0, a->floor()) << "denied: cooldown 6, probe " << t << " waits";
    }
    a->apply_z_motion();
    EXPECT_EQ(1, a->floor()) << "the 7th probe climbs";
    EXPECT_EQ(ax, a->xpos());
    EXPECT_EQ(ay, a->ypos());
}

// ---------------------------------------------------------------------------
// walker.cpp:2338 walker::apply_z_motion (the fall's landing)
// ---------------------------------------------------------------------------

// Straight down clear: the faller lands at the same x/y and waits two probes
// before falling on. A nudged landing waits two probes too. A fall refused for
// want of any landing waits six probes before it looks again.
TEST_F(SemWalker3, a_fall_lands_in_place_and_paces_two_ticks_and_a_hover_six)
{
    // Straight down, then on (air under the landing cell too).
    build_shaft(3, 7, 7, 1);
    walker* a = soldier_on_cell(2, 7, 7);
    ASSERT_NE(nullptr, a);
    const std::int32_t x0 = a->xpos();
    const std::int32_t y0 = a->ypos();
    a->apply_z_motion();
    ASSERT_EQ(1, a->floor());
    EXPECT_EQ(x0, a->xpos()) << "straight down clear: same x";
    EXPECT_EQ(y0, a->ypos()) << "straight down clear: same y";
    a->apply_z_motion();
    a->apply_z_motion();
    EXPECT_EQ(1, a->floor()) << "after a landing: cooldown 2";
    a->apply_z_motion();
    EXPECT_EQ(0, a->floor()) << "after a landing: the third probe falls on";

    // Nudged: floor 1 under the fall line is a wall, its first ring-1
    // candidate (6,6) is air, so the nudged walker falls on after its pause.
    fresh_world();
    build_shaft(3, 7, 7, 2);
    paint(1, 7, 7, PIX_H_WALL1);
    paint(1, 6, 6, PIX_AIR);
    walker* n = soldier_on_cell(2, 7, 7);
    ASSERT_NE(nullptr, n);
    ASSERT_EQ(16, n->sizex());
    n->apply_z_motion();
    ASSERT_EQ(1, n->floor()) << "nudged landing on floor 1";
    ASSERT_EQ(6 * GRID_SIZE, n->xpos()) << "nudged to (6,6)";
    ASSERT_EQ(6 * GRID_SIZE, n->ypos());
    n->apply_z_motion();
    n->apply_z_motion();
    EXPECT_EQ(1, n->floor()) << "after a nudged landing: cooldown 2";
    n->apply_z_motion();
    EXPECT_EQ(0, n->floor()) << "after a nudged landing: the third probe falls on";

    // Hover: floor 0 all wall, then opened right after the refusal.
    fresh_world();
    build_shaft(2, 7, 7, 1);
    walker* h = soldier_on_cell(1, 7, 7);
    ASSERT_NE(nullptr, h);
    const int cells = world().grid.w * world().grid.h;
    std::fill(world().grid.data.get(), world().grid.data.get() + cells,
              static_cast<unsigned char>(PIX_H_WALL1));
    h->apply_z_motion();
    ASSERT_EQ(1, h->floor()) << "nothing clear: hovering";
    std::fill(world().grid.data.get(), world().grid.data.get() + cells,
              static_cast<unsigned char>(PIX_GRASS1));
    for (int t = 1; t <= 6; ++t) {
        h->apply_z_motion();
        ASSERT_EQ(1, h->floor()) << "after a refused fall: cooldown 6, probe " << t << " waits";
    }
    h->apply_z_motion();
    EXPECT_EQ(0, h->floor()) << "the 7th probe falls";
}

// ---------------------------------------------------------------------------
// walker.cpp:2473 walker::is_friendly (a corpse)
// ---------------------------------------------------------------------------

// Two walkers of one colour and no owners are friends until either is dead;
// then each answers unfriendly of the other.
TEST_F(SemWalker3, nothing_is_friendly_with_a_corpse_either_way)
{
    walker* a = living_at(FAMILY_SOLDIER, 0, 100, 100);
    walker* b = living_at(FAMILY_SOLDIER, 0, 200, 100);
    ASSERT_TRUE(a && b);
    ASSERT_EQ(1, a->is_friendly(b)) << "both alive, one colour: friends";
    a->set_dead(1);
    EXPECT_EQ(0, a->is_friendly(b)) << "this one dead: unfriendly";
    EXPECT_EQ(0, b->is_friendly(a)) << "the target dead: unfriendly";
}

// ---------------------------------------------------------------------------
// walker.cpp:2528 entity_display_name (the fallback)
// ---------------------------------------------------------------------------

// A walker with no character and no authored name is called by the fallback
// its caller passes (the old game's eat-corpse notice says "Orc").
TEST_F(SemWalker3, a_nameless_walker_is_called_by_the_callers_fallback)
{
    walker* orc = living_at(FAMILY_ORC, 1, 100, 100);
    ASSERT_NE(nullptr, orc);
    ASSERT_EQ(nullptr, orc->myguy);
    ASSERT_TRUE(orc->stats()->name.empty());
    EXPECT_EQ("Orc", std::string(entity_display_name(orc, "Orc"))) << "neither: the fallback";
    orc->stats()->name = "Gatekeeper";
    EXPECT_EQ("Gatekeeper", std::string(entity_display_name(orc, "Orc"))) << "named NPC: its name";
}

// ---------------------------------------------------------------------------
// walker.cpp:2603 walker::set_hidden
// ---------------------------------------------------------------------------

// Hiding sets KIT_HIDDEN, drops the outline and leaves the obmap; revealing a
// walker gone dormant meanwhile keeps it out of the obmap; asking for the
// state already held changes nothing (outline, kit bits, obmap). set_hidden
// reads no setting; it runs the same under both values of New Specials.
TEST_F(SemWalker3, hiding_drops_the_outline_and_a_repeat_or_dormant_reveal_changes_nothing)
{
    for (short ns : {short{0}, short{1}}) {
        fresh_world();
        world().new_specials = ns;
        walker* s = living_at(FAMILY_SOLDIER, 0, 160, 160);
        ASSERT_NE(nullptr, s);
        ASSERT_EQ(1u, in_obmap(s));

        // Same value on a visible walker: nothing.
        s->set_outline(5);
        s->set_kit_state(KIT_FEARLESS);
        s->set_hidden(false);
        EXPECT_EQ(5, s->outline()) << "ns " << ns << ": same value (visible): outline kept";
        EXPECT_EQ(KIT_FEARLESS, s->kit_state()) << "ns " << ns << ": same value: kit bits kept";
        EXPECT_EQ(1u, in_obmap(s)) << "ns " << ns << ": same value: still in the obmap";

        // Hide.
        s->set_outline(OUTLINE_NAMED);
        s->set_hidden(true);
        EXPECT_EQ(KIT_FEARLESS | KIT_HIDDEN, s->kit_state()) << "ns " << ns << ": hide sets KIT_HIDDEN";
        EXPECT_EQ(0, s->outline()) << "ns " << ns << ": hide drops the outline";
        EXPECT_EQ(0u, in_obmap(s)) << "ns " << ns << ": hide leaves the obmap";

        // Same value on a hidden walker: nothing.
        s->set_outline(9);
        s->set_hidden(true);
        EXPECT_EQ(9, s->outline()) << "ns " << ns << ": same value (hidden): outline kept";
        EXPECT_EQ(KIT_FEARLESS | KIT_HIDDEN, s->kit_state());
        EXPECT_EQ(0u, in_obmap(s));

        // Reveal while dormant: stays out.
        s->set_dormant(true);
        s->set_hidden(false);
        EXPECT_FALSE(s->hidden()) << "ns " << ns << ": reveal clears KIT_HIDDEN";
        EXPECT_EQ(KIT_FEARLESS, s->kit_state());
        EXPECT_EQ(0u, in_obmap(s)) << "ns " << ns << ": reveal, dormant: stays out of the obmap";
    }
}

// ---------------------------------------------------------------------------
// walker.cpp:2586 walker::set_hidden (a gone partner)
// ---------------------------------------------------------------------------

// A possession link to a walker that no longer exists does not stop a hide.
TEST_F(SemWalker3, a_possession_link_to_a_gone_walker_does_not_stop_a_hide)
{
    for (short ns : {short{0}, short{1}}) {
        fresh_world();
        world().new_specials = ns;
        walker* s = living_at(FAMILY_SOLDIER, 0, 160, 160);
        ASSERT_NE(nullptr, s);
        ASSERT_EQ(nullptr, world().find_by_id(99999u));
        s->set_possess_link(99999u);
        s->set_hidden(true);
        EXPECT_TRUE(s->hidden()) << "ns " << ns << ": partner gone: the hide proceeds";
        EXPECT_EQ(0u, in_obmap(s)) << "ns " << ns << ": partner gone: out of the obmap";
    }
}

// ---------------------------------------------------------------------------
// walker.cpp:2716 walker::credit_head (no living on the chain)
// ---------------------------------------------------------------------------

// An effect with no owner, or owned by a generator, has no living on its
// chain: credit_head is null, its blow lands but stamps no attacker. Both
// values of Gameplay Fixes.
TEST_F(SemWalker3, a_blow_with_no_living_on_its_chain_is_credited_to_nobody)
{
    for (short fixes : {short{0}, short{1}}) {
        fresh_world();
        world().gameplay_fixes = fixes;
        walker* victim = living_at(FAMILY_SOLDIER, 1, 300, 300);
        walker* tent = generator_at(0, 600, 600);
        ASSERT_TRUE(victim && tent);

        walker* loose = world().add_ob(Order::FX, FAMILY_EXPLOSION);
        walker* posted = world().add_ob(Order::FX, FAMILY_EXPLOSION);
        ASSERT_TRUE(loose && posted);
        for (walker* fx : {loose, posted}) {
            fx->set_team_num(0);
            fx->set_damage(50.0f);
        }
        posted->set_owner(tent);
        EXPECT_EQ(nullptr, loose->credit_head()) << "fixes " << fixes << ": no owner: nullptr";
        EXPECT_EQ(nullptr, posted->credit_head()) << "fixes " << fixes << ": generator owner: nullptr";

        ASSERT_TRUE(loose->attack(victim));
        EXPECT_FLOAT_EQ(354.0f, victim->stats()->hitpoints()) << "fixes " << fixes << ": the blow lands (46)";
        EXPECT_EQ(0u, victim->last_attacker_id()) << "fixes " << fixes << ": no stamp";
        ASSERT_TRUE(posted->attack(victim));
        EXPECT_FLOAT_EQ(308.0f, victim->stats()->hitpoints());
        EXPECT_EQ(0u, victim->last_attacker_id()) << "fixes " << fixes << ": generator chain: no stamp";
    }
}

// ---------------------------------------------------------------------------
// walker_combat.cpp:67 exp_from_action (no target)
// ---------------------------------------------------------------------------

// An action with no target counts as against a level-0 target; with a target
// its level is used.
TEST_F(SemWalker3, experience_for_an_action_without_a_target_counts_a_level_zero_target)
{
    walker* w = hero_at(FAMILY_SOLDIER, 0, 100, 100);
    walker* orc = living_at(FAMILY_ORC, 1, 200, 100);
    ASSERT_TRUE(w && orc);
    w->stats()->set_level(4);
    orc->stats()->set_level(5);
    ASSERT_NE(compute_xp_from_attack(4, 10.0f), compute_xp_from_attack(-1, 10.0f));
    EXPECT_EQ(compute_xp_from_attack(4, 10.0f), exp_from_action(ExpAction::Attack, w, nullptr, 10))
        << "no target: level diff 4 - 0";
    EXPECT_EQ(compute_xp_from_attack(-1, 10.0f), exp_from_action(ExpAction::Attack, w, orc, 10))
        << "target: level diff 4 - 5";
    EXPECT_EQ(compute_xp_from_kill(4), exp_from_action(ExpAction::Kill, w, nullptr, 0))
        << "no target, kill: level diff 4";
    EXPECT_EQ(30, exp_from_action(ExpAction::TurnUndead, w, nullptr, 10))
        << "turn undead (the scripted caller): 3 per undead";
}

// ---------------------------------------------------------------------------
// walker_combat.cpp:90 award_score (out-of-range team)
// ---------------------------------------------------------------------------

// A scoring blow from a walker whose team is no score team adds nothing and
// emits no ScoreChange; a team-0 hero's blow emits exactly one (the control).
TEST_F(SemWalker3, a_blow_from_an_out_of_range_team_scores_nothing_and_emits_no_event)
{
    walker* odd = hero_at(FAMILY_SOLDIER, 250, 100, 100);
    walker* victim = living_at(FAMILY_SOLDIER, 1, 120, 100);
    ASSERT_TRUE(odd && victim);
    odd->set_damage(10.0f);
    const auto before = scores();
    events().clear();
    ASSERT_TRUE(odd->attack(victim));
    EXPECT_FLOAT_EQ(392.0f, victim->stats()->hitpoints()) << "the blow lands";
    EXPECT_EQ(before, scores()) << "team 250: no score";
    EXPECT_EQ(0u, count_events(og::sim::EventKind::ScoreChange)) << "team 250: no event";

    walker* hero = hero_at(FAMILY_SOLDIER, 0, 140, 100);
    ASSERT_NE(nullptr, hero);
    hero->set_damage(10.0f);
    ASSERT_TRUE(hero->attack(victim));
    EXPECT_EQ(before[0] + 8u + 4u, world().m_score[0]) << "valid team: dmg + level";
    EXPECT_EQ(1u, count_events(og::sim::EventKind::ScoreChange)) << "valid team: one event";
}

// ---------------------------------------------------------------------------
// walker_combat.cpp:118 walker::do_hit_effects (no attacker)
// ---------------------------------------------------------------------------

// A hit with no credited attacker shows one red number to the victim alone.
TEST_F(SemWalker3, a_hit_with_no_attacker_shows_its_number_to_the_victim_only)
{
    walker* s = living_at(FAMILY_SOLDIER, 0, 100, 100);
    walker* o = living_at(FAMILY_ORC, 1, 120, 100);
    ASSERT_TRUE(s && o);
    s->damage_numbers.clear();
    o->damage_numbers.clear();
    s->do_hit_effects(nullptr, o, 5);
    ASSERT_EQ(1u, o->damage_numbers.size()) << "no attacker: the victim's number";
    EXPECT_EQ(RED, o->damage_numbers.back().color);
    EXPECT_FLOAT_EQ(5.0f, o->damage_numbers.back().value);
    EXPECT_TRUE(s->damage_numbers.empty()) << "no attacker: nobody else gets one";
}

// ---------------------------------------------------------------------------
// walker_combat.cpp:175 walker::do_hit_effects (flash and recoil)
// ---------------------------------------------------------------------------

// A hit that took hp flashes the victim; a living victim also recoils at 1.0
// along the hitter->victim angle; a non-living only flashes; a 0 hit neither.
TEST_F(SemWalker3, a_damaging_hit_flashes_and_a_living_victim_recoils_away)
{
    walker* s = living_at(FAMILY_SOLDIER, 0, 100, 100);
    walker* o = living_at(FAMILY_ORC, 1, 130, 140);
    walker* tent = generator_at(1, 300, 300);
    walker* fresh = living_at(FAMILY_ORC, 1, 500, 500);
    ASSERT_TRUE(s && o && tent && fresh);

    s->do_hit_effects(s, o, 5);
    EXPECT_TRUE(o->hurt_flash()) << "living, damage: flash";
    EXPECT_FLOAT_EQ(1.0f, o->hit_recoil()) << "living, damage: recoil 1.0";
    const float dy = (140.0f + o->sizey() * 0.5f) - (100.0f + s->sizey() * 0.5f);
    const float dx = (130.0f + o->sizex() * 0.5f) - (100.0f + s->sizex() * 0.5f);
    EXPECT_FLOAT_EQ(std::atan2(dy, dx), o->hit_recoil_angle()) << "living: away from the hitter";
    EXPECT_FLOAT_EQ(std::atan2(40.0f, 30.0f), o->hit_recoil_angle());

    s->do_hit_effects(s, tent, 5);
    EXPECT_TRUE(tent->hurt_flash()) << "non-living, damage: flash";
    EXPECT_FLOAT_EQ(0.0f, tent->hit_recoil()) << "non-living: no recoil";

    s->do_hit_effects(s, fresh, 0);
    EXPECT_FALSE(fresh->hurt_flash()) << "damage 0: no flash";
    EXPECT_FLOAT_EQ(0.0f, fresh->hit_recoil()) << "damage 0: no recoil";
}

// ---------------------------------------------------------------------------
// walker_combat.cpp:139 walker::do_hit_effects (the hit flash)
// ---------------------------------------------------------------------------

// Every hit spawns one FAMILY_HIT on the victim's floor, on the hitter's
// team, animation 1 + rng(3) from the cosmetic stream: centred on the victim
// in melee, halfway between the projectile and the victim's centre for a
// projectile. An effect spawns one only when its family says so (knife_back
// yes, explosion no), as in the old game (only FAMILY_KNIFE_BACK).
TEST_F(SemWalker3, every_hit_spawns_a_hit_flash_except_from_a_quiet_effect)
{
    world().set_floor_count(2);
    fill_floor(1, PIX_GRASS1);
    walker* s = living_at(FAMILY_SOLDIER, 0, 100, 100);
    walker* o = living_at(FAMILY_ORC, 1, 120, 100);
    ASSERT_TRUE(s && o);
    o->set_floor(1);

    // Melee.
    std::size_t hits = count_family(world().oblist, Order::FX, FAMILY_HIT);
    og::sim::SimRandom copy = world().rng_;
    const char want_ani = static_cast<char>(1 + copy.next(3));
    s->do_hit_effects(s, o, 5);
    auto flashes = of_family(world().oblist, Order::FX, FAMILY_HIT);
    ASSERT_EQ(hits + 1, flashes.size()) << "non-FX hitter: one hit flash";
    walker* f = flashes.back();
    EXPECT_EQ(o->xpos() + o->sizex() / 2, f->xpos() + f->sizex() / 2) << "melee: centred on the victim (x)";
    EXPECT_EQ(o->ypos() + o->sizey() / 2, f->ypos() + f->sizey() / 2) << "melee: centred on the victim (y)";
    EXPECT_EQ(1, f->floor()) << "the victim's floor";
    EXPECT_EQ(0, f->team_num()) << "the hitter's team";
    EXPECT_EQ(o, f->owner());
    EXPECT_EQ(want_ani, f->ani_type()) << "ani 1 + rng(3)";

    // Projectile: the knife sits at (40, 100), the thrower is credited.
    walker* knife = knife_of(s, 10.0f);
    ASSERT_NE(nullptr, knife);
    knife->setxy(40, 100);
    hits = flashes.size();
    knife->do_hit_effects(s, o, 5);
    flashes = of_family(world().oblist, Order::FX, FAMILY_HIT);
    ASSERT_EQ(hits + 1, flashes.size()) << "projectile: one hit flash";
    f = flashes.back();
    const float start_x = static_cast<float>(40 + knife->sizex() / 2 - f->sizex() / 2);
    const float start_y = static_cast<float>(100 + knife->sizey() / 2 - f->sizey() / 2);
    EXPECT_FLOAT_EQ((static_cast<float>(120 + o->sizex() / 2) + start_x) / 2.0f, f->worldx())
        << "projectile: halfway (x)";
    EXPECT_FLOAT_EQ((static_cast<float>(100 + o->sizey() / 2) + start_y) / 2.0f, f->worldy())
        << "projectile: halfway (y)";

    // Effects.
    walker* quiet = world().add_ob(Order::FX, FAMILY_EXPLOSION);
    walker* loud = world().add_ob(Order::FX, FAMILY_KNIFE_BACK);
    ASSERT_TRUE(quiet && loud);
    hits = count_family(world().oblist, Order::FX, FAMILY_HIT);
    quiet->do_hit_effects(quiet, o, 5);
    EXPECT_EQ(hits, count_family(world().oblist, Order::FX, FAMILY_HIT))
        << "FX with creates_hit_effect=false: no hit flash";
    loud->do_hit_effects(loud, o, 5);
    EXPECT_EQ(hits + 1, count_family(world().oblist, Order::FX, FAMILY_HIT))
        << "FX with creates_hit_effect: one hit flash";
}

// ---------------------------------------------------------------------------
// walker_combat.cpp:204 walker::do_combat_damage (the hitter's record)
// ---------------------------------------------------------------------------

// A hero's record counts damage dealt to livings only (total and scenario);
// a hit on a generator is not recorded; a hitter with no record credits
// nobody (a hero victim's own dealt-damage record is untouched).
TEST_F(SemWalker3, a_heros_damage_record_counts_living_victims_only)
{
    walker* hero = hero_at(FAMILY_SOLDIER, 0, 100, 100);
    walker* orc = living_at(FAMILY_ORC, 1, 120, 100);
    walker* tent = generator_at(1, 300, 300);
    walker* npc = living_at(FAMILY_SOLDIER, 0, 400, 400);
    walker* hero_victim = hero_at(FAMILY_ORC, 1, 420, 400);
    ASSERT_TRUE(hero && orc && tent && npc && hero_victim);
    hero->myguy->total_damage = 0;
    hero->myguy->scen_damage = 0.0f;

    hero->do_combat_damage(hero, orc, 7);
    EXPECT_EQ(7, hero->myguy->total_damage) << "hero hits living: total_damage";
    EXPECT_FLOAT_EQ(7.0f, hero->myguy->scen_damage) << "hero hits living: scen_damage";

    hero->do_combat_damage(hero, tent, 5);
    EXPECT_FLOAT_EQ(95.0f, tent->stats()->hitpoints()) << "the generator takes the damage";
    EXPECT_EQ(7, hero->myguy->total_damage) << "hero hits non-living: not recorded";
    EXPECT_FLOAT_EQ(7.0f, hero->myguy->scen_damage);

    hero_victim->myguy->total_damage = 0;
    hero_victim->myguy->scen_damage = 0.0f;
    hero_victim->myguy->scen_damage_taken = 0.0f;
    npc->do_combat_damage(npc, hero_victim, 6);
    EXPECT_FLOAT_EQ(394.0f, hero_victim->stats()->hitpoints());
    EXPECT_EQ(0, hero_victim->myguy->total_damage) << "no-record hitter: nothing recorded";
    EXPECT_FLOAT_EQ(0.0f, hero_victim->myguy->scen_damage);
    EXPECT_FLOAT_EQ(6.0f, hero_victim->myguy->scen_damage_taken) << "only the victim's taken grows";
}

// ---------------------------------------------------------------------------
// walker_combat.cpp:220 walker::do_combat_damage (regen delay, overkill)
// ---------------------------------------------------------------------------

// A 0 hit leaves the regen delay alone; overkill counts only the hp there
// were to lose.
TEST_F(SemWalker3, a_zero_hit_leaves_regen_alone_and_overkill_counts_only_the_hp_lost)
{
    walker* a = living_at(FAMILY_SOLDIER, 0, 100, 100);
    walker* v = hero_at(FAMILY_ORC, 1, 120, 100);
    ASSERT_TRUE(a && v);
    v->set_regen_delay(7);
    a->do_combat_damage(a, v, 0);
    EXPECT_EQ(7, v->regen_delay()) << "damage 0: regen_delay unchanged";

    v->stats()->set_hitpoints(3.0f);
    v->myguy->scen_damage_taken = 0.0f;
    a->do_combat_damage(a, v, 10);
    EXPECT_FLOAT_EQ(-7.0f, v->stats()->hitpoints());
    EXPECT_FLOAT_EQ(3.0f, v->myguy->scen_damage_taken) << "overkill: 3, not 10";
    EXPECT_EQ(50, v->regen_delay());
}

// ---------------------------------------------------------------------------
// walker_combat.cpp:226 walker::do_combat_damage (the victim's record)
// ---------------------------------------------------------------------------

// A hero victim's lowest-hp mark follows its hp down and never back up, and
// its damage-taken tally is trimmed by overkill; a victim with no record
// takes the hit with nothing recorded on it.
TEST_F(SemWalker3, a_hero_victims_record_keeps_its_lowest_hp)
{
    walker* a = hero_at(FAMILY_SOLDIER, 0, 100, 100);
    walker* v = hero_at(FAMILY_ORC, 1, 120, 100);
    walker* plain = living_at(FAMILY_ORC, 1, 300, 100);
    ASSERT_TRUE(a && v && plain);
    v->stats()->set_max_hitpoints(100.0f);
    v->stats()->set_hitpoints(100.0f);
    v->myguy->scen_min_hp = 100.0f;
    v->myguy->scen_damage_taken = 0.0f;
    a->do_combat_damage(a, v, 30);
    EXPECT_FLOAT_EQ(70.0f, v->myguy->scen_min_hp) << "hero: the mark follows hp down";
    v->stats()->set_hitpoints(100.0f);
    a->do_combat_damage(a, v, 10);
    EXPECT_FLOAT_EQ(70.0f, v->myguy->scen_min_hp) << "hero: 90 does not raise the mark";
    v->stats()->set_hitpoints(3.0f);
    a->do_combat_damage(a, v, 10);
    EXPECT_FLOAT_EQ(43.0f, v->myguy->scen_damage_taken) << "hero: 30 + 10 + 3 (overkill trimmed)";
    EXPECT_FLOAT_EQ(-7.0f, v->myguy->scen_min_hp);

    a->myguy->scen_damage = 0.0f;
    a->do_combat_damage(a, plain, 9);
    EXPECT_FLOAT_EQ(391.0f, plain->stats()->hitpoints()) << "not a hero: the hit lands";
    EXPECT_EQ(nullptr, plain->myguy) << "not a hero: no record";
    EXPECT_FLOAT_EQ(9.0f, a->myguy->scen_damage) << "the hitter's record still counts it";
}

// ---------------------------------------------------------------------------
// walker_combat.cpp:261 walker::attack (the credit climb)
// ---------------------------------------------------------------------------

namespace {
constexpr unsigned char kRed = 0;
constexpr unsigned char kGreen = 1;
}  // namespace

// With Gameplay Fixes off a charmed troop's blow is credited up through the
// charm to its original owner: a green tent's troop charmed red, my_team
// green, strikes a red soldier and red is paid dmg + level (getscore came
// from the green head). With the fixes on and the same my_team the troop
// heads its own chain (red, no record, not my_team): its blow on green pays
// nothing.
TEST_F(SemWalker3, classic_credit_for_a_charmed_troops_blow_climbs_to_its_original_owner)
{
    for (short fixes : {short{0}, short{1}}) {
        fresh_world();
        world().gameplay_fixes = fixes;
        world().my_team = kGreen;
        walker* tent = generator_at(kGreen, 600, 600);
        walker* troop = living_at(FAMILY_SOLDIER, kRed, 100, 100);
        walker* red = living_at(FAMILY_SOLDIER, kRed, 120, 100);
        walker* green = living_at(FAMILY_SOLDIER, kGreen, 140, 100);
        ASSERT_TRUE(tent && troop && red && green);
        troop->set_owner(tent);
        troop->set_real_team_num(kGreen);
        troop->set_charm_left(100);
        troop->set_damage(10.0f);
        red->stats()->set_level(2);
        green->stats()->set_level(2);
        const auto before = scores();
        if (fixes == 0) {
            ASSERT_TRUE(troop->attack(red)) << "classic: the troop fights for green";
            EXPECT_FLOAT_EQ(392.0f, red->stats()->hitpoints());
            EXPECT_EQ(before[kRed] + 8u + 2u, world().m_score[kRed])
                << "fixes off: head = the green tent (my_team), so red is paid";
        } else {
            ASSERT_FALSE(troop->attack(red)) << "fixed: the troop is red";
            ASSERT_TRUE(troop->attack(green));
            EXPECT_FLOAT_EQ(392.0f, green->stats()->hitpoints());
            EXPECT_EQ(before, scores())
                << "fixes on: head = the red troop, not my_team, no record: no score";
        }
    }
}

// The climb passes a dead owner (is_friendly's stops at it): a knife whose
// hero thrower has died still pays the thrower's record its attack
// experience and the team its score. The old game climbs the same way.
TEST_F(SemWalker3, classic_credit_for_a_blow_climbs_past_a_dead_thrower)
{
    walker* thrower = hero_at(FAMILY_SOLDIER, 0, 100, 100);
    walker* orc = living_at(FAMILY_ORC, 1, 300, 100);
    ASSERT_TRUE(thrower && orc);
    thrower->stats()->set_level(4);
    orc->stats()->set_level(2);
    walker* knife = knife_of(thrower, 10.0f);
    ASSERT_NE(nullptr, knife);
    thrower->myguy->exp = 0;
    thrower->set_dead(1);
    const auto before = scores();
    ASSERT_TRUE(knife->attack(orc));
    EXPECT_FLOAT_EQ(392.0f, orc->stats()->hitpoints());
    EXPECT_EQ(static_cast<std::uint32_t>(compute_xp_from_attack(2, 8.0f)), thrower->myguy->exp)
        << "the dead thrower's record is paid";
    EXPECT_EQ(before[0] + 8u + 2u, world().m_score[0]) << "the team is paid";
}

// ---------------------------------------------------------------------------
// walker_combat.cpp:268 walker::attack (getscore)
// ---------------------------------------------------------------------------

// A blow whose chain head is neither a hero nor on the player's team scores
// nothing; the same blow from a head on my_team scores dmg + level.
TEST_F(SemWalker3, an_npc_fight_off_the_players_team_scores_nothing)
{
    walker* npc2 = living_at(FAMILY_SOLDIER, 2, 100, 100);
    walker* npc0 = living_at(FAMILY_SOLDIER, 0, 100, 300);
    walker* victim = living_at(FAMILY_SOLDIER, 1, 300, 200);
    ASSERT_TRUE(npc2 && npc0 && victim);
    walker* k2 = knife_of(npc2, 10.0f);
    walker* k0 = knife_of(npc0, 10.0f);
    ASSERT_TRUE(k2 && k0);
    const auto before = scores();
    events().clear();
    ASSERT_TRUE(k2->attack(victim));
    EXPECT_FLOAT_EQ(392.0f, victim->stats()->hitpoints()) << "the blow lands";
    EXPECT_EQ(before, scores()) << "head team 2, no record: no score";
    EXPECT_EQ(0u, count_events(og::sim::EventKind::ScoreChange));

    ASSERT_TRUE(k0->attack(victim));
    EXPECT_EQ(before[0] + 8u + 4u, world().m_score[0]) << "head on my_team: dmg + level";
    EXPECT_EQ(before[2], world().m_score[2]);
}

// ---------------------------------------------------------------------------
// walker_combat.cpp:277 walker::attack (hits on the record)
// ---------------------------------------------------------------------------

// A hit on a living by a chain with no hero credits no hit anywhere (the
// victim's own record included); a hero's thrower is credited (control).
TEST_F(SemWalker3, a_hit_by_a_walker_with_no_record_credits_no_hit)
{
    walker* npc = living_at(FAMILY_SOLDIER, 0, 100, 100);
    walker* hero = hero_at(FAMILY_SOLDIER, 0, 100, 300);
    walker* orc = hero_at(FAMILY_ORC, 1, 300, 200);
    ASSERT_TRUE(npc && hero && orc);
    orc->myguy->total_hits = 0;
    orc->myguy->scen_hits = 0;
    hero->myguy->total_hits = 0;
    hero->myguy->scen_hits = 0;
    walker* k = knife_of(npc, 10.0f);
    ASSERT_NE(nullptr, k);
    ASSERT_TRUE(k->attack(orc));
    EXPECT_FLOAT_EQ(392.0f, orc->stats()->hitpoints());
    EXPECT_EQ(nullptr, npc->myguy) << "no hero: no record made";
    EXPECT_EQ(0, orc->myguy->total_hits) << "no hero: the victim's record is not credited";
    EXPECT_EQ(0, orc->myguy->scen_hits);
    EXPECT_EQ(0, hero->myguy->total_hits) << "no hero: a bystander's record is not credited";

    // A hero's summon (no record) throws: the attacker is the summon, so the
    // hero at the head of the chain is not credited with the hit either.
    walker* summon = living_at(FAMILY_SOLDIER, 0, 100, 500);
    ASSERT_NE(nullptr, summon);
    summon->set_owner(hero);
    walker* sk = knife_of(summon, 10.0f);
    ASSERT_NE(nullptr, sk);
    ASSERT_TRUE(sk->attack(orc));
    EXPECT_FLOAT_EQ(384.0f, orc->stats()->hitpoints());
    EXPECT_EQ(0, hero->myguy->total_hits) << "attacker with no record: the head's record is not credited";
    EXPECT_EQ(0, hero->myguy->scen_hits);

    walker* hk = knife_of(hero, 10.0f);
    ASSERT_NE(nullptr, hk);
    ASSERT_TRUE(hk->attack(orc));
    EXPECT_EQ(1, hero->myguy->total_hits) << "hero thrower: the hit counted";
    EXPECT_EQ(1, hero->myguy->scen_hits);
}

// ---------------------------------------------------------------------------
// walker_combat.cpp:295 walker::attack (shots refunded on non-livings)
// ---------------------------------------------------------------------------

// A hit on a generator by a chain with no hero refunds no shot to anyone; a
// hero's knife on the same generator refunds its shot (control).
TEST_F(SemWalker3, a_non_living_hit_by_a_walker_with_no_record_refunds_no_shot)
{
    walker* npc = living_at(FAMILY_SOLDIER, 0, 100, 100);
    walker* hero = hero_at(FAMILY_SOLDIER, 0, 100, 300);
    walker* tent = generator_at(1, 300, 200);
    ASSERT_TRUE(npc && hero && tent);
    hero->myguy->total_shots = 5;
    hero->myguy->scen_shots = 5;
    walker* k = knife_of(npc, 10.0f);
    ASSERT_NE(nullptr, k);
    ASSERT_TRUE(k->attack(tent));
    EXPECT_FLOAT_EQ(92.0f, tent->stats()->hitpoints()) << "the generator is hit";
    EXPECT_EQ(nullptr, npc->myguy);
    EXPECT_EQ(5, hero->myguy->total_shots) << "no hero: nothing refunded";
    EXPECT_EQ(5, hero->myguy->scen_shots);

    walker* summon = living_at(FAMILY_SOLDIER, 0, 100, 500);
    ASSERT_NE(nullptr, summon);
    summon->set_owner(hero);
    walker* sk = knife_of(summon, 10.0f);
    ASSERT_NE(nullptr, sk);
    ASSERT_TRUE(sk->attack(tent));
    EXPECT_FLOAT_EQ(84.0f, tent->stats()->hitpoints());
    EXPECT_EQ(5, hero->myguy->total_shots) << "attacker with no record: the head's record is not refunded";
    EXPECT_EQ(5, hero->myguy->scen_shots);

    walker* hk = knife_of(hero, 10.0f);
    ASSERT_NE(nullptr, hk);
    ASSERT_TRUE(hk->attack(tent));
    EXPECT_EQ(4, hero->myguy->total_shots) << "hero thrower: the shot refunded";
    EXPECT_EQ(4, hero->myguy->scen_shots);
}

// ---------------------------------------------------------------------------
// walker_combat.cpp:323 walker::attack (the stamp's tick)
// ---------------------------------------------------------------------------

// A landed blow stamps the world tick; a later blow that lands 0 (armour
// absorbs it) leaves the earlier attacker and tick.
TEST_F(SemWalker3, a_zero_damage_blow_keeps_the_earlier_attacker_stamp)
{
    walker* a = living_at(FAMILY_SOLDIER, 0, 100, 100);
    walker* b = living_at(FAMILY_SOLDIER, 0, 100, 140);
    walker* v = living_at(FAMILY_ORC, 1, 120, 100);
    ASSERT_TRUE(a && b && v);
    a->set_damage(10.0f);
    b->set_damage(10.0f);
    world().tick_count_ = 5;
    ASSERT_TRUE(a->attack(v));
    ASSERT_FLOAT_EQ(392.0f, v->stats()->hitpoints());
    EXPECT_EQ(a->entity_id(), v->last_attacker_id());
    EXPECT_EQ(5u, v->last_attacker_tick()) << "damage > 0: tick stamped";

    v->stats()->set_armor(1000.0f);
    world().tick_count_ = 9;
    ASSERT_TRUE(b->attack(v));
    ASSERT_FLOAT_EQ(392.0f, v->stats()->hitpoints()) << "armour absorbs the blow: 0";
    EXPECT_EQ(a->entity_id(), v->last_attacker_id()) << "damage 0: the earlier attacker kept";
    EXPECT_EQ(5u, v->last_attacker_tick()) << "damage 0: the earlier tick kept";
}

// ---------------------------------------------------------------------------
// walker_combat.cpp:332 walker::attack (retaliation)
// ---------------------------------------------------------------------------

// A knife's living victim turns on the thrower, and the thrower on it; a
// striker with no record gets retaliation only: no score although its team
// is my_team.
TEST_F(SemWalker3, the_victim_turns_on_whoever_is_behind_the_blow)
{
    walker* a = living_at(FAMILY_SOLDIER, 0, 100, 100);
    walker* b = living_at(FAMILY_SOLDIER, 1, 300, 100);
    ASSERT_TRUE(a && b);
    ASSERT_EQ(nullptr, b->foe());
    walker* k = knife_of(a, 10.0f);
    ASSERT_NE(nullptr, k);
    ASSERT_TRUE(k->attack(b));
    EXPECT_EQ(a, b->foe()) << "weapon: the victim targets the owner";
    EXPECT_EQ(b, a->foe()) << "weapon: the owner targets the victim";

    walker* s = living_at(FAMILY_SOLDIER, 0, 100, 300);
    walker* o = living_at(FAMILY_SOLDIER, 1, 120, 300);
    ASSERT_TRUE(s && o);
    s->set_damage(10.0f);
    const auto before = scores();
    ASSERT_TRUE(s->attack(o));
    EXPECT_FLOAT_EQ(392.0f, o->stats()->hitpoints());
    EXPECT_EQ(s, o->foe()) << "melee, NPC striker: retaliation";
    EXPECT_EQ(before, scores()) << "melee, NPC striker: no score";
    EXPECT_EQ(nullptr, s->myguy);
}

// ---------------------------------------------------------------------------
// walker_combat.cpp:355 walker::attack (a projectile's wear)
// ---------------------------------------------------------------------------

// A projectile loses hp equal to the damage it dealt and 1 damage per hit;
// an immortal one at <= 0 hp runs its death but stays alive.
TEST_F(SemWalker3, a_projectile_is_worn_by_what_it_hits)
{
    walker* thrower = living_at(FAMILY_SOLDIER, 0, 100, 100);
    walker* orc = living_at(FAMILY_ORC, 1, 300, 100);
    ASSERT_TRUE(thrower && orc);
    walker* k = knife_of(thrower, 10.0f);
    ASSERT_NE(nullptr, k);
    k->stats()->set_hitpoints(100.0f);
    ASSERT_TRUE(k->attack(orc));
    EXPECT_FLOAT_EQ(392.0f, orc->stats()->hitpoints());
    EXPECT_FLOAT_EQ(92.0f, k->stats()->hitpoints()) << "hp > dmg: hp -= 8";
    EXPECT_FLOAT_EQ(9.0f, k->damage()) << "hp > dmg: damage -1";
    EXPECT_EQ(0, k->dead());
    EXPECT_EQ(0, k->death_called());

    walker* imm = knife_of(thrower, 10.0f);
    ASSERT_NE(nullptr, imm);
    imm->stats()->set_hitpoints(5.0f);
    imm->stats()->set_bit_flags(BIT_IMMORTAL, 1);
    ASSERT_TRUE(imm->attack(orc));
    EXPECT_FLOAT_EQ(-3.0f, imm->stats()->hitpoints());
    EXPECT_EQ(0, imm->dead()) << "immortal at <= 0 hp: not dead";
    EXPECT_EQ(1, imm->death_called()) << "immortal at <= 0 hp: death() ran";
}

// ---------------------------------------------------------------------------
// walker_combat.cpp:368 walker::attack (owned hitters)
// ---------------------------------------------------------------------------

// A summoned living's blow pays its hero head the attack experience and the
// team dmg + level; a knife's blow on an enemy weapon pays no score and the
// owner's experience once.
TEST_F(SemWalker3, a_summons_blow_pays_its_heros_experience_and_a_weapon_victim_scores_nothing)
{
    walker* mage = hero_at(FAMILY_MAGE, 0, 100, 100);
    walker* elem = living_at(FAMILY_FIREELEMENTAL, 0, 300, 100);
    walker* orc = living_at(FAMILY_ORC, 1, 320, 100);
    ASSERT_TRUE(mage && elem && orc);
    elem->set_owner(mage);
    elem->stats()->set_level(3);
    orc->stats()->set_level(2);
    elem->set_damage(10.0f);
    mage->myguy->exp = 0;
    auto before = scores();
    ASSERT_TRUE(elem->attack(orc));
    EXPECT_FLOAT_EQ(392.0f, orc->stats()->hitpoints());
    EXPECT_EQ(static_cast<std::uint32_t>(compute_xp_from_attack(1, 8.0f)), mage->myguy->exp)
        << "summon: the head's record gets the attack xp";
    EXPECT_EQ(before[0] + 8u + 2u, world().m_score[0]) << "summon: dmg + level";

    walker* hero = hero_at(FAMILY_SOLDIER, 0, 100, 400);
    walker* archer = living_at(FAMILY_ARCHER, 1, 600, 600);
    ASSERT_TRUE(hero && archer);
    hero->stats()->set_level(4);
    walker* k = knife_of(hero, 10.0f);
    walker* arrow = world().add_weap_ob(Order::Weapon, FAMILY_ARROW);
    ASSERT_TRUE(k && arrow);
    arrow->set_team_num(1);
    arrow->set_owner(archer);
    arrow->stats()->set_level(1);
    arrow->stats()->set_armor(0.0f);
    arrow->stats()->set_hitpoints(1000.0f);
    hero->myguy->exp = 0;
    before = scores();
    ASSERT_TRUE(k->attack(arrow));
    EXPECT_FLOAT_EQ(992.0f, arrow->stats()->hitpoints()) << "the arrow is hit";
    EXPECT_EQ(before, scores()) << "weapon victim: no score";
    EXPECT_EQ(static_cast<std::uint32_t>(compute_xp_from_attack(4 - 1, 8.0f)), hero->myguy->exp)
        << "weapon victim: the owner arm's xp once";
}

// ---------------------------------------------------------------------------
// walker_combat.cpp:403 walker::attack (a non-living's death)
// ---------------------------------------------------------------------------

// A destroyed generator just dies: no kill on the hero's record, no blood, no
// notice, no death cry.
TEST_F(SemWalker3, a_destroyed_generator_dies_without_credit_blood_or_cry)
{
    walker* hero = hero_at(FAMILY_SOLDIER, 0, 100, 100);
    walker* tent = generator_at(1, 120, 100);
    ASSERT_TRUE(hero && tent);
    tent->stats()->set_hitpoints(1.0f);
    tent->stats()->name = "BARRACKS";  // a named living would be announced
    hero->set_damage(10.0f);
    hero->myguy->kills = 0;
    hero->myguy->scen_kills = 0;
    hero->myguy->level_kills = 0;
    const std::size_t blood = count_family(world().weaplist, Order::Weapon, FAMILY_BLOOD);
    events().clear();
    ASSERT_TRUE(hero->attack(tent));
    EXPECT_EQ(1, tent->dead()) << "non-living: dead";
    EXPECT_EQ(1, tent->death_called()) << "non-living: death() ran";
    EXPECT_EQ(0, hero->myguy->kills) << "non-living: no kill";
    EXPECT_EQ(0, hero->myguy->scen_kills);
    EXPECT_EQ(0, hero->myguy->level_kills);
    EXPECT_EQ(blood, count_family(world().weaplist, Order::Weapon, FAMILY_BLOOD)) << "non-living: no blood";
    EXPECT_EQ(0u, count_events(og::sim::EventKind::Notification)) << "non-living: no notice";
    EXPECT_EQ(0u, count_sound(SOUND_DIE1) + count_sound(SOUND_DIE2)) << "non-living: no cry";
}

// ---------------------------------------------------------------------------
// walker_combat.cpp:409 walker::attack (an NPC's kill)
// ---------------------------------------------------------------------------

// A kill by a chain head that is neither a hero nor on my_team credits no
// score and emits no ScoreChange. (The hero arm's experience:
// a_kill_adds_the_kill_bonus_alone_to_its_heros_experience, below.)
TEST_F(SemWalker3, an_npc_kill_off_the_players_team_credits_no_score)
{
    walker* npc = living_at(FAMILY_SOLDIER, 2, 100, 100);
    walker* victim = living_at(FAMILY_SOLDIER, 1, 120, 100);
    ASSERT_TRUE(npc && victim);
    victim->stats()->set_hitpoints(1.0f);
    npc->set_damage(500.0f);
    const auto before = scores();
    events().clear();
    ASSERT_TRUE(npc->attack(victim));
    EXPECT_EQ(1, victim->dead()) << "the kill";
    EXPECT_EQ(before, scores()) << "NPC head: no score";
    EXPECT_EQ(0u, count_events(og::sim::EventKind::ScoreChange)) << "NPC head: no event";
}

// ---------------------------------------------------------------------------
// walker_combat.cpp walker::attack (a hero's kill, #440)
// ---------------------------------------------------------------------------

// A killing blow pays the hero at the head of the hitter's chain what the same
// blow pays when it does not kill, plus one kill bonus (the experience of 20
// damage at the same level gap), and counts one kill, one scenario kill and
// the victim's level: for a melee blow, a thrown knife and a summon's blow, on
// both settings. The old game pays the blow's experience a second time at the
// kill (`newexp + exp_from_action(EXP_KILL, ...)` in its walker.cpp); this
// game has not since #78, a deliberate difference (#440).
// Break: the kill arm's `exp_from_action(ExpAction::Kill, this, target, 0)` ->
// `attack_exp + exp_from_action(ExpAction::Kill, this, target, 0)` (every
// kill check red: the kill pays the blow's 72 again).
TEST_F(SemWalker3, a_kill_adds_the_kill_bonus_alone_to_its_heros_experience)
{
    // Every hitter and every victim is level 4 (a level gap of 0); a blow of
    // damage 10 lands 8 on armour 0 (FixedRandom{0}).
    const auto hit_xp = static_cast<std::uint32_t>(compute_xp_from_attack(0, 8.0f));
    const auto kill_xp = static_cast<std::uint32_t>(compute_xp_from_kill(0));
    ASSERT_GT(hit_xp, 0u) << "a blow pays experience, so a second payment shows";
    ASSERT_GT(kill_xp, 0u);

    enum class Hitter { Melee, Knife, Summon };
    for (short fixes : {short{0}, short{1}}) {
        for (Hitter how : {Hitter::Melee, Hitter::Knife, Hitter::Summon}) {
            SCOPED_TRACE(testing::Message() << "fixes " << fixes << ", hitter "
                                            << static_cast<int>(how));
            fresh_world();
            world().gameplay_fixes = fixes;
            walker* hero = hero_at(FAMILY_SOLDIER, 0, 100, 100);
            walker* struck = living_at(FAMILY_SOLDIER, 1, 300, 300);
            walker* slain = living_at(FAMILY_SOLDIER, 1, 340, 300);
            ASSERT_TRUE(hero && struck && slain);
            walker* hitter = hero;
            if (how == Hitter::Knife) {
                hitter = knife_of(hero, 10.0f);  // knife level 4
            } else if (how == Hitter::Summon) {
                hitter = living_at(FAMILY_SOLDIER, 0, 320, 340);
                if (hitter != nullptr)
                    hitter->set_owner(hero);
            }
            ASSERT_NE(nullptr, hitter);
            slain->stats()->set_hitpoints(8.0f);
            hero->myguy->exp = 0;
            hero->myguy->kills = 0;
            hero->myguy->scen_kills = 0;
            hero->myguy->level_kills = 0;

            hitter->set_damage(10.0f);
            ASSERT_TRUE(hitter->attack(struck));
            EXPECT_FLOAT_EQ(392.0f, struck->stats()->hitpoints()) << "the blow lands 8";
            ASSERT_EQ(0, struck->dead());
            const std::uint32_t paid_for_a_blow = hero->myguy->exp;
            EXPECT_EQ(hit_xp, paid_for_a_blow) << "a blow pays its experience once";
            EXPECT_EQ(0, hero->myguy->kills);

            hitter->set_damage(10.0f);  // a knife is worn by each hit (damage - 1)
            ASSERT_TRUE(hitter->attack(slain));
            ASSERT_EQ(1, slain->dead()) << "the same blow kills";
            EXPECT_EQ(paid_for_a_blow + hit_xp + kill_xp, hero->myguy->exp)
                << "the kill: the blow's experience once and one kill bonus";
            EXPECT_EQ(1, hero->myguy->kills);
            EXPECT_EQ(1, hero->myguy->scen_kills);
            EXPECT_EQ(4, hero->myguy->level_kills);
        }
    }
}
