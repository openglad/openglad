#include <openglad/legacy/base.h>
#include <openglad/core/stats.h>
#include <openglad/data/gloader.h>
#include <openglad/entities/walker.h>
#include <openglad/runtime/screen.h>
#include "test_framework.h"
#include <memory>

extern screen* myscreen;

static std::unique_ptr<walker> create_living(char family)
{
    loader* l = myscreen->level_data.myloader.get();
    if (!l)
        return nullptr;
    auto w = l->create_walker_owned(Order::Living, family);
    if (!w)
        return nullptr;
    w->setxy(50, 50);
    return w;
}

void test_statistics_bit_flags_set_clear()
{
    auto w = create_living(FAMILY_FAERIE);
    TEST_ASSERT(w != nullptr, "create_walker(faerie) should succeed");

    // Start clean.
    w->stats()->clear_bit_flags();
    TEST_ASSERT(!w->stats()->query_bit_flags(BIT_FLYING), "BIT_FLYING should be cleared");

    w->stats()->set_bit_flags(BIT_FLYING, 1);
    TEST_ASSERT(w->stats()->query_bit_flags(BIT_FLYING), "BIT_FLYING should be set");

    w->stats()->set_bit_flags(BIT_FLYING, 0);
    TEST_ASSERT(!w->stats()->query_bit_flags(BIT_FLYING), "BIT_FLYING should be cleared after setting to 0");

}
REGISTER_TEST(test_statistics_bit_flags_set_clear);

void test_statistics_add_command_walk_clamps_direction()
{
    auto w = create_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "create_walker(soldier) should succeed");

    w->stats()->commands.clear();
    w->stats()->add_command(COMMAND_WALK, 5, 99, -99);
    TEST_ASSERT(!w->stats()->commands.empty(), "add_command should append a command");

    const command& c = w->stats()->commands.back();
    TEST_ASSERT_EQ(COMMAND_WALK, c.commandtype, "commandtype should be COMMAND_WALK");
    TEST_ASSERT_EQ(1, c.com1, "walk com1 should be clamped to [-1,1]");
    TEST_ASSERT_EQ(-1, c.com2, "walk com2 should be clamped to [-1,1]");

}
REGISTER_TEST(test_statistics_add_command_walk_clamps_direction);

void test_statistics_clear_command_restores_weapon_and_team()
{
    auto w = create_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "create_walker(soldier) should succeed");

    // Simulate being charmed / weapon-swapped.
    w->default_weapon = FAMILY_KNIFE;
    w->current_weapon = FAMILY_ARROW;
    w->team_num = 2;
    w->real_team_num = 0;
    w->leader = reinterpret_cast<walker*>(0x1); // only checked for nullptr

    w->stats()->clear_command();

    TEST_ASSERT_EQ((int)w->default_weapon, (int)w->current_weapon, "clear_command should restore current_weapon");
    TEST_ASSERT_EQ(0, (int)w->team_num, "clear_command should restore team_num from real_team_num");
    TEST_ASSERT_EQ(255, (int)w->real_team_num, "clear_command should reset real_team_num to 255");
    TEST_ASSERT(w->leader == nullptr, "clear_command should clear leader");

}
REGISTER_TEST(test_statistics_clear_command_restores_weapon_and_team);
