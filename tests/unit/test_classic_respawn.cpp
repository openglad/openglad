// Classic (non-CTF) respawn engine tests: death scheduling, requested-delay
// resolution, spawn-point revives (clear / blocked / cross-floor), mode-2 AI
// respawns with blocked retry, generator/summon skips, the end-of-level
// revive-all, the team-wipe suppression predicates, CTF non-interference,
// and the GameServer control reclaim after a classic revive.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/ctf/ctf_state.h>
#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gloader_ctf.h>

#include "test_game_world_fixture.h"

#include <algorithm>
#include <cstdint>
#include <memory>

namespace {

loader& classic_test_loader()
{
    static loader instance{EntityFactory{}};
    static const bool registered = [] {
        register_ctf_loader_entries(instance);
        return true;
    }();
    (void)registered;
    return instance;
}

// Classic world: same loader wiring as the CTF fixtures, but the world type
// stays 0 — the classic respawn engine gates on !(type & TYPE_CTF).
struct ClassicWorld : TestGameWorld
{
    explicit ClassicWorld(int level_id = 3)
        : TestGameWorld(level_id)
    {
        loader* game_loader = &classic_test_loader();
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
    }

    walker* spawn_living(int family, int team, int x, int y)
    {
        walker* w = world().add_ob(Order::Living, family);
        if (w == nullptr)
            return nullptr;
        w->setxy(static_cast<short>(x), static_cast<short>(y));
        w->set_team_num(static_cast<unsigned char>(team));
        w->set_real_team_num(255);
        w->set_act_type(ACT_CONTROL); // stationary without input
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

    void kill(walker* w)
    {
        w->set_dead(1);
        w->death();
    }

    void tick(int count = 1)
    {
        for (int i = 0; i < count; ++i)
            world().tick();
    }
};

// Standard classic arena: one hero (team 0, spawn point recorded) and one
// stationary enemy (team 1) that keeps the level from auto-completing.
struct ClassicArena
{
    ClassicWorld fx;
    walker* hero = nullptr;  // team 0, myguy
    walker* enemy = nullptr; // team 1, unowned AI

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
        {
            return w;
        }
    }
    return nullptr;
}

// Fill floor 1 with grass so cross-floor spawn probes have legal terrain
// (mirrors the test_zaxis helper; PixieData takes buffer ownership).
void fill_floor1_grass(GameWorld& w)
{
    const int gw = w.grid.w;
    const int gh = w.grid.h;
    auto* buf = new unsigned char[static_cast<std::size_t>(gw) * gh];
    std::fill(buf, buf + static_cast<std::size_t>(gw) * gh,
              static_cast<unsigned char>(PIX_GRASS1));
    w.grid_for_floor(1) = PixieData(1, static_cast<unsigned char>(gw),
                                    static_cast<unsigned char>(gh), buf);
    w.smoother_for_floor(1).set_target(w.grid_for_floor(1));
}

} // namespace

TEST(ClassicRespawn, predicate_and_suppression_truth_table)
{
    ClassicWorld fx;
    GameWorld& w = fx.world();

    // Classic world, mode off: nothing active, nothing suppressed.
    w.respawn_mode = 0;
    EXPECT_FALSE(og::sim::classic_respawn_active(w));
    EXPECT_FALSE(og::sim::respawn_suppress_team_wipe_endgame(w));

    // Classic world, heroes / everyone: active, team wipe suppressed.
    w.respawn_mode = 1;
    EXPECT_TRUE(og::sim::classic_respawn_active(w));
    EXPECT_TRUE(og::sim::respawn_suppress_team_wipe_endgame(w));
    w.respawn_mode = 2;
    EXPECT_TRUE(og::sim::classic_respawn_active(w));
    EXPECT_TRUE(og::sim::respawn_suppress_team_wipe_endgame(w));

    // CTF world type: the classic engine never activates, even with the
    // mode set. An inactive CTF match suppresses nothing.
    w.type = GameWorld::TYPE_CTF;
    w.respawn_mode = 2;
    EXPECT_FALSE(og::sim::classic_respawn_active(w));
    EXPECT_FALSE(og::sim::respawn_suppress_team_wipe_endgame(w));

    // Active undecided CTF match: CTF suppression carries through the
    // combined predicate; a latched winner releases it.
    w.ctf.active = true;
    w.ctf.winner_team = -1;
    EXPECT_FALSE(og::sim::classic_respawn_active(w));
    EXPECT_TRUE(og::sim::respawn_suppress_team_wipe_endgame(w));
    w.ctf.winner_team = 0;
    EXPECT_FALSE(og::sim::respawn_suppress_team_wipe_endgame(w));
}

