/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch world-3: the rules the review found in mode_tick.cpp,
// placement.cpp, possession.cpp and respawn.cpp (the declared winner, the
// swap verb, possession's refusals, releases and death hand-offs, the seat
// redirect, the REASSEMBLE ward, and the respawn engine's queue, placement,
// stain scrub, control keep-alive, wipe suppression and win flush). Each
// test is named after the rule it asserts, and each failure message names
// the arm.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/kit_state.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/placement.h>
#include <openglad/gameplay/possession.h>
#include <openglad/gameplay/respawn/respawn_state.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/packs.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

loader& sem_world_3_loader()
{
    static loader instance{EntityFactory{}};
    return instance;
}

// Wires the real loader into a world, so add_ob builds walkers with their
// family's graphics and derived stats (the respawn engine's fresh bots).
void wire_loader(GameWorld& world)
{
    loader* game_loader = &sem_world_3_loader();
    world.entity_factory = [game_loader](Order order, std::int32_t family) {
        return game_loader->create_walker_owned(order, family);
    };
    world.entity_configurator =
        [game_loader](walker& entity, Order order,
                      std::int32_t family) -> const PixieData* {
        game_loader->set_walker(&entity, order, family);
        return game_loader->graphics_for(entity.query_order(), entity.family());
    };
    world.entity_derived_stats =
        [game_loader](walker* entity, Order order, std::int32_t family) {
            if (entity != nullptr)
                game_loader->set_derived_stats(entity, order, family);
        };
}

walker* spawn_living_in(GameWorld& world, int family, int team, int x, int y)
{
    walker* w = world.add_ob(Order::Living, family);
    if (w == nullptr)
        return nullptr;
    w->setxy(static_cast<short>(x), static_cast<short>(y));
    w->set_team_num(static_cast<unsigned char>(team));
    w->set_real_team_num(255);
    w->set_act_type(ACT_CONTROL);  // stationary without input
    return w;
}

void give_guy(walker* w, int guy_id)
{
    w->set_owned_myguy(std::make_unique<guy>(w->family()));
    w->myguy->id = guy_id;
}

void kill(walker* w)
{
    w->set_dead(1);
    w->death();
}

// A scripted (TYPE_SCRIPTED) world whose mode is hand-armed with no Lua at
// all: the respawn engine's scripted arms without a VM.
struct ScriptedWorld : TestGameWorld
{
    explicit ScriptedWorld(int level_id = 500)
        : TestGameWorld(level_id)
    {
        wire_loader(world());
        world().type = GameWorld::TYPE_SCRIPTED;
        world().mode.active = true;
        world().mode.init_attempted = true;
    }

    walker* spawn_anchor(int team, int x, int y)
    {
        walker* marker = world().add_ob(Order::Special, FAMILY_RESERVED_TEAM);
        if (marker == nullptr)
            return nullptr;
        marker->setxy(static_cast<short>(x), static_cast<short>(y));
        marker->set_team_num(static_cast<unsigned char>(team));
        return marker;
    }

    walker* spawn_living(int family, int team, int x, int y)
    {
        return spawn_living_in(world(), family, team, x, y);
    }

    void tick(int count = 1)
    {
        for (int i = 0; i < count; ++i)
            world().tick();
    }
};

// The respawn arena: anchors scanned then consumed the way the level
// bootstrap consumes them, a stationary soldier per team, a 6-tick timer.
struct RespawnArena
{
    ScriptedWorld fx;
    walker* runner = nullptr;  // team 0 at (320,320)
    walker* enemy = nullptr;   // team 1 at (480,760)

    RespawnArena()
    {
        walker* a0 = fx.spawn_anchor(0, 128, 128);
        walker* a1 = fx.spawn_anchor(1, 512, 832);
        og::sim::respawn_scan_anchors(fx.world());
        for (walker* marker : {a0, a1})
            if (marker != nullptr)
                marker->set_dead(1);
        runner = fx.spawn_living(FAMILY_SOLDIER, 0, 320, 320);
        enemy = fx.spawn_living(FAMILY_SOLDIER, 1, 480, 760);
        fx.world().respawn.respawn_ticks = 6;
        fx.tick();
    }

    GameWorld& world() { return fx.world(); }

    bool schedule(walker* w, int ticks_override = 0)
    {
        return og::sim::respawn_schedule_corpse(fx.world(), w, ticks_override);
    }
};

// A scripted world driven by a registered level script (level 42), so the
// tests can see whether on_respawn ran.
struct ModeWorld : TestGameWorld
{
    og::test::ScopedPackStoreState pack_store_restore;

    explicit ModeWorld(int level_id = 42)
        : TestGameWorld(level_id)
    {
        init_all_registries();
        og::script::clear_pack_scripts();
        wire_loader(world());
        world().type = GameWorld::TYPE_SCRIPTED;
    }

    ~ModeWorld() { og::script::clear_pack_scripts(); }

    void register_script(const std::string& body)
    {
        og::script::register_pack_script({"test.mode", "mode.lua", body});
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
        return spawn_living_in(world(), family, team, x, y);
    }

    void tick(int count = 1)
    {
        for (int i = 0; i < count; ++i)
            world().tick();
    }
};

// A mode whose only Lua is an on_respawn logger: every dispatch logs
// 'respawn_called' before it reads the handle.
constexpr const char* kRespawnLoggerScript =
    "og.register_level_hooks(42, {\n"
    "  on_mode_init = function(level)\n"
    "  end,\n"
    "  on_mode_tick = function(level, tick)\n"
    "  end,\n"
    "  on_respawn = function(ent)\n"
    "    og.log('respawn_called')\n"
    "    og.log('respawn', ent:xpos(), ent:ypos(), ent:dead())\n"
    "  end,\n"
    "})\n";

// A classic world (type 0) with the real loader.
struct ClassicWorld : TestGameWorld
{
    explicit ClassicWorld(int level_id = 3)
        : TestGameWorld(level_id)
    {
        wire_loader(world());
    }

    walker* spawn_living(int family, int team, int x, int y)
    {
        return spawn_living_in(world(), family, team, x, y);
    }

    walker* spawn_hero(int family, int team, int x, int y, int guy_id)
    {
        walker* w = spawn_living(family, team, x, y);
        if (w != nullptr)
            give_guy(w, guy_id);
        return w;
    }

    void tick(int count = 1)
    {
        for (int i = 0; i < count; ++i)
            world().tick();
    }
};

// A classic arena: a hero (team 0, guy 41, seat 0, spawn point (128,128))
// and a stationary enemy that keeps the level from completing.
struct ClassicArena
{
    ClassicWorld fx;
    walker* hero = nullptr;
    walker* enemy = nullptr;

    explicit ClassicArena(short respawn_mode, short requested_ticks = 12)
    {
        fx.world().respawn_mode = respawn_mode;
        fx.world().ctf_requested_respawn_ticks = requested_ticks;
        hero = fx.spawn_hero(FAMILY_SOLDIER, 0, 320, 320, 41);
        hero->set_user(0);
        hero->set_spawn_point(128, 128, 0);
        enemy = fx.spawn_living(FAMILY_SOLDIER, 1, 480, 760);
    }

    GameWorld& world() { return fx.world(); }
};

// Floor 1 painted with grass (PixieData takes the buffer).
void fill_floor1_grass(GameWorld& w)
{
    const int gw = w.grid.w;
    const int gh = w.grid.h;
    auto* buf = new unsigned char[static_cast<std::size_t>(gw) *
                                  static_cast<std::size_t>(gh)];
    std::fill(buf, buf + static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh),
              static_cast<unsigned char>(PIX_GRASS1));
    w.grid_for_floor(1) = PixieData(1, static_cast<unsigned char>(gw),
                                    static_cast<unsigned char>(gh), buf);
}

walker* find_alive_with(GameWorld& world, int family, int team,
                        std::uint32_t excluding_id = 0)
{
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w != nullptr && !w->dead() && w->query_order() == Order::Living &&
            w->family() == family &&
            w->team_num() == static_cast<unsigned char>(team) &&
            w->entity_id() != excluding_id)
            return w;
    }
    return nullptr;
}

int count_alive_with(GameWorld& world, int family)
{
    int n = 0;
    for (const auto& uptr : world.oblist)
        if (uptr && !uptr->dead() && uptr->query_order() == Order::Living &&
            uptr->family() == family)
            ++n;
    return n;
}

