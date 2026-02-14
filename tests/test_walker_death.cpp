#include <openglad/entities/guy.h>
#include <openglad/entities/walker.h>
#include <openglad/data/gloader.h>
#include <openglad/render/view.h>
#include <openglad/runtime/screen.h>
#include "test_framework.h"
#include <memory>

extern screen* myscreen;

static std::unique_ptr<walker> make_guy(char family, unsigned char team = 0, short level = 3)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(level, true);
    auto w = g.create_walker_owned(myscreen);
    if (w) w->setxy(100, 100);
    return w;
}

// ---------------------------------------------------------------------------
// walker::death - various family-specific death behaviors
// ---------------------------------------------------------------------------

void test_walker_death_soldier()
{
    auto w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->dead = 1;
    w->death();
}
REGISTER_TEST(test_walker_death_soldier);

void test_walker_death_mage()
{
    auto w = make_guy(FAMILY_MAGE, 0);
    if (!w) return;
    w->dead = 1;
    w->death();
}
REGISTER_TEST(test_walker_death_mage);

void test_walker_death_skeleton()
{
    auto w = make_guy(FAMILY_SKELETON, 0);
    if (!w) return;
    w->dead = 1;
    w->death();
}
REGISTER_TEST(test_walker_death_skeleton);

void test_walker_death_fire_elemental2()
{
    auto w = make_guy(FAMILY_FIREELEMENTAL, 0);
    if (!w) return;
    w->dead = 1;
    w->death();
    // Fire elemental death should create an explosion
}
REGISTER_TEST(test_walker_death_fire_elemental2);

void test_walker_death_small_slime()
{
    auto w = make_guy(FAMILY_SMALL_SLIME, 0);
    if (!w) return;
    w->dead = 1;
    w->death();
}
REGISTER_TEST(test_walker_death_small_slime);

void test_walker_death_medium_slime()
{
    auto w = myscreen->level_data.myloader->create_walker_owned(Order::Living, FAMILY_MEDIUM_SLIME, myscreen);
    if (!w) return;
    w->setxy(100, 100);
    w->dead = 1;
    w->death();
}
REGISTER_TEST(test_walker_death_medium_slime);

void test_walker_death_large_slime()
{
    auto w = myscreen->level_data.myloader->create_walker_owned(Order::Living, FAMILY_SLIME, myscreen);
    if (!w) return;
    w->setxy(100, 100);
    w->dead = 1;
    w->death();
}
REGISTER_TEST(test_walker_death_large_slime);

void test_walker_death_ghost()
{
    auto w = make_guy(FAMILY_GHOST, 0);
    if (!w) return;
    w->dead = 1;
    w->death();
}
REGISTER_TEST(test_walker_death_ghost);

void test_walker_death_faerie()
{
    auto w = make_guy(FAMILY_FAERIE, 0);
    if (!w) return;
    w->dead = 1;
    w->death();
}
REGISTER_TEST(test_walker_death_faerie);

void test_walker_death_myguy_present()
{
    auto w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    TEST_ASSERT(w->myguy != nullptr, "should have myguy from guy::create_walker_owned");
    w->dead = 1;
    w->death();
}
REGISTER_TEST(test_walker_death_myguy_present);

void test_walker_death_orc()
{
    auto w = make_guy(FAMILY_ORC, 1);
    if (!w) return;
    w->dead = 1;
    w->death();
}
REGISTER_TEST(test_walker_death_orc);

void test_walker_death_barbarian()
{
    auto w = make_guy(FAMILY_BARBARIAN, 1);
    if (!w) return;
    w->dead = 1;
    w->death();
}
REGISTER_TEST(test_walker_death_barbarian);

void test_walker_death_archer()
{
    auto w = make_guy(FAMILY_ARCHER, 0);
    if (!w) return;
    w->dead = 1;
    w->death();
}
REGISTER_TEST(test_walker_death_archer);

void test_walker_death_cleric()
{
    auto w = make_guy(FAMILY_CLERIC, 0);
    if (!w) return;
    w->dead = 1;
    w->death();
}
REGISTER_TEST(test_walker_death_cleric);

void test_walker_death_druid()
{
    auto w = make_guy(FAMILY_DRUID, 0);
    if (!w) return;
    w->dead = 1;
    w->death();
}
REGISTER_TEST(test_walker_death_druid);

void test_walker_death_thief()
{
    auto w = make_guy(FAMILY_THIEF, 0);
    if (!w) return;
    w->dead = 1;
    w->death();
}
REGISTER_TEST(test_walker_death_thief);

void test_walker_death_elf()
{
    auto w = make_guy(FAMILY_ELF, 0);
    if (!w) return;
    w->dead = 1;
    w->death();
}
REGISTER_TEST(test_walker_death_elf);

// ---------------------------------------------------------------------------
// walker::death double-call protection
// ---------------------------------------------------------------------------

void test_walker_death_double_call()
{
    auto w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->dead = 1;
    w->death();
    bool result = w->death();
    TEST_ASSERT(!result, "second death call returns false");
}
REGISTER_TEST(test_walker_death_double_call);

// ---------------------------------------------------------------------------
// walker::compute_outline
// ---------------------------------------------------------------------------

void test_walker_compute_outline_invulnerable()
{
    auto w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->invulnerable_left = 10;
    w->invisibility_left = 0;
    w->flight_left = 0;

    viewscreen* vs = myscreen->viewob[0].get();
    w->compute_outline(vs ? vs->control : nullptr);

}
REGISTER_TEST(test_walker_compute_outline_invulnerable);

void test_walker_compute_outline_flying()
{
    auto w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->flight_left = 10;
    w->invisibility_left = 0;
    w->invulnerable_left = 0;

    viewscreen* vs = myscreen->viewob[0].get();
    w->compute_outline(vs ? vs->control : nullptr);

}
REGISTER_TEST(test_walker_compute_outline_flying);

void test_walker_compute_outline_invisible()
{
    auto w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->invisibility_left = 10;
    w->flight_left = 0;
    w->invulnerable_left = 0;

    viewscreen* vs = myscreen->viewob[0].get();
    w->compute_outline(vs ? vs->control : nullptr);

}
REGISTER_TEST(test_walker_compute_outline_invisible);

void test_walker_compute_outline_no_status()
{
    auto w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->invisibility_left = 0;
    w->flight_left = 0;
    w->invulnerable_left = 0;

    viewscreen* vs = myscreen->viewob[0].get();
    w->compute_outline(vs ? vs->control : nullptr);

}
REGISTER_TEST(test_walker_compute_outline_no_status);
