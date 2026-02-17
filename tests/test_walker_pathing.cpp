#include <openglad/entities/guy.h>
#include <openglad/runtime/guy_create.h>
#include <openglad/entities/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/render/view.h>
#include <openglad/render/walker_draw.h>
#include <openglad/runtime/screen.h>
#include "test_framework.h"

extern screen* myscreen;

static walker* make_guy(char family, unsigned char team)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, myscreen);
    if (w)
        w->setxy(32, 32);
    return w.release();
}

void test_walker_pathfinding_follow_and_draw_path_smoke()
{
    viewscreen* v = myscreen->viewob[0].get();
    TEST_ASSERT(v != nullptr, "viewob[0] should exist");

    walker* a = make_guy(FAMILY_SOLDIER, 0);
    walker* b = make_guy(FAMILY_ORC, 1);
    TEST_ASSERT(a != nullptr, "attacker should be created");
    TEST_ASSERT(b != nullptr, "target should be created");

    a->foe = b;
    b->setxy(96, 32);

    a->find_path_to_foe();
    // Solve can fail depending on map/obstacles; main goal is to execute logic.
    if (!a->path_to_foe.empty())
    {
        // Follow a few nodes.
        for (int i = 0; i < 5; i++)
            a->follow_path_to_foe();

        draw_walker_path(*a, v);
    }

    delete a;
    delete b;
}
REGISTER_TEST(test_walker_pathfinding_follow_and_draw_path_smoke);

void test_walker_damage_numbers_and_compute_outline_smoke()
{
    viewscreen* v = myscreen->viewob[0].get();
    TEST_ASSERT(v != nullptr, "viewob[0] should exist");

    walker* w = make_guy(FAMILY_SOLDIER, 0);
    TEST_ASSERT(w != nullptr, "walker should be created");

    // Damage numbers only draw for the controlling walker.
    v->control = w;
    w->damage_numbers.emplace_back(w->xpos, w->ypos, 12.0f, 55);

    // Outline mode changes based on these fields.
    w->invisibility_left = 1;
    w->invulnerable_left = 1;
    w->flight_left = 1;
    w->compute_outline(v->control);

    // Draw should update and consume damage numbers over time.
    (void)draw_walker(*w, v);

    // Restore view control.
    v->control = nullptr;
}
REGISTER_TEST(test_walker_damage_numbers_and_compute_outline_smoke);
