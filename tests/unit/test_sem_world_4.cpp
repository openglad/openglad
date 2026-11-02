/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch world-4: the rules the review found in respawn.cpp, scenario_strip.cpp, sim_control_policy.cpp, sim_input_handler.cpp, specials_view.cpp.
// Each test is named after the rule it asserts.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/sound_ids.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/specials_view.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/respawn/respawn_state.h>
#include <openglad/gameplay/scenario_strip.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/sim_control_policy.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/gloader.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

// --- shared staging -------------------------------------------------------

void open_field(TestGameWorld& tw, int tiles = 64)
{
    GameWorld& world = tw.world();
    world.resize_grid(tiles, tiles);
    std::fill_n(world.grid.data.get(),
                static_cast<std::size_t>(world.grid.w) * world.grid.h,
                static_cast<std::uint8_t>(PIX_GRASS1));
    world.my_team = 0;
    world.rng_.state_ = 4242u;
}

walker* add_living(TestGameWorld& tw, int family, unsigned char team,
                   short x, short y)
{
    walker* w = tw.world().add_ob(Order::Living, family);
    if (w == nullptr)
        return nullptr;
    w->set_team_num(team);
    w->setxy(x, y);
    w->set_act_type(ACT_GUARD);
    w->stats()->set_max_hitpoints(40.0f);
    w->stats()->set_hitpoints(40.0f);
    return w;
}

walker* add_hero(TestGameWorld& tw, int family, unsigned char team,
                 short x, short y)
{
    walker* w = add_living(tw, family, team, x, y);
    if (w != nullptr)
        w->set_owned_myguy(std::make_unique<guy>(family));
    return w;
}

int count_cue(const og::sim::SimEventLog& log, std::string_view text,
              std::int32_t target)
{
    int found = 0;
    for (const og::sim::Event& ev : log.events())
        if (ev.kind == og::sim::EventKind::Notification && ev.text == text &&
            ev.target_player == target)
            found++;
    return found;
}

int count_seat_lines(const og::sim::SimEventLog& log, std::int32_t target)
{
    int found = 0;
    for (const og::sim::Event& ev : log.events())
        if (ev.kind == og::sim::EventKind::Notification &&
            ev.target_player == target)
            found++;
    return found;
}

int count_clang(const og::sim::SimEventLog& log, std::int32_t target)
{
    int found = 0;
    for (const og::sim::Event& ev : log.events())
        if (ev.kind == og::sim::EventKind::PlaySound && ev.a == SOUND_CLANG &&
            ev.target_player == target)
            found++;
    return found;
}

// Seats a walker for player 0 and drives sim_process_player_input.
struct Seat
{
    TestGameWorld& tw;
    walker* control;
    SimInputDebounce debounce{};
    InputState input;

    Seat(TestGameWorld& world, walker* w) : tw(world), control(w)
    {
        input.clear();
        w->set_user(0);
        w->set_act_type(ACT_CONTROL);
    }
    void press(InputAction a)
    {
        input.players[0].pressed[static_cast<int>(a)] = true;
        input.players[0].held[static_cast<int>(a)] = true;
    }
    void hold(InputAction a)
    {
        input.players[0].held[static_cast<int>(a)] = true;
    }
    // One input tick; the keys stay as they are unless `clear` is set.
    SimInputResult run(bool clear = true)
    {
        SimInputResult r = sim_process_player_input(
            input.players[0], control, tw.world(), 0, 0, debounce, &tw.events);
        if (clear)
            input.clear();
        return r;
    }
};

// --- respawn staging (the scripted-mode respawn engine) --------------------

loader& sem_loader()
{
    static loader instance{EntityFactory{}};
    return instance;
}

struct ScriptedWorld : TestGameWorld
{
    ScriptedWorld() : TestGameWorld(500)
    {
        loader* game_loader = &sem_loader();
        world().entity_factory = [game_loader](Order order, std::int32_t family) {
            return game_loader->create_walker_owned(order, family);
        };
        world().entity_configurator =
            [game_loader](walker& entity, Order order,
                          std::int32_t family) -> const PixieData* {
            game_loader->set_walker(&entity, order, family);
            return game_loader->graphics_for(entity.query_order(), entity.family());
        };
        world().entity_derived_stats =
            [game_loader](walker* entity, Order order, std::int32_t family) {
                if (entity != nullptr)
                    game_loader->set_derived_stats(entity, order, family);
            };
        world().type = GameWorld::TYPE_SCRIPTED;
        world().mode.active = true;
        world().mode.init_attempted = true;
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
};

// Anchors for both teams, one living per team, a short timer: the shape the
// respawn engine's own tests use.
struct RespawnArena
{
    ScriptedWorld fx;

    RespawnArena()
    {
        std::vector<walker*> markers;
        for (const auto& [team, x, y] :
             {std::array<int, 3>{0, 128, 128}, std::array<int, 3>{1, 512, 832}})
        {
            walker* m = fx.world().add_ob(Order::Special, FAMILY_RESERVED_TEAM);
            m->setxy(static_cast<short>(x), static_cast<short>(y));
            m->set_team_num(static_cast<unsigned char>(team));
            markers.push_back(m);
        }
        og::sim::respawn_scan_anchors(fx.world());
        for (walker* m : markers)
            m->set_dead(1);
        fx.spawn_living(FAMILY_SOLDIER, 0, 320, 320);
        fx.spawn_living(FAMILY_SOLDIER, 1, 480, 760);
        fx.world().respawn.respawn_ticks = 6;
        fx.world().tick();
    }

    GameWorld& world() { return fx.world(); }
};

} // namespace

// ===========================================================================
// respawn.cpp
// ===========================================================================