TEST(ClassicRespawn, hero_scheduled_on_death_and_revives_at_spawn_point)
{
    ClassicArena arena(1);
    walker* hero = arena.hero;
    const std::uint32_t corpse_id = hero->entity_id();

    arena.fx.kill(hero);
    arena.fx.tick();
    ASSERT_EQ(1u, arena.world().ctf.respawn_queue.size());
    const og::sim::CtfRespawnEntry& entry = arena.world().ctf.respawn_queue[0];
    ASSERT_EQ(0, entry.kind);
    ASSERT_EQ(corpse_id, entry.walker_entity_id);
    ASSERT_EQ(12, entry.ticks_left);
    ASSERT_EQ(128, entry.x) << "entry records the corpse's spawn point";
    ASSERT_EQ(128, entry.y);
    ASSERT_EQ(0, entry.floor);

    arena.fx.tick(11);
    ASSERT_TRUE(hero->dead()) << "timer must run out before the revive";

    arena.fx.tick();
    ASSERT_FALSE(hero->dead());
    ASSERT_EQ(corpse_id, hero->entity_id()) << "same walker, same entity id";
    ASSERT_EQ(hero->stats()->max_hitpoints(), hero->stats()->hitpoints());
    ASSERT_EQ(0, hero->user()) << "control binding survives the revive";
    ASSERT_EQ(128, hero->xpos()) << "clear spawn point pulls the hero home";
    ASSERT_EQ(128, hero->ypos());
    ASSERT_EQ(0, hero->floor());
    ASSERT_TRUE(arena.world().ctf.respawn_queue.empty());
}

TEST(ClassicRespawn, hero_revives_in_place_when_spawn_point_is_blocked)
{
    ClassicArena arena(1);
    walker* hero = arena.hero;
    // A living parked exactly on the recorded spawn point vetoes the move.
    walker* blocker = arena.fx.spawn_living(FAMILY_SOLDIER, 1, 128, 128);
    ASSERT_NE(nullptr, blocker);

    arena.fx.kill(hero);
    arena.fx.tick(13);

    ASSERT_FALSE(hero->dead());
    ASSERT_EQ(320, hero->xpos()) << "blocked spawn point revives in place";
    ASSERT_EQ(320, hero->ypos());
}

TEST(ClassicRespawn, entry_records_death_position_without_a_spawn_point)
{
    ClassicWorld fx;
    fx.world().respawn_mode = 1;
    fx.world().ctf_requested_respawn_ticks = 12;
    walker* hero = fx.spawn_hero(FAMILY_SOLDIER, 0, 320, 320, 7);
    ASSERT_NE(nullptr, hero);
    ASSERT_EQ(-1, hero->spawn_x()) << "spawn point defaults to unset";
    walker* enemy = fx.spawn_living(FAMILY_SOLDIER, 1, 480, 760);
    ASSERT_NE(nullptr, enemy);

    fx.kill(hero);
    fx.tick();
    ASSERT_EQ(1u, fx.world().ctf.respawn_queue.size());
    ASSERT_EQ(320, fx.world().ctf.respawn_queue[0].x)
        << "no spawn point: the entry records where the corpse fell";
    ASSERT_EQ(320, fx.world().ctf.respawn_queue[0].y);

    fx.tick(12);
    ASSERT_FALSE(hero->dead());
    ASSERT_EQ(320, hero->xpos()) << "no spawn point: revive where it fell";
    ASSERT_EQ(320, hero->ypos());
}

TEST(ClassicRespawn, requested_delay_resolution_clamps_out_of_range_values)
{
    struct Case
    {
        short requested;
        int expected_ticks;
    };
    const Case cases[] = {
        {0, og::sim::kCtfDefaultRespawnTicks},    // unset -> default
        {24, 24},                                 // in [12, 1200] -> honored
        {6, og::sim::kCtfDefaultRespawnTicks},    // below min -> default
        {2000, og::sim::kCtfDefaultRespawnTicks}, // above max -> default
    };
    for (const Case& c : cases)
    {
        ClassicArena arena(1, c.requested);
        arena.fx.kill(arena.hero);
        arena.fx.tick();
        ASSERT_EQ(1u, arena.world().ctf.respawn_queue.size())
            << "requested=" << c.requested;
        EXPECT_EQ(c.expected_ticks,
                  static_cast<int>(arena.world().ctf.respawn_queue[0].ticks_left))
            << "requested=" << c.requested;
    }
}

