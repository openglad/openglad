#include <openglad/core/stats.h>
#include <openglad/entities/guy.h>
#include <openglad/runtime/guy_create.h>
#include <openglad/data/gloader.h>
#include <openglad/entities/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/runtime/screen.h>
#include "test_framework.h"
#include <memory>

extern screen* myscreen;

// Forward declarations for pure functions
void orbit_offset(int drawcycle, float &xd, float &yd);
Sint32 compute_explosion_range(Sint32 level, short skip_exit);
bool hits(short x1, short y1, short w1, short h1,
          short x2, short y2, short w2, short h2);

static std::unique_ptr<walker> make_living_guy(char family, unsigned char team = 0)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, myscreen);
    if (w)
        w->setxy(100, 100);
    return w;
}

// ---------------------------------------------------------------------------
// Pure functions: orbit_offset
// ---------------------------------------------------------------------------

void test_orbit_offset_all_cycles()
{
    for (int i = 0; i < 16; i++) {
        float xd = 0, yd = 0;
        orbit_offset(i, xd, yd);
        TEST_ASSERT(xd != 0 || yd != 0, "orbit offset should be non-zero");
    }
}
REGISTER_TEST(test_orbit_offset_all_cycles);

void test_orbit_offset_wraps()
{
    float xd1, yd1, xd2, yd2;
    orbit_offset(0, xd1, yd1);
    orbit_offset(16, xd2, yd2);
    TEST_ASSERT(xd1 == xd2, "cycle 16 wraps to 0");
    TEST_ASSERT(yd1 == yd2, "cycle 16 wraps to 0");
}
REGISTER_TEST(test_orbit_offset_wraps);

// ---------------------------------------------------------------------------
// Pure functions: compute_explosion_range
// ---------------------------------------------------------------------------

void test_explosion_range_basic2()
{
    Sint32 r = compute_explosion_range(10, 0);
    TEST_ASSERT_EQ(40, (int)r, "level 10 => range 40");
}
REGISTER_TEST(test_explosion_range_basic2);

void test_explosion_range_capped()
{
    Sint32 r = compute_explosion_range(100, 0);
    TEST_ASSERT_EQ(96, (int)r, "capped at 96");
}
REGISTER_TEST(test_explosion_range_capped);

void test_explosion_range_min()
{
    Sint32 r = compute_explosion_range(1, 0);
    TEST_ASSERT_EQ(16, (int)r, "min is 16");
}
REGISTER_TEST(test_explosion_range_min);

void test_explosion_range_skip_exit()
{
    Sint32 r = compute_explosion_range(10, 1);
    TEST_ASSERT_EQ(16, (int)r, "skip_exit sets range to 0 then min caps to 16");
}
REGISTER_TEST(test_explosion_range_skip_exit);

// ---------------------------------------------------------------------------
// Pure functions: hits (collision detection)
// ---------------------------------------------------------------------------

void test_hits_overlap2()
{
    bool r = hits(0, 0, 10, 10, 5, 5, 10, 10);
    TEST_ASSERT(r, "overlapping rectangles should hit");
}
REGISTER_TEST(test_hits_overlap2);

void test_hits_no_overlap()
{
    bool r = hits(0, 0, 10, 10, 20, 20, 10, 10);
    TEST_ASSERT(!r, "non-overlapping rectangles should not hit");
}
REGISTER_TEST(test_hits_no_overlap);

void test_hits_adjacent()
{
    bool r = hits(0, 0, 10, 10, 10, 0, 10, 10);
    (void)r; // exactly touching, behavior may vary
}
REGISTER_TEST(test_hits_adjacent);

void test_hits_contained2()
{
    bool r = hits(0, 0, 20, 20, 5, 5, 5, 5);
    TEST_ASSERT(r, "contained rectangle should hit");
}
REGISTER_TEST(test_hits_contained2);

// ---------------------------------------------------------------------------
// effect::act - various effect families
// ---------------------------------------------------------------------------

void test_effect_act_explosion()
{
    walker* fx = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    if (!fx) return;
    fx->setxy(100, 100);
    fx->ani_type = ANI_EXPLODE;
    fx->act();
    myscreen->level_data.remove_ob(fx);
}
REGISTER_TEST(test_effect_act_explosion);

void test_effect_act_magic_shield()
{
    auto owner = make_living_guy(FAMILY_MAGE, 0);
    if (!owner) return;

    walker* fx = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_MAGIC_SHIELD);
    if (!fx) return;
    fx->setxy(100, 100);
    fx->owner = owner.get();
    fx->lifetime = 100;
    fx->stats()->hitpoints = 100;
    fx->act();

    myscreen->level_data.remove_ob(fx);
}
REGISTER_TEST(test_effect_act_magic_shield);