// respawn.cpp:819 respawn_schedule_corpse: a queue full of heroes refuses a
// bot's corpse (the post-schedule re-check), and an explicit delay is
// clamped to the entry's 16-bit timer.
TEST(SemRespawn, a_respawn_request_fails_on_a_queue_full_of_heroes_and_clamps_its_delay)
{
    {
        RespawnArena arena;
        walker* bot = arena.fx.spawn_living(FAMILY_ARCHER, 1, 480, 700);
        ASSERT_NE(nullptr, bot);
        auto& queue = arena.world().respawn.respawn_queue;
        for (int i = 0; i < og::sim::kRespawnMaxQueueEntries; ++i)
        {
            og::sim::RespawnEntry filler;
            filler.kind = 0;
            filler.team = 0;
            filler.ticks_left = 5000;
            filler.walker_entity_id = 800000u + static_cast<std::uint32_t>(i);
            queue.push_back(filler);
        }
        bot->set_dead(1);
        EXPECT_FALSE(og::sim::respawn_schedule_corpse(arena.world(), bot, 0))
            << "arm queue full of heroes: the request reports failure";
        EXPECT_FALSE(og::sim::respawn_pending_for(arena.world(), bot))
            << "arm queue full of heroes: nothing is pending for the bot";
        EXPECT_EQ(static_cast<std::size_t>(og::sim::kRespawnMaxQueueEntries),
                  queue.size());
    }
    {
        RespawnArena arena;
        walker* bot = arena.fx.spawn_living(FAMILY_ARCHER, 1, 480, 700);
        ASSERT_NE(nullptr, bot);
        bot->set_dead(1);
        bot->death();
        ASSERT_TRUE(og::sim::respawn_schedule_corpse(arena.world(), bot, 70000))
            << "arm override 70000: the request is accepted";
        ASSERT_FALSE(arena.world().respawn.respawn_queue.empty());
        EXPECT_EQ(bot->entity_id(),
                  arena.world().respawn.respawn_queue.back().walker_entity_id);
        EXPECT_EQ(65535, arena.world().respawn.respawn_queue.back().ticks_left)
            << "arm override 70000: the delay clamps to 65535";
    }
}

// respawn.cpp:879 make_body_probe: a family slot the loader holds no body
// for answers "does not fit" from every probe, and the Lua door answers nil
// without a host error. The known family on the same open ground fits.
// (A living never reaches the arm: set_walker turns a living family with no
// descriptor into a soldier. An empty slot of another order does.)
TEST(SemRespawn, a_body_probe_answers_no_for_a_family_slot_the_loader_cannot_make)
{
    og::test::ScopedPackStoreState pack_store_restore;
    og::test::ScopedHookFailureGuard guard;
    og::test::mount_core_pack();
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    constexpr std::int32_t kEmptySlot = 100;  // a mod slot nothing registered
    ASSERT_GE(kEmptySlot, NUM_FAMILIES);
    ASSERT_LT(kEmptySlot, 128) << "the walker's family byte is a char";

    const std::size_t oblist_before = world.oblist.size();
    const std::size_t weap_before = world.weaplist.size();
    const std::uint32_t rng_before = world.rng_.state_;
    EXPECT_TRUE(og::sim::body_ground_clear(world, Order::Weapon, FAMILY_KNIFE,
                                           400, 400, 0))
        << "control: a known family on open grass passes the ground probe";
    EXPECT_TRUE(og::sim::body_spot_clear(world, Order::Weapon, FAMILY_KNIFE,
                                         400, 400, 0))
        << "control: a known family on open grass fits";
    EXPECT_FALSE(og::sim::body_ground_clear(world, Order::Weapon, kEmptySlot,
                                            400, 400, 0))
        << "arm unknown family: body_ground_clear says no";
    EXPECT_FALSE(og::sim::body_spot_clear(world, Order::Weapon, kEmptySlot,
                                          400, 400, 0))
        << "arm unknown family: body_spot_clear says no";
    short ox = -7;
    short oy = -7;
    EXPECT_FALSE(og::sim::body_spot_near(world, Order::Weapon, kEmptySlot,
                                         400, 400, 0, 16, ox, oy))
        << "arm unknown family: no spot near either";
    EXPECT_EQ(-7, ox) << "the out spot is left alone";
    EXPECT_EQ(-7, oy);
    EXPECT_EQ(oblist_before, world.oblist.size()) << "the probe makes no entity";
    EXPECT_EQ(weap_before, world.weaplist.size());
    EXPECT_EQ(rng_before, world.rng_.state_) << "and draws nothing";

    // The Lua door, asked from a pack script (the soldier's special).
    walker* soldier = add_living(tw, FAMILY_SOLDIER, 0, 200, 200);
    ASSERT_NE(nullptr, soldier);
    og::script::register_pack_script(
        {"test.sem_world_4", "sem_world_4_probe.lua",
         "og.register_hooks('living', 'core:soldier', {\n"
         "  do_special = function(self)\n"
         "    og.log('<probe>')\n"
         "    local x, y = og.body_spot_near('weapon', 100, 400, 400, 0, 16)\n"
         "    og.log('empty_slot', tostring(x) .. ',' .. tostring(y))\n"
         "    local kx, ky = og.body_spot_near('weapon', 0, 400, 400, 0, 16)\n"
         "    og.log('knife', tostring(kx) .. ',' .. tostring(ky))\n"
         "    return true\n"
         "  end,\n"
         "})\n"});
    const auto handled = og::script::hooks::do_special(
        get_family_descriptor(FAMILY_SOLDIER), soldier);
    ASSERT_TRUE(handled.has_value());
    const auto& log = world.scripts().host().log();
    std::string empty_slot = "(not logged)";
    std::string known = "(not logged)";
    for (const std::string& line : log)
    {
        if (line.rfind("empty_slot\t", 0) == 0)
            empty_slot = line.substr(11);
        if (line.rfind("knife\t", 0) == 0)
            known = line.substr(6);
    }
    EXPECT_EQ("nil,nil", empty_slot)
        << "arm Lua og.body_spot_near with an unknown family: nil";
    EXPECT_EQ("400,400", known) << "control: the knife fits at the corner";
    EXPECT_TRUE(world.scripts().host().errors().empty())
        << "and no host error";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// respawn.cpp:969 respawn_scrub_stains_at: a life gem at the corner is
// scrubbed, floor -1 scrubs every floor, and an orphan stain 9+ px away or on
// another named floor survives (no pending respawn holds any of these).
TEST(SemRespawn, a_positional_scrub_takes_gems_and_every_floor_when_none_is_named)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();

    const auto drop = [&](Order order, int family, bool fx, short x, short y,
                          short floor, unsigned char team) -> std::uint32_t {
        walker* w = fx ? world.add_fx_ob(order, family) : world.add_ob(order, family);
        EXPECT_NE(nullptr, w);
        if (w == nullptr)
            return 0;
        w->set_sizex(16);
        w->set_sizey(16);
        w->set_floor(floor);
        w->set_team_num(team);
        w->setxy(x, y);
        return w->entity_id();
    };
    const auto alive = [&](std::uint32_t id) {
        const walker* w = world.find_by_id(id);
        return w != nullptr && !w->dead();
    };

    // Floor 0 scrub: the gem at the corner dies, the stain 9 px away and
    // the stain on floor 1 at the same corner survive; a stain of another
    // team at the corner dies too (no team filter).
    const std::uint32_t gem = drop(Order::Treasure, FAMILY_LIFE_GEM, false, 320, 320, 0, 1);
    const std::uint32_t far = drop(Order::Treasure, FAMILY_STAIN, true, 329, 320, 0, 1);
    const std::uint32_t upstairs = drop(Order::Treasure, FAMILY_STAIN, true, 320, 320, 1, 1);
    const std::uint32_t other_team = drop(Order::Treasure, FAMILY_STAIN, true, 320, 320, 0, 3);
    ASSERT_TRUE(alive(gem) && alive(far) && alive(upstairs) && alive(other_team));
    og::sim::respawn_scrub_stains_at(world, 320, 320, 0);
    EXPECT_FALSE(alive(gem)) << "arm life gem at the corner: dead";
    EXPECT_FALSE(alive(other_team)) << "a stain of any team at the corner: dead";
    EXPECT_TRUE(alive(far)) << "arm drop 9 px away: survives";
    EXPECT_TRUE(alive(upstairs)) << "arm another floor (floor 0 named): survives";

    // Floor -1: the corner's drops on every floor go.
    const std::uint32_t ground = drop(Order::Treasure, FAMILY_STAIN, true, 320, 320, 0, 1);
    ASSERT_TRUE(alive(ground));
    og::sim::respawn_scrub_stains_at(world, 320, 320, -1);
    EXPECT_FALSE(alive(ground)) << "arm floor -1: the floor-0 stain is scrubbed";
    EXPECT_FALSE(alive(upstairs)) << "arm floor -1: the floor-1 stain is scrubbed";
    EXPECT_TRUE(alive(far)) << "the 9 px stain still survives";
}

