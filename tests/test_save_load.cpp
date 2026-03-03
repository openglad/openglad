#include <openglad/legacy/test_trace.h>
#include "test_framework.h"
#include <openglad/resources/save_data.h>
#include <openglad/interface/screen.h>
// myscreen is now a macro defined in base.h (via game_session.h)

void test_save_load_roundtrip() {
    // Set up known values
    og::runtime::current_session->myscreen_->save_data.scen_num = 3;
    og::runtime::current_session->myscreen_->save_data.totalcash = 12345;
    og::runtime::current_session->myscreen_->save_data.totalscore = 67890;
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;

    // Save
    trace_clear();
    bool saved = og::runtime::current_session->myscreen_->save_data.save("test_save");
    TEST_ASSERT(saved, "save should succeed");
    TEST_ASSERT(trace_contains("save", "SaveData::save"), "save trace should be logged");
    TEST_ASSERT(trace_contains("save", "SaveData::save complete"), "save complete trace should be logged");

    // Modify values to prove load restores them
    og::runtime::current_session->myscreen_->save_data.scen_num = 999;
    og::runtime::current_session->myscreen_->save_data.totalcash = 0;
    og::runtime::current_session->myscreen_->save_data.totalscore = 0;

    // Load
    trace_clear();
    bool loaded = og::runtime::current_session->myscreen_->save_data.load("test_save");
    TEST_ASSERT(loaded, "load should succeed");
    TEST_ASSERT(trace_contains("load", "SaveData::load"), "load trace should be logged");
    TEST_ASSERT(trace_contains("load", "SaveData::load complete"), "load complete trace should be logged");

    // Verify restored values
    TEST_ASSERT_EQ(3, og::runtime::current_session->myscreen_->save_data.scen_num, "scen_num should be restored");
    TEST_ASSERT_EQ(12345, static_cast<int>(og::runtime::current_session->myscreen_->save_data.totalcash), "totalcash should be restored");
    TEST_ASSERT_EQ(67890, static_cast<int>(og::runtime::current_session->myscreen_->save_data.totalscore), "totalscore should be restored");
}
REGISTER_TEST(test_save_load_roundtrip);

void test_load_saved_game_with_error_null_screen()
{
    LoadSavedGameError err = load_saved_game_with_error("save0", nullptr);
    TEST_ASSERT_EQ(static_cast<int>(LoadSavedGameError::MissingScreen), static_cast<int>(err),
        "load_saved_game_with_error should report MissingScreen on nullptr");
}
REGISTER_TEST(test_load_saved_game_with_error_null_screen);

void test_load_saved_game_with_error_reports_fallback_level()
{
    const short old_scen = og::runtime::current_session->myscreen_->save_data.scen_num;
    const int old_level_id = og::runtime::current_session->myscreen_->world().id;

    // Use an invalid scenario id so loader falls back to scenario 1.
    og::runtime::current_session->myscreen_->save_data.scen_num = 9999;
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.team_size = 0;

    LoadSavedGameError err = load_saved_game_with_error("save0", og::runtime::current_session->myscreen_);
    TEST_ASSERT_EQ(static_cast<int>(LoadSavedGameError::UsedFallbackLevel), static_cast<int>(err),
        "load_saved_game_with_error should report UsedFallbackLevel for missing scenario");
    TEST_ASSERT_EQ(1, og::runtime::current_session->myscreen_->save_data.scen_num, "fallback should update save_data.scen_num to 1");

    og::runtime::current_session->myscreen_->save_data.scen_num = old_scen;
    og::runtime::current_session->myscreen_->world().id = old_level_id;
}
REGISTER_TEST(test_load_saved_game_with_error_reports_fallback_level);
