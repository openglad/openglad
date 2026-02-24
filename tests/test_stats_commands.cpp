#include <openglad/entities/guy.h>
#include <openglad/runtime/guy_create.h>
#include <openglad/data/gloader.h>
#include <openglad/entities/walker.h>
#include <openglad/core/stats.h>
#include <openglad/runtime/screen.h>
#include <openglad/legacy/base.h>
#include "test_framework.h"
#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

static std::unique_ptr<walker> make_walker(char family)
{
    guy g(family);
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, myscreen);
    if (w)
        w->setxy(100, 100);
    return w;
}

// ---------------------------------------------------------------------------
// do_command - exercises the big switch (lines 51-276 in stats.cpp)
// Each COMMAND_* case is a different branch
// ---------------------------------------------------------------------------

void test_stats_do_command_walk()
{
    auto w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->stats()->force_command(COMMAND_WALK, 5, 1, 0);
    TEST_ASSERT(w->stats()->has_commands(), "should have commands");
    short result = w->stats()->do_command();
    (void)result;
}
REGISTER_TEST(test_stats_do_command_walk);

void test_stats_do_command_fire()
{
    auto w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->stats()->force_command(COMMAND_FIRE, 1, 1, 0);
    short result = w->stats()->do_command();
    (void)result;
}
REGISTER_TEST(test_stats_do_command_fire);

void test_stats_do_command_random_walk()
{
    auto w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->stats()->force_command(COMMAND_RANDOM_WALK, 5, 0, 0);
    short result = w->stats()->do_command();
    (void)result;
}
REGISTER_TEST(test_stats_do_command_random_walk);

void test_stats_do_command_search()
{
    auto w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->stats()->force_command(COMMAND_SEARCH, 10, 0, 0);
    short result = w->stats()->do_command();
    (void)result;
}
REGISTER_TEST(test_stats_do_command_search);

void test_stats_do_command_set_weapon()
{
    auto w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->stats()->force_command(COMMAND_SET_WEAPON, 1, FAMILY_KNIFE, 0);
    short result = w->stats()->do_command();
    (void)result;
}
REGISTER_TEST(test_stats_do_command_set_weapon);

void test_stats_do_command_reset_weapon()
{
    auto w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->stats()->force_command(COMMAND_RESET_WEAPON, 1, 0, 0);
    short result = w->stats()->do_command();
    (void)result;
}
REGISTER_TEST(test_stats_do_command_reset_weapon);

void test_stats_do_command_rush()
{
    auto w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->stats()->force_command(COMMAND_RUSH, 3, 1, 0);
    short result = w->stats()->do_command();
    (void)result;
}
REGISTER_TEST(test_stats_do_command_rush);

void test_stats_do_command_quick_fire()
{
    auto w = make_walker(FAMILY_ARCHER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->stats()->force_command(COMMAND_QUICK_FIRE, 1, 1, 0);
    short result = w->stats()->do_command();
    (void)result;
}
REGISTER_TEST(test_stats_do_command_quick_fire);

void test_stats_do_command_attack()
{
    auto w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->stats()->force_command(COMMAND_ATTACK, 5, 1, 0);
    short result = w->stats()->do_command();
    (void)result;
}
REGISTER_TEST(test_stats_do_command_attack);

void test_stats_do_command_right_walk()
{
    auto w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->stats()->force_command(COMMAND_RIGHT_WALK, 5, 1, 0);
    short result = w->stats()->do_command();
    (void)result;
}
REGISTER_TEST(test_stats_do_command_right_walk);

void test_stats_do_command_die_sets_delete_me()
{
    auto w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->dead = 1; // avoid log spam; COMMAND_DIE expects a dead controller
    w->stats()->delete_me = 0;
    w->stats()->force_command(COMMAND_DIE, 1, 0, 0);
    (void)w->stats()->do_command();
    TEST_ASSERT(w->stats()->delete_me == 1, "COMMAND_DIE should set delete_me when count < 2");
}
REGISTER_TEST(test_stats_do_command_die_sets_delete_me);

// COMMAND_SPECIAL doesn't exist as a constant - specials are called directly

// ---------------------------------------------------------------------------
// forward_blocked / right_blocked / variants for all 8 directions
// These exercise 8 big direction switch cases each
// ---------------------------------------------------------------------------

void test_stats_forward_blocked_all_dirs()
{
    auto w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");

    for (int dir = 0; dir < 8; dir++) {
        w->curdir = static_cast<char>(dir);
        short result = w->stats()->forward_blocked();
        (void)result;
    }
}
REGISTER_TEST(test_stats_forward_blocked_all_dirs);

void test_stats_right_blocked_all_dirs()
{
    auto w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");

    for (int dir = 0; dir < 8; dir++) {
        w->curdir = static_cast<char>(dir);
        short result = w->stats()->right_blocked();
        (void)result;
    }
}
REGISTER_TEST(test_stats_right_blocked_all_dirs);

void test_stats_right_forward_blocked_all_dirs()
{
    auto w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");

    for (int dir = 0; dir < 8; dir++) {
        w->curdir = static_cast<char>(dir);
        short result = w->stats()->right_forward_blocked();
        (void)result;
    }
}
REGISTER_TEST(test_stats_right_forward_blocked_all_dirs);

void test_stats_right_back_blocked_all_dirs()
{
    auto w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");

    for (int dir = 0; dir < 8; dir++) {
        w->curdir = static_cast<char>(dir);
        short result = w->stats()->right_back_blocked();
        (void)result;
    }
}
REGISTER_TEST(test_stats_right_back_blocked_all_dirs);

// ---------------------------------------------------------------------------
// hit_response for different families (line ~305+)
// ---------------------------------------------------------------------------

void test_stats_hit_response_all_families()
{
    char families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };

    for (int i = 0; i < 14; i++) {
        auto target = make_walker(families[i]);
        auto attacker = make_walker(FAMILY_SOLDIER);
        if (target && attacker) {
            attacker->team_num = 1;
            target->team_num = 0;
            attacker->setxy(105, 100);
            target->stats()->hit_response(attacker.get());
        }
    }
}
REGISTER_TEST(test_stats_hit_response_all_families);

