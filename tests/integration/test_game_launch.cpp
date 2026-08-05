#include <openglad/core/test_trace.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <gtest/gtest.h>
#include <openglad/resources/save_data.h>
// myscreen is now a macro defined in base.h (via game_session.h)

short load_saved_game(const char *filename, screen *scr);

TEST(GameLaunch, level_loading) {
    // Set up save_data for scenario 1
    og::runtime::current_session->myscreen_->save_data.scen_num = static_cast<short>(1);
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;

    // Save a minimal save file so load_saved_game has something to load
    og::runtime::current_session->myscreen_->save_data.save("test_level_save");

    trace_clear();
    short result = load_saved_game("test_level_save", og::runtime::current_session->myscreen_);
    (void)result;
    // Check the traces were fired regardless of full success
    ASSERT_TRUE(trace_contains("game", "load_saved_game")) << "load_saved_game trace should be logged";
    ASSERT_TRUE(trace_contains("game", "LevelRuntimeData::load")) << "LevelRuntimeData::load trace should be logged";

    // Clean up loaded objects
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(GameLaunch, level_load_initial_view_centers_on_player_control)
{
    og::runtime::current_session->myscreen_->save_data.scen_num = static_cast<short>(1);
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.save("test_level_initial_view_focus");

    short result = load_saved_game("test_level_initial_view_focus", og::runtime::current_session->myscreen_);
    ASSERT_TRUE(result != 0) << "load_saved_game should succeed";

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen 0 should exist";
    if (!vs)
        return;

    ASSERT_TRUE(vs->control != nullptr) << "control should be initialized on level load";
    if (!vs->control)
        return;

    // Simulate the first render pass during level intro.
    og::runtime::current_session->myscreen_->redraw();

    const Sint32 expected_topx = vs->control->xpos() - (vs->xview - vs->control->sizex()) / 2;
    const Sint32 expected_topy = vs->control->ypos() - (vs->yview - vs->control->sizey()) / 2;
    ASSERT_EQ(expected_topx, vs->topx) << "initial redraw should center viewport x on control";
    ASSERT_EQ(expected_topy, vs->topy) << "initial redraw should center viewport y on control";

    og::runtime::current_session->myscreen_->world().delete_objects();
}

TEST(GameLaunch, reordered_same_team_roster_sets_initial_player_controls)
{
    screen* const game = og::runtime::current_session->myscreen_;
    SaveData& save = game->save_data;
    save.reset();
    save.current_campaign = "gladiator";
    save.scen_num = 1;
    save.numplayers = 2;
    save.my_team = 0;
    save.allied_mode = 1;

    auto first = std::make_unique<guy>(FAMILY_SOLDIER);
    first->name = "FIRST CONTROL";
    first->teamnum = 0;
    auto second = std::make_unique<guy>(FAMILY_ARCHER);
    second->name = "SECOND CONTROL";
    second->teamnum = 0;
    save.team_list[0] = std::move(first);
    save.team_list[1] = std::move(second);
    save.team_size = 2;

    ASSERT_EQ(0, og::ui::move_team_member_up(save, 1));
    ASSERT_TRUE(load_saved_game("", game));
    ASSERT_NE(nullptr, game->viewob[0]);
    ASSERT_NE(nullptr, game->viewob[1]);
    ASSERT_NE(nullptr, game->viewob[0]->control);
    ASSERT_NE(nullptr, game->viewob[1]->control);
    ASSERT_NE(nullptr, game->viewob[0]->control->myguy);
    ASSERT_NE(nullptr, game->viewob[1]->control->myguy);
    EXPECT_EQ("SECOND CONTROL", game->viewob[0]->control->myguy->name);
    EXPECT_EQ("FIRST CONTROL", game->viewob[1]->control->myguy->name);

    game->world().delete_objects();
    save.reset();
}
