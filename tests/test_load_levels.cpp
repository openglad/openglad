#include <memory>
#include <openglad/input/button.h>
#include <openglad/runtime/screen.h>
#include <openglad/render/view.h>
#include <openglad/legacy/test_trace.h>
#include "test_framework.h"
#include <openglad/data/save_data.h>
#include <openglad/data/level_data.h>
#include <openglad/entities/guy.h>
// myscreen is now a macro defined in base.h (via game_session.h)

short load_saved_game(const char *filename, screen *scr);

// Test: Load levels 1-10, covering both version 9 and version 6 scenario formats.
//
// Levels 3, 4, 8 use version 6 format which previously had a buffer overflow
// in the description text reader (tempwidth could exceed the 80-byte oneline
// buffer). This test verifies the fix works.

void test_load_multiple_levels() {
    for (int level = 1; level <= 10; level++) {
        trace_clear();

        og::runtime::current_session->myscreen_->save_data.scen_num = static_cast<short>(level);
        og::runtime::current_session->myscreen_->save_data.numplayers = 1;
        og::runtime::current_session->myscreen_->save_data.save("test_level_multi");

        short result = load_saved_game("test_level_multi", og::runtime::current_session->myscreen_);
        (void)result;

        char msg[80];
        snprintf(msg, 80, "level %d should load successfully", level);
        TEST_ASSERT(trace_contains("game", "level loaded"), msg);

        // Clean up loaded objects before loading the next level
        og::runtime::current_session->myscreen_->level_data.delete_objects();
    }
}
REGISTER_TEST(test_load_multiple_levels);


// Test: Level data integrity -- verify that loaded level has sensible data
void test_level_data_integrity() {
    trace_clear();

    og::runtime::current_session->myscreen_->save_data.scen_num = static_cast<short>(1);
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.save("test_level_integrity");

    load_saved_game("test_level_integrity", og::runtime::current_session->myscreen_);

    // Level 1 should have a valid grid
    TEST_ASSERT(og::runtime::current_session->myscreen_->level_data.grid.valid(), "level 1 should have a valid grid");

    // Level 1 should have some objects (enemies)
    TEST_ASSERT(!og::runtime::current_session->myscreen_->level_data.oblist.empty(),
        "level 1 should have objects (enemies/npcs)");

    // Level ID should match what we requested
    TEST_ASSERT_EQ(1, og::runtime::current_session->myscreen_->level_data.id, "level id should be 1");

    og::runtime::current_session->myscreen_->level_data.delete_objects();
}
REGISTER_TEST(test_level_data_integrity);


// Test: Loading a nonexistent level falls back to level 1
void test_level_fallback() {
    trace_clear();

    og::runtime::current_session->myscreen_->save_data.scen_num = 9999;  // This level shouldn't exist
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.save("test_level_fallback");

    load_saved_game("test_level_fallback", og::runtime::current_session->myscreen_);

    // Should have fallen back to level 1
    TEST_ASSERT_EQ(1, og::runtime::current_session->myscreen_->level_data.id,
        "nonexistent level should fall back to level 1");

    og::runtime::current_session->myscreen_->level_data.delete_objects();
}
REGISTER_TEST(test_level_fallback);

// Regression: saved multiplayer teams must map to views by saved team ids,
// not by view index.
void test_load_saved_game_maps_views_to_saved_team_ids() {
    trace_clear();

    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.numplayers = 2;

    auto team1 = std::make_unique<guy>(FAMILY_SOLDIER);
    team1->name = "TEAM1";
    team1->teamnum = 1;
    og::runtime::current_session->myscreen_->save_data.team_list[0] = std::move(team1);

    auto team3 = std::make_unique<guy>(FAMILY_ARCHER);
    team3->name = "TEAM3";
    team3->teamnum = 3;
    og::runtime::current_session->myscreen_->save_data.team_list[1] = std::move(team3);
    og::runtime::current_session->myscreen_->save_data.team_size = 2;

    TEST_ASSERT(og::runtime::current_session->myscreen_->save_data.save("test_level_team_mapping"),
        "save should succeed for team mapping regression");
    TEST_ASSERT(load_saved_game("test_level_team_mapping", og::runtime::current_session->myscreen_) != 0,
        "load_saved_game should succeed for team mapping regression");

    TEST_ASSERT(og::runtime::current_session->myscreen_->viewob[0] != nullptr, "view 0 should exist");
    TEST_ASSERT(og::runtime::current_session->myscreen_->viewob[1] != nullptr, "view 1 should exist");
    if (!og::runtime::current_session->myscreen_->viewob[0] || !og::runtime::current_session->myscreen_->viewob[1]) {
        og::runtime::current_session->myscreen_->level_data.delete_objects();
        return;
    }

    TEST_ASSERT_EQ(1, (int)og::runtime::current_session->myscreen_->viewob[0]->my_team,
        "view 0 should map to first distinct saved team id");
    TEST_ASSERT_EQ(3, (int)og::runtime::current_session->myscreen_->viewob[1]->my_team,
        "view 1 should map to second distinct saved team id");

    og::runtime::current_session->myscreen_->level_data.delete_objects();
}
REGISTER_TEST(test_load_saved_game_maps_views_to_saved_team_ids);
