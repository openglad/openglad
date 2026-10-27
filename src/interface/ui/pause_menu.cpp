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
#include <openglad/interface/device_seats.h>
#include <openglad/interface/input.h>
#include <openglad/interface/input_mappings.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/abilities_screen.h>
#include <openglad/interface/ui/input_cycler.h>
#include <openglad/interface/ui/keymap_screen.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/gparser.h>

#include "picker_sdl_defs.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <initializer_list>
#include <format>
#include <span>
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
// PAUSED screen (design §2.1/§7.2): a centered 140px column over the
// darkened world. Full rows are x=90 w=140 h=15 on an 18px pitch; the VIEW
// TEAM / BRIEFING pair is two 66px half-width faces sharing the y=86 band.
// The PLAYERS divider and the title are content text, not buttons.

constexpr Sint32 kPauseColumnX = 90;
constexpr Sint32 kPauseColumnW = 140;
constexpr Sint32 kPauseRowH = 15;
// Half-width pair: 90..156 and 164..230 span the same 90..230 column.
constexpr Sint32 kPauseHalfW = 66;
constexpr Sint32 kPauseHalfRightX = 164;

RowState pause_restart_row_state(const MenuLabelContext& /*context*/)
{
    if (g_pause_state == nullptr || g_pause_state->host == nullptr)
        return RowState::Visible;  // bare engine/pin shape
    return g_pause_state->host->restart_allowed ? RowState::Visible
                                                : RowState::Hidden;
}

RowState pause_briefing_row_state(const MenuLabelContext& /*context*/)
{
    // Gated on the level, not the pause host: no description lines means
    // nothing to show, so the row reads Disabled (visible for nav, inert).
    screen* const scr = og::runtime::current_session != nullptr
        ? og::runtime::current_session->myscreen_
        : nullptr;
    if (scr == nullptr) return RowState::Visible;  // bare engine/pin shape
    return scr->level_description().empty() ? RowState::Disabled
                                            : RowState::Visible;
}

RowState pause_add_player_row_state(const MenuLabelContext& /*context*/)
{
    if (g_pause_state == nullptr || g_pause_state->host == nullptr)
        return RowState::Visible;
    const PauseMenuHost& host = *g_pause_state->host;
    if (host.networked)
        return RowState::Hidden;
    if (host.can_add_player != nullptr && !host.can_add_player())
        return RowState::Hidden;  // 4 seats: the row would only say no
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
             .down = kPauseMenuViewTeamIndex}},
    {.id = "pause_view_team", .label = "VIEW TEAM",
     .x = kPauseColumnX, .y = 86, .w = kPauseHalfW, .h = kPauseRowH,
     .action = ButtonAction::MenuSpecRow, .arg = kPauseMenuViewTeamIndex,
     .nav = {.up = kPauseMenuQuitIndex, .down = kPauseMenuPlayerBaseIndex,
             .right = kPauseMenuBriefingIndex}},
    {.id = "pause_briefing", .label = "BRIEFING",
     .x = kPauseHalfRightX, .y = 86, .w = kPauseHalfW, .h = kPauseRowH,
     .action = ButtonAction::MenuSpecRow, .arg = kPauseMenuBriefingIndex,
     .nav = {.up = kPauseMenuQuitIndex, .down = kPauseMenuPlayerBaseIndex,
             .left = kPauseMenuViewTeamIndex},
     .state_override = &pause_briefing_row_state},
    {.id = "pause_player_0", .label = "P1",
     .x = kPauseColumnX, .y = 112, .w = kPauseColumnW, .h = kPauseRowH,
     .action = ButtonAction::MenuSpecRow, .arg = kPauseMenuPlayerBaseIndex,
     .nav = {.up = kPauseMenuViewTeamIndex,
             .down = kPauseMenuPlayerBaseIndex + 1},
     .state_override = &pause_player_row_state<0>},
    {.id = "pause_player_1", .label = "P2",
     .x = kPauseColumnX, .y = 130, .w = kPauseColumnW, .h = kPauseRowH,
     .action = ButtonAction::MenuSpecRow,
     .arg = kPauseMenuPlayerBaseIndex + 1,
     .nav = {.up = kPauseMenuPlayerBaseIndex,
             .down = kPauseMenuPlayerBaseIndex + 2},
     .state_override = &pause_player_row_state<1>, .hidden = true},
    {.id = "pause_player_2", .label = "P3",
     .x = kPauseColumnX, .y = 148, .w = kPauseColumnW, .h = kPauseRowH,
     .action = ButtonAction::MenuSpecRow,
     .arg = kPauseMenuPlayerBaseIndex + 2,
     .nav = {.up = kPauseMenuPlayerBaseIndex + 1,
             .down = kPauseMenuPlayerBaseIndex + 3},
     .state_override = &pause_player_row_state<2>, .hidden = true},
    {.id = "pause_player_3", .label = "P4",
     .x = kPauseColumnX, .y = 166, .w = kPauseColumnW, .h = kPauseRowH,
     .action = ButtonAction::MenuSpecRow,
     .arg = kPauseMenuPlayerBaseIndex + 3,
     .nav = {.up = kPauseMenuPlayerBaseIndex + 2,
             .down = kPauseMenuAddPlayerIndex},
     .state_override = &pause_player_row_state<3>, .hidden = true},
    {.id = "pause_add_player", .label = "+ ADD PLAYER",
     .x = kPauseColumnX, .y = 182, .w = kPauseColumnW, .h = kPauseRowH,
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

    // Vertical ring over the visible rows, treating the VIEW TEAM/BRIEFING
    // half-width pair as one band anchored on VIEW TEAM: BRIEFING mirrors
    // its vertical links and the two faces link left/right. Both faces are
    // always visible (BRIEFING may be Disabled — still nav-visible).
    std::vector<int> column;
    column.reserve(static_cast<std::size_t>(kPauseMenuButtonCount));
    for (int i = 0; i < kPauseMenuButtonCount; ++i)
    {
        if (!buttons[i].hidden && i != kPauseMenuBriefingIndex)
            column.push_back(i);
    }
    for (std::size_t order = 0; order < column.size(); ++order)
    {
        const int index = column[order];
        const int up = column[(order + column.size() - 1) % column.size()];
        const int down = column[(order + 1) % column.size()];
        buttons[index].nav = {.up = up, .down = down};
    }
    buttons[kPauseMenuBriefingIndex].nav =
        buttons[kPauseMenuViewTeamIndex].nav;
    buttons[kPauseMenuViewTeamIndex].nav.right = kPauseMenuBriefingIndex;
    buttons[kPauseMenuBriefingIndex].nav.left = kPauseMenuViewTeamIndex;

    ensure_highlighted_button_visible(buttons, count, highlighted_button);
}