// ===========================================================================
// scenario_strip.cpp
// ===========================================================================

namespace {

struct StripWorld : TestGameWorld
{
    std::vector<std::unique_ptr<guy>> roster;

    walker* living(int family, int team, short x, short y)
    {
        walker* w = world().add_ob(Order::Living, family);
        w->setxy(x, y);
        w->set_team_num(static_cast<unsigned char>(team));
        return w;
    }
    walker* hero(int family, int team, short x, short y)
    {
        walker* w = living(family, team, x, y);
        roster.push_back(std::make_unique<guy>(family));
        w->myguy = roster.back().get();
        return w;
    }
    int count_alive(const GameWorld::EntityList& list, Order order) const
    {
        int n = 0;
        for (const auto& uptr : list)
            if (uptr != nullptr && !uptr->dead() && uptr->query_order() == order)
                n++;
        return n;
    }
};

} // namespace

// scenario_strip.cpp:20 is_authored_troop: weapons, effects and treasure
// are never troops; only the authored living goes.
TEST(SemScenarioStrip, the_troop_strip_leaves_weapons_effects_and_treasure_alone)
{
    StripWorld sw;
    open_field(sw);
    GameWorld& world = sw.world();
    sw.hero(FAMILY_SOLDIER, 0, 160, 160);
    sw.living(FAMILY_ORC, 1, 320, 320);
    walker* food = world.add_ob(Order::Treasure, FAMILY_DRUMSTICK);
    ASSERT_NE(nullptr, food);
    food->setxy(400, 400);
    walker* knife = world.add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, knife);
    knife->setxy(420, 400);
    walker* flash = world.add_fx_ob(Order::FX, FAMILY_FLASH);
    ASSERT_NE(nullptr, flash);
    flash->setxy(440, 400);

    const int treasure_before = sw.count_alive(world.oblist, Order::Treasure);
    const std::size_t weap_before = world.weaplist.size();
    const std::size_t fx_before = world.fxlist.size();
    ASSERT_EQ(1, treasure_before);

    EXPECT_EQ(1, og::sim::classic_strip_authored_troops(world))
        << "only the authored orc is a troop";
    EXPECT_EQ(treasure_before, sw.count_alive(world.oblist, Order::Treasure))
        << "arm non-living order: the treasure beside the troops is kept";
    EXPECT_EQ(weap_before, world.weaplist.size())
        << "arm non-living order: the weapon is kept";
    EXPECT_EQ(fx_before, world.fxlist.size())
        << "arm non-living order: the effect is kept";
    EXPECT_EQ(1, sw.count_alive(world.oblist, Order::Living)) << "the hero stays";
}

