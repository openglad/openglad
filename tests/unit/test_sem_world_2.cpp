/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch world-2: the rules the review found in game_world.cpp, mode_tick.cpp.
// Each test is named after the rule it asserts.

#include <gtest/gtest.h>

#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/respawn/respawn_state.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/resources/gloader.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

walker* add_living(TestGameWorld& tw, int family, unsigned char team,
                   short x, short y)
{
    walker* w = tw.world().add_ob(Order::Living, family);
    if (w == nullptr)
        return nullptr;
    w->set_team_num(team);
    w->setxy(x, y);
    w->set_act_type(ACT_GUARD);  // hold still: positions stay pinned
    w->stats()->set_max_hitpoints(40.0f);
    w->stats()->set_hitpoints(40.0f);
    return w;
}

void open_field(TestGameWorld& tw)
{
    GameWorld& world = tw.world();
    world.resize_grid(32, 32);
    std::fill_n(world.grid.data.get(),
                static_cast<std::size_t>(world.grid.w) * world.grid.h,
                static_cast<std::uint8_t>(PIX_GRASS1));
    world.my_team = 0;
}

bool in_obmap(TestGameWorld& tw, const walker* w)
{
    return tw.world().myobmap->walker_to_pos.count(const_cast<walker*>(w)) != 0;
}

bool in_list(const GameWorld::EntityList& list, const walker* w)
{
    for (const auto& uptr : list)
        if (uptr.get() == w)
            return true;
    return false;
}

int count_events(const og::sim::SimEventLog& log, og::sim::EventKind kind)
{
    int n = 0;
    for (const auto& e : log.events())
        if (e.kind == kind)
            ++n;
    return n;
}

int count_family(const GameWorld::EntityList& list, Order order, int family,
                 bool alive_only = true)
{
    int n = 0;
    for (const auto& uptr : list)
        if (uptr && uptr->query_order() == order && uptr->family() == family &&
            (!alive_only || !uptr->dead()))
            ++n;
    return n;
}

// A walker whose act() is counted (and optionally scripted). Filed into the
// world's lists directly, so the tick's control flow is observed without any
// family AI in the way.
struct TickWalker : walker
{
    int acts = 0;
    std::function<void(TickWalker&)> on_act;
    bool act() override
    {
        ++acts;
        if (on_act)
            on_act(*this);
        return true;
    }
};

TickWalker* push_tick_walker(GameWorld::EntityList& list, Order order,
                             int family, unsigned char team, short x = 0,
                             short y = 0)
{
    auto w = std::make_unique<TickWalker>();
    w->set_order_family(order, static_cast<char>(family));
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_team_num(team);
    w->set_real_team_num(255);
    TickWalker* out = w.get();
    list.push_back(std::move(w));
    out->setxy(x, y);
    return out;
}

constexpr std::array<unsigned char, 4> kGrass = {PIX_GRASS1, PIX_GRASS2,
                                                 PIX_GRASS3, PIX_GRASS4};

// Scripted-world fixture (the shape of tests/unit/test_mode_tick.cpp's).
loader& sem_world_2_loader()
{
    static loader instance{EntityFactory{}};
    return instance;
}

struct ScriptWorld : TestGameWorld
{
    og::test::ScopedPackStoreState pack_store_restore;

    explicit ScriptWorld(int level_id, bool scripted)
        : TestGameWorld(level_id)
    {
        init_all_registries();
        og::script::clear_pack_scripts();
        loader* game_loader = &sem_world_2_loader();
        world().entity_factory =
            [game_loader](Order order, std::int32_t family) {
                return game_loader->create_walker_owned(order, family);
            };
        world().entity_configurator =
            [game_loader](walker& entity, Order order,
                          std::int32_t family) -> const PixieData* {
                game_loader->set_walker(&entity, order, family);
                return game_loader->graphics_for(entity.query_order(),
                                                 entity.family());
            };
        world().entity_derived_stats =
            [game_loader](walker* entity, Order order, std::int32_t family) {
                if (entity != nullptr)
                    game_loader->set_derived_stats(entity, order, family);
            };
        if (scripted)
            world().type = GameWorld::TYPE_SCRIPTED;
        world().my_team = 0;
    }

    ~ScriptWorld() { og::script::clear_pack_scripts(); }

    void register_script(const std::string& body)
    {
        og::script::register_pack_script({"test.semworld2", "zz_semworld2.lua", body});
    }

    int log_count(const std::string& needle)
    {
        int count = 0;
        for (const auto& line : world().scripts().host().log())
            if (line.find(needle) != std::string::npos)
                ++count;
        return count;
    }

    walker* spawn_living(int family, int team, int x, int y)
    {
        walker* w = world().add_ob(Order::Living, family);
        if (w == nullptr)
            return nullptr;
        w->setxy(static_cast<short>(x), static_cast<short>(y));
        w->set_team_num(static_cast<unsigned char>(team));
        w->set_real_team_num(255);
        w->set_act_type(ACT_CONTROL);
        return w;
    }

