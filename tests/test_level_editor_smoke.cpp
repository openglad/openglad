#include <openglad/interface/screen.h>
#include "test_framework.h"

// myscreen is now a macro defined in base.h (via game_session.h)

// Implemented in src/level_editor.cpp
Sint32 level_editor();

TEST(LevelEditorSmoke, end_flag_exits_quickly)
{
    // level_editor() has its own event loop, but it exits if myscreen->end is set.
    const char old_end = og::runtime::current_session->myscreen_->world().end;
    og::runtime::current_session->myscreen_->world().end = 1;

    Sint32 r = level_editor();
    (void)r;

    og::runtime::current_session->myscreen_->world().end = old_end;
}

