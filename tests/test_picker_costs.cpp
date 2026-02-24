#include <openglad/entities/guy.h>
#include <openglad/data/save_data.h>
#include <openglad/ui/picker_common.h>
#include <openglad/runtime/screen.h>
#include "test_framework.h"
#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

void test_picker_increase_decrease_stats_and_levels()
{
    // Put a soldier on the team so TrainSession has something to edit
    auto* team0 = new guy(FAMILY_SOLDIER);
    team0->upgrade_to_level(2);
    og::runtime::current_session->myscreen_->save_data.team_list[0].reset(team0);
    og::runtime::current_session->myscreen_->save_data.team_size = 1;

    og::ui::TrainSession session(og::runtime::current_session->myscreen_->save_data);
    TEST_ASSERT(!session.empty(), "session should not be empty");

    using S = og::ui::TrainSession::Stat;

    int str0 = session.working_copy().strength;
    session.increase_stat(S::Strength, 1);
    TEST_ASSERT_EQ(str0 + 1, (int)session.working_copy().strength,
                   "increase_stat should increment STR");

    int str1 = session.working_copy().strength;
    session.decrease_stat(S::Strength, 1);
    TEST_ASSERT_EQ(str1 - 1, (int)session.working_copy().strength,
                   "decrease_stat should decrement STR");

    // If a stat has already increased relative to original, level increase should be blocked.
    session.increase_stat(S::Strength, 1);
    int level_before = session.working_copy().level;
    session.increase_stat(S::Level, 1);
    TEST_ASSERT_EQ(level_before, (int)session.working_copy().level,
                   "level increase should be blocked when stats already increased");

    // Reset stat back to original, then level-up should be allowed.
    session.decrease_stat(S::Strength, 1);
    TEST_ASSERT(!session.stats_increased(), "stats should match original after undo");
    level_before = session.working_copy().level;
    session.increase_stat(S::Level, 1);
    TEST_ASSERT(session.working_copy().level >= level_before,
                "level should not decrease on increase_stat(level)");

    // When level is higher than original, stat decreases should be blocked.
    TEST_ASSERT(session.level_increased(), "level should be marked as increased");
    int dex0 = session.working_copy().dexterity;
    session.decrease_stat(S::Dexterity, 1);
    TEST_ASSERT_EQ(dex0, (int)session.working_copy().dexterity,
                   "stat decrease should be blocked when level increased");

    og::runtime::current_session->myscreen_->save_data.team_list[0].reset();
    og::runtime::current_session->myscreen_->save_data.team_size = 0;
}
REGISTER_TEST(test_picker_increase_decrease_stats_and_levels);

void test_picker_calculate_hire_cost_clamps_to_minimums()
{
    // HireSession creates a recruit with stats clamped to family base.
    // Verify the cost is positive and stats are at least the base values.
    og::ui::HireSession session(og::runtime::current_session->myscreen_->save_data, 0);
    const guy* recruit = session.current_recruit();
    TEST_ASSERT(recruit != nullptr, "recruit should exist");

    std::uint32_t cost = session.current_cost();
    TEST_ASSERT(cost > 0, "hire cost should be positive");
    TEST_ASSERT(recruit->strength > 0, "strength should be positive");
    TEST_ASSERT(recruit->dexterity > 0, "dexterity should be positive");
    TEST_ASSERT(recruit->constitution > 0, "constitution should be positive");
    TEST_ASSERT(recruit->intelligence > 0, "intelligence should be positive");
    TEST_ASSERT(recruit->armor > 0, "armor should be positive");
    TEST_ASSERT(recruit->level >= 1, "level should be at least 1");
}
REGISTER_TEST(test_picker_calculate_hire_cost_clamps_to_minimums);

void test_picker_calculate_train_cost_basic_and_level_upgrade_paths()
{
    auto* team0 = new guy(FAMILY_SOLDIER);
    team0->upgrade_to_level(2);
    og::runtime::current_session->myscreen_->save_data.team_list[0].reset(team0);
    og::runtime::current_session->myscreen_->save_data.team_size = 1;

    og::ui::TrainSession session(og::runtime::current_session->myscreen_->save_data);
    TEST_ASSERT(!session.empty(), "session should not be empty");

    using S = og::ui::TrainSession::Stat;

    // Increase a stat and verify cost is positive.
    session.increase_stat(S::Strength, 2);
    std::uint32_t cost_stats = session.current_cost();
    TEST_ASSERT(cost_stats > 0, "training cost should be positive for stat increases");

    // Reset stats, increase level to exercise level-up cost path.
    session.decrease_stat(S::Strength, 2);
    session.increase_stat(S::Level, 1);
    std::uint32_t cost_level = session.current_cost();
    (void)cost_level; // just exercise the level-up path

    og::runtime::current_session->myscreen_->save_data.team_list[0].reset();
    og::runtime::current_session->myscreen_->save_data.team_size = 0;
}
REGISTER_TEST(test_picker_calculate_train_cost_basic_and_level_upgrade_paths);
