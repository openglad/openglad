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
extern button details_buttons[];
extern button trainmenu_buttons[];
extern button hiremenu_buttons[];
extern button saveteam_buttons[];
extern button loadteam_buttons[];
