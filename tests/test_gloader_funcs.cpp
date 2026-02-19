#include <openglad/core/stats.h>
#include <openglad/entities/walker.h>
#include <openglad/render/pixien.h>
#include <openglad/entities/guy.h>
#include <openglad/data/gloader.h>
#include <openglad/legacy/base.h>
#include <openglad/runtime/screen.h>
#include "test_framework.h"

extern screen* myscreen;

// ---------------------------------------------------------------------------
// create_walker for all order+family combos
// exercises gloader's create_walker (the biggest function)
// ---------------------------------------------------------------------------

void test_gloader_create_living_all()
{
    loader* l = myscreen->level_data.myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");

    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        auto w = l->create_walker_owned(Order::Living, families[i]);
        TEST_ASSERT(w != nullptr, "create_walker should succeed for all living families");
        TEST_ASSERT(w->stats() != nullptr, "stats should exist");
        TEST_ASSERT(w->query_order() == Order::Living, "order should be Living");
        TEST_ASSERT_EQ((int)families[i], (int)w->query_family(), "family should match");
    }
}
REGISTER_TEST(test_gloader_create_living_all);

void test_gloader_create_weapon_families()
{
    loader* l = myscreen->level_data.myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");

    short weap_families[] = { FAMILY_KNIFE, FAMILY_ROCK, FAMILY_ARROW,
                              FAMILY_FIREBALL, FAMILY_LIGHTNING, FAMILY_METEOR };
    for (int i = 0; i < 6; i++) {
        auto w = l->create_walker_owned(Order::Weapon, weap_families[i]);
        if (w) {
            TEST_ASSERT(w->query_order() == Order::Weapon, "order should be Weapon");
        }
    }

    int total_created = 0;
    for (int fam = 0; fam < NUM_FAMILIES; fam++) {
        auto ww = l->create_walker_owned(Order::Weapon, static_cast<char>(fam));
        if (ww) {
            total_created++;
            TEST_ASSERT(ww->query_order() == Order::Weapon, "sweep: order should be Weapon");
        }
    }
    TEST_ASSERT(total_created > 0, "weapon sweep should create at least one object");
}
REGISTER_TEST(test_gloader_create_weapon_families);

void test_gloader_create_treasure()
{
    loader* l = myscreen->level_data.myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");

    auto w = l->create_walker_owned(Order::Treasure, FAMILY_STAIN);
    if (w) {
        TEST_ASSERT(w->query_order() == Order::Treasure, "order should be Treasure");
    }

    int total_created = 0;
    for (int fam = 0; fam < NUM_FAMILIES; fam++) {
        auto wt = l->create_walker_owned(Order::Treasure, static_cast<char>(fam));
        if (wt) {
            total_created++;
            TEST_ASSERT(wt->query_order() == Order::Treasure, "sweep: order should be Treasure");
        }
    }
    TEST_ASSERT(total_created > 0, "treasure sweep should create at least one object");
}
REGISTER_TEST(test_gloader_create_treasure);

void test_gloader_create_effect()
{
    loader* l = myscreen->level_data.myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");

    auto w = l->create_walker_owned(Order::FX, FAMILY_EXPLOSION);
    if (w) {
        TEST_ASSERT(w->query_order() == Order::FX, "order should be FX");
    }

    int total_created = 0;
    for (int fam = 0; fam < NUM_FAMILIES; fam++) {
        auto wf = l->create_walker_owned(Order::FX, static_cast<char>(fam));
        if (wf) {
            total_created++;
            TEST_ASSERT(wf->query_order() == Order::FX, "sweep: order should be FX");
        }
    }
    TEST_ASSERT(total_created > 0, "fx sweep should create at least one object");
}
REGISTER_TEST(test_gloader_create_effect);