    walker* spawn_hero(int family, int team, int x, int y, int guy_id)
    {
        walker* w = spawn_living(family, team, x, y);
        if (w == nullptr)
            return nullptr;
        w->set_owned_myguy(std::make_unique<guy>(family));
        w->myguy->id = guy_id;
        return w;
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// game_world.cpp:1404 find_friends_in_range
// ---------------------------------------------------------------------------

// Only Livings are friends: a friendly generator and a friendly weapon in
// range are never returned, wherever the searched list holds them.
TEST(SemWorld, the_ally_finder_returns_friendly_livings_never_generators_or_weapons)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    walker* searcher = add_living(tw, FAMILY_SOLDIER, 0, 100, 100);
    walker* archer = add_living(tw, FAMILY_ARCHER, 0, 110, 100);
    walker* tent = world.add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_TRUE(searcher && archer && tent);
    tent->set_team_num(0);
    tent->setxy(120, 100);
    // A friendly weapon filed in the object list itself, and one in weaplist.
    TickWalker* oblist_knife =
        push_tick_walker(world.oblist, Order::Weapon, FAMILY_KNIFE, 0, 105, 100);
    walker* knife = world.add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_TRUE(oblist_knife && knife);
    knife->set_team_num(0);
    knife->setxy(104, 100);
    ASSERT_TRUE(searcher->is_friendly(tent)) << "staging: the tent is friendly";
    ASSERT_TRUE(searcher->is_friendly(oblist_knife)) << "staging: the knife is friendly";

    std::int32_t n = -1;
    const std::list<walker*> found =
        world.find_friends_in_range(world.oblist, 400, &n, searcher);
    EXPECT_EQ(std::list<walker*>{archer}, found)
        << "friendly generator / weapon arm: only the archer is a friend";
    EXPECT_EQ(1, n);

    n = -1;
    EXPECT_TRUE(world.find_friends_in_range(world.weaplist, 400, &n, searcher).empty())
        << "a friendly weapon in weaplist is never a friend";
    EXPECT_EQ(0, n);
}

// ---------------------------------------------------------------------------
// game_world.cpp:1441 create_new_grid
// ---------------------------------------------------------------------------

// A new blank level is 40x60 cells, each one of the four grass variants
// chosen by exactly one world-RNG draw of next(4), in cell order.
TEST(SemWorld, a_new_blank_level_is_forty_by_sixty_grass_one_draw_per_cell)
{
    TestGameWorld tw;
    GameWorld& world = tw.world();
    world.rng_.state_ = 0x1234u;
    og::sim::SimRandom twin(0x1234u);
    world.create_new_grid();

    ASSERT_EQ(40, world.grid.w);
    ASSERT_EQ(60, world.grid.h);
    EXPECT_EQ(640, world.pixmaxx);
    EXPECT_EQ(960, world.pixmaxy);
    int mismatches = 0;
    std::array<int, 4> seen{};
    for (int i = 0; i < 40 * 60; ++i)
    {
        const std::uint32_t draw = twin.next(4);
        ++seen[static_cast<std::size_t>(draw)];
        if (world.grid.data[static_cast<std::size_t>(i)] != kGrass[draw])
            ++mismatches;
    }
    EXPECT_EQ(0, mismatches) << "cell i is grass variant [draw i of next(4)]";
    EXPECT_EQ(twin.state_, world.rng_.state_) << "exactly 2400 draws";
    for (int k = 0; k < 4; ++k)
        EXPECT_NE(0, seen[static_cast<std::size_t>(k)]) << "staging: variant " << k << " drawn at least once";
}

// ---------------------------------------------------------------------------
// game_world.cpp:1458 resize_grid
// ---------------------------------------------------------------------------

// Sizes below 3 or above 255 are refused: grid, extents and RNG unchanged.
TEST(SemWorld, a_resize_below_three_or_above_255_leaves_the_level_unchanged)
{
    TestGameWorld tw;
    GameWorld& world = tw.world();
    world.rng_.state_ = 77u;
    world.create_new_grid();
    walker* orc = add_living(tw, FAMILY_ORC, 1, 64, 64);
    ASSERT_NE(nullptr, orc);
    const std::vector<unsigned char> before(world.grid.data.get(),
                                            world.grid.data.get() + 40 * 60);
    const std::uint32_t rng_before = world.rng_.state_;

    const std::array<std::pair<int, int>, 4> refused = {
        std::pair{2, 10}, std::pair{10, 2}, std::pair{256, 10}, std::pair{10, 256}};
    for (const auto& [w, h] : refused)
    {
        world.resize_grid(w, h);
        EXPECT_EQ(40, world.grid.w) << w << "x" << h;
        EXPECT_EQ(60, world.grid.h) << w << "x" << h;
        EXPECT_EQ(640, world.pixmaxx) << w << "x" << h;
        EXPECT_EQ(960, world.pixmaxy) << w << "x" << h;
        EXPECT_EQ(rng_before, world.rng_.state_) << w << "x" << h;
        EXPECT_TRUE(std::equal(before.begin(), before.end(), world.grid.data.get()))
            << w << "x" << h << ": every tile unchanged";
    }
    EXPECT_TRUE(in_list(world.oblist, orc));

    // Control: the limits themselves are accepted.
    world.resize_grid(3, 255);
    EXPECT_EQ(3, world.grid.w);
    EXPECT_EQ(255, world.grid.h);
}

// ---------------------------------------------------------------------------
// game_world.cpp:1614 damage_tile (no grid)
// ---------------------------------------------------------------------------

// A level with no grid damages nothing: 0, and no dirty tile.
TEST(SemWorld, a_blast_on_a_level_with_no_grid_damages_no_tile)
{
    TestGameWorld tw;
    GameWorld& world = tw.world();
    world.delete_grid();
    ASSERT_FALSE(world.grid.valid());
    EXPECT_EQ(0, world.damage_tile(0, 0));
    EXPECT_EQ(0, world.damage_tile(20, 20));
    EXPECT_TRUE(world.grid_dirty_tiles().empty());

    // The guard holds even when the dimensions survive without tile data.
    world.grid.w = 3;
    world.grid.h = 3;
    ASSERT_FALSE(world.grid.valid());
    EXPECT_EQ(0, world.damage_tile(16, 16)) << "no grid data: returns 0";
    EXPECT_TRUE(world.grid_dirty_tiles().empty());
    world.grid.w = 0;
    world.grid.h = 0;
}

// ---------------------------------------------------------------------------
// game_world.cpp:1638 damage_tile (grass chars)
// ---------------------------------------------------------------------------

// Each of the four plain grass variants chars to PIX_GRASS1_DAMAGED and is
// returned; damaged, dark and light grass are returned unchanged.
TEST(SemWorld, a_blast_chars_each_plain_grass_variant_and_nothing_else)
{
    TestGameWorld tw;
    GameWorld& world = tw.world();
    world.resize_grid(3, 3);
    const std::array<unsigned char, 9> cells = {
        PIX_GRASS1,          PIX_GRASS2,       PIX_GRASS3,
        PIX_GRASS4,          PIX_GRASS1_DAMAGED, PIX_GRASS_DARK_1,
        PIX_GRASS_LIGHT_1,   PIX_GRASS1,       PIX_GRASS1};
    for (std::size_t i = 0; i < cells.size(); ++i)
        world.grid.data[i] = cells[i];
    world.clear_grid_dirty_tiles();

    for (int i = 0; i < 4; ++i)
    {
        const short x = static_cast<short>((i % 3) * GRID_SIZE + 2);
        const short y = static_cast<short>((i / 3) * GRID_SIZE + 2);
        EXPECT_EQ(static_cast<char>(PIX_GRASS1_DAMAGED), world.damage_tile(x, y))
            << "GRASS" << (i + 1) << " is returned charred";
        EXPECT_EQ(PIX_GRASS1_DAMAGED, world.grid.data[static_cast<std::size_t>(i)])
            << "GRASS" << (i + 1) << " cell rewritten";
    }
    for (int i = 4; i < 7; ++i)
    {
        const short x = static_cast<short>((i % 3) * GRID_SIZE + 2);
        const short y = static_cast<short>((i / 3) * GRID_SIZE + 2);
        EXPECT_EQ(static_cast<char>(cells[static_cast<std::size_t>(i)]),
                  world.damage_tile(x, y))
            << "other byte " << int(cells[static_cast<std::size_t>(i)]) << " returned as is";
        EXPECT_EQ(cells[static_cast<std::size_t>(i)],
                  world.grid.data[static_cast<std::size_t>(i)]);
    }
    const std::vector<std::pair<short, short>> dirty = {
        {0, 0}, {1, 0}, {2, 0}, {0, 1}};
    EXPECT_EQ(dirty, world.grid_dirty_tiles())
        << "only the four charred cells are dirty";
}

// ---------------------------------------------------------------------------
// game_world.cpp:1673 remaining_foes
// ---------------------------------------------------------------------------

// The HUD foe count: a hostile generator does not count, a dormant or hidden
// hostile Living does.
TEST(SemWorld, the_foe_count_skips_generators_and_counts_dormant_and_hidden_foes)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    walker* hero = add_living(tw, FAMILY_SOLDIER, 0, 64, 64);
    walker* tent = world.add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_TRUE(hero && tent);
    tent->set_team_num(1);
    tent->setxy(200, 200);
    EXPECT_EQ(0, world.remaining_foes(hero)) << "hostile generator: not counted";

    walker* sleeper = add_living(tw, FAMILY_ORC, 1, 300, 300);
    ASSERT_NE(nullptr, sleeper);
    sleeper->set_dormant(true);
    EXPECT_EQ(1, world.remaining_foes(hero)) << "dormant hostile: counted";

    walker* ghost = add_living(tw, FAMILY_ORC, 1, 340, 300);
    ASSERT_NE(nullptr, ghost);
    ghost->set_hidden(true);
    ASSERT_TRUE(ghost->hidden());
    EXPECT_EQ(2, world.remaining_foes(hero)) << "hidden hostile: counted";
}

// ---------------------------------------------------------------------------
// game_world.cpp:1727 tick (freeze countdown)
// ---------------------------------------------------------------------------

// FREEZE TIME counts down by one; only the tick that reaches 1 restores the
// palette. With no freeze the counter stays 0 and no palette event is pushed.
TEST(SemWorld, freeze_time_counts_down_and_only_its_last_tick_restores_the_palette)
{
    TestGameWorld tw;
    GameWorld& world = tw.world();
    world.my_team = 0;

    world.enemy_freeze = 0;
    world.current_palette_id = 1;
    tw.events.clear();
    world.tick();
    EXPECT_EQ(0, world.enemy_freeze) << "freeze 0 stays 0";
    EXPECT_EQ(0, count_events(tw.events, og::sim::EventKind::SetPalette))
        << "freeze 0: no SetPalette";
    EXPECT_EQ(1, world.current_palette_id);

    world.enemy_freeze = 11;
    tw.events.clear();
    world.tick();
    EXPECT_EQ(10, world.enemy_freeze) << "freeze 11 -> 10";
    EXPECT_EQ(0, count_events(tw.events, og::sim::EventKind::SetPalette))
        << "freeze 11: no SetPalette";
    EXPECT_EQ(1, world.current_palette_id) << "freeze 11: palette untouched";
}

// ---------------------------------------------------------------------------
// game_world.cpp:1757 wake_delayed_spawn
// ---------------------------------------------------------------------------

