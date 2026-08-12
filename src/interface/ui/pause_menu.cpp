/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
/* The in-game PAUSED menu + per-player screen (docs/pause-menu-design.md
 * §2.1/§2.2). Two MenuScreenSpecs run through run_menu_screen, hosted from
 * the game loop's Esc branch over a darkened world snapshot. Deliberately
 * absent from the MenuScreenId registry (the registry sweep runs screens in
 * picker context); the accessor storage is file-local for the same reason.
 */

#include <openglad/interface/ui/pause_menu.h>

#include <openglad/core/test_trace.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/input_mappings.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/input_cycler.h>
#include <openglad/resources/gparser.h>

#include "picker_sdl_defs.h"

#include <array>
#include <format>
#include <string>
#include <vector>

// Shared picker loop helpers and dialogs (defined in picker_input.cpp /
// button.cpp / picker_dialogs.cpp; declared locally by every consumer — repo
// pattern).
void sync_button_hidden_state(const button* buttons, int button_index);
void ensure_highlighted_button_visible(const button* buttons, int num_buttons,
                                       int& highlighted_button);
bool yes_or_no_prompt(const char* title, const char* message,
                      bool default_value);
bool no_or_yes_prompt(const char* title, const char* message,
                      bool default_value);
void popup_dialog(const char* title, const char* message);

