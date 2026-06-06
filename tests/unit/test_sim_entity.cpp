// Headless tests for SimEntity and walker (G4).
// These verify that SimEntity/walker can be created and manipulated without SDL.

#include <openglad/gameplay/sim_entity.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/irandom.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/statistics.h>
#include <gtest/gtest.h>

TEST(SimEntity, default_construction)
{
    og::sim::SimEntity e;
    ASSERT_TRUE(e.xpos() == 0);
    ASSERT_TRUE(e.ypos() == 0);
    ASSERT_TRUE(e.entity_id() == 0);
    ASSERT_TRUE(e.dead() == 0);
    ASSERT_TRUE(e.user() == -1);
    ASSERT_TRUE(e.team_num() == 0);
    ASSERT_TRUE(e.real_team_num() == 255);
}

TEST(SimEntity, set_position)
{
    og::sim::SimEntity e;
    e.set_xpos(100);
    e.set_ypos(200);
    e.set_sizex(16);
    e.set_sizey(16);

    ASSERT_TRUE(e.xpos() == 100);
    ASSERT_TRUE(e.ypos() == 200);
    ASSERT_TRUE(e.sizex() == 16);
    ASSERT_TRUE(e.sizey() == 16);
}

TEST(SimEntity, team_and_identity)
{
    og::sim::SimEntity e;
    e.set_team_num(1);
    e.set_real_team_num(255);
    e.set_user(0);      // Player 0

    ASSERT_TRUE(e.team_num() == 1);
    ASSERT_TRUE(e.user() == 0);
}

TEST(SimEntity, state_flags)
{
    og::sim::SimEntity e;
    e.set_invulnerable_left(30);
    e.set_flight_left(15);
    e.set_invisibility_left(0);
    e.set_bonus_rounds(2);
    e.set_dead(0);

    ASSERT_TRUE(e.invulnerable_left() == 30);
    ASSERT_TRUE(e.flight_left() == 15);
    ASSERT_TRUE(e.invisibility_left() == 0);
    ASSERT_TRUE(e.bonus_rounds() == 2);
    ASSERT_TRUE(!e.dead());

    e.set_dead(1);
    ASSERT_TRUE(e.dead());
}

TEST(SimEntity, event_log_binding)
{
    GameWorld world(7);
    GameplayContext game_ctx;
    game_ctx.world = &world;

    og::sim::SimEventLog log;
    game_ctx.sim_events = &log;

    GameplayContext* prev = current_game;
    current_game = &game_ctx;

    ASSERT_TRUE(current_game != nullptr);
    ASSERT_TRUE(current_game->world == &world);
    ASSERT_TRUE(current_game->sim_events == &log);

    current_game->sim_events->push(og::sim::EventKind::PlaySound, 42);
    ASSERT_TRUE(current_game->sim_events->size() == 1);
    ASSERT_TRUE(current_game->sim_events->events()[0].a == 42);

    current_game = prev;
}

// ---------------------------------------------------------------------------
// Headless walker creation tests (G4)
// Verify walker can be created without SDL, without pixieN rendering data.
// ---------------------------------------------------------------------------

TEST(SimEntity, walker_headless_construction)
{
    walker w;  // No PixieData — headless mode

    ASSERT_TRUE(w.xpos() == 0);
    ASSERT_TRUE(w.ypos() == 0);
    ASSERT_TRUE(w.dead() == 0);
    ASSERT_TRUE(w.user() == -1);
    ASSERT_TRUE(!w.has_render());
    ASSERT_TRUE(w.bmp_data() == nullptr);
    ASSERT_TRUE(w.render_component() == nullptr);
}

TEST(SimEntity, walker_headless_position_and_movement)
{
    walker w;
    w.setxy(100, 200);
    ASSERT_TRUE(w.xpos() == 100);
    ASSERT_TRUE(w.ypos() == 200);

    w.setxy(50, 75);
    ASSERT_TRUE(w.xpos() == 50);
    ASSERT_TRUE(w.ypos() == 75);
}

TEST(SimEntity, walker_headless_with_rng)
{
    GameWorld world(42);
    std::uint32_t val = world.rng_.next(100);
    ASSERT_TRUE(val < 100);
}

TEST(SimEntity, walker_headless_stats)
{
    walker w;
    statistics* st = w.stats();
    ASSERT_TRUE(st != nullptr);
    st->set_hitpoints(50);
    st->set_max_hitpoints(100);
    ASSERT_TRUE(st->hitpoints() == 50);
    ASSERT_TRUE(st->max_hitpoints() == 100);
}

TEST(SimEntity, walker_headless_frame_tracking)
{
    walker w;
    ASSERT_TRUE(w.frame() == 0);

    // set_frame validates against frames count; headless walker has 0 frames
    short result = w.set_frame(2);
    ASSERT_TRUE(result == 0);           // Should fail — no frames allocated
    ASSERT_TRUE(w.frame() == 0);  // Frame unchanged
}
