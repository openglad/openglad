#pragma once

// PickerState: per-session mutable state for the picker UI (Phase 7a).
// Kept in a separate header to avoid pulling picker/pixie types into game_session.h.

#include <array>
#include <cstdint>
#include <vector>
#include <openglad/interface/button.h>
#include <openglad/gameplay/pixie_data.h>

class pixieN;
class guy;
namespace og::ui {
struct PickerMenuItem;
class HireSession;
class TrainSession;
} // namespace og::ui

class PickerPixieHandle {
public:
    using DestroyFn = void (*)(pixieN*);

    PickerPixieHandle() = default;
    ~PickerPixieHandle() { reset(); }

    PickerPixieHandle(const PickerPixieHandle&) = delete;
    PickerPixieHandle& operator=(const PickerPixieHandle&) = delete;

    PickerPixieHandle(PickerPixieHandle&& other) noexcept
        : ptr_(other.ptr_)
        , destroy_(other.destroy_)
    {
        other.ptr_ = nullptr;
        other.destroy_ = nullptr;
    }

    PickerPixieHandle& operator=(PickerPixieHandle&& other) noexcept
    {
        if (this != &other) {
            reset();
            ptr_ = other.ptr_;
            destroy_ = other.destroy_;
            other.ptr_ = nullptr;
            other.destroy_ = nullptr;
        }
        return *this;
    }

    pixieN* get() const { return ptr_; }
    pixieN* operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

    void reset(pixieN* ptr = nullptr, DestroyFn destroy = nullptr)
    {
        if (ptr_ && destroy_) {
            destroy_(ptr_);
        }
        ptr_ = ptr;
        destroy_ = destroy;
    }

private:
    pixieN* ptr_ = nullptr;
    DestroyFn destroy_ = nullptr;
};

struct PickerState {
    // Zardus: PORT: put in a backpics var here so we can free the pixie files themselves
    PixieData backpics[5]{};
    std::array<PickerPixieHandle, 5> backdrops{};
    PixieData main_title_logo_data{};
    PixieData main_columns_data{};
    PickerPixieHandle main_title_logo_pix;
    PickerPixieHandle main_columns_pix;

    // Team build state
    guy* old_guy = nullptr;

    // Menu navigation
    bool menu_nav_enabled = false;
    std::uint32_t menu_nav_enabled_time = 0;

    // Intercept state (for test menu interception)
    int intercept_scope = 0;  // 0=None, 1=MainMenu, 2=TeamBuild
    const og::ui::PickerMenuItem* selected_menu_item = nullptr;

    // Team build sessions (non-owning pointers, valid only during hire/train flows)
    og::ui::HireSession* hire_session = nullptr;
    og::ui::TrainSession* train_session = nullptr;

    // Mutable menu descriptor arrays (Phase 12).
    std::vector<button> mainmenu_buttons;
    std::vector<button> seat_settings_buttons;
    std::vector<button> createmenu_buttons;
    std::vector<button> main_options_buttons;
    std::vector<button> display_settings_buttons;
    std::vector<button> gameplay_fx_options_buttons;
    std::vector<button> ui_fx_options_buttons;
    std::vector<button> graphics_fx_options_buttons;
    std::vector<button> details_buttons;
    std::vector<button> trainmenu_buttons;
    std::vector<button> hiremenu_buttons;
    std::vector<button> networking_buttons;
    std::vector<button> viewscenario_buttons;
    std::vector<button> progressmenu_buttons;
    std::vector<button> scenariomenu_buttons;
    std::vector<button> difficulty_menu_buttons;
    // §2.2 new-company name entry (Layer F engine screen).
    std::vector<button> name_entry_buttons;
    // §2.3 Company List (Load) — Layer F engine screen.
    std::vector<button> company_list_buttons;
    // §2.4 Backups sub-view (per company) — Layer F engine screen.
    std::vector<button> company_backups_buttons;
    // #155 CLOUD SAVE subscreen — engine screen.
    std::vector<button> cloud_save_buttons;
    // #168 full-screen HELP — engine screen.
    std::vector<button> help_buttons;
    // Campaign zone submenu (the scripted page chassis) — engine screen.
    std::vector<button> zone_submenu_buttons;
    std::vector<button> lineup_buttons;

    // VIEW LEVEL: page step requested by the PREV/NEXT ButtonAction handler
    // (-1/+1), consumed by the engine screen's consume_click hook
    // (picker_view_scenario_engine_consume_click).
    int view_scenario_page_step = 0;

    // NETWORKING subscreen: ACTIVE GAMES row clicked by the last
    // JoinRelayRoomListEntry dispatch (-1 = none). Set in vbutton::do_call,
    // consumed by configure_networking's menu loop.
    int networking_clicked_room_slot = -1;

    // Engine screens: spec row activated by the last ButtonAction::MenuSpecRow
    // dispatch (-1 = none). Set in vbutton::do_call; run_menu_screen MUST
    // consume it every frame (TESTING-enforced retvalue-zero discipline).
    int menu_spec_clicked_row = -1;

    // UI-canvas pointer position at the last button activation, stamped by
    // EVERY do_call dispatch site: the mouse branch of vbutton::leftclick
    // writes the live pointer, while the hotkey branch and the keyboard-FIRE
    // dispatch in handle_menu_nav write -1/-1. A spec-row handler can
    // therefore route on WHERE its rect was clicked without ever misreading
    // a stale pointer on a coordinate-free activation (#202: the seat-card
    // team chip).
    int menu_click_x = -1;
    int menu_click_y = -1;
};

// Load the four quadrant pixies of the picker's tiled title backdrop into the
// session's PickerState. picker_main runs this once when it builds the shared
// menu state; anything that composes a picker screen WITHOUT going through
// picker_main — the UX-shot probe's direct create_team_menu fixtures — must
// run it too, or its capture shows the screen's chrome floating on black
// instead of the frame a player sees.
void picker_load_menu_backdrops();