TEST(ClassicRespawn, mode2_respawns_fresh_ai_walker_at_authored_spot)
{
    ClassicArena arena(2);
    walker* bot = arena.fx.spawn_living(FAMILY_ARCHER, 1, 480, 700);
    ASSERT_NE(nullptr, bot);
    bot->stats()->set_level(2);
    bot->set_spawn_point(224, 128, 0);
    const std::uint32_t old_id = bot->entity_id();

    arena.fx.kill(bot);
    arena.fx.tick();
    ASSERT_EQ(1u, arena.world().ctf.respawn_queue.size());
    ASSERT_EQ(1, arena.world().ctf.respawn_queue[0].kind);
    ASSERT_EQ(224, arena.world().ctf.respawn_queue[0].x);
    ASSERT_EQ(128, arena.world().ctf.respawn_queue[0].y);

    arena.fx.tick(12);
    walker* fresh = find_alive_with(arena.world(), FAMILY_ARCHER, 1);
    ASSERT_NE(nullptr, fresh);
    ASSERT_NE(old_id, fresh->entity_id()) << "AI respawn is a fresh walker";
    ASSERT_EQ(2, fresh->stats()->level());
    ASSERT_EQ(255, fresh->real_team_num());
    ASSERT_EQ(224, fresh->xpos()) << "authored placement, not an anchor";
    ASSERT_EQ(128, fresh->ypos());
    ASSERT_EQ(224, fresh->spawn_x())
        << "the replacement inherits the spawn point for later deaths";
    ASSERT_EQ(128, fresh->spawn_y());
    ASSERT_TRUE(arena.world().ctf.respawn_queue.empty());
}

TEST(ClassicRespawn, mode2_blocked_ai_respawn_retries_until_clear)
{
    ClassicArena arena(2);
    walker* bot = arena.fx.spawn_living(FAMILY_ARCHER, 1, 480, 700);
    ASSERT_NE(nullptr, bot);
    bot->set_spawn_point(224, 128, 0);
    walker* blocker = arena.fx.spawn_living(FAMILY_SOLDIER, 1, 224, 128);
    ASSERT_NE(nullptr, blocker);

    arena.fx.kill(bot);
    arena.fx.tick(13); // schedule + full 12-tick countdown: fire is blocked

    ASSERT_EQ(nullptr, find_alive_with(arena.world(), FAMILY_ARCHER, 1))
        << "blocked spot must not place the fresh walker";
    ASSERT_EQ(1u, arena.world().ctf.respawn_queue.size())
        << "blocked fire re-enqueues the entry";
    const og::sim::CtfRespawnEntry& retry = arena.world().ctf.respawn_queue[0];
    ASSERT_EQ(1, retry.kind);
    ASSERT_GT(retry.ticks_left, 0);
    ASSERT_LE(static_cast<int>(retry.ticks_left),
              static_cast<int>(og::sim::kClassicBlockedRetryTicks));

    blocker->setxy(600, 600); // clear the spot
    arena.fx.tick(static_cast<int>(og::sim::kClassicBlockedRetryTicks));
    walker* fresh = find_alive_with(arena.world(), FAMILY_ARCHER, 1);
    ASSERT_NE(nullptr, fresh);
    EXPECT_EQ(224, fresh->xpos());
    EXPECT_EQ(128, fresh->ypos());
    EXPECT_TRUE(arena.world().ctf.respawn_queue.empty());
}

TEST(ClassicRespawn, mode1_does_not_respawn_unowned_ai)
{
    ClassicArena arena(1);
    walker* bot = arena.fx.spawn_living(FAMILY_ARCHER, 1, 480, 700);
    ASSERT_NE(nullptr, bot);

    arena.fx.kill(bot);
    arena.fx.tick();

    ASSERT_TRUE(arena.world().ctf.respawn_queue.empty())
        << "mode 1 respawns heroes only";
}