// scenario_strip.cpp:79 classic_strip_authored_troops: a stripped troop
// leaves the collision table as well as the object list, and no corpse.
TEST(SemScenarioStrip, a_stripped_troop_leaves_the_collision_table_and_no_corpse)
{
    StripWorld sw;
    open_field(sw);
    GameWorld& world = sw.world();
    walker* hero = sw.hero(FAMILY_SOLDIER, 0, 160, 160);
    walker* orc = sw.living(FAMILY_ORC, 1, 320, 320);
    ASSERT_TRUE(hero && orc);
    ASSERT_EQ(1u, world.myobmap->walker_to_pos.count(orc))
        << "precondition: the orc is in the collision table";
    const auto orc_key = reinterpret_cast<std::uintptr_t>(orc);
    const std::size_t dead_before = world.dead_list.size();

    ASSERT_EQ(1, og::sim::classic_strip_authored_troops(world));

    bool still_filed = false;
    for (const auto& [ptr, cells] : world.myobmap->walker_to_pos)
        if (reinterpret_cast<std::uintptr_t>(ptr) == orc_key)
            still_filed = true;
    EXPECT_FALSE(still_filed) << "arm strip: the troop left the collision table";
    bool in_cell = false;
    for (walker* w : world.myobmap->obmap_get_list(320, 320, 0))
        if (reinterpret_cast<std::uintptr_t>(w) == orc_key)
            in_cell = true;
    EXPECT_FALSE(in_cell) << "arm strip: its cell no longer lists it";
    EXPECT_EQ(1u, world.myobmap->walker_to_pos.count(hero)) << "the hero stays filed";
    EXPECT_EQ(dead_before, world.dead_list.size()) << "arm strip: no corpse";
    EXPECT_EQ(1, sw.count_alive(world.oblist, Order::Living));
}

// ===========================================================================
// sim_control_policy.cpp
// ===========================================================================

// sim_control_policy.cpp:344 first_eligible_follow_target_id (through
// next_follow_target_id with no current target): with only anonymous troops
// the camera starts on the first visible one; with nobody visible, nothing.
TEST(SemControlPolicy, a_spectator_with_no_hero_to_watch_starts_on_the_first_visible_troop)
{
    {
        TestGameWorld tw;
        open_field(tw);
        walker* dead = add_living(tw, FAMILY_SOLDIER, 0, 64, 64);
        walker* first = add_living(tw, FAMILY_ORC, 1, 96, 64);
        walker* second = add_living(tw, FAMILY_SOLDIER, 0, 128, 64);
        ASSERT_TRUE(dead && first && second);
        dead->set_dead(1);
        EXPECT_EQ(first->entity_id(),
                  og::sim::next_follow_target_id(tw.world(), nullptr, false))
            << "arm only anonymous troops: the first visible troop";
    }
    {
        TestGameWorld tw;
        open_field(tw);
        walker* dead = add_living(tw, FAMILY_SOLDIER, 0, 64, 64);
        walker* dormant = add_living(tw, FAMILY_ORC, 1, 96, 64);
        ASSERT_TRUE(dead && dormant);
        dead->set_dead(1);
        dormant->set_dormant(true);
        EXPECT_EQ(0u, og::sim::next_follow_target_id(tw.world(), nullptr, false))
            << "arm no visible living: no target";
    }
}

// sim_control_policy.cpp:361 default_follow_target_id: the lowest player
// index with a live controlled walker wins over world order; with none
// alive, the first eligible walker in world order.
TEST(SemControlPolicy, a_seatless_view_follows_the_lowest_player_with_a_live_walker)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    walker* a = add_hero(tw, FAMILY_SOLDIER, 0, 64, 64);
    walker* b = add_hero(tw, FAMILY_SOLDIER, 0, 96, 64);
    ASSERT_TRUE(a && b);
    const std::uint32_t ida = a->entity_id();
    const std::uint32_t idb = b->entity_id();

    {
        const std::array<std::uint32_t, 3> ids{0, idb, ida};
        EXPECT_EQ(idb, og::sim::default_follow_target_id(world, ids))
            << "arm some controlled id alive: player 1's B, not world-first A";
    }
    b->set_dead(1);
    {
        const std::array<std::uint32_t, 3> ids{0, idb, ida};
        EXPECT_EQ(ida, og::sim::default_follow_target_id(world, ids))
            << "arm some controlled id alive: B dead, player 2's A";
    }
    b->set_dead(0);
    {
        const std::array<std::uint32_t, 2> ids{0, 0};
        EXPECT_EQ(ida, og::sim::default_follow_target_id(world, ids))
            << "arm all 0: the first eligible walker in world order";
    }
    walker* troop = add_living(tw, FAMILY_ORC, 1, 128, 64);
    ASSERT_NE(nullptr, troop);
    a->set_dead(1);
    b->set_dead(1);
    {
        const std::array<std::uint32_t, 2> ids{idb, ida};
        EXPECT_EQ(troop->entity_id(), og::sim::default_follow_target_id(world, ids))
            << "arm all dead: the first eligible walker (the troop)";
    }
}

// sim_control_policy.cpp:376 next_follow_target_id: with nobody else to
// cycle to, the camera keeps watching the current walker while it is
// visible, and drops to no target once it is gone.
TEST(SemControlPolicy, switch_character_with_nobody_else_keeps_the_visible_target_or_drops_it)
{
    TestGameWorld tw;
    open_field(tw);
    walker* h = add_hero(tw, FAMILY_SOLDIER, 0, 64, 64);
    ASSERT_NE(nullptr, h);
    EXPECT_EQ(h->entity_id(), og::sim::next_follow_target_id(tw.world(), h, false))
        << "arm nobody else, current visible: keeps it (forward)";
    EXPECT_EQ(h->entity_id(), og::sim::next_follow_target_id(tw.world(), h, true))
        << "arm nobody else, current visible: keeps it (reverse)";
    h->set_hidden(true);
    EXPECT_EQ(0u, og::sim::next_follow_target_id(tw.world(), h, false))
        << "arm nobody else, current gone (hidden): no target";
    h->set_hidden(false);
    h->set_dead(1);
    EXPECT_EQ(0u, og::sim::next_follow_target_id(tw.world(), h, true))
        << "arm nobody else, current gone (dead): no target";
}

