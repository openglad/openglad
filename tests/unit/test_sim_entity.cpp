// Headless tests for SimEntity and walker (G4).
// These verify that SimEntity/walker can be created and manipulated without SDL.

#include <openglad/sim/sim_entity.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/irandom.h>
#include <openglad/entities/walker.h>
#include <openglad/core/stats.h>
#include "unit.h"

OG_UNIT_TEST(test_sim_entity_default_construction)
{
    og::sim::SimEntity e;
    OG_ASSERT(e.xpos == 0);
    OG_ASSERT(e.ypos == 0);
    OG_ASSERT(e.dead == 0);
    OG_ASSERT(e.user == -1);
    OG_ASSERT(e.team_num == 0);
    OG_ASSERT(e.real_team_num == 255);
    OG_ASSERT(e.sim_save == nullptr);
    OG_ASSERT(e.sim_config == nullptr);
}

OG_UNIT_TEST(test_sim_entity_set_position)
{
    og::sim::SimEntity e;
    e.xpos = 100;
    e.ypos = 200;
    e.sizex = 16;
    e.sizey = 16;

    OG_ASSERT(e.xpos == 100);
    OG_ASSERT(e.ypos == 200);
    OG_ASSERT(e.sizex == 16);
    OG_ASSERT(e.sizey == 16);
}

OG_UNIT_TEST(test_sim_entity_team_and_identity)
{
    og::sim::SimEntity e;
    e.team_num = 1;
    e.real_team_num = 255;
    e.user = 0;      // Player 0

    OG_ASSERT(e.team_num == 1);
    OG_ASSERT(e.user == 0);
}

OG_UNIT_TEST(test_sim_entity_state_flags)
{
    og::sim::SimEntity e;
    e.invulnerable_left = 30;
    e.flight_left = 15;
    e.invisibility_left = 0;
    e.bonus_rounds = 2;
    e.dead = 0;

    OG_ASSERT(e.invulnerable_left == 30);
    OG_ASSERT(e.flight_left == 15);
    OG_ASSERT(e.invisibility_left == 0);
    OG_ASSERT(e.bonus_rounds == 2);
    OG_ASSERT(!e.dead);

    e.dead = 1;
    OG_ASSERT(e.dead);
}

OG_UNIT_TEST(test_sim_entity_event_log_binding)
{
    og::sim::SimEventLog log;
    log.push(og::sim::EventKind::PlaySound, 42);
    OG_ASSERT(log.size() == 1);
    OG_ASSERT(log.events()[0].a == 42);
}

// ---------------------------------------------------------------------------
// Headless walker creation tests (G4)
// Verify walker can be created without SDL, without pixieN rendering data.
// ---------------------------------------------------------------------------

OG_UNIT_TEST(test_walker_headless_construction)
{
    walker w;  // No PixieData — headless mode

    OG_ASSERT(w.xpos == 0);
    OG_ASSERT(w.ypos == 0);
    OG_ASSERT(w.dead == 0);
    OG_ASSERT(w.user == -1);
    OG_ASSERT(!w.has_render());
    OG_ASSERT(w.bmp_data() == nullptr);
    OG_ASSERT(w.render_component() == nullptr);
}

OG_UNIT_TEST(test_walker_headless_position_and_movement)
{
    walker w;
    w.setxy(100, 200);
    OG_ASSERT(w.xpos == 100);
    OG_ASSERT(w.ypos == 200);

    w.setxy(50, 75);
    OG_ASSERT(w.xpos == 50);
    OG_ASSERT(w.ypos == 75);
}

OG_UNIT_TEST(test_walker_headless_with_rng)
{
    SeededRandom rng(42);
    std::uint32_t val = rng.next(100);
    OG_ASSERT(val < 100);
}

OG_UNIT_TEST(test_walker_headless_stats)
{
    walker w;
    statistics* st = w.stats();
    OG_ASSERT(st != nullptr);
    st->hitpoints = 50;
    st->max_hitpoints = 100;
    OG_ASSERT(st->hitpoints == 50);
    OG_ASSERT(st->max_hitpoints == 100);
}

OG_UNIT_TEST(test_walker_headless_frame_tracking)
{
    walker w;
    OG_ASSERT(w.frame == 0);

    // set_frame validates against frames count; headless walker has 0 frames
    short result = w.set_frame(2);
    OG_ASSERT(result == 0);           // Should fail — no frames allocated
    OG_ASSERT(w.frame == 0);  // Frame unchanged
}