TEST(ClassicRespawn, generator_owned_and_summoned_walkers_never_respawn)
{
    ClassicArena arena(2);
    walker* summon = arena.fx.spawn_living(FAMILY_SKELETON, 1, 480, 700);
    ASSERT_NE(nullptr, summon);
    summon->set_owner(arena.enemy);

    summon->set_dead(1);
    arena.fx.tick();

    ASSERT_TRUE(arena.world().ctf.respawn_queue.empty())
        << "walkers with a live owner are the owner's business";
}

TEST(ClassicRespawn, end_of_level_revives_pending_heroes_and_clears_queue)
{
    // Long delay (default 120) so nothing fires on its own before the win.
    // Mode 1: a pending HERO is friendly and never holds the completion
    // decision open — mode 2's pending hostile AI does (see
    // mode2_pending_hostile_ai_blocks_extermination_win).
    ClassicArena arena(1, 0);
    walker* hero = arena.hero;

    arena.fx.kill(hero);
    arena.fx.tick();
    ASSERT_EQ(1u, arena.world().ctf.respawn_queue.size());
    ASSERT_FALSE(arena.world().game_ended)
        << "the surviving enemy keeps the level open";

    arena.fx.kill(arena.enemy);
    arena.fx.tick();

    ASSERT_TRUE(arena.world().game_ended);
    ASSERT_EQ(0, arena.world().ending);
    ASSERT_EQ(static_cast<short>(arena.world().id + 1),
              arena.world().next_level);
    ASSERT_FALSE(hero->dead())
        << "the win-tick revive-all must run before roster persistence";
    ASSERT_EQ(128, hero->xpos());
    ASSERT_EQ(128, hero->ypos());
    ASSERT_TRUE(arena.world().ctf.respawn_queue.empty());
}

TEST(ClassicRespawn, hero_revives_across_floors_at_its_spawn_floor)
{
    ClassicArena arena(1);
    GameWorld& w = arena.world();
    w.set_floor_count(2);
    fill_floor1_grass(w);
    walker* hero = arena.hero;
    hero->set_spawn_point(128, 128, 1); // home is on the upper floor

    arena.fx.kill(hero);
    arena.fx.tick(13);

    ASSERT_FALSE(hero->dead());
    EXPECT_EQ(1, hero->floor()) << "revive honors the recorded spawn floor";
    EXPECT_EQ(128, hero->xpos());
    EXPECT_EQ(128, hero->ypos());
}

TEST(ClassicRespawn, ctf_worlds_ignore_respawn_mode)
{
    // Full CTF arena with respawn_mode set: the CTF engine stays
    // authoritative — AI respawns keep rotating to team anchors and ignore
    // the entry's recorded coordinates.
    ClassicWorld fx;
    GameWorld& w = fx.world();
    w.type = GameWorld::TYPE_CTF;
    w.respawn_mode = 2;
    w.ctf_requested_respawn_ticks = 6;

    walker* flag0 = w.add_fx_ob(Order::Treasure, og::FAMILY_FLAG);
    ASSERT_NE(nullptr, flag0);
    flag0->setxy(96, 96);
    flag0->set_team_num(0);
    walker* flag1 = w.add_fx_ob(Order::Treasure, og::FAMILY_FLAG);
    ASSERT_NE(nullptr, flag1);
    flag1->setxy(544, 800);
    flag1->set_team_num(1);
    walker* anchor0 = w.add_ob(Order::Special, FAMILY_RESERVED_TEAM);
    ASSERT_NE(nullptr, anchor0);
    anchor0->setxy(128, 128);
    anchor0->set_team_num(0);
    walker* anchor1 = w.add_ob(Order::Special, FAMILY_RESERVED_TEAM);
    ASSERT_NE(nullptr, anchor1);
    anchor1->setxy(512, 832);
    anchor1->set_team_num(1);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 320, 320);
    ASSERT_NE(nullptr, runner);
    walker* bot = fx.spawn_living(FAMILY_ARCHER, 1, 480, 700);
    ASSERT_NE(nullptr, bot);
    bot->set_spawn_point(480, 700, 0);

    fx.tick();
    ASSERT_TRUE(w.ctf.active);
    ASSERT_FALSE(og::sim::classic_respawn_active(w))
        << "TYPE_CTF worlds never activate the classic engine";

    fx.kill(bot);
    fx.tick();
    ASSERT_EQ(1u, w.ctf.respawn_queue.size());
    ASSERT_EQ(480, w.ctf.respawn_queue[0].x)
        << "the shared scheduler still records the coordinates";

    fx.tick(6);
    walker* fresh = find_alive_with(w, FAMILY_ARCHER, 1);
    ASSERT_NE(nullptr, fresh);
    EXPECT_EQ(512, fresh->xpos())
        << "CTF fire paths ignore the entry coordinates: anchor placement";
    EXPECT_EQ(832, fresh->ypos());
}

