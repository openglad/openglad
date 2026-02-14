#include <openglad/legacy/graph.h>
#include <openglad/entities/guy.h>
#include <openglad/data/save_data.h>
#include "test_framework.h"
#include <memory>

extern screen* myscreen;

// From picker.cpp
extern std::unique_ptr<guy> current_guy;
extern guy* old_guy;
extern Sint32 editguy;

Sint32 increase_stat(Sint32 whatstat, Sint32 howmuch);
Sint32 decrease_stat(Sint32 whatstat, Sint32 howmuch);
Uint32 calculate_hire_cost();
Uint32 calculate_train_cost(guy* oldguy);

// Stat button constants from picker.cpp
#define BUT_STR 0
#define BUT_DEX 1
#define BUT_CON 2
#define BUT_INT 3
#define BUT_ARMOR 4
#define BUT_LEVEL 5

static constexpr int RET_OK = 4; // picker.cpp's OK macro

struct PickerGuyOverride
{
    std::unique_ptr<guy> prev_current;
    guy* prev_old = nullptr;
    Sint32 prev_editguy = 0;

    PickerGuyOverride(std::unique_ptr<guy> cur, guy* old, Sint32 eg)
    {
        prev_current = std::move(current_guy);
        prev_old = old_guy;
        prev_editguy = editguy;
        current_guy = std::move(cur);
        old_guy = old;
        editguy = eg;
    }

    ~PickerGuyOverride()
    {
        current_guy = std::move(prev_current);
        old_guy = prev_old;
        editguy = prev_editguy;
    }
};

void test_picker_increase_decrease_stats_and_levels()
{
    guy* team0 = new guy(FAMILY_SOLDIER);
    team0->upgrade_to_level(1);
    myscreen->save_data.team_list[0].reset(team0);
    myscreen->save_data.team_size = 1;

    auto cur = std::make_unique<guy>(FAMILY_SOLDIER);
    guy* cur_raw = cur.get();
    auto old = std::make_unique<guy>(FAMILY_SOLDIER);
    cur_raw->upgrade_to_level(2);
    old->upgrade_to_level(2);

    PickerGuyOverride guard(std::move(cur), old.get(), 0);

    int str0 = cur_raw->strength;
    TEST_ASSERT_EQ(RET_OK, (int)increase_stat(BUT_STR, 1), "increase_stat should return OK");
    TEST_ASSERT_EQ(str0 + 1, (int)cur_raw->strength, "increase_stat should increment STR");

    int str1 = cur_raw->strength;
    TEST_ASSERT_EQ(RET_OK, (int)decrease_stat(BUT_STR, 1), "decrease_stat should return OK");
    TEST_ASSERT_EQ(str1 - 1, (int)cur_raw->strength, "decrease_stat should decrement STR");

    // If a stat has already increased relative to old_guy, increasing level should be blocked.
    cur_raw->strength = old->strength + 1;
    int level_before = cur_raw->level;
    TEST_ASSERT_EQ(RET_OK, (int)increase_stat(BUT_LEVEL, 1), "increase_stat(level) should return OK");
    TEST_ASSERT_EQ(level_before, (int)cur_raw->level, "level increase should be blocked when stats already increased");

    // If no stats are increased, level-up is allowed.
    cur_raw->strength = old->strength;
    level_before = cur_raw->level;
    TEST_ASSERT_EQ(RET_OK, (int)increase_stat(BUT_LEVEL, 1), "increase_stat(level) should return OK");
    TEST_ASSERT(cur_raw->level >= level_before, "level should not decrease on increase_stat(level)");

    // When the current level is higher than old_guy, stat decreases should be blocked (level_increased path).
    old->upgrade_to_level(1);
    cur_raw->upgrade_to_level(3);
    int dex0 = cur_raw->dexterity;
    TEST_ASSERT_EQ(RET_OK, (int)decrease_stat(BUT_DEX, 1), "decrease_stat should return OK");
    TEST_ASSERT_EQ(dex0, (int)cur_raw->dexterity, "stat decrease should be blocked when level increased");

    myscreen->save_data.team_list[0].reset();
    myscreen->save_data.team_size = 0;
}
REGISTER_TEST(test_picker_increase_decrease_stats_and_levels);

void test_picker_calculate_hire_cost_clamps_to_minimums()
{
    auto cur = std::make_unique<guy>(FAMILY_SOLDIER);
    guy* cur_raw = cur.get();
    auto old = std::make_unique<guy>(FAMILY_SOLDIER);
    PickerGuyOverride guard(std::move(cur), old.get(), 0);

    // Force stats below baseline so calculate_hire_cost has to clamp them up.
    cur_raw->strength = 0;
    cur_raw->dexterity = 0;
    cur_raw->constitution = 0;
    cur_raw->intelligence = 0;
    cur_raw->armor = 0;
    cur_raw->level = 0;

    Uint32 cost = calculate_hire_cost();
    TEST_ASSERT(cost > 0, "hire cost should be positive");
    TEST_ASSERT(cur_raw->strength > 0, "strength should be clamped up");
    TEST_ASSERT(cur_raw->dexterity > 0, "dexterity should be clamped up");
    TEST_ASSERT(cur_raw->constitution > 0, "constitution should be clamped up");
    TEST_ASSERT(cur_raw->intelligence > 0, "intelligence should be clamped up");
    TEST_ASSERT(cur_raw->armor > 0, "armor should be clamped up");
    TEST_ASSERT(cur_raw->level >= 1, "level should be clamped up");
}
REGISTER_TEST(test_picker_calculate_hire_cost_clamps_to_minimums);

void test_picker_calculate_train_cost_basic_and_level_upgrade_paths()
{
    guy* baseline = new guy(FAMILY_SOLDIER);
    baseline->upgrade_to_level(2);

    auto cur = std::make_unique<guy>(FAMILY_SOLDIER);
    guy* cur_raw = cur.get();
    cur_raw->upgrade_to_level(2);
    cur_raw->strength = baseline->strength + 2;

    auto old = std::make_unique<guy>(FAMILY_SOLDIER);
    old->upgrade_to_level(2);
    PickerGuyOverride guard(std::move(cur), old.get(), 0);

    Uint32 cost_stats = calculate_train_cost(baseline);
    TEST_ASSERT(cost_stats > 0, "training cost should be positive for stat increases");

    // Now upgrade level above old_guy->level to hit the path that skips stat costs.
    cur_raw->upgrade_to_level(4);
    Uint32 cost_level = calculate_train_cost(baseline);
    (void)cost_level; // just exercise the level-up path

    delete baseline;
}
REGISTER_TEST(test_picker_calculate_train_cost_basic_and_level_upgrade_paths);