// ===========================================================================
// sim_input_handler.cpp
// ===========================================================================

// sim_input_handler.cpp:41 special_failure_message and :64
// emit_throttled_cue: a cast that fails because the seat's body is not a
// living says nothing (no line, no clang, the throttle stays cold); the same
// seat failing for an answerable reason (no special there) is told.
TEST(SemSimInput, a_special_that_fails_on_a_non_living_body_stays_silent)
{
    TestGameWorld tw;
    open_field(tw);
    walker* knife = tw.world().add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, knife);
    knife->setxy(200, 200);
    knife->set_team_num(0);
    knife->set_current_special(1);
    knife->stats()->set_special_cost(1, 0);
    knife->stats()->set_max_magicpoints(100.0f);
    knife->stats()->set_magicpoints(100.0f);
    {
        walker::SpecialFailure why = walker::SpecialFailure::None;
        std::string reason;
        ASSERT_FALSE(knife->special(&why, &reason));
        ASSERT_EQ(walker::SpecialFailure::NotLiving, why)
            << "precondition: the body answers NotLiving";
    }
    Seat seat(tw, knife);
    seat.press(InputAction::Special);
    seat.run();
    EXPECT_EQ(knife, seat.control);
    EXPECT_EQ(0, count_seat_lines(tw.events, 0))
        << "arm NotLiving: no line for the seat";
    EXPECT_EQ(0, count_clang(tw.events, 0)) << "arm NotLiving: no clang";
    EXPECT_EQ(0, seat.debounce.cue_delay)
        << "arm no message: the throttle is not re-armed by a silent failure";

    // Control: the same body with the slot priced out answers Unavailable,
    // which the seat hears.
    knife->stats()->set_special_cost(1, kSpecialCostDisabled);
    seat.press(InputAction::Special);
    seat.run();
    EXPECT_EQ(1, count_cue(tw.events, "NO SPECIAL AVAILABLE", 0))
        << "control: an answerable failure is voiced";
    EXPECT_EQ(1, count_clang(tw.events, 0));
    EXPECT_EQ(kSimCueThrottleTicks, seat.debounce.cue_delay);
}

// sim_input_handler.cpp:182 sim_cycle_next_character: a current body that
// is not in the list yields nothing in either direction, even with eligible
// bodies in the list.
TEST(SemSimInput, switch_character_from_a_body_outside_the_list_finds_nobody)
{
    TestGameWorld tw;
    open_field(tw);
    walker* a = add_living(tw, FAMILY_SOLDIER, 0, 64, 64);
    walker* b = add_living(tw, FAMILY_SOLDIER, 0, 96, 64);
    ASSERT_TRUE(a && b);
    walker stranger;
    const auto any = [](const walker*) { return true; };
    EXPECT_EQ(nullptr,
              sim_cycle_next_character(tw.world().oblist, &stranger, false, any))
        << "arm current missing (forward): nullptr";
    EXPECT_EQ(nullptr,
              sim_cycle_next_character(tw.world().oblist, &stranger, true, any))
        << "arm current missing (reverse): nullptr";
    EXPECT_EQ(b, sim_cycle_next_character(tw.world().oblist, a, false, any))
        << "control: from a listed body the cycle advances";
}

// sim_input_handler.cpp:241 sim_advance_current_special: stepping past the
// last slot wraps to slot 1 (the old game: current_special > NUM_SPECIALS-1),
// and so does a negative current slot whose next step is still negative.
TEST(SemSimInput, switch_special_past_the_last_slot_wraps_to_slot_one)
{
    TestGameWorld tw;
    open_field(tw);
    walker* soldier = add_living(tw, FAMILY_SOLDIER, 0, 64, 64);
    ASSERT_NE(nullptr, soldier);
    soldier->stats()->set_level(20);
    for (const short ns : {short{0}, short{1}})
    {
        tw.world().new_specials = ns;
        soldier->set_current_special(static_cast<char>(NUM_SPECIALS - 1));
        sim_advance_current_special(tw.world(), *soldier);
        EXPECT_EQ(1, soldier->current_special())
            << "arm past the end: slot " << NUM_SPECIALS << " wraps to 1, new_specials=" << ns;
    }
    // The old game reads special_name[family][-1] here, which is the row
    // before (FAMILY_SOLDIER's slot 5, "NONE"), so an elf wraps there too.
    walker* elf = add_living(tw, FAMILY_ELF, 0, 96, 64);
    ASSERT_NE(nullptr, elf);
    elf->stats()->set_level(20);
    tw.world().new_specials = 0;
    elf->set_current_special(static_cast<char>(-2));
    sim_advance_current_special(tw.world(), *elf);
    EXPECT_EQ(1, elf->current_special()) << "arm negative: wraps to 1";
}

// sim_input_handler.cpp:251 sim_switch_control: switching away from a body
// another seat holds leaves that seat's claim alone.
TEST(SemSimInput, switching_away_from_another_seats_body_leaves_it_seated)
{
    TestGameWorld tw;
    open_field(tw);
    walker* held = add_living(tw, FAMILY_SOLDIER, 0, 64, 64);
    walker* ally = add_living(tw, FAMILY_SOLDIER, 0, 96, 64);
    ASSERT_TRUE(held && ally);
    held->set_act_type(ACT_CONTROL);
    held->set_user(1);
    walker* next = sim_switch_control(tw.world(), *held, held, 0, 0, false);
    EXPECT_EQ(ally, next) << "the cycle still finds the ally";
    EXPECT_EQ(1, held->user()) << "arm not ours: still seat 1's";
    EXPECT_EQ(ACT_CONTROL, held->act_type()) << "arm not ours: act type untouched";
}