TEST(ClassicRespawn, server_reclaims_control_after_classic_revive)
{
    ClassicWorld fx;
    fx.world().respawn_mode = 1;
    fx.world().ctf_requested_respawn_ticks = 12;
    walker* hero = fx.spawn_hero(FAMILY_SOLDIER, 0, 320, 320, 77);
    ASSERT_NE(nullptr, hero);
    hero->set_user(-1);
    hero->set_act_type(ACT_RANDOM);
    hero->set_spawn_point(128, 128, 0);
    walker* enemy = fx.spawn_living(FAMILY_SOLDIER, 1, 480, 760);
    ASSERT_NE(nullptr, enemy);

    auto server_transport = og::sim::InProcessTransport::create_server();
    server_transport->accept_connections();
    auto client = server_transport->create_client_transport();

    og::sim::GameServer server(fx.world(), fx.events, *server_transport);
    server.connect_client(client->local_peer_id());
    server.bind_player(client->local_peer_id(), 0u, 0);
    ASSERT_EQ(hero, server.player_control(0));
    ASSERT_EQ(0, hero->user());

    fx.kill(hero);
    bool reclaimed = false;
    for (int i = 0; i < 40 && !reclaimed; ++i)
    {
        server.step();
        reclaimed = (server.player_control(0) == hero && !hero->dead());
    }

    ASSERT_TRUE(reclaimed)
        << "the server must rebind the revived walker to its player";
    EXPECT_EQ(0, fx.world().ending)
        << "an active classic respawn mode suppresses the team-wipe endgame";
    EXPECT_GT(fx.world().tick_count_, 13u)
        << "the authoritative world must keep ticking through the death";
    EXPECT_EQ(0, hero->user());
    EXPECT_EQ(128, hero->xpos());
    EXPECT_EQ(128, hero->ypos());
}

// --- Endless battle (mode 2) completion semantics ---------------------------

TEST(ClassicRespawn, mode2_pending_hostile_ai_blocks_extermination_win)
{
    ClassicArena arena(2); // 12-tick delay
    GameWorld& w = arena.world();
    arena.enemy->set_spawn_point(480, 760, 0);
    arena.fx.kill(arena.enemy);

    // The kill tick: only the corpse scan can see the foe (the death scan
    // runs AFTER the completion decision, so no queue entry exists yet).
    arena.fx.tick();
    ASSERT_FALSE(w.game_ended)
        << "a corpse the death scan will queue is still a foe";
    ASSERT_EQ(1u, w.ctf.respawn_queue.size());

    // Queued ticks, the respawn itself, and the fresh foe: never a win.
    for (int i = 0; i < 30; ++i)
    {
        arena.fx.tick();
        ASSERT_FALSE(w.game_ended) << "tick " << i;
    }
    ASSERT_NE(nullptr, find_alive_with(w, FAMILY_SOLDIER, 1))
        << "the foe respawned at its authored spot and the battle goes on";
}

TEST(ClassicRespawn, end_driven_completion_flushes_and_drops_pending_ai)
{
    // Long delay (default 120): nothing fires on its own.
    ClassicArena arena(2, 0);
    GameWorld& w = arena.world();
    arena.enemy->set_spawn_point(480, 760, 0);
    arena.fx.kill(arena.hero);
    arena.fx.kill(arena.enemy);
    arena.fx.tick();
    ASSERT_FALSE(w.game_ended)
        << "extermination cannot win past the pending hostile AI";
    ASSERT_EQ(2u, w.ctf.respawn_queue.size());

    // The session layer ends the level (world.end): the end-of-level
    // revive-all must cover this end shape too.
    w.end = 1;
    arena.fx.tick();
    ASSERT_TRUE(w.game_ended);
    ASSERT_FALSE(arena.hero->dead())
        << "the end-driven completion still revives pending heroes";
    ASSERT_TRUE(w.ctf.respawn_queue.empty());
    ASSERT_EQ(nullptr, find_alive_with(w, FAMILY_SOLDIER, 1))
        << "pending AI entries are dropped at level end";
}