namespace {

walker* make_sleeper(TestGameWorld& tw, short x, short y, unsigned delay)
{
    walker* ob = tw.world().add_ob(Order::Living, FAMILY_ORC);
    if (ob == nullptr)
        return nullptr;
    ob->setxy(x, y);
    ob->set_team_num(1);
    ob->set_real_team_num(1);
    ob->set_act_type(ACT_GUARD);
    ob->set_spawn_delay(static_cast<std::uint16_t>(delay));
    ob->set_dormant(true);
    return ob;
}

walker* make_post(TestGameWorld& tw, short x, short y)
{
    walker* g = add_living(tw, FAMILY_SKELETON, 1, x, y);
    if (g != nullptr)
        g->set_guard_hold_post(true);
    return g;
}

}  // namespace

// A BIT_NO_COLLIDE waker wakes in place even on a held spot (overlap is legal
// for it); without the bit the same waker is moved off the spot.
TEST(SemWorld, a_no_collide_sleeper_wakes_in_place_on_a_held_spot)
{
    for (const bool no_collide : {true, false})
    {
        TestGameWorld tw;
        open_field(tw);
        GameWorld& world = tw.world();
        walker* hero = add_living(tw, FAMILY_SOLDIER, 0, 32, 32);
        walker* blocker = make_post(tw, 160, 160);
        walker* sleeper = make_sleeper(tw, 160, 160, 2);
        ASSERT_TRUE(hero && blocker && sleeper);
        if (no_collide)
            sleeper->stats()->set_bit_flags(BIT_NO_COLLIDE, 1);
        world.tick();
        world.tick();
        ASSERT_TRUE(sleeper->dormant()) << "still within the delay";
        world.tick();  // level tick 3 > delay 2
        EXPECT_FALSE(sleeper->dormant()) << "no_collide=" << no_collide;
        if (no_collide)
        {
            EXPECT_EQ(160, sleeper->xpos()) << "BIT_NO_COLLIDE: wakes in place";
            EXPECT_EQ(160, sleeper->ypos()) << "BIT_NO_COLLIDE: wakes in place";
        }
        else
        {
            // The first clear ring-1 cell in scan order (one tile up and left).
            EXPECT_EQ(144, sleeper->xpos())
                << "control: a colliding waker is moved off the held spot";
            EXPECT_EQ(144, sleeper->ypos())
                << "control: a colliding waker is moved off the held spot";
        }
    }
}

// Every wake spawns one FAMILY_FLASH, on the waker's own floor.
TEST(SemWorld, a_wake_flashes_once_on_the_wakers_floor)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    world.set_floor_count(2);
    walker* hero = add_living(tw, FAMILY_SOLDIER, 0, 32, 32);
    walker* sleeper = make_sleeper(tw, 160, 160, 2);
    ASSERT_TRUE(hero && sleeper);
    sleeper->set_floor(1);
    world.tick();
    world.tick();
    ASSERT_EQ(0, count_family(world.oblist, Order::FX, FAMILY_FLASH, false));
    world.tick();
    ASSERT_FALSE(sleeper->dormant());
    EXPECT_EQ(160, sleeper->xpos());
    EXPECT_EQ(160, sleeper->ypos());
    ASSERT_EQ(1, count_family(world.oblist, Order::FX, FAMILY_FLASH, false))
        << "exactly one flash per wake";
    for (const auto& uptr : world.oblist)
    {
        if (uptr->query_order() == Order::FX && uptr->family() == FAMILY_FLASH)
        {
            EXPECT_EQ(1, uptr->floor()) << "the flash is on the waker's floor";
        }
    }
}

// ---------------------------------------------------------------------------
// game_world.cpp:1781 tick (withdraw)
// ---------------------------------------------------------------------------