// Live stains of `team` on `floor`; identity nullptr = guy-less (AI) stains.
int live_stains(GameWorld& world, int team, int floor, const guy* identity)
{
    const auto matches = [&](const walker* s) {
        if (s == nullptr || s->dead() || s->query_order() != Order::Treasure ||
            s->family() != FAMILY_STAIN ||
            s->team_num() != static_cast<unsigned char>(team) ||
            s->floor() != floor)
            return false;
        if (identity == nullptr)
            return s->myguy == nullptr;
        return s->myguy != nullptr && s->myguy->id == identity->id &&
               s->myguy->owner_player_index == identity->owner_player_index &&
               s->myguy->owner_save_slot == identity->owner_save_slot;
    };
    int n = 0;
    for (const auto* list : {&world.fxlist, &world.oblist})
        for (const auto& uptr : *list)
            if (matches(uptr.get()))
                ++n;
    return n;
}

// A stain lying in the world with no body (no corpse can shield it), as a
// corpse's drop would lie: treasure order, team, floor, a copied guy.
walker* orphan_stain(GameWorld& world, int team, int floor, int x, int y,
                     const guy* identity)
{
    walker* s = world.add_fx_ob(Order::Treasure, FAMILY_STAIN);
    if (s == nullptr)
        return nullptr;
    s->set_team_num(static_cast<unsigned char>(team));
    s->set_floor(static_cast<short>(floor));
    s->setxy(static_cast<short>(x), static_cast<short>(y));
    if (identity != nullptr)
        s->set_owned_myguy(std::make_unique<guy>(*identity));
    return s;
}

og::sim::RespawnEntry bot_entry(int family, int team, int x, int y,
                                std::uint16_t ticks, std::uint32_t id)
{
    og::sim::RespawnEntry e;
    e.kind = 1;
    e.team = static_cast<std::uint8_t>(team);
    e.family = static_cast<std::uint8_t>(family);
    e.level = 1;
    e.ticks_left = ticks;
    e.walker_entity_id = id;
    e.x = static_cast<std::int16_t>(x);
    e.y = static_cast<std::int16_t>(y);
    return e;
}

// --- possession ------------------------------------------------------------

constexpr unsigned char kGhostTeam = 0;
constexpr unsigned char kOrcTeam = 1;

walker* add_fighter(TestGameWorld& tw, int family, unsigned char team,
                    short x, short y)
{
    walker* w = tw.world().add_ob(Order::Living, family);
    if (w == nullptr)
        return nullptr;
    w->set_team_num(team);
    w->setxy(x, y);
    w->stats()->set_max_hitpoints(100.0f);
    w->stats()->set_hitpoints(100.0f);
    return w;
}

void seat(walker& w, short player)
{
    w.set_user(static_cast<signed char>(player));
    w.set_act_type(ACT_CONTROL);
}

bool notified(const TestGameWorld& tw, const std::string& text)
{
    const auto& events = tw.events.events();
    return std::any_of(events.begin(), events.end(), [&text](const auto& e) {
        return e.kind == og::sim::EventKind::Notification && e.text == text;
    });
}

int count_fx(const TestGameWorld& tw, int family)
{
    int n = 0;
    for (const auto* list : {&tw.world().oblist, &tw.world().fxlist})
        for (const auto& uptr : *list)
            if (uptr && !uptr->dead() && uptr->query_order() == Order::FX &&
                uptr->family() == family)
                ++n;
    return n;
}

// A ghost on team 0 at (64,64) and an orc on team 1 at (96,80), in a world
// with New Specials at `new_specials`.
struct PossessionScene {
    TestGameWorld tw;
    walker* ghost = nullptr;
    walker* orc = nullptr;

    explicit PossessionScene(int new_specials = 1)
    {
        tw.world().new_specials = static_cast<short>(new_specials);
        ghost = add_fighter(tw, FAMILY_GHOST, kGhostTeam, 64, 64);
        orc = add_fighter(tw, FAMILY_ORC, kOrcTeam, 96, 80);
    }

    GameWorld& world() { return tw.world(); }
};

}  // namespace

// ===========================================================================
// mode_tick.cpp
// ===========================================================================

// mode_tick.cpp:139. The declared winner is the player's only when a LIVE
// hero wears exactly that team number: a hero the flush could not revive
// (its character walks again on another team) and a live hero on another
// team both leave winner_is_player false and replay the level (next = id).
TEST(SemMode, a_declared_winner_is_the_players_only_with_a_live_hero_on_that_team)
{
    // A dead team-0 hero whose character walks again on team 1: the flush
    // scrubs it and leaves it dead.
    {
        RespawnArena arena;
        give_guy(arena.runner, 61);
        kill(arena.runner);
        walker* dup = arena.fx.spawn_living(FAMILY_SOLDIER, 1, 256, 520);
        ASSERT_NE(nullptr, dup);
        give_guy(dup, 61);

        og::sim::mode_declare_winner(arena.world(), 0);
        EXPECT_TRUE(arena.runner->dead()) << "dead hero: the flush leaves it dead";
        EXPECT_EQ(0, arena.world().mode.winner_team);
        EXPECT_FALSE(arena.world().mode.winner_is_player)
            << "dead hero on the winning team: not the player's win";
        EXPECT_EQ(arena.world().id, arena.world().mode.win_next_level)
            << "dead hero on the winning team: the level replays";
    }
    // A live hero on team 1 only; team 0 wins.
    {
        RespawnArena arena;
        walker* hero = arena.fx.spawn_living(FAMILY_SOLDIER, 1, 256, 520);
        ASSERT_NE(nullptr, hero);
        give_guy(hero, 62);
        arena.world().allied_mode = 1;

        og::sim::mode_declare_winner(arena.world(), 0);
        EXPECT_FALSE(hero->dead());
        EXPECT_FALSE(arena.world().mode.winner_is_player)
            << "live hero on another team: not the player's win";
        EXPECT_EQ(arena.world().id, arena.world().mode.win_next_level)
            << "live hero on another team: the level replays";
    }
    // Control: the same hero on the winning team is the player's win.
    {
        RespawnArena arena;
        walker* hero = arena.fx.spawn_living(FAMILY_SOLDIER, 0, 256, 520);
        ASSERT_NE(nullptr, hero);
        give_guy(hero, 63);
        og::sim::mode_declare_winner(arena.world(), 0);
        EXPECT_TRUE(arena.world().mode.winner_is_player) << "control";
        EXPECT_EQ(arena.world().id + 1, arena.world().mode.win_next_level);
    }
}

// ===========================================================================
// placement.cpp
// ===========================================================================

// placement.cpp:41. A dead or dormant walker cannot be swapped (either
// side): swap_places answers false and nothing moves.
TEST(SemPlacement, a_dead_or_dormant_walker_never_swaps_places)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);
    walker* self = w.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* other = w.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, self);
    ASSERT_NE(nullptr, other);
    self->setxy(64, 64);
    other->set_floor(1);
    other->setxy(192, 96);

    const auto unchanged = [&](const char* arm) {
        EXPECT_EQ(64, self->xpos()) << arm;
        EXPECT_EQ(64, self->ypos()) << arm;
        EXPECT_EQ(0, self->floor()) << arm;
        EXPECT_EQ(192, other->xpos()) << arm;
        EXPECT_EQ(96, other->ypos()) << arm;
        EXPECT_EQ(1, other->floor()) << arm;
    };

    other->set_dead(1);
    EXPECT_FALSE(og::sim::swap_places(w, *self, *other)) << "other dead";
    unchanged("other dead");
    EXPECT_FALSE(og::sim::swap_places(w, *other, *self)) << "first side dead";
    unchanged("first side dead");
    other->set_dead(0);

    other->set_dormant(true);
    EXPECT_FALSE(og::sim::swap_places(w, *self, *other)) << "other dormant";
    unchanged("other dormant");
    other->set_dormant(false);

    self->set_dead(1);
    EXPECT_FALSE(og::sim::swap_places(w, *self, *other)) << "self dead";
    unchanged("self dead");
    self->set_dead(0);

    // Control: both alive and awake, they trade.
    EXPECT_TRUE(og::sim::swap_places(w, *self, *other)) << "control";
    EXPECT_EQ(192, self->xpos());
    EXPECT_EQ(1, self->floor());
}

