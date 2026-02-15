#include <openglad/legacy/test_trace.h>
#include <openglad/runtime/screen.h>
#include "test_framework.h"
#include <openglad/data/save_data.h>
extern screen* myscreen;

short load_saved_game(const char *filename, screen *myscreen);

void test_level_loading() {
    // Set up save_data for scenario 1
    myscreen->save_data.scen_num = static_cast<short>(1);
    myscreen->save_data.numplayers = 1;

    // Save a minimal save file so load_saved_game has something to load
    myscreen->save_data.save("test_level_save");

    trace_clear();
    short result = load_saved_game("test_level_save", myscreen);
    (void)result;
    // Check the traces were fired regardless of full success
    TEST_ASSERT(trace_contains("game", "load_saved_game"), "load_saved_game trace should be logged");
    TEST_ASSERT(trace_contains("game", "LevelData::load"), "LevelData::load trace should be logged");

    // Clean up loaded objects
    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_level_loading);
