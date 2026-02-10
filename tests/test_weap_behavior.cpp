#include "graph.h"
#include "guy.h"
#include "gloader.h"
#include "test_framework.h"

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

static walker* make_living(char family, unsigned char team = 0)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    walker* w = g.create_walker(myscreen);
    if (w) w->setxy(100, 100);
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
    walker* owner = make_living(FAMILY_SOLDIER, 0);
    if (!owner) return;

    walker* knife = make_weapon(FAMILY_KNIFE);
    if (!knife) { delete owner; return; }
    knife->owner = owner;
    knife->dead = 1;
    knife->death();
    // Should create a KNIFE_BACK effect

    delete owner;
    myscreen->level_data.remove_ob(knife);
}
REGISTER_TEST(test_weap_death_knife_soldier_owner);

void test_weap_death_knife_non_soldier()
{
    walker* owner = make_living(FAMILY_ARCHER, 0);
    if (!owner) return;

    walker* knife = make_weapon(FAMILY_KNIFE);
    if (!knife) { delete owner; return; }
    knife->owner = owner;
    knife->dead = 1;
    knife->death();
    // Should NOT create a KNIFE_BACK since owner is not soldier

    delete owner;
    myscreen->level_data.remove_ob(knife);
}
REGISTER_TEST(test_weap_death_knife_non_soldier);

void test_weap_death_fire_arrow_exploding()
{
    walker* owner = make_living(FAMILY_ARCHER, 0);
    if (!owner) return;

    walker* arrow = make_weapon(FAMILY_FIRE_ARROW);
    if (!arrow) { delete owner; return; }
    arrow->owner = owner;
    arrow->skip_exit = 1; // means it's supposed to explode
    arrow->dead = 1;
    arrow->death();

    delete owner;
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
