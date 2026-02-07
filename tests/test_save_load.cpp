#include "graph.h"
#include "test_trace.h"
#include "test_framework.h"
#include "save_data.h"

extern screen* myscreen;

void test_save_load_roundtrip() {
    // Set up known values
    myscreen->save_data.scen_num = 3;
    myscreen->save_data.totalcash = 12345;
    myscreen->save_data.totalscore = 67890;
    myscreen->save_data.numplayers = 1;

    // Save
    trace_clear();
    bool saved = myscreen->save_data.save("test_save");
    TEST_ASSERT(saved, "save should succeed");
    TEST_ASSERT(trace_contains("save", "SaveData::save"), "save trace should be logged");
    TEST_ASSERT(trace_contains("save", "SaveData::save complete"), "save complete trace should be logged");

    // Modify values to prove load restores them
    myscreen->save_data.scen_num = 999;
    myscreen->save_data.totalcash = 0;
    myscreen->save_data.totalscore = 0;

    // Load
    trace_clear();
    bool loaded = myscreen->save_data.load("test_save");
    TEST_ASSERT(loaded, "load should succeed");
    TEST_ASSERT(trace_contains("load", "SaveData::load"), "load trace should be logged");
    TEST_ASSERT(trace_contains("load", "SaveData::load complete"), "load complete trace should be logged");

    // Verify restored values
    TEST_ASSERT_EQ(3, myscreen->save_data.scen_num, "scen_num should be restored");
    TEST_ASSERT_EQ(12345, (int)myscreen->save_data.totalcash, "totalcash should be restored");
    TEST_ASSERT_EQ(67890, (int)myscreen->save_data.totalscore, "totalscore should be restored");
}
REGISTER_TEST(test_save_load_roundtrip);