TEST(ClassicRespawn, same_tick_mutual_kill_still_revives_the_hero)
{
    // Hero and the last foe die on the same tick: the win latches (a pending
    // FRIENDLY hero never holds it open) and the flush's death scan must
    // schedule the fresh hero corpse before firing — scan-then-revive,
    // matching CTF's death-scan-before-win-check ordering.
    ClassicArena arena(1, 0);
    arena.fx.kill(arena.hero);
    arena.fx.kill(arena.enemy);
    arena.fx.tick();

    ASSERT_TRUE(arena.world().game_ended);
    ASSERT_EQ(0, arena.world().ending);
    ASSERT_FALSE(arena.hero->dead())
        << "a hero dying on the winning tick is revived, not persisted dead";
    ASSERT_TRUE(arena.world().ctf.respawn_queue.empty());
}

TEST(ClassicRespawn, runtime_spawns_without_authored_placement_never_respawn)
{
    // TOWER/TREEHOUSE generator emissions are orphaned at creation
    // (clear_owner), so the owner()==nullptr check alone would adopt them as
    // PERMANENT mode-2 respawners; summons in the one-tick owner-nulled
    // window would be adopted the same way. Neither carries a level-authored
    // spawn point, so the spawn_x() gate keeps "endless battle" scoped to
    // authored walkers — and placement-less corpses do not hold the
    // extermination win open either.
    ClassicArena arena(2);
    GameWorld& w = arena.world();
    walker* emission = arena.fx.spawn_living(FAMILY_SKELETON, 1, 480, 700);
    ASSERT_NE(nullptr, emission);
    ASSERT_EQ(-1, emission->spawn_x()) << "runtime spawns record no placement";

    arena.fx.kill(emission);
    arena.fx.tick();
    ASSERT_TRUE(w.ctf.respawn_queue.empty())
        << "an unowned runtime spawn (no spawn point) is never adopted";
    ASSERT_FALSE(w.game_ended) << "the authored enemy is still alive";

    arena.fx.kill(arena.enemy); // the fixture enemy has no spawn point either
    arena.fx.tick();
    ASSERT_TRUE(w.ctf.respawn_queue.empty());
    ASSERT_TRUE(w.game_ended)
        << "corpses without a placement do not hold the extermination win";
}

TEST(ClassicRespawn, pending_hostile_foe_truth_table)
{
    ClassicWorld fx;
    GameWorld& w = fx.world();

    og::sim::CtfRespawnEntry ai_other{};
    ai_other.kind = 1;
    ai_other.team = 1;
    og::sim::CtfRespawnEntry hero_own{};
    hero_own.kind = 0;
    hero_own.team = 0;
    og::sim::CtfRespawnEntry hero_other{};
    hero_other.kind = 0;
    hero_other.team = 2;

    w.ctf.respawn_queue.push_back(ai_other);
    w.respawn_mode = 0;
    EXPECT_FALSE(og::sim::classic_respawn_pending_hostile_foe(w))
        << "engine off";
    w.respawn_mode = 1;
    EXPECT_FALSE(og::sim::classic_respawn_pending_hostile_foe(w))
        << "kind-1 entries only count in mode 2";
    w.respawn_mode = 2;
    EXPECT_TRUE(og::sim::classic_respawn_pending_hostile_foe(w))
        << "pending hostile AI";
    w.allied_mode = 1;
    EXPECT_TRUE(og::sim::classic_respawn_pending_hostile_foe(w))
        << "PVP seating mode does not change AI hostility";
    w.allied_mode = 0;

    w.ctf.respawn_queue.clear();
    w.ctf.respawn_queue.push_back(hero_own);
    EXPECT_FALSE(og::sim::classic_respawn_pending_hostile_foe(w))
        << "own-team hero";
    w.ctf.respawn_queue.push_back(hero_other);
    w.allied_mode = 0;
    EXPECT_TRUE(og::sim::classic_respawn_pending_hostile_foe(w))
        << "an enemy player's pending hero is still a foe";
    w.allied_mode = 1;
    w.my_team = 3;
    EXPECT_TRUE(og::sim::classic_respawn_pending_hostile_foe(w))
        << "different-color company heroes stay hostile in every seat mode";
}