// A withdraw raised by a walker's act stops the object phase there: later
// walkers and every weapon skip the tick, and the dead sweep does not run.
TEST(SemWorld, a_withdraw_raised_in_the_object_phase_ends_the_tick)
{
    TestGameWorld tw;
    GameWorld& world = tw.world();
    world.my_team = 0;
    TickWalker* raiser = push_tick_walker(world.oblist, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    TickWalker* later = push_tick_walker(world.oblist, Order::Living, FAMILY_SOLDIER, 0, 96, 64);
    TickWalker* foe = push_tick_walker(world.oblist, Order::Living, FAMILY_ORC, 1, 112, 64);
    TickWalker* corpse = push_tick_walker(world.oblist, Order::Living, FAMILY_ORC, 1, 128, 64);
    TickWalker* missile = push_tick_walker(world.weaplist, Order::Weapon, FAMILY_KNIFE, 0, 160, 64);
    corpse->set_dead(1);
    bool raise = false;
    raiser->on_act = [&world, &raise](TickWalker&) {
        if (raise)
            world.withdraw_requested = true;
    };

    world.tick();  // control: no withdraw, everyone acts
    EXPECT_EQ(1, raiser->acts);
    EXPECT_EQ(1, later->acts) << "no withdraw: all act";
    EXPECT_EQ(1, missile->acts) << "no withdraw: all act";
    EXPECT_FALSE(in_list(world.oblist, corpse)) << "control: the sweep runs";

    TickWalker* corpse2 = push_tick_walker(world.oblist, Order::Living, FAMILY_ORC, 1, 128, 96);
    corpse2->set_dead(1);
    raise = true;
    world.tick();
    EXPECT_TRUE(world.withdraw_requested);
    EXPECT_EQ(2, raiser->acts);
    EXPECT_EQ(1, later->acts) << "mid object phase: a later walker does not act";
    EXPECT_EQ(1, foe->acts) << "mid object phase: a later walker does not act";
    EXPECT_EQ(1, missile->acts) << "mid object phase: weapons do not act";
    EXPECT_TRUE(in_list(world.oblist, corpse2)) << "the dead sweep is skipped";
    world.withdraw_requested = false;
}

// ---------------------------------------------------------------------------
// game_world.cpp:1789 tick (dormant / hidden)
// ---------------------------------------------------------------------------

// A hidden friendly walker neither acts nor holds the level open.
TEST(SemWorld, a_hidden_friend_does_not_act_or_hold_the_level)
{
    TestGameWorld tw;
    GameWorld& world = tw.world();
    world.my_team = 0;
    TickWalker* hero = push_tick_walker(world.oblist, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    TickWalker* friend_ = push_tick_walker(world.oblist, Order::Living, FAMILY_SOLDIER, 0, 96, 64);
    friend_->set_hidden(true);
    ASSERT_TRUE(friend_->hidden());
    world.tick();
    EXPECT_EQ(1, hero->acts);
    EXPECT_EQ(0, friend_->acts) << "hidden friendly: no act";
    EXPECT_EQ(2, world.level_done) << "hidden friendly: does not hold the level";
    EXPECT_TRUE(world.game_ended);
}

// A hidden foe holds the level open on its own, without acting.
TEST(SemWorld, a_hidden_foe_holds_the_level_without_acting)
{
    TestGameWorld tw;
    GameWorld& world = tw.world();
    world.my_team = 0;
    push_tick_walker(world.oblist, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    TickWalker* foe = push_tick_walker(world.oblist, Order::Living, FAMILY_ORC, 1, 96, 64);
    foe->set_hidden(true);
    world.tick();
    EXPECT_EQ(0, foe->acts);
    EXPECT_EQ(0, world.level_done) << "hidden hostile: holds the level";
    EXPECT_FALSE(world.game_ended);
}

// A dormant walker does not act before its delay, and acts on its wake tick.
TEST(SemWorld, a_sleeper_acts_first_on_the_tick_it_wakes)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    push_tick_walker(world.oblist, Order::Living, FAMILY_SOLDIER, 0, 32, 32);
    TickWalker* sleeper = push_tick_walker(world.oblist, Order::Living, FAMILY_ORC, 1, 160, 160);
    sleeper->set_spawn_delay(2);
    sleeper->set_dormant(true);
    world.tick();
    world.tick();
    EXPECT_EQ(0, sleeper->acts) << "dormant, delay not reached: no act";
    EXPECT_TRUE(sleeper->dormant());
    EXPECT_EQ(0, world.level_done);
    world.tick();
    EXPECT_FALSE(sleeper->dormant());
    EXPECT_EQ(1, sleeper->acts) << "woken: acts that same tick";
}

// ---------------------------------------------------------------------------
// game_world.cpp:1802 tick (freeze act gate)
// ---------------------------------------------------------------------------

// During FREEZE TIME a hostile generator skips its turn, while effects and
// treasures in the object list still act, on both settings: with my_team 0
// and team-1 walkers, fix AF's two rules agree.
TEST(SemWorld, freeze_time_stops_hostile_generators_but_not_effects_or_treasures)
{
    for (const short fixes : {short{0}, short{1}})
    {
        TestGameWorld tw;
        GameWorld& world = tw.world();
        world.my_team = 0;
        world.gameplay_fixes = fixes;
        push_tick_walker(world.oblist, Order::Living, FAMILY_SOLDIER, 0, 32, 32);
        TickWalker* gen = push_tick_walker(world.oblist, Order::Generator, FAMILY_TENT, 1, 64, 64);
        TickWalker* fx = push_tick_walker(world.oblist, Order::FX, FAMILY_EXPLOSION, 1, 96, 64);
        TickWalker* gold = push_tick_walker(world.oblist, Order::Treasure, FAMILY_GOLD_BAR, 1, 128, 64);
        world.enemy_freeze = 20;
        for (int i = 0; i < 5; ++i)
            world.tick();
        EXPECT_EQ(0, gen->acts) << "freeze, hostile Generator: does not act; fixes " << fixes;
        EXPECT_EQ(5, fx->acts) << "freeze, effect in oblist: acts; fixes " << fixes;
        EXPECT_EQ(5, gold->acts) << "freeze, treasure in oblist: acts; fixes " << fixes;
    }
}

// Fix AF (#461). During FREEZE TIME the old game lets a Living or Generator
// act when it is on team 0 itself; with Gameplay Fixes on it acts when it is
// friendly to the player's team, the head of its owner chain wearing
// my_team. A team-0 Living owned by a live team-1 walker acts with the
// setting off and is frozen with it on; a team-1 Living owned by a live
// team-0 walker is frozen off and acts on, and a team-1 Generator owned by
// a team-0 walker goes the same way. The unowned owners agree on both
// settings.
TEST(SemWorld, freeze_time_asks_team_zero_with_the_fixes_off_and_the_owner_chain_with_them_on)
{
    for (const short fixes : {short{0}, short{1}})
    {
        TestGameWorld tw;
        GameWorld& world = tw.world();
        world.my_team = 0;
        world.gameplay_fixes = fixes;
        TickWalker* foe_owner = push_tick_walker(world.oblist, Order::Living, FAMILY_MAGE, 1, 32, 32);
        TickWalker* our_owner = push_tick_walker(world.oblist, Order::Living, FAMILY_MAGE, 0, 64, 32);
        TickWalker* red_of_foe = push_tick_walker(world.oblist, Order::Living, FAMILY_SKELETON, 0, 96, 32);
        TickWalker* blue_of_ours = push_tick_walker(world.oblist, Order::Living, FAMILY_SKELETON, 1, 128, 32);
        TickWalker* tent_of_ours = push_tick_walker(world.oblist, Order::Generator, FAMILY_TENT, 1, 160, 64);
        red_of_foe->set_owner(foe_owner);
        blue_of_ours->set_owner(our_owner);
        tent_of_ours->set_owner(our_owner);
        world.enemy_freeze = 20;
        for (int i = 0; i < 3; ++i)
            world.tick();
        EXPECT_EQ(0, foe_owner->acts) << "a team-1 owner: frozen; fixes " << fixes;
        EXPECT_EQ(3, our_owner->acts) << "a team-0 owner: acts; fixes " << fixes;
        EXPECT_EQ(fixes != 0 ? 0 : 3, red_of_foe->acts)
            << "team 0, owned by a team-1 walker: acts off (team 0), frozen on (chain); fixes "
            << fixes;
        EXPECT_EQ(fixes != 0 ? 3 : 0, blue_of_ours->acts)
            << "team 1, owned by a team-0 walker: frozen off (team 1), acts on (chain); fixes "
            << fixes;
        EXPECT_EQ(fixes != 0 ? 3 : 0, tent_of_ours->acts)
            << "a team-1 Generator owned by a team-0 walker: the same rule; fixes " << fixes;
        EXPECT_EQ(0, world.level_done) << "frozen or not, the team-1 owner holds the level";
    }
}

// ---------------------------------------------------------------------------
// game_world.cpp:1812 tick (level census)
// ---------------------------------------------------------------------------

// A foe that dies during its own act does not hold the level open.
TEST(SemWorld, a_foe_that_dies_in_its_own_act_does_not_hold_the_level)
{
    TestGameWorld tw;
    GameWorld& world = tw.world();
    world.my_team = 0;
    push_tick_walker(world.oblist, Order::Living, FAMILY_SOLDIER, 0, 32, 32);
    TickWalker* foe = push_tick_walker(world.oblist, Order::Living, FAMILY_ORC, 1, 64, 32);
    world.living_count = 2;
    foe->on_act = [](TickWalker& self) { self.set_dead(1); };
    world.tick();
    EXPECT_EQ(1, foe->acts);
    EXPECT_EQ(2, world.level_done) << "dies in its act: does not hold the level";
    EXPECT_TRUE(world.game_ended);
}

// Hostile generators alone never hold the level: 2 without an exit, 1 with.
TEST(SemWorld, hostile_generators_alone_never_hold_the_level_open)
{
    for (const bool with_exit : {false, true})
    {
        TestGameWorld tw;
        GameWorld& world = tw.world();
        world.my_team = 0;
        push_tick_walker(world.oblist, Order::Living, FAMILY_SOLDIER, 0, 32, 32);
        TickWalker* gen = push_tick_walker(world.oblist, Order::Generator, FAMILY_TENT, 1, 64, 64);
        if (with_exit)
            push_tick_walker(world.fxlist, Order::Treasure, FAMILY_EXIT, 0, 200, 200);
        world.tick();
        EXPECT_EQ(1, gen->acts) << "no freeze: the generator acts";
        EXPECT_EQ(with_exit ? 1 : 2, world.level_done) << "exit=" << with_exit;
        EXPECT_EQ(!with_exit, world.game_ended) << "exit=" << with_exit;
    }
}

// ---------------------------------------------------------------------------
// game_world.cpp:1816 tick (force foes)
// ---------------------------------------------------------------------------

// After acting, a walker with neither foe nor leader is handed the nearest
// foe; one with a leader is not; with no foe on the map it stays without.
TEST(SemWorld, a_walker_without_foe_or_leader_is_handed_the_nearest_foe)
{
    {
        TestGameWorld tw;
        open_field(tw);
        GameWorld& world = tw.world();
        walker* soldier = add_living(tw, FAMILY_SOLDIER, 0, 100, 100);
        walker* orc = add_living(tw, FAMILY_ORC, 1, 200, 100);
        walker* far_orc = add_living(tw, FAMILY_ORC, 1, 400, 400);
        ASSERT_TRUE(soldier && orc && far_orc);
        for (walker* w : {soldier, orc, far_orc})
            w->set_act_type(ACT_CONTROL);
        world.tick();
        EXPECT_EQ(orc, soldier->foe()) << "no foe, no leader: nearest hostile";
        EXPECT_EQ(soldier, orc->foe()) << "no foe, no leader: nearest hostile";
    }
    {
        TestGameWorld tw;
        open_field(tw);
        GameWorld& world = tw.world();
        walker* soldier = add_living(tw, FAMILY_SOLDIER, 0, 100, 100);
        walker* archer = add_living(tw, FAMILY_ARCHER, 0, 80, 100);
        walker* orc = add_living(tw, FAMILY_ORC, 1, 200, 100);
        ASSERT_TRUE(soldier && archer && orc);
        for (walker* w : {soldier, archer, orc})
            w->set_act_type(ACT_CONTROL);
        soldier->set_leader(archer);
        world.tick();
        EXPECT_EQ(nullptr, soldier->foe()) << "has leader: no forced foe";
        EXPECT_EQ(archer, soldier->leader());
        EXPECT_EQ(orc, archer->foe()) << "control: the leaderless archer gets one";
    }
    {
        TestGameWorld tw;
        open_field(tw);
        GameWorld& world = tw.world();
        walker* soldier = add_living(tw, FAMILY_SOLDIER, 0, 100, 100);
        walker* archer = add_living(tw, FAMILY_ARCHER, 0, 150, 100);
        ASSERT_TRUE(soldier && archer);
        soldier->set_act_type(ACT_CONTROL);
        archer->set_act_type(ACT_CONTROL);
        world.tick();
        EXPECT_EQ(nullptr, soldier->foe()) << "no hostile: foe stays null";
    }
}

// ---------------------------------------------------------------------------
// game_world.cpp:1858 tick (weapon phase)
// ---------------------------------------------------------------------------

// Missiles are never frozen; a dead weapon is skipped.
TEST(SemWorld, live_missiles_act_through_freeze_time_and_dead_ones_never)
{
    for (const int freeze : {20, 0})
    {
        TestGameWorld tw;
        GameWorld& world = tw.world();
        world.my_team = 0;
        push_tick_walker(world.oblist, Order::Living, FAMILY_SOLDIER, 0, 32, 32);
        TickWalker* foe = push_tick_walker(world.oblist, Order::Living, FAMILY_ORC, 1, 64, 32);
        TickWalker* missile = push_tick_walker(world.weaplist, Order::Weapon, FAMILY_ARROW, 1, 96, 32);
        TickWalker* spent = push_tick_walker(world.weaplist, Order::Weapon, FAMILY_ARROW, 1, 128, 32);
        spent->set_dead(1);
        // The sweep frees the dead weapon at tick end, so its acts are
        // counted outside it.
        int spent_acts = 0;
        spent->on_act = [&spent_acts](TickWalker&) { ++spent_acts; };
        world.enemy_freeze = freeze;
        world.tick();
        EXPECT_EQ(freeze ? 0 : 1, foe->acts) << "staging: freeze=" << freeze;
        EXPECT_EQ(1, missile->acts) << "live missile acts, freeze=" << freeze;
        EXPECT_EQ(0, spent_acts) << "dead weapon: skipped, freeze=" << freeze;
    }
}

// ---------------------------------------------------------------------------
// game_world.cpp:1877 tick (exit scan)
// ---------------------------------------------------------------------------

// A dead exit pad is ignored: the level is "no foes, no exit" and ends.
TEST(SemWorld, a_dead_exit_pad_does_not_keep_a_cleared_level_waiting)
{
    for (const bool exit_dead : {true, false})
    {
        TestGameWorld tw;
        GameWorld& world = tw.world();
        world.my_team = 0;
        push_tick_walker(world.oblist, Order::Living, FAMILY_SOLDIER, 0, 32, 32);
        walker* pad = world.add_fx_ob(Order::Treasure, FAMILY_EXIT);
        ASSERT_NE(nullptr, pad);
        pad->setxy(200, 200);
        if (exit_dead)
            pad->set_dead(1);
        world.tick();
        EXPECT_EQ(exit_dead ? 2 : 1, world.level_done) << "exit dead=" << exit_dead;
        EXPECT_EQ(exit_dead, world.game_ended) << "exit dead=" << exit_dead;
    }
}

// ---------------------------------------------------------------------------
// game_world.cpp:1892 tick (scripted completion)
// ---------------------------------------------------------------------------

constexpr const char* kQuietModeScript =
    "og.register_level_hooks(42, {\n"
    "  on_mode_init = function(level)\n"
    "    og.log('mode_init', level)\n"
    "  end,\n"
    "  on_mode_tick = function(level, tick)\n"
    "    og.log('mode_tick', tick)\n"
    "  end,\n"
    "})\n";

// A scripted level whose mode is active never takes the classic kill-all:
// with no foe on the map it stays open, tick after tick.
TEST(SemWorld, an_active_mode_owns_completion_and_kill_all_never_fires)
{
    ScriptWorld fx(42, /*scripted=*/true);
    fx.register_script(kQuietModeScript);
    ASSERT_NE(nullptr, fx.spawn_hero(FAMILY_SOLDIER, 0, 160, 160, 1));
    for (int t = 1; t <= 3; ++t)
    {
        fx.world().tick();
        EXPECT_TRUE(fx.world().mode.active) << "tick " << t;
        EXPECT_EQ(2, fx.world().level_done) << "tick " << t << ": no foe on the map";
        EXPECT_FALSE(fx.world().game_ended)
            << "tick " << t << ": the classic kill-all never fires";
    }
    EXPECT_EQ(3, fx.log_count("mode_tick"));
}

// When the mode declares a winner the rest of the tick is skipped: the corpse
// lying in the object list is not swept that tick.
TEST(SemWorld, a_mode_win_ends_the_tick_before_the_dead_sweep)
{
    ScriptWorld fx(42, /*scripted=*/true);
    fx.register_script(
        "og.register_level_hooks(42, {\n"
        "  on_mode_init = function(level) end,\n"
        "  on_mode_tick = function(level, tick)\n"
        "    if tick == 3 then og.declare_winner(0) end\n"
        "  end,\n"
        "})\n");
    ASSERT_NE(nullptr, fx.spawn_hero(FAMILY_SOLDIER, 0, 160, 160, 1));
    walker* foe = fx.spawn_living(FAMILY_ORC, 1, 400, 400);
    ASSERT_NE(nullptr, foe);
    fx.world().tick();
    fx.world().tick();
    walker* corpse = fx.spawn_living(FAMILY_ORC, 1, 300, 300);
    ASSERT_NE(nullptr, corpse);
    corpse->set_dead(1);
    fx.world().tick();
    EXPECT_TRUE(fx.world().game_ended);
    EXPECT_TRUE(in_list(fx.world().oblist, corpse))
        << "mode declared winner: rest of tick skipped (no dead sweep)";
}

// ---------------------------------------------------------------------------
// game_world.cpp:1940 tick (lineup stage step)
// ---------------------------------------------------------------------------

constexpr const char* kLineupLogOnly =
    "og.register_level_hooks(-1, {\n"
    "  on_lineup_stage = function(level)\n"
    "    og.log('lineup_stage', level)\n"
    "  end,\n"
    "})\n";

// An un-staged classic level whose lineup step fields nobody completes on
// its first tick like any other empty level.
TEST(SemWorld, a_lineup_step_that_fields_nobody_lets_tick_one_complete)
{
    ScriptWorld fx(7, /*scripted=*/false);
    fx.register_script(kLineupLogOnly);
    fx.world().tick();
    EXPECT_EQ(1, fx.log_count("lineup_stage\t7"));
    EXPECT_EQ(2, fx.world().level_done) << "a no-op step does not hold the level";
    EXPECT_TRUE(fx.world().game_ended);
    EXPECT_EQ(8, fx.world().next_level);
}

// ---------------------------------------------------------------------------
// game_world.cpp:2081 run_lineup_stage_step
// ---------------------------------------------------------------------------

// When a mode owns the level the lineup hook never runs.
TEST(SemWorld, the_lineup_step_never_runs_where_a_mode_owns_the_level)
{
    ScriptWorld fx(7, /*scripted=*/false);
    fx.register_script(kLineupLogOnly);
    fx.world().mode.active = true;
    EXPECT_FALSE(fx.world().run_lineup_stage_step()) << "mode active: returns false";
    EXPECT_EQ(0, fx.log_count("lineup_stage")) << "mode active: hook not called";
    fx.world().mode.active = false;
    EXPECT_TRUE(fx.world().run_lineup_stage_step()) << "no mode: the hook's result";
    EXPECT_EQ(1, fx.log_count("lineup_stage\t7"));
}

// ---------------------------------------------------------------------------
// game_world.cpp:2104 landing_blocked_by / 2128 floor_landing_clear
// ---------------------------------------------------------------------------

namespace {

// One world per occupant: the lander probes (96, 96) on floor 0 with the
// occupant built by `make` standing on it.
bool landing_clear_with(const std::function<walker*(TestGameWorld&)>& make,
                        bool* occupant_in_obmap = nullptr)
{
    TestGameWorld tw;
    open_field(tw);
    walker* lander = add_living(tw, FAMILY_SOLDIER, 0, 400, 400);
    walker* occupant = make(tw);
    if (lander == nullptr || occupant == nullptr)
    {
        ADD_FAILURE() << "staging: lander or occupant missing";
        return false;
    }
    if (occupant_in_obmap != nullptr)
        *occupant_in_obmap = in_obmap(tw, occupant);
    return tw.world().floor_landing_clear(lander, 96.0f, 96.0f, 0);
}

walker* make_at(TestGameWorld& tw, Order order, int family, short x, short y)
{
    walker* w = tw.world().add_ob(order, family);
    if (w == nullptr)
        return nullptr;
    w->set_team_num(1);
    w->setxy(x, y);
    return w;
}

}  // namespace

// Arrival is blocked by a live Living or Generator and by solid scenery
// (door, tree, boulder); a missile, a treasure, a corpse or a walker whose
// edge only touches the box never block.
TEST(SemWorld, only_bodies_generators_and_solid_scenery_block_an_arrival)
{
    struct Case
    {
        const char* name;
        Order order;
        int family;
        bool expect_clear;
    };
    const std::array<Case, 7> cases = {{
        {"live orc", Order::Living, FAMILY_ORC, false},
        {"tent generator", Order::Generator, FAMILY_TENT, false},
        {"door", Order::Weapon, FAMILY_DOOR, false},
        {"tree", Order::Weapon, FAMILY_TREE, false},
        {"boulder", Order::Weapon, FAMILY_BOULDER, false},
        {"knife missile", Order::Weapon, FAMILY_KNIFE, true},
        {"gold treasure", Order::Treasure, FAMILY_GOLD_BAR, true},
    }};
    for (const Case& c : cases)
    {
        bool filed = false;
        const bool clear = landing_clear_with(
            [&c](TestGameWorld& tw) { return make_at(tw, c.order, c.family, 96, 96); },
            &filed);
        EXPECT_TRUE(filed) << c.name << ": staging, the occupant is in the obmap";
        EXPECT_EQ(c.expect_clear, clear) << c.name;
    }

    bool filed = false;
    EXPECT_TRUE(landing_clear_with(
        [](TestGameWorld& tw) {
            walker* w = make_at(tw, Order::Living, FAMILY_ORC, 96, 96);
            if (w != nullptr)
                w->set_dead(1);
            return w;
        },
        &filed))
        << "dead orc: not blocked";
    EXPECT_TRUE(filed) << "staging: the corpse is still filed in the obmap";

    // Edges: the lander is sizex wide; an orc starting at 96 + sizex only
    // touches the box, one pixel closer overlaps it.
    short width = 0;
    {
        TestGameWorld tw;
        walker* probe = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, probe);
        width = probe->sizex();
    }
    ASSERT_GT(width, 1);
    EXPECT_TRUE(landing_clear_with([width](TestGameWorld& tw) {
        return make_at(tw, Order::Living, FAMILY_ORC, static_cast<short>(96 + width), 96);
    })) << "touching edges only: not blocked";
    EXPECT_FALSE(landing_clear_with([width](TestGameWorld& tw) {
        return make_at(tw, Order::Living, FAMILY_ORC, static_cast<short>(96 + width - 1), 96);
    })) << "control: one pixel of overlap blocks";
}

// The landing probe never eats a treasure it overlaps.
TEST(SemWorld, the_landing_probe_never_eats_the_treasure_on_the_spot)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    walker* lander = add_living(tw, FAMILY_SOLDIER, 0, 400, 400);
    walker* gold = make_at(tw, Order::Treasure, FAMILY_GOLD_BAR, 96, 96);
    ASSERT_TRUE(lander && gold);
    const std::uint32_t gold_id = gold->entity_id();
    const auto score_before = tw.save.m_totalcash[0];
    EXPECT_TRUE(world.floor_landing_clear(lander, 96.0f, 96.0f, 0))
        << "treasure overlaps: clear";
    ASSERT_EQ(gold, world.find_by_id(gold_id));
    EXPECT_FALSE(gold->dead()) << "the treasure is still there";
    EXPECT_TRUE(in_obmap(tw, gold));
    EXPECT_EQ(score_before, tw.save.m_totalcash[0]) << "nothing was collected";
}