// ===========================================================================
// possession.cpp (setting new-specials: possession itself never reads the
// setting, so each test runs under both values and asserts the same arm)
// ===========================================================================

// possession.cpp:116-118. A dead, hidden or dormant rider is not a free
// rider: 'CANNOT POSSESS' and nothing changes.
TEST(SemPossession, a_dead_hidden_or_dormant_rider_cannot_possess)
{
    for (int ns : {0, 1}) {
        SCOPED_TRACE(ns ? "new specials on" : "new specials off");
        const auto refused = [](PossessionScene& s, const char* arm) {
            const og::sim::PossessResult r =
                og::sim::possess(s.world(), *s.ghost, *s.orc, 100);
            EXPECT_FALSE(r.ok) << arm;
            EXPECT_STREQ("CANNOT POSSESS", r.reason) << arm;
            EXPECT_EQ(0u, s.ghost->possess_link()) << arm;
            EXPECT_EQ(0u, s.orc->possess_link()) << arm;
            EXPECT_EQ(kOrcTeam, s.orc->team_num()) << arm;
            EXPECT_EQ(255, s.ghost->real_team_num()) << arm;
        };
        {
            PossessionScene s(ns);
            s.ghost->set_dead(1);
            refused(s, "rider dead");
            EXPECT_FALSE(s.ghost->hidden()) << "rider dead: not hidden";
        }
        {
            PossessionScene s(ns);
            s.ghost->set_hidden(true);
            refused(s, "rider hidden");
        }
        {
            PossessionScene s(ns);
            s.ghost->set_dormant(true);
            refused(s, "rider dormant");
            EXPECT_FALSE(s.ghost->hidden()) << "rider dormant: not hidden";
        }
        {
            PossessionScene s(ns);
            EXPECT_TRUE(og::sim::possess(s.world(), *s.ghost, *s.orc, 100).ok)
                << "control: a free rider possesses";
        }
    }
}

// possession.cpp:125. A dormant body cannot be possessed: 'NO HOST IN REACH'.
TEST(SemPossession, a_dormant_body_is_no_host_in_reach)
{
    for (int ns : {0, 1}) {
        SCOPED_TRACE(ns ? "new specials on" : "new specials off");
        PossessionScene s(ns);
        s.orc->set_dormant(true);
        const og::sim::PossessResult r =
            og::sim::possess(s.world(), *s.ghost, *s.orc, 100);
        EXPECT_FALSE(r.ok) << "host dormant";
        EXPECT_STREQ("NO HOST IN REACH", r.reason) << "host dormant";
        EXPECT_FALSE(s.ghost->hidden()) << "host dormant: the rider stays out";
        EXPECT_EQ(0u, s.ghost->possess_link());
        EXPECT_EQ(0u, s.orc->possess_link());
        EXPECT_EQ(kOrcTeam, s.orc->team_num());
    }
}

// possession.cpp:170. Releasing a walker that neither rides nor is ridden
// changes nothing: not a visible bot, and not a hidden charmed walker (which
// the release would otherwise reveal, burst and uncharm as a 'rider').
TEST(SemPossession, releasing_an_unlinked_walker_changes_nothing)
{
    for (int ns : {0, 1}) {
        SCOPED_TRACE(ns ? "new specials on" : "new specials off");
        PossessionScene s(ns);
        seat(*s.ghost, 0);
        EXPECT_EQ(nullptr, og::sim::release_possession(s.world(), *s.orc))
            << "unlinked: return";
        EXPECT_EQ(kOrcTeam, s.orc->team_num()) << "unlinked: nothing changes";
        EXPECT_EQ(-1, s.orc->user());
        EXPECT_FALSE(s.orc->hidden());
        EXPECT_FLOAT_EQ(100.0f, s.orc->stats()->hitpoints());
        EXPECT_EQ(0, s.ghost->user()) << "the seat's tag stays on the ghost";
        EXPECT_EQ(ACT_CONTROL, s.ghost->act_type());

        // A hidden, charmed, unlinked walker.
        s.orc->set_real_team_num(2);
        s.orc->set_charm_left(5);
        s.orc->set_hidden(true);
        EXPECT_EQ(nullptr, og::sim::release_possession(s.world(), *s.orc))
            << "unlinked hidden: return";
        EXPECT_TRUE(s.orc->hidden()) << "unlinked hidden: not revealed";
        EXPECT_EQ(2, s.orc->real_team_num()) << "unlinked hidden: charm kept";
        EXPECT_EQ(kOrcTeam, s.orc->team_num());
        EXPECT_EQ(0, count_fx(s.tw, FAMILY_GHOST_SCARE))
            << "unlinked: no scare burst";
        EXPECT_FALSE(notified(s.tw, "ORC is cast out"));
    }
}

// possession.cpp:201. A release whose rider has left the world only unlinks
// the host (no reveal, no burst; the host keeps the rider's team); with the
// rider present the release reveals it and bursts.
TEST(SemPossession, a_release_with_the_rider_gone_only_unlinks_the_host)
{
    for (int ns : {0, 1}) {
        SCOPED_TRACE(ns ? "new specials on" : "new specials off");
        {
            PossessionScene s(ns);
            ASSERT_TRUE(og::sim::possess(s.world(), *s.ghost, *s.orc, 100).ok);
            s.orc->set_possess_link(0xfffffff0u);  // no walker carries it
            EXPECT_EQ(nullptr, og::sim::release_possession(s.world(), *s.orc))
                << "rider gone: returns nothing";
            EXPECT_EQ(0u, s.orc->possess_link()) << "rider gone: host unlinked";
            EXPECT_EQ(0, s.orc->possess_ticks());
            EXPECT_EQ(kGhostTeam, s.orc->team_num())
                << "rider gone: the host keeps the rider's team";
            EXPECT_EQ(0, count_fx(s.tw, FAMILY_GHOST_SCARE))
                << "rider gone: no burst";
        }
        {
            PossessionScene s(ns);
            ASSERT_TRUE(og::sim::possess(s.world(), *s.ghost, *s.orc, 100).ok);
            EXPECT_EQ(s.ghost, og::sim::release_possession(s.world(), *s.orc))
                << "rider present: returns the rider";
            EXPECT_FALSE(s.ghost->hidden()) << "rider present: revealed";
            EXPECT_EQ(0u, s.ghost->possess_link());
            EXPECT_EQ(kOrcTeam, s.orc->team_num());
            EXPECT_EQ(1, count_fx(s.tw, FAMILY_GHOST_SCARE))
                << "rider present: one burst";
        }
    }
}

// possession.cpp:204-210. A bot rider let go moves no seat tag; and the
// rider always comes out on the host's floor.
TEST(SemPossession, a_released_rider_takes_the_hosts_floor_and_a_bot_moves_no_tag)
{
    for (int ns : {0, 1}) {
        SCOPED_TRACE(ns ? "new specials on" : "new specials off");
        {
            PossessionScene s(ns);
            const auto bot_act = s.ghost->act_type();
            ASSERT_NE(ACT_CONTROL, bot_act) << "control: the bot ghost is not driven";
            ASSERT_TRUE(og::sim::possess(s.world(), *s.ghost, *s.orc, 1).ok);
            og::sim::possession_host_tick(s.world(), *dynamic_cast<living*>(s.orc));
            ASSERT_EQ(0u, s.orc->possess_link()) << "the countdown released it";
            EXPECT_EQ(-1, s.ghost->user()) << "bot: no tag on the ghost";
            EXPECT_EQ(bot_act, s.ghost->act_type())
                << "bot: not driven (act type as before the possess)";
            EXPECT_EQ(-1, s.orc->user()) << "bot: no tag on the host";
        }
        {
            PossessionScene s(ns);
            s.world().set_floor_count(2);
            ASSERT_TRUE(og::sim::possess(s.world(), *s.ghost, *s.orc, 100).ok);
            ASSERT_EQ(0, s.ghost->floor());
            s.orc->set_floor(1);
            ASSERT_NE(nullptr, og::sim::release_possession(s.world(), *s.orc));
            EXPECT_EQ(1, s.ghost->floor()) << "any: the rider is on the host's floor";
        }
    }
}

