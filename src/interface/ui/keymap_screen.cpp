/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// KEYMAP: see keymap_screen.h.

#include <openglad/interface/ui/keymap_screen.h>

#include <openglad/core/test_trace.h>
#include <openglad/interface/base.h>
#include <openglad/interface/button.h>
#include <openglad/interface/device_seats.h>
#include <openglad/interface/input.h>
#include <openglad/interface/input_mappings.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/input_cycler.h>
#include <openglad/interface/web_control_defaults.h>

#include "picker_sdl_defs.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <optional>

namespace og::ui {

namespace {

constexpr MenuButtonSpec kKeymapRows[] = {
    {.id = "keymap_back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = kKeymapBackX, .y = kKeymapBackY, .w = kKeymapBackW,
     .h = kKeymapBackH, .action = ButtonAction::ReturnMenu, .arg = MENU_EXIT},
};

struct KeymapScreenState {
    const KeymapScreenHost* host = nullptr;
    std::string title;
    std::vector<std::string> legend;
    keyboard_picture::Output picture;
    bool session_ended = false;
};

KeymapScreenState* g_keymap = nullptr;

std::vector<button>& keymap_buttons()
{
    static std::vector<button> buttons;
    return buttons;
}

std::string upper(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

std::string cut(std::string text)
{
    if (text.size() > kKeymapMaxGlyphs)
        text.resize(kKeymapMaxGlyphs);
    return text;
}

std::string native_key_name(int keycode)
{
    const char* name = og::input_native::key_name(keycode);
    return name != nullptr && name[0] != '\0' ? std::string(name)
                                              : "KEY " + std::to_string(keycode);
}

KeymapSeatView describe_seat(const KeymapSeat& seat)
{
    const InputCycleOption selection = current_input_selection(seat.seat);
    KeymapSeatView view;
    view.player_number = seat.player_number;
    view.on_pad = selection.is_joystick;
    // The seat card's owner rule: a phone's keyboard seat is the screen.
    view.owner = og::input::seat_owner_is_screen(
                     og::input::is_single_seat_device(), selection.is_joystick,
                     seat.seat)
        ? std::string(og::input::kScreenSeatOwnerLabel)
        : og::input::mapping_display_name(selection.name);
    return view;
}

keyboard_picture::Seat picture_seat(const KeymapSeat& seat)
{
    keyboard_picture::Seat out;
    out.player_number = seat.player_number;
    out.on_keyboard = !playerHasJoystick(seat.seat);
    out.eight_direction = get_player_control_mode(seat.seat) ==
                          static_cast<int>(ControlDirectionMode::EightDirection);
    for (int slot = 0; slot < NUM_KEYS; ++slot)
        out.keys[static_cast<std::size_t>(slot)] =
            input_player_keys()[seat.seat][slot];
    return out;
}

void keymap_draw_background(void* screen_state)
{
    options_panel_draw_background(nullptr);
    const auto* const state = static_cast<KeymapScreenState*>(screen_state);
    if (state != nullptr &&
        draw_picture_in_band(state->picture.picture, kKeymapBandX,
                             kKeymapBandY, kKeymapBandW, kKeymapBandH))
        return;
    TRACE("keymap", "KEYMAP: picture unavailable");
    og::runtime::current_session->myscreen_->text_normal.write_xy_center(
        160, kKeymapBandY + kKeymapBandH / 2 - 3, DARK_BLUE, "%s",
        kKeymapNoPictureLine);
}

void keymap_draw_content(void* screen_state)
{
    const auto* const state = static_cast<KeymapScreenState*>(screen_state);
    if (state == nullptr)
        return;
    screen* const scr = og::runtime::current_session->myscreen_;
    scr->text_normal.write_xy_center(160, kKeymapTitleY, DARK_BLUE, "%s",
                                     state->title.c_str());
    int y = kKeymapLegendY;
    for (const std::string& line : state->legend) {
        scr->text_normal.write_xy(kKeymapLegendX, y, DARK_BLUE, "%s",
                                  line.c_str());
        y += kKeymapLegendPitch;
    }
}

bool keymap_pause_frame_tick(void* screen_state, int /*frame*/)
{
    auto* const state = static_cast<KeymapScreenState*>(screen_state);
    if (state == nullptr || state->host == nullptr ||
        state->host->pump == nullptr)
        return true;
    if (!state->host->pump(state->host->pump_context)) {
        state->session_ended = true;
        return false;
    }
    return true;
}

}  // namespace

bool keymap_door_shown(int seat)
{
    return !og::input::seat_owner_is_screen(
        og::input::is_single_seat_device(),
        current_input_selection(seat).is_joystick, seat);
}

std::string keymap_title(const std::vector<KeymapSeatView>& seats)
{
    std::string title = "KEYS:";
    for (const KeymapSeatView& seat : seats)
        title += (title.size() > 5 ? "  P" : " P") +
                 std::to_string(seat.player_number) + " " + upper(seat.owner);
    return cut(title);
}

std::vector<std::string> keymap_legend_lines(
    const std::vector<KeymapSeatView>& seats,
    const keyboard_picture::Output& picture, bool web,
    std::string (*key_name)(int keycode))
{
    std::vector<std::string> lines;
    for (const KeymapSeatView& seat : seats)
        if (seat.on_pad)
            lines.push_back("P" + std::to_string(seat.player_number) +
                            ": GAMEPAD (" + upper(seat.owner) +
                            "), NOT ON THE PICTURE");
    for (const keyboard_picture::SharedCap& shared : picture.shared) {
        std::string line;
        for (const keyboard_picture::CapUse& use : shared.uses) {
            if (!line.empty())
                line += " + ";
            line += use.player_number == 0
                ? std::string(use.action)
                : "P" + std::to_string(use.player_number) + " " + use.action;
        }
        lines.push_back(line + ": " + upper(shared.cap->id));
    }
    for (const keyboard_picture::OffPicture& off : picture.off_picture)
        lines.push_back("P" + std::to_string(off.player_number) + " " +
                        off.action + ": " +
                        upper(key_name != nullptr
                                  ? key_name(off.keycode)
                                  : "KEY " + std::to_string(off.keycode)));
    if (web) {
        bool uses_delete = false;
        for (const keyboard_picture::DrawnCap& drawn : picture.drawn)
            if (drawn.cap->keycode == KEYCODE_DELETE &&
                !drawn.player_numbers.empty())
                uses_delete = true;
        if (uses_delete)
            lines.push_back(kKeymapMacDeleteLine);
    }
    if (lines.size() > kKeymapLegendLines) {
        const std::size_t hidden = lines.size() - (kKeymapLegendLines - 1);
        lines.resize(kKeymapLegendLines - 1);
        lines.push_back("AND " + std::to_string(hidden) + " MORE ON THE KEY LIST");
    }
    for (std::string& line : lines)
        line = cut(std::move(line));
    return lines;
}

const MenuScreenSpec& keymap_menu_screen_spec()
{
    static const MenuScreenSpec spec{
        .name = "keymap",
        .rows = kKeymapRows,
        .row_count = static_cast<int>(std::size(kKeymapRows)),
        .buttons_accessor = &picker_keymap_buttons,
        .count_accessor = &picker_keymap_button_count,
        // Opened from the Base Camp seat screen: a joiner parked here follows
        // the host's GO like every team-build screen.
        .remote_start = RemoteStartScope::TeamBuildScope,
        .remote_start_exit = RemoteStartExit::ReturnMenuExit,
        .polls_lobby = true,
        .draw_background = &keymap_draw_background,
        .draw_content = &keymap_draw_content,
        .exit_value = MENU_REDRAW,
    };
    return spec;
}

const MenuScreenSpec& keymap_pause_menu_screen_spec()
{
    static const MenuScreenSpec spec{
        .name = "keymap_pause",
        .rows = kKeymapRows,
        .row_count = static_cast<int>(std::size(kKeymapRows)),
        .buttons_accessor = &picker_keymap_buttons,
        .count_accessor = &picker_keymap_button_count,
        .remote_start = RemoteStartScope::None,
        // Pause family: a mid-mission modal card, never faded (#237).
        .kind = MenuScreenKind::Overlay,
        .polls_lobby = false,
        .draw_background = &keymap_draw_background,
        .draw_content = &keymap_draw_content,
        .frame_tick = &keymap_pause_frame_tick,
        .exit_value = MENU_REDRAW,
    };
    return spec;
}

Sint32 run_keymap_screen(int seat, const KeymapScreenHost& host,
                         bool* session_ended)
{
    std::vector<KeymapSeat> seats = host.seats;
    const auto opening = std::find_if(
        seats.begin(), seats.end(),
        [seat](const KeymapSeat& s) { return s.seat == seat; });
    if (opening == seats.end())
        seats.insert(seats.begin(), KeymapSeat{seat, seat + 1});
    else
        std::rotate(seats.begin(), opening, opening + 1);

    KeymapScreenState state;
    state.host = &host;
    std::vector<KeymapSeatView> views;
    keyboard_picture::Input input;
    input.web = og::input::kWebControlDefaults;
    input.ability_select = og::input::ability_select_on();
    for (const KeymapSeat& s : seats) {
        views.push_back(describe_seat(s));
        input.seats.push_back(picture_seat(s));
    }
    state.title = keymap_title(views);
    state.picture = keyboard_picture::render(input);
    state.legend =
        keymap_legend_lines(views, state.picture, input.web, &native_key_name);
    TRACE("keymap", "KEYMAP: open seat=%d seats=%d shared=%d off=%d", seat,
          static_cast<int>(seats.size()),
          static_cast<int>(state.picture.shared.size()),
          static_cast<int>(state.picture.off_picture.size()));

    KeymapScreenState* const previous = g_keymap;
    g_keymap = &state;
    const Sint32 result =
        run_menu_screen(host.pump != nullptr ? keymap_pause_menu_screen_spec()
                                             : keymap_menu_screen_spec(),
                        &state);
    g_keymap = previous;
    if (session_ended != nullptr)
        *session_ended = state.session_ended;
    TRACE("keymap", "KEYMAP: closed seat=%d result=%d", seat,
          static_cast<int>(result));
    return result;
}

std::string keymap_screen_title()
{
    return g_keymap != nullptr ? g_keymap->title : std::string();
}

std::vector<std::string> keymap_screen_legend()
{
    return g_keymap != nullptr ? g_keymap->legend : std::vector<std::string>{};
}

const keyboard_picture::Output* keymap_screen_picture()
{
    return g_keymap != nullptr ? &g_keymap->picture : nullptr;
}

}  // namespace og::ui

button* picker_keymap_buttons()
{
    og::ui::materialize_menu_buttons(og::ui::keymap_menu_screen_spec(),
                                     og::ui::keymap_buttons());
    return og::ui::keymap_buttons().data();
}

int picker_keymap_button_count()
{
    return static_cast<int>(og::ui::keymap_buttons().size());
}
