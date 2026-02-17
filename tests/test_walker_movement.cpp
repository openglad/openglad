#include <openglad/entities/guy.h>
#include <openglad/runtime/guy_create.h>
#include <openglad/data/gloader.h>
#include <openglad/entities/walker.h>
#include <openglad/core/stats.h>
#include <openglad/runtime/screen.h>
#include <openglad/render/view.h>
#include <openglad/render/walker_draw.h>
#include <openglad/legacy/base.h>
#include "test_framework.h"

extern screen* myscreen;

static walker* make_guy(char family, unsigned char team = 0)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, myscreen);
    if (w) w->setxy(100, 100);
    return w.release();
}

// ---------------------------------------------------------------------------
// walker::facing - comprehensive direction testing
// ---------------------------------------------------------------------------

void test_walker_facing_all_16_vectors()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;

    // Test all 8 quadrants plus cardinal directions
    struct { short x; short y; } dirs[] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1},
        {2, 1}, {1, 2}, {-2, 1}, {-1, 2},
        {2, -1}, {1, -2}, {-2, -1}, {-1, -2}
    };
    for (auto& d : dirs) {
        short dir = w->facing(d.x, d.y);
        TEST_ASSERT(dir >= 0 && dir < 8, "facing should be 0-7");
    }
}
REGISTER_TEST(test_walker_facing_all_16_vectors);

void test_walker_facing_zero()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    short dir = w->facing(0, 0);
    (void)dir; // behavior for (0,0) may vary
}
REGISTER_TEST(test_walker_facing_zero);

// ---------------------------------------------------------------------------
// walker::turn - exercises the turning logic
// ---------------------------------------------------------------------------

void test_walker_turn_to_all_targets()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;

    for (short target = 0; target < 8; target++) {
        w->curdir = 0;
        w->turn(target);
    }
}
REGISTER_TEST(test_walker_turn_to_all_targets);

void test_walker_turn_from_all_starts()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;

    for (short start = 0; start < 8; start++) {
        w->curdir = static_cast<char>(start);
        w->turn(0);
    }
}
REGISTER_TEST(test_walker_turn_from_all_starts);

// ---------------------------------------------------------------------------
// walker::walkstep - movement logic
// ---------------------------------------------------------------------------

void test_walker_walkstep_cardinals()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(100, 100);

    w->walkstep(1, 0);
    w->walkstep(-1, 0);
    w->walkstep(0, 1);
    w->walkstep(0, -1);

}
REGISTER_TEST(test_walker_walkstep_cardinals);

void test_walker_walkstep_diagonals()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(100, 100);

    w->walkstep(1, 1);
    w->walkstep(-1, 1);
    w->walkstep(1, -1);
    w->walkstep(-1, -1);

}
REGISTER_TEST(test_walker_walkstep_diagonals);

void test_walker_walkstep_zero()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(100, 100);
    w->walkstep(0, 0);

    // Blocked movement near map edge (npc path).
    w->setxy(0, 0);
    w->user = -1;
    (void)w->walkstep(-1, 0);
    (void)w->walkstep(0, -1);
    (void)w->walkstep(-1, -1);

    // User slide path on blocked diagonal movement.
    w->setxy(0, 10);
    w->user = 0;
    (void)w->walkstep(-1, -1);
    (void)w->walkstep(-1, 1);

}
REGISTER_TEST(test_walker_walkstep_zero);

// ---------------------------------------------------------------------------
// walker::draw and walker::draw_tile via viewscreen
// ---------------------------------------------------------------------------

void test_walker_draw_basic()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(100, 100);

    viewscreen* vs = myscreen->viewob[0].get();
    if (vs) {
        draw_walker(*w, vs);
    }
}
REGISTER_TEST(test_walker_draw_basic);

void test_walker_draw_tile_basic()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(100, 100);

    viewscreen* vs = myscreen->viewob[0].get();
    if (vs) {
        walker* old_control = vs->control;
        walker* control = make_guy(FAMILY_SOLDIER, 0);
        if (control) {
            control->setxy(96, 96);
            vs->control = control;
        }

        draw_walker_tile(*w, vs);

        // draw_tile invisibility path (requires non-null control).
        w->invisibility_left = 12;
        draw_walker_tile(*w, vs);
        w->invisibility_left = 0;

        // draw_tile outline path.
        w->invulnerable_left = 10;
        draw_walker_tile(*w, vs);
        w->invulnerable_left = 0;

        vs->control = old_control;
        delete control;
    }
}
REGISTER_TEST(test_walker_draw_tile_basic);

void test_walker_draw_with_flight()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(100, 100);
    w->flight_left = 10;

    viewscreen* vs = myscreen->viewob[0].get();
    if (vs) {
        draw_walker(*w, vs);
    }
}
REGISTER_TEST(test_walker_draw_with_flight);