TEST(ClassicRespawn, timeout_end_shape_flushes_pending_heroes)
{
    struct TickLimitGuard
    {
        ~TickLimitGuard() { og::sim::g_test_level_tick_limit_override = 0; }
    } guard;
    og::sim::g_test_level_tick_limit_override = 30;

    ClassicArena arena(1, 0);
    arena.fx.kill(arena.hero);
    arena.fx.tick(29);
    ASSERT_FALSE(arena.world().game_ended);

    arena.fx.tick(5);
    ASSERT_TRUE(arena.world().game_ended);
    ASSERT_EQ(1, arena.world().ending) << "the timeout is a loss";
    ASSERT_FALSE(arena.hero->dead())
        << "the results screen shows revived heroes, not mid-respawn corpses";
    ASSERT_TRUE(arena.world().ctf.respawn_queue.empty());
}

TEST(ClassicRespawn, flush_pending_covers_the_synchronous_exit_accept_shape)
{
    // The exit-accept path (GameServer::handle_exit_prompt_response) calls
    // the flush directly, with NO world tick between the accept and the
    // roster persist. A hero dead-and-queued AND a hero dead-but-unscanned
    // must both come back; the flush is idempotent for the display mirrors
    // that repeat it.
    ClassicArena arena(1, 0);
    arena.fx.kill(arena.hero);
    arena.fx.tick(); // scheduled; the default 120-tick delay never fires here
    walker* late = arena.fx.spawn_hero(FAMILY_SOLDIER, 0, 352, 352, 55);
    ASSERT_NE(nullptr, late);
    late->set_spawn_point(160, 160, 0);
    arena.fx.kill(late); // no tick: no queue entry yet

    og::sim::classic_respawn_flush_pending(arena.world());

    EXPECT_FALSE(arena.hero->dead());
    EXPECT_FALSE(late->dead()) << "the flush death-scans before it fires";
    EXPECT_TRUE(arena.world().ctf.respawn_queue.empty());

    og::sim::classic_respawn_flush_pending(arena.world());
    EXPECT_TRUE(arena.world().ctf.respawn_queue.empty()) << "idempotent";
}

TEST(ClassicRespawn, full_queue_blocked_evict_keeps_the_incoming_corpse)
{
    // 500-tick delay: no crafted entry fires during the test.
    ClassicArena arena(2, 500);
    GameWorld& w = arena.world();
    // Park a blocker on every crafted entry's recorded spot, so the cap
    // evict-fire is blocked and re-enqueues itself.
    walker* blocker = arena.fx.spawn_living(FAMILY_SOLDIER, 1, 224, 128);
    ASSERT_NE(nullptr, blocker);
    for (int i = 0; i < og::sim::kCtfMaxRespawnEntries; ++i)
    {
        og::sim::CtfRespawnEntry entry{};
        entry.kind = 1;
        entry.team = 1;
        entry.family = FAMILY_ARCHER;
        entry.level = 1;
        entry.ticks_left = 500;
        entry.x = 224;
        entry.y = 128;
        entry.floor = 0;
        w.ctf.respawn_queue.push_back(entry);
    }

    walker* bot = arena.fx.spawn_living(FAMILY_ARCHER, 1, 480, 700);
    ASSERT_NE(nullptr, bot);
    bot->set_spawn_point(320, 512, 0);
    const std::uint32_t corpse_id = bot->entity_id();
    arena.fx.kill(bot);
    arena.fx.tick();

    ASSERT_EQ(static_cast<std::size_t>(og::sim::kCtfMaxRespawnEntries),
              w.ctf.respawn_queue.size())
        << "the serializer's queue cap holds";
    EXPECT_EQ(corpse_id, w.ctf.respawn_queue.back().walker_entity_id)
        << "the incoming corpse wins the slot";
    int crafted = 0;
    for (const og::sim::CtfRespawnEntry& entry : w.ctf.respawn_queue)
    {
        if (entry.x == 224)
            ++crafted;
    }
    EXPECT_EQ(og::sim::kCtfMaxRespawnEntries - 1, crafted)
        << "the blocked evict retry is the entry dropped, not the corpse";
}
