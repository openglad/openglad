#include <openglad/core/stats.h>
#include <openglad/entities/guy.h>
#include <openglad/runtime/guy_create.h>
#include <openglad/entities/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/render/view.h>
#include <openglad/runtime/screen.h>
#include "test_framework.h"

#include <memory>

extern screen* myscreen;

namespace
{
static std::unique_ptr<walker> make_walker(char family)
{
    guy g(family);
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, myscreen);
    if (w)
        w->setxy(GRID_SIZE * 6, GRID_SIZE * 6);
    return w;
}
} // namespace

void test_stats_do_command_follow_branches()
{
    // Create a leader via viewob[0]->control and a follower that will receive COMMAND_FOLLOW.
    auto leader = make_walker(FAMILY_SOLDIER);
    auto follower = make_walker(FAMILY_ELF);
    TEST_ASSERT(leader && follower, "walkers created");
    if (!(leader && follower))
        return;

    leader->team_num = 0;
    follower->team_num = 0;

    myscreen->viewob[0]->control = leader.get();
    follower->leader = nullptr;
    follower->foe = nullptr;

    // Ensure leader is far enough to exercise walkstep and normalization.
    leader->setxy(static_cast<Sint32>(follower->xpos) + 200, static_cast<Sint32>(follower->ypos));

    follower->stats()->force_command(COMMAND_FOLLOW, 2, 0, 0);
    (void)follower->stats()->do_command();

    // If we're close, leader should be cleared (distance < 60 path).
    leader->setxy(static_cast<Sint32>(follower->xpos) + 10, static_cast<Sint32>(follower->ypos));
    follower->leader = leader.get();
    follower->stats()->force_command(COMMAND_FOLLOW, 1, 0, 0);
    (void)follower->stats()->do_command();

    myscreen->viewob[0]->control = nullptr;
}
REGISTER_TEST(test_stats_do_command_follow_branches);

void test_stats_do_command_follow_early_exit_when_foe_present()
{
    auto leader = make_walker(FAMILY_SOLDIER);
    auto follower = make_walker(FAMILY_ELF);
    auto foe = make_walker(FAMILY_ORC);
    TEST_ASSERT(leader && follower && foe, "walkers created");
    if (!(leader && follower && foe))
        return;

    myscreen->viewob[0]->control = leader.get();
    follower->foe = foe.get();
    follower->leader = leader.get();

    follower->stats()->force_command(COMMAND_FOLLOW, 1, 0, 0);
    (void)follower->stats()->do_command(); // should clear leader + finish command

    myscreen->viewob[0]->control = nullptr;
}
REGISTER_TEST(test_stats_do_command_follow_early_exit_when_foe_present);

void test_stats_do_command_fire_nonliving_logs_and_returns()
{
    // Cover COMMAND_FIRE branch for non-living order.
    walker* weapon = myscreen->level_data.add_weap_ob(Order::Weapon, FAMILY_ARROW);
    TEST_ASSERT(weapon != nullptr, "weapon created");
    if (!weapon)
        return;

    weapon->stats()->force_command(COMMAND_FIRE, 1, 1, 0);
    (void)weapon->stats()->do_command();
}
REGISTER_TEST(test_stats_do_command_fire_nonliving_logs_and_returns);

void test_stats_hit_response_triggers_yell_for_help_for_low_hp()
{
    auto target = make_walker(FAMILY_SOLDIER);
    auto attacker = make_walker(FAMILY_ORC);
    TEST_ASSERT(target && attacker, "walkers created");
    if (!(target && attacker))
        return;

    target->team_num = 0;
    attacker->team_num = 1;
    target->yo_delay = 0;
    target->stats()->hitpoints = 1; // below flee threshold

    attacker->setxy(static_cast<Sint32>(target->xpos) + 5, static_cast<Sint32>(target->ypos));
    target->stats()->hit_response(attacker.get());
}
REGISTER_TEST(test_stats_hit_response_triggers_yell_for_help_for_low_hp);

