#include "graph.h"
#include "guy.h"
#include "gloader.h"
#include "test_framework.h"

extern screen* myscreen;

static walker* make_walker(char family)
{
    guy g(family);
    g.upgrade_to_level(3, true);
    walker* w = g.create_walker(myscreen);
    if (w) w->setxy(100, 100);
    return w;
}

// ---------------------------------------------------------------------------
// do_command - exercises the big switch (lines 51-276 in stats.cpp)
// Each COMMAND_* case is a different branch
// ---------------------------------------------------------------------------

void test_stats_do_command_walk()
{
    walker* w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->stats()->force_command(COMMAND_WALK, 5, 1, 0);
    TEST_ASSERT(w->stats()->has_commands(), "should have commands");
    short result = w->stats()->do_command();
    (void)result;
    delete w;
}
REGISTER_TEST(test_stats_do_command_walk);

void test_stats_do_command_fire()
{
    walker* w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->stats()->force_command(COMMAND_FIRE, 1, 1, 0);
    short result = w->stats()->do_command();
    (void)result;
    delete w;
}
REGISTER_TEST(test_stats_do_command_fire);

void test_stats_do_command_random_walk()
{
    walker* w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->stats()->force_command(COMMAND_RANDOM_WALK, 5, 0, 0);
    short result = w->stats()->do_command();
    (void)result;
    delete w;
}
REGISTER_TEST(test_stats_do_command_random_walk);

void test_stats_do_command_search()
{
    walker* w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->stats()->force_command(COMMAND_SEARCH, 10, 0, 0);
    short result = w->stats()->do_command();
    (void)result;
    delete w;
}
REGISTER_TEST(test_stats_do_command_search);

void test_stats_do_command_set_weapon()
{
    walker* w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->stats()->force_command(COMMAND_SET_WEAPON, 1, FAMILY_KNIFE, 0);
    short result = w->stats()->do_command();
    (void)result;
    delete w;
}
REGISTER_TEST(test_stats_do_command_set_weapon);

void test_stats_do_command_reset_weapon()
{
    walker* w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->stats()->force_command(COMMAND_RESET_WEAPON, 1, 0, 0);
    short result = w->stats()->do_command();
    (void)result;
    delete w;
}
REGISTER_TEST(test_stats_do_command_reset_weapon);

void test_stats_do_command_rush()
{
    walker* w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->stats()->force_command(COMMAND_RUSH, 3, 1, 0);
    short result = w->stats()->do_command();
    (void)result;
    delete w;
}
REGISTER_TEST(test_stats_do_command_rush);

void test_stats_do_command_quick_fire()
{
    walker* w = make_walker(FAMILY_ARCHER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->stats()->force_command(COMMAND_QUICK_FIRE, 1, 1, 0);
    short result = w->stats()->do_command();
    (void)result;
    delete w;
}
REGISTER_TEST(test_stats_do_command_quick_fire);

void test_stats_do_command_attack()
{
    walker* w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->stats()->force_command(COMMAND_ATTACK, 5, 1, 0);
    short result = w->stats()->do_command();
    (void)result;
    delete w;
}
REGISTER_TEST(test_stats_do_command_attack);

void test_stats_do_command_right_walk()
{
    walker* w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->stats()->force_command(COMMAND_RIGHT_WALK, 5, 1, 0);
    short result = w->stats()->do_command();
    (void)result;
    delete w;
}
REGISTER_TEST(test_stats_do_command_right_walk);

// COMMAND_SPECIAL doesn't exist as a constant - specials are called directly

// ---------------------------------------------------------------------------
// forward_blocked / right_blocked / variants for all 8 directions
// These exercise 8 big direction switch cases each
// ---------------------------------------------------------------------------

void test_stats_forward_blocked_all_dirs()
{
    walker* w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");

    for (int dir = 0; dir < 8; dir++) {
        w->curdir = static_cast<char>(dir);
        short result = w->stats()->forward_blocked();
        (void)result;
    }
    delete w;
}
REGISTER_TEST(test_stats_forward_blocked_all_dirs);

void test_stats_right_blocked_all_dirs()
{
    walker* w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");

    for (int dir = 0; dir < 8; dir++) {
        w->curdir = static_cast<char>(dir);
        short result = w->stats()->right_blocked();
        (void)result;
    }
    delete w;
}
REGISTER_TEST(test_stats_right_blocked_all_dirs);

void test_stats_right_forward_blocked_all_dirs()
{
    walker* w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");

    for (int dir = 0; dir < 8; dir++) {
        w->curdir = static_cast<char>(dir);
        short result = w->stats()->right_forward_blocked();
        (void)result;
    }
    delete w;
}
REGISTER_TEST(test_stats_right_forward_blocked_all_dirs);

void test_stats_right_back_blocked_all_dirs()
{
    walker* w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");

    for (int dir = 0; dir < 8; dir++) {
        w->curdir = static_cast<char>(dir);
        short result = w->stats()->right_back_blocked();
        (void)result;
    }
    delete w;
}
REGISTER_TEST(test_stats_right_back_blocked_all_dirs);

// ---------------------------------------------------------------------------
// hit_response for different families (line ~305+)
// ---------------------------------------------------------------------------

void test_stats_hit_response_all_families()
{
    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };

    for (int i = 0; i < 14; i++) {
        walker* target = make_walker(families[i]);
        walker* attacker = make_walker(FAMILY_SOLDIER);
        if (target && attacker) {
            attacker->team_num = 1;
            target->team_num = 0;
            attacker->setxy(105, 100);
            target->stats()->hit_response(attacker);
        }
        delete target;
        delete attacker;
    }
}
REGISTER_TEST(test_stats_hit_response_all_families);