void test_walker_draw_with_invisibility()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(100, 100);
    w->invisibility_left = 10;

    viewscreen* vs = myscreen->viewob[0].get();
    if (vs) {
        walker* old_control = vs->control;
        walker* control = make_guy(FAMILY_SOLDIER, 1);
        if (control) {
            control->setxy(96, 96);
            vs->control = control;
        }
        draw_walker(*w, vs);
        w->compute_outline(vs->control);
        w->flight_left = 8;
        w->compute_outline(vs->control);
        w->invulnerable_left = 8;
        w->compute_outline(vs->control);
        vs->control = old_control;
        delete control;
    }
}
REGISTER_TEST(test_walker_draw_with_invisibility);

void test_walker_draw_with_invulnerability()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(100, 100);
    w->invulnerable_left = 10;

    viewscreen* vs = myscreen->viewob[0].get();
    if (vs) {
        draw_walker(*w, vs);
    }
}
REGISTER_TEST(test_walker_draw_with_invulnerability);

// ---------------------------------------------------------------------------
// walker::animate - different animation types
// ---------------------------------------------------------------------------

void test_walker_animate_walk()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->ani_type = ANI_WALK;
    w->animate();
}
REGISTER_TEST(test_walker_animate_walk);

void test_walker_animate_attack()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->ani_type = ANI_ATTACK;
    w->animate();
}
REGISTER_TEST(test_walker_animate_attack);

void test_walker_animate_all_families()
{
    char families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        walker* w = make_guy(families[i], 0);
        if (!w) continue;
        w->ani_type = ANI_WALK;
        w->animate();
    }
}
REGISTER_TEST(test_walker_animate_all_families);

// ---------------------------------------------------------------------------
// walker::create_weapon
// ---------------------------------------------------------------------------

void test_walker_create_weapon_soldier()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(100, 100);
    w->lastx = 1;
    w->lasty = 0;

    walker* weap = w->fire();
    if (weap) {
        myscreen->level_data.remove_ob(weap);
    }

}
REGISTER_TEST(test_walker_create_weapon_soldier);

void test_walker_create_weapon_archer()
{
    walker* w = make_guy(FAMILY_ARCHER, 0);
    if (!w) return;
    w->setxy(100, 100);
    w->lastx = 1;
    w->lasty = 0;

    walker* weap = w->fire();
    if (weap) {
        myscreen->level_data.remove_ob(weap);
    }

}
REGISTER_TEST(test_walker_create_weapon_archer);

void test_walker_create_weapon_mage()
{
    walker* w = make_guy(FAMILY_MAGE, 0);
    if (!w) return;
    w->setxy(100, 100);
    w->lastx = 0;
    w->lasty = 1;

    walker* weap = w->fire();
    if (weap) {
        myscreen->level_data.remove_ob(weap);
    }

}
REGISTER_TEST(test_walker_create_weapon_mage);

// ---------------------------------------------------------------------------
// walker on_screen
// ---------------------------------------------------------------------------

void test_walker_on_screen()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(100, 100);
    // on_screen() is a render-layer method on pixie, not walker.
    // Verify walker position is set correctly instead.
    TEST_ASSERT(w->xpos == 100, "xpos set");
    TEST_ASSERT(w->ypos == 100, "ypos set");
}
REGISTER_TEST(test_walker_on_screen);

void test_walker_draw_tile_phantom_and_forestwalk_paths()
{
    walker* w = make_guy(FAMILY_ELF, 0);
    TEST_ASSERT(w != nullptr, "walker created");
    w->setxy(96, 96);

    viewscreen* vs = myscreen->viewob[0].get();
    TEST_ASSERT(vs != nullptr, "viewscreen exists");
    if (vs) {
        walker* old_control = vs->control;
        walker* control = make_guy(FAMILY_SOLDIER, 0);
        if (control) {
            control->setxy(80, 80);
            vs->control = control;
        }

        // PHANTOM draw_tile branch.
        w->stats()->set_bit_flags(BIT_PHANTOM, 1);
        (void)draw_walker_tile(*w, vs);
        w->stats()->set_bit_flags(BIT_PHANTOM, 0);

        // FORESTWALK draw_tile branch.
        int tx = w->xpos / GRID_SIZE;
        int ty = w->ypos / GRID_SIZE;
        if (tx >= 0 && ty >= 0 && tx < myscreen->level_data.grid.w && ty < myscreen->level_data.grid.h) {
            myscreen->level_data.grid.data[ty * myscreen->level_data.grid.w + tx] = PIX_TREE_T1;
            myscreen->level_data.mysmoother.set_target(myscreen->level_data.grid);
        }
        w->flight_left = 0;
        w->stats()->set_bit_flags(BIT_FLYING, 0);
        (void)draw_walker_tile(*w, vs);

        vs->control = old_control;
        delete control;
    }

}
REGISTER_TEST(test_walker_draw_tile_phantom_and_forestwalk_paths);