// ---------------------------------------------------------------------------
// game_world.cpp:1993 tick (stale cross-references)
// ---------------------------------------------------------------------------

// Links to live walkers survive the end-of-tick cleanup untouched. (The
// controller arm and the fxlist drop are deferred: the old game cleans only
// oblist and weaplist links and never a dead controller.)
TEST(SemWorld, links_to_live_walkers_survive_the_stale_link_cleanup)
{
    TestGameWorld tw;
    GameWorld& world = tw.world();
    world.my_team = 0;
    // Walkers whose act does nothing, so only the cleanup can touch a link.
    TickWalker* soldier = push_tick_walker(world.oblist, Order::Living, FAMILY_SOLDIER, 0, 100, 100);
    TickWalker* orc = push_tick_walker(world.oblist, Order::Living, FAMILY_ORC, 1, 300, 300);
    TickWalker* knife = push_tick_walker(world.weaplist, Order::Weapon, FAMILY_KNIFE, 0, 50, 50);
    TickWalker* spark = push_tick_walker(world.fxlist, Order::FX, FAMILY_EXPLOSION, 0, 60, 60);
    for (walker* w : {static_cast<walker*>(soldier), static_cast<walker*>(knife),
                      static_cast<walker*>(spark)})
    {
        w->set_foe(orc);
        w->set_leader(orc);
        w->set_owner(orc);
        w->set_collide_ob(orc);
    }
    world.tick();
    ASSERT_FALSE(world.game_ended) << "staging: the cleanup runs on unfinished ticks";
    for (walker* w : {static_cast<walker*>(soldier), static_cast<walker*>(knife),
                      static_cast<walker*>(spark)})
    {
        const char* list = w == soldier ? "oblist" : (w == knife ? "weaplist" : "fxlist");
        EXPECT_EQ(orc, w->foe()) << list << ": link alive, foe kept";
        EXPECT_EQ(orc, w->leader()) << list << ": link alive, leader kept";
        EXPECT_EQ(orc, w->owner()) << list << ": link alive, owner kept";
        EXPECT_EQ(orc, w->collide_ob()) << list << ": link alive, collide_ob kept";
    }

    // Control: the same links to a dead walker are dropped from oblist and
    // weaplist walkers, which the old game cleans too. (fxlist links to the
    // dead are not pinned: the old game never cleans fxlist.)
    TickWalker* corpse = push_tick_walker(world.oblist, Order::Living, FAMILY_ORC, 1, 340, 340);
    for (walker* w : {static_cast<walker*>(soldier), static_cast<walker*>(knife)})
    {
        w->set_foe(corpse);
        w->set_leader(corpse);
        w->set_owner(corpse);
        w->set_collide_ob(corpse);
    }
    corpse->set_dead(1);
    world.tick();
    for (walker* w : {static_cast<walker*>(soldier), static_cast<walker*>(knife)})
    {
        const char* list = w == soldier ? "oblist" : "weaplist";
        EXPECT_EQ(nullptr, w->foe()) << list << ": link dead, foe dropped";
        EXPECT_EQ(nullptr, w->leader()) << list << ": link dead, leader dropped";
        EXPECT_EQ(nullptr, w->owner()) << list << ": link dead, owner dropped";
        EXPECT_EQ(nullptr, w->collide_ob()) << list << ": link dead, collide_ob dropped";
    }
}

