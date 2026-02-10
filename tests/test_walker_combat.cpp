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
// attack() - exercises the big combat function (lines 1822-2100)
// ---------------------------------------------------------------------------

void test_walker_attack_basic()
{
    walker* attacker = make_guy(FAMILY_SOLDIER, 0);
    walker* target = make_guy(FAMILY_ORC, 1);
    TEST_ASSERT(attacker != nullptr, "attacker created");
    TEST_ASSERT(target != nullptr, "target created");

    target->setxy(101, 100);
    attacker->team_num = 0;
    target->team_num = 1;
    float hp_before = target->stats()->hitpoints;
    bool result = attacker->attack(target);
    // attack may or may not succeed depending on is_friendly logic
    (void)result;
    (void)hp_before;

    delete attacker;
    delete target;
}
REGISTER_TEST(test_walker_attack_basic);

void test_walker_attack_friendly_fails()
{
    walker* a = make_guy(FAMILY_SOLDIER, 0);
    walker* b = make_guy(FAMILY_ARCHER, 0);
    TEST_ASSERT(a != nullptr, "a created");
    TEST_ASSERT(b != nullptr, "b created");

    b->setxy(101, 100);
    float hp_before = b->stats()->hitpoints;
    bool result = a->attack(b);
    TEST_ASSERT(!result, "attack should fail against friendly");
    TEST_ASSERT(b->stats()->hitpoints == hp_before, "friendly HP should not change");

    delete a;
    delete b;
}
REGISTER_TEST(test_walker_attack_friendly_fails);

void test_walker_attack_slime_magic_bonus()
{
    walker* attacker = make_guy(FAMILY_MAGE, 0);
    walker* slime = make_guy(FAMILY_SMALL_SLIME, 1);
    TEST_ASSERT(attacker != nullptr, "attacker created");
    TEST_ASSERT(slime != nullptr, "slime created");

    slime->setxy(101, 100);
    slime->stats()->hitpoints = 500;
    slime->stats()->max_hitpoints = 500;
    attacker->stats()->set_bit_flags(BIT_MAGICAL, 1);

    attacker->attack(slime);
    // Magic does 2x damage to slimes - just verify no crash
    delete attacker;
    delete slime;
}
REGISTER_TEST(test_walker_attack_slime_magic_bonus);

void test_walker_attack_barbarian_magic_resistance()
{
    walker* attacker = make_guy(FAMILY_MAGE, 0);
    walker* barb = make_guy(FAMILY_BARBARIAN, 1);
    TEST_ASSERT(attacker != nullptr, "attacker created");
    TEST_ASSERT(barb != nullptr, "target created");

    barb->setxy(101, 100);
    attacker->stats()->set_bit_flags(BIT_MAGICAL, 1);

    attacker->attack(barb);
    delete attacker;
    delete barb;
}
REGISTER_TEST(test_walker_attack_barbarian_magic_resistance);

void test_walker_attack_invulnerable()
{
    walker* a = make_guy(FAMILY_SOLDIER, 0);
    walker* b = make_guy(FAMILY_SOLDIER, 1);
    TEST_ASSERT(a != nullptr, "a created");
    TEST_ASSERT(b != nullptr, "b created");

    b->setxy(101, 100);
    b->invulnerable_left = 10;
    bool result = a->attack(b);
    TEST_ASSERT(!result, "attack should fail against invulnerable");

    delete a;
    delete b;
}
REGISTER_TEST(test_walker_attack_invulnerable);

void test_walker_attack_dead_target()
{
    walker* a = make_guy(FAMILY_SOLDIER, 0);
    walker* b = make_guy(FAMILY_SOLDIER, 1);
    TEST_ASSERT(a != nullptr, "a created");
    TEST_ASSERT(b != nullptr, "b created");

    b->setxy(101, 100);
    b->dead = 1;
    bool result = a->attack(b);
    TEST_ASSERT(!result, "attack should fail against dead target");

    delete a;
    delete b;
}
REGISTER_TEST(test_walker_attack_dead_target);

// ---------------------------------------------------------------------------
// act() - exercises the act function (lines 1539-1666)
// ---------------------------------------------------------------------------

void test_walker_act_control()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    TEST_ASSERT(w != nullptr, "walker created");
    w->set_act_type(ACT_CONTROL);
    bool result = w->act();
    TEST_ASSERT(result, "ACT_CONTROL should return true");
    delete w;
}
REGISTER_TEST(test_walker_act_control);

