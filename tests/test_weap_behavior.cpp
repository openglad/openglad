#include <openglad/entities/guy.h>
#include <openglad/runtime/guy_create.h>
#include <openglad/data/gloader.h>
#include <openglad/entities/walker.h>
#include <openglad/runtime/screen.h>
#include <openglad/legacy/base.h>
#include "test_framework.h"
#include <memory>

extern screen* myscreen;

static walker* make_weapon(char family)
{
    walker* w = myscreen->level_data.add_weap_ob(Order::Weapon, family);
    if (w) {
        w->setxy(100, 100);
        w->owner = w;
    }
    return w;
}

static std::unique_ptr<walker> make_living(char family, unsigned char team = 0)
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
// weap::act - various act types
// ---------------------------------------------------------------------------

void test_weap_act_fire()
{
    walker* w = make_weapon(FAMILY_KNIFE);
    if (!w) return;
    w->set_act_type(ACT_FIRE);
    w->lastx = 1;
    w->lasty = 0;
    w->act();
    myscreen->level_data.remove_ob(w);
}
REGISTER_TEST(test_weap_act_fire);

void test_weap_act_sit_tree()
{
    walker* w = make_weapon(FAMILY_TREE);
    if (!w) return;
    w->set_act_type(ACT_SIT);
    bool result = w->act();
    TEST_ASSERT(result, "tree sit returns 1");
    myscreen->level_data.remove_ob(w);
}
REGISTER_TEST(test_weap_act_sit_tree);

void test_weap_act_sit_blood()
{
    walker* w = make_weapon(FAMILY_BLOOD);
    if (!w) return;
    w->set_act_type(ACT_SIT);
    bool result = w->act();
    TEST_ASSERT(result, "blood sit returns 1");
    myscreen->level_data.remove_ob(w);
}
REGISTER_TEST(test_weap_act_sit_blood);

void test_weap_act_sit_door()
{
    walker* w = make_weapon(FAMILY_DOOR);
    if (!w) return;
    w->set_act_type(ACT_SIT);
    bool result = w->act();
    TEST_ASSERT(result, "door sit returns 1");
    myscreen->level_data.remove_ob(w);
}
REGISTER_TEST(test_weap_act_sit_door);

void test_weap_act_die()
{
    walker* w = make_weapon(FAMILY_KNIFE);
    if (!w) return;
    w->set_act_type(ACT_DIE);
    w->act();
    TEST_ASSERT(w->dead == 1, "weap act die sets dead");
    myscreen->level_data.remove_ob(w);
}
REGISTER_TEST(test_weap_act_die);

void test_weap_act_random()
{
    walker* w = make_weapon(FAMILY_KNIFE);
    if (!w) return;
    w->set_act_type(ACT_RANDOM);
    w->act();
    myscreen->level_data.remove_ob(w);
}
REGISTER_TEST(test_weap_act_random);

// ---------------------------------------------------------------------------
// weap::death - various weapon families
// ---------------------------------------------------------------------------

void test_weap_death_knife_soldier_owner()
{
    auto owner = make_living(FAMILY_SOLDIER, 0);
    if (!owner) return;

    walker* knife = make_weapon(FAMILY_KNIFE);
    if (!knife) return;
    knife->owner = owner.get();
    knife->dead = 1;
    knife->death();
    // Should create a KNIFE_BACK effect

    myscreen->level_data.remove_ob(knife);
}
REGISTER_TEST(test_weap_death_knife_soldier_owner);

void test_weap_death_knife_non_soldier()
{
    auto owner = make_living(FAMILY_ARCHER, 0);
    if (!owner) return;

    walker* knife = make_weapon(FAMILY_KNIFE);
    if (!knife) return;
    knife->owner = owner.get();
    knife->dead = 1;
    knife->death();
    // Should NOT create a KNIFE_BACK since owner is not soldier

    myscreen->level_data.remove_ob(knife);
}
REGISTER_TEST(test_weap_death_knife_non_soldier);

void test_weap_death_fire_arrow_exploding()
{
    auto owner = make_living(FAMILY_ARCHER, 0);
	    if (!owner) return;
	    
	    walker* arrow = make_weapon(FAMILY_FIRE_ARROW);
	    if (!arrow) return;
	    arrow->owner = owner.get();
	    arrow->skip_exit = 1; // means it's supposed to explode
	    arrow->dead = 1;
	    arrow->death();

    myscreen->level_data.remove_ob(arrow);
}
REGISTER_TEST(test_weap_death_fire_arrow_exploding);

void test_weap_death_fire_arrow_no_explode()
{
    walker* arrow = make_weapon(FAMILY_FIRE_ARROW);
    if (!arrow) return;
    arrow->skip_exit = 0; // not supposed to explode
    arrow->dead = 1;
    arrow->death();
    myscreen->level_data.remove_ob(arrow);
}
REGISTER_TEST(test_weap_death_fire_arrow_no_explode);

void test_weap_death_wave_transforms()
{
    walker* wave = make_weapon(FAMILY_WAVE);
    if (!wave) return;
    wave->dead = 1;
    wave->death();
    // Should transform to WAVE2 and un-dead
    TEST_ASSERT(wave->dead == 0, "wave should un-dead on transform");
    myscreen->level_data.remove_ob(wave);
}
REGISTER_TEST(test_weap_death_wave_transforms);