// game_world.cpp:1993 tick (stale link cleanup), the stats controller arm.
// A walker's stats controller that is dead is dropped at the end of the tick
// in every list; a live one is kept. The divergence the review suspected
// here (the old game never clears a controller, nor cleans fxlist links) was
// refuted in triage: no player sees a difference, since a controller in the
// old game is the walker itself and fxlist walkers never act in either game.
// The rule is pinned as the classic one: nobody is driven by a corpse into
// the next tick.
//
// RED (`stats->controller()->dead()` -> `!stats->controller()->dead()` at
// game_world.cpp:2005): Expected equality of these values: orc
// w->stats()->controller() Which is: NULL (the kept arm; the dead arm keeps
// the corpse).
TEST(SemWorld, a_stats_controller_that_died_is_dropped_at_the_end_of_the_tick)
{
    TestGameWorld tw;
    GameWorld& world = tw.world();
    world.my_team = 0;
    TickWalker* soldier = push_tick_walker(world.oblist, Order::Living, FAMILY_SOLDIER, 0, 100, 100);
    TickWalker* orc = push_tick_walker(world.oblist, Order::Living, FAMILY_ORC, 1, 300, 300);
    TickWalker* knife = push_tick_walker(world.weaplist, Order::Weapon, FAMILY_KNIFE, 0, 50, 50);
    TickWalker* spark = push_tick_walker(world.fxlist, Order::FX, FAMILY_EXPLOSION, 0, 60, 60);
    const std::array<walker*, 3> drivers = {soldier, knife, spark};
    for (walker* w : drivers)
    {
        ASSERT_NE(nullptr, w->stats()) << "staging: every walker has stats";
        w->stats()->set_controller(orc);
    }
    world.tick();
    ASSERT_FALSE(world.game_ended) << "staging: the cleanup runs on unfinished ticks";
    for (walker* w : drivers)
    {
        const char* list = w == soldier ? "oblist" : (w == knife ? "weaplist" : "fxlist");
        EXPECT_EQ(orc, w->stats()->controller()) << list << ": controller alive, kept";
    }

    TickWalker* corpse = push_tick_walker(world.oblist, Order::Living, FAMILY_ORC, 1, 340, 340);
    for (walker* w : drivers)
        w->stats()->set_controller(corpse);
    corpse->set_dead(1);
    world.tick();
    ASSERT_FALSE(world.game_ended) << "staging: the cleanup runs on unfinished ticks";
    for (walker* w : drivers)
    {
        const char* list = w == soldier ? "oblist" : (w == knife ? "weaplist" : "fxlist");
        EXPECT_EQ(nullptr, w->stats()->controller()) << list << ": controller dead, dropped";
    }
}