// ---------------------------------------------------------------------------
// try_command and command management
// ---------------------------------------------------------------------------

void test_stats_try_command_when_full()
{
    walker* w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");

    // Fill up commands
    for (int i = 0; i < 20; i++) {
        w->stats()->add_command(COMMAND_WALK, 1, 1, 0);
    }
    // try_command should not add if commands exist
    short result = w->stats()->try_command(COMMAND_WALK, 5, 1, 0);
    (void)result;

    delete w;
}
REGISTER_TEST(test_stats_try_command_when_full);

void test_stats_clear_command()
{
    walker* w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");

    w->stats()->add_command(COMMAND_WALK, 5, 1, 0);
    TEST_ASSERT(w->stats()->has_commands(), "should have commands");

    w->stats()->clear_command();
    TEST_ASSERT(!w->stats()->has_commands(), "should be empty after clear");

    delete w;
}
REGISTER_TEST(test_stats_clear_command);

void test_stats_force_command_clamping()
{
    walker* w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");

    w->stats()->add_command(COMMAND_WALK, 5, 1, 0);
    w->stats()->force_command(COMMAND_FIRE, 1, 0, 1);
    // force_command should replace existing commands

    delete w;
}
REGISTER_TEST(test_stats_force_command_clamping);

// ---------------------------------------------------------------------------
// right_walk and direct_walk (command helpers)
// ---------------------------------------------------------------------------

void test_stats_right_walk_smoke()
{
    walker* w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->curdir = FACE_RIGHT;
    w->stats()->right_walk();
    delete w;
}
REGISTER_TEST(test_stats_right_walk_smoke);

void test_stats_direct_walk_smoke()
{
    walker* w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->foe = nullptr;
    w->stats()->direct_walk();
    delete w;
}
REGISTER_TEST(test_stats_direct_walk_smoke);

void test_stats_walk_to_foe_no_foe()
{
    walker* w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->foe = nullptr;
    w->stats()->walk_to_foe();
    delete w;
}
REGISTER_TEST(test_stats_walk_to_foe_no_foe);

void test_stats_walk_to_foe_with_foe()
{
    walker* w = make_walker(FAMILY_SOLDIER);
    walker* enemy = make_walker(FAMILY_ORC);
    TEST_ASSERT(w != nullptr, "walker created");
    TEST_ASSERT(enemy != nullptr, "enemy created");
    w->team_num = 0;
    enemy->team_num = 1;
    enemy->setxy(120, 100);
    w->foe = enemy;
    w->stats()->walk_to_foe();
    delete w;
    delete enemy;
}
REGISTER_TEST(test_stats_walk_to_foe_with_foe);

void test_stats_yell_for_help_smoke()
{
    walker* w = make_walker(FAMILY_SOLDIER);
    walker* enemy = make_walker(FAMILY_ORC);
    TEST_ASSERT(w != nullptr, "walker created");
    TEST_ASSERT(enemy != nullptr, "enemy created");
    enemy->team_num = 1;
    enemy->setxy(120, 100);
    w->stats()->yell_for_help(enemy);
    delete w;
    delete enemy;
}
REGISTER_TEST(test_stats_yell_for_help_smoke);