void pause_menu_draw_background(void* screen_state)
{
    auto* const state = static_cast<PauseMenuScreenState*>(screen_state);
    screen* const scr = og::runtime::current_session->myscreen_;

    // Darkening must never repeat on the same pixels (it accumulates to
    // black), and prepare_ui_canvas_from_world cannot be the per-frame reset:
    // at default canvas dims the world and UI canvases SHARE one surface and
    // the seed is a silent no-op. So: darken ONCE, capture the darkened
    // pixels, and restore them every frame. The full-canvas restore erases
    // the previous frame's pulsing highlight ring (and any nested dialog's
    // scribbles), so nothing needs backing pads over the world.
    if (state == nullptr)
    {
        // Bare engine/pin shape (no installed state): one-shot look only.
        scr->prepare_ui_canvas_from_world();
        scr->darken_screen();
        return;
    }

    if (state->backdrop.empty())
    {
        scr->prepare_ui_canvas_from_world();
        scr->darken_screen();
        // One extra dimming pass on the title strip so PAUSED reads over the
        // viewport HUD text at the top edge — a brightness step on the
        // already-dark image, not a hard box.
        for (int y = 3; y <= 31; ++y)
            for (int x = 0; x < kUiCanvasW; ++x)
                scr->pointb(x, y, PURE_BLACK, 100);
        // Capture RAW RGB, never palette indices: darken_screen blends to
        // arbitrary RGB values, and the indexed get_pixel's exact-match
        // palette scan crushes every miss to black.
        state->backdrop.resize(static_cast<std::size_t>(kUiCanvasW) *
                               static_cast<std::size_t>(kUiCanvasH) * 3u);
        std::size_t at = 0;
        for (int y = 0; y < kUiCanvasH; ++y)
        {
            for (int x = 0; x < kUiCanvasW; ++x)
            {
                Uint8 r = 0, g = 0, b = 0;
                scr->get_pixel(x, y, &r, &g, &b);
                state->backdrop[at++] = r;
                state->backdrop[at++] = g;
                state->backdrop[at++] = b;
            }
        }
        return;
    }

    std::size_t at = 0;
    for (int y = 0; y < kUiCanvasH; ++y)
    {
        for (int x = 0; x < kUiCanvasW; ++x)
        {
            const int r = state->backdrop[at++];
            const int g = state->backdrop[at++];
            const int b = state->backdrop[at++];
            scr->pointb(x, y, r, g, b);
        }
    }
}

