#pragma once
#include <cstdint>

// Menu loop return value flags (bitmask).
inline constexpr int32_t MENU_EXIT   = 1;
inline constexpr int32_t MENU_REDRAW = 2;
inline constexpr int32_t MENU_OK     = 4;

inline constexpr int32_t BUTTON_HEIGHT = 15;

// Networking menu panel frame (outer border corners) and its inset background.
// draw_button() takes corners (x1,y1,x2,y2); draw_button_inverted() takes
// (x,y,WIDTH,HEIGHT) — the background is derived from these so the two can never
// disagree (the old code passed the corners as a width/height, drawing a panel
// far larger than the frame and off-screen).
inline constexpr int32_t PICKER_NETWORKING_FRAME_X1 = 26;
inline constexpr int32_t PICKER_NETWORKING_FRAME_Y1 = 34;
inline constexpr int32_t PICKER_NETWORKING_FRAME_X2 = 294;
inline constexpr int32_t PICKER_NETWORKING_FRAME_Y2 = 188;
inline constexpr int32_t PICKER_NETWORKING_FRAME_BORDER = 6;

inline constexpr int32_t PICKER_NETWORKING_FIELD_X = 128;
inline constexpr int32_t PICKER_NETWORKING_FIELD_WIDTH = 132;
inline constexpr int32_t PICKER_NETWORKING_FIELD_Y = 58;
inline constexpr int32_t PICKER_NETWORKING_FIELD_PITCH = 22;
inline constexpr int32_t PICKER_NETWORKING_ACTION_Y = 166;
inline constexpr int32_t PICKER_NETWORKING_ACTION_WIDTH = 74;
inline constexpr int32_t PICKER_NETWORKING_LABEL_GAP = 8;
inline constexpr int32_t PICKER_NETWORKING_INSTRUCTION_GAP = 3;

// Forward declare button for menu descriptor arrays.
struct button;

// Per-session mutable button descriptors (Phase 12).
button* picker_mainmenu_buttons();
int picker_mainmenu_button_count();
button* picker_createmenu_buttons();
int picker_createmenu_button_count();
button* picker_viewteam_buttons();
int picker_viewteam_button_count();
button* picker_saveteam_buttons();
int picker_saveteam_button_count();
button* picker_loadteam_buttons();
int picker_loadteam_button_count();

button* picker_main_options_buttons();
int picker_main_options_button_count();
button* picker_control_options_buttons();
int picker_control_options_button_count();
button* picker_trainmenu_buttons();
int picker_trainmenu_button_count();
button* picker_hiremenu_buttons();
int picker_hiremenu_button_count();
button* picker_networking_buttons();
int picker_networking_button_count();
