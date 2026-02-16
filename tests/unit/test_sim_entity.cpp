// Headless tests for SimEntity (G4).
// These verify that SimEntity can be created and manipulated without SDL.

#include <openglad/sim/sim_entity.h>
#include <openglad/sim/sim_event_log.h>
#include "unit.h"

OG_UNIT_TEST(test_sim_entity_default_construction)
{
    og::sim::SimEntity e;
    OG_ASSERT(e.worldx == -1.0f);
    OG_ASSERT(e.worldy == -1.0f);
    OG_ASSERT(e.xpos == 0);
    OG_ASSERT(e.ypos == 0);
    OG_ASSERT(e.dead == 0);
    OG_ASSERT(e.user == -1);
    OG_ASSERT(e.team_num == 0);
    OG_ASSERT(e.real_team_num == 255);
    OG_ASSERT(e.sim_level == nullptr);
    OG_ASSERT(e.sim_events == nullptr);
}

OG_UNIT_TEST(test_sim_entity_set_position)
{
    og::sim::SimEntity e;
    e.worldx = 100.5f;
    e.worldy = 200.3f;
    e.xpos = 100;
    e.ypos = 200;
    e.sizex = 16;
    e.sizey = 16;

    OG_ASSERT(e.worldx == 100.5f);
    OG_ASSERT(e.worldy == 200.3f);
    OG_ASSERT(e.xpos == 100);
    OG_ASSERT(e.ypos == 200);
    OG_ASSERT(e.sizex == 16);
    OG_ASSERT(e.sizey == 16);
}

OG_UNIT_TEST(test_sim_entity_team_and_identity)
{
    og::sim::SimEntity e;
    e.order = 0;     // Order::Living
    e.family = 3;    // FAMILY_MAGE
    e.team_num = 1;
    e.real_team_num = 255;
    e.user = 0;      // Player 0

    OG_ASSERT(e.order == 0);
    OG_ASSERT(e.family == 3);
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
    og::sim::SimEntity e;
    e.sim_events = &log;

    OG_ASSERT(e.sim_events != nullptr);
    OG_ASSERT(e.sim_events == &log);

    // Verify we can emit events through the bound log
    e.sim_events->push(og::sim::EventKind::PlaySound, 42);
    OG_ASSERT(e.sim_events->size() == 1);
    OG_ASSERT(e.sim_events->events()[0].a == 42);
}