// ---------------------------------------------------------------------------
// game_world.cpp:2029 tick (dead sweep)
// ---------------------------------------------------------------------------

// A hero corpse (it has a guy record) and a corpse whose respawn is pending
// stay in the object list; a plain AI corpse goes to the dead list.
TEST(SemWorld, hero_corpses_and_corpses_awaiting_respawn_stay_in_the_world)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    walker* keeper = add_living(tw, FAMILY_SOLDIER, 0, 32, 32);
    walker* foe = add_living(tw, FAMILY_ORC, 1, 400, 400);
    walker* hero = add_living(tw, FAMILY_SOLDIER, 0, 100, 100);
    walker* pending = add_living(tw, FAMILY_ORC, 1, 200, 100);
    walker* plain = add_living(tw, FAMILY_ORC, 1, 300, 100);
    ASSERT_TRUE(keeper && foe && hero && pending && plain);
    hero->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    const std::uint32_t hero_id = hero->entity_id();
    const std::uint32_t pending_id = pending->entity_id();
    og::sim::RespawnEntry entry;
    entry.kind = 0;
    entry.team = 1;
    entry.ticks_left = 500;
    entry.walker_entity_id = pending_id;
    world.respawn.respawn_queue.push_back(entry);
    ASSERT_TRUE(og::sim::respawn_pending_for(world, pending));

    hero->set_dead(1);
    pending->set_dead(1);
    plain->set_dead(1);
    const int living_before = world.living_count;
    const std::size_t dead_before = world.dead_list.size();
    world.tick();
    ASSERT_FALSE(world.game_ended);
    EXPECT_TRUE(in_list(world.oblist, hero)) << "dead hero with myguy: stays";
    EXPECT_EQ(hero, world.find_by_id(hero_id));
    EXPECT_TRUE(in_list(world.oblist, pending)) << "pending respawn: stays";
    EXPECT_EQ(pending, world.find_by_id(pending_id));
    EXPECT_FALSE(in_list(world.oblist, plain)) << "control: AI corpse swept";
    EXPECT_TRUE(in_list(world.dead_list, plain));
    EXPECT_EQ(dead_before + 1, world.dead_list.size()) << "only the AI corpse moved";
    EXPECT_EQ(living_before - 1, world.living_count) << "only the AI corpse uncounted";
}

// ---------------------------------------------------------------------------
// game_world.cpp:2175 dormant_overlaps_box
// ---------------------------------------------------------------------------

// Only a live, still-dormant walker on the same floor reserves a spot.
TEST(SemWorld, only_a_live_sleeper_on_the_same_floor_reserves_a_spot)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    world.set_floor_count(2);
    walker* probe = add_living(tw, FAMILY_SOLDIER, 0, 96, 96);
    walker* other = add_living(tw, FAMILY_ORC, 1, 100, 100);
    ASSERT_TRUE(probe && other);
    EXPECT_FALSE(world.dormant_occupies_spot(probe)) << "awake: no reservation";
    other->set_dormant(true);
    EXPECT_TRUE(world.dormant_occupies_spot(probe)) << "dormant, same floor, overlapping";
    other->set_floor(1);
    EXPECT_FALSE(world.dormant_occupies_spot(probe)) << "dormant on another floor";
    other->set_floor(0);
    other->set_dead(1);
    EXPECT_FALSE(world.dormant_occupies_spot(probe)) << "dead dormant";
    other->set_dead(0);
    EXPECT_TRUE(world.dormant_occupies_spot(probe)) << "control: live again";
}

// ---------------------------------------------------------------------------
// game_world.cpp:2220 wake_spot_blocked
// ---------------------------------------------------------------------------

// A missile on a sleeper's spot does not block its wake; a body does.
TEST(SemWorld, a_missile_on_a_sleepers_spot_does_not_block_the_wake)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    walker* sleeper = make_sleeper(tw, 160, 160, 2);
    walker* knife = make_at(tw, Order::Weapon, FAMILY_KNIFE, 160, 160);
    ASSERT_TRUE(sleeper && knife);
    ASSERT_TRUE(in_obmap(tw, knife)) << "staging: the knife is filed on the spot";
    EXPECT_FALSE(world.wake_spot_blocked(sleeper)) << "missile on the spot: not blocked";
    walker* orc = make_at(tw, Order::Living, FAMILY_ORC, 160, 160);
    ASSERT_NE(nullptr, orc);
    EXPECT_TRUE(world.wake_spot_blocked(sleeper)) << "control: a live orc blocks";
}

// ---------------------------------------------------------------------------
// game_world.cpp:2235 relocate_to_nearest_wake_spot
// ---------------------------------------------------------------------------

// With every cell of the four rings blocked the waker does not move, and the
// wake defers with the waker still on its authored spot.
TEST(SemWorld, a_sleeper_walled_in_beyond_four_rings_stays_put)
{
    TestGameWorld tw;
    GameWorld& world = tw.world();
    world.my_team = 0;
    walker* hero = add_living(tw, FAMILY_SOLDIER, 0, 2 * GRID_SIZE, 2 * GRID_SIZE);
    ASSERT_NE(nullptr, hero);
    const short cx = 12;
    const short cy = 12;
    for (int gy = cy - 5; gy <= cy + 5; ++gy)
        for (int gx = cx - 5; gx <= cx + 5; ++gx)
            ASSERT_NE(nullptr, make_post(tw, static_cast<short>(gx * GRID_SIZE),
                                         static_cast<short>(gy * GRID_SIZE)));
    walker* waker = make_sleeper(tw, cx * GRID_SIZE, cy * GRID_SIZE, 1);
    ASSERT_NE(nullptr, waker);

    EXPECT_FALSE(world.relocate_to_nearest_wake_spot(waker)) << "all blocked: false";
    EXPECT_EQ(cx * GRID_SIZE, waker->xpos()) << "all blocked: position unchanged";
    EXPECT_EQ(cy * GRID_SIZE, waker->ypos()) << "all blocked: position unchanged";

    world.tick();
    world.tick();  // the wake tick defers
    EXPECT_TRUE(waker->dormant());
    EXPECT_EQ(cx * GRID_SIZE, waker->xpos());
    EXPECT_EQ(cy * GRID_SIZE, waker->ypos());
}

// ---------------------------------------------------------------------------
// game_world.cpp:2276 has_save_all_protected
// ---------------------------------------------------------------------------