// possession.cpp:223. Only a release from a dying body announces 'cast
// out': the countdown and Switch Character do not.
TEST(SemPossession, only_a_dying_host_casts_its_rider_out_aloud)
{
    for (int ns : {0, 1}) {
        SCOPED_TRACE(ns ? "new specials on" : "new specials off");
        {
            PossessionScene s(ns);
            ASSERT_TRUE(og::sim::possess(s.world(), *s.ghost, *s.orc, 1).ok);
            og::sim::possession_host_tick(s.world(), *dynamic_cast<living*>(s.orc));
            ASSERT_EQ(0u, s.orc->possess_link());
            EXPECT_FALSE(notified(s.tw, "GHOST is cast out"))
                << "timer release: no announcement";
        }
        {
            PossessionScene s(ns);
            seat(*s.ghost, 0);
            walker* control = s.ghost;
            SimInputDebounce debounce{};
            InputState input;
            input.clear();
            ASSERT_TRUE(og::sim::possess(s.world(), *s.ghost, *s.orc, 100).ok);
            sim_process_player_input(input.players[0], control, s.world(), 0,
                                     kGhostTeam, debounce, &s.tw.events);
            ASSERT_EQ(s.orc, control);
            input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
            sim_process_player_input(input.players[0], control, s.world(), 0,
                                     kGhostTeam, debounce, &s.tw.events);
            ASSERT_EQ(0u, s.orc->possess_link()) << "Switch Character released it";
            EXPECT_FALSE(notified(s.tw, "GHOST is cast out"))
                << "switch release: no announcement";
        }
        {
            PossessionScene s(ns);
            ASSERT_TRUE(og::sim::possess(s.world(), *s.ghost, *s.orc, 100).ok);
            kill(s.orc);
            EXPECT_TRUE(notified(s.tw, "GHOST is cast out")) << "control: death";
        }
    }
}

// possession.cpp:244. A rider marked dead without walker::death: the host
// just drops the link and keeps the team it has; no reveal, no burst.
TEST(SemPossession, a_host_whose_rider_is_dead_just_drops_the_link)
{
    for (int ns : {0, 1}) {
        SCOPED_TRACE(ns ? "new specials on" : "new specials off");
        PossessionScene s(ns);
        ASSERT_TRUE(og::sim::possess(s.world(), *s.ghost, *s.orc, 100).ok);
        s.ghost->set_dead(1);
        og::sim::possession_host_tick(s.world(), *dynamic_cast<living*>(s.orc));
        EXPECT_EQ(0u, s.orc->possess_link()) << "rider dead: host unlinked";
        EXPECT_EQ(0, s.orc->possess_ticks()) << "rider dead: no countdown left";
        EXPECT_EQ(kGhostTeam, s.orc->team_num())
            << "rider dead: the host keeps the rider's team";
        EXPECT_TRUE(s.ghost->hidden()) << "rider dead: the corpse stays hidden";
        EXPECT_EQ(0, count_fx(s.tw, FAMILY_GHOST_SCARE)) << "rider dead: no burst";
    }
}

// possession.cpp:274. The REASSEMBLE ward stands the skeleton back up on
// the teleport-in row from its first frame.
TEST(SemPossession, a_warded_skeleton_stands_up_on_the_teleport_in_row)
{
    for (int ns : {0, 1}) {
        SCOPED_TRACE(ns ? "new specials on" : "new specials off");
        TestGameWorld tw;
        tw.world().new_specials = static_cast<short>(ns);
        walker* skel = add_fighter(tw, FAMILY_SKELETON, 0, 96, 96);
        ASSERT_NE(nullptr, skel);
        skel->stats()->set_max_hitpoints(60.0f);
        skel->set_ani_type(ANI_WALK);
        skel->set_cycle(3);
        skel->set_kit_state(KIT_WARD);
        skel->stats()->set_hitpoints(-5.0f);
        skel->set_dead(1);
        EXPECT_TRUE(og::sim::kit_on_death(tw.world(), *skel)) << "warded: survives";
        EXPECT_FALSE(skel->dead());
        EXPECT_FLOAT_EQ(15.0f, skel->stats()->hitpoints());
        EXPECT_EQ(ANI_TELE_IN, skel->ani_type()) << "warded: teleport-in row";
        EXPECT_EQ(0, skel->cycle()) << "warded: from the first frame";
    }
}

// possession.cpp:289. The death of a walker that neither rides nor is
// ridden has no possession effect, even when it is hidden.
TEST(SemPossession, an_unlinked_death_touches_no_possession)
{
    for (int ns : {0, 1}) {
        SCOPED_TRACE(ns ? "new specials on" : "new specials off");
        PossessionScene s(ns);
        walker* other = add_fighter(s.tw, FAMILY_ORC, kOrcTeam, 128, 80);
        ASSERT_NE(nullptr, other);
        s.orc->stats()->set_hitpoints(-10.0f);
        kill(s.orc);
        EXPECT_EQ(1, s.orc->death_called()) << "unlinked: the death runs";
        EXPECT_FLOAT_EQ(100.0f, s.ghost->stats()->hitpoints())
            << "unlinked: no overkill passed to anyone";
        EXPECT_FALSE(s.ghost->hidden());
        EXPECT_EQ(0, count_fx(s.tw, FAMILY_GHOST_SCARE)) << "unlinked: no burst";
        EXPECT_FALSE(notified(s.tw, "ORC is cast out"));
        EXPECT_FALSE(notified(s.tw, "GHOST is cast out"));

        // A hidden unlinked walker dies (a script's kill of a hidden body).
        other->set_hidden(true);
        EXPECT_FALSE(og::sim::kit_on_death(s.world(), *other))
            << "unlinked hidden: the death proceeds";
        EXPECT_TRUE(other->hidden()) << "unlinked hidden: no reveal";
        EXPECT_EQ(0, count_fx(s.tw, FAMILY_GHOST_SCARE))
            << "unlinked hidden: no burst";
        EXPECT_EQ(kOrcTeam, other->team_num());
    }
}

// possession.cpp:315. The seat drives the ridden body only while the body
// is alive and wears the seat's tag.
TEST(SemPossession, the_seat_drives_a_ridden_body_only_if_alive_and_tagged_for_it)
{
    for (int ns : {0, 1}) {
        SCOPED_TRACE(ns ? "new specials on" : "new specials off");
        PossessionScene s(ns);
        seat(*s.ghost, 0);
        ASSERT_TRUE(og::sim::possess(s.world(), *s.ghost, *s.orc, 100).ok);
        ASSERT_EQ(0, s.orc->user());

        walker* control = s.ghost;
        s.orc->set_user(1);
        og::sim::possession_seat_redirect(s.world(), control, 0);
        EXPECT_EQ(s.ghost, control) << "host tagged for another seat";

        s.orc->set_user(0);
        s.orc->set_dead(1);
        control = s.ghost;
        og::sim::possession_seat_redirect(s.world(), control, 0);
        EXPECT_EQ(s.ghost, control) << "host dead";

        s.orc->set_dead(0);
        control = s.ghost;
        og::sim::possession_seat_redirect(s.world(), control, 0);
        EXPECT_EQ(s.orc, control) << "control: alive and tagged";
    }
}

// possession.cpp:326. A seat whose body still wears a tag keeps it: the
// tag-following scan runs only for an untagged body.
TEST(SemPossession, a_seat_on_a_still_tagged_body_keeps_it)
{
    for (int ns : {0, 1}) {
        SCOPED_TRACE(ns ? "new specials on" : "new specials off");
        TestGameWorld tw;
        tw.world().new_specials = static_cast<short>(ns);
        walker* a = add_fighter(tw, FAMILY_SOLDIER, 0, 32, 32);
        walker* b = add_fighter(tw, FAMILY_SOLDIER, 0, 96, 32);
        ASSERT_TRUE(a && b);
        seat(*a, 0);
        seat(*b, 0);
        walker* control = b;
        og::sim::possession_seat_redirect(tw.world(), control, 0);
        EXPECT_EQ(b, control) << "control still tagged: unchanged";

        // Untagged: on, the scan follows the tag to A; off, nothing moves.
        b->set_user(-1);
        control = b;
        og::sim::possession_seat_redirect(tw.world(), control, 0);
        EXPECT_EQ(ns ? a : b, control)
            << (ns ? "setting on: follows the tag" : "setting off: unchanged");
    }
}

// ===========================================================================
// respawn.cpp (setting none: classic Gladiator has no respawn engine, so no
// arm here has an old-game counterpart)
// ===========================================================================

