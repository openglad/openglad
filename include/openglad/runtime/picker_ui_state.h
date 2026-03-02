#pragma once

// PickerState: per-session mutable state for the picker UI (Phase 7a).
// Kept in a separate header to avoid pulling picker/pixie types into game_session.h.

#include <array>
#include <memory>
#include "SDL.h"
#include <openglad/resources/pixie_data.h>

class pixieN;
class guy;
namespace og::ui {
struct PickerMenuItem;
class HireSession;
class TrainSession;
} // namespace og::ui

struct PickerState {
    // Loaded backdrop assets
    PixieData backpics[5]{};
    std::array<std::unique_ptr<pixieN>, 5> backdrops{};
    PixieData main_title_logo_data{};
    PixieData main_columns_data{};
    std::unique_ptr<pixieN> main_title_logo_pix;
    std::unique_ptr<pixieN> main_columns_pix;

    // Team build state
    guy* old_guy = nullptr;

    // Menu navigation
    bool menu_nav_enabled = false;
    Uint32 menu_nav_enabled_time = 0;

    // Intercept state (for test menu interception)
    int intercept_scope = 0;  // 0=None, 1=MainMenu, 2=TeamBuild
    const og::ui::PickerMenuItem* selected_menu_item = nullptr;

    // Team build sessions (non-owning pointers, valid only during hire/train flows)
    og::ui::HireSession* hire_session = nullptr;
    og::ui::TrainSession* train_session = nullptr;
};