void test_weap_death_wave2_transforms()
{
    walker* wave = make_weapon(FAMILY_WAVE2);
    if (!wave) return;
    wave->dead = 1;
    wave->death();
    TEST_ASSERT(wave->dead == 0, "wave2 should un-dead on transform");
    myscreen->level_data.remove_ob(wave);
}
REGISTER_TEST(test_weap_death_wave2_transforms);

void test_weap_death_door()
{
    walker* door = make_weapon(FAMILY_DOOR);
    if (!door) return;
    door->dead = 1;
    door->death();
    myscreen->level_data.remove_ob(door);
}
REGISTER_TEST(test_weap_death_door);

void test_weap_death_rock_no_bounce()
{
    walker* rock = make_weapon(FAMILY_ROCK);
    if (!rock) return;
    // do_bounce is a member of weap, not walker base
    // Just test death with default state
    rock->dead = 1;
    rock->death();
    myscreen->level_data.remove_ob(rock);
}
REGISTER_TEST(test_weap_death_rock_no_bounce);

void test_weap_death_boulder_exploding()
{
    walker* boulder = make_weapon(FAMILY_BOULDER);
    if (!boulder) return;
    boulder->skip_exit = 1;
    boulder->dead = 1;
    boulder->death();
    myscreen->level_data.remove_ob(boulder);
}
REGISTER_TEST(test_weap_death_boulder_exploding);

// ---------------------------------------------------------------------------
// weap::animate
// ---------------------------------------------------------------------------

void test_weap_animate_knife()
{
    walker* w = make_weapon(FAMILY_KNIFE);
    if (!w) return;
    w->ani_type = ANI_ATTACK;
    w->animate();
    myscreen->level_data.remove_ob(w);
}
REGISTER_TEST(test_weap_animate_knife);

void test_weap_animate_arrow()
{
    walker* w = make_weapon(FAMILY_ARROW);
    if (!w) return;
    w->ani_type = ANI_ATTACK;
    w->animate();
    myscreen->level_data.remove_ob(w);
}
REGISTER_TEST(test_weap_animate_arrow);

void test_weap_act_clears_dead_refs_and_defaults_owner_and_tree_lineofsight()
{
    myscreen->level_data.create_new_grid();
    walker* w = make_weapon(FAMILY_KNIFE);
    auto dead_living = make_living(FAMILY_SOLDIER, 1);
    TEST_ASSERT(w && dead_living, "weapon and dead living created");
    if (!(w && dead_living))
        return;

    dead_living->dead = 1;
    w->foe = dead_living.get();
    w->leader = dead_living.get();
    w->owner = dead_living.get();
    w->setxy(0, 0);
    w->lineofsight = 5;
    myscreen->level_data.grid.data[0] = PIX_TREE_M1;
    w->set_act_type(ACT_RANDOM);

    (void)w->act();
    TEST_ASSERT(w->foe == nullptr && w->leader == nullptr, "dead foe/leader should be cleared");
    TEST_ASSERT(w->owner == w, "dead owner should be cleared then default to self");
    TEST_ASSERT_EQ(4, (int)w->lineofsight, "trees tile should decrement lineofsight");

    myscreen->level_data.remove_ob(w);
}
REGISTER_TEST(test_weap_act_clears_dead_refs_and_defaults_owner_and_tree_lineofsight);

void test_weap_act_control_generate_guard_and_default_paths()
{
    walker* control = make_weapon(FAMILY_KNIFE);
    walker* gen = make_weapon(FAMILY_KNIFE);
    walker* guard = make_weapon(FAMILY_KNIFE);
    walker* unknown = make_weapon(FAMILY_KNIFE);
    TEST_ASSERT(control && gen && guard && unknown, "weapons created");
    if (!(control && gen && guard && unknown))
        return;

    control->set_act_type(ACT_CONTROL);
    TEST_ASSERT(control->act(), "ACT_CONTROL should return true");

    gen->set_act_type(ACT_GENERATE);
    TEST_ASSERT(!gen->act(), "ACT_GENERATE path should fall through to return false");

    guard->set_act_type(ACT_GUARD);
    TEST_ASSERT(!guard->act(), "ACT_GUARD path should fall through to return false");

    unknown->set_act_type(123);
    TEST_ASSERT(!unknown->act(), "unknown act should return false");

    myscreen->level_data.remove_ob(control);
    myscreen->level_data.remove_ob(gen);
    myscreen->level_data.remove_ob(guard);
    myscreen->level_data.remove_ob(unknown);
}
REGISTER_TEST(test_weap_act_control_generate_guard_and_default_paths);

void test_weap_death_is_idempotent()
{
    walker* w = make_weapon(FAMILY_KNIFE);
    TEST_ASSERT(w != nullptr, "weapon created");
    if (!w)
        return;
    w->dead = 1;
    TEST_ASSERT(w->death(), "first death() call should succeed");
    TEST_ASSERT(!w->death(), "second death() call should short-circuit");
    myscreen->level_data.remove_ob(w);
}
REGISTER_TEST(test_weap_death_is_idempotent);