// respawn.cpp:51-54. A hero revived on its own tile (setxy a no-op) is put
// back into the collision map, so it blocks a spawn probe; a walker the map
// already holds is left where the map has it; a dead, hidden, dormant or
// ignored walker is never put in.
TEST(SemRespawn, a_revived_walker_rejoins_the_collision_map_and_an_absent_one_never_does)
{
    RespawnArena arena;
    GameWorld& world = arena.world();
    walker* runner = arena.runner;
    give_guy(runner, 41);
    kill(runner);
    ASSERT_EQ(0u, world.myobmap->walker_to_pos.count(runner))
        << "a corpse is out of the map";
    ASSERT_TRUE(arena.schedule(runner));
    arena.fx.tick(6);
    ASSERT_FALSE(runner->dead());
    ASSERT_EQ(320, runner->xpos()) << "revived in place";
    ASSERT_EQ(1u, world.myobmap->walker_to_pos.count(runner))
        << "revived in place: back in the map";
    EXPECT_FALSE(og::sim::respawn_spot_clear(world, arena.enemy, 320, 320, 0))
        << "revived in place: it blocks the spot";

    // Already registered: the map's cells for it are left alone (a moved
    // coordinate the map never heard of is not re-indexed by this call).
    const auto cells_before = world.myobmap->walker_to_pos.at(runner);
    const std::size_t size_before = world.myobmap->walker_to_pos.size();
    runner->set_xpos(640);
    og::sim::ensure_obmap_registration(world, runner);
    EXPECT_EQ(size_before, world.myobmap->walker_to_pos.size())
        << "already registered: no new entry";
    EXPECT_EQ(cells_before, world.myobmap->walker_to_pos.at(runner))
        << "already registered: left where the map has it";
    runner->set_xpos(320);

    // Never registered: dead, hidden, dormant, ignored.
    struct Case { const char* arm; int x; };
    const Case cases[] = {{"dead", 96}, {"hidden", 160}, {"dormant", 224},
                          {"ignored", 288}};
    for (const Case& c : cases) {
        walker* w = arena.fx.spawn_living(FAMILY_SOLDIER, 0, c.x, 600);
        ASSERT_NE(nullptr, w);
        world.myobmap->remove(w);
        const std::string arm = c.arm;
        if (arm == "dead")
            w->set_dead(1);
        else if (arm == "hidden")
            w->set_hidden(true);
        else if (arm == "dormant")
            w->set_dormant(true);
        else
            w->set_ignore(1);
        og::sim::ensure_obmap_registration(world, w);
        EXPECT_EQ(0u, world.myobmap->walker_to_pos.count(w))
            << c.arm << ": never put into the map";
    }
}

// respawn.cpp:67-76. A tree, a boulder and a weapon a pack declares a
// placement blocker veto a spawn spot their box overlaps (and only an
// overlapping one); a dead or hidden living and a plain weapon never do.
TEST(SemRespawn, trees_boulders_and_declared_blockers_take_a_spot_and_dead_or_hidden_bodies_do_not)
{
    og::test::ScopedPackStoreState pack_store_restore;
    og::test::mount_core_pack();
    const int bone_wall = og::families::resolve_family_string_id(
        Order::Weapon, "core:bone_wall");
    if (bone_wall != 20)
        (void)og::resources::refresh_pack_scripts();
    ASSERT_EQ(20, og::families::resolve_family_string_id(Order::Weapon,
                                                         "core:bone_wall"));
    TestGameWorld tw;
    GameWorld& world = tw.world();
    walker* probe = spawn_living_in(world, FAMILY_SOLDIER, 0, 600, 600);
    ASSERT_NE(nullptr, probe);

    const auto place = [&world](Order order, int family, int x, int y) {
        walker* w = order == Order::Weapon ? world.add_weap_ob(order, family)
                                           : world.add_ob(order, family);
        if (w != nullptr)
            w->setxy(static_cast<short>(x), static_cast<short>(y));
        return w;
    };
    walker* tree = place(Order::Weapon, FAMILY_TREE, 64, 64);
    walker* boulder = place(Order::Weapon, FAMILY_BOULDER, 192, 64);
    walker* wall = place(Order::Weapon, 20, 320, 64);
    walker* knife = place(Order::Weapon, FAMILY_KNIFE, 448, 64);
    walker* corpse = place(Order::Living, FAMILY_SOLDIER, 64, 256);
    walker* hidden = place(Order::Living, FAMILY_SOLDIER, 192, 256);
    ASSERT_TRUE(tree && boulder && wall && knife && corpse && hidden);
    ASSERT_TRUE(og::sim::declares_blocks_placement(*wall));
    // Both stay in the collision map: set_dead alone leaves the corpse there
    // (as on the tick it falls), and set_hidden takes a walker out, so the
    // hidden one is put back by hand: the probe's own guard is what must
    // pass over it.
    corpse->set_dead(1);
    hidden->set_hidden(true);
    world.myobmap->add(hidden, hidden->xpos(), hidden->ypos());
    ASSERT_EQ(1u, world.myobmap->walker_to_pos.count(corpse));
    ASSERT_EQ(1u, world.myobmap->walker_to_pos.count(hidden));

    for (int floor : {-1, 0}) {
        SCOPED_TRACE(floor < 0 ? "anchor probe" : "floor-explicit probe");
        const auto clear = [&](const walker* at, int dx) {
            return og::sim::respawn_spot_clear(
                world, probe, static_cast<short>(at->xpos() + dx),
                at->ypos(), floor);
        };
        EXPECT_FALSE(clear(tree, 0)) << "tree overlapping";
        EXPECT_TRUE(clear(tree, tree->sizex())) << "tree: edge-adjacent is clear";
        EXPECT_FALSE(clear(boulder, 0)) << "boulder overlapping";
        EXPECT_TRUE(clear(boulder, boulder->sizex()))
            << "boulder: edge-adjacent is clear";
        EXPECT_FALSE(clear(wall, 0)) << "declared placement blocker overlapping";
        EXPECT_TRUE(clear(wall, wall->sizex()))
            << "declared blocker: edge-adjacent is clear";
        EXPECT_TRUE(clear(knife, 0)) << "a plain weapon never blocks";
        EXPECT_TRUE(clear(corpse, 0)) << "a dead living never blocks";
        EXPECT_TRUE(clear(hidden, 0)) << "a hidden living never blocks";
    }
}

// respawn.cpp:118-128. A revived hero's stats are bound back to it, and its
// death bookkeeping is reset, so a second death runs the death path again.
TEST(SemRespawn, a_revived_hero_is_its_own_controller_and_can_die_again)
{
    RespawnArena arena;
    walker* runner = arena.runner;
    give_guy(runner, 41);
    seat(*runner, 0);
    kill(runner);
    ASSERT_EQ(1, runner->death_called());
    ASSERT_TRUE(arena.schedule(runner));
    arena.fx.tick(6);
    ASSERT_FALSE(runner->dead());
    EXPECT_EQ(runner, runner->stats()->controller())
        << "revived: the controller binding is the walker";
    EXPECT_EQ(0, runner->death_called()) << "revived: death_called reset";
    EXPECT_EQ(0, live_stains(arena.world(), 0, 0, runner->myguy))
        << "the fire scrubbed the first stain";

    runner->stats()->set_hitpoints(-1.0f);
    kill(runner);
    EXPECT_EQ(1, runner->death_called()) << "a second death runs the death path";
    EXPECT_EQ(1, live_stains(arena.world(), 0, 0, runner->myguy))
        << "a second death drops a second stain";
}

// respawn.cpp:134-136. An uncharmed hero revives on the team it died on,
// whichever team that is.
TEST(SemRespawn, an_uncharmed_hero_revives_on_its_own_team)
{
    RespawnArena arena;
    walker* runner = arena.runner;
    give_guy(runner, 43);
    walker* other = arena.fx.spawn_living(FAMILY_SOLDIER, 1, 480, 320);
    ASSERT_NE(nullptr, other);
    give_guy(other, 44);
    kill(runner);
    kill(other);
    ASSERT_TRUE(arena.schedule(runner));
    ASSERT_TRUE(arena.schedule(other));
    arena.fx.tick(7);
    ASSERT_FALSE(runner->dead());
    ASSERT_FALSE(other->dead());
    EXPECT_EQ(0, runner->team_num()) << "uncharmed team-0 hero";
    EXPECT_EQ(1, other->team_num()) << "uncharmed team-1 hero";
    EXPECT_EQ(255, runner->real_team_num());
    EXPECT_EQ(255, other->real_team_num());
}

