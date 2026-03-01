#include <openglad/runtime/screen.h>
#include "test_framework.h"

#include <algorithm>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

// From picker.cpp
std::vector<int> get_accessible_levels();

static bool contains(const std::vector<int>& v, int x)
{
    return std::find(v.begin(), v.end(), x) != v.end();
}

void test_picker_get_accessible_levels_always_has_level1_and_current()
{
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 3;

    std::vector<int> levels = get_accessible_levels();
    TEST_ASSERT(contains(levels, 1), "level 1 should always be accessible");
    TEST_ASSERT(contains(levels, 3), "current level should be accessible");
}
REGISTER_TEST(test_picker_get_accessible_levels_always_has_level1_and_current);

void test_picker_get_accessible_levels_includes_exits_of_cleared_levels()
{
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;

    // Mark level 1 as cleared; get_accessible_levels should attempt to load it and add exits.
    og::runtime::current_session->myscreen_->save_data.add_level_completed(og::runtime::current_session->myscreen_->save_data.current_campaign, 1);

    std::vector<int> levels = get_accessible_levels();
    TEST_ASSERT(contains(levels, 1), "level 1 should be accessible");

    // Most campaigns have at least one exit from level 1.
    bool has_exit = false;
    for (int id : levels) {
        if (id > 1)
            has_exit = true;
    }
    TEST_ASSERT(has_exit, "cleared level 1 should yield at least one additional accessible level via exits");
}
REGISTER_TEST(test_picker_get_accessible_levels_includes_exits_of_cleared_levels);

void test_picker_get_accessible_levels_handles_missing_leveldata()
{
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;

    // Add a bogus "completed" level id to force LevelRuntimeData::load() failure path.
    og::runtime::current_session->myscreen_->save_data.completed_levels[og::runtime::current_session->myscreen_->save_data.current_campaign].insert(9999);

    std::vector<int> levels = get_accessible_levels();
    TEST_ASSERT(contains(levels, 1), "level 1 should still be accessible");
    TEST_ASSERT(contains(levels, 9999), "bogus completed level id should still be included as accessible");
}
REGISTER_TEST(test_picker_get_accessible_levels_handles_missing_leveldata);
