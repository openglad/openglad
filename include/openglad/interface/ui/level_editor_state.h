#pragma once

// LevelEditorState: per-session mutable state for the level editor (Phase 7b).
// Kept in a separate header to avoid pulling editor types into game_session.h.

#include <vector>
#include <cstdint>
#include <openglad/core/constants.h>

struct LevelEditorObjectType {
    Order order{Order::Living};
    unsigned char family{0};
};

struct LevelEditorState {
    unsigned char scenpalette[768]{};
    std::int32_t redraw{1};
    std::int32_t campaignchanged{0};
    std::int32_t levelchanged{0};
    std::int32_t cyclemode{1};
    std::int32_t start_time_s{0};
    std::int32_t rowsdown{0};
    std::int32_t maxrows{0};
    // Z-axis: which stacked floor the editor is currently painting/placing on.
    std::int32_t current_floor{0};

    // Mouse state
    int mouse_up_button{0};
    int mouse_motion_x{0}, mouse_motion_y{0};
    int mouse_last_x{0}, mouse_last_y{0};

    // Pan flags
    bool pan_left{false}, pan_right{false};
    bool pan_up{false}, pan_down{false};

    // Terrain/object picker state (Phase 12).
    std::vector<std::int32_t> backgrounds{};
    std::vector<LevelEditorObjectType> object_pane{};
};
