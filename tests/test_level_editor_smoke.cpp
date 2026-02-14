#include <openglad/legacy/graph.h>
#include "test_framework.h"

extern screen* myscreen;

// Implemented in src/level_editor.cpp
Sint32 level_editor();

void test_level_editor_smoke_end_flag_exits_quickly()
{
    // level_editor() has its own event loop, but it exits if myscreen->end is set.
    const char old_end = myscreen->end;
    myscreen->end = 1;

    Sint32 r = level_editor();
    (void)r;

    myscreen->end = old_end;
}
REGISTER_TEST(test_level_editor_smoke_end_flag_exits_quickly);