// sim_input_handler.cpp:301: a cold throttle stays at 0 tick after tick.
TEST(SemSimInput, a_cold_cue_throttle_stays_cold)
{
    TestGameWorld tw;
    open_field(tw);
    walker* hero = add_hero(tw, FAMILY_SOLDIER, 0, 200, 200);
    ASSERT_NE(nullptr, hero);
    Seat seat(tw, hero);
    for (int tick = 0; tick < 3; ++tick)
    {
        seat.run();
        EXPECT_EQ(0, seat.debounce.cue_delay) << "arm cold: tick " << tick;
    }
}

// sim_input_handler.cpp:370: Switch Character held through a second tick
// switches once.
TEST(SemSimInput, switch_character_held_down_switches_once)
{
    TestGameWorld tw;
    open_field(tw);
    walker* a = add_living(tw, FAMILY_SOLDIER, 0, 64, 64);
    walker* b = add_living(tw, FAMILY_SOLDIER, 0, 96, 64);
    walker* c = add_living(tw, FAMILY_SOLDIER, 0, 128, 64);
    ASSERT_TRUE(a && b && c);
    Seat seat(tw, a);
    seat.press(InputAction::SwitchChar);
    seat.run(false);
    ASSERT_EQ(b, seat.control) << "the press switches to B";
    ASSERT_EQ(1, seat.debounce.changedchar);
    seat.run(false);
    EXPECT_EQ(b, seat.control) << "arm held through: still B";
    EXPECT_EQ(0, b->user()) << "arm held through: B stays seated";
    EXPECT_EQ(-1, c->user()) << "arm held through: C never claimed";
    EXPECT_EQ(1, seat.debounce.changedchar);
}

// sim_input_handler.cpp:428 (New Specials): a hidden hero holding Special
// casts on the held repeat too.
TEST(SemSimInput, a_hidden_hero_holding_special_casts_on_the_repeat)
{
    og::test::mount_core_pack();
    for (const short ns : {short{0}, short{1}})
    {
        TestGameWorld tw;
        open_field(tw);
        tw.world().new_specials = ns;
        walker* orc = add_living(tw, FAMILY_ORC, 0, 96, 96);
        ASSERT_NE(nullptr, orc);
        orc->stats()->set_max_magicpoints(100.0f);
        orc->stats()->set_magicpoints(0.0f);
        orc->set_current_special(1);
        orc->set_busy(0.0f);
        Seat seat(tw, orc);
        orc->set_hidden(true);
        seat.hold(InputAction::Special);
        seat.run();
        EXPECT_FLOAT_EQ(2.0f, orc->busy())
            << "arm hidden, hold: HOWL dispatched by the held arm, new_specials=" << ns;
        EXPECT_EQ(orc, seat.control);
    }
}

// sim_input_handler.cpp:460 Yell: recruits drop their foe and take a forced
// 100-step follow; a led, seated, foreign or non-living object is untouched;
// and (classic, old view.cpp:626-629) a dead, dormant or hidden leaderless
// teammate is recruited too.
TEST(SemSimInput, yell_recruits_unled_teammates_and_classic_recruits_the_dead_dormant_and_hidden)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    walker* hero = add_hero(tw, FAMILY_SOLDIER, 0, 200, 200);
    walker* orc = add_living(tw, FAMILY_ORC, 1, 600, 600);
    walker* recruit = add_living(tw, FAMILY_SOLDIER, 0, 232, 200);
    walker* other_leader = add_hero(tw, FAMILY_SOLDIER, 0, 264, 200);
    walker* led = add_living(tw, FAMILY_SOLDIER, 0, 296, 200);
    walker* seated = add_living(tw, FAMILY_SOLDIER, 0, 328, 200);
    walker* corpse = add_living(tw, FAMILY_SOLDIER, 0, 360, 200);
    walker* sleeper = add_living(tw, FAMILY_SOLDIER, 0, 392, 200);
    walker* ghost = add_living(tw, FAMILY_SOLDIER, 0, 424, 200);
    walker* food = world.add_ob(Order::Treasure, FAMILY_DRUMSTICK);
    ASSERT_TRUE(hero && orc && recruit && other_leader && led && seated &&
                corpse && sleeper && ghost && food);
    food->setxy(456, 200);
    food->set_team_num(0);
    // The loader gives treasure ACT_CONTROL, which the act-type term would
    // also skip; an act type of its own leaves only the order term.
    food->set_act_type(ACT_GUARD);
    recruit->set_foe(orc);
    led->set_leader(other_leader);
    led->set_foe(orc);
    seated->set_act_type(ACT_CONTROL);
    seated->set_user(1);
    corpse->set_dead(1);
    sleeper->set_dormant(true);
    ghost->set_hidden(true);
    for (walker* w : {corpse, sleeper, ghost})
        w->set_foe(orc);

    Seat seat(tw, hero);
    seat.press(InputAction::Yell);
    seat.run();
    ASSERT_EQ(30, hero->yo_delay()) << "the yell ran";

    const auto expect_follow = [&](walker* w, const char* arm) {
        EXPECT_EQ(hero, w->leader()) << arm << ": leader is the hero";
        EXPECT_EQ(nullptr, w->foe()) << arm << ": foe dropped";
        ASSERT_EQ(1u, w->stats()->commands.size()) << arm << ": one command";
        const command& c = w->stats()->commands.front();
        EXPECT_EQ(COMMAND_FOLLOW, c.commandtype) << arm;
        EXPECT_EQ(100, c.commandcount) << arm;
        EXPECT_TRUE(c.forced) << arm << ": forced onto the queue";
    };
    expect_follow(recruit, "arm yell: foe dropped and forced follow");
    expect_follow(corpse, "arm dead teammate (classic)");
    expect_follow(sleeper, "arm dormant teammate (classic)");
    expect_follow(ghost, "arm hidden teammate (classic)");

    EXPECT_EQ(other_leader, led->leader()) << "arm already led: leader kept";
    EXPECT_EQ(orc, led->foe()) << "arm already led: foe kept";
    EXPECT_TRUE(led->stats()->commands.empty()) << "arm already led: no command";
    EXPECT_EQ(nullptr, seated->leader()) << "arm seated: untouched";
    EXPECT_TRUE(seated->stats()->commands.empty()) << "arm seated: no command";
    EXPECT_EQ(nullptr, orc->leader()) << "arm other team: untouched";
    EXPECT_TRUE(orc->stats()->commands.empty()) << "arm other team: no command";
    EXPECT_EQ(nullptr, food->leader()) << "arm order not Living: untouched";
    EXPECT_TRUE(food->stats()->commands.empty()) << "arm order not Living: no command";
}