void test_gloader_create_generator()
{
    loader* l = myscreen->level_data.myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");

    auto w = l->create_walker_owned(Order::Generator, FAMILY_TENT);
    if (w) {
        TEST_ASSERT(w->query_order() == Order::Generator, "order should be Generator");
    }

    int total_created = 0;
    for (int fam = 0; fam < NUM_FAMILIES; fam++) {
        auto wg = l->create_walker_owned(Order::Generator, static_cast<char>(fam));
        if (wg) {
            total_created++;
            TEST_ASSERT(wg->query_order() == Order::Generator, "sweep: order should be Generator");
        }
    }
    TEST_ASSERT(total_created > 0, "generator sweep should create at least one object");
}
REGISTER_TEST(test_gloader_create_generator);

// ---------------------------------------------------------------------------
// set_derived_stats
// ---------------------------------------------------------------------------

void test_gloader_set_derived_stats_all()
{
    loader* l = myscreen->level_data.myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");

    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        auto w = l->create_walker_owned(Order::Living, families[i]);
        if (w) {
            l->set_derived_stats(w.get(), Order::Living, families[i]);
            TEST_ASSERT(w->stats()->max_hitpoints > 0, "HP should be set");
        }
    }
}
REGISTER_TEST(test_gloader_set_derived_stats_all);

// ---------------------------------------------------------------------------
// set_walker (sets order/family on existing walker)
// ---------------------------------------------------------------------------

void test_gloader_set_walker()
{
    loader* l = myscreen->level_data.myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");

    auto w = l->create_walker_owned(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    walker* wp = w.get();

    myscreen->set_walker(wp, Order::Living, FAMILY_MAGE);
    TEST_ASSERT_EQ((int)FAMILY_MAGE, (int)wp->query_family(), "family should change to mage");

    const Order orders[] = {Order::Living, Order::Weapon, Order::Treasure, Order::FX, Order::Generator, Order::Special};
    for (Order o : orders) {
        for (int fam = 0; fam < NUM_FAMILIES; fam++) {
            if (!l->graphics[PIX(o, fam)].valid()) {
                continue;
            }
            walker* changed = l->set_walker(wp, o, static_cast<char>(fam));
            TEST_ASSERT(changed != nullptr, "set_walker should return object");
            TEST_ASSERT(changed->stats() != nullptr, "set_walker should leave stats valid");
        }
    }

    int pixie_created = 0;
    for (Order o : orders) {
        for (int fam = 0; fam < NUM_FAMILIES; fam++) {
            auto p = l->create_pixieN_owned(o, static_cast<char>(fam));
            if (p) {
                pixie_created++;
            }
        }
    }
    TEST_ASSERT(pixie_created > 0, "create_pixieN sweep should create objects");
}
REGISTER_TEST(test_gloader_set_walker);

void test_gloader_invalid_family_clamp_paths()
{
    loader* l = myscreen->level_data.myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");
    if (!l)
        return;

    auto living_w = l->create_walker_owned(Order::Living, NUM_FAMILIES + 5);
    TEST_ASSERT(living_w != nullptr, "invalid living family should fall back to soldier");
    if (!living_w)
        return;
    TEST_ASSERT_EQ((int)FAMILY_SOLDIER, (int)living_w->query_family(),
                   "invalid living family should clamp to soldier");

    auto weapon_w = l->create_walker_owned(Order::Weapon, NUM_FAMILIES + 5);
    TEST_ASSERT(weapon_w != nullptr, "invalid weapon family should clamp to 0 and still construct");
    if (weapon_w)
        TEST_ASSERT_EQ(0, (int)weapon_w->query_family(),
                       "invalid non-living family should clamp to family 0");

    l->set_derived_stats(living_w.get(), Order::Living, NUM_FAMILIES + 9);
    TEST_ASSERT(living_w->normal_stepsize >= 0.0f, "set_derived_stats should clamp invalid family safely");

    walker* changed = l->set_walker(living_w.get(), Order::Living, NUM_FAMILIES + 9);
    TEST_ASSERT(changed != nullptr, "set_walker should clamp invalid family and return object");
    TEST_ASSERT_EQ(0, (int)living_w->query_family(), "set_walker invalid family should clamp to 0");
}
REGISTER_TEST(test_gloader_invalid_family_clamp_paths);
