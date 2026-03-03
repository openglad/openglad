#pragma once
#include <cstdint>

// Menu loop return value flags (bitmask).
inline constexpr int32_t MENU_EXIT   = 1;
inline constexpr int32_t MENU_REDRAW = 2;
inline constexpr int32_t MENU_OK     = 4;

inline constexpr int32_t BUTTON_HEIGHT = 15;

// Forward declare button for extern arrays.
struct button;

// Button arrays defined in picker.cpp.
extern button mainmenu_buttons[];
extern button createmenu_buttons[];
extern button viewteam_buttons[];
extern button saveteam_buttons[];
extern button loadteam_buttons[];

// Per-session mutable button descriptors (Phase 12).
button* picker_main_options_buttons();
int picker_main_options_button_count();
button* picker_control_options_buttons();
int picker_control_options_button_count();
button* picker_trainmenu_buttons();
int picker_trainmenu_button_count();
button* picker_hiremenu_buttons();
int picker_hiremenu_button_count();