void test_stats_right_walk_exercises_direction_switch_when_direct_walk_fails()
{
    // Exercise the large direction switch in statistics::right_walk() by ensuring:
    // - right/forward/right-back checks are false (open grid)
    // - direct_walk() returns false (foe is null)
    myscreen->level_data.create_new_grid();

    walker* w = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    if (!w)
        return;

    w->setxy(GRID_SIZE * 10, GRID_SIZE * 10);
    w->lastx = 1;
    w->lasty = 0;
    w->foe = nullptr; // forces direct_walk() to return 0

    for (int dir = 0; dir < 8; dir++) {
        w->curdir = static_cast<char>(dir);
        w->enddir = static_cast<char>(dir);
        (void)w->stats()->right_walk();
    }
}
REGISTER_TEST(test_stats_right_walk_exercises_direction_switch_when_direct_walk_fails);

void test_stats_constructor_null_controller_and_command_die_shortcuts()
{
    statistics s(nullptr);
    TEST_ASSERT(s.controller == nullptr, "null-controller ctor should keep controller null");
    TEST_ASSERT_EQ((int)Order::Living, (int)s.old_order, "null-controller ctor should set fallback old_order");
    TEST_ASSERT_EQ((int)FAMILY_SOLDIER, (int)s.old_family, "null-controller ctor should set fallback old_family");

    auto w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    if (!w)
        return;

    w->stats()->delete_me = 0;
    w->stats()->add_command(COMMAND_DIE, 1, 0, 0);
    TEST_ASSERT(w->stats()->delete_me == 1, "COMMAND_DIE add_command should mark delete_me");
}
REGISTER_TEST(test_stats_constructor_null_controller_and_command_die_shortcuts);

void test_stats_do_command_set_reset_weapon_and_search_without_foe()
{
    auto w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    if (!w)
        return;

    w->default_weapon = FAMILY_KNIFE;
    w->current_weapon = FAMILY_ARROW;
    w->stats()->commands.clear();

    w->stats()->force_command(COMMAND_SET_WEAPON, 1, FAMILY_BOMB, 0);
    (void)w->stats()->do_command();
    TEST_ASSERT_EQ((int)FAMILY_BOMB, (int)w->current_weapon, "COMMAND_SET_WEAPON should set current weapon");

    w->stats()->force_command(COMMAND_RESET_WEAPON, 1, 0, 0);
    (void)w->stats()->do_command();
    TEST_ASSERT_EQ((int)w->default_weapon, (int)w->current_weapon, "COMMAND_RESET_WEAPON should restore default");

    w->foe = nullptr;
    w->stats()->force_command(COMMAND_SEARCH, 1, 0, 0);
    (void)w->stats()->do_command();
    TEST_ASSERT(!w->stats()->has_commands(), "COMMAND_SEARCH with no foe should clear command");
}
REGISTER_TEST(test_stats_do_command_set_reset_weapon_and_search_without_foe);

void test_stats_blocked_helpers_default_dir_branches()
{
    auto w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    if (!w)
        return;

    myscreen->level_data.create_new_grid();
    w->setxy(128, 128);
    w->curdir = 99;

    (void)w->stats()->right_blocked();
    (void)w->stats()->right_forward_blocked();
    (void)w->stats()->right_back_blocked();
    (void)w->stats()->forward_blocked();
}
REGISTER_TEST(test_stats_blocked_helpers_default_dir_branches);

void test_stats_forward_and_side_blocked_invalid_direction_defaults()
{
    auto w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    if (!w)
        return;

    myscreen->level_data.create_new_grid();
    w->setxy(GRID_SIZE * 4, GRID_SIZE * 4);
    w->curdir = static_cast<char>(127);
    w->enddir = static_cast<char>(127);

    TEST_ASSERT(!w->stats()->forward_blocked(), "invalid curdir should fall back to no forward block");
    TEST_ASSERT(!w->stats()->right_forward_blocked(), "invalid curdir should fall back to no right-forward block");
    TEST_ASSERT(!w->stats()->right_back_blocked(), "invalid curdir should fall back to no right-back block");
    TEST_ASSERT(w->stats()->right_walk(), "invalid direction fallback in right_walk should still return true");
}
REGISTER_TEST(test_stats_forward_and_side_blocked_invalid_direction_defaults);