namespace og::ui {

namespace {

PauseMenuScreenState* g_pause_state = nullptr;
PausePlayerScreenState* g_pause_player_state = nullptr;

// Keep-alive cadence: comfortably inside the server's 60s auto-resume.
constexpr std::uint32_t kPauseKeepaliveIntervalMs = 20'000;
// Retry cadence when the baseline unpaused underneath the open menu.
constexpr std::uint32_t kPauseRepauseRetryMs = 1'000;

#ifdef TESTING
struct QueuedPauseOutcome {
    PauseMenuResult outcome = PauseMenuResult::Resumed;
    bool release_pause = true;
};
std::vector<QueuedPauseOutcome> s_outcome_queue;
bool s_force_real_menu = false;
std::uint32_t s_keepalive_interval_override_ms = 0;
#endif

std::uint32_t pause_keepalive_interval_ms()
{
#ifdef TESTING
    if (s_keepalive_interval_override_ms != 0)
        return s_keepalive_interval_override_ms;
#endif
    return kPauseKeepaliveIntervalMs;
}

// One request per cadence window: the ~20s keep-alive while paused (the
// server-side same-owner refresh defeats the auto-resume), a 1s re-request
// when the baseline unpaused underneath the open menu.
void maintain_pause_keepalive(const PauseMenuHost& host,
                              std::uint32_t& last_request_ms)
{
    if (host.request_pause == nullptr)
        return;
    const std::uint32_t now = og::input_native::ticks_ms();
    const bool unpaused_underneath =
        host.is_paused != nullptr && !host.is_paused();
    const std::uint32_t interval = unpaused_underneath
        ? kPauseRepauseRetryMs
        : pause_keepalive_interval_ms();
    if (now - last_request_ms >= interval)
    {
        host.request_pause();
        last_request_ms = now;
    }
}

// Dynamic labels have TWO surfaces: the descriptor row (backs redraws and
// resets) and the live vbutton (what draw_buttons shows).
void sync_pause_label(button* buttons, int index, const std::string& label)
{
    buttons[index].label = label;
    if (index >= 0 &&
        index < static_cast<int>(
            og::runtime::current_session->allbuttons_.size()))
    {
        if (vbutton* const live =
                og::runtime::current_session
                    ->allbuttons_[static_cast<std::size_t>(index)])
        {
            live->label = label;
        }
    }
}

bool pause_state_networked()
{
    return g_pause_state != nullptr && g_pause_state->host != nullptr &&
        g_pause_state->host->networked;
}

// --------------------------------------------------------------------------
// PAUSED screen (design §2.1): a centered 140px column over the darkened
// world. Row geometry: x=90 w=140 h=15, 18px pitch; the PLAYERS divider and
// the title are content text, not buttons.

constexpr Sint32 kPauseColumnX = 90;
constexpr Sint32 kPauseColumnW = 140;
constexpr Sint32 kPauseRowH = 15;

RowState pause_restart_row_state(const MenuLabelContext& /*context*/)
{
    if (g_pause_state == nullptr || g_pause_state->host == nullptr)
        return RowState::Visible;  // bare engine/pin shape
    return g_pause_state->host->restart_allowed ? RowState::Visible
                                                : RowState::Hidden;
}

RowState pause_add_player_row_state(const MenuLabelContext& /*context*/)
{
    if (g_pause_state == nullptr || g_pause_state->host == nullptr)
        return RowState::Visible;
    const PauseMenuHost& host = *g_pause_state->host;
    if (host.networked)
        return RowState::Hidden;
    if (host.can_add_player != nullptr && !host.can_add_player())
        return RowState::Disabled;  // 4 seats: visible for nav, inert
    return RowState::Visible;
}

template <int K>
RowState pause_player_row_state(const MenuLabelContext& /*context*/)
{
    if (g_pause_state == nullptr)
        return K == 0 ? RowState::Visible : RowState::Hidden;
    const int seat_count =
        static_cast<int>(collect_pause_seats(pause_state_networked()).size());
    return K < seat_count ? RowState::Visible : RowState::Hidden;
}

constexpr MenuButtonSpec kPauseMenuRows[] = {
    {.id = "pause_resume", .label = "RESUME",
     .x = kPauseColumnX, .y = 32, .w = kPauseColumnW, .h = kPauseRowH,
     .action = ButtonAction::MenuSpecRow, .arg = kPauseMenuResumeIndex,
     .nav = {.up = kPauseMenuAddPlayerIndex, .down = kPauseMenuRestartIndex}},
    {.id = "pause_restart", .label = "RESTART MISSION",
     .x = kPauseColumnX, .y = 50, .w = kPauseColumnW, .h = kPauseRowH,
     .action = ButtonAction::MenuSpecRow, .arg = kPauseMenuRestartIndex,
     .nav = {.up = kPauseMenuResumeIndex, .down = kPauseMenuQuitIndex},
     .state_override = &pause_restart_row_state},
    {.id = "pause_quit", .label = "QUIT MISSION",
     .x = kPauseColumnX, .y = 68, .w = kPauseColumnW, .h = kPauseRowH,
     .action = ButtonAction::MenuSpecRow, .arg = kPauseMenuQuitIndex,
     .nav = {.up = kPauseMenuRestartIndex,
             .down = kPauseMenuPlayerBaseIndex}},
    {.id = "pause_player_0", .label = "P1",
     .x = kPauseColumnX, .y = 100, .w = kPauseColumnW, .h = kPauseRowH,
     .action = ButtonAction::MenuSpecRow, .arg = kPauseMenuPlayerBaseIndex,
     .nav = {.up = kPauseMenuQuitIndex,
             .down = kPauseMenuPlayerBaseIndex + 1},
     .state_override = &pause_player_row_state<0>},
    {.id = "pause_player_1", .label = "P2",
     .x = kPauseColumnX, .y = 118, .w = kPauseColumnW, .h = kPauseRowH,
     .action = ButtonAction::MenuSpecRow,
     .arg = kPauseMenuPlayerBaseIndex + 1,
     .nav = {.up = kPauseMenuPlayerBaseIndex,
             .down = kPauseMenuPlayerBaseIndex + 2},
     .state_override = &pause_player_row_state<1>, .hidden = true},
    {.id = "pause_player_2", .label = "P3",
     .x = kPauseColumnX, .y = 136, .w = kPauseColumnW, .h = kPauseRowH,
     .action = ButtonAction::MenuSpecRow,
     .arg = kPauseMenuPlayerBaseIndex + 2,
     .nav = {.up = kPauseMenuPlayerBaseIndex + 1,
             .down = kPauseMenuPlayerBaseIndex + 3},
     .state_override = &pause_player_row_state<2>, .hidden = true},
    {.id = "pause_player_3", .label = "P4",
     .x = kPauseColumnX, .y = 154, .w = kPauseColumnW, .h = kPauseRowH,
     .action = ButtonAction::MenuSpecRow,
     .arg = kPauseMenuPlayerBaseIndex + 3,
     .nav = {.up = kPauseMenuPlayerBaseIndex + 2,
             .down = kPauseMenuAddPlayerIndex},
     .state_override = &pause_player_row_state<3>, .hidden = true},
    {.id = "pause_add_player", .label = "+ ADD PLAYER",
     .x = kPauseColumnX, .y = 172, .w = kPauseColumnW, .h = kPauseRowH,
     .action = ButtonAction::MenuSpecRow, .arg = kPauseMenuAddPlayerIndex,
     .nav = {.up = kPauseMenuPlayerBaseIndex + 3,
             .down = kPauseMenuResumeIndex},
     .state_override = &pause_add_player_row_state},
};
static_assert(static_cast<int>(std::size(kPauseMenuRows)) ==
              kPauseMenuButtonCount);

std::vector<button> g_pause_menu_buttons;
std::vector<button> g_pause_player_buttons;

button* pause_menu_buttons_accessor()
{
    materialize_menu_buttons(pause_menu_screen_spec(), g_pause_menu_buttons);
    return g_pause_menu_buttons.data();
}

int pause_menu_button_count_accessor()
{
    return static_cast<int>(g_pause_menu_buttons.size());
}

button* pause_player_buttons_accessor()
{
    materialize_menu_buttons(pause_player_menu_screen_spec(),
                             g_pause_player_buttons);
    return g_pause_player_buttons.data();
}

int pause_player_button_count_accessor()
{
    return static_cast<int>(g_pause_player_buttons.size());
}

// Per-frame labels + a vertical nav chain over the visible rows (visibility
// is current here — the gate pass runs first). Runs every frame: seats can
// change under the open screen (ADD PLAYER, REMOVE PLAYER).
void pause_menu_rewire(button* buttons, int count, int& highlighted_button)
{
    if (buttons == nullptr || count < kPauseMenuButtonCount)
        return;

    const std::vector<PauseSeatInfo> seats =
        collect_pause_seats(pause_state_networked());
    for (int k = 0; k < MAX_PLAYERS; ++k)
    {
        if (k < static_cast<int>(seats.size()))
        {
            sync_pause_label(buttons, kPauseMenuPlayerBaseIndex + k,
                             pause_player_row_label(
                                 seats[static_cast<std::size_t>(k)]));
        }
    }

    std::vector<int> visible;
    visible.reserve(static_cast<std::size_t>(kPauseMenuButtonCount));
    for (int i = 0; i < kPauseMenuButtonCount; ++i)
    {
        if (!buttons[i].hidden)
            visible.push_back(i);
    }
    for (std::size_t order = 0; order < visible.size(); ++order)
    {
        const int index = visible[order];
        const int up = visible[(order + visible.size() - 1) % visible.size()];
        const int down = visible[(order + 1) % visible.size()];
        buttons[index].nav = {.up = up, .down = down};
    }

    ensure_highlighted_button_visible(buttons, count, highlighted_button);
}

void pause_menu_draw_background(void* screen_state)
{
    auto* const state = static_cast<PauseMenuScreenState*>(screen_state);
    screen* const scr = og::runtime::current_session->myscreen_;
    if (scr == nullptr)
        return;

    if (state != nullptr && state->background_dirty)
    {
        // ONE darken per seed (never per frame — it accumulates): re-seed the
        // fixed UI canvas from the world frame, then dim it. Set again after
        // any nested dialog/screen scribbled on the canvas.
        scr->prepare_ui_canvas_from_world();
        scr->darken_screen();
        state->background_dirty = false;
    }

    // Per-frame halo pads behind the title/divider bands and each visible
    // button: the pulsing keyboard highlight paints up to 3px OUTSIDE a
    // button face, and without a repaint a moved highlight leaves its old
    // ring burned into the (otherwise static) darkened backdrop.
    scr->draw_box(60, 4, 260, 29, PURE_BLACK, 1, 1);
    scr->draw_box(kPauseColumnX, 86, kPauseColumnX + kPauseColumnW, 96,
                  PURE_BLACK, 1, 1);
    const button* const buttons = g_pause_menu_buttons.data();
    for (std::size_t i = 0; i < g_pause_menu_buttons.size(); ++i)
    {
        if (buttons[i].hidden)
            continue;
        scr->draw_box(buttons[i].x - 5, buttons[i].y - 5,
                      buttons[i].x + buttons[i].sizex + 5,
                      buttons[i].y + buttons[i].sizey + 5, PURE_BLACK, 1, 1);
    }
}

void pause_menu_draw_content(void* screen_state)
{
    auto* const state = static_cast<PauseMenuScreenState*>(screen_state);
    screen* const scr = og::runtime::current_session->myscreen_;
    if (scr == nullptr)
        return;

    scr->text_big.write_xy_center(160, 8, WHITE, "%s", "P A U S E D");
    const std::string owner =
        state != nullptr && state->host != nullptr &&
            state->host->remote_pause_owner != nullptr
        ? state->host->remote_pause_owner()
        : std::string();
    if (!owner.empty())
    {
        scr->text_normal.write_xy_center(160, 22, YELLOW, "%s",
                                         ("by " + owner).c_str());
    }
    if (!collect_pause_seats(pause_state_networked()).empty())
        scr->text_normal.write_xy_center(160, 89, WHITE, "%s", "- PLAYERS -");
}

bool pause_menu_frame_tick(void* screen_state, int /*frame*/)
{
    auto* const state = static_cast<PauseMenuScreenState*>(screen_state);
    if (state == nullptr || state->host == nullptr)
        return false;
    const PauseMenuHost& host = *state->host;

    if (host.pump_paused != nullptr && !host.pump_paused())
    {
        state->outcome = PauseMenuResult::SessionEnded;
        return false;
    }
    maintain_pause_keepalive(host, state->last_keepalive_ms);

    // Esc closes = resume. Keystate EDGE, not the raw key_press_event: the
    // press that opened the menu (or a held web Backspace autorepeating)
    // must not close the menu it just opened, so a release must be observed
    // first. keystates_ refresh through the loop's event polling; on web the
    // Escape slot mirrors physical Backspace (web_back_key.h).
    const bool esc_down =
        og::runtime::current_session->keystates_ != nullptr &&
        og::runtime::current_session->keystates_[KEYSTATE_ESCAPE];
    if (!esc_down)
    {
        state->esc_seen_up = true;
    }
    else if (state->esc_seen_up)
    {
        state->outcome = PauseMenuResult::Resumed;
        return false;
    }
    return true;
}

Sint32 run_pause_player_screen(const PauseSeatInfo& seat,
                               const PauseMenuHost* host,
                               bool& session_ended);

Sint32 pause_menu_on_spec_row(int row, void* screen_state)
{
    auto* const state = static_cast<PauseMenuScreenState*>(screen_state);
    if (state == nullptr)
        return MENU_OK;

    if (row == kPauseMenuResumeIndex)
    {
        state->outcome = PauseMenuResult::Resumed;
        return MENU_EXIT;
    }
    if (row == kPauseMenuRestartIndex)
    {
        const bool confirmed = yes_or_no_prompt(
            "Restart Mission", "Restart this mission?", false);
        state->background_dirty = true;
        if (!confirmed)
            return MENU_OK;
        TRACE("pause_menu", "restart_confirmed");
        state->outcome = PauseMenuResult::Restart;
        return MENU_EXIT;
    }
    if (row == kPauseMenuQuitIndex)
    {
        const bool confirmed =
            yes_or_no_prompt("Abort Mission", "Quit this mission?", false);
        state->background_dirty = true;
        if (!confirmed)
            return MENU_OK;
        TRACE("pause_menu", "quit_confirmed");
        state->outcome = PauseMenuResult::Quit;
        return MENU_EXIT;
    }
    if (row == kPauseMenuAddPlayerIndex)
    {
        if (state->host != nullptr && state->host->add_player != nullptr &&
            state->host->add_player())
        {
            TRACE("pause_menu", "add_player");
        }
        else
        {
            popup_dialog("PLAYERS", "COULD NOT ADD");
            state->background_dirty = true;
        }
        return MENU_OK;
    }

    const int seat_order = row - kPauseMenuPlayerBaseIndex;
    if (seat_order >= 0 && seat_order < MAX_PLAYERS)
    {
        const std::vector<PauseSeatInfo> seats =
            collect_pause_seats(pause_state_networked());
        if (seat_order < static_cast<int>(seats.size()))
        {
            bool session_ended = false;
            (void)run_pause_player_screen(
                seats[static_cast<std::size_t>(seat_order)], state->host,
                session_ended);
            state->background_dirty = true;
            if (session_ended)
            {
                state->outcome = PauseMenuResult::SessionEnded;
                return MENU_EXIT;
            }
        }
    }
    return MENU_OK;
}

// --------------------------------------------------------------------------
// Player screen (design §2.2): BACK / INPUT cycler / direction toggle /
// REMAP / RESET / REMOVE PLAYER over the live binding grid.

RowState pause_player_remove_row_state(const MenuLabelContext& /*context*/)
{
    if (g_pause_player_state == nullptr ||
        g_pause_player_state->host == nullptr)
    {
        return RowState::Visible;  // bare engine/pin shape
    }
    const PauseMenuHost& host = *g_pause_player_state->host;
    if (host.networked)
        return RowState::Hidden;
    if (host.can_remove_player != nullptr &&
        !host.can_remove_player(g_pause_player_state->seat))
    {
        return RowState::Hidden;  // last local seat
    }
    return RowState::Visible;
}

constexpr MenuButtonSpec kPausePlayerRows[] = {
    {.id = "pause_player_back", .label = "BACK",
     .hotkey = KEYSTATE_ESCAPE,
     .x = 10, .y = 8, .w = 50, .h = 15,
     .action = ButtonAction::ReturnMenu, .arg = MENU_REDRAW,
     .nav = {.up = kPausePlayerRemoveIndex,
             .down = kPausePlayerInputIndex}},
    {.id = "pause_input", .label = "INPUT: WASD",
     .x = 16, .y = 38, .w = 98, .h = 18,
     .action = ButtonAction::MenuSpecRow, .arg = kPausePlayerInputIndex,
     .nav = {.up = kPausePlayerBackIndex,
             .down = kPausePlayerRemoveIndex,
             .right = kPausePlayerModeIndex}},
    {.id = "pause_direction", .label = "4-DIRECTION",
     .x = 118, .y = 38, .w = 74, .h = 18,
     .action = ButtonAction::MenuSpecRow, .arg = kPausePlayerModeIndex,
     .nav = {.up = kPausePlayerBackIndex,
             .down = kPausePlayerRemoveIndex,
             .left = kPausePlayerInputIndex,
             .right = kPausePlayerRemapIndex}},
    {.id = "pause_remap", .label = "REMAP",
     .x = 196, .y = 38, .w = 56, .h = 18,
     .action = ButtonAction::MenuSpecRow, .arg = kPausePlayerRemapIndex,
     .nav = {.up = kPausePlayerBackIndex,
             .down = kPausePlayerRemoveIndex,
             .left = kPausePlayerModeIndex,
             .right = kPausePlayerResetIndex}},
    {.id = "pause_reset", .label = "RESET",
     .x = 256, .y = 38, .w = 56, .h = 18,
     .action = ButtonAction::MenuSpecRow, .arg = kPausePlayerResetIndex,
     .nav = {.up = kPausePlayerBackIndex,
             .down = kPausePlayerRemoveIndex,
             .left = kPausePlayerRemapIndex}},
    {.id = "pause_remove", .label = "REMOVE PLAYER",
     .x = 166, .y = 169, .w = 138, .h = 18,
     .action = ButtonAction::MenuSpecRow, .arg = kPausePlayerRemoveIndex,
     .nav = {.up = kPausePlayerInputIndex,
             .down = kPausePlayerBackIndex},
     .state_override = &pause_player_remove_row_state},
};
static_assert(static_cast<int>(std::size(kPausePlayerRows)) ==
              kPausePlayerButtonCount);

void pause_player_rewire(button* buttons, int count, int& highlighted_button)
{
    if (buttons == nullptr || count < kPausePlayerButtonCount ||
        g_pause_player_state == nullptr)
    {
        return;
    }
    PausePlayerScreenState* const state = g_pause_player_state;

    sync_pause_label(buttons, kPausePlayerInputIndex,
                     input_cycle_button_label(state->seat));
    const bool eight_dir =
        get_player_control_mode(state->seat) ==
        static_cast<int>(ControlDirectionMode::EightDirection);
    sync_pause_label(buttons, kPausePlayerModeIndex,
                     eight_dir ? "8-DIRECTION" : "4-DIRECTION");

    // The gate pass ran first, so REMOVE's hidden flag is current: route the
    // vertical chain around it when it is gone.
    const bool remove_visible = !buttons[kPausePlayerRemoveIndex].hidden;
    const int below_band = remove_visible ? kPausePlayerRemoveIndex
                                          : kPausePlayerBackIndex;
    for (const int index : {kPausePlayerInputIndex, kPausePlayerModeIndex,
                            kPausePlayerRemapIndex, kPausePlayerResetIndex})
    {
        buttons[index].nav.up = kPausePlayerBackIndex;
        buttons[index].nav.down = below_band;
    }
    buttons[kPausePlayerBackIndex].nav.down = kPausePlayerInputIndex;
    buttons[kPausePlayerBackIndex].nav.up =
        remove_visible ? kPausePlayerRemoveIndex : kPausePlayerInputIndex;

    ensure_highlighted_button_visible(buttons, count, highlighted_button);
}

void pause_player_draw_background(void* /*screen_state*/)
{
    // Opaque full-screen repaint every frame (the options-panel shape): the
    // binding grid re-derives per frame, and the remap wizard scribbles
    // full-screen prompts this pass must heal.
    screen* const scr = og::runtime::current_session->myscreen_;
    if (scr == nullptr)
        return;
    scr->clear_window();
    scr->draw_button(0, 0, 320, 200, 0);
    scr->draw_button_inverted(4, 4, 312, 192);
}

void pause_player_draw_content(void* screen_state)
{
    auto* const state = static_cast<PausePlayerScreenState*>(screen_state);
    if (state == nullptr)
        return;
    screen* const scr = og::runtime::current_session->myscreen_;
    if (scr == nullptr)
        return;

    text& mytext = scr->text_normal;
    mytext.write_xy_center(
        160, 13, DARK_BLUE, "%s",
        std::format("LOCAL PLAYER {} / P{}", state->seat + 1,
                    state->player_number)
            .c_str());

    scr->draw_button(12, 62, 308, 159, 2, 1);
    mytext.write_xy(20, 66, DARK_BLUE, "%s", "MOVEMENT");
    mytext.write_xy(164, 66, DARK_BLUE, "%s", "ACTIONS");

    struct BindingLine {
        const char* label;
        int key;
        bool diagonal;
    };
    static constexpr std::array<BindingLine, 8> movement{{
        {"UP", KEY_UP, false},
        {"UP-RIGHT", KEY_UP_RIGHT, true},
        {"RIGHT", KEY_RIGHT, false},
        {"DOWN-RIGHT", KEY_DOWN_RIGHT, true},
        {"DOWN", KEY_DOWN, false},
        {"DOWN-LEFT", KEY_DOWN_LEFT, true},
        {"LEFT", KEY_LEFT, false},
        {"UP-LEFT", KEY_UP_LEFT, true},
    }};
    static constexpr std::array<BindingLine, 7> actions{{
        {"FIRE", KEY_FIRE, false},
        {"SPECIAL", KEY_SPECIAL, false},
        {"YELL", KEY_YELL, false},
        {"SHIFTER", KEY_SHIFTER, false},
        {"LOOK UP", KEY_LOOKUP, false},
        {"CHAR SW", KEY_SWITCH, false},
        {"SPEC SW", KEY_SPECIAL_SWITCH, false},
    }};
    const bool eight_dir =
        get_player_control_mode(state->seat) ==
        static_cast<int>(ControlDirectionMode::EightDirection);
    // Joystick seats show the device bindings (B0/A0+/HU style) — the
    // keyboard names would be misleading while the assigned device drives.
    const bool joystick_seat = playerHasJoystick(state->seat);
    const auto binding_value = [state, joystick_seat](int key) {
        if (joystick_seat)
        {
            return joy_binding_display_name(
                player_joy[state->seat].key_type[key],
                player_joy[state->seat].key_index[key]);
        }
        return player_control_key_display_name(state->seat, key);
    };

    for (std::size_t index = 0; index < movement.size(); ++index)
    {
        const BindingLine& binding = movement[index];
        const bool active = !binding.diagonal || eight_dir;
        const std::string value =
            active ? binding_value(binding.key) : std::string("--");
        const std::string line =
            std::format("{}: {}", binding.label, value);
        mytext.write_xy(20, 77 + static_cast<int>(index) * 10,
                        active ? DARK_BLUE : GREY, "%s", line.c_str());
    }
    for (std::size_t index = 0; index < actions.size(); ++index)
    {
        const BindingLine& binding = actions[index];
        const std::string line = std::format(
            "{}: {}", binding.label, binding_value(binding.key));
        mytext.write_xy(164, 77 + static_cast<int>(index) * 10,
                        DARK_BLUE, "%s", line.c_str());
    }
}

bool pause_player_frame_tick(void* screen_state, int /*frame*/)
{
    auto* const state = static_cast<PausePlayerScreenState*>(screen_state);
    if (state == nullptr || state->removed)
        return false;
    if (state->host != nullptr)
    {
        if (state->host->pump_paused != nullptr &&
            !state->host->pump_paused())
        {
            state->session_ended = true;
            return false;
        }
        maintain_pause_keepalive(*state->host, state->last_keepalive_ms);
    }
    const int seat_count = static_cast<int>(
        collect_pause_seats(state->host != nullptr && state->host->networked)
            .size());
    return state->seat < seat_count;
}

void persist_pause_player_controls()
{
    save_player_control_settings_to_cfg(cfg);
    cfg.save_settings();
}

// KeyWaitPollCallback for the remap wizard: pump the paused transport
// between key waits; false cancels the remaining prompts when the session
// died underneath the menu.
bool pause_remap_poll()
{
    PausePlayerScreenState* const state = g_pause_player_state;
    if (state == nullptr || state->host == nullptr)
        return true;
    if (state->host->pump_paused != nullptr && !state->host->pump_paused())
    {
        state->session_ended = true;
        return false;
    }
    maintain_pause_keepalive(*state->host, state->last_keepalive_ms);
    return true;
}

Sint32 pause_player_on_spec_row(int row, void* screen_state)
{
    auto* const state = static_cast<PausePlayerScreenState*>(screen_state);
    if (state == nullptr)
        return MENU_OK;
    const bool networked = state->host != nullptr && state->host->networked;

    if (row == kPausePlayerInputIndex)
    {
        const int active_count =
            static_cast<int>(collect_pause_seats(networked).size());
        (void)cycle_player_input(cfg, state->seat, active_count);
        persist_pause_player_controls();
        return MENU_OK;
    }
    if (row == kPausePlayerModeIndex)
    {
        (void)toggle_player_control_mode(state->seat);
        persist_pause_player_controls();
        return MENU_OK;
    }
    if (row == kPausePlayerRemapIndex)
    {
        (void)edit_player_keymap_with_poll(state->seat, &pause_remap_poll);
        // Write the customization through to the mapping library (design
        // §3.2); false = the seat is factory-identical, which is success.
        (void)og::input::save_player_mapping_to_library(cfg, state->seat);
        persist_pause_player_controls();
        return MENU_OK;
    }
    if (row == kPausePlayerResetIndex)
    {
        if (reset_default_player_controls_for_player(state->seat))
            persist_pause_player_controls();
        return MENU_OK;
    }
    if (row == kPausePlayerRemoveIndex)
    {
        if (networked)
            return MENU_OK;
        if (!no_or_yes_prompt("REMOVE PLAYER?", "REMOVE THIS PLAYER SEAT?",
                              false))
        {
            return MENU_OK;
        }
        if (state->host == nullptr || state->host->remove_player == nullptr ||
            !state->host->remove_player(state->seat))
        {
            popup_dialog("PLAYERS", "REMOVE DENIED");
            return MENU_OK;
        }
        // The shadow's removal already compacted the controller profiles;
        // persist the rotated pool.
        persist_pause_player_controls();
        state->removed = true;
        TRACE("pause_menu", "remove_player seat=%d", state->seat);
        return MENU_REDRAW;  // exit_on_redraw pops back to the PAUSED screen
    }
    return MENU_OK;
}

constexpr MenuScreenSpec make_pause_menu_spec()
{
    MenuScreenSpec spec{};
    spec.name = "pause_menu";
    spec.rows = kPauseMenuRows;
    spec.row_count = kPauseMenuButtonCount;
    spec.buttons_accessor = &pause_menu_buttons_accessor;
    spec.count_accessor = &pause_menu_button_count_accessor;
    spec.nav = {.kind = NavProgramKind::Rewire, .rewire = &pause_menu_rewire};
    spec.remote_start = RemoteStartScope::None;
    spec.default_highlight = kPauseMenuResumeIndex;
    spec.enter = EnterTransition::None;
    spec.backdrop = false;
    spec.polls_lobby = false;
    spec.draw_background = &pause_menu_draw_background;
    spec.draw_content = &pause_menu_draw_content;
    spec.frame_tick = &pause_menu_frame_tick;
    spec.on_spec_row = &pause_menu_on_spec_row;
    spec.exit_value = MENU_REDRAW;
    return spec;
}

constexpr MenuScreenSpec make_pause_player_spec()
{
    MenuScreenSpec spec{};
    spec.name = "pause_player";
    spec.rows = kPausePlayerRows;
    spec.row_count = kPausePlayerButtonCount;
    spec.buttons_accessor = &pause_player_buttons_accessor;
    spec.count_accessor = &pause_player_button_count_accessor;
    spec.nav = {.kind = NavProgramKind::Rewire,
                .rewire = &pause_player_rewire};
    spec.remote_start = RemoteStartScope::None;
    spec.default_highlight = kPausePlayerInputIndex;
    spec.enter = EnterTransition::None;
    spec.exit_on_redraw = true;
    spec.backdrop = false;
    spec.polls_lobby = false;
    spec.draw_background = &pause_player_draw_background;
    spec.draw_content = &pause_player_draw_content;
    spec.frame_tick = &pause_player_frame_tick;
    spec.on_spec_row = &pause_player_on_spec_row;
    spec.exit_value = MENU_REDRAW;
    return spec;
}

constexpr MenuScreenSpec kPauseMenuSpec = make_pause_menu_spec();
constexpr MenuScreenSpec kPausePlayerSpec = make_pause_player_spec();

Sint32 run_pause_player_screen(const PauseSeatInfo& seat,
                               const PauseMenuHost* host,
                               bool& session_ended)
{
    PausePlayerScreenState state;
    state.host = host;
    state.seat = seat.seat;
    state.player_number = seat.player_number;
    state.last_keepalive_ms = og::input_native::ticks_ms();
    PausePlayerScreenState* const previous = g_pause_player_state;
    g_pause_player_state = &state;
    const Sint32 result = run_menu_screen(kPausePlayerSpec, &state);
    g_pause_player_state = previous;
    session_ended = state.session_ended;
    return result;
}

} // namespace

const MenuScreenSpec& pause_menu_screen_spec()
{
    return kPauseMenuSpec;
}

const MenuScreenSpec& pause_player_menu_screen_spec()
{
    return kPausePlayerSpec;
}

std::vector<PauseSeatInfo> collect_pause_seats(bool networked)
{
    std::vector<PauseSeatInfo> seats;
    if (og::runtime::current_session == nullptr)
        return seats;

    if (networked)
    {
        // A networked machine's local seats are the session's own player
        // indices (GLOBAL ids), in view/profile-slot order.
        const auto& own = og::runtime::current_session->own_player_indices_;
        for (std::size_t order = 0;
             order < own.size() &&
             order < static_cast<std::size_t>(MAX_PLAYERS);
             ++order)
        {
            seats.push_back(PauseSeatInfo{
                .seat = static_cast<int>(order),
                .player_number = static_cast<int>(own[order]) + 1,
            });
        }
        return seats;
    }

    screen* const scr = og::runtime::current_session->myscreen_;
    if (scr == nullptr)
        return seats;
    const int count = std::min<int>(scr->save_data.numplayers, MAX_PLAYERS);
    for (int index = 0; index < count; ++index)
    {
        seats.push_back(PauseSeatInfo{.seat = index,
                                      .player_number = index + 1});
    }
    return seats;
}

std::string pause_player_row_label(const PauseSeatInfo& seat)
{
    return std::format("P{}: {}", seat.player_number,
                       current_input_selection(seat.seat).name);
}

void install_pause_menu_state_for_screen(PauseMenuScreenState* state)
{
    g_pause_state = state;
}

void install_pause_player_state_for_screen(PausePlayerScreenState* state)
{
    g_pause_player_state = state;
}

PauseMenuResult run_pause_menu(const PauseMenuHost& host)
{
    screen* const scr = og::runtime::current_session != nullptr
        ? og::runtime::current_session->myscreen_
        : nullptr;
    if (scr == nullptr)
        return PauseMenuResult::SessionEnded;

    TRACE("pause_menu", "open");
    // Open the pause first: the world must stop underneath the menu. The
    // server-side same-owner refresh makes a redundant request harmless.
    if (host.request_pause != nullptr)
        host.request_pause();
    if (host.pump_paused != nullptr && !host.pump_paused())
        return PauseMenuResult::SessionEnded;

#ifdef TESTING
    if (!s_force_real_menu)
    {
        // The dialog-seam pattern: consume a scripted outcome instead of
        // blocking in the real loop. release_pause=false leaves the pause
        // pending — the "menu is open" state.
        QueuedPauseOutcome scripted{};
        if (!s_outcome_queue.empty())
        {
            scripted = s_outcome_queue.front();
            s_outcome_queue.erase(s_outcome_queue.begin());
        }
        TRACE("pause_menu", "scripted_outcome %d release=%d",
              static_cast<int>(scripted.outcome),
              scripted.release_pause ? 1 : 0);
        if (scripted.release_pause)
        {
            if (host.resume != nullptr)
                host.resume();
            if (host.pump_paused != nullptr)
                (void)host.pump_paused();
        }
        scr->redrawme = 1;
        return scripted.outcome;
    }
#endif

    // The Esc that opened the menu must not immediately close it.
    clear_key_press_event();

    PauseMenuScreenState state;
    state.host = &host;
    state.background_dirty = true;
    state.last_keepalive_ms = og::input_native::ticks_ms();
    PauseMenuScreenState* const previous = g_pause_state;
    g_pause_state = &state;
    {
        // Fixed 320x200 modal canvas, seeded from the finished world frame.
        ScopedUiCanvas ui_canvas(*scr);
        (void)run_menu_screen(kPauseMenuSpec, &state);
    }
    g_pause_state = previous;

    // Exit hygiene: cursor off, no phantom held keys, no stale interactable
    // buttons for tests to trip over.
    release_mouse();
    clear_transient_input_state();
    clear_allbuttons();

    if (state.outcome == PauseMenuResult::Resumed)
    {
        if (host.resume != nullptr)
            host.resume();
        if (host.pump_paused != nullptr && !host.pump_paused())
            state.outcome = PauseMenuResult::SessionEnded;
    }

    // Rebuild the world frame on the real World target (the view_team
    // re-entry dance): drawing the world while a UI scope is active clips
    // zoomed canvases to 320x200 and leaves the actual world surface stale.
    {
        ScopedCanvasTarget world_target(*scr, CanvasTarget::World);
        scr->redraw();
        scr->swap();
    }
    scr->redrawme = 1;
    return state.outcome;
}

#ifdef TESTING
void pause_menu_testing_queue_outcome(PauseMenuResult outcome,
                                      bool release_pause)
{
    s_outcome_queue.push_back(
        QueuedPauseOutcome{.outcome = outcome,
                           .release_pause = release_pause});
}

void pause_menu_testing_clear_queue()
{
    s_outcome_queue.clear();
}

int pause_menu_testing_queue_remaining()
{
    return static_cast<int>(s_outcome_queue.size());
}

void pause_menu_testing_set_force_real(bool force)
{
    s_force_real_menu = force;
}

void pause_menu_testing_set_keepalive_interval_ms(std::uint32_t interval_ms)
{
    s_keepalive_interval_override_ms = interval_ms;
}
#endif

} // namespace og::ui
