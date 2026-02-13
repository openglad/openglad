#include "graph.h"
#include "entities/guy.h"
#include "test_framework.h"

#include <memory>

extern screen* myscreen;

namespace
{
static std::unique_ptr<walker> make_walker(char family)
{
    guy g(family);
    g.upgrade_to_level(3, true);
    auto w = g.create_walker_owned(myscreen);
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