void test_stats_add_and_force_command_walk_clamp_and_zero_fallback()
{
    auto w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    if (!w)
        return;

    w->stats()->commands.clear();
    w->stats()->add_command(COMMAND_WALK, 2, 7, -9);
    TEST_ASSERT(!w->stats()->commands.empty(), "add_command should enqueue walk command");
    if (!w->stats()->commands.empty())
    {
        const command& c = w->stats()->commands.back();
        TEST_ASSERT_EQ(1, (int)c.com1, "add_command should clamp walk com1 to +1");
        TEST_ASSERT_EQ(-1, (int)c.com2, "add_command should clamp walk com2 to -1");
    }

    w->stats()->force_command(COMMAND_WALK, 1, 0, 0);
    TEST_ASSERT(!w->stats()->commands.empty(), "force_command should prepend command");
    if (!w->stats()->commands.empty())
    {
        const command& c = w->stats()->commands.front();
        TEST_ASSERT_EQ(1, (int)c.com1, "force_command should convert zero walk x to 1");
        TEST_ASSERT_EQ(1, (int)c.com2, "force_command should convert zero walk y to 1");
    }
}
REGISTER_TEST(test_stats_add_and_force_command_walk_clamp_and_zero_fallback);

void test_stats_do_command_die_and_multido_paths()
{
    auto w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    if (!w)
        return;

    // COMMAND_DIE path in do_command() (distinct from add_command shortcut).
    statistics local_stats_die(w.get());
    local_stats_die.commands.clear();
    local_stats_die.force_command(COMMAND_DIE, 1, 0, 0);
    (void)local_stats_die.do_command();
    TEST_ASSERT(local_stats_die.delete_me == 1, "COMMAND_DIE do_command should mark delete_me");

    // COMMAND_MULTIDO case branch with com1=0 avoids recursive self-entry while still
    // executing the switch branch and post-switch command decrement path.
    statistics local_stats_multi(w.get());
    local_stats_multi.commands.clear();
    local_stats_multi.force_command(COMMAND_MULTIDO, 1, 0, 0);
    (void)local_stats_multi.do_command();
    TEST_ASSERT(!local_stats_multi.has_commands(), "COMMAND_MULTIDO with zero iterations should finish command");
}
REGISTER_TEST(test_stats_do_command_die_and_multido_paths);

void test_stats_set_command_die_and_hit_response_early_returns()
{
    auto target = make_walker(FAMILY_SOLDIER);
    auto attacker = make_walker(FAMILY_ORC);
    TEST_ASSERT(target && attacker, "walkers created");
    if (!(target && attacker))
        return;

    // set_command(COMMAND_DIE) logging branch.
    target->stats()->commands.clear();
    target->stats()->set_command(COMMAND_DIE, 1, 0, 0);
    TEST_ASSERT(!target->stats()->commands.empty(), "set_command COMMAND_DIE should still enqueue a command");

    // hit_response early return: null attacker.
    target->stats()->hit_response(nullptr);

    // hit_response early return: dead attacker.
    attacker->dead = 1;
    target->stats()->hit_response(attacker.get());
    attacker->dead = 0;

    // hit_response early return: dead controller.
    target->dead = 1;
    target->stats()->hit_response(attacker.get());
    target->dead = 0;

    // hit_response early return: ACT_CONTROL.
    target->set_act_type(ACT_CONTROL);
    target->stats()->hit_response(attacker.get());
    target->set_act_type(ACT_RANDOM);

    // hit_response early return: non-living order.
    target->set_order_family(Order::Weapon, FAMILY_ARROW);
    target->stats()->hit_response(attacker.get());
    target->set_order_family(Order::Living, FAMILY_SOLDIER);
}
REGISTER_TEST(test_stats_set_command_die_and_hit_response_early_returns);
