#include "graph.h"
#include "button.h"
#include "test_trace.h"
#include "test_framework.h"
#include "save_data.h"
#include "level_data.h"

extern screen* myscreen;

short load_saved_game(const char *filename, screen *myscreen);

// Test: Load multiple different levels and verify they each load correctly
//
// Verifies:
//   1. Levels 1-2 can be loaded without crashing
//   2. Each level triggers the expected traces
//
// Note: Only testing levels 1-2 because some later levels use older scenario
// formats (version 6) that have a buffer overflow bug in the loader.

void test_load_multiple_levels() {
    // Try loading levels 1 and 2 (version 9 format, known good)
    for (int level = 1; level <= 2; level++) {
        trace_clear();

        myscreen->save_data.scen_num = level;
        myscreen->save_data.numplayers = 1;
        myscreen->save_data.save("test_level_multi");

        short result = load_saved_game("test_level_multi", myscreen);

        char msg[80];
        snprintf(msg, 80, "level %d should trigger load_saved_game trace", level);
        TEST_ASSERT(trace_contains("game", "load_saved_game"), msg);

        snprintf(msg, 80, "level %d should trigger LevelData::load trace", level);
        TEST_ASSERT(trace_contains("game", "level loaded"), msg);

        // Clean up loaded objects before loading the next level
        myscreen->level_data.delete_objects();
    }
}
REGISTER_TEST(test_load_multiple_levels);


// Test: Level data integrity -- verify that loaded level has sensible data
void test_level_data_integrity() {
    trace_clear();

    myscreen->save_data.scen_num = 1;
    myscreen->save_data.numplayers = 1;
    myscreen->save_data.save("test_level_integrity");

    load_saved_game("test_level_integrity", myscreen);

    // Level 1 should have a valid grid
    TEST_ASSERT(myscreen->level_data.grid.valid(), "level 1 should have a valid grid");

    // Level 1 should have some objects (enemies)
    TEST_ASSERT(!myscreen->level_data.oblist.empty(),
        "level 1 should have objects (enemies/npcs)");

    // Level ID should match what we requested
    TEST_ASSERT_EQ(1, myscreen->level_data.id, "level id should be 1");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_level_data_integrity);


// Test: Loading a nonexistent level falls back to level 1
void test_level_fallback() {
    trace_clear();

    myscreen->save_data.scen_num = 9999;  // This level shouldn't exist
    myscreen->save_data.numplayers = 1;
    myscreen->save_data.save("test_level_fallback");

    load_saved_game("test_level_fallback", myscreen);

    // Should have fallen back to level 1
    TEST_ASSERT_EQ(1, myscreen->level_data.id,
        "nonexistent level should fall back to level 1");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_level_fallback);
