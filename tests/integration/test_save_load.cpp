#include <openglad/core/test_trace.h>
#include <gtest/gtest.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>
#include <openglad/interface/screen.h>
#include <openglad/platform/game_session.h>
// myscreen is now a macro defined in base.h (via game_session.h)

namespace {

bool prepare_default_save_load_state()
{
    restore_default_campaigns();
    restore_default_settings();
#ifdef TESTING
    set_mounted_campaign_for_testing("");
#endif
    og::runtime::current_session->myscreen_->save_data.current_campaign = "gladiator";
    return mount_campaign_package_with_error("gladiator") == CampaignPackageIoError::None;
}

} // namespace

TEST(SaveLoad, roundtrip) {
    ASSERT_TRUE(prepare_default_save_load_state()) << "default campaign should be restored and mounted before save/load roundtrip";
    // Set up known values
    og::runtime::current_session->myscreen_->save_data.scen_num = 3;
    og::runtime::current_session->myscreen_->save_data.totalcash = 12345;
    og::runtime::current_session->myscreen_->save_data.totalscore = 67890;
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;

    // Save
    trace_clear();
    bool saved = og::runtime::current_session->myscreen_->save_data.save("test_save");
    ASSERT_TRUE(saved) << "save should succeed";
    ASSERT_TRUE(trace_contains("save", "SaveData::save")) << "save trace should be logged";
    ASSERT_TRUE(trace_contains("save", "SaveData::save complete")) << "save complete trace should be logged";

    // Modify values to prove load restores them
    og::runtime::current_session->myscreen_->save_data.scen_num = 999;
    og::runtime::current_session->myscreen_->save_data.totalcash = 0;
    og::runtime::current_session->myscreen_->save_data.totalscore = 0;

    // Load
    trace_clear();
    bool loaded = og::runtime::current_session->myscreen_->save_data.load("test_save");
    ASSERT_TRUE(loaded) << "load should succeed";
    ASSERT_TRUE(trace_contains("load", "SaveData::load")) << "load trace should be logged";
    ASSERT_TRUE(trace_contains("load", "SaveData::load complete")) << "load complete trace should be logged";

    // Verify restored values
    ASSERT_EQ(3, og::runtime::current_session->myscreen_->save_data.scen_num) << "scen_num should be restored";
    ASSERT_EQ(12345, static_cast<int>(og::runtime::current_session->myscreen_->save_data.totalcash)) << "totalcash should be restored";
    ASSERT_EQ(67890, static_cast<int>(og::runtime::current_session->myscreen_->save_data.totalscore)) << "totalscore should be restored";
}


TEST(SaveLoad, load_saved_game_with_error_null_screen)
{
    LoadSavedGameError err = load_saved_game_with_error("save0", nullptr);
    ASSERT_EQ(static_cast<int>(LoadSavedGameError::MissingScreen), static_cast<int>(err)) << "load_saved_game_with_error should report MissingScreen on nullptr";
}


TEST(SaveLoad, load_saved_game_with_error_reports_fallback_level)
{
    ASSERT_TRUE(prepare_default_save_load_state()) << "default campaign should be restored and mounted before fallback-level save/load";
    const short old_scen = og::runtime::current_session->myscreen_->save_data.scen_num;
    const int old_level_id = og::runtime::current_session->myscreen_->world().id;

    // Use an invalid scenario id so loader falls back to scenario 1.
    og::runtime::current_session->myscreen_->save_data.scen_num = 9999;
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.team_size = 0;
    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_data.save("save0"))
        << "fixture save0 should exist before load_saved_game_with_error";

    LoadSavedGameError err = load_saved_game_with_error("save0", og::runtime::current_session->myscreen_);
    ASSERT_EQ(static_cast<int>(LoadSavedGameError::UsedFallbackLevel), static_cast<int>(err)) << "load_saved_game_with_error should report UsedFallbackLevel for missing scenario";
    ASSERT_EQ(1, og::runtime::current_session->myscreen_->save_data.scen_num) << "fallback should update save_data.scen_num to 1";

    og::runtime::current_session->myscreen_->save_data.scen_num = old_scen;
    og::runtime::current_session->myscreen_->world().id = old_level_id;
}

TEST(SaveLoad, load_saved_game_reads_named_save_into_fresh_screen)
{
    ASSERT_TRUE(prepare_default_save_load_state())
        << "default campaign should be restored and mounted before cross-session load";

    screen* const source_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(source_screen != nullptr);

    source_screen->save_data.reset();
    source_screen->save_data.current_campaign = "gladiator";
    source_screen->save_data.scen_num = 17;
    source_screen->save_data.numplayers = 1;
    ASSERT_TRUE(source_screen->save_data.save("test_shadow_load"))
        << "fixture save should succeed";

    og::runtime::GameSession::Config session_cfg;
    session_cfg.create_display = false;
    session_cfg.install_legacy_globals = false;
    og::runtime::GameSession fresh_session(session_cfg);
    auto fresh_scope = fresh_session.activate();

    screen* const fresh_screen = fresh_session.myscreen_;
    ASSERT_TRUE(fresh_screen != nullptr);
    ASSERT_NE(17, fresh_screen->save_data.scen_num)
        << "fresh session should start with different in-memory save state";

    ASSERT_TRUE(load_saved_game("test_shadow_load", fresh_screen) != 0)
        << "fresh screen should load the named save file";
    EXPECT_EQ(17, fresh_screen->save_data.scen_num)
        << "load_saved_game should populate save_data from disk";
    EXPECT_EQ(17, fresh_screen->world().id)
        << "loaded world should match the save file scenario";
}
