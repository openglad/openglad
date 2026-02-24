#pragma once

// LevelEditorState: per-session mutable state for the level editor (Phase 7b).
// Kept in a separate header to avoid pulling editor types into game_session.h.

#include "SDL.h"

struct LevelEditorState {
    unsigned char scenpalette[768]{};
    Sint32 redraw{1};
    Sint32 campaignchanged{0};
    Sint32 levelchanged{0};
    Sint32 cyclemode{1};
    Sint32 start_time_s{0};
    Sint32 rowsdown{0};
    Sint32 maxrows{0};

    // Mouse state
    int mouse_up_button{0};
    int mouse_motion_x{0}, mouse_motion_y{0};
    int mouse_last_x{0}, mouse_last_y{0};

    // Pan flags
    bool pan_left{false}, pan_right{false};
    bool pan_up{false}, pan_down{false};
};