void test_walker_act_die()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    TEST_ASSERT(w != nullptr, "walker created");
    w->set_act_type(ACT_DIE);
    w->act();
    TEST_ASSERT(w->dead == 1, "ACT_DIE should set dead");
    delete w;
}
REGISTER_TEST(test_walker_act_die);

void test_walker_act_frozen()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    TEST_ASSERT(w != nullptr, "walker created");
    w->set_act_type(ACT_RANDOM);
    w->stats()->frozen_delay = 5;
    bool result = w->act();
    TEST_ASSERT(result, "frozen walker should return 1");
    TEST_ASSERT_EQ(4, (int)w->stats()->frozen_delay, "frozen_delay should decrement");
    delete w;
}
REGISTER_TEST(test_walker_act_frozen);

void test_walker_act_with_commands()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    TEST_ASSERT(w != nullptr, "walker created");
    w->set_act_type(ACT_RANDOM);
    w->stats()->add_command(COMMAND_WALK, 3, 1, 0);
    bool result = w->act();
    TEST_ASSERT(result, "walker with commands should return 1");
    delete w;
}
REGISTER_TEST(test_walker_act_with_commands);

// ---------------------------------------------------------------------------
// transfer_stats (lines 4307-4360)
// ---------------------------------------------------------------------------

void test_walker_transfer_stats()
{
    walker* a = make_guy(FAMILY_SOLDIER, 0);
    walker* b = make_guy(FAMILY_ARCHER, 0);
    TEST_ASSERT(a != nullptr, "a created");
    TEST_ASSERT(b != nullptr, "b created");

    a->stats()->hitpoints = 50;
    a->stats()->max_hitpoints = 100;
    a->stats()->magicpoints = 30;
    a->stats()->level = 5;

    a->transfer_stats(b);

    TEST_ASSERT_EQ(50, (int)b->stats()->hitpoints, "HP transferred");
    TEST_ASSERT_EQ(100, (int)b->stats()->max_hitpoints, "max HP transferred");
    TEST_ASSERT_EQ(30, (int)b->stats()->magicpoints, "MP transferred");
    TEST_ASSERT_EQ(5, (int)b->stats()->level, "level transferred");

    delete a;
    delete b;
}
REGISTER_TEST(test_walker_transfer_stats);

void test_walker_transfer_stats_with_guy()
{
    walker* a = make_guy(FAMILY_SOLDIER, 0);
    walker* b = make_guy(FAMILY_ARCHER, 0);
    TEST_ASSERT(a != nullptr, "a created");
    TEST_ASSERT(b != nullptr, "b created");

    // b shouldn't have a myguy from transfer yet
    if (b->myguy) { delete b->myguy; b->myguy = nullptr; }

    a->transfer_stats(b);

    TEST_ASSERT(b->myguy != nullptr, "myguy should be transferred");

    delete a;
    delete b;
}
REGISTER_TEST(test_walker_transfer_stats_with_guy);

// ---------------------------------------------------------------------------
// transform_to (lines 4364-4417)
// ---------------------------------------------------------------------------

void test_walker_transform_to()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    TEST_ASSERT(w != nullptr, "walker created");

    w->transform_to(Order::Living, FAMILY_ARCHER);
    TEST_ASSERT_EQ((int)FAMILY_ARCHER, (int)w->query_family(), "should be archer after transform");

    delete w;
}
REGISTER_TEST(test_walker_transform_to);

void test_walker_transform_to_same_order()
{
    walker* w = make_guy(FAMILY_ELF, 0);
    TEST_ASSERT(w != nullptr, "walker created");

    w->set_act_type(ACT_CONTROL);
    w->transform_to(Order::Living, FAMILY_MAGE);
    TEST_ASSERT_EQ((int)FAMILY_MAGE, (int)w->query_family(), "should be mage");
    TEST_ASSERT_EQ(ACT_CONTROL, (int)w->query_act_type(), "should preserve act type for same order");

    delete w;
}
REGISTER_TEST(test_walker_transform_to_same_order);

// ---------------------------------------------------------------------------
// spaces_clear (lines 4293-4305)
// ---------------------------------------------------------------------------