// respawn.cpp:207 (and :291). A fire scrubs only its own corpse's stain: an
// AI corpse never clears a player's stain, and a player corpse never clears
// another character's, even with no body there to shield it.
TEST(SemRespawn, a_respawn_fire_scrubs_only_a_stain_of_its_own_identity)
{
    guy g74(FAMILY_SOLDIER);
    g74.id = 74;
    {
        RespawnArena arena;
        // An archer: the arena's own runner is a live team-0 soldier, so the
        // replacement is found by a family nobody else on the field has.
        walker* bot = arena.fx.spawn_living(FAMILY_ARCHER, 0, 224, 224);
        ASSERT_NE(nullptr, bot);
        const std::uint32_t bot_id = bot->entity_id();
        ASSERT_NE(nullptr, orphan_stain(arena.world(), 0, 0, 224, 224, &g74));
        kill(bot);
        ASSERT_EQ(1, live_stains(arena.world(), 0, 0, nullptr));
        ASSERT_TRUE(arena.schedule(bot, 2));
        arena.fx.tick(2);
        EXPECT_EQ(1, count_alive_with(arena.world(), FAMILY_ARCHER))
            << "the bot's replacement is fielded";
        EXPECT_NE(nullptr, find_alive_with(arena.world(), FAMILY_ARCHER, 0, bot_id))
            << "the bot's replacement is a fresh team-0 archer";
        EXPECT_EQ(0, live_stains(arena.world(), 0, 0, nullptr))
            << "AI fire: its own guy-less stain is scrubbed";
        EXPECT_EQ(1, live_stains(arena.world(), 0, 0, &g74))
            << "AI fire: a player's stain at the spot survives";
    }
    {
        RespawnArena arena;
        walker* runner = arena.runner;
        give_guy(runner, 71);
        guy g72(FAMILY_SOLDIER);
        g72.id = 72;
        ASSERT_NE(nullptr, orphan_stain(arena.world(), 0, 0, 320, 320, &g72));
        kill(runner);
        ASSERT_TRUE(arena.schedule(runner, 2));
        arena.fx.tick(2);
        ASSERT_FALSE(runner->dead());
        EXPECT_EQ(0, live_stains(arena.world(), 0, 0, runner->myguy))
            << "player fire: its own stain is scrubbed";
        EXPECT_EQ(1, live_stains(arena.world(), 0, 0, &g72))
            << "player fire: another character's stain survives";
    }
}

// respawn.cpp:278-283. A fire scrubs drops on the corpse's own team only: a
// same-identity stain at the same corner on another team survives.
TEST(SemRespawn, a_respawn_fire_leaves_another_teams_drop)
{
    RespawnArena arena;
    walker* runner = arena.runner;
    give_guy(runner, 75);
    ASSERT_NE(nullptr, orphan_stain(arena.world(), 1, 0, 320, 320, runner->myguy));
    kill(runner);
    ASSERT_EQ(1, live_stains(arena.world(), 0, 0, runner->myguy));
    ASSERT_TRUE(arena.schedule(runner, 2));
    arena.fx.tick(2);
    ASSERT_FALSE(runner->dead());
    EXPECT_EQ(0, live_stains(arena.world(), 0, 0, runner->myguy))
        << "own team: scrubbed";
    EXPECT_EQ(1, live_stains(arena.world(), 1, 0, runner->myguy))
        << "drop on another team: survives";
}

// respawn.cpp:315. A drop is the corpse's when its centre is within 8px
// (Manhattan) of the corpse's centre: 8 is scrubbed, 9 survives.
TEST(SemRespawn, a_drop_more_than_eight_pixels_from_the_corpse_is_not_its_drop)
{
    RespawnArena arena;
    walker* runner = arena.runner;
    give_guy(runner, 90);
    const int cx = runner->xpos() + runner->sizex() / 2;
    const int cy = runner->ypos() + runner->sizey() / 2;
    walker* at8 = orphan_stain(arena.world(), 0, 0, 0, 0, runner->myguy);
    walker* at9 = orphan_stain(arena.world(), 0, 0, 0, 0, runner->myguy);
    ASSERT_TRUE(at8 && at9);
    const std::uint32_t id8 = at8->entity_id();
    const std::uint32_t id9 = at9->entity_id();
    const int sy = cy - at8->sizey() / 2;
    at8->setxy(static_cast<short>(cx + 8 - at8->sizex() / 2), static_cast<short>(sy));
    at9->setxy(static_cast<short>(cx + 9 - at9->sizex() / 2), static_cast<short>(sy));
    kill(runner);
    ASSERT_EQ(3, live_stains(arena.world(), 0, 0, runner->myguy));
    ASSERT_TRUE(arena.schedule(runner, 2));
    arena.fx.tick(2);
    ASSERT_FALSE(runner->dead());
    const walker* s8 = arena.world().find_by_id(id8);
    const walker* s9 = arena.world().find_by_id(id9);
    EXPECT_TRUE(s8 == nullptr || s8->dead()) << "centre 8px away: scrubbed";
    EXPECT_TRUE(s9 != nullptr && !s9->dead()) << "centre 9px away: survives";
    EXPECT_EQ(1, live_stains(arena.world(), 0, 0, runner->myguy))
        << "only the 9px drop is left";
}

// respawn.cpp:330-356. At the queue cap a player is refused when only
// players are queued (the scheduler says so), and an evicted bot is fielded
// at once at its recorded spot; in a scripted mode that eviction fire calls
// no on_respawn.
TEST(SemRespawn, a_full_queue_refuses_a_player_and_fields_an_evicted_bot_at_once)
{
    {
        RespawnArena arena;
        walker* bot = arena.fx.spawn_living(FAMILY_ARCHER, 1, 480, 700);
        ASSERT_NE(nullptr, bot);
        auto& queue = arena.world().respawn.respawn_queue;
        for (int i = 0; i < og::sim::kRespawnMaxQueueEntries; ++i) {
            og::sim::RespawnEntry filler;
            filler.kind = 0;
            filler.ticks_left = 5000;
            filler.walker_entity_id = 800000u + static_cast<std::uint32_t>(i);
            queue.push_back(filler);
        }
        bot->set_dead(1);
        EXPECT_FALSE(arena.schedule(bot)) << "all players queued: refused";
        EXPECT_EQ(static_cast<std::size_t>(og::sim::kRespawnMaxQueueEntries),
                  queue.size());
        EXPECT_FALSE(og::sim::respawn_pending_for(arena.world(), bot));
    }
    {
        RespawnArena arena;
        walker* runner = arena.runner;
        give_guy(runner, 41);
        auto& queue = arena.world().respawn.respawn_queue;
        for (int i = 0; i < og::sim::kRespawnMaxQueueEntries; ++i)
            queue.push_back(bot_entry(FAMILY_ARCHER, 1, 224, 128, 5000,
                                      900000u + static_cast<std::uint32_t>(i)));
        ASSERT_EQ(nullptr, find_alive_with(arena.world(), FAMILY_ARCHER, 1));
        kill(runner);
        ASSERT_TRUE(arena.schedule(runner));
        walker* fielded = find_alive_with(arena.world(), FAMILY_ARCHER, 1);
        ASSERT_NE(nullptr, fielded) << "evicted bot: fielded at once";
        EXPECT_EQ(224, fielded->xpos()) << "at its recorded spot";
        EXPECT_EQ(128, fielded->ypos());
        EXPECT_EQ(static_cast<std::size_t>(og::sim::kRespawnMaxQueueEntries),
                  queue.size());
        EXPECT_EQ(0, queue.back().kind) << "the newcomer is queued";
    }
    {
        ModeWorld fx;
        fx.register_script(kRespawnLoggerScript);
        ASSERT_NE(nullptr, fx.spawn_living(FAMILY_ORC, 1, 480, 480));
        walker* hero = fx.spawn_living(FAMILY_SOLDIER, 0, 320, 320);
        ASSERT_NE(nullptr, hero);
        give_guy(hero, 45);
        fx.tick();
        ASSERT_TRUE(fx.world().mode.active);
        auto& queue = fx.world().respawn.respawn_queue;
        for (int i = 0; i < og::sim::kRespawnMaxQueueEntries; ++i)
            queue.push_back(bot_entry(FAMILY_ARCHER, 1, 224, 128, 5000,
                                      900000u + static_cast<std::uint32_t>(i)));
        kill(hero);
        ASSERT_TRUE(og::sim::respawn_schedule_corpse(fx.world(), hero, 0));
        walker* fielded = find_alive_with(fx.world(), FAMILY_ARCHER, 1);
        ASSERT_NE(nullptr, fielded) << "scripted eviction: the bot is fielded";
        EXPECT_EQ(224, fielded->xpos()) << "at its entry spot";
        EXPECT_EQ(128, fielded->ypos());
        EXPECT_EQ(0, fx.log_count("respawn_called"))
            << "scripted eviction fire: no on_respawn";
    }
}

