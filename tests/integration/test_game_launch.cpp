#include <openglad/core/test_trace.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <gtest/gtest.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>
// myscreen is now a macro defined in base.h (via game_session.h)

short load_saved_game(const char *filename, screen *scr);

namespace {

// Live hostile livings across the world's three lists — the number the
// completed-level purge zeroes and an armed replay restores (#207).
int count_live_hostile_livings(GameWorld& world)
{
    int hostiles = 0;
    const auto count_list = [&hostiles](auto& list) {
        for (auto& uptr : list)
        {
            walker* const w = uptr.get();
            if (w == nullptr || w->dead())
                continue;
            if (w->query_order() == Order::Living && w->team_num() != 0 &&
                w->myguy == nullptr)
                ++hostiles;
        }
    };
    count_list(world.oblist);
    count_list(world.weaplist);
    count_list(world.fxlist);
    return hostiles;
}

} // namespace

// #207 on the SDL production launch path (the headless twin is pinned in
// test_headless_server_runtime): load_saved_game is the load the shipped
// game takes — camp row -> GO -> shadow install -> load_saved_game — and
// its completed-level purge, purge-skip conjunct and arm carry across the
// disk round-trip had no executing test.
TEST(GameLaunch, armed_replay_load_restores_completed_level_census)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    screen* const game = og::runtime::current_session->myscreen_;
    SaveData& save = game->save_data;
    save.reset();
    save.current_campaign = "gladiator";
    save.current_levels["gladiator"] = 1;
    save.scen_num = 1;
    save.numplayers = 1;
    save.add_level_completed("gladiator", 1);
    ASSERT_TRUE(save.save("test_replay_launch"));

    // Control: the unarmed load of a completed level keeps the classic
    // purge (the VISIT walk-through).
    ASSERT_NE(0, load_saved_game("test_replay_launch", game));
    EXPECT_EQ(0, count_live_hostile_livings(game->world()))
        << "an unarmed completed load must purge";
    game->world().delete_objects();

    // Armed: SaveData::load clears the transient pair, so this executes the
    // carry (the loaded cursor still points at the armed level) AND the
    // purge-skip conjunct — the authored census loads.
    save.arm_replay(1);
    ASSERT_NE(0, load_saved_game("test_replay_launch", game));
    EXPECT_EQ(1, static_cast<int>(save.replay_level))
        << "the arm must survive the disk round-trip";
    EXPECT_EQ(12, count_live_hostile_livings(game->world()))
        << "SOUTH OF TALWOOD's 12 elves are back on an armed replay";
    game->world().delete_objects();

    // A stale arm for ANOTHER level dies at the reload (the loaded cursor
    // does not match), and the purge applies as on any plain visit.
    save.clear_replay_arm();
    save.replay_level = 3;   // stale by hand: the disk cursor stays 1
    save.replay_origin = 5;
    ASSERT_NE(0, load_saved_game("test_replay_launch", game));
    EXPECT_EQ(0, static_cast<int>(save.replay_level))
        << "the carry must drop an arm the loaded cursor does not cover";
    EXPECT_EQ(0, count_live_hostile_livings(game->world()));
    game->world().delete_objects();
    save.reset();
}

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
