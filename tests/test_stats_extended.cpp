#include <openglad/legacy/base.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/screen.h>
#include "test_framework.h"
#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

static std::unique_ptr<walker> create_living(char family)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    if (!l) return nullptr;
    auto w = l->create_walker_owned(Order::Living, family);
    if (!w) return nullptr;
    w->setxy(50, 50);
    return w;
}

// ---------------------------------------------------------------------------
// force_command tests
// ---------------------------------------------------------------------------

TEST(StatsExtended, statistics_force_command_prepends)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    w->stats()->commands.clear();
    w->stats()->add_command(COMMAND_WALK, 5, 1, 0);
    w->stats()->force_command(COMMAND_FIRE, 3, 0, 0);

    ASSERT_TRUE(w->stats()->commands.size() >= 2) << "should have at least 2 commands";
    ASSERT_EQ(COMMAND_FIRE, w->stats()->commands.front().commandtype) << "force_command should prepend";

}


TEST(StatsExtended, statistics_force_command_walk_clamps)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    w->stats()->commands.clear();
    w->stats()->force_command(COMMAND_WALK, 10, 50, -50);

    const command& c = w->stats()->commands.front();
    ASSERT_EQ(COMMAND_WALK, c.commandtype) << "should be walk command";
    ASSERT_EQ(1, c.com1) << "com1 should be clamped to 1";
    ASSERT_EQ(-1, c.com2) << "com2 should be clamped to -1";

}


// ---------------------------------------------------------------------------
// has_commands tests
// ---------------------------------------------------------------------------

TEST(StatsExtended, statistics_has_commands_empty)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    w->stats()->commands.clear();
    ASSERT_TRUE(!w->stats()->has_commands()) << "empty queue should have no commands";

}


TEST(StatsExtended, statistics_has_commands_nonempty)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    w->stats()->commands.clear();
    w->stats()->add_command(COMMAND_WALK, 5, 1, 0);
    ASSERT_TRUE(w->stats()->has_commands()) << "non-empty queue should have commands";

}


// ---------------------------------------------------------------------------
// do_command smoke tests
// ---------------------------------------------------------------------------

TEST(StatsExtended, statistics_do_command_walk)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    w->stats()->commands.clear();
    w->stats()->add_command(COMMAND_WALK, 3, 1, 0);

    // Execute the command - should not crash
    w->stats()->do_command();

}


TEST(StatsExtended, statistics_do_command_fire)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";
    w->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));

    w->stats()->commands.clear();
    w->stats()->add_command(COMMAND_FIRE, 3, 0, 0);

    w->stats()->do_command();

}


TEST(StatsExtended, statistics_do_command_attack)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    auto foe = create_living(FAMILY_SMALL_SLIME);
    ASSERT_TRUE(foe != nullptr) << "create foe should succeed";
    foe->team_num = 1;
    foe->setxy(60, 50);
    w->foe = foe.get();
    w->team_num = 0;

    w->stats()->commands.clear();
    w->stats()->add_command(COMMAND_ATTACK, 5, 0, 0);

    w->stats()->do_command();

}


TEST(StatsExtended, statistics_do_command_random_walk)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    w->stats()->commands.clear();
    w->stats()->add_command(COMMAND_RANDOM_WALK, 3, 0, 0);

    w->stats()->do_command();

}


// ---------------------------------------------------------------------------
// forward_blocked / right_blocked smoke tests
// ---------------------------------------------------------------------------

TEST(StatsExtended, statistics_forward_blocked_smoke)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    w->setxy(100, 100);
    w->curdir = FACE_UP;
    bool result = w->stats()->forward_blocked();
    // Just check it doesn't crash
    (void)result;

    w->curdir = FACE_RIGHT;
    result = w->stats()->forward_blocked();
    (void)result;

    w->curdir = FACE_DOWN;
    result = w->stats()->forward_blocked();
    (void)result;

    w->curdir = FACE_LEFT;
    result = w->stats()->forward_blocked();
    (void)result;

}


TEST(StatsExtended, statistics_right_blocked_smoke)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    w->setxy(100, 100);
    for (int dir = 0; dir < 8; dir++) {
        w->curdir = static_cast<char>(dir);
        bool result = w->stats()->right_blocked();
        (void)result;
    }

}


TEST(StatsExtended, statistics_right_forward_blocked_smoke)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    w->setxy(100, 100);
    for (int dir = 0; dir < 8; dir++) {
        w->curdir = static_cast<char>(dir);
        bool result = w->stats()->right_forward_blocked();
        (void)result;
    }

}


TEST(StatsExtended, statistics_right_back_blocked_smoke)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    w->setxy(100, 100);
    for (int dir = 0; dir < 8; dir++) {
        w->curdir = static_cast<char>(dir);
        bool result = w->stats()->right_back_blocked();
        (void)result;
    }

}


// ---------------------------------------------------------------------------
// hit_response smoke test
// ---------------------------------------------------------------------------

TEST(StatsExtended, statistics_hit_response_soldier)
{
    auto w = create_living(FAMILY_SOLDIER);
    auto attacker = create_living(FAMILY_SMALL_SLIME);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";
    ASSERT_TRUE(attacker != nullptr) << "create attacker should succeed";

    w->team_num = 0;
    attacker->team_num = 1;
    w->setxy(100, 100);
    attacker->setxy(110, 100);

    w->stats()->hit_response(attacker.get());

}


TEST(StatsExtended, statistics_hit_response_mage)
{
    auto w = create_living(FAMILY_MAGE);
    auto attacker = create_living(FAMILY_SMALL_SLIME);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";
    ASSERT_TRUE(attacker != nullptr) << "create attacker should succeed";

    w->team_num = 0;
    w->set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
    w->stats()->magicpoints = 100;
    w->stats()->max_magicpoints = 100;
    attacker->team_num = 1;
    w->setxy(100, 100);
    attacker->setxy(110, 100);

    w->stats()->hit_response(attacker.get());

}


TEST(StatsExtended, statistics_hit_response_archer)
{
    auto w = create_living(FAMILY_ARCHER);
    auto attacker = create_living(FAMILY_SMALL_SLIME);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";
    ASSERT_TRUE(attacker != nullptr) << "create attacker should succeed";

    w->team_num = 0;
    attacker->team_num = 1;
    w->setxy(100, 100);
    attacker->setxy(110, 100);

    w->stats()->hit_response(attacker.get());

}


// ---------------------------------------------------------------------------
// set_command / try_command tests
// ---------------------------------------------------------------------------

TEST(StatsExtended, statistics_set_command_basic)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    w->stats()->commands.clear();
    w->stats()->set_command(COMMAND_WALK, 5, 1, 0);

    ASSERT_TRUE(!w->stats()->commands.empty()) << "set_command should add a command";
    ASSERT_EQ(COMMAND_WALK, w->stats()->commands.front().commandtype) << "should be walk";

}


TEST(StatsExtended, statistics_try_command_basic)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    w->stats()->commands.clear();
    w->stats()->try_command(COMMAND_WALK, 5, 1, 0);

    ASSERT_TRUE(!w->stats()->commands.empty()) << "try_command should add a command";

}


// ---------------------------------------------------------------------------
// command constructor test
// ---------------------------------------------------------------------------

TEST(StatsExtended, command_default_constructor)
{
    command c;
    ASSERT_EQ(0, c.commandtype) << "default commandtype should be 0";
    ASSERT_EQ(0, c.commandcount) << "default commandcount should be 0";
    ASSERT_EQ(0, c.com1) << "default com1 should be 0";
    ASSERT_EQ(0, c.com2) << "default com2 should be 0";
}