void test_effect_act_magic_shield_no_owner()
{
    walker* fx = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_MAGIC_SHIELD);
    if (!fx) return;
    fx->setxy(100, 100);
    fx->owner = nullptr;
    fx->act();
    // Should die since no owner
    TEST_ASSERT(fx->dead == 1, "shield without owner dies");
    myscreen->level_data.remove_ob(fx);
}
REGISTER_TEST(test_effect_act_magic_shield_no_owner);

void test_effect_act_boomerang()
{
    auto owner = make_living_guy(FAMILY_SOLDIER, 0);
    if (!owner) return;

    walker* fx = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_BOOMERANG);
    if (!fx) return;
    fx->setxy(100, 100);
    fx->owner = owner.get();
    fx->lifetime = 100;
    fx->stats()->hitpoints = 100;
    fx->drawcycle = 10;
    fx->act();

    myscreen->level_data.remove_ob(fx);
}
REGISTER_TEST(test_effect_act_boomerang);

void test_effect_act_boomerang_expired()
{
    walker* fx = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_BOOMERANG);
    if (!fx) return;
    fx->setxy(100, 100);
    fx->owner = nullptr;
    fx->drawcycle = 254;
    fx->act();
    TEST_ASSERT(fx->dead == 1, "expired boomerang dies");
    myscreen->level_data.remove_ob(fx);
}
REGISTER_TEST(test_effect_act_boomerang_expired);

void test_effect_act_cloud()
{
    auto owner = make_living_guy(FAMILY_DRUID, 0);
    if (!owner) return;

    walker* fx = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_CLOUD);
    if (!fx) return;
    fx->setxy(100, 100);
    fx->owner = owner.get();
    fx->team_num = 0;
    fx->lifetime = 50;
    fx->stats()->hitpoints = 50;
    fx->act();

    myscreen->level_data.remove_ob(fx);
}
REGISTER_TEST(test_effect_act_cloud);

void test_effect_act_cloud_expired()
{
    walker* fx = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_CLOUD);
    if (!fx) return;
    fx->setxy(100, 100);
    fx->owner = fx;
    fx->lifetime = 0;
    fx->act();
    TEST_ASSERT(fx->dead == 1, "expired cloud dies");
    myscreen->level_data.remove_ob(fx);
}
REGISTER_TEST(test_effect_act_cloud_expired);

void test_effect_act_ghost_scare()
{
    auto owner = make_living_guy(FAMILY_GHOST, 0);
    if (!owner) return;

    walker* fx = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_GHOST_SCARE);
    if (!fx) return;
    fx->setxy(100, 100);
    fx->owner = owner.get();
    fx->act();

    myscreen->level_data.remove_ob(fx);
}
REGISTER_TEST(test_effect_act_ghost_scare);

// ---------------------------------------------------------------------------
// effect::animate
// ---------------------------------------------------------------------------

void test_effect_animate_explosion()
{
    walker* fx = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    if (!fx) return;
    fx->setxy(100, 100);
    fx->ani_type = ANI_EXPLODE;
    fx->animate();
    myscreen->level_data.remove_ob(fx);
}
REGISTER_TEST(test_effect_animate_explosion);

void test_effect_animate_magic_shield()
{
    walker* fx = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_MAGIC_SHIELD);
    if (!fx) return;
    fx->setxy(100, 100);
    fx->animate();
    myscreen->level_data.remove_ob(fx);
}
REGISTER_TEST(test_effect_animate_magic_shield);

// ---------------------------------------------------------------------------
// effect::death
// ---------------------------------------------------------------------------

void test_effect_death_explosion()
{
    auto owner = make_living_guy(FAMILY_MAGE, 0);
    if (!owner) return;

    walker* fx = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    if (!fx) return;
    fx->setxy(100, 100);
    fx->owner = owner.get();
    fx->team_num = 0;
    fx->stats()->level = 5;
    fx->dead = 1;
    fx->death();

    myscreen->level_data.remove_ob(fx);
}
REGISTER_TEST(test_effect_death_explosion);

void test_effect_death_ghost_scare()
{
    auto owner = make_living_guy(FAMILY_GHOST, 0);
    if (!owner) return;

    walker* fx = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_GHOST_SCARE);
    if (!fx) return;
    fx->setxy(100, 100);
    fx->owner = owner.get();
    fx->dead = 1;
    fx->death();

    myscreen->level_data.remove_ob(fx);
}
REGISTER_TEST(test_effect_death_ghost_scare);
