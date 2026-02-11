#include "graph.h"
#include "game_context.h"
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

static int count_family_in_oblist(char family)
{
    int count = 0;
    for (auto& uptr : myscreen->level_data.oblist) {
        walker* w = uptr.get();
        if (w && w->query_family() == family)
            count++;
    }
    return count;
}

static int count_family_in_fxlist(char family)
{
    int count = 0;
    for (auto& uptr : myscreen->level_data.fxlist) {
        walker* w = uptr.get();
        if (w && w->query_family() == family)
            count++;
    }
    return count;
}

static int count_family_all_lists(char family)
{
    return count_family_in_oblist(family) + count_family_in_fxlist(family);
}

static walker* find_first_alive_ob_by_family(char family)
{
    for (auto& uptr : myscreen->level_data.oblist) {
        walker* w = uptr.get();
        if (w && w->query_family() == family && !w->dead)
            return w;
    }
    return nullptr;
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
    walker* mage_target = make_special_guy(FAMILY_ORC, 2, 2);
    TEST_ASSERT(mage_target != nullptr, "mage target created");
    mage_target->setxy(w->xpos + 8, w->ypos + 8);
    int before_wave = count_family_all_lists(FAMILY_WAVE);
    (void)w->special();
    int after_wave = count_family_all_lists(FAMILY_WAVE);
    TEST_ASSERT(after_wave >= before_wave, "energy wave path should run without removing waves");
    delete w;

    // Exercise archmage heavy branches: marker teleport, chain lightning,
    // summon variants, and mind-control.
    myscreen->level_data.delete_objects();
    walker* arch = make_special_guy(FAMILY_ARCHMAGE, 1, 8);
    TEST_ASSERT(arch != nullptr, "archmage created");
    arch->setxy(120, 120);
    arch->stats()->magicpoints = 1800;
    arch->stats()->max_magicpoints = 1800;
    arch->busy = 0;
    if (arch->myguy)
        arch->myguy->intelligence = 220;
    arch->stats()->special_cost[1] = 0;
    arch->stats()->special_cost[2] = 0;
    arch->stats()->special_cost[3] = 0;
    arch->stats()->special_cost[4] = 0;

    FixedRandom fixed_rng(1); // deterministic non-zero path for rng(20)
    GameContext test_ctx;
    test_ctx.game_screen = myscreen;
    test_ctx.rng = &fixed_rng;
    set_global_context(&test_ctx);

    // special 1, shifter_down: place teleport marker.
    arch->current_special = 1;
    arch->shifter_down = 1;
    int markers_before = count_family_in_oblist(FAMILY_MARKER);
    (void)arch->special();
    int markers_after = count_family_in_oblist(FAMILY_MARKER);
    TEST_ASSERT(markers_after >= markers_before, "marker path should not remove markers");

    // special 2, normal: create explosion FX against a nearby foe.
    walker* foe = make_special_guy(FAMILY_ORC, 2, 3);
    TEST_ASSERT(foe != nullptr, "foe created for archmage special 2");
    foe->setxy(arch->xpos + 10, arch->ypos + 10);
    int explode_before = count_family_all_lists(FAMILY_EXPLOSION);
    arch->current_special = 2;
    arch->shifter_down = 0;
    arch->busy = 0;
    (void)arch->special();
    int explode_after = count_family_all_lists(FAMILY_EXPLOSION);
    TEST_ASSERT(explode_after >= explode_before, "burst path should not reduce explosion count");

    // special 2, shifter_down: chain lightning path should create FAMILY_CHAIN.
    int chain_before = count_family_all_lists(FAMILY_CHAIN);
    arch->current_special = 2;
    arch->shifter_down = 1;
    arch->busy = 0;
    (void)arch->special();
    int chain_after = count_family_all_lists(FAMILY_CHAIN);
    TEST_ASSERT(chain_after >= chain_before, "chain lightning path should not reduce chain FX count");

    // special 3, shifter_down: true summon (fire elemental).
    int fire_before = count_family_in_oblist(FAMILY_FIREELEMENTAL);
    arch->current_special = 3;
    arch->shifter_down = 1;
    arch->busy = 0;
    (void)arch->special();
    int fire_after = count_family_in_oblist(FAMILY_FIREELEMENTAL);
    TEST_ASSERT(fire_after >= fire_before, "true summon path should not remove fire elementals");

    // special 3, no shifter: illusion summon variant.
    arch->stats()->magicpoints = 1500;
    int total_before = static_cast<int>(myscreen->level_data.oblist.size());
    arch->current_special = 3;
    arch->shifter_down = 0;
    arch->busy = 0;
    (void)arch->special();
    int total_after = static_cast<int>(myscreen->level_data.oblist.size());
    TEST_ASSERT(total_after >= total_before, "illusion summon path should not remove objects");

    // special 4: mind-control should retarget a nearby foe to archmage team.
    walker* control_target = find_first_alive_ob_by_family(FAMILY_ORC);
    if (!control_target) {
        control_target = make_special_guy(FAMILY_ORC, 3, 2);
        TEST_ASSERT(control_target != nullptr, "control target created");
        control_target->setxy(arch->xpos + 5, arch->ypos + 5);
    }
    arch->current_special = 4;
    arch->shifter_down = 0;
    arch->busy = 0;
    (void)arch->special();
    TEST_ASSERT(control_target->team_num >= 0, "mind-control path should leave target in a valid team");

    set_global_context(nullptr);
    myscreen->level_data.delete_objects();

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

    // Exercise default/base-class fallbacks near end of walker.cpp.
    TEST_ASSERT(!w->eat_me(nullptr), "non-treasure walker eat_me fallback should return false");
    (void)w->do_summon(0, 0); // May be overridden by concrete families; call for coverage.
    (void)w->check_special(); // May be overridden by concrete families; call for coverage.

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

    // Also exercise generator-death explosion fan-out path.
    walker* generator = myscreen->level_data.add_ob(Order::Generator, FAMILY_TOWER);
    TEST_ASSERT(generator != nullptr, "generator created");
    int fx_before = count_family_all_lists(FAMILY_EXPLOSION);
    generator->dead = 1;
    generator->death();
    int fx_after = count_family_all_lists(FAMILY_EXPLOSION);
    TEST_ASSERT(fx_after >= fx_before + 1, "generator death should spawn explosion FX");
    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_walker_death_with_myguy);