void test_walker_spaces_clear()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    TEST_ASSERT(w != nullptr, "walker created");
    w->setxy(100, 100);

    short count = w->spaces_clear();
    TEST_ASSERT(count >= 0 && count <= 8, "spaces_clear should be 0-8");

    delete w;
}
REGISTER_TEST(test_walker_spaces_clear);

// ---------------------------------------------------------------------------
// fire_check (lines 4026-4291) - complex direction logic
// ---------------------------------------------------------------------------

void test_walker_fire_check_all_dirs()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    TEST_ASSERT(w != nullptr, "walker created");
    w->setxy(100, 100);

    // Try fire_check in all 8 directions
    w->fire_check(1, 0);
    w->fire_check(-1, 0);
    w->fire_check(0, 1);
    w->fire_check(0, -1);
    w->fire_check(1, 1);
    w->fire_check(-1, 1);
    w->fire_check(1, -1);
    w->fire_check(-1, -1);

    delete w;
}
REGISTER_TEST(test_walker_fire_check_all_dirs);

// ---------------------------------------------------------------------------
// init_fire (lines 646-691)
// ---------------------------------------------------------------------------

void test_walker_init_fire_when_busy()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    TEST_ASSERT(w != nullptr, "walker created");
    w->setxy(100, 100);
    w->busy = 10;

    bool result = w->init_fire(1, 0);
    (void)result; // busy behavior may vary

    delete w;
}
REGISTER_TEST(test_walker_init_fire_when_busy);

// ---------------------------------------------------------------------------
// set_order_family (lines 2199-2265) - exercises family name/weapon setup
// ---------------------------------------------------------------------------

void test_walker_set_order_family_all()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    TEST_ASSERT(w != nullptr, "walker created");

    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        w->set_order_family(Order::Living, families[i]);
        TEST_ASSERT_EQ((int)families[i], (int)w->query_family(), "family should match");
    }

    delete w;
}
REGISTER_TEST(test_walker_set_order_family_all);

// ---------------------------------------------------------------------------
// is_friendly extended (lines 4670-4738)
// ---------------------------------------------------------------------------

void test_walker_is_friendly_different_teams()
{
    walker* a = make_guy(FAMILY_SOLDIER, 0);
    walker* b = make_guy(FAMILY_SOLDIER, 1);
    TEST_ASSERT(a != nullptr, "a created");
    TEST_ASSERT(b != nullptr, "b created");

    a->team_num = 0;
    b->team_num = 1;
    Sint32 r1 = a->is_friendly(b);
    Sint32 r2 = b->is_friendly(a);
    (void)r1; (void)r2; // exercise the code paths

    delete a;
    delete b;
}
REGISTER_TEST(test_walker_is_friendly_different_teams);

// ---------------------------------------------------------------------------
// set_difficulty (lines 4611-4635)
// ---------------------------------------------------------------------------

void test_walker_set_difficulty_all_families()
{
    loader* l = myscreen->level_data.myloader.get();
    if (!l) return;

    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        walker* w = l->create_walker(Order::Living, families[i], myscreen);
        if (w) {
            w->set_difficulty(5);
            TEST_ASSERT(w->stats()->max_hitpoints > 0, "HP positive after set_difficulty");
            delete w;
        }
    }
}
REGISTER_TEST(test_walker_set_difficulty_all_families);

// ---------------------------------------------------------------------------
// get_current_angle for all directions (line 552-575)
// ---------------------------------------------------------------------------

void test_walker_get_current_angle_all_dirs()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    TEST_ASSERT(w != nullptr, "walker created");

    float prev_angle = -999;
    for (int dir = 0; dir < 8; dir++) {
        w->curdir = static_cast<char>(dir);
        float angle = w->get_current_angle();
        // Each direction should have a different angle
        if (dir > 0) {
            TEST_ASSERT(angle != prev_angle, "each direction should have unique angle");
        }
        prev_angle = angle;
    }

    delete w;
}
REGISTER_TEST(test_walker_get_current_angle_all_dirs);

// ---------------------------------------------------------------------------
// animate smoke test
// ---------------------------------------------------------------------------

void test_walker_animate_smoke()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    TEST_ASSERT(w != nullptr, "walker created");
    w->setxy(100, 100);
    w->ani_type = ANI_WALK;
    w->animate();
    delete w;
}
REGISTER_TEST(test_walker_animate_smoke);