// ---------------------------------------------------------------------------
// try_command and command management
// ---------------------------------------------------------------------------

void test_stats_try_command_when_full()
{
    auto w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");

    // Fill up commands
    for (int i = 0; i < 20; i++) {
        w->stats()->add_command(COMMAND_WALK, 1, 1, 0);
    }
    // try_command should not add if commands exist
    short result = w->stats()->try_command(COMMAND_WALK, 5, 1, 0);
    (void)result;

}
REGISTER_TEST(test_stats_try_command_when_full);

void test_stats_clear_command()
{
    auto w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");

    w->stats()->add_command(COMMAND_WALK, 5, 1, 0);
    TEST_ASSERT(w->stats()->has_commands(), "should have commands");

    w->stats()->clear_command();
    TEST_ASSERT(!w->stats()->has_commands(), "should be empty after clear");

}
REGISTER_TEST(test_stats_clear_command);

void test_stats_force_command_clamping()
{
    auto w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");

    w->stats()->add_command(COMMAND_WALK, 5, 1, 0);
    w->stats()->force_command(COMMAND_FIRE, 1, 0, 1);
    // force_command should replace existing commands

}
REGISTER_TEST(test_stats_force_command_clamping);

// ---------------------------------------------------------------------------
// right_walk and direct_walk (command helpers)
// ---------------------------------------------------------------------------

void test_stats_right_walk_smoke()
{
    auto w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->curdir = FACE_RIGHT;
    w->stats()->right_walk();
}
REGISTER_TEST(test_stats_right_walk_smoke);

void test_stats_direct_walk_smoke()
{
    auto w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->foe = nullptr;
    w->stats()->direct_walk();
}
REGISTER_TEST(test_stats_direct_walk_smoke);

void test_stats_walk_to_foe_no_foe()
{
    auto w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->foe = nullptr;
    w->stats()->walk_to_foe();
}
REGISTER_TEST(test_stats_walk_to_foe_no_foe);

void test_stats_walk_to_foe_with_foe()
{
    auto w = make_walker(FAMILY_SOLDIER);
    auto enemy = make_walker(FAMILY_ORC);
    TEST_ASSERT(w != nullptr, "walker created");
    TEST_ASSERT(enemy != nullptr, "enemy created");
    w->team_num = 0;
    enemy->team_num = 1;
    enemy->setxy(120, 100);
    w->foe = enemy.get();
    w->stats()->walk_to_foe();
}
REGISTER_TEST(test_stats_walk_to_foe_with_foe);

void test_stats_yell_for_help_smoke()
{
    auto w = make_walker(FAMILY_SOLDIER);
    auto enemy = make_walker(FAMILY_ORC);
    TEST_ASSERT(w != nullptr, "walker created");
    TEST_ASSERT(enemy != nullptr, "enemy created");
    enemy->team_num = 1;
    enemy->setxy(120, 100);
    w->stats()->yell_for_help(enemy.get());
}
REGISTER_TEST(test_stats_yell_for_help_smoke);
