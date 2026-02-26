#pragma once
#include <cstdint>

// Menu loop return value flags (bitmask).
inline constexpr int32_t MENU_EXIT   = 1;
inline constexpr int32_t MENU_REDRAW = 2;
inline constexpr int32_t MENU_OK     = 4;

inline constexpr int32_t BUTTON_HEIGHT = 15;

// Forward declare button for extern arrays.
struct button;

// Session-local button layout accessors defined in picker.cpp.
button* get_mainmenu_buttons();
button* get_createmenu_buttons();
button* get_viewteam_buttons();
button* get_details_buttons();
button* get_trainmenu_buttons();
button* get_hiremenu_buttons();
button* get_saveteam_buttons();
button* get_loadteam_buttons();
button* get_main_options_buttons();
button* get_control_options_buttons();
