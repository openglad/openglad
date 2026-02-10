#include "graph.h"
#include "guy.h"
#include "gloader.h"
#include "test_framework.h"

extern screen* myscreen;

static walker* make_special_guy(char family, unsigned char team = 0, int level = 3)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(level, true);
    walker* w = g.create_walker(myscreen);
    if (w) {
        w->setxy(100, 100);
        w->stats()->magicpoints = 500; // lots of magic for specials
        w->stats()->max_magicpoints = 500;
    }
    return w;
}

// ---------------------------------------------------------------------------
// special() - exercises the massive family switch (lines 2293-3909)
// Each family test covers a different switch case
// ---------------------------------------------------------------------------

void test_walker_special_soldier_charge()
{
    walker* w = make_special_guy(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->current_special = 1; // charge
    w->special();
    delete w;
}
REGISTER_TEST(test_walker_special_soldier_charge);

void test_walker_special_soldier_boomerang()
{
    walker* w = make_special_guy(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 2; // boomerang
    w->special();
    delete w;
}
REGISTER_TEST(test_walker_special_soldier_boomerang);

void test_walker_special_soldier_whirlwind()
{
    walker* w = make_special_guy(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 3; // whirlwind
    w->busy = 0;
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->special();
    delete w;
}
REGISTER_TEST(test_walker_special_soldier_whirlwind);

void test_walker_special_archer_fire_arrows()
{
    walker* w = make_special_guy(FAMILY_ARCHER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->current_special = 1; // fire arrows
    w->special();
    delete w;
}
REGISTER_TEST(test_walker_special_archer_fire_arrows);

void test_walker_special_archer_flurry()
{
    walker* w = make_special_guy(FAMILY_ARCHER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    w->current_special = 2; // flurry
    w->special();
    delete w;
}
REGISTER_TEST(test_walker_special_archer_flurry);

void test_walker_special_archer_exploding()
{
    walker* w = make_special_guy(FAMILY_ARCHER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    w->current_special = 3; // exploding arrows
    w->special();
    delete w;
}
REGISTER_TEST(test_walker_special_archer_exploding);

void test_walker_special_mage_teleport()
{
    walker* w = make_special_guy(FAMILY_MAGE);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 1; // teleport
    w->special();
    delete w;
}
REGISTER_TEST(test_walker_special_mage_teleport);

void test_walker_special_mage_freeze()
{
    walker* w = make_special_guy(FAMILY_MAGE);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 2; // freeze time
    w->special();
    delete w;
}
REGISTER_TEST(test_walker_special_mage_freeze);

void test_walker_special_mage_energy_wave()
{
    walker* w = make_special_guy(FAMILY_MAGE);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 3; // energy wave
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    w->special();
    delete w;
}
REGISTER_TEST(test_walker_special_mage_energy_wave);

void test_walker_special_cleric_heal()
{
    walker* w = make_special_guy(FAMILY_CLERIC);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 1; // heal
    w->shifter_down = 0;
    w->special();
    delete w;
}
REGISTER_TEST(test_walker_special_cleric_heal);

void test_walker_special_cleric_raise_undead()
{
    walker* w = make_special_guy(FAMILY_CLERIC);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 2; // raise undead
    w->special();
    delete w;
}
REGISTER_TEST(test_walker_special_cleric_raise_undead);

void test_walker_special_elf_rocks()
{
    walker* w = make_special_guy(FAMILY_ELF);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 1; // rocks
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    w->special();
    delete w;
}
REGISTER_TEST(test_walker_special_elf_rocks);

void test_walker_special_elf_speed()
{
    walker* w = make_special_guy(FAMILY_ELF);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 2; // speed
    w->special();
    delete w;
}
REGISTER_TEST(test_walker_special_elf_speed);

void test_walker_special_elf_heal()
{
    walker* w = make_special_guy(FAMILY_ELF);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 3; // nature heal
    w->special();
    delete w;
}
REGISTER_TEST(test_walker_special_elf_heal);

void test_walker_special_thief_stealth()
{
    walker* w = make_special_guy(FAMILY_THIEF);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 1; // stealth
    w->special();
    delete w;
}
REGISTER_TEST(test_walker_special_thief_stealth);

void test_walker_special_thief_taunt()
{
    walker* w = make_special_guy(FAMILY_THIEF);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 2; // taunt
    w->special();
    delete w;
}
REGISTER_TEST(test_walker_special_thief_taunt);

void test_walker_special_skeleton_tunnel()
{
    walker* w = make_special_guy(FAMILY_SKELETON);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 1; // tunnel
    w->special();
    delete w;
}
REGISTER_TEST(test_walker_special_skeleton_tunnel);

void test_walker_special_fireelemental_explode()
{
    walker* w = make_special_guy(FAMILY_FIREELEMENTAL);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 1; // explode
    w->special();
    delete w;
}
REGISTER_TEST(test_walker_special_fireelemental_explode);

void test_walker_special_faerie_charm()
{
    walker* w = make_special_guy(FAMILY_FAERIE);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 1; // charm
    w->special();
    delete w;
}
REGISTER_TEST(test_walker_special_faerie_charm);

void test_walker_special_druid_plant_tree()
{
    walker* w = make_special_guy(FAMILY_DRUID);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 1; // plant tree
    w->special();
    delete w;
}
REGISTER_TEST(test_walker_special_druid_plant_tree);

void test_walker_special_druid_summon()
{
    walker* w = make_special_guy(FAMILY_DRUID);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 2; // summon animal
    w->special();
    delete w;
}
REGISTER_TEST(test_walker_special_druid_summon);

void test_walker_special_ghost_scare()
{
    walker* w = make_special_guy(FAMILY_GHOST);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 1; // scare
    w->special();
    delete w;
}
REGISTER_TEST(test_walker_special_ghost_scare);

void test_walker_special_orc_howl()
{
    walker* w = make_special_guy(FAMILY_ORC);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 1; // howl
    w->special();
    delete w;
}
REGISTER_TEST(test_walker_special_orc_howl);

void test_walker_special_barbarian_hurl()
{
    walker* w = make_special_guy(FAMILY_BARBARIAN);
    TEST_ASSERT(w != nullptr, "walker created");
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    w->current_special = 1; // hurl boulder
    w->special();
    delete w;
}
REGISTER_TEST(test_walker_special_barbarian_hurl);

// ---------------------------------------------------------------------------
// special() when dead, no stats, or not enough magic
// ---------------------------------------------------------------------------

void test_walker_special_dead()
{
    walker* w = make_special_guy(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->dead = 1;
    bool result = w->special();
    TEST_ASSERT(!result, "dead walker should not special");
    w->dead = 0; // so destructor works
    delete w;
}
REGISTER_TEST(test_walker_special_dead);

void test_walker_special_no_magic()
{
    walker* w = make_special_guy(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->stats()->magicpoints = 0;
    bool result = w->special();
    TEST_ASSERT(!result, "no magic should fail special");
    delete w;
}
REGISTER_TEST(test_walker_special_no_magic);

// ---------------------------------------------------------------------------
// death() - exercises order/family switches (lines 4422-4534)
// ---------------------------------------------------------------------------

void test_walker_death_fire_elemental()
{
    walker* w = make_special_guy(FAMILY_FIREELEMENTAL, 1);
    TEST_ASSERT(w != nullptr, "walker created");
    w->dead = 1;
    w->death();
    delete w;
}
REGISTER_TEST(test_walker_death_fire_elemental);

void test_walker_death_with_myguy()
{
    walker* w = make_special_guy(FAMILY_SOLDIER, 0);
    TEST_ASSERT(w != nullptr, "walker created");
    TEST_ASSERT(w->myguy != nullptr, "should have myguy");
    w->dead = 1;
    w->death();
    delete w;
}
REGISTER_TEST(test_walker_death_with_myguy);