// respawn.cpp:364-367. A queued respawn records the corpse's TRUE team: a
// charmed hero's entry and a charmed bot's (whose fresh replacement takes
// the entry's team) both carry the team the charm took them from.
TEST(SemRespawn, a_charmed_corpse_is_queued_on_its_true_team)
{
    RespawnArena arena;
    walker* runner = arena.runner;
    give_guy(runner, 46);
    runner->set_real_team_num(0);
    runner->set_team_num(1);
    kill(runner);
    ASSERT_TRUE(arena.schedule(runner));
    EXPECT_EQ(0, arena.world().respawn.respawn_queue[0].team)
        << "charmed hero: entry on the true team";

    walker* bot = arena.fx.spawn_living(FAMILY_ARCHER, 1, 480, 700);
    ASSERT_NE(nullptr, bot);
    bot->set_real_team_num(1);
    bot->set_team_num(0);
    kill(bot);
    ASSERT_TRUE(arena.schedule(bot));
    EXPECT_EQ(1, arena.world().respawn.respawn_queue.back().team)
        << "charmed bot: entry on the true team";
    arena.fx.tick(7);
    EXPECT_NE(nullptr, find_alive_with(arena.world(), FAMILY_ARCHER, 1))
        << "charmed bot: the replacement fights for the true team";
    EXPECT_EQ(nullptr, find_alive_with(arena.world(), FAMILY_ARCHER, 0))
        << "charmed bot: nothing on the charmer's team";
}

// respawn.cpp:378-388. A respawn without a spawn point is queued on the
// floor the corpse fell on; one with a spawn point on its spawn floor.
TEST(SemRespawn, a_respawn_is_queued_on_the_floor_of_its_spawn_point_or_of_its_fall)
{
    ClassicWorld fx;
    fx.world().respawn_mode = og::sim::kRespawnModeHeroes;
    fx.world().ctf_requested_respawn_ticks = 12;
    fx.world().set_floor_count(2);
    fill_floor1_grass(fx.world());
    ASSERT_NE(nullptr, fx.spawn_living(FAMILY_SOLDIER, 1, 480, 760));
    walker* fell = fx.spawn_hero(FAMILY_SOLDIER, 0, 320, 320, 47);
    walker* homed = fx.spawn_hero(FAMILY_SOLDIER, 0, 400, 320, 48);
    ASSERT_TRUE(fell && homed);
    fell->change_floor(1);
    homed->set_spawn_point(128, 128, 1);
    kill(fell);
    kill(homed);
    fx.tick();
    const auto& queue = fx.world().respawn.respawn_queue;
    ASSERT_EQ(2u, queue.size());
    const auto entry_for = [&queue](const walker* w) {
        return *std::find_if(queue.begin(), queue.end(), [w](const auto& e) {
            return e.walker_entity_id == w->entity_id();
        });
    };
    const og::sim::RespawnEntry a = entry_for(fell);
    EXPECT_EQ(320, a.x) << "no spawn point: where it fell";
    EXPECT_EQ(320, a.y);
    EXPECT_EQ(1, a.floor) << "no spawn point: the corpse's floor";
    const og::sim::RespawnEntry b = entry_for(homed);
    EXPECT_EQ(128, b.x) << "spawn point: its spot";
    EXPECT_EQ(128, b.y);
    EXPECT_EQ(1, b.floor) << "spawn point: its floor";
}

// respawn.cpp:423-428. A timer fire that revived nothing (a bot's spot is
// taken) calls no on_respawn and queues a retry; the retry that does field
// the bot calls it once. A classic world never calls Lua for a respawn.
TEST(SemRespawn, on_respawn_runs_only_for_a_scripted_fire_that_revived_someone)
{
    {
        ModeWorld fx;
        fx.register_script(kRespawnLoggerScript);
        ASSERT_NE(nullptr, fx.spawn_living(FAMILY_ORC, 1, 480, 480));
        fx.tick();
        ASSERT_TRUE(fx.world().mode.active);
        walker* bot = fx.spawn_living(FAMILY_SOLDIER, 0, 224, 224);
        ASSERT_NE(nullptr, bot);
        kill(bot);
        ASSERT_TRUE(og::sim::respawn_schedule_corpse(fx.world(), bot, 2));
        walker* squatter = fx.spawn_living(FAMILY_ORC, 1, 224, 224);
        ASSERT_NE(nullptr, squatter);
        fx.tick(2);
        EXPECT_EQ(0, fx.log_count("respawn_called"))
            << "scripted, fire revived nothing: no hook";
        const auto& queue = fx.world().respawn.respawn_queue;
        ASSERT_EQ(1u, queue.size()) << "a retry is queued";
        EXPECT_EQ(1, queue[0].kind);
        EXPECT_EQ(og::sim::kClassicBlockedRetryTicks - 1, queue[0].ticks_left)
            << "the retry joined this tick's pass";
        squatter->setxy(480, 600);
        fx.tick(og::sim::kClassicBlockedRetryTicks - 1);
        EXPECT_EQ(1, fx.log_count("respawn_called"))
            << "scripted, revive succeeded: one hook";
        EXPECT_EQ(1, fx.log_count("respawn\t224\t224\t0"));
    }
    {
        ModeWorld fx;
        fx.world().type = 0;
        fx.world().respawn_mode = og::sim::kRespawnModeHeroes;
        fx.world().ctf_requested_respawn_ticks = 12;
        fx.register_script(kRespawnLoggerScript);
        ASSERT_NE(nullptr, fx.spawn_living(FAMILY_ORC, 1, 480, 760));
        walker* hero = fx.spawn_living(FAMILY_SOLDIER, 0, 224, 224);
        ASSERT_NE(nullptr, hero);
        give_guy(hero, 49);
        kill(hero);
        fx.tick(14);
        EXPECT_FALSE(hero->dead()) << "classic mode: the revive happens";
        EXPECT_EQ(0, fx.log_count("respawn_called"))
            << "classic mode: no Lua dispatch";
    }
}

// respawn.cpp:438. Respawn mode 0 is off: a dead hero stays dead, nothing
// is queued and the seat is not held for it.
TEST(SemRespawn, respawn_mode_off_leaves_a_dead_hero_dead)
{
    ClassicArena arena(og::sim::kRespawnModeOff);
    kill(arena.hero);
    arena.fx.tick(30);
    EXPECT_TRUE(arena.hero->dead()) << "mode 0: the hero stays dead";
    EXPECT_TRUE(arena.world().respawn.respawn_queue.empty()) << "mode 0: no entry";
    EXPECT_FALSE(og::sim::respawn_retains_player_control(arena.world(), arena.hero))
        << "mode 0: the seat is not held";
}

// respawn.cpp:446-469. Only a dead Living hero can hold its seat (not a
// live hero, nothing, or a dead non-living carrying a guy), and in a classic
// mode not while its character walks again in another body.
TEST(SemRespawn, only_a_dead_hero_whose_character_walks_nowhere_else_holds_its_seat)
{
    ClassicArena arena(og::sim::kRespawnModeHeroes);
    GameWorld& world = arena.world();
    EXPECT_FALSE(og::sim::respawn_retains_player_control(world, arena.hero))
        << "a live hero";
    EXPECT_FALSE(og::sim::respawn_retains_player_control(world, nullptr))
        << "nothing";
    walker* gem = world.add_fx_ob(Order::Treasure, FAMILY_STAIN);
    ASSERT_NE(nullptr, gem);
    gem->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    gem->set_dead(1);
    EXPECT_FALSE(og::sim::respawn_retains_player_control(world, gem))
        << "a dead non-living with a guy";

    kill(arena.hero);
    ASSERT_TRUE(og::sim::respawn_retains_player_control(world, arena.hero))
        << "control: a dead hero in mode 1";
    walker* dup = arena.fx.spawn_hero(FAMILY_SOLDIER, 0, 256, 320, 41);
    ASSERT_NE(nullptr, dup);
    EXPECT_FALSE(og::sim::respawn_retains_player_control(world, arena.hero))
        << "classic, live duplicate exists";
    kill(dup);
    EXPECT_TRUE(og::sim::respawn_retains_player_control(world, arena.hero))
        << "the duplicate died: the seat is held again";
}