void pause_menu_draw_content(void* screen_state)
{
    auto* const state = static_cast<PauseMenuScreenState*>(screen_state);
    screen* const scr = og::runtime::current_session->myscreen_;

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
        scr->text_normal.write_xy_center(160, 105, WHITE, "%s", "- PLAYERS -");
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

// KeyWaitPollCallback for view_team's blocking wait: pump the paused
// transport once per wait pass and keep the pause alive; false ends the
// wait when the session died underneath the open roster.
bool pause_view_team_poll()
{
    PauseMenuScreenState* const state = g_pause_state;
    if (state == nullptr || state->host == nullptr) return true;
    if (state->host->pump_paused != nullptr && !state->host->pump_paused())
    {
        state->outcome = PauseMenuResult::SessionEnded;
        return false;
    }
    maintain_pause_keepalive(*state->host, state->last_keepalive_ms);
    return true;
}

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
        if (!confirmed)
            return MENU_OK;
        TRACE("pause_menu", "quit_confirmed");
        state->outcome = PauseMenuResult::Quit;
        return MENU_EXIT;
    }
    if (row == kPauseMenuViewTeamIndex)
    {
        screen* const scr = og::runtime::current_session->myscreen_;
        if (scr != nullptr && scr->viewob[0] != nullptr)
        {
            TRACE("pause_menu", "view_team");
            // The poll keeps the transport pumped and the pause alive for
            // as long as the roster blocks. The menu's per-frame backdrop
            // restore heals its scribbles; the world-canvas rebuild happens
            // in run_pause_menu's exit dance.
            scr->viewob[0]->view_team(&pause_view_team_poll);
            if (state->outcome == PauseMenuResult::SessionEnded)
                return MENU_EXIT;
        }
        return MENU_OK;
    }
    if (row == kPauseMenuBriefingIndex)
    {
        // Only reachable with description lines present (Disabled gate).
        screen* const scr = og::runtime::current_session->myscreen_;
        if (scr != nullptr)
        {
            // read_scenario's scroll view blocks without a poll hook, so
            // the frame-tick pump is silent while it is open. Bound the
            // exposure: refresh the pause keep-alive window at entry (the
            // view dismisses on any key/click, so the window is
            // user-controlled), and pump immediately on return.
            if (state->host != nullptr &&
                state->host->request_pause != nullptr)
            {
                state->host->request_pause();
                state->last_keepalive_ms = og::input_native::ticks_ms();
            }
            TRACE("pause_menu", "briefing");
            (void)read_scenario(scr);
            if (state->host != nullptr &&
                state->host->pump_paused != nullptr &&
                !state->host->pump_paused())
            {
                state->outcome = PauseMenuResult::SessionEnded;
                return MENU_EXIT;
            }
        }
        return MENU_OK;
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
// Player screen (design §2.2, geometry unified with Base Camp seat settings
// per §7.1 — the kPlayerScreen* grid in picker_sdl_defs.h): BACK / the y=30
// mode band / the y=54 INPUT+ZOOM band / the binding panel with the
// RADAR/HP/FOES/SCORE stack on its right (both spanning y=78..161) /
// REMOVE PLAYER on the y=169 command band.

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

RowState pause_player_zoom_row_state(const MenuLabelContext& /*context*/)
{
    // A backend without the off-screen floor-layer compositor can never
    // zoom: the row reads Disabled instead of silently doing nothing.
    return per_view_zoom_available() ? RowState::Visible
                                     : RowState::Disabled;
}

// #358 click cells: each cell's state for the seat on screen (the shared
// rule, binding_panel_cell_state). No installed state = the bare spec shape.
template <int Cell>
RowState pause_binding_cell_state(const MenuLabelContext& /*context*/)
{
    return binding_panel_cell_state(
        g_pause_player_state != nullptr ? g_pause_player_state->seat : -1,
        Cell);
}

// ABILITIES (#376): the character this seat drives. Dimmed with New Ability
// Select off (there is no layout to edit, design E.1) and while the seat
// drives no living body. No installed state = the bare spec shape.
RowState pause_player_abilities_row_state(const MenuLabelContext& /*context*/)
{
    if (!og::input::ability_select_on())
        return RowState::Disabled;
    if (g_pause_player_state == nullptr)
        return RowState::Visible;
    return seat_driven_walker(g_pause_player_state->seat) != nullptr
        ? RowState::Visible
        : RowState::Disabled;
}

// KEYMAP (#388): every seat but a phone's touch seat.
RowState pause_player_keymap_row_state(const MenuLabelContext& /*context*/)
{
    if (g_pause_player_state == nullptr)
        return RowState::Visible;
    return keymap_door_shown(g_pause_player_state->seat) ? RowState::Visible
                                                          : RowState::Hidden;
}

constexpr MenuButtonSpec kPausePlayerRows[] = {
    {.id = "pause_player_back", .label = "BACK",
     .hotkey = KEYSTATE_ESCAPE,
     .x = 10, .y = 8, .w = 50, .h = 15,
     .action = ButtonAction::ReturnMenu, .arg = MENU_REDRAW,
     .nav = {.up = kPausePlayerRemoveIndex,
             .down = kPausePlayerModeIndex}},
    {.id = "pause_input", .label = "INPUT: WASD",
     .x = kPlayerScreenColAX, .y = kPlayerScreenBand2Y,
     .w = kPlayerScreenColAW, .h = kPlayerScreenBandH,
     .action = ButtonAction::MenuSpecRow, .arg = kPausePlayerInputIndex,
     .nav = {.up = kPausePlayerModeIndex,
             .down = kPausePlayerCellFirstIndex,
             .right = kPausePlayerZoomIndex}},
    {.id = "pause_direction", .label = "4-DIRECTION",
     .x = kPlayerScreenColAX, .y = kPlayerScreenBand1Y,
     .w = kPlayerScreenColAW, .h = kPlayerScreenBandH,
     .action = ButtonAction::MenuSpecRow, .arg = kPausePlayerModeIndex,
     .nav = {.up = kPausePlayerBackIndex,
             .down = kPausePlayerInputIndex,
             .right = kPausePlayerRemapIndex}},
    {.id = "pause_remap", .label = "REMAP ALL",
     .x = kPlayerScreenColBX, .y = kPlayerScreenBand1Y,
     .w = kPlayerScreenColBW, .h = kPlayerScreenBandH,
     .action = ButtonAction::MenuSpecRow, .arg = kPausePlayerRemapIndex,
     .nav = {.up = kPausePlayerBackIndex,
             .down = kPausePlayerZoomIndex,
             .left = kPausePlayerModeIndex,
             .right = kPausePlayerResetIndex}},
    {.id = "pause_reset", .label = "RESET",
     .x = kPlayerScreenColCX, .y = kPlayerScreenBand1Y,
     .w = kPlayerScreenColCW, .h = kPlayerScreenBandH,
     .action = ButtonAction::MenuSpecRow, .arg = kPausePlayerResetIndex,
     .nav = {.up = kPausePlayerBackIndex,
             .down = kPausePlayerHudRadarIndex,
             .left = kPausePlayerRemapIndex}},
    {.id = "pause_remove", .label = "REMOVE PLAYER",
     .x = 166, .y = kPlayerScreenBottomBandY, .w = 138, .h = 18,
     .action = ButtonAction::MenuSpecRow, .arg = kPausePlayerRemoveIndex,
     .nav = {.up = kPausePlayerHudScoreIndex,
             .down = kPausePlayerBackIndex},
     .state_override = &pause_player_remove_row_state},
    {.id = "pause_zoom", .label = "ZOOM: GAME",
     .x = kPlayerScreenColBX, .y = kPlayerScreenBand2Y,
     .w = kPlayerScreenColBW, .h = kPlayerScreenBandH,
     .action = ButtonAction::MenuSpecRow, .arg = kPausePlayerZoomIndex,
     .nav = {.up = kPausePlayerRemapIndex,
             .down = kPausePlayerCellFirstIndex + 8,
             .left = kPausePlayerInputIndex,
             .right = kPausePlayerHudRadarIndex},
     .state_override = &pause_player_zoom_row_state},
    {.id = "pause_hud_radar", .label = "RADAR: ON",
     .x = kPlayerScreenColCX, .y = kPlayerScreenHudTopY,
     .w = kPlayerScreenColCW, .h = kPlayerScreenBandH,
     .action = ButtonAction::MenuSpecRow, .arg = kPausePlayerHudRadarIndex,
     .nav = {.up = kPausePlayerResetIndex,
             .down = kPausePlayerHudLifeIndex,
             .left = kPausePlayerCellFirstIndex + 8 +
                     binding_cell_line_beside_hud(0)}},
    {.id = "pause_hud_life", .label = "HP: ON",
     .x = kPlayerScreenColCX,
     .y = kPlayerScreenHudTopY + kPlayerScreenHudPitch,
     .w = kPlayerScreenColCW, .h = kPlayerScreenBandH,
     .action = ButtonAction::MenuSpecRow, .arg = kPausePlayerHudLifeIndex,
     .nav = {.up = kPausePlayerHudRadarIndex,
             .down = kPausePlayerHudFoesIndex,
             .left = kPausePlayerCellFirstIndex + 8 +
                     binding_cell_line_beside_hud(1)}},
    {.id = "pause_hud_foes", .label = "FOES: ON",
     .x = kPlayerScreenColCX,
     .y = kPlayerScreenHudTopY + 2 * kPlayerScreenHudPitch,
     .w = kPlayerScreenColCW, .h = kPlayerScreenBandH,
     .action = ButtonAction::MenuSpecRow, .arg = kPausePlayerHudFoesIndex,
     .nav = {.up = kPausePlayerHudLifeIndex,
             .down = kPausePlayerHudScoreIndex,
             .left = kPausePlayerCellFirstIndex + 8 +
                     binding_cell_line_beside_hud(2)}},
    {.id = "pause_hud_score", .label = "SCORE: ON",
     .x = kPlayerScreenColCX,
     .y = kPlayerScreenHudTopY + 3 * kPlayerScreenHudPitch,
     .w = kPlayerScreenColCW, .h = kPlayerScreenBandH,
     .action = ButtonAction::MenuSpecRow, .arg = kPausePlayerHudScoreIndex,
     .nav = {.up = kPausePlayerHudFoesIndex,
             .down = kPausePlayerRemoveIndex,
             .left = kPausePlayerCellFirstIndex + 8 +
                     binding_cell_line_beside_hud(3)}},
    // #358: the binding panel's click cells, 11..26.
    OG_BINDING_PANEL_CELLS("pause", kPausePlayerCellFirstIndex,
                           pause_binding_cell_state, kPausePlayerInputIndex,
                           kPausePlayerZoomIndex, kPausePlayerHudRadarIndex,
                           kPausePlayerAbilitiesIndex, kPausePlayerRemoveIndex),
    // The bottom-left band's two doors, ABILITIES (#376) and KEYMAP (#388):
    // left to right ABILITIES, KEYMAP, REMOVE PLAYER, each down to BACK.
    {.id = "pause_abilities", .label = "ABILITIES",
     .x = kPlayerScreenColAX, .y = kPlayerScreenBottomBandY,
     .w = kPlayerScreenBottomHalfW,
     .h = kPlayerScreenBandH,
     .action = ButtonAction::MenuSpecRow, .arg = kPausePlayerAbilitiesIndex,
     .nav = {.up = kPausePlayerCellFirstIndex + 7,
             .down = kPausePlayerBackIndex,
             .right = kPausePlayerKeymapIndex},
     .state_override = &pause_player_abilities_row_state},
    {.id = "pause_keymap", .label = "KEYMAP",
     .x = kPlayerScreenBottomMidX, .y = kPlayerScreenBottomBandY,
     .w = kPlayerScreenBottomHalfW,
     .h = kPlayerScreenBandH,
     .action = ButtonAction::MenuSpecRow, .arg = kPausePlayerKeymapIndex,
     .nav = {.up = kPausePlayerCellFirstIndex + 15,
             .down = kPausePlayerBackIndex,
             .left = kPausePlayerAbilitiesIndex,
             .right = kPausePlayerRemoveIndex},
     .state_override = &pause_player_keymap_row_state},
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
    sync_pause_label(buttons, kPausePlayerZoomIndex,
                     player_view_zoom_label(state->seat));
    sync_pause_label(buttons, kPausePlayerHudRadarIndex,
                     player_hud_row_label(state->seat, PlayerHudRow::Radar));
    sync_pause_label(buttons, kPausePlayerHudLifeIndex,
                     player_hud_row_label(state->seat, PlayerHudRow::Life));
    sync_pause_label(buttons, kPausePlayerHudFoesIndex,
                     player_hud_row_label(state->seat, PlayerHudRow::Foes));
    sync_pause_label(buttons, kPausePlayerHudScoreIndex,
                     player_hud_row_label(state->seat, PlayerHudRow::Score));

    // The gate pass ran first, so REMOVE's hidden flag is current: route the
    // vertical chains around it when it is gone (ZOOM is only ever Disabled
    // — still nav-visible — so the static links through it stand).
    const bool remove_visible = !buttons[kPausePlayerRemoveIndex].hidden;
    buttons[kPausePlayerHudScoreIndex].nav.down =
        remove_visible ? kPausePlayerRemoveIndex : kPausePlayerBackIndex;
    // #358: the click cells (and INPUT/ZOOM/HUD links into them) around the
    // cells this seat cannot use.
    rewire_binding_panel_cells(
        buttons, count,
        {.first_cell = kPausePlayerCellFirstIndex,
         .input = kPausePlayerInputIndex,
         .zoom = kPausePlayerZoomIndex,
         .hud_first = kPausePlayerHudRadarIndex,
         .bottom_left = kPausePlayerAbilitiesIndex,
         // Under the ACTIONS column: REMOVE, or KEYMAP when REMOVE is gone.
         .bottom_right = remove_visible ? kPausePlayerRemoveIndex
                                        : kPausePlayerKeymapIndex,
         .back = kPausePlayerBackIndex});
    rewire_player_screen_bottom_band(
        buttons, count,
        {.first_cell = kPausePlayerCellFirstIndex,
         .left = kPausePlayerAbilitiesIndex,
         .keymap = kPausePlayerKeymapIndex,
         .remove = kPausePlayerRemoveIndex,
         .back = kPausePlayerBackIndex});
    buttons[kPausePlayerBackIndex].nav.up =
        remove_visible ? kPausePlayerRemoveIndex : kPausePlayerHudScoreIndex;

    ensure_highlighted_button_visible(buttons, count, highlighted_button);
}

void pause_player_draw_background(void* /*screen_state*/)
{
    // Opaque full-screen repaint every frame (the options-panel shape): the
    // binding grid re-derives per frame, and the remap wizard scribbles
    // full-screen prompts this pass must heal.
    screen* const scr = og::runtime::current_session->myscreen_;
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

    text& mytext = scr->text_normal;
    mytext.write_xy_center(
        160, 13, DARK_BLUE, "%s",
        std::format("LOCAL PLAYER {} / P{}", state->seat + 1,
                    state->player_number)
            .c_str());

    // §7.1 unified geometry: the one binding panel both player screens
    // draw (x=12..208, top/bottom aligned with the HUD stack at y=78..161).
    draw_binding_panel(state->seat, og::input::ability_select_on());
}

bool pause_player_frame_tick(void* screen_state, int /*frame*/)
{
    auto* const state = static_cast<PausePlayerScreenState*>(screen_state);
    if (state == nullptr || state->removed || state->session_ended)
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

// The pump a door on this screen hands its own screen (KEYMAP, ABILITIES):
// the paused transport plus the menu's keep-alive, once per frame of the
// child; false when the session died underneath it.
bool pause_player_child_pump(void* context)
{
    auto* const state = static_cast<PausePlayerScreenState*>(context);
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
        std::string unavailable;
        (void)cycle_player_input(cfg, state->seat, active_count, &unavailable);
        persist_pause_player_controls();
        if (!unavailable.empty())
        {
            // The device is listed but its node will not open (permissions);
            // say so instead of letting the row look like it did nothing.
            popup_dialog("INPUT",
                         std::format("COULD NOT OPEN {}", unavailable).c_str());
        }
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
        save_player_keys_after_remap(state->seat);
        return MENU_OK;
    }
    if (row >= kPausePlayerCellFirstIndex &&
        row < kPausePlayerCellFirstIndex + kBindingPanelCellCount)
    {
        // #358: one key, from its line in the panel.
        const int cell = row - kPausePlayerCellFirstIndex;
        const int key =
            binding_panel_cell_key(cell, og::input::ability_select_on());
        if (key < 0 ||
            binding_panel_cell_state(state->seat, cell) != RowState::Visible)
        {
            return MENU_OK;
        }
        TRACE("pause_menu", "rebind seat=%d key=%d", state->seat, key);
        (void)rebind_player_key_with_poll(state->seat, key, &pause_remap_poll);
        save_player_keys_after_remap(state->seat);
        return MENU_OK;
    }
    if (row == kPausePlayerAbilitiesIndex)
    {
        // #376: the character this seat drives, edited in place; BACK sends
        // a changed layout to the server.
        if (!og::input::ability_select_on() ||
            seat_driven_walker(state->seat) == nullptr)
        {
            return MENU_OK;
        }
        const PauseAbilitiesHost abilities_host{
            .seat = state->seat,
            .pump = &pause_player_child_pump,
            .pump_context = state,
            .set_belt = state->host != nullptr ? state->host->set_belt
                                               : nullptr,
        };
        [[maybe_unused]] const PauseAbilitiesResult result =
            run_pause_abilities_screen(abilities_host);
        TRACE("pause_menu", "abilities seat=%d committed=%d changed=%d",
              state->seat, result.committed ? 1 : 0,
              result.control_changed ? 1 : 0);
        return MENU_OK;
    }
    if (row == kPausePlayerKeymapIndex)
    {
        // #388: the keyboard picture of this machine's seats, this seat
        // first.
        if (!keymap_door_shown(state->seat))
            return MENU_OK;
        KeymapScreenHost keymap_host;
        for (const PauseSeatInfo& seat : collect_pause_seats(networked))
            keymap_host.seats.push_back(
                KeymapSeat{.seat = seat.seat,
                           .player_number = seat.player_number});
        keymap_host.pump = &pause_player_child_pump;
        keymap_host.pump_context = state;
        TRACE("pause_menu", "keymap seat=%d", state->seat);
        (void)run_keymap_screen(state->seat, keymap_host);
        return MENU_OK;
    }
    if (row == kPausePlayerResetIndex)
    {
        if (reset_default_player_controls_for_player(state->seat))
            persist_pause_player_controls();
        return MENU_OK;
    }
    if (row == kPausePlayerZoomIndex)
    {
        if (per_view_zoom_available())
        {
            (void)cycle_player_view_zoom(state->seat);
            persist_pause_player_controls();
        }
        return MENU_OK;
    }
    if (row == kPausePlayerHudRadarIndex || row == kPausePlayerHudLifeIndex ||
        row == kPausePlayerHudFoesIndex || row == kPausePlayerHudScoreIndex)
    {
        const PlayerHudRow hud_row = row == kPausePlayerHudRadarIndex
            ? PlayerHudRow::Radar
            : row == kPausePlayerHudLifeIndex
                ? PlayerHudRow::Life
                : row == kPausePlayerHudFoesIndex ? PlayerHudRow::Foes
                                                  : PlayerHudRow::Score;
        toggle_player_hud_row(state->seat, hud_row);
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
    // A true modal over the live (darkened) world: Overlay never fades,
    // even at depth 1 — the only screens carrying this kind are the pause
    // family (#237).
    spec.kind = MenuScreenKind::Overlay;
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
    // Pause family: a mid-mission modal card, never faded (#237).
    spec.kind = MenuScreenKind::Overlay;
    spec.exit_on_redraw = true;
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

// --------------------------------------------------------------------------
// §7.1 shared per-player HUD/zoom helpers (declared in picker_sdl_defs.h;
// one implementation behind both the pause player screen and Base Camp seat
// settings). The seat's live viewscreen (viewob[seat]) is the runtime
// carrier when present; cfg is the carrier otherwise and always the mirror.

namespace {

viewscreen* seat_view(int seat)
{
    screen* const scr = og::runtime::current_session != nullptr
        ? og::runtime::current_session->myscreen_
        : nullptr;
    if (scr == nullptr || seat < 0 || seat >= scr->numviews ||
        seat >= MAX_VIEWS)
    {
        return nullptr;
    }
    return scr->viewob[static_cast<std::size_t>(seat)].get();
}

// Effective settings for `seat`: cfg first (also the no-live-view carrier),
// overlaid by the live viewscreen prefs when one exists.
PlayerHudSettings gather_player_hud(int seat)
{
    PlayerHudSettings hud;
    (void)load_player_hud_settings_from_cfg(cfg, seat, hud);
    if (const viewscreen* const view = seat_view(seat))
    {
        hud.radar = view->prefs[PREF_RADAR] != PREF_RADAR_OFF ? 1 : 0;
        hud.life_on = view->prefs[PREF_LIFE] != PREF_LIFE_OFF ? 1 : 0;
        hud.foes = view->prefs[PREF_FOES] != PREF_FOES_OFF ? 1 : 0;
        hud.score = view->prefs[PREF_SCORE] != PREF_SCORE_OFF ? 1 : 0;
        hud.zoom_step = static_cast<int>(view->view_zoom_step_);
    }
    return hud;
}

bool view_zoom_step_allowed(int seat, int step)
{
    if (step == 0)
        return true;  // GAME is always selectable
    screen* const scr = og::runtime::current_session != nullptr
        ? og::runtime::current_session->myscreen_
        : nullptr;
    if (scr == nullptr)
        return false;
    // §7.1: the canvas derives from the minimum effective zoom over the
    // seats, so the gate is whether the candidate COMPOSITION still fits
    // the canvas budget (the same clamp machinery the global zoom rides).
    int n_min = viewscreen::view_scale_num_for_step(step);
    for (int i = 0; i < scr->numviews && i < MAX_VIEWS; ++i)
    {
        if (i == seat)
            continue;
        if (const viewscreen* const other = scr->viewob[
                static_cast<std::size_t>(i)].get())
            n_min = std::min(n_min, static_cast<int>(other->view_scale_num()));
    }
    return scr->world_zoom_composition_fits(n_min);
}

} // namespace

std::string player_hud_row_label(int seat, PlayerHudRow row)
{
    const PlayerHudSettings hud = gather_player_hud(seat);
    switch (row)
    {
    case PlayerHudRow::Radar:
        return hud.radar != 0 ? "RADAR: ON" : "RADAR: OFF";
    case PlayerHudRow::Life:
        return hud.life_on != 0 ? "HP: ON" : "HP: OFF";
    case PlayerHudRow::Foes:
        return hud.foes != 0 ? "FOES: ON" : "FOES: OFF";
    case PlayerHudRow::Score:
        return hud.score != 0 ? "SCORE: ON" : "SCORE: OFF";
    }
    return "--";
}

void toggle_player_hud_row(int seat, PlayerHudRow row)
{
    PlayerHudSettings hud = gather_player_hud(seat);
    viewscreen* const view = seat_view(seat);
    switch (row)
    {
    case PlayerHudRow::Radar:
        hud.radar = hud.radar != 0 ? 0 : 1;
        if (view != nullptr)
            view->prefs[PREF_RADAR] =
                hud.radar != 0 ? PREF_RADAR_ON : PREF_RADAR_OFF;
        break;
    case PlayerHudRow::Life:
        // ON/OFF only: the first toggle normalizes a legacy TEXT/BARS/SMALL
        // value (all of which display as ON) to BOTH/OFF.
        hud.life_on = hud.life_on != 0 ? 0 : 1;
        if (view != nullptr)
            view->prefs[PREF_LIFE] =
                hud.life_on != 0 ? PREF_LIFE_BOTH : PREF_LIFE_OFF;
        break;
    case PlayerHudRow::Foes:
        hud.foes = hud.foes != 0 ? 0 : 1;
        if (view != nullptr)
            view->prefs[PREF_FOES] =
                hud.foes != 0 ? PREF_FOES_ON : PREF_FOES_OFF;
        break;
    case PlayerHudRow::Score:
        hud.score = hud.score != 0 ? 0 : 1;
        if (view != nullptr)
            view->prefs[PREF_SCORE] =
                hud.score != 0 ? PREF_SCORE_ON : PREF_SCORE_OFF;
        break;
    }
    save_player_hud_settings_to_cfg(cfg, seat, hud);
    if (screen* const scr = og::runtime::current_session != nullptr
            ? og::runtime::current_session->myscreen_
            : nullptr)
    {
        scr->redrawme = 1;  // the toggle takes effect on the next redraw
    }
    TRACE("pause_menu", "hud_toggle seat=%d row=%d radar=%d life=%d foes=%d score=%d",
          seat, static_cast<int>(row), hud.radar, hud.life_on, hud.foes,
          hud.score);
}

std::string player_view_zoom_label(int seat)
{
    const int step = gather_player_hud(seat).zoom_step;
    if (step <= 0)
        return "ZOOM: GAME";
    return std::format("ZOOM: 0.{}X", 10 - step);
}

int cycle_player_view_zoom(int seat)
{
    PlayerHudSettings hud = gather_player_hud(seat);
    if (!per_view_zoom_available())
        return hud.zoom_step;
    int step = hud.zoom_step;
    for (int probe = 0; probe < viewscreen::kViewZoomStepCount; ++probe)
    {
        step = (step + 1) % viewscreen::kViewZoomStepCount;
        if (view_zoom_step_allowed(seat, step))
            break;  // budget clamp: over-budget steps are skipped
    }
    hud.zoom_step = step;
    const bool live_view_changed = seat_view(seat) != nullptr;
    if (viewscreen* const view = seat_view(seat))
        view->view_zoom_step_ = static_cast<Sint32>(step);
    save_player_hud_settings_to_cfg(cfg, seat, hud);
    if (screen* const scr = og::runtime::current_session != nullptr
            ? og::runtime::current_session->myscreen_
            : nullptr)
    {
        // §7.1: fold the new composition into the canvas, re-derive every
        // window + slot and publish the presentation partition. Render/
        // geometry only — the sim and transports are untouched.
        if (live_view_changed)
            scr->relayout_views();
        scr->redrawme = 1;
    }
    TRACE("pause_menu", "view_zoom seat=%d step=%d", seat, step);
    return step;
}

bool per_view_zoom_available()
{
    // §7.1: per-view zoom needs the partitioned-presentation seam (canvas
    // composition + per-view slices). Backends without it disable the row —
    // never wrong output.
    screen* const scr = og::runtime::current_session != nullptr
        ? og::runtime::current_session->myscreen_
        : nullptr;
    return scr != nullptr && scr->world_present_partition_supported();
}

std::string format_binding_panel_line(const char* label,
                                      const std::string& value,
                                      int budget_chars)
{
    std::string line = std::format("{}: {}", label, value);
    if (budget_chars > 0 &&
        static_cast<int>(line.size()) > budget_chars)
    {
        line.resize(static_cast<std::size_t>(budget_chars));
    }
    return line;
}

// --- The one binding panel (#358 folded the two screens' copies) ---------

namespace {

bool seat_in_range(int seat)
{
    return seat >= 0 && seat < MAX_PLAYERS;
}
} // namespace

void draw_binding_panel(int seat, bool ability_select)
{
    screen* const scr = og::runtime::current_session->myscreen_;
    text& mytext = scr->text_normal;
    scr->draw_button(kPlayerScreenColAX, kPlayerScreenPanelTopY,
                     kPlayerScreenPanelRightX, kPlayerScreenPanelBottomY,
                     2, 1);
    mytext.write_xy(20, kPlayerScreenPanelHeaderY, DARK_BLUE, "%s",
                    "MOVEMENT");
    mytext.write_xy(104, kPlayerScreenPanelHeaderY, DARK_BLUE, "%s",
                    "ACTIONS");

    // The ACTIONS rows follow New Ability Select: the classic seven with the
    // setting off, one row per ability key (SPEC 1/2/3) with it on
    // (og::ui::binding_panel_action_rows).
    const std::span<const BindingPanelRow> actions =
        binding_panel_action_rows(ability_select);
    const bool eight_dir =
        get_player_control_mode(seat) ==
        static_cast<int>(ControlDirectionMode::EightDirection);
    // A joystick-driven seat reads its own device bindings (B0/A0+/HU style);
    // the still-stored keyboard names would be a lie while the pad drives.
    const bool joystick = seat_in_range(seat) && playerHasJoystick(seat);
    const auto binding_value = [seat, joystick](int key) {
        return joystick
            ? joy_binding_display_name(player_joy[seat].key_type[key],
                                       player_joy[seat].key_index[key])
            : player_control_key_display_name(seat, key);
    };

    const std::span<const BindingPanelMovementRow> movement =
        binding_panel_movement_rows();
    for (std::size_t index = 0; index < movement.size(); ++index)
    {
        const BindingPanelMovementRow& binding = movement[index];
        const bool active = !binding.diagonal || eight_dir;
        const std::string value =
            active ? binding_value(binding.key) : std::string("--");
        const std::string line = format_binding_panel_line(
            binding.label, value, kBindingPanelMovementChars);
        mytext.write_xy(
            20,
            kPlayerScreenPanelLineY +
                static_cast<int>(index) * kBindingPanelLinePitch,
            active ? DARK_BLUE : GREY, "%s", line.c_str());
    }
    for (std::size_t index = 0; index < actions.size(); ++index)
    {
        const BindingPanelRow& binding = actions[index];
        const std::string line = format_binding_panel_line(
            binding.label, binding_value(binding.key),
            kBindingPanelActionsChars);
        mytext.write_xy(
            104,
            kPlayerScreenPanelLineY +
                static_cast<int>(index) * kBindingPanelLinePitch,
            DARK_BLUE, "%s", line.c_str());
    }
}

int binding_panel_cell_key(int cell, bool ability_select)
{
    if (cell < 0 || cell >= kBindingPanelCellCount)
        return -1;
    if (cell < kBindingPanelLinesPerColumn)
        return binding_panel_movement_rows()[static_cast<std::size_t>(cell)].key;
    const std::span<const BindingPanelRow> actions =
        binding_panel_action_rows(ability_select);
    const auto line =
        static_cast<std::size_t>(cell - kBindingPanelLinesPerColumn);
    return line < actions.size() ? actions[line].key : -1;
}

RowState binding_panel_cell_state(int seat, int cell)
{
    if (binding_panel_cell_key(cell, og::input::ability_select_on()) < 0)
        return RowState::Hidden;
    if (cell < kBindingPanelLinesPerColumn && seat_in_range(seat) &&
        binding_panel_movement_rows()[static_cast<std::size_t>(cell)].diagonal)
    {
        // The panel draws an unused diagonal grey "--": nothing to click.
        // Pads never bind diagonals (JoyData::setKeyFromEvent forces NONE).
        const bool eight_dir =
            get_player_control_mode(seat) ==
            static_cast<int>(ControlDirectionMode::EightDirection);
        if (!eight_dir || playerHasJoystick(seat))
            return RowState::Hidden;
    }
    return RowState::Visible;
}

void rewire_binding_panel_cells(button* buttons, int count,
                                const BindingCellNavAnchors& anchors)
{
    if (buttons == nullptr ||
        anchors.first_cell + kBindingPanelCellCount > count)
    {
        return;
    }
    const auto visible = [buttons, count](int index) {
        return index >= 0 && index < count && !buttons[index].hidden;
    };
    // The visible cells of each column, top to bottom.
    std::vector<int> movement;
    std::vector<int> actions;
    for (int line = 0; line < kBindingPanelLinesPerColumn; ++line)
    {
        const int move_cell = anchors.first_cell + line;
        const int action_cell =
            anchors.first_cell + kBindingPanelLinesPerColumn + line;
        if (visible(move_cell))
            movement.push_back(move_cell);
        if (visible(action_cell))
            actions.push_back(action_cell);
    }
    // The column's cell nearest a panel line (ties go up).
    const auto nearest = [&anchors](const std::vector<int>& column,
                                    int line, int column_first) {
        int best = -1;
        int best_distance = 0;
        for (const int cell : column)
        {
            const int distance =
                std::abs((cell - anchors.first_cell - column_first) - line);
            if (best < 0 || distance < best_distance)
            {
                best = cell;
                best_distance = distance;
            }
        }
        return best;
    };
    const auto first_visible = [&visible](std::initializer_list<int> order,
                                          int fallback) {
        for (const int index : order)
            if (visible(index))
                return index;
        return fallback;
    };
    const int movement_exit = first_visible(
        {anchors.bottom_left, anchors.bottom_right}, anchors.back);
    const int actions_exit = first_visible(
        {anchors.bottom_right, anchors.bottom_left}, anchors.back);

    for (std::size_t k = 0; k < movement.size(); ++k)
    {
        MenuNav& nav = buttons[movement[k]].nav;
        const int line = movement[k] - anchors.first_cell;
        nav.up = k > 0 ? movement[k - 1] : anchors.input;
        nav.down = k + 1 < movement.size() ? movement[k + 1] : movement_exit;
        nav.left = -1;
        nav.right = actions.empty()
            ? anchors.hud_first + binding_cell_hud_offset(line)
            : nearest(actions, line, kBindingPanelLinesPerColumn);
    }
    for (std::size_t k = 0; k < actions.size(); ++k)
    {
        MenuNav& nav = buttons[actions[k]].nav;
        const int line =
            actions[k] - anchors.first_cell - kBindingPanelLinesPerColumn;
        nav.up = k > 0 ? actions[k - 1] : anchors.zoom;
        nav.down = k + 1 < actions.size() ? actions[k + 1] : actions_exit;
        nav.left = movement.empty() ? anchors.input : nearest(movement, line, 0);
        nav.right = anchors.hud_first + binding_cell_hud_offset(line);
    }

    // The neighbours' links in.
    if (!movement.empty())
        buttons[anchors.input].nav.down = movement.front();
    if (!actions.empty())
        buttons[anchors.zoom].nav.down = actions.front();
    for (int row = 0; row < 4; ++row)
    {
        const int hud = anchors.hud_first + row;
        if (hud < count && !actions.empty())
            buttons[hud].nav.left =
                nearest(actions, binding_cell_line_beside_hud(row),
                        kBindingPanelLinesPerColumn);
    }
    if (visible(anchors.bottom_left) && !movement.empty())
        buttons[anchors.bottom_left].nav.up = movement.back();
}

void rewire_player_screen_bottom_band(button* buttons, int count,
                                      const PlayerScreenBottomBand& band)
{
    if (buttons == nullptr ||
        band.first_cell + kBindingPanelCellCount > count)
    {
        return;
    }
    const auto visible = [buttons, count](int index) {
        return index >= 0 && index < count && !buttons[index].hidden;
    };
    std::vector<int> faces;
    for (const int face : {band.left, band.keymap, band.remove})
        if (visible(face))
            faces.push_back(face);
    for (std::size_t k = 0; k < faces.size(); ++k)
    {
        MenuNav& nav = buttons[faces[k]].nav;
        nav.left = k > 0 ? faces[k - 1] : -1;
        nav.right = k + 1 < faces.size() ? faces[k + 1] : -1;
        nav.down = band.back;
    }
    if (!visible(band.keymap))
        return;
    // KEYMAP sits under the ACTIONS column's left half: up is its last line.
    for (int line = kBindingPanelLinesPerColumn - 1; line >= 0; --line)
    {
        const int cell = band.first_cell + kBindingPanelLinesPerColumn + line;
        if (visible(cell))
        {
            buttons[band.keymap].nav.up = cell;
            return;
        }
    }
}

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
    const InputCycleOption selection = current_input_selection(seat.seat);
    // Same owner-token rule as the seat card and the INPUT cycler
    // (seat_owner_is_screen): a phone's keyboard-mapped seat is really
    // driven by the touchscreen, so the row names the screen.
    const std::string owner = og::input::seat_owner_is_screen(
                                  og::input::is_single_seat_device(),
                                  selection.is_joystick, seat.seat)
        ? std::string(og::input::kScreenSeatOwnerLabel)
        : og::input::mapping_display_name(selection.name);
    return std::format("P{}: {}", seat.player_number, owner);
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

    // Every outcome that hands control back to the game releases the pause.
    // Restart and Quit tear the mission down: leaving the server suspended
    // strands the next mission (and every other seat) behind a PAUSED overlay
    // nobody can clear (#246). SessionEnded is the one exception — there is
    // no host left to resume.
    if (state.outcome == PauseMenuResult::Resumed ||
        state.outcome == PauseMenuResult::Restart ||
        state.outcome == PauseMenuResult::Quit)
    {
        if (host.resume != nullptr)
            host.resume();
        // The resume RESPONSE only reaches the server through a pump; without
        // one it dies in the outbound queue with the pause still standing.
        const bool session_alive =
            host.pump_paused == nullptr || host.pump_paused();
        if (!session_alive && state.outcome == PauseMenuResult::Resumed)
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
