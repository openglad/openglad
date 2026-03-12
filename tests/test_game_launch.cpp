#include <openglad/core/test_trace.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <openglad/gameplay/walker.h>
#include "test_framework.h"
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

    const Sint32 expected_topx = vs->control->xpos - (vs->xview - vs->control->sizex) / 2;
    const Sint32 expected_topy = vs->control->ypos - (vs->yview - vs->control->sizey) / 2;
    ASSERT_EQ(expected_topx, vs->topx) << "initial redraw should center viewport x on control";
    ASSERT_EQ(expected_topy, vs->topy) << "initial redraw should center viewport y on control";

    og::runtime::current_session->myscreen_->world().delete_objects();
}