// A flagged NPC that already died (and was swept to the dead list) still
// scopes the SAVE_ALL rule.
TEST(SemWorld, a_dead_protected_npc_still_scopes_the_save_all_rule)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    walker* keeper = add_living(tw, FAMILY_SOLDIER, 0, 32, 32);
    walker* foe = add_living(tw, FAMILY_ORC, 1, 400, 400);
    walker* npc = add_living(tw, FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_TRUE(keeper && foe && npc);
    EXPECT_FALSE(world.has_save_all_protected()) << "control: none flagged";
    npc->set_save_all_protected(true);
    npc->set_dead(1);
    world.tick();
    ASSERT_FALSE(in_list(world.oblist, npc));
    ASSERT_TRUE(in_list(world.dead_list, npc)) << "staging: swept to dead_list";
    EXPECT_TRUE(world.has_save_all_protected())
        << "only a flagged NPC already in dead_list: true";
}

// ---------------------------------------------------------------------------
// game_world.cpp:2326 retire_corpse
// ---------------------------------------------------------------------------

// Retiring a held corpse moves it to the dead list and uncounts it once; a
// walker not in the object list is ignored.
TEST(SemWorld, retiring_a_held_corpse_files_it_dead_exactly_once)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    walker* bystander = add_living(tw, FAMILY_SOLDIER, 0, 32, 32);
    walker* orc = add_living(tw, FAMILY_ORC, 1, 100, 100);
    ASSERT_TRUE(bystander && orc);
    const std::uint32_t id = orc->entity_id();
    orc->set_dead(1);
    const int living_before = world.living_count;
    const std::size_t ob_before = world.oblist.size();
    const std::size_t dead_before = world.dead_list.size();

    world.retire_corpse(orc);
    EXPECT_FALSE(in_list(world.oblist, orc)) << "corpse in oblist: moved out";
    ASSERT_FALSE(world.dead_list.empty());
    EXPECT_EQ(orc, world.dead_list.back().get()) << "last entry of dead_list";
    EXPECT_EQ(nullptr, world.find_by_id(id)) << "unindexed";
    EXPECT_EQ(living_before - 1, world.living_count);
    EXPECT_EQ(ob_before - 1, world.oblist.size());
    EXPECT_EQ(dead_before + 1, world.dead_list.size());

    // Again (now in dead_list), and a walker that was never in oblist.
    TickWalker stranger;
    stranger.set_order_family(Order::Living, FAMILY_ORC);
    world.retire_corpse(orc);
    world.retire_corpse(&stranger);
    EXPECT_EQ(living_before - 1, world.living_count) << "not in oblist: no change";
    EXPECT_EQ(ob_before - 1, world.oblist.size()) << "not in oblist: no change";
    EXPECT_EQ(dead_before + 1, world.dead_list.size()) << "not in oblist: no change";
    EXPECT_TRUE(in_list(world.oblist, bystander));
}

// ---------------------------------------------------------------------------
// mode_tick.cpp:77 authored_team_mask
// ---------------------------------------------------------------------------

// A consumed (dead) team-start marker still counts; a marker for team 4 or
// beyond, and any walker that is not a team-start marker, set no bit.
TEST(SemWorld, the_authored_team_mask_counts_consumed_markers_and_only_markers)
{
    TestGameWorld tw;
    GameWorld& world = tw.world();
    auto marker = [&world](unsigned char team) {
        walker* m = world.add_ob(Order::Special, FAMILY_RESERVED_TEAM);
        if (m != nullptr)
            m->set_team_num(team);
        return m;
    };
    ASSERT_NE(nullptr, marker(0));
    walker* consumed = marker(2);
    ASSERT_NE(nullptr, consumed);
    consumed->set_dead(1);
    ASSERT_NE(nullptr, marker(4));
    ASSERT_NE(nullptr, marker(5));
    walker* soldier = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, soldier);
    soldier->set_team_num(3);
    TickWalker* other_special =
        push_tick_walker(world.oblist, Order::Special, FAMILY_RESERVED_TEAM + 1, 1);
    ASSERT_NE(nullptr, other_special);
    EXPECT_EQ(0b0101, og::sim::authored_team_mask(world))
        << "dead team-2 marker counts; team 4/5 markers, a Living and another "
           "special set no bit";
}

// ---------------------------------------------------------------------------
// mode_tick.cpp:97 effective_team_mask
// ---------------------------------------------------------------------------

// A requested count of 1 is clamped to 2 teams.
TEST(SemWorld, a_one_team_request_is_clamped_to_two_teams)
{
    EXPECT_EQ(0b0011, og::sim::effective_team_mask(0b1111, 1));
    EXPECT_EQ(0b0101, og::sim::effective_team_mask(0b1101, 1));
    EXPECT_EQ(0b0101, og::sim::effective_team_mask(0b1101, 2)) << "control: 2 itself";
}

// ---------------------------------------------------------------------------
// mode_tick.cpp:119 mode_end_level / 128 mode_declare_winner
// ---------------------------------------------------------------------------

// The first end of a scripted match fires every queued respawn and revives
// the dead heroes; ending it again does not flush a second time.
TEST(SemWorld, ending_a_match_flushes_respawns_once_and_never_again)
{
    ScriptWorld fx(42, /*scripted=*/true);
    GameWorld& world = fx.world();
    walker* hero = fx.spawn_hero(FAMILY_SOLDIER, 0, 160, 160, 7);
    ASSERT_NE(nullptr, fx.spawn_living(FAMILY_ORC, 1, 480, 480));
    ASSERT_NE(nullptr, hero);
    hero->set_dead(1);
    og::sim::RespawnEntry ai;
    ai.kind = 1;
    ai.team = 1;
    ai.family = FAMILY_ORC;
    ai.level = 1;
    ai.ticks_left = 50;
    ai.x = 320;
    ai.y = 320;
    world.respawn.respawn_queue.push_back(ai);
    const int orcs_before = count_family(world.oblist, Order::Living, FAMILY_ORC);

    og::sim::mode_end_level(world, 1, -1);
    EXPECT_TRUE(world.mode.win_latched);
    EXPECT_FALSE(hero->dead()) << "first arming: dead hero revived";
    EXPECT_EQ(hero->stats()->max_hitpoints(), hero->stats()->hitpoints());
    EXPECT_TRUE(world.respawn.respawn_queue.empty()) << "first arming: queue fired";
    EXPECT_EQ(orcs_before + 1, count_family(world.oblist, Order::Living, FAMILY_ORC))
        << "first arming: the queued AI respawn fired";

    hero->set_dead(1);
    world.respawn.respawn_queue.push_back(ai);
    og::sim::mode_end_level(world, 0, 7);
    EXPECT_TRUE(hero->dead()) << "re-arming: no re-flush";
    EXPECT_EQ(1u, world.respawn.respawn_queue.size()) << "re-arming: queue untouched";
    EXPECT_EQ(0, world.mode.win_ending);
    EXPECT_EQ(7, world.mode.win_next_level);
}

// Declaring a winner on an already latched match does not flush: the dead
// hero is not revived and so does not make the winner a player.
TEST(SemWorld, a_winner_declared_after_the_latch_revives_nobody)
{
    ScriptWorld fx(42, /*scripted=*/true);
    GameWorld& world = fx.world();
    walker* hero = fx.spawn_hero(FAMILY_SOLDIER, 0, 160, 160, 7);
    ASSERT_NE(nullptr, hero);
    og::sim::mode_end_level(world, 0, 43);
    ASSERT_TRUE(world.mode.win_latched);
    hero->set_dead(1);
    og::sim::RespawnEntry entry;
    entry.kind = 0;
    entry.team = 0;
    entry.ticks_left = 50;
    entry.walker_entity_id = hero->entity_id();
    world.respawn.respawn_queue.push_back(entry);

    og::sim::mode_declare_winner(world, 0);
    EXPECT_TRUE(hero->dead()) << "already latched: no flush";
    EXPECT_EQ(1u, world.respawn.respawn_queue.size()) << "already latched: entry kept";
    EXPECT_FALSE(world.mode.winner_is_player);
    EXPECT_EQ(42, world.mode.win_next_level);
}