// respawn.cpp:477. An undecided scripted match swallows a team wipe: the
// server never ends the level (ending stays 0); the same wipe on a world
// whose mode failed activation ends it.
TEST(SemRespawn, an_undecided_scripted_match_survives_a_team_wipe)
{
    for (bool mode_active : {true, false}) {
        SCOPED_TRACE(mode_active ? "scripted, undecided" : "mode failed activation");
        RespawnArena arena;
        arena.world().mode.active = mode_active;
        walker* runner = arena.runner;
        give_guy(runner, 81);
        runner->set_act_type(ACT_RANDOM);
        runner->set_user(-1);

        auto server_transport = og::sim::InProcessTransport::create_server();
        server_transport->accept_connections();
        auto client = server_transport->create_client_transport();
        og::sim::GameServer server(arena.world(), arena.fx.events, *server_transport);
        server.set_wall_clock_ms_source([] { return std::uint64_t{0}; });
        server.connect_client(client->local_peer_id());
        server.bind_player(client->local_peer_id(), 0u, 0);
        ASSERT_EQ(runner, server.player_control(0));

        kill(runner);
        for (int i = 0; i < 5; ++i)
            server.step();
        if (mode_active) {
            EXPECT_EQ(0, arena.world().ending) << "scripted undecided: no end";
            EXPECT_FALSE(arena.world().game_ended);
        } else {
            EXPECT_EQ(1, arena.world().ending) << "control: the wipe ends the level";
        }
    }
}

// respawn.cpp:529. A fire for a hero already alive again (a cleric beat
// the timer) does not heal it a second time.
TEST(SemRespawn, a_fire_for_a_hero_already_alive_does_not_heal_it_again)
{
    RespawnArena arena;
    walker* runner = arena.runner;
    give_guy(runner, 9);
    kill(runner);
    ASSERT_TRUE(arena.schedule(runner));
    runner->set_dead(0);
    runner->stats()->set_heal_per_round(0.0f);  // no regeneration either
    runner->stats()->set_max_heal_delay(1000000);
    runner->stats()->set_current_heal_delay(0);
    runner->stats()->set_hitpoints(3.0f);
    arena.fx.tick(7);
    EXPECT_FALSE(runner->dead());
    EXPECT_FLOAT_EQ(3.0f, runner->stats()->hitpoints())
        << "corpse already alive at fire: no re-heal";
    EXPECT_TRUE(arena.world().respawn.respawn_queue.empty())
        << "the entry is still consumed";
}

// respawn.cpp:587. A bot entry with no recorded spot (negative coordinates)
// fields nothing and queues no retry.
TEST(SemRespawn, a_bot_entry_without_a_spot_fields_nothing_and_never_retries)
{
    ClassicArena arena(og::sim::kRespawnModeEveryone);
    arena.world().respawn.respawn_queue.push_back(
        bot_entry(FAMILY_ARCHER, 1, -1, -1, 1, 0));
    arena.fx.tick();
    EXPECT_TRUE(arena.world().respawn.respawn_queue.empty())
        << "entry spot negative: no retry";
    EXPECT_EQ(0, count_alive_with(arena.world(), FAMILY_ARCHER))
        << "entry spot negative: nothing fielded";
}

// respawn.cpp:766-773. A scripted mode's anchors are teams 0-3's start
// markers, in list order, up to the per-team cap: a team-5 marker and the
// markers past the cap are ignored.
TEST(SemRespawn, respawn_anchors_take_score_team_markers_up_to_the_cap)
{
    ScriptedWorld fx;
    ASSERT_NE(nullptr, fx.spawn_anchor(5, 64, 64));
    for (int i = 0; i <= og::sim::kRespawnMaxAnchorsPerTeam; ++i)
        ASSERT_NE(nullptr, fx.spawn_anchor(0, 32 * i, 96));
    og::sim::respawn_scan_anchors(fx.world());
    const og::sim::RespawnState& r = fx.world().respawn;
    EXPECT_EQ(og::sim::kRespawnMaxAnchorsPerTeam, r.anchor_count[0])
        << "more than the cap: the count stops at the cap";
    EXPECT_EQ(0, r.anchor_x[0][0]) << "list order: the first marker first";
    EXPECT_EQ(32 * (og::sim::kRespawnMaxAnchorsPerTeam - 1),
              r.anchor_x[0][og::sim::kRespawnMaxAnchorsPerTeam - 1])
        << "the last kept is the cap-th marker; the one after it is dropped";
    EXPECT_EQ(96, r.anchor_y[0][og::sim::kRespawnMaxAnchorsPerTeam - 1]);
    for (int t = 1; t < 4; ++t)
        EXPECT_EQ(0, r.anchor_count[t]) << "a team-5 marker touches team " << t;
}

// respawn.cpp:790-810. The win flush revives an unqueued charmed hero on
// its true team, fields a fresh replacement for a queued bot, never calls
// on_respawn, and drops a blocked bot's retry.
TEST(SemRespawn, the_win_flush_fields_queued_bots_and_restores_true_teams_without_lua)
{
    {
        RespawnArena arena;
        walker* runner = arena.runner;
        give_guy(runner, 51);
        runner->set_real_team_num(2);
        runner->set_team_num(1);
        kill(runner);
        ASSERT_TRUE(arena.world().respawn.respawn_queue.empty());
        og::sim::respawn_flush_revive_all(arena.world());
        EXPECT_FALSE(runner->dead());
        EXPECT_EQ(2, runner->team_num()) << "charmed hero: on its true team";
        EXPECT_EQ(255, runner->real_team_num());
    }
    {
        RespawnArena arena;
        walker* bot = arena.fx.spawn_living(FAMILY_ARCHER, 1, 480, 700);
        ASSERT_NE(nullptr, bot);
        const std::uint32_t corpse_id = bot->entity_id();
        kill(bot);
        ASSERT_TRUE(arena.schedule(bot));
        og::sim::mode_declare_winner(arena.world(), 1);
        walker* fresh = find_alive_with(arena.world(), FAMILY_ARCHER, 1, corpse_id);
        ASSERT_NE(nullptr, fresh) << "queued bot: a fresh replacement is fielded";
        EXPECT_EQ(480, fresh->xpos());
        EXPECT_EQ(700, fresh->ypos());
        EXPECT_TRUE(arena.world().respawn.respawn_queue.empty());
    }
    {
        ModeWorld fx;
        fx.register_script(kRespawnLoggerScript);
        ASSERT_NE(nullptr, fx.spawn_living(FAMILY_ORC, 1, 480, 480));
        fx.tick();
        ASSERT_TRUE(fx.world().mode.active);
        walker* hero = fx.spawn_living(FAMILY_SOLDIER, 0, 224, 224);
        walker* bot = fx.spawn_living(FAMILY_SOLDIER, 0, 320, 320);
        ASSERT_TRUE(hero && bot);
        give_guy(hero, 52);
        kill(hero);
        kill(bot);
        ASSERT_TRUE(og::sim::respawn_schedule_corpse(fx.world(), hero, 0));
        ASSERT_TRUE(og::sim::respawn_schedule_corpse(fx.world(), bot, 0));
        ASSERT_NE(nullptr, fx.spawn_living(FAMILY_ORC, 1, 320, 320));
        og::sim::mode_declare_winner(fx.world(), 1);
        fx.tick();
        EXPECT_FALSE(hero->dead()) << "the queued hero is revived";
        EXPECT_EQ(0, fx.log_count("respawn_called"))
            << "scripted flush: no on_respawn even for queued entries";
        EXPECT_TRUE(fx.world().respawn.respawn_queue.empty())
            << "the blocked bot's retry is dropped";
    }
}