// sim_input_handler.cpp:492 Shift+Yell summon: every friendly teammate,
// the hero itself included (classic: the old game's summon loop has no
// order or self filter either), takes the hero as leader and drops its foe.
TEST(SemSimInput, summon_defense_sets_the_leader_and_drops_the_foe_classic_including_the_hero)
{
    TestGameWorld tw;
    open_field(tw);
    walker* hero = add_hero(tw, FAMILY_SOLDIER, 0, 200, 200);
    walker* orc = add_living(tw, FAMILY_ORC, 1, 600, 600);
    walker* a1 = add_living(tw, FAMILY_SOLDIER, 0, 232, 200);
    walker* a2 = add_living(tw, FAMILY_ARCHER, 0, 264, 200);
    ASSERT_TRUE(hero && orc && a1 && a2);
    a1->set_foe(orc);
    a2->set_foe(orc);
    hero->set_action(0);
    Seat seat(tw, hero);
    seat.press(InputAction::Yell);
    seat.hold(InputAction::Shift);
    SimInputResult r = seat.run();
    ASSERT_EQ("SUMMONING DEFENSE!", r.notify_text);
    for (walker* w : {a1, a2})
    {
        EXPECT_EQ(hero, w->leader()) << "arm hero action 0: leader set";
        EXPECT_EQ(nullptr, w->foe()) << "arm hero action 0: foe dropped";
        EXPECT_EQ(ACTION_FOLLOW, w->action());
    }
    EXPECT_EQ(hero, hero->leader()) << "classic: the hero leads itself";
    EXPECT_EQ(ACTION_FOLLOW, hero->action());
    EXPECT_EQ(nullptr, orc->leader()) << "the enemy is not summoned";
    EXPECT_EQ(0, orc->action());
}

// sim_input_handler.cpp:544 (old view.cpp:983): a seated hero mid-animation
// advances its animation in the input tick; a walking one does not.
TEST(SemSimInput, a_seated_hero_mid_attack_animates_in_the_input_tick)
{
    TestGameWorld tw;
    open_field(tw);
    walker* attacker = add_hero(tw, FAMILY_SOLDIER, 0, 200, 200);
    walker* walking = add_hero(tw, FAMILY_SOLDIER, 0, 300, 200);
    ASSERT_TRUE(attacker && walking);
    ASSERT_FALSE(walking->stats()->query_bit_flags(BIT_ANIMATE));
    for (walker* w : {attacker, walking})
    {
        w->set_curdir(static_cast<signed char>(FACE_UP));
        w->set_cycle(0);
    }
    attacker->set_ani_type(ANI_ATTACK);
    walking->set_ani_type(ANI_WALK);
    const auto walk_frame = walking->frame();

    Seat s1(tw, attacker);
    s1.run();
    EXPECT_EQ(ANI_ATTACK, attacker->ani_type());
    EXPECT_EQ(1, attacker->cycle()) << "arm animating: animate() ran once";

    Seat s2(tw, walking);
    s2.run();
    EXPECT_EQ(0, walking->cycle()) << "arm walking: no animation step";
    EXPECT_EQ(walk_frame, walking->frame()) << "arm walking: frame unchanged";
}

// sim_input_handler.cpp:548: a seat that kept its body keeps its queued
// commands.
TEST(SemSimInput, a_seat_that_keeps_its_body_keeps_its_queued_commands)
{
    TestGameWorld tw;
    open_field(tw);
    walker* hero = add_hero(tw, FAMILY_SOLDIER, 0, 200, 200);
    ASSERT_NE(nullptr, hero);
    Seat seat(tw, hero);
    hero->stats()->add_command(COMMAND_WALK, 5, 1, 0);
    seat.run();
    EXPECT_EQ(hero, seat.control);
    ASSERT_EQ(1u, hero->stats()->commands.size()) << "arm same: commands kept";
    EXPECT_EQ(COMMAND_WALK, hero->stats()->commands.front().commandtype);
    EXPECT_FALSE(hero->stats()->commands.front().forced);
}

// sim_input_handler.cpp:569: a hero with queued work does not cast on a
// Special press; the same hero with an empty queue does.
TEST(SemSimInput, a_hero_carrying_out_a_queued_action_does_not_cast)
{
    og::test::mount_core_pack();
    const auto stage = [](TestGameWorld& tw, bool queued) {
        open_field(tw);
        walker* orc = add_hero(tw, FAMILY_ORC, 0, 96, 96);
        orc->stats()->set_max_magicpoints(500.0f);
        orc->stats()->set_magicpoints(500.0f);
        orc->set_current_special(1);
        orc->set_busy(0.0f);
        if (queued)
            orc->stats()->add_command(COMMAND_WALK, 5, 1, 0);
        return orc;
    };
    {
        TestGameWorld tw;
        walker* orc = stage(tw, true);
        Seat seat(tw, orc);
        seat.press(InputAction::Special);
        seat.run();
        EXPECT_FLOAT_EQ(500.0f, orc->stats()->magicpoints())
            << "arm queued work: no cast (MP unchanged)";
        EXPECT_FLOAT_EQ(0.0f, orc->busy()) << "arm queued work: no cast (not busy)";
        EXPECT_EQ(0, count_seat_lines(tw.events, 0)) << "and no cue";
    }
    {
        TestGameWorld tw;
        walker* orc = stage(tw, false);
        Seat seat(tw, orc);
        seat.press(InputAction::Special);
        seat.run();
        EXPECT_FLOAT_EQ(2.0f, orc->busy()) << "control: the empty queue casts HOWL";
        EXPECT_LT(orc->stats()->magicpoints(), 500.0f) << "control: HOWL is paid for";
    }
}

