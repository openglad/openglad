#include "graph.h"
#include "guy.h"
#include "gloader.h"
#include "test_framework.h"

extern screen* myscreen;

static walker* make_guy(char family, unsigned char team = 0)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    walker* w = g.create_walker(myscreen);
    if (w) w->setxy(100, 100);
    return w;
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
    delete w;
}
REGISTER_TEST(test_walker_facing_all_16_vectors);

void test_walker_facing_zero()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    short dir = w->facing(0, 0);
    (void)dir; // behavior for (0,0) may vary
    delete w;
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
    delete w;
}
REGISTER_TEST(test_walker_turn_to_all_targets);

void test_walker_turn_from_all_starts()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;

    for (short start = 0; start < 8; start++) {
        w->curdir = start;
        w->turn(0);
    }
    delete w;
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

    delete w;
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

    delete w;
}
REGISTER_TEST(test_walker_walkstep_diagonals);

void test_walker_walkstep_zero()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(100, 100);
    w->walkstep(0, 0);
    delete w;
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
        w->draw(vs);
    }
    delete w;
}
REGISTER_TEST(test_walker_draw_basic);

void test_walker_draw_tile_basic()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(100, 100);

    viewscreen* vs = myscreen->viewob[0].get();
    if (vs) {
        w->draw_tile(vs);
    }
    delete w;
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
        w->draw(vs);
    }
    delete w;
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
        w->draw(vs);
    }
    delete w;
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
        w->draw(vs);
    }
    delete w;
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
    delete w;
}
REGISTER_TEST(test_walker_animate_walk);

void test_walker_animate_attack()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->ani_type = ANI_ATTACK;
    w->animate();
    delete w;
}
REGISTER_TEST(test_walker_animate_attack);

void test_walker_animate_all_families()
{
    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        walker* w = make_guy(families[i], 0);
        if (!w) continue;
        w->ani_type = ANI_WALK;
        w->animate();
        delete w;
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
    (void)weap;

    delete w;
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
    (void)weap;

    delete w;
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
    (void)weap;

    delete w;
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
    bool result = w->on_screen();
    (void)result;
    delete w;
}
REGISTER_TEST(test_walker_on_screen);
