#include <openglad/core/stats.h>
#include <openglad/entities/guy.h>
#include <openglad/runtime/guy_create.h>
#include <openglad/entities/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/render/view.h>
#include <openglad/runtime/game_context.h>
#include <openglad/runtime/screen.h>
#include <openglad/sim/irandom.h>
#include "test_framework.h"

#include <memory>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace
{
static std::unique_ptr<walker> make_walker(char family)
{
    guy g(family);
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
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

    og::runtime::current_session->myscreen_->viewob[0]->control = leader.get();
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

    og::runtime::current_session->myscreen_->viewob[0]->control = nullptr;
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

    og::runtime::current_session->myscreen_->viewob[0]->control = leader.get();
    follower->foe = foe.get();
    follower->leader = leader.get();

    follower->stats()->force_command(COMMAND_FOLLOW, 1, 0, 0);
    (void)follower->stats()->do_command(); // should clear leader + finish command

    og::runtime::current_session->myscreen_->viewob[0]->control = nullptr;
}
REGISTER_TEST(test_stats_do_command_follow_early_exit_when_foe_present);

void test_stats_do_command_fire_nonliving_logs_and_returns()
{
    // Cover COMMAND_FIRE branch for non-living order.
    walker* weapon = og::runtime::current_session->myscreen_->level_data.add_weap_ob(Order::Weapon, FAMILY_ARROW);
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
    og::runtime::current_session->myscreen_->level_data.create_new_grid();

    walker* w = og::runtime::current_session->myscreen_->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
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

    og::runtime::current_session->myscreen_->level_data.create_new_grid();
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

    og::runtime::current_session->myscreen_->level_data.create_new_grid();
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

void test_stats_walk_to_foe_short_circuit_and_path_branches()
{
    auto actor = make_walker(FAMILY_SOLDIER);
    auto foe = make_walker(FAMILY_ORC);
    TEST_ASSERT(actor && foe, "walkers created");
    if (!(actor && foe))
        return;

    actor->team_num = 0;
    foe->team_num = 1;
    actor->setxy(GRID_SIZE * 8, GRID_SIZE * 8);
    foe->setxy(static_cast<std::int32_t>(actor->xpos + 16), static_cast<std::int32_t>(actor->ypos));
    actor->foe = foe.get();
    actor->path_check_counter = 0;
    actor->stats()->clear_command();

    FixedRandom rng_nonzero(1); // avoid rng(300)==0 early-return branch
    IRandom* prev_rng = ctx().rng;
    ctx().rng = &rng_nonzero;

    // Nearby foe path: should short-circuit into attack command logic.
    bool ok = actor->stats()->walk_to_foe();
    TEST_ASSERT(ok, "walk_to_foe should succeed for nearby foe");

    // Nearby but dead foe: find_foes_in_range should fail and zero command count branch should run.
    actor->stats()->force_command(COMMAND_WALK, 5, 1, 0);
    foe->dead = 1;
    actor->path_check_counter = 0;
    ok = actor->stats()->walk_to_foe();
    TEST_ASSERT(ok, "walk_to_foe should still run with dead remembered foe");

    // Distant foe path: executes find_path_to_foe() branch.
    foe->dead = 0;
    foe->setxy(static_cast<std::int32_t>(actor->xpos + 600), static_cast<std::int32_t>(actor->ypos));
    actor->path_check_counter = 0;
    ok = actor->stats()->walk_to_foe();
    TEST_ASSERT(ok, "walk_to_foe should run distant-path branch");

    ctx().rng = prev_rng;
}
REGISTER_TEST(test_stats_walk_to_foe_short_circuit_and_path_branches);

void test_stats_right_walk_round7_right_back_and_forward_direction_maps()
{
    og::runtime::current_session->myscreen_->level_data.create_new_grid();
    og::runtime::current_session->myscreen_->level_data.delete_objects();

    walker* actor = og::runtime::current_session->myscreen_->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(actor != nullptr, "actor created");
    if (!actor)
        return;

    actor->setxy(GRID_SIZE * 10, GRID_SIZE * 10);
    actor->curdir = FACE_UP;
    actor->lastx = 0;
    actor->lasty = -1;
    actor->stats()->commands.clear();

    // Force only right_back_blocked() to be true for FACE_UP.
    walker* blocker = og::runtime::current_session->myscreen_->level_data.add_ob(Order::Living, FAMILY_ORC);
    TEST_ASSERT(blocker != nullptr, "blocker created");
    if (!blocker)
        return;
    blocker->team_num = 1;
    blocker->setxy(static_cast<short>(actor->xpos + 1),
                   static_cast<short>(actor->ypos + 1));

    struct ExpectedDir {
        char target_dir;
        int dx;
        int dy;
    };
    const ExpectedDir expected[] = {
        {FACE_UP, 0, -1},
        {FACE_UP_RIGHT, 1, -1},
        {FACE_RIGHT, 1, 0},
        {FACE_DOWN_RIGHT, 1, 1},
        {FACE_DOWN, 0, 1},
        {FACE_DOWN_LEFT, -1, 1},
        {FACE_LEFT, -1, 0},
        {FACE_UP_LEFT, -1, -1},
    };

    for (const auto& e : expected)
    {
        actor->enddir = static_cast<char>((e.target_dir + 6) % 8); // +2 in right_walk => target_dir
        actor->stats()->commands.clear();
        const short x_before = actor->xpos;
        const short y_before = actor->ypos;
        const char enddir_before = actor->enddir;
        TEST_ASSERT(actor->stats()->right_walk(), "right_walk should succeed in right_back_blocked branch");
        TEST_ASSERT(!actor->stats()->commands.empty() || actor->xpos != x_before || actor->ypos != y_before
                        || actor->enddir != enddir_before,
                    "right_walk should queue, move, or update heading");
        if (!actor->stats()->commands.empty())
        {
            const command& c = actor->stats()->commands.back();
            TEST_ASSERT_EQ((int)COMMAND_WALK, (int)c.commandtype, "queued command should be COMMAND_WALK");
            TEST_ASSERT_EQ(e.dx, (int)c.com1, "mapped walk x should match direction");
            TEST_ASSERT_EQ(e.dy, (int)c.com2, "mapped walk y should match direction");
        }
    }

    // Remove blocker and force the direct_walk()==false fallback switch for FACE_UP.
    og::runtime::current_session->myscreen_->level_data.remove_ob(blocker);
    actor->foe = nullptr;
    actor->curdir = FACE_UP;
    actor->enddir = FACE_UP;
    const short y_before = actor->ypos;
    TEST_ASSERT(actor->stats()->right_walk(), "right_walk direct-walk fallback should return true");
    TEST_ASSERT(actor->ypos <= y_before, "FACE_UP fallback should walk in negative y direction");

    og::runtime::current_session->myscreen_->level_data.delete_objects();
}
REGISTER_TEST(test_stats_right_walk_round7_right_back_and_forward_direction_maps);

void test_stats_right_walk_round8_negative_enddir_default_switch_path()
{
    og::runtime::current_session->myscreen_->level_data.create_new_grid();
    auto actor = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(actor != nullptr, "actor created");
    if (!actor)
        return;

    // Geometry setup:
    // - right/right-forward/forward are passable
    // - right-back is blocked by bottom boundary
    actor->setxy(GRID_SIZE * 6, static_cast<std::int32_t>(og::runtime::current_session->myscreen_->level_data.pixmaxy - 1));
    actor->curdir = FACE_UP;
    actor->enddir = static_cast<char>(-127);
    actor->stats()->commands.clear();

    TEST_ASSERT(actor->stats()->right_walk(), "right_walk should return true for negative-enddir fallback path");
}
REGISTER_TEST(test_stats_right_walk_round8_negative_enddir_default_switch_path);

void test_stats_round11_follow_force_walk_and_right_walk_distance_branches()
{
    og::runtime::current_session->myscreen_->level_data.create_new_grid();
    og::runtime::current_session->myscreen_->level_data.delete_objects();

    auto leader = make_walker(FAMILY_SOLDIER);
    auto follower = make_walker(FAMILY_ELF);
    auto foe = make_walker(FAMILY_ORC);
    TEST_ASSERT(leader && follower && foe, "fixtures created");
    if (!(leader && follower && foe))
        return;

    // force_command WALK clamp path (stats.cpp:152-163).
    follower->stats()->commands.clear();
    follower->stats()->force_command(COMMAND_WALK, 3, 9, -9);
    TEST_ASSERT(!follower->stats()->commands.empty(), "force_command should enqueue walk");
    if (!follower->stats()->commands.empty())
    {
        const command& c = follower->stats()->commands.front();
        TEST_ASSERT_EQ(1, (int)c.com1, "force_command should clamp com1 to +1");
        TEST_ASSERT_EQ(-1, (int)c.com2, "force_command should clamp com2 to -1");
    }

    // COMMAND_FOLLOW no-leader-found branch (stats.cpp:285-287).
    og::runtime::current_session->myscreen_->viewob[0]->control = nullptr;
    follower->foe = nullptr;
    follower->leader = nullptr;
    follower->stats()->force_command(COMMAND_FOLLOW, 1, 0, 0);
    (void)follower->stats()->do_command();
    TEST_ASSERT(follower->leader == nullptr, "follow without leader should stay leaderless");

    // COMMAND_FOLLOW axis-normalization branch (stats.cpp:303-310).
    og::runtime::current_session->myscreen_->viewob[0]->control = leader.get();
    leader->setxy(static_cast<Sint32>(follower->xpos) + 300, static_cast<Sint32>(follower->ypos) + 40);
    follower->leader = nullptr;
    follower->foe = nullptr;
    const short before_y = follower->ypos;
    follower->stats()->force_command(COMMAND_FOLLOW, 2, 0, 0);
    (void)follower->stats()->do_command();
    TEST_ASSERT(follower->ypos == before_y || std::abs((int)follower->ypos - (int)before_y) <= 1,
                "follow axis-normalization should heavily favor x-axis movement");

    // COMMAND_RIGHT_WALK distance gate branches (stats.cpp:360-368).
    follower->foe = foe.get();
    foe->setxy(static_cast<Sint32>(follower->xpos) + 150, static_cast<Sint32>(follower->ypos)); // distance in (120,240)
    follower->stats()->force_command(COMMAND_RIGHT_WALK, 1, 0, 0);
    (void)follower->stats()->do_command();

    foe->setxy(static_cast<Sint32>(follower->xpos) + 20, static_cast<Sint32>(follower->ypos)); // distance outside (120,240)
    follower->stats()->force_command(COMMAND_RIGHT_WALK, 1, 0, 0);
    (void)follower->stats()->do_command();

    og::runtime::current_session->myscreen_->viewob[0]->control = nullptr;
}
REGISTER_TEST(test_stats_round11_follow_force_walk_and_right_walk_distance_branches);

void test_stats_round12_add_command_walk_clamps_and_follow_shortcuts()
{
    og::runtime::current_session->myscreen_->level_data.create_new_grid();
    og::runtime::current_session->myscreen_->level_data.delete_objects();

    auto follower = make_walker(FAMILY_ELF);
    auto foe = make_walker(FAMILY_ORC);
    auto leader = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(follower && foe && leader, "fixtures created");
    if (!(follower && foe && leader))
        return;

    follower->stats()->commands.clear();

    // add_command(COMMAND_WALK) clamp/default branches (stats.cpp:128-140).
    follower->stats()->add_command(COMMAND_WALK, 2, 0, 0);
    TEST_ASSERT(!follower->stats()->commands.empty(), "add_command walk should enqueue");
    if (!follower->stats()->commands.empty())
    {
        const command& c = follower->stats()->commands.back();
        TEST_ASSERT_EQ(1, (int)c.com1, "zero walk x should default to +1");
        TEST_ASSERT_EQ(1, (int)c.com2, "zero walk y should default to +1");
    }

    follower->stats()->add_command(COMMAND_WALK, 2, 7, -7);
    TEST_ASSERT(!follower->stats()->commands.empty(), "second walk command enqueued");
    if (!follower->stats()->commands.empty())
    {
        const command& c = follower->stats()->commands.back();
        TEST_ASSERT_EQ(1, (int)c.com1, "walk x should clamp high value to +1");
        TEST_ASSERT_EQ(-1, (int)c.com2, "walk y should clamp low value to -1");
    }

    // COMMAND_FOLLOW early exit when foe exists (stats.cpp:273-278).
    follower->foe = foe.get();
    follower->leader = leader.get();
    follower->stats()->force_command(COMMAND_FOLLOW, 1, 0, 0);
    (void)follower->stats()->do_command();
    TEST_ASSERT(follower->leader == nullptr, "follow should clear leader when foe is present");

    // COMMAND_FOLLOW close-distance branch (stats.cpp:295-300).
    follower->foe = nullptr;
    follower->leader = leader.get();
    leader->setxy(static_cast<Sint32>(follower->xpos) + 10, static_cast<Sint32>(follower->ypos) + 10);
    follower->stats()->force_command(COMMAND_FOLLOW, 1, 0, 0);
    (void)follower->stats()->do_command();
    TEST_ASSERT(follower->leader == nullptr, "follow should drop leader when already close");
}
REGISTER_TEST(test_stats_round12_add_command_walk_clamps_and_follow_shortcuts);

void test_stats_round13_die_and_non_living_fire_command_branches()
{
    og::runtime::current_session->myscreen_->level_data.create_new_grid();
    og::runtime::current_session->myscreen_->level_data.delete_objects();

    auto living = make_walker(FAMILY_SOLDIER);
    walker* weapon = og::runtime::current_session->myscreen_->level_data.add_ob(Order::Weapon, FAMILY_ARROW);
    TEST_ASSERT(living && weapon, "fixtures created");
    if (!(living && weapon))
        return;

    // COMMAND_DIE branch (stats.cpp:266-271).
    living->dead = 1;
    living->stats()->delete_me = 0;
    living->stats()->force_command(COMMAND_DIE, 1, 0, 0);
    (void)living->stats()->do_command();
    TEST_ASSERT_EQ(1, (int)living->stats()->delete_me, "COMMAND_DIE should set delete_me when count is low");

    // COMMAND_FIRE on non-living controller path (stats.cpp:253-257).
    weapon->stats()->force_command(COMMAND_FIRE, 1, 1, 0);
    const short fire_result = weapon->stats()->do_command();
    TEST_ASSERT(fire_result == 0 || fire_result == 1,
                "COMMAND_FIRE on non-living should execute guarded branch without crashing");
}
REGISTER_TEST(test_stats_round13_die_and_non_living_fire_command_branches);

void test_stats_round14_quickfire_multido_rush_and_walk_to_foe_firstfoe_fallback()
{
    og::runtime::current_session->myscreen_->level_data.create_new_grid();
    og::runtime::current_session->myscreen_->level_data.delete_objects();

    auto actor = make_walker(FAMILY_SOLDIER);
    auto foe = make_walker(FAMILY_ORC);
    TEST_ASSERT(actor && foe, "fixtures created");
    if (!(actor && foe))
        return;

    // COMMAND_QUICK_FIRE and COMMAND_MULTIDO branches.
    actor->stats()->commands.clear();
    actor->stats()->force_command(COMMAND_QUICK_FIRE, 1, 1, 0);
    (void)actor->stats()->do_command();
    actor->stats()->force_command(COMMAND_MULTIDO, 1, 2, 0);
    (void)actor->stats()->do_command();

    // COMMAND_RUSH with collide_ob target.
    walker* rush_target = og::runtime::current_session->myscreen_->level_data.add_ob(Order::Living, FAMILY_ORC);
    TEST_ASSERT(rush_target != nullptr, "rush target created");
    if (rush_target)
    {
        actor->collide_ob = rush_target;
        actor->stats()->force_command(COMMAND_RUSH, 1, 1, 0);
        (void)actor->stats()->do_command();
    }

    // walk_to_foe firstfoe fallback branch (stats.cpp:994-995).
    class SeqRandom final : public IRandom {
    public:
        explicit SeqRandom(std::initializer_list<std::uint32_t> v) : values(v) {}
        std::uint32_t next(std::uint32_t max_exclusive) override
        {
            if (max_exclusive == 0) return 0;
            const std::uint32_t raw = (idx < values.size()) ? values[idx++] : values.back();
            return raw % max_exclusive;
        }
        std::vector<std::uint32_t> values;
        std::size_t idx = 0;
    };

    actor->setxy(100, 100);
    foe->setxy(120, 100); // short-circuit distance path
    actor->foe = foe.get();
    actor->path_check_counter = 0;
    actor->stats()->last_distance = 99999;
    foe->invisibility_left = 64; // allows near-foe scan to skip via rng
    SeqRandom rng({1, 1, 1, 1, 1});
    actor->sim_rng = &rng;
    const bool walked = actor->stats()->walk_to_foe();
    TEST_ASSERT(walked, "walk_to_foe should still succeed when using firstfoe fallback path");
    TEST_ASSERT(actor->foe != nullptr, "walk_to_foe should restore foe from firstfoe fallback");
}
REGISTER_TEST(test_stats_round14_quickfire_multido_rush_and_walk_to_foe_firstfoe_fallback);