// sim_input_handler.cpp:597: a caster friendly to my_team is not told
// SPECIALS FROZEN during an enemy freeze, even with queued work.
TEST(SemSimInput, a_friendly_caster_during_an_enemy_freeze_is_not_told_specials_frozen)
{
    og::test::mount_core_pack();
    TestGameWorld tw;
    open_field(tw);
    walker* soldier = add_hero(tw, FAMILY_SOLDIER, 0, 96, 96);
    ASSERT_NE(nullptr, soldier);
    soldier->set_current_special(1);  // CHARGE queues a RUSH
    soldier->stats()->set_max_magicpoints(500.0f);
    soldier->stats()->set_magicpoints(500.0f);
    tw.world().my_team = 0;
    tw.world().enemy_freeze = 100;
    Seat seat(tw, soldier);
    seat.press(InputAction::Special);
    seat.run();
    ASSERT_EQ(1u, soldier->stats()->commands.size()) << "the charge queued work";
    EXPECT_EQ(0, count_cue(tw.events, "SPECIALS FROZEN", 0))
        << "arm caster friendly to my_team: no cue";
    EXPECT_EQ(0, count_seat_lines(tw.events, 0));
    EXPECT_EQ(0, count_clang(tw.events, 0));
}

// sim_input_handler.cpp:615: with Fire neither pressed nor held, a walking
// hero does not attack.
TEST(SemSimInput, no_fire_key_means_no_attack)
{
    TestGameWorld tw;
    open_field(tw);
    walker* hero = add_hero(tw, FAMILY_SOLDIER, 0, 200, 200);
    ASSERT_NE(nullptr, hero);
    hero->set_curdir(static_cast<signed char>(FACE_RIGHT));
    hero->set_enddir(static_cast<char>(FACE_RIGHT));
    hero->set_ani_type(ANI_WALK);
    hero->set_busy(0.0f);
    Seat seat(tw, hero);
    seat.hold(InputAction::MoveRight);
    seat.run();
    EXPECT_FLOAT_EQ(hero->stepsize(), hero->lastx()) << "the tick walked";
    EXPECT_EQ(ANI_WALK, hero->ani_type()) << "arm neither: no attack row";
    EXPECT_FLOAT_EQ(0.0f, hero->busy()) << "arm neither: not busy";
    EXPECT_EQ(0u, tw.world().weaplist.size()) << "arm neither: nothing thrown";
}

// sim_input_handler.cpp:641: an idle hero without BIT_ANIMATE does nothing
// in the input tick.
TEST(SemSimInput, an_idle_hero_that_does_not_always_animate_stands_still)
{
    TestGameWorld tw;
    open_field(tw);
    walker* hero = add_hero(tw, FAMILY_SOLDIER, 0, 200, 200);
    ASSERT_NE(nullptr, hero);
    ASSERT_FALSE(hero->stats()->query_bit_flags(BIT_ANIMATE));
    hero->set_curdir(static_cast<signed char>(FACE_RIGHT));
    hero->set_ani_type(ANI_WALK);
    hero->set_cycle(0);
    hero->set_lastx(0.0f);
    hero->set_lasty(0.0f);
    const auto frame_before = hero->frame();
    Seat seat(tw, hero);
    seat.run();
    EXPECT_EQ(0, hero->cycle()) << "arm idle: cycle unchanged";
    EXPECT_EQ(frame_before, hero->frame()) << "arm idle: frame unchanged";
    EXPECT_EQ(200, hero->xpos()) << "arm idle: no step";
    EXPECT_FLOAT_EQ(0.0f, hero->lastx()) << "arm idle: no heading";
}

// ===========================================================================
// specials_view.cpp
// ===========================================================================

// specials_view.cpp:51: a slot out of range or a missing descriptor has no
// name, under either New Specials value.
TEST(SemSpecialsView, a_bad_slot_or_no_descriptor_has_no_special_name)
{
    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, fd);
    for (const short ns : {short{0}, short{1}})
    {
        EXPECT_STREQ(kSpecialNameNone, og::sim::special_name(fd, FD_NUM_SPECIALS, ns))
            << "arm bad slot (past the end), ns=" << ns;
        EXPECT_STREQ(kSpecialNameNone, og::sim::special_name(fd, -1, ns))
            << "arm bad slot (negative), ns=" << ns;
        EXPECT_STREQ(kSpecialNameNone, og::sim::special_name(nullptr, 1, ns))
            << "arm null descriptor, ns=" << ns;
        EXPECT_STREQ(kSpecialNameNone, og::sim::alternate_name(fd, FD_NUM_SPECIALS, ns))
            << "arm bad slot (past the end), alternate, ns=" << ns;
        EXPECT_STREQ(kSpecialNameNone, og::sim::alternate_name(fd, -1, ns))
            << "arm bad slot (negative), alternate, ns=" << ns;
        EXPECT_STREQ(kSpecialNameNone, og::sim::alternate_name(nullptr, 1, ns))
            << "arm null descriptor, alternate, ns=" << ns;
        EXPECT_STREQ("CHARGE", og::sim::special_name(fd, 1, ns))
            << "control: an in-range slot is named";
    }
}
