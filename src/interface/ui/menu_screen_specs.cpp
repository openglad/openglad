/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
/* Engine-hosted screen specs (docs/menu-engine.md, design §1.3), transcribed
 * row-for-row from the legacy k_* tables — which are DELETED from picker.cpp
 * in the same commit that migrates each screen. The D3 accessors live here
 * as materialization shims: same signatures, same "buttons() fills what
 * count() reads" contract, same PickerState vector lifetime (cleared in
 * picker_cleanup_resources). test_menu_layout's hand-owned kExpected tables
 * remain the independent oracle over this transcription (G11).
 */

#include <openglad/interface/ui/menu_screen_spec.h>

#include <openglad/core/irandom.h>
#include <openglad/core/test_trace.h>
#include <openglad/interface/base.h>
#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/sound.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/resources/campaign_metadata.h>
#include <openglad/resources/company.h>
#include <openglad/resources/gparser.h>

#include "picker_sdl_defs.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <ctime>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Shared FX-face draw helpers and picker loop helpers (defined in picker.cpp
// / picker_team_build.cpp; declared locally by every consumer — repo
// pattern).
void draw_toggle_effect_button(button& b, const std::string& category,
                               const std::string& setting);
void draw_cycle_effect_button(button& b, const std::string& category,
                              const std::string& setting);
void draw_sprite_sheet_button(button& b);
void draw_version_number();
void sync_button_hidden_state(const button* buttons, int button_index);
void ensure_highlighted_button_visible(const button* buttons, int num_buttons,
                                       int& highlighted_button);
void sync_view_team_host_control_visibility(button* buttons, int num_buttons,
                                            int& highlighted_button);
void sync_scenario_menu_host_control_visibility(button* buttons,
                                                int num_buttons,
                                                int& highlighted_button);
void sync_team_build_host_control_visibility(button* buttons, int num_buttons,
                                             int& highlighted_button);
void view_team(short left, short top, short right, short bottom);
std::string get_saved_name(const char* filename);
bool team_build_start_selected();
// TEAMS engine hooks (defined beside their file-local helpers in
// picker_team_build.cpp).
void picker_teams_menu_engine_reset_open_state();
void picker_teams_menu_engine_rewire(button* buttons, int num_buttons,
                                     int& highlighted_button);
bool picker_teams_menu_engine_frame_tick(void* screen_state, int frame);
void picker_teams_menu_engine_draw_background(void* screen_state);
void picker_teams_menu_engine_draw_content(void* screen_state);
// HIRE / TRAIN engine hooks (§1.8 step 6; defined beside their file-local
// helpers — sessions, show_guy, the stat-panel painters — in
// picker_team_build.cpp).
void picker_hire_menu_engine_prepare_buttons(button* buttons, int num_buttons,
                                             void* screen_state);
void picker_hire_menu_engine_rewire(button* buttons, int num_buttons,
                                    int& highlighted_button);
bool picker_hire_menu_engine_frame_tick(void* screen_state, int frame);
void picker_hire_menu_engine_on_reset(void* screen_state);
void picker_hire_menu_engine_draw_content(void* screen_state);
void picker_train_menu_engine_rewire(button* buttons, int num_buttons,
                                     int& highlighted_button);
void picker_train_menu_engine_on_reset(void* screen_state);
void picker_train_menu_engine_draw_content(void* screen_state);
// PROGRESS / VIEW LEVEL engine hooks (§1.8 step 6; defined beside their
// file-local helpers in picker_team_build.cpp).
void picker_progress_menu_engine_draw_background(void* screen_state);
bool picker_progress_menu_engine_frame_tick(void* screen_state, int frame);
void picker_progress_menu_engine_draw_content(void* screen_state);
og::ui::RowState picker_view_scenario_engine_pager_row_state(
    const og::ui::MenuLabelContext& context);
void picker_view_scenario_engine_rewire(button* buttons, int num_buttons,
                                        int& highlighted_button);
Sint32 picker_view_scenario_engine_consume_click(Sint32 retvalue,
                                                 void* screen_state);
void picker_view_scenario_engine_draw_content(void* screen_state);

static inline PickerState& pks()
{
    return *og::runtime::current_session->picker_;
}

namespace og::ui {

namespace {

// ---------------------------------------------------------------------------
// DIFFICULTY subscreen (the main-menu DIFFICULTY door; §1.8 step 2, the
// first engine-hosted screen).
//
// One centered 140px column (23-char label budget at 6px/char; the widest
// label, "Difficulty: Slaughter", is 21) on the FX subscreen row pitch; nav
// is a vertical cycle through BACK. Static labels are the default-state
// formatter outputs; every settings row re-derives from session/save each
// frame (an open lobby can rewrite the save under the open menu) — the
// LabelBindings below, written to BOTH surfaces by the runner. BACK id is
// unique ("difficulty_back") because injector flows disambiguate screens by
// button id; the cycling rows keep their menu-model ids ("difficulty" is
// shared with the main-menu door, which is never live at the same time).
//
// Every settings row is LobbySettings-backed (difficulty included), so a
// non-host joiner sees only BACK: the HostOnly gates mirror
// sync_difficulty_menu_visibility, which stays plugged verbatim as the
// screen's Rewire nav program (G1) and also rewires BACK's vertical cycle.

std::string difficulty_row_label(const MenuLabelContext& context)
{
    return format_difficulty_label(context.session_difficulty);
}

std::string respawn_mode_row_label(const MenuLabelContext& context)
{
    return context.save != nullptr ? format_respawn_mode_label(*context.save)
                                   : std::string("Respawns: Off");
}

std::string respawn_delay_row_label(const MenuLabelContext& context)
{
    return context.save != nullptr
        ? format_respawn_delay_label(*context.save)
        : std::string("Spawn Delay: Normal");
}

std::string permadeath_row_label(const MenuLabelContext& context)
{
    return context.save != nullptr ? format_permadeath_label(*context.save)
                                   : std::string("Permadeath: On");
}

std::string generator_rate_row_label(const MenuLabelContext& context)
{
    return context.save != nullptr
        ? format_generator_rate_label(*context.save)
        : std::string("Generators: Normal");
}

constexpr GateBinding kHostOnlyGate{.gate = MenuGate::HostOnly};

constexpr MenuButtonSpec kDifficultyRows[] = {
    {.id = "difficulty_back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 10, .y = 10, .w = 50, .h = 15,
     .action = ButtonAction::ReturnMenu, .arg = MENU_EXIT,
     .nav = {.up = 5, .down = 1}},
    {.id = "difficulty", .label = "Difficulty: Battle",
     .x = 90, .y = 35, .w = 140, .h = 15,
     .action = ButtonAction::SetDifficulty, .arg = -1,
     .nav = {.up = 0, .down = 2},
     .label_binding = {.formatter = &difficulty_row_label},
     .gate = kHostOnlyGate},
    {.id = "respawn_mode", .label = "Respawns: Off",
     .x = 90, .y = 58, .w = 140, .h = 15,
     .action = ButtonAction::CycleRespawnMode, .arg = -1,
     .nav = {.up = 1, .down = 3},
     .label_binding = {.formatter = &respawn_mode_row_label},
     .gate = kHostOnlyGate},
    {.id = "respawn_delay", .label = "Spawn Delay: Normal",
     .x = 90, .y = 81, .w = 140, .h = 15,
     .action = ButtonAction::CycleRespawnDelay, .arg = -1,
     .nav = {.up = 2, .down = 4},
     .label_binding = {.formatter = &respawn_delay_row_label},
     .gate = kHostOnlyGate},
    {.id = "permadeath", .label = "Permadeath: On",
     .x = 90, .y = 104, .w = 140, .h = 15,
     .action = ButtonAction::TogglePermadeath, .arg = -1,
     .nav = {.up = 3, .down = 5},
     .label_binding = {.formatter = &permadeath_row_label},
     .gate = kHostOnlyGate},
    {.id = "generator_rate", .label = "Generators: Normal",
     .x = 90, .y = 127, .w = 140, .h = 15,
     .action = ButtonAction::CycleGeneratorRate, .arg = -1,
     .nav = {.up = 4, .down = 0},
     .label_binding = {.formatter = &generator_rate_row_label},
     .gate = kHostOnlyGate},
};

// The options-family panel chrome, shared by DIFFICULTY and every options
// subscreen: full-window clear (the overscan may have been adjusted), the
// black frame, the inverted bevel.
void options_panel_draw_background(void* /*screen_state*/)
{
    screen* scr = og::runtime::current_session->myscreen_;
    scr->clear_window();
    scr->draw_button(0, 0, 320, 200, 0);
    scr->draw_button_inverted(4, 4, 312, 192);
}

void difficulty_draw_content(void* /*screen_state*/)
{
    og::runtime::current_session->myscreen_->text_normal.write_xy(
        80, 13, DARK_BLUE, "%s", "DIFFICULTY");
}

// ---------------------------------------------------------------------------
// The three FX subscreens (§1.8 step 3): toggle grid rows transcribed
// VERBATIM from the deleted k_*_fx_options_buttons tables. Columns
// x=15/115/215, 90px faces (15-char label budget at 6px/char), rows at 23px
// pitch from y=35 (bottoms inside the 4..196 bevel). All toggle button ids
// and (category, setting) cfg pairs are unchanged from the single pre-split
// EFFECTS screen. Each BACK id is unique (gameplay_fx_back / ui_fx_back /
// graphics_fx_back) because injector flows disambiguate screens by button
// id. Toggles only write cfg; main_options() persists cfg when it exits,
// which is the only path back out of these screens. The legacy loops had NO
// remote-start check (a joiner parked here launches on the next screen
// exit), so RemoteStartScope stays None — 10a fidelity, pinned by the
// engine-test allowlist.

inline constexpr Sint32 fx_row_y(int row) { return 35 + row * 23; }

// One per-frame FX subscreen draw entry: which toggle button reflects which
// cfg (category, setting) pair. A cycle entry's setting is a value string
// (the depth selector) rather than an on/off flag.
struct FxToggleDraw
{
    int index;
    const char* category;
    const char* setting;
    bool cycle = false;
};

// Shared content pass for the three FX subscreens: title text plus the
// green/red state faces drawn over the bevels (the draw reads each
// descriptor row's geometry and label).
void fx_options_draw_content(const char* title,
                             std::span<const FxToggleDraw> toggles,
                             std::vector<button>& rows)
{
    og::runtime::current_session->myscreen_->text_normal.write_xy(
        80, 13, DARK_BLUE, "%s", title);

    for (const FxToggleDraw& toggle : toggles)
    {
        if (toggle.cycle)
            draw_cycle_effect_button(rows[static_cast<std::size_t>(toggle.index)],
                                     toggle.category, toggle.setting);
        else
            draw_toggle_effect_button(rows[static_cast<std::size_t>(toggle.index)],
                                      toggle.category, toggle.setting);
    }
}

// GAMEPLAY FX: toggles that change how the game feels to play. Single
// centered column; nav is a vertical cycle through BACK.
constexpr MenuButtonSpec kGameplayFxRows[] = {
    {.id = "gameplay_fx_back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 10, .y = 10, .w = 50, .h = 15,
     .action = ButtonAction::ReturnMenu, .arg = MENU_EXIT,
     .nav = {.up = 2, .down = 1}},
    {.id = "toggle_hit_recoil", .label = "Hit recoil",
     .x = 115, .y = fx_row_y(0), .w = 90, .h = 15,
     .action = ButtonAction::ToggleHitRecoil, .arg = -1,
     .nav = {.up = 0, .down = 2}},
    {.id = "toggle_attack_lunge", .label = "Attack lunge",
     .x = 115, .y = fx_row_y(1), .w = 90, .h = 15,
     .action = ButtonAction::ToggleAttackLunge, .arg = -1,
     .nav = {.up = 1, .down = 0}},
};

constexpr FxToggleDraw kGameplayFxToggleDraws[] = {
    {1, "effects", "hit_recoil"},
    {2, "effects", "attack_lunge"},
};

void gameplay_fx_draw_content(void* /*screen_state*/)
{
    fx_options_draw_content("Gameplay effects", kGameplayFxToggleDraws,
                            pks().gameplay_fx_options_buttons);
}

// UI FX: informational overlays. Same single-column idiom as GAMEPLAY FX.
constexpr MenuButtonSpec kUiFxRows[] = {
    {.id = "ui_fx_back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 10, .y = 10, .w = 50, .h = 15,
     .action = ButtonAction::ReturnMenu, .arg = MENU_EXIT,
     .nav = {.up = 3, .down = 1}},
    {.id = "toggle_mini_hp_bar", .label = "Mini HP bar",
     .x = 115, .y = fx_row_y(0), .w = 90, .h = 15,
     .action = ButtonAction::ToggleMiniHpBar, .arg = -1,
     .nav = {.up = 0, .down = 2}},
    {.id = "toggle_damage_numbers", .label = "Damage numbers",
     .x = 115, .y = fx_row_y(1), .w = 90, .h = 15,
     .action = ButtonAction::ToggleDamageNumbers, .arg = -1,
     .nav = {.up = 1, .down = 3}},
    {.id = "toggle_heal_numbers", .label = "Healing numbers",
     .x = 115, .y = fx_row_y(2), .w = 90, .h = 15,
     .action = ButtonAction::ToggleHealNumbers, .arg = -1,
     .nav = {.up = 2, .down = 0}},
};

constexpr FxToggleDraw kUiFxToggleDraws[] = {
    {1, "effects", "mini_hp_bar"},
    {2, "effects", "damage_numbers"},
    {3, "effects", "heal_numbers"},
};

void ui_fx_draw_content(void* /*screen_state*/)
{
    fx_options_draw_content("Interface effects", kUiFxToggleDraws,
                            pks().ui_fx_options_buttons);
}

// GRAPHICS FX: purely visual effects. 13 toggles on the 3-column grid:
// 4 full rows plus a single-button fifth row (floor glide, the
// generator_rate lone-row idiom). Weather (cfg effects/weather) is the
// client-side display opt-out for the per-level sim weather (the old
// Clouds/Rain pair merged). Rows wrap left/right; the top row's up and the
// bottom row's down land on BACK; BACK's up wraps to the bottom toggle.
// The depth row's label is cfg-derived ("Depth: Fog" ... "Depth: Off") —
// the LabelBinding below replaces both the legacy pre-entry descriptor
// write and change_depth_fx()'s click-side label writes (G8).

std::string depth_fx_row_label(const MenuLabelContext& /*context*/)
{
    return format_depth_fx_label(cfg.get_setting("effects", "depth_fx"));
}

constexpr MenuButtonSpec kGraphicsFxRows[] = {
    {.id = "graphics_fx_back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 10, .y = 10, .w = 50, .h = 15,
     .action = ButtonAction::ReturnMenu, .arg = MENU_EXIT,
     .nav = {.up = 13, .down = 1}},
    {.id = "toggle_hit_flash", .label = "Hit flash",
     .x = 15, .y = fx_row_y(0), .w = 90, .h = 15,
     .action = ButtonAction::ToggleHitFlash, .arg = -1,
     .nav = {.up = 0, .down = 4, .left = 3, .right = 2}},
    {.id = "toggle_hit_sparks", .label = "Hit sparks",
     .x = 115, .y = fx_row_y(0), .w = 90, .h = 15,
     .action = ButtonAction::ToggleHitAnim, .arg = -1,
     .nav = {.up = 0, .down = 5, .left = 1, .right = 3}},
    {.id = "toggle_gore", .label = "Gore",
     .x = 215, .y = fx_row_y(0), .w = 90, .h = 15,
     .action = ButtonAction::ToggleGore, .arg = -1,
     .nav = {.up = 0, .down = 6, .left = 2, .right = 1}},
    {.id = "toggle_shadows", .label = "Shadows",
     .x = 15, .y = fx_row_y(1), .w = 90, .h = 15,
     .action = ButtonAction::ToggleShadows, .arg = -1,
     .nav = {.up = 1, .down = 7, .left = 6, .right = 5}},
    {.id = "toggle_reflections", .label = "Reflections",
     .x = 115, .y = fx_row_y(1), .w = 90, .h = 15,
     .action = ButtonAction::ToggleReflections, .arg = -1,
     .nav = {.up = 2, .down = 8, .left = 4, .right = 6}},
    {.id = "toggle_weather", .label = "Weather",
     .x = 215, .y = fx_row_y(1), .w = 90, .h = 15,
     .action = ButtonAction::ToggleWeather, .arg = -1,
     .nav = {.up = 3, .down = 9, .left = 5, .right = 4}},
    {.id = "toggle_dust", .label = "Dust",
     .x = 15, .y = fx_row_y(2), .w = 90, .h = 15,
     .action = ButtonAction::ToggleDust, .arg = -1,
     .nav = {.up = 4, .down = 10, .left = 9, .right = 8}},
    {.id = "depth_fx", .label = "Depth: Fog",
     .x = 115, .y = fx_row_y(2), .w = 90, .h = 15,
     .action = ButtonAction::CycleDepthFx, .arg = -1,
     .nav = {.up = 5, .down = 11, .left = 7, .right = 9},
     .label_binding = {.formatter = &depth_fx_row_label}},
    {.id = "toggle_trails", .label = "Trails",
     .x = 215, .y = fx_row_y(2), .w = 90, .h = 15,
     .action = ButtonAction::ToggleTrails, .arg = -1,
     .nav = {.up = 6, .down = 12, .left = 8, .right = 7}},
    {.id = "toggle_fire_glow", .label = "Fire glow",
     .x = 15, .y = fx_row_y(3), .w = 90, .h = 15,
     .action = ButtonAction::ToggleFireGlow, .arg = -1,
     .nav = {.up = 7, .down = 13, .left = 12, .right = 11}},
    {.id = "toggle_ripples", .label = "Ripples",
     .x = 115, .y = fx_row_y(3), .w = 90, .h = 15,
     .action = ButtonAction::ToggleRipples, .arg = -1,
     .nav = {.up = 8, .down = 13, .left = 10, .right = 12}},
    {.id = "toggle_screen_shake", .label = "Screen shake",
     .x = 215, .y = fx_row_y(3), .w = 90, .h = 15,
     .action = ButtonAction::ToggleScreenShake, .arg = -1,
     .nav = {.up = 9, .down = 13, .left = 11, .right = 10}},
    {.id = "toggle_floor_glide", .label = "Floor glide",
     .x = 15, .y = fx_row_y(4), .w = 90, .h = 15,
     .action = ButtonAction::ToggleFloorGlide, .arg = -1,
     .nav = {.up = 10, .down = 0}},
};

constexpr FxToggleDraw kGraphicsFxToggleDraws[] = {
    {1, "effects", "hit_flash"},
    {2, "effects", "hit_anim"},
    {3, "effects", "gore"},
    {4, "effects", "shadows"},
    {5, "effects", "reflections"},
    {6, "effects", "weather"},
    {7, "effects", "dust"},
    {8, "effects", "depth_fx", true},
    {9, "effects", "trails"},
    {10, "effects", "fire_glow"},
    {11, "effects", "ripples"},
    {12, "effects", "screen_shake"},
    {13, "effects", "floor_glide"},
};

void graphics_fx_draw_content(void* /*screen_state*/)
{
    fx_options_draw_content("Graphics effects", kGraphicsFxToggleDraws,
                            pks().graphics_fx_options_buttons);
}

// The shared FX-subscreen spec shape: cold entry, lobby kept alive, static
// verbatim nav (no gated rows), MENU_REDRAW back into main_options().
constexpr MenuScreenSpec make_fx_options_spec(const char* name,
                                              const MenuButtonSpec* rows,
                                              int row_count,
                                              button* (*buttons_accessor)(),
                                              int (*count_accessor)(),
                                              void (*draw_content)(void*))
{
    MenuScreenSpec spec{};
    spec.name = name;
    spec.rows = rows;
    spec.row_count = row_count;
    spec.buttons_accessor = buttons_accessor;
    spec.count_accessor = count_accessor;
    spec.polls_lobby = true;
    spec.draw_background = &options_panel_draw_background;
    spec.draw_content = draw_content;
    spec.exit_value = MENU_REDRAW;
    return spec;
}

const MenuScreenSpec& gameplay_fx_menu_screen_spec()
{
    static const MenuScreenSpec spec = make_fx_options_spec(
        "gameplay_fx", kGameplayFxRows,
        static_cast<int>(std::size(kGameplayFxRows)),
        &picker_gameplay_fx_options_buttons,
        &picker_gameplay_fx_options_button_count, &gameplay_fx_draw_content);
    return spec;
}

const MenuScreenSpec& ui_fx_menu_screen_spec()
{
    static const MenuScreenSpec spec = make_fx_options_spec(
        "ui_fx", kUiFxRows, static_cast<int>(std::size(kUiFxRows)),
        &picker_ui_fx_options_buttons, &picker_ui_fx_options_button_count,
        &ui_fx_draw_content);
    return spec;
}

const MenuScreenSpec& graphics_fx_menu_screen_spec()
{
    static const MenuScreenSpec spec = make_fx_options_spec(
        "graphics_fx", kGraphicsFxRows,
        static_cast<int>(std::size(kGraphicsFxRows)),
        &picker_graphics_fx_options_buttons,
        &picker_graphics_fx_options_button_count, &graphics_fx_draw_content);
    return spec;
}

// ---------------------------------------------------------------------------
// DISPLAY subscreen (§1.8 step 3): the window/presentation settings a normal
// game keeps together — mode (windowed / borderless / exclusive fullscreen),
// a real WxH resolution that sizes the window when windowed and picks the
// closest exclusive video mode when fullscreen, the overscan trim, the
// sprite/world scale, and the present filter. 102px faces (17-char label
// budget) so "Mode: Borderless" and "Res: 2560x1440" fit. Rows on the
// effects grid. Every face is cfg-derived: the LabelBindings re-derive all
// four each frame on both surfaces (the click callbacks used to write them
// too — those tails are gone, G8 — and RESTORE DEFAULTS or a lobby-applied
// cfg must reflect here regardless). cfg persists when main_options()
// exits, which is the only path back out.

// Borderless always uses the desktop mode; showing the remembered window
// size here made a 640x400 label describe a 1920x1080 screen.
std::string active_resolution_label()
{
    screen* scr = og::runtime::current_session->myscreen_;
    if (parse_display_mode(cfg.get_setting("graphics", "fullscreen")) ==
        DisplayMode::Borderless)
    {
        const std::pair<int, int> desktop = scr->desktop_resolution();
        if (desktop.first >= 320 && desktop.second >= 200)
            return format_resolution_label(
                std::to_string(desktop.first), std::to_string(desktop.second));
        return "Res: Desktop";
    }
    return format_resolution_label(
        cfg.get_setting("graphics", "width"),
        cfg.get_setting("graphics", "height"));
}

// SDL may reject a requested mode/zoom and normalize cfg to the real state
// (the platform apply reflects rejections back), so every formatter reads
// cfg fresh instead of echoing the last request.
std::string display_mode_row_label(const MenuLabelContext& /*context*/)
{
    return format_display_mode_label(cfg.get_setting("graphics", "fullscreen"));
}

std::string display_resolution_row_label(const MenuLabelContext& /*context*/)
{
    return active_resolution_label();
}

std::string display_zoom_row_label(const MenuLabelContext& /*context*/)
{
    return format_zoom_label(cfg.get_setting("graphics", "zoom"));
}

std::string display_smoothing_row_label(const MenuLabelContext& /*context*/)
{
    return format_smoothing_label(
        effective_smoothing_setting(cfg.get_setting("graphics", "smoothing"),
                                    cfg.get_setting("graphics", "render")),
        og::runtime::current_session->myscreen_->world_smoothing_supported());
}

constexpr MenuButtonSpec kDisplaySettingsRows[] = {
    {.id = "display_back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 10, .y = 10, .w = 50, .h = 15,
     .action = ButtonAction::ReturnMenu, .arg = MENU_EXIT,
     .nav = {.up = 6, .down = 1}},
    {.id = "display_mode", .label = "Mode: Windowed",
     .x = 115, .y = fx_row_y(0), .w = 102, .h = 15,
     .action = ButtonAction::CycleDisplayMode, .arg = -1,
     .nav = {.up = 0, .down = 2},
     .label_binding = {.formatter = &display_mode_row_label}},
    {.id = "display_resolution", .label = "Res: 640x400",
     .x = 115, .y = fx_row_y(1), .w = 102, .h = 15,
     .action = ButtonAction::CycleResolution, .arg = -1,
     .nav = {.up = 1, .down = 3},
     .label_binding = {.formatter = &display_resolution_row_label}},
    {.id = "overscan_minus", .label = "- ",
     .x = 115, .y = fx_row_y(2), .w = 30, .h = 15,
     .action = ButtonAction::OverscanAdjust, .arg = -1,
     .nav = {.up = 2, .down = 5, .right = 4}},
    {.id = "overscan_plus", .label = "+ ",
     .x = 159, .y = fx_row_y(2), .w = 30, .h = 15,
     .action = ButtonAction::OverscanAdjust, .arg = 1,
     .nav = {.up = 2, .down = 5, .left = 3}},
    {.id = "display_zoom", .label = "Zoom: 1.0x",
     .x = 115, .y = fx_row_y(3), .w = 102, .h = 15,
     .action = ButtonAction::CycleZoom, .arg = -1,
     .nav = {.up = 3, .down = 6},
     .label_binding = {.formatter = &display_zoom_row_label}},
    {.id = "display_smoothing", .label = "Smooth: Off",
     .x = 115, .y = fx_row_y(4), .w = 102, .h = 15,
     .action = ButtonAction::CycleSmoothing, .arg = -1,
     .nav = {.up = 5, .down = 0},
     .label_binding = {.formatter = &display_smoothing_row_label}},
};

// No window to size or mode to pick: TV/mobile targets are always
// fullscreen, and on web the page/CSS owns the window (the fullscreen cfg
// is also deliberately ignored at boot there). Hide both rows and route the
// vertical cycle around them (BACK <-> overscan pair). Compile-time
// platform fork, applied per frame (idempotent) as the screen's Rewire
// program; a desktop-native build is a no-op with the verbatim static nav.
void display_settings_platform_rewire(button* buttons, int num_buttons,
                                      int& highlighted_button)
{
#if defined(OUYA) || defined(ANDROID) || defined(__IPHONEOS__) || \
    defined(SDL_PLATFORM_IOS) || defined(__EMSCRIPTEN__)
    if (buttons == nullptr || num_buttons <= kDisplayMenuSmoothingIndex)
        return;
    buttons[kDisplayMenuModeIndex].hidden =
        buttons[kDisplayMenuModeIndex].no_draw = true;
    buttons[kDisplayMenuResolutionIndex].hidden =
        buttons[kDisplayMenuResolutionIndex].no_draw = true;
    sync_button_hidden_state(buttons, kDisplayMenuModeIndex);
    sync_button_hidden_state(buttons, kDisplayMenuResolutionIndex);
    buttons[kDisplayMenuBackIndex].nav.down = kDisplayMenuOverscanMinusIndex;
    buttons[kDisplayMenuOverscanMinusIndex].nav.up = kDisplayMenuBackIndex;
    buttons[kDisplayMenuOverscanPlusIndex].nav.up = kDisplayMenuBackIndex;
    ensure_highlighted_button_visible(buttons, num_buttons,
                                      highlighted_button);
#else
    (void)buttons;
    (void)num_buttons;
    (void)highlighted_button;
#endif
}

void display_settings_draw_content(void* /*screen_state*/)
{
    text& mytext = og::runtime::current_session->myscreen_->text_normal;
    mytext.write_xy(80, 13, DARK_BLUE, "%s", "Display");
    // Live overscan percentage next to its +/- pair.
    mytext.write_xy(200, fx_row_y(2) + 4, DARK_BLUE, "Overscan: %d%%",
                    static_cast<int>(
                        og::runtime::current_session->overscan_percentage_ *
                            100.0f + 0.5f));
}

const MenuScreenSpec& display_settings_menu_screen_spec()
{
    static const MenuScreenSpec spec{
        .name = "display_settings",
        .rows = kDisplaySettingsRows,
        .row_count = static_cast<int>(std::size(kDisplaySettingsRows)),
        .buttons_accessor = &picker_display_settings_buttons,
        .count_accessor = &picker_display_settings_button_count,
        .nav = {.kind = NavProgramKind::Rewire,
                .rewire = &display_settings_platform_rewire},
        .polls_lobby = true,
        .draw_background = &options_panel_draw_background,
        .draw_content = &display_settings_draw_content,
        .exit_value = MENU_REDRAW,
    };
    return spec;
}

// ---------------------------------------------------------------------------
// CONTROLS subscreen (§1.8 step 3): 4 player sections at 28px pitch, each
// with mode + remap buttons; the "Px" captions and key-summary lines are
// drawn text below each section's buttons. The mode faces re-derive from
// the live control mode every frame on both surfaces (LabelBindings — the
// legacy loop's per-frame writes).

inline constexpr Sint32 ctrl_player_y(int player) { return 40 + player * 28; }

template <int PlayerIndex>
std::string player_mode_row_label(const MenuLabelContext& /*context*/)
{
    const bool eight_dir = get_player_control_mode(PlayerIndex) ==
        static_cast<int>(ControlDirectionMode::EightDirection);
    return eight_dir ? "8-DIRECTION" : "4-DIRECTION";
}

constexpr MenuButtonSpec kControlOptionsRows[] = {
    {.id = "controls_back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 10, .y = 8, .w = 50, .h = 15,
     .action = ButtonAction::ReturnMenu, .arg = MENU_EXIT,
     .nav = {.down = 1}},
    {.id = "player1_mode", .label = "4-DIRECTION",
     .x = 30, .y = ctrl_player_y(0), .w = 100, .h = 15,
     .action = ButtonAction::ToggleControlMode, .arg = 0,
     .nav = {.up = 0, .down = 3, .right = 2},
     .label_binding = {.formatter = &player_mode_row_label<0>}},
    {.id = "player1_remap", .label = "REMAP P1",
     .x = 170, .y = ctrl_player_y(0), .w = 100, .h = 15,
     .action = ButtonAction::EditPlayerKeymap, .arg = 0,
     .nav = {.up = 0, .down = 4, .left = 1}},
    {.id = "player2_mode", .label = "4-DIRECTION",
     .x = 30, .y = ctrl_player_y(1), .w = 100, .h = 15,
     .action = ButtonAction::ToggleControlMode, .arg = 1,
     .nav = {.up = 1, .down = 5, .right = 4},
     .label_binding = {.formatter = &player_mode_row_label<1>}},
    {.id = "player2_remap", .label = "REMAP P2",
     .x = 170, .y = ctrl_player_y(1), .w = 100, .h = 15,
     .action = ButtonAction::EditPlayerKeymap, .arg = 1,
     .nav = {.up = 2, .down = 6, .left = 3}},
    {.id = "player3_mode", .label = "4-DIRECTION",
     .x = 30, .y = ctrl_player_y(2), .w = 100, .h = 15,
     .action = ButtonAction::ToggleControlMode, .arg = 2,
     .nav = {.up = 3, .down = 7, .right = 6},
     .label_binding = {.formatter = &player_mode_row_label<2>}},
    {.id = "player3_remap", .label = "REMAP P3",
     .x = 170, .y = ctrl_player_y(2), .w = 100, .h = 15,
     .action = ButtonAction::EditPlayerKeymap, .arg = 2,
     .nav = {.up = 4, .down = 8, .left = 5}},
    {.id = "player4_mode", .label = "4-DIRECTION",
     .x = 30, .y = ctrl_player_y(3), .w = 100, .h = 15,
     .action = ButtonAction::ToggleControlMode, .arg = 3,
     .nav = {.up = 5, .down = 9, .right = 8},
     .label_binding = {.formatter = &player_mode_row_label<3>}},
    {.id = "player4_remap", .label = "REMAP P4",
     .x = 170, .y = ctrl_player_y(3), .w = 100, .h = 15,
     .action = ButtonAction::EditPlayerKeymap, .arg = 3,
     .nav = {.up = 6, .down = 9, .left = 7}},
    {.id = "controls_restore_defaults", .label = "RESET DEFAULTS",
     .x = 80, .y = 170, .w = 160, .h = 15,
     .action = ButtonAction::RestoreDefaultControls, .arg = -1,
     .nav = {.up = 7}},
};

void control_options_draw_content(void* /*screen_state*/)
{
    text& mytext = og::runtime::current_session->myscreen_->text_normal;

    // The header sits below the BACK button's animated highlight box
    // (which reaches 3px past the bevel) and above the P1 row at y=40;
    // drawing it any higher lets the highlight overwrite the first chars.
    mytext.write_xy(PICKER_CONTROLS_HEADER_X, PICKER_CONTROLS_HEADER_Y,
                    DARK_BLUE, "Player control modes and key remapping");

    for (int i = 0; i < 4; ++i)
    {
        const Sint32 btn_y = ctrl_player_y(i);
        mytext.write_xy(10, btn_y + 3, DARK_BLUE, "P%d", i + 1);
        const std::string summary = build_player_control_summary(i);
        mytext.write_xy(30, btn_y + 17, DARK_BLUE, "%s", summary.c_str());
    }

    mytext.write_xy(10, 155, DARK_BLUE,
                    "4-dir = cardinal only. 8-dir adds diagonals.");
}

const MenuScreenSpec& control_options_menu_screen_spec()
{
    static const MenuScreenSpec spec{
        .name = "control_options",
        .rows = kControlOptionsRows,
        .row_count = static_cast<int>(std::size(kControlOptionsRows)),
        .buttons_accessor = &picker_control_options_buttons,
        .count_accessor = &picker_control_options_button_count,
        .polls_lobby = true,
        .draw_background = &options_panel_draw_background,
        .draw_content = &control_options_draw_content,
        .exit_value = MENU_REDRAW,
    };
    return spec;
}

// ---------------------------------------------------------------------------
// MAIN OPTIONS (§1.8 step 3, last slice): sound/graphics settings plus doors
// into the CONTROLS screen and the three FX subscreens (GAMEPLAY FX / UI FX /
// GRAPHICS FX). Rows transcribed VERBATIM from the deleted
// k_main_options_buttons table: the settings/door column stacks at the
// classic 23px pitch (BUTTON_HEIGHT 15 + 8 padding). The sound face and the
// sprite-sheet face are content-pass state draws over the bevels (green per
// cfg), not label bindings. Every subscreen door just opens its blocking
// screen; the exit epilogue in main_options() below is the single point
// where the whole family's cfg edits persist.

inline constexpr Sint32 kOptionsButtonPadding = 8;
inline constexpr Sint32 kOptionsButtonPitch = 15 + kOptionsButtonPadding;
inline constexpr Sint32 options_col_y(int row)
{
    return 10 + row * kOptionsButtonPitch;
}

// The legacy loop re-wrote "Sprite Sheet" to BOTH surfaces every frame
// (after reset_buttons — the pick-spritesheet subscreen swaps allbuttons_
// under this screen and a MENU_REDRAW re-init restores it from the
// descriptor row). A fixed binding reproduces that dual-surface restore at
// the same point in the frame.
std::string sprite_sheet_row_label(const MenuLabelContext& /*context*/)
{
    return "Sprite Sheet";
}

constexpr MenuButtonSpec kMainOptionsRows[] = {
    {.id = "options_back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 10, .y = 10, .w = 50, .h = 15,
     .action = ButtonAction::ReturnMenu, .arg = MENU_EXIT,
     .nav = {.up = 8, .down = 1, .right = 5}},
    {.id = "toggle_sound", .label = "Sound",
     .x = 135, .y = options_col_y(1), .w = 50, .h = 15,
     .action = ButtonAction::ToggleSound, .arg = -1,
     .nav = {.up = 0, .down = 2, .right = 6}},
    // Door into the DISPLAY subscreen (mode / resolution / overscan /
    // scaling / filter live there).
    {.id = "display_settings", .label = "DISPLAY",
     .x = 130, .y = options_col_y(2), .w = 90, .h = 15,
     .action = ButtonAction::OpenDisplaySettings, .arg = -1,
     .nav = {.up = 1, .down = 3, .right = 6}},
    {.id = "gameplay_fx", .label = "GAMEPLAY FX",
     .x = 130, .y = options_col_y(3), .w = 90, .h = 15,
     .action = ButtonAction::OpenGameplayFxSettings, .arg = -1,
     .nav = {.up = 2, .down = 7}},
    {.id = "restore_defaults", .label = "RESTORE DEFAULTS",
     .x = 210, .y = 10, .w = 100, .h = 15,
     .action = ButtonAction::RestoreDefaultSettings, .arg = -1,
     .nav = {.up = 8, .down = 6, .left = 5}},
    {.id = "player_controls", .label = "CONTROLS",
     .x = 100, .y = 10, .w = 80, .h = 15,
     .action = ButtonAction::OpenControlSettings, .arg = -1,
     .nav = {.up = 8, .down = 1, .left = 0, .right = 4}},
    {.id = "pick_sprite_sheet", .label = "Sprite Sheet",
     .x = 210, .y = options_col_y(1), .w = 90, .h = 15,
     .action = ButtonAction::PickSpriteSheet, .arg = 0,
     .nav = {.up = 4, .down = 2, .left = 1},
     .label_binding = {.formatter = &sprite_sheet_row_label}},
    {.id = "ui_fx", .label = "UI FX",
     .x = 130, .y = options_col_y(4), .w = 90, .h = 15,
     .action = ButtonAction::OpenUiFxSettings, .arg = -1,
     .nav = {.up = 3, .down = 8}},
    {.id = "graphics_fx", .label = "GRAPHICS FX",
     .x = 130, .y = options_col_y(5), .w = 90, .h = 15,
     .action = ButtonAction::OpenGraphicsFxSettings, .arg = -1,
     .nav = {.up = 7, .down = 0}},
};

void main_options_draw_content(void* /*screen_state*/)
{
    screen* scr = og::runtime::current_session->myscreen_;
    std::vector<button>& rows = pks().main_options_buttons;

    draw_toggle_effect_button(rows[1], "sound", "sound");
    // Section rule between the sound row and the DISPLAY/effects doors.
    scr->hor_line(60, rows[2].y - kOptionsButtonPadding / 2, 200, PURE_WHITE);
    scr->text_normal.write_xy(20, rows[2].y + 3, DARK_BLUE, "Display:");
    scr->text_normal.write_xy(20, rows[3].y + 3, DARK_BLUE, "Effects:");
    draw_sprite_sheet_button(rows[6]);
}

const MenuScreenSpec& main_options_menu_screen_spec()
{
    static const MenuScreenSpec spec{
        .name = "main_options",
        .rows = kMainOptionsRows,
        .row_count = static_cast<int>(std::size(kMainOptionsRows)),
        .buttons_accessor = &picker_main_options_buttons,
        .count_accessor = &picker_main_options_button_count,
        .polls_lobby = true,
        .draw_background = &options_panel_draw_background,
        .draw_content = &main_options_draw_content,
        .exit_value = MENU_REDRAW,
    };
    return spec;
}

// ---------------------------------------------------------------------------
// MAIN MENU (§1.8 step 4, the heaviest 10a screen). The four legacy
// k_mainmenu_buttons build variants (picker.cpp, deleted with this
// migration) unify into TWO specs — MP and no-MP (the no-MP variant
// genuinely repositions rows into a single column and moves the bottom row
// to y=175) — each carrying the web/native fork as the build-gated
// quit/help row pair: identical geometry and nav, different id/label/
// hotkey/action, exactly one surviving materialization. Nav links are
// written in MATERIALIZED index space and are identical across the fork
// (§1.6: 2 static nav columns cover all 4 variants).
//
// redraw_mainmenu's raw allbuttons_[N] writes became bindings (§1.6):
//   [2..5] player-count outlines -> OutlineBinding::PlayerCountEquals,
//   [7] SPECTATOR/PVP label      -> the pvp_allied LabelBinding,
//   [0]/[options] pixie faces    -> art_family rows (FAMILY_NORMAL1 /
//       FAMILY_WRENCH — the normal1.png Begin New Game face is preserved by
//       construction: same empty label, same set_graphic path, re-applied
//       after init_buttons and after every reset_buttons).
// Its title/columns drawMix + the FULL re-vdisplay-after-title pass + the
// native version stamp survive as the draw_content hook (G14): the 136x58
// title frames at (15,8)/(151,8) overlap begin_new_game (80,50,140,20) in
// y=50-66, and legacy re-vdisplayed EVERY button after the title drawMix —
// the runner's draw_buttons -> content order would invert that overlap
// without the full re-vdisplay here.
//
// Both OPTIONS_BUTTON_INDEX #defines (picker.cpp x4 and the
// picker_main_menu.cpp pair) are gone — picker_mainmenu_options_index()
// derives the index from the materialized spec.

// The legacy USE_TOUCH_INPUT => DISABLE_MULTIPLAYER mapping, kept at the
// variant-selection point (it lived beside the k_mainmenu tables in
// picker.cpp:1484-1486 until they were deleted with this migration).
#ifdef USE_TOUCH_INPUT
#ifndef DISABLE_MULTIPLAYER
#define DISABLE_MULTIPLAYER
#endif
#endif

// Company & Base Camp (design §2.1): CONTINUE and LOAD are gated on the
// existence of at least one company file, and the main menu shows the
// active/most-recent company name in a black caption strip. list_companies()
// touches the filesystem, so the view is cached and refreshed exactly once
// per mainmenu() entry — the company set is stable while the blocking loop
// runs (Begin New Game and the Load list both exit the loop first). The gate,
// nav rewire, and caption all read this cache; the default (companies
// present) keeps the headless engine sweeps deterministic without any
// filesystem, and a test seam pins the no-company shape.
struct MainMenuCompanyView {
    bool present = true;
    std::string display_name;
};
MainMenuCompanyView g_main_menu_company_view;

// The generator's hard cap (design §2.2): a real display name never exceeds
// this, so the caption clip never truncates one.
constexpr std::size_t kMainMenuCompanyNameClip = 18;

// Caption strip Y per §2.1: y=171 with the MP split, y=166 without (the no-MP
// variant's bottom row sits one notch higher). The compiled build selects the
// variant, so the constant follows it.
#ifdef DISABLE_MULTIPLAYER
constexpr int kMainMenuCaptionY = 166;
#else
constexpr int kMainMenuCaptionY = 171;
#endif

// §2.1 gate: CONTINUE and LOAD are hidden when no company file exists.
bool main_menu_company_present(const MenuLabelContext& /*context*/)
{
    return g_main_menu_company_view.present;
}

int main_menu_row_index(const button* buttons, int count, std::string_view id)
{
    for (int i = 0; i < count; ++i) {
        if (buttons[i].id == id)
            return i;
    }
    return -1;
}

// §2.1 nav rewire: CONTINUE (index 1) and LOAD (the appended row) share the
// company gate, so the graph routes around them when no company exists. The
// static graph is re-asserted when they are present, keeping the rewire
// idempotent regardless of a prior frame's state. Links are resolved by id so
// the one function serves both the MP and no-MP variants (no 2_player /
// player row in the no-MP graph). Verbatim design links: begin.down -> the
// row below the pair (1_player in MP, difficulty in no-MP); the player/
// difficulty up-links point back at begin while the pair is hidden.
void main_menu_nav_rewire(button* buttons, int count, int& /*highlighted*/)
{
    const int i_begin = main_menu_row_index(buttons, count, "begin_new_game");
    const int i_continue = main_menu_row_index(buttons, count, "continue_game");
    const int i_load = main_menu_row_index(buttons, count, "load_company");
    const int i_1p = main_menu_row_index(buttons, count, "1_player");
    const int i_2p = main_menu_row_index(buttons, count, "2_player");
    const int i_diff = main_menu_row_index(buttons, count, "difficulty");
    const bool nomp = (i_2p < 0);              // no player grid => no-MP variant
    const int i_below = nomp ? i_diff : i_1p;  // the row beneath the split pair

    if (g_main_menu_company_view.present) {
        if (i_begin >= 0) buttons[i_begin].nav.down = i_continue;
        if (i_1p >= 0) buttons[i_1p].nav.up = i_continue;
        if (i_2p >= 0) buttons[i_2p].nav.up = i_load;
        if (nomp && i_diff >= 0) buttons[i_diff].nav.up = i_continue;
    } else {
        if (i_begin >= 0) buttons[i_begin].nav.down = i_below;
        if (i_1p >= 0) buttons[i_1p].nav.up = i_begin;
        if (i_2p >= 0) buttons[i_2p].nav.up = i_begin;
        if (nomp && i_diff >= 0) buttons[i_diff].nav.up = i_begin;
    }
}

// Live label for the pvp_allied row: redraw_mainmenu's per-frame write,
// verbatim (SPECTATOR when no local player count is selected).
std::string pvp_allied_row_label(const MenuLabelContext& context)
{
    if (context.save == nullptr)
        return "PVP: Allied";
    if (context.save->numplayers == 0)
        return "SPECTATOR";
    return format_allied_mode_label(*context.save);
}

constexpr MenuButtonSpec kMainMenuRowsMP[] = {
    {.id = "begin_new_game", .label = "",
     .x = 80, .y = 50, .w = 140, .h = 20,
     .action = ButtonAction::BeginMenu, .arg = 1,
     .nav = {.down = 1},
     .art_family = FAMILY_NORMAL1},
    // §2.1: CONTINUE and LOAD split the old full-width continue_game into the
    // established side-by-side 68x20 player-button grammar. Both are gated on
    // company existence and appear/vanish together; load_company is appended
    // at the table END (index 11) so indices 0-10 — and the raw allbuttons_[N]
    // writes keyed off them — stay valid.
    {.id = "continue_game", .label = "CONTINUE",
     .x = 80, .y = 75, .w = 68, .h = 20,
     .action = ButtonAction::CreateTeamMenu, .arg = -1,
     .nav = {.up = 0, .down = 5, .right = 11},
     .gate = {.gate = MenuGate::Custom, .custom = &main_menu_company_present}},
    {.id = "4_player", .label = "4 PLAYER", .hotkey = KEYSTATE_4,
     .x = 152, .y = 125, .w = 68, .h = 20,
     .action = ButtonAction::SetPlayerMode, .arg = 4,
     .nav = {.up = 4, .down = 6, .left = 3},
     .outline = MenuOutlineBinding::PlayerCountEquals, .outline_arg = 4},
    {.id = "3_player", .label = "3 PLAYER", .hotkey = KEYSTATE_3,
     .x = 80, .y = 125, .w = 68, .h = 20,
     .action = ButtonAction::SetPlayerMode, .arg = 3,
     .nav = {.up = 5, .down = 6, .right = 2},
     .outline = MenuOutlineBinding::PlayerCountEquals, .outline_arg = 3},
    // up -> load_company (index 11), the right-column row directly above.
    {.id = "2_player", .label = "2 PLAYER", .hotkey = KEYSTATE_2,
     .x = 152, .y = 100, .w = 68, .h = 20,
     .action = ButtonAction::SetPlayerMode, .arg = 2,
     .nav = {.up = 11, .down = 2, .left = 5},
     .outline = MenuOutlineBinding::PlayerCountEquals, .outline_arg = 2},
    {.id = "1_player", .label = "1 PLAYER", .hotkey = KEYSTATE_1,
     .x = 80, .y = 100, .w = 68, .h = 20,
     .action = ButtonAction::SetPlayerMode, .arg = 1,
     .nav = {.up = 1, .down = 3, .right = 4},
     .outline = MenuOutlineBinding::PlayerCountEquals, .outline_arg = 1},
    {.id = "difficulty", .label = "DIFFICULTY",
     .x = 80, .y = 148, .w = 140, .h = 10,
     .action = ButtonAction::OpenDifficultyMenu, .arg = -1,
     .nav = {.up = 3, .down = 7}},
    {.id = "pvp_allied", .label = "PVP: Allied",
     .x = 80, .y = 160, .w = 68, .h = 10,
     .action = ButtonAction::AlliedMode, .arg = -1,
     .nav = {.up = 6, .down = 9, .right = 8},
     .label_binding = {.formatter = &pvp_allied_row_label}},
    {.id = "level_edit", .label = "Level Edit",
     .x = 152, .y = 160, .w = 68, .h = 10,
     .action = ButtonAction::DoLevelEdit, .arg = -1,
     .nav = {.up = 6, .down = 10, .left = 7}},
    // The web/native fork (§1.6): one of this pair survives materialization,
    // always at index 9 with identical geometry and nav. The trailing space
    // in "QUIT " is part of the shipped label.
    {.id = "quit", .label = "QUIT ", .hotkey = KEYSTATE_ESCAPE,
     .x = 120, .y = 182, .w = 60, .h = 15,
     .action = ButtonAction::QuitMenu, .arg = 0,
     .nav = {.up = 7, .left = 10},
     .build = MenuBuildGate::NativeOnly},
    {.id = "help", .label = "HELP",
     .x = 120, .y = 182, .w = 60, .h = 15,
     .action = ButtonAction::ShowHelp, .arg = -1,
     .nav = {.up = 7, .left = 10},
     .build = MenuBuildGate::WebOnly},
    {.id = "options", .label = "",
     .x = 90, .y = 182, .w = 20, .h = 15,
     .action = ButtonAction::MainOptions, .arg = -1,
     .nav = {.up = 8, .right = 9},
     .art_family = FAMILY_WRENCH},
    // §2.1 index 11 (appended at the table END): the LOAD half of the split.
    // Opens the load flow (Company List — the legacy load-slots screen until
    // WP3's Company List screen lands). Gated on company existence with
    // CONTINUE.
    {.id = "load_company", .label = "LOAD",
     .x = 152, .y = 75, .w = 68, .h = 20,
     .action = ButtonAction::CreateLoadMenu, .arg = 0,
     .nav = {.up = 0, .down = 4, .left = 1},
     .gate = {.gate = MenuGate::Custom, .custom = &main_menu_company_present}},
};

constexpr MenuButtonSpec kMainMenuRowsNoMP[] = {
    {.id = "begin_new_game", .label = "",
     .x = 80, .y = 50, .w = 140, .h = 20,
     .action = ButtonAction::BeginMenu, .arg = 1,
     .nav = {.down = 1},
     .art_family = FAMILY_NORMAL1},
    // §2.1 no-MP split: same 68x20 CONTINUE | LOAD pair at y=75.
    {.id = "continue_game", .label = "CONTINUE",
     .x = 80, .y = 75, .w = 68, .h = 20,
     .action = ButtonAction::CreateTeamMenu, .arg = -1,
     .nav = {.up = 0, .down = 2, .right = 6},
     .gate = {.gate = MenuGate::Custom, .custom = &main_menu_company_present}},
    {.id = "difficulty", .label = "DIFFICULTY",
     .x = 80, .y = 100, .w = 140, .h = 15,
     .action = ButtonAction::OpenDifficultyMenu, .arg = -1,
     .nav = {.up = 1, .down = 3}},
    {.id = "level_edit", .label = "Level Edit",
     .x = 80, .y = 118, .w = 140, .h = 15,
     .action = ButtonAction::DoLevelEdit, .arg = -1,
     .nav = {.up = 2, .down = 4}},
    // The web/native fork — always index 4 here.
    {.id = "quit", .label = "QUIT ", .hotkey = KEYSTATE_ESCAPE,
     .x = 120, .y = 175, .w = 60, .h = 15,
     .action = ButtonAction::QuitMenu, .arg = 0,
     .nav = {.up = 3, .left = 5},
     .build = MenuBuildGate::NativeOnly},
    {.id = "help", .label = "HELP",
     .x = 120, .y = 175, .w = 60, .h = 15,
     .action = ButtonAction::ShowHelp, .arg = -1,
     .nav = {.up = 3, .left = 5},
     .build = MenuBuildGate::WebOnly},
    {.id = "options", .label = "",
     .x = 90, .y = 175, .w = 20, .h = 15,
     .action = ButtonAction::MainOptions, .arg = -1,
     .nav = {.up = 3, .right = 4},
     .art_family = FAMILY_WRENCH},
    // §2.1 index 6 (appended at the table END): the no-MP LOAD half.
    {.id = "load_company", .label = "LOAD",
     .x = 152, .y = 75, .w = 68, .h = 20,
     .action = ButtonAction::CreateLoadMenu, .arg = 0,
     .nav = {.up = 0, .down = 2, .left = 1},
     .gate = {.gate = MenuGate::Custom, .custom = &main_menu_company_present}},
};

void main_menu_draw_background(void* /*screen_state*/)
{
    og::runtime::current_session->myscreen_->clearbuffer();
}

// The surviving body of redraw_mainmenu (picker_main_menu.cpp, deleted with
// this migration): title + columns, then the G14 FULL re-vdisplay pass, then
// the native version stamp. The outline/label/set_graphic writes it carried
// are bindings now (see the spec comment above); the per-frame set_graphic
// re-applies were redundant (set_graphic draws nothing — the art survives
// on the live vbutton until a reset, which the runner re-applies after).
// The pixie null-guards only matter to headless engine tests that run the
// screen without picker_initialize_shared_menu_state; production always has
// them loaded.
void main_menu_draw_content(void* /*screen_state*/)
{
    screen* game = og::runtime::current_session->myscreen_;

    if (pks().main_title_logo_pix) {
        pks().main_title_logo_pix->set_frame(0);
        pks().main_title_logo_pix->drawMix(15, 8, game->viewob[0].get());
        pks().main_title_logo_pix->set_frame(1);
        pks().main_title_logo_pix->drawMix(151, 8, game->viewob[0].get());
    }
    if (pks().main_columns_pix) {
        pks().main_columns_pix->set_frame(0);
        pks().main_columns_pix->drawMix(12, 40, game->viewob[0].get());
        pks().main_columns_pix->set_frame(1);
        pks().main_columns_pix->drawMix(242, 40, game->viewob[0].get());
    }

    // G14: re-vdisplay EVERY button after the title drawMix (the title
    // frames overlap begin_new_game; buttons must win that overlap).
    int count = 0;
    while (count < static_cast<int>(og::runtime::current_session->allbuttons_.size())
           && og::runtime::current_session->allbuttons_[count]) {
        og::runtime::current_session->allbuttons_[count]->vdisplay();
        count++;
    }

    // §2.1 company caption: a black strip (the SCEN-hint idiom) naming the
    // active/most-recent company CONTINUE would open, or the new-game prompt
    // when none exists yet. Drawn after the re-vdisplay so it wins the strip's
    // own y-band.
    {
        std::string caption;
        if (g_main_menu_company_view.present) {
            std::string name = g_main_menu_company_view.display_name;
            if (name.size() > kMainMenuCompanyNameClip)
                name.resize(kMainMenuCompanyNameClip);
            caption = "COMPANY: " + name;
            if (caption.size() > 28)
                caption.resize(28);
        } else {
            caption = "NO COMPANY YET - BEGIN A NEW GAME";
        }
        const int width = static_cast<int>(caption.size()) * 6;
        const int caption_x = 160 - width / 2;
        game->draw_rect_filled(caption_x - 2, kMainMenuCaptionY - 1, width + 4, 8,
                               PURE_BLACK, 150);
        game->text_normal.write_xy(caption_x, kMainMenuCaptionY, WHITE, "%s",
                                   caption.c_str());
    }

    // On native builds, show the version number on the main menu. On
    // Emscripten/web builds the version is displayed elsewhere (the help UI).
#ifndef __EMSCRIPTEN__
    draw_version_number();
#endif
}

// ---------------------------------------------------------------------------
// VIEW TEAM (§1.8 step 5): the roster listing with GO | BACK. Rows
// transcribed VERBATIM from the deleted k_viewteam_buttons. BACK carries
// MENU_REDRAW (the parent create_team_menu keeps running) — exit_on_redraw;
// GO propagates MENU_EXIT (an intercepted GO maps to StartGame). The
// legacy loop cleared the buffer around the screen (the wrapper keeps
// that) and drew cold — no fade.

constexpr MenuButtonSpec kViewTeamRows[] = {
    {.id = "go", .label = "GO",
     .x = 270, .y = 170, .w = 40, .h = 20,
     .action = ButtonAction::GoMenu, .arg = -1,
     .nav = {.left = 1}},
    {.id = "back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 10, .y = 170, .w = 44, .h = 20,
     .action = ButtonAction::ReturnMenu, .arg = MENU_REDRAW,
     .nav = {.right = 0}},
};

// The classic picker frame: clear, then the tiled backdrop (the shape every
// team-build-family loop drew per frame).
void picker_backdrop_draw_background(void* /*screen_state*/)
{
    og::runtime::current_session->myscreen_->clearbuffer();
    draw_backdrop();
}

// G1: the legacy per-frame sync (GO host-gating) plugged verbatim, plus the
// nav closure the engine invariant requires: legacy left BACK's right-link
// pointing at the hidden GO — a no-op in handle_menu_nav (it refuses hidden
// targets) — so writing the explicit no-op (-1) is behavior-identical.
void view_team_rewire(button* buttons, int num_buttons,
                      int& highlighted_button)
{
    sync_view_team_host_control_visibility(buttons, num_buttons,
                                           highlighted_button);
    if (num_buttons > 1)
        buttons[1].nav.right = buttons[0].hidden ? -1 : 0;
}

void view_team_draw_content(void* /*screen_state*/)
{
    view_team(5, 5, 314, 160);
}

const MenuScreenSpec& view_team_menu_screen_spec()
{
    static const MenuScreenSpec spec{
        .name = "view_team",
        .rows = kViewTeamRows,
        .row_count = static_cast<int>(std::size(kViewTeamRows)),
        .buttons_accessor = &picker_viewteam_buttons,
        .count_accessor = &picker_viewteam_button_count,
        .nav = {.kind = NavProgramKind::Rewire, .rewire = &view_team_rewire},
        // A host GO must launch a joiner parked here (the legacy loop's
        // team_build_remote_start_requested check).
        .remote_start = RemoteStartScope::TeamBuildScope,
        .remote_start_exit = RemoteStartExit::ReturnMenuExit,
        .default_highlight = 1,  // back, as the legacy loop
        .exit_on_redraw = true,
        .polls_lobby = true,
        .draw_background = &picker_backdrop_draw_background,
        .draw_content = &view_team_draw_content,
        // MENU_EXIT-bearing exits propagate so TeamBuild interception can
        // map GO -> StartGame; BACK's redraw-break returns MENU_REDRAW.
        .exit_value = MENU_EXIT,
    };
    return spec;
}

// ---------------------------------------------------------------------------
// SAVE / LOAD slot menus (§1.8 step 5): ten 220x10 slot rows at 15px pitch
// from y=25 plus BACK, one vertical nav cycle — transcribed VERBATIM from
// the deleted k_saveteam_buttons / k_loadteam_buttons (including the shipped
// mixed-case "SLOT Six".."SLOT Ten" static labels). The static labels stay
// on the descriptor rows; the content pass overwrites the LIVE labels from
// the slot headers every frame (get_saved_name), exactly as the legacy
// create_slot_menu drew. A slot action (DoSave/DoLoad) returns MENU_REDRAW
// — the screen exits back to team build (exit_on_redraw).

constexpr MenuButtonSpec kSaveSlotsRows[] = {
    {.id = "save_slot_1", .label = "SLOT ONE", .x = 25, .y = 25, .w = 220,
     .h = 10, .action = ButtonAction::DoSave, .arg = 1,
     .nav = {.up = 10, .down = 1}},
    {.id = "save_slot_2", .label = "SLOT TWO", .x = 25, .y = 40, .w = 220,
     .h = 10, .action = ButtonAction::DoSave, .arg = 2,
     .nav = {.up = 0, .down = 2}},
    {.id = "save_slot_3", .label = "SLOT THREE", .x = 25, .y = 55, .w = 220,
     .h = 10, .action = ButtonAction::DoSave, .arg = 3,
     .nav = {.up = 1, .down = 3}},
    {.id = "save_slot_4", .label = "SLOT FOUR", .x = 25, .y = 70, .w = 220,
     .h = 10, .action = ButtonAction::DoSave, .arg = 4,
     .nav = {.up = 2, .down = 4}},
    {.id = "save_slot_5", .label = "SLOT FIVE", .x = 25, .y = 85, .w = 220,
     .h = 10, .action = ButtonAction::DoSave, .arg = 5,
     .nav = {.up = 3, .down = 5}},
    {.id = "save_slot_6", .label = "SLOT Six", .x = 25, .y = 100, .w = 220,
     .h = 10, .action = ButtonAction::DoSave, .arg = 6,
     .nav = {.up = 4, .down = 6}},
    {.id = "save_slot_7", .label = "SLOT Seven", .x = 25, .y = 115, .w = 220,
     .h = 10, .action = ButtonAction::DoSave, .arg = 7,
     .nav = {.up = 5, .down = 7}},
    {.id = "save_slot_8", .label = "SLOT Eight", .x = 25, .y = 130, .w = 220,
     .h = 10, .action = ButtonAction::DoSave, .arg = 8,
     .nav = {.up = 6, .down = 8}},
    {.id = "save_slot_9", .label = "SLOT Nine", .x = 25, .y = 145, .w = 220,
     .h = 10, .action = ButtonAction::DoSave, .arg = 9,
     .nav = {.up = 7, .down = 9}},
    {.id = "save_slot_10", .label = "SLOT Ten", .x = 25, .y = 160, .w = 220,
     .h = 10, .action = ButtonAction::DoSave, .arg = 10,
     .nav = {.up = 8, .down = 10}},
    {.id = "back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 25, .y = 175, .w = 40, .h = 20,
     .action = ButtonAction::ReturnMenu, .arg = MENU_EXIT,
     .nav = {.up = 9, .down = 0}},
};

constexpr MenuButtonSpec kLoadSlotsRows[] = {
    {.id = "load_slot_1", .label = "SLOT ONE", .x = 25, .y = 25, .w = 220,
     .h = 10, .action = ButtonAction::DoLoad, .arg = 1,
     .nav = {.up = 10, .down = 1}},
    {.id = "load_slot_2", .label = "SLOT TWO", .x = 25, .y = 40, .w = 220,
     .h = 10, .action = ButtonAction::DoLoad, .arg = 2,
     .nav = {.up = 0, .down = 2}},
    {.id = "load_slot_3", .label = "SLOT THREE", .x = 25, .y = 55, .w = 220,
     .h = 10, .action = ButtonAction::DoLoad, .arg = 3,
     .nav = {.up = 1, .down = 3}},
    {.id = "load_slot_4", .label = "SLOT FOUR", .x = 25, .y = 70, .w = 220,
     .h = 10, .action = ButtonAction::DoLoad, .arg = 4,
     .nav = {.up = 2, .down = 4}},
    {.id = "load_slot_5", .label = "SLOT FIVE", .x = 25, .y = 85, .w = 220,
     .h = 10, .action = ButtonAction::DoLoad, .arg = 5,
     .nav = {.up = 3, .down = 5}},
    {.id = "load_slot_6", .label = "SLOT Six", .x = 25, .y = 100, .w = 220,
     .h = 10, .action = ButtonAction::DoLoad, .arg = 6,
     .nav = {.up = 4, .down = 6}},
    {.id = "load_slot_7", .label = "SLOT Seven", .x = 25, .y = 115, .w = 220,
     .h = 10, .action = ButtonAction::DoLoad, .arg = 7,
     .nav = {.up = 5, .down = 7}},
    {.id = "load_slot_8", .label = "SLOT Eight", .x = 25, .y = 130, .w = 220,
     .h = 10, .action = ButtonAction::DoLoad, .arg = 8,
     .nav = {.up = 6, .down = 8}},
    {.id = "load_slot_9", .label = "SLOT Nine", .x = 25, .y = 145, .w = 220,
     .h = 10, .action = ButtonAction::DoLoad, .arg = 9,
     .nav = {.up = 7, .down = 9}},
    {.id = "load_slot_10", .label = "SLOT Ten", .x = 25, .y = 160, .w = 220,
     .h = 10, .action = ButtonAction::DoLoad, .arg = 10,
     .nav = {.up = 8, .down = 10}},
    {.id = "back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 25, .y = 175, .w = 40, .h = 20,
     .action = ButtonAction::ReturnMenu, .arg = MENU_EXIT,
     .nav = {.up = 9, .down = 0}},
};

// The legacy create_slot_menu draw, verbatim: panel frame + title bar over
// the buttons draw_buttons already painted (the frame covers them), then
// the ten slot rows re-labeled LIVE from the slot headers and re-vdisplayed
// with their text bars and boxes, then BACK's bar/box.
void slot_menu_draw_content(const char* title)
{
    screen* const scr = og::runtime::current_session->myscreen_;
    text& menutext = scr->text_normal;
    auto& allbuttons = og::runtime::current_session->allbuttons_;

    scr->draw_button(15, 9, 255, 199, 1, 1);
    scr->draw_text_bar(19, 13, 251, 21);
    const int title_len = static_cast<int>(std::strlen(title));
    menutext.write_xy(135 - (title_len * 3), 15, title, RED, 1);
    for (Sint32 i = 0; i < 10; i++)
    {
        std::string temp_filename = std::format("save{}", i + 1);
        allbuttons[i]->label = get_saved_name(temp_filename.c_str());
        scr->draw_text_bar(23, 23 + i * BUTTON_HEIGHT, 246,
                           36 + BUTTON_HEIGHT * i);
        allbuttons[i]->vdisplay();
        scr->draw_box(allbuttons[i]->xloc - 1, allbuttons[i]->yloc - 1,
                      allbuttons[i]->xend, allbuttons[i]->yend, 0, 0, 1);
    }
    scr->draw_text_bar(23, allbuttons[10]->yloc - 2, 66,
                       allbuttons[10]->yend + 1);
    allbuttons[10]->vdisplay();
    scr->draw_box(allbuttons[10]->xloc - 1, allbuttons[10]->yloc - 1,
                  allbuttons[10]->xend, allbuttons[10]->yend, 0, 0, 1);
}

void save_slots_draw_content(void* /*screen_state*/)
{
    slot_menu_draw_content("Gladiator: Save Game");
}

void load_slots_draw_content(void* /*screen_state*/)
{
    slot_menu_draw_content("Gladiator: Load Game");
}

constexpr MenuScreenSpec make_slot_menu_spec(const char* name,
                                             const MenuButtonSpec* rows,
                                             int row_count,
                                             button* (*buttons_accessor)(),
                                             int (*count_accessor)(),
                                             void (*draw_content)(void*))
{
    MenuScreenSpec spec{};
    spec.name = name;
    spec.rows = rows;
    spec.row_count = row_count;
    spec.buttons_accessor = buttons_accessor;
    spec.count_accessor = count_accessor;
    // A host GO must launch a joiner parked in a slot menu (the legacy
    // loop's check). The remote MENU_EXIT propagates directly — the parent
    // team-build loop breaks with StartGame selected (legacy folded it to
    // MENU_REDRAW and re-detected the start one loop later; same launch).
    spec.remote_start = RemoteStartScope::TeamBuildScope;
    spec.remote_start_exit = RemoteStartExit::ReturnMenuExit;
    spec.default_highlight = 10;  // back, as the legacy loop
    spec.exit_on_redraw = true;
    spec.polls_lobby = true;
    spec.draw_background = &picker_backdrop_draw_background;
    spec.draw_content = draw_content;
    spec.exit_value = MENU_REDRAW;
    return spec;
}

const MenuScreenSpec& save_slots_menu_screen_spec()
{
    static const MenuScreenSpec spec = make_slot_menu_spec(
        "save_slots", kSaveSlotsRows,
        static_cast<int>(std::size(kSaveSlotsRows)), &picker_saveteam_buttons,
        &picker_saveteam_button_count, &save_slots_draw_content);
    return spec;
}

const MenuScreenSpec& load_slots_menu_screen_spec()
{
    static const MenuScreenSpec spec = make_slot_menu_spec(
        "load_slots", kLoadSlotsRows,
        static_cast<int>(std::size(kLoadSlotsRows)), &picker_loadteam_buttons,
        &picker_loadteam_button_count, &load_slots_draw_content);
    return spec;
}

// ---------------------------------------------------------------------------
// Level-reload guard (§1.4 frame_tick): the team-build family's per-frame
// obligation — a SET LEVEL pick, a host sync while parked, or a nested
// screen's reset must reload the picker world. screen_state carries one
// open screen's cursor; the wrapper initializes it (team build forces the
// entry reload with -1; SCENARIO starts at the current level).

struct LevelReloadGuardState
{
    short last_level_id = -1;
    bool was_reset = false;
};

void level_reload_guard_on_reset(void* screen_state)
{
    if (screen_state == nullptr)
        return;
    static_cast<LevelReloadGuardState*>(screen_state)->was_reset = true;
}

bool level_reload_guard_frame_tick(void* screen_state, int /*frame*/)
{
    // Null state = a caller that never reaches the guard in practice (the
    // G5 remote-start sweep exits before any frame tick); stay inert.
    if (screen_state == nullptr)
        return true;
    auto* const guard = static_cast<LevelReloadGuardState*>(screen_state);
    screen* const myscreen = og::runtime::current_session->myscreen_;
    if (guard->last_level_id != myscreen->save_data.scen_num ||
        guard->was_reset)
    {
        guard->was_reset = false;
        guard->last_level_id = myscreen->save_data.scen_num;
        myscreen->world().id = guard->last_level_id;
        myscreen->load_level();
    }
    return true;
}

// ---------------------------------------------------------------------------
// SCENARIO subscreen (§1.8 step 5): the column at x=30 stacks the host-gated
// SET CAMPAIGN / SET LEVEL (their name strips draw alongside) over the
// always-visible VIEW LEVEL | TEAMS | PROGRESS row; BACK sits apart at
// (30,170) so no other screen's "back" shares its geometry (injector tests
// disambiguate the per-screen "back" buttons by position). Rows transcribed
// VERBATIM from the deleted k_scenariomenu_buttons; static nav encodes the
// host variant and the legacy sync (hide + up-link rewire) is the spec's
// Rewire program (G1). Nested screens return MENU_REDRAW for reset_buttons
// to consume (exit_on_redraw stays FALSE); BACK carries MENU_EXIT and the
// create_scenario_menu wrapper folds it to MENU_REDRAW unless a start was
// selected.

constexpr MenuButtonSpec kScenarioMenuRows[] = {
    {.id = "back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 30, .y = 170, .w = 60, .h = 20,
     .action = ButtonAction::ReturnMenu, .arg = MENU_EXIT,
     .nav = {.up = 3}},
    {.id = "set_campaign", .label = "SET CAMPAIGN",
     .x = 30, .y = 40, .w = 80, .h = 15,
     .action = ButtonAction::DoPickCampaign, .arg = -1,
     .nav = {.down = 2}},
    {.id = "set_level", .label = "SET LEVEL",
     .x = 30, .y = 70, .w = 80, .h = 15,
     .action = ButtonAction::DoSetScenLevel, .arg = -1,
     .nav = {.up = 1, .down = 3}},
    {.id = "view_scenario", .label = "VIEW LEVEL",
     .x = 30, .y = 100, .w = 80, .h = 15,
     .action = ButtonAction::ViewScenario, .arg = -1,
     .nav = {.up = 2, .down = 0, .right = 4}},
    {.id = "teams", .label = "TEAMS",
     .x = 120, .y = 100, .w = 80, .h = 15,
     .action = ButtonAction::CreateTeamsMenu, .arg = -1,
     .nav = {.up = 2, .down = 0, .left = 3, .right = 5}},
    {.id = "progress", .label = "PROGRESS",
     .x = 210, .y = 100, .w = 80, .h = 15,
     .action = ButtonAction::CreateProgressMenu, .arg = -1,
     .nav = {.up = 2, .down = 0, .left = 4}},
};

// The campaign-name / level-title strips sit beside the buttons that change
// them (always drawn — joiners see the host's choices even while
// SET CAMPAIGN / SET LEVEL are hidden). Verbatim from the deleted loop.
void scenario_menu_draw_content(void* /*screen_state*/)
{
    screen* const myscreen = og::runtime::current_session->myscreen_;
    text& mytext = myscreen->text_normal;

    mytext.write_xy(10, 8, "SCENARIO", WHITE, 1);

    const auto draw_strip = [&mytext, myscreen](int y, std::string value) {
        if (value.size() > 32)
            value.resize(32);
        const int strip_w = static_cast<int>(value.size()) * 6;
        myscreen->draw_rect_filled(114, y - 1, strip_w + 4, 8, PURE_BLACK,
                                   150);
        mytext.write_xy(116, y, WHITE, "%s", value.c_str());
    };
    const std::vector<button>& rows = pks().scenariomenu_buttons;
    draw_strip(rows[kScenarioMenuSetCampaignIndex].y + 4,
               og::data::campaign_display_title(
                   myscreen->save_data.current_campaign));
    draw_strip(rows[kScenarioMenuSetLevelIndex].y + 4,
               std::format("SCEN {}: {}", myscreen->save_data.scen_num,
                           myscreen->world().title));
}

void scenario_menu_rewire(button* buttons, int num_buttons,
                          int& highlighted_button)
{
    sync_scenario_menu_host_control_visibility(buttons, num_buttons,
                                               highlighted_button);
}

const MenuScreenSpec& scenario_menu_screen_spec()
{
    static const MenuScreenSpec spec{
        .name = "scenario_menu",
        .rows = kScenarioMenuRows,
        .row_count = static_cast<int>(std::size(kScenarioMenuRows)),
        .buttons_accessor = &picker_scenariomenu_buttons,
        .count_accessor = &picker_scenariomenu_button_count,
        .nav = {.kind = NavProgramKind::Rewire,
                .rewire = &scenario_menu_rewire},
        // A joiner parked here still follows the host's GO.
        .remote_start = RemoteStartScope::TeamBuildScope,
        .remote_start_exit = RemoteStartExit::ReturnMenuExit,
        .default_highlight = kScenarioMenuBackIndex,
        .polls_lobby = true,
        .draw_background = &picker_backdrop_draw_background,
        .draw_content = &scenario_menu_draw_content,
        .frame_tick = &level_reload_guard_frame_tick,
        .on_reset = &level_reload_guard_on_reset,
        .exit_value = MENU_EXIT,
    };
    return spec;
}

// ---------------------------------------------------------------------------
// TEAMS subscreen (§1.8 step 5): team rows drawn at y=32+30*t with the
// per-team JOIN column at x=240; the CTF match settings (host-gated, CTF
// campaign only) at the top (Teams/Limit) and bottom-right (Troops); the
// local guy-cycling row at y=146; READY replaces the guy row when networked;
// per-team member pagers at the row bars' right edge. Rows transcribed
// VERBATIM from the deleted k_teamsmenu_buttons (static nav = the
// local-classic variant; conditional rows start hidden). Every frame the
// spec's Rewire program (picker_teams_menu_engine_rewire) replays the legacy
// compute + sync + trace: visibility for every conditional row, both label
// surfaces, the full picker_wire_teams_menu_nav rewire, and the highlight
// pull — the 9-variant BFS matrix in test_menu_layout stays the oracle.

constexpr MenuButtonSpec kTeamsMenuRows[] = {
    {.id = "back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 10, .y = 170, .w = 40, .h = 20,
     .action = ButtonAction::ReturnMenu, .arg = MENU_REDRAW,
     .nav = {.up = 7}},
    {.id = "ctf_teams", .label = "Teams: Auto",
     .x = 120, .y = 8, .w = 80, .h = 15,
     .action = ButtonAction::CycleCtfTeamCount, .arg = -1,
     .nav = {.down = 3, .right = 2},
     .hidden = true},
    {.id = "ctf_caps", .label = "Limit: Map",
     .x = 210, .y = 8, .w = 80, .h = 15,
     .action = ButtonAction::CycleCtfCaptureLimit, .arg = -1,
     .nav = {.down = 3, .left = 1},
     .hidden = true},
    {.id = "join_team_0", .label = "JOIN",
     .x = 240, .y = 32, .w = 50, .h = 12,
     .action = ButtonAction::JoinTeam, .arg = 0,
     .nav = {.down = 4}},
    {.id = "join_team_1", .label = "JOIN",
     .x = 240, .y = 62, .w = 50, .h = 12,
     .action = ButtonAction::JoinTeam, .arg = 1,
     .nav = {.up = 3, .down = 5}},
    {.id = "join_team_2", .label = "JOIN",
     .x = 240, .y = 92, .w = 50, .h = 12,
     .action = ButtonAction::JoinTeam, .arg = 2,
     .nav = {.up = 4, .down = 6}},
    {.id = "join_team_3", .label = "JOIN",
     .x = 240, .y = 122, .w = 50, .h = 12,
     .action = ButtonAction::JoinTeam, .arg = 3,
     .nav = {.up = 5, .down = 9}},
    {.id = "guy_prev", .label = "<",
     .x = 10, .y = 146, .w = 16, .h = 12,
     .action = ButtonAction::TeamsCycleGuy, .arg = -1,
     .nav = {.up = 3, .down = 0, .right = 8}},
    {.id = "guy_next", .label = ">",
     .x = 120, .y = 146, .w = 16, .h = 12,
     .action = ButtonAction::TeamsCycleGuy, .arg = 1,
     .nav = {.up = 3, .down = 0, .left = 7, .right = 9}},
    {.id = "guy_team", .label = "TEAM >",
     .x = 150, .y = 146, .w = 70, .h = 12,
     .action = ButtonAction::TeamsCycleGuyTeam, .arg = 1,
     .nav = {.up = 6, .down = 0, .left = 8}},
    {.id = "ready", .label = "READY",
     .x = 120, .y = 170, .w = 80, .h = 20,
     .action = ButtonAction::ToggleLobbyReady, .arg = -1,
     .nav = {.up = 6, .left = 0},
     .hidden = true},
    {.id = "ctf_troops", .label = "Troops: Scen",
     .x = 210, .y = 170, .w = 80, .h = 20,
     .action = ButtonAction::CycleCtfScenarioTroops, .arg = -1,
     .nav = {.up = 9, .left = 0},
     .hidden = true},
    // Per-team member pagers: a '>' at the right edge of each team row's
    // readability bar (8..234), left of the JOIN column at x=240. Hidden
    // unless that team's detail line needs more than one slice; nav is
    // fully rewired per frame like every other conditional button here.
    {.id = "team_page_0", .label = ">",
     .x = 219, .y = 39, .w = 14, .h = 12,
     .action = ButtonAction::TeamsPageFlip, .arg = 0,
     .hidden = true},
    {.id = "team_page_1", .label = ">",
     .x = 219, .y = 69, .w = 14, .h = 12,
     .action = ButtonAction::TeamsPageFlip, .arg = 1,
     .hidden = true},
    {.id = "team_page_2", .label = ">",
     .x = 219, .y = 99, .w = 14, .h = 12,
     .action = ButtonAction::TeamsPageFlip, .arg = 2,
     .hidden = true},
    {.id = "team_page_3", .label = ">",
     .x = 219, .y = 129, .w = 14, .h = 12,
     .action = ButtonAction::TeamsPageFlip, .arg = 3,
     .hidden = true},
};

const MenuScreenSpec& teams_menu_screen_spec()
{
    static const MenuScreenSpec spec{
        .name = "teams_menu",
        .rows = kTeamsMenuRows,
        .row_count = static_cast<int>(std::size(kTeamsMenuRows)),
        .buttons_accessor = &picker_teamsmenu_buttons,
        .count_accessor = &picker_teamsmenu_button_count,
        .nav = {.kind = NavProgramKind::Rewire,
                .rewire = &picker_teams_menu_engine_rewire},
        // A joiner parked here still follows the host's GO.
        .remote_start = RemoteStartScope::TeamBuildScope,
        .remote_start_exit = RemoteStartExit::ReturnMenuExit,
        .default_highlight = kTeamsMenuBackIndex,
        // BACK returns MENU_REDRAW to signal "go back to team menu" — end
        // the loop before reset_buttons could consume it (legacy check).
        .exit_on_redraw = true,
        .polls_lobby = true,
        .draw_background = &picker_teams_menu_engine_draw_background,
        .draw_content = &picker_teams_menu_engine_draw_content,
        .frame_tick = &picker_teams_menu_engine_frame_tick,
        .exit_value = MENU_EXIT,
    };
    return spec;
}

// ---------------------------------------------------------------------------
// HIRE (§1.8 step 6): PREV/NEXT candidate cyclers over the recruit portrait,
// the hire-team cycler, HIRE ME, BACK. Rows transcribed VERBATIM from the
// deleted k_hiremenu_buttons — including the PREV/NEXT table positions
// (10,40)/(110,40) that the legacy loop immediately recomputed from the
// description/name box geometry before init_buttons: that entry-time
// repositioning is the spec's prepare_buttons hook, so the accessor output
// (what test_menu_pins pins) stays the table shape while the live screen
// keeps the computed layout. The heavy stat/cost/description panels stay a
// client-side content hook (§1.3). Right-click is live (reverse cycling via
// do_call_right). BACK carries MENU_EXIT; the runner folds it to the
// spec's MENU_REDRAW exit_value (the legacy return); a remote start
// propagates its MENU_EXIT directly (the slot-menu normalization).

// The team cycler is hidden for solo play (the legacy entry-time
// `buttons[2].hidden = (numplayers == 1)` — numplayers can only change on
// screens that are never open at the same time, so the per-frame gate is
// entry-equivalent) and on DISABLE_MULTIPLAYER builds.
RowState hire_change_team_row_state(const MenuLabelContext& context)
{
#ifdef DISABLE_MULTIPLAYER
    (void)context;
    return RowState::Hidden;
#else
    return (context.save != nullptr && context.save->numplayers == 1)
        ? RowState::Hidden
        : RowState::Visible;
#endif
}

constexpr MenuButtonSpec kHireMenuRows[] = {
    {.id = "prev", .label = "PREV",
     .x = 10, .y = 40, .w = 40, .h = 20,
     .action = ButtonAction::CycleGuy, .arg = -1,
     .nav = {.down = 4, .right = 1}},
    {.id = "next", .label = "NEXT",
     .x = 110, .y = 40, .w = 40, .h = 20,
     .action = ButtonAction::CycleGuy, .arg = 1,
     .nav = {.down = 3, .left = 0, .right = 3}},
    {.id = "change_hire_team", .label = "hiring for team X",
     .x = 190, .y = 170, .w = 110, .h = 20,
     .action = ButtonAction::ChangeHireTeam, .arg = 1,
     .nav = {.up = 1, .left = 3},
     .state_override = &hire_change_team_row_state},
    {.id = "hire_me", .label = "HIRE ME",
     .x = 82, .y = 166, .w = 88, .h = 28,
     .action = ButtonAction::AddGuy, .arg = -1,
     .nav = {.up = 1, .left = 4, .right = 2}},
    {.id = "back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 10, .y = 170, .w = 40, .h = 20,
     .action = ButtonAction::ReturnMenu, .arg = MENU_EXIT,
     .nav = {.up = 0, .right = 3}},
};

// ---------------------------------------------------------------------------
// TRAIN (§1.8 step 6): the six +/- stat pairs beside the portrait (pixie
// faces FAMILY_MINUS/FAMILY_PLUS — the legacy set_graphic loop after
// init_buttons and after every MENU_REDRAW reset is the art_family binding,
// which the runner re-applies after EVERY reset), PREV/NEXT team cyclers,
// RENAME / DETAILS.. header row, the team cycler, ACCEPT / VIEW TEAM / BACK.
// Rows transcribed VERBATIM from the deleted k_trainmenu_buttons. The stat
// panels and the live allbuttons_[18] "Playing on Team N" write (G8: swept
// only at Layer F) stay in the content hook. Nested submenus (DETAILS,
// RENAME, VIEW TEAM) return MENU_REDRAW for reset_buttons to consume
// (exit_on_redraw stays false); a VIEW TEAM GO or a remote start propagates
// MENU_EXIT, and the wrapper folds the exit exactly as the legacy loop did.

// The team cycler only exists with multiplayer compiled in (the legacy
// `buttons[18].hidden = true` under DISABLE_MULTIPLAYER).
RowState train_change_team_row_state(const MenuLabelContext& context)
{
    (void)context;
#ifdef DISABLE_MULTIPLAYER
    return RowState::Hidden;
#else
    return RowState::Visible;
#endif
}

constexpr MenuButtonSpec kTrainMenuRows[] = {
    {.id = "prev", .label = "PREV",
     .x = 10, .y = 40, .w = 40, .h = 20,
     .action = ButtonAction::CycleTeamGuy, .arg = -1,
     .nav = {.down = 2, .right = 1}},
    {.id = "next", .label = "NEXT",
     .x = 110, .y = 40, .w = 40, .h = 20,
     .action = ButtonAction::CycleTeamGuy, .arg = 1,
     .nav = {.down = 3, .left = 0, .right = 16}},
    {.id = "dec_str", .label = "",
     .x = 16, .y = 70, .w = 16, .h = 10,
     .action = ButtonAction::DecreaseStat, .arg = BUT_STR,
     .nav = {.up = 0, .down = 4, .right = 3},
     .art_family = FAMILY_MINUS},
    {.id = "inc_str", .label = "",
     .x = 126, .y = 70, .w = 16, .h = 12,
     .action = ButtonAction::IncreaseStat, .arg = BUT_STR,
     .nav = {.up = 1, .down = 5, .left = 2},
     .art_family = FAMILY_PLUS},
    {.id = "dec_dex", .label = "",
     .x = 16, .y = 85, .w = 16, .h = 10,
     .action = ButtonAction::DecreaseStat, .arg = BUT_DEX,
     .nav = {.up = 2, .down = 6, .right = 5},
     .art_family = FAMILY_MINUS},
    {.id = "inc_dex", .label = "",
     .x = 126, .y = 85, .w = 16, .h = 12,
     .action = ButtonAction::IncreaseStat, .arg = BUT_DEX,
     .nav = {.up = 3, .down = 7, .left = 4},
     .art_family = FAMILY_PLUS},
    {.id = "dec_con", .label = "",
     .x = 16, .y = 100, .w = 16, .h = 10,
     .action = ButtonAction::DecreaseStat, .arg = BUT_CON,
     .nav = {.up = 4, .down = 8, .right = 7},
     .art_family = FAMILY_MINUS},
    {.id = "inc_con", .label = "",
     .x = 126, .y = 100, .w = 16, .h = 12,
     .action = ButtonAction::IncreaseStat, .arg = BUT_CON,
     .nav = {.up = 5, .down = 9, .left = 6},
     .art_family = FAMILY_PLUS},
    {.id = "dec_int", .label = "",
     .x = 16, .y = 115, .w = 16, .h = 10,
     .action = ButtonAction::DecreaseStat, .arg = BUT_INT,
     .nav = {.up = 6, .down = 10, .right = 9},
     .art_family = FAMILY_MINUS},
    {.id = "inc_int", .label = "",
     .x = 126, .y = 115, .w = 16, .h = 12,
     .action = ButtonAction::IncreaseStat, .arg = BUT_INT,
     .nav = {.up = 7, .down = 11, .left = 8},
     .art_family = FAMILY_PLUS},
    {.id = "dec_armor", .label = "",
     .x = 16, .y = 130, .w = 16, .h = 10,
     .action = ButtonAction::DecreaseStat, .arg = BUT_ARMOR,
     .nav = {.up = 8, .down = 12, .right = 11},
     .art_family = FAMILY_MINUS},
    {.id = "inc_armor", .label = "",
     .x = 126, .y = 130, .w = 16, .h = 12,
     .action = ButtonAction::IncreaseStat, .arg = BUT_ARMOR,
     .nav = {.up = 9, .down = 13, .left = 10},
     .art_family = FAMILY_PLUS},
    {.id = "dec_level", .label = "",
     .x = 16, .y = 145, .w = 16, .h = 10,
     .action = ButtonAction::DecreaseStat, .arg = BUT_LEVEL,
     .nav = {.up = 10, .down = 19, .right = 13},
     .art_family = FAMILY_MINUS},
    {.id = "inc_level", .label = "",
     .x = 126, .y = 145, .w = 16, .h = 12,
     .action = ButtonAction::IncreaseStat, .arg = BUT_LEVEL,
     .nav = {.up = 11, .down = 15, .left = 12, .right = 18},
     .art_family = FAMILY_PLUS},
    {.id = "view_team", .label = "VIEW TEAM",
     .x = 190, .y = 170, .w = 90, .h = 20,
     .action = ButtonAction::CreateViewMenu, .arg = -1,
     .nav = {.up = 18, .left = 15}},
    {.id = "accept", .label = "ACCEPT",
     .x = 80, .y = 170, .w = 80, .h = 20,
     .action = ButtonAction::EditGuy, .arg = -1,
     .nav = {.up = 13, .left = 19, .right = 14}},
    {.id = "rename", .label = "RENAME",
     .x = 174, .y = 8, .w = 64, .h = 22,
     .action = ButtonAction::NameGuy, .arg = 1,
     .nav = {.down = 18, .left = 1, .right = 17}},
    {.id = "details", .label = "DETAILS..",
     .x = 240, .y = 8, .w = 64, .h = 22,
     .action = ButtonAction::CreateDetailMenu, .arg = 0,
     .nav = {.down = 18, .left = 16}},
    {.id = "change_team", .label = "Playing on Team X",
     .x = 174, .y = 138, .w = 133, .h = 22,
     .action = ButtonAction::ChangeTeam, .arg = 1,
     .nav = {.up = 17, .down = 14, .left = 13},
     .state_override = &train_change_team_row_state},
    {.id = "back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 10, .y = 170, .w = 40, .h = 20,
     .action = ButtonAction::ReturnMenu, .arg = MENU_EXIT,
     .nav = {.up = 12, .right = 15}},
};

// ---------------------------------------------------------------------------
// PROGRESS (§1.8 step 6): the campaign-progress report (cleared levels,
// per-level GO shortcuts) under PREV | NEXT | BACK. Rows transcribed
// VERBATIM from the deleted function-local table in create_progress_menu —
// including the shipped quirk that PREV/NEXT are KEYBOARD-DEAD (myfun = 0;
// action Invalid materializes the same 0): the legacy loop dispatched them
// by raw mouse-rect checks, which survive in the frame_tick hook. Making
// them keyboard-live would be a visible change — deferred past Layer E
// (the gate-lattice sweep carries the declared exception). The row list,
// GO-button scan, and scroll indicator are the content/frame hooks in
// picker_team_build.cpp; the screen draws over a plain cleared buffer (no
// backdrop — legacy shape).

constexpr MenuButtonSpec kProgressMenuRows[] = {
    {.id = "prev", .label = "PREV",
     .x = 30, .y = 170, .w = 40, .h = 20,
     .action = ButtonAction::Invalid, .arg = -1,
     .nav = {.right = 1}},
    {.id = "next", .label = "NEXT",
     .x = 80, .y = 170, .w = 40, .h = 20,
     .action = ButtonAction::Invalid, .arg = -1,
     .nav = {.left = 0, .right = 2}},
    {.id = "back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 260, .y = 170, .w = 50, .h = 20,
     .action = ButtonAction::ReturnMenu, .arg = MENU_EXIT,
     .nav = {.left = 1}},
};

// ---------------------------------------------------------------------------
// VIEW LEVEL (§1.8 step 6): the read-only framed roster report of the
// current scenario. Rows transcribed VERBATIM from the deleted
// k_viewscenario_buttons (PREV/NEXT start hidden — the static table
// defaults). Visibility re-derives per frame from the open screen's
// PageModel (state override; hidden while the report fits one page) and the
// rewire keeps BACK's right-link closed over it — both read the file-static
// screen state in picker_team_build.cpp, null (single-page shape) when no
// wrapper is open. The page-step stash consumption sits in consume_click at
// the exact legacy point (a pager MENU_OK flips the page instead of
// reaching reset_buttons). BACK carries MENU_REDRAW (exit_on_redraw); a
// remote start propagates MENU_EXIT.

constexpr MenuButtonSpec kViewScenarioRows[] = {
    {.id = "back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 10, .y = 170, .w = 44, .h = 20,
     .action = ButtonAction::ReturnMenu, .arg = MENU_REDRAW,
     .nav = {.right = 1}},
    {.id = "page_prev", .label = "PREV",
     .x = 220, .y = 170, .w = 40, .h = 20,
     .action = ButtonAction::ViewScenarioPageFlip, .arg = -1,
     .nav = {.left = 0, .right = 2},
     .state_override = &picker_view_scenario_engine_pager_row_state,
     .hidden = true},
    {.id = "page_next", .label = "NEXT",
     .x = 270, .y = 170, .w = 40, .h = 20,
     .action = ButtonAction::ViewScenarioPageFlip, .arg = 1,
     .nav = {.left = 1},
     .state_override = &picker_view_scenario_engine_pager_row_state,
     .hidden = true},
};

// ---------------------------------------------------------------------------
// TEAM BUILD (§1.8 step 5, the cluster's parent screen). A clean 3x3 grid on
// the classic x=30/120/210 columns, transcribed VERBATIM from the deleted
// k_createmenu_buttons: VIEW/TRAIN/HIRE (y=70), LOAD/SAVE/GO (y=100),
// BACK | SCENARIO | NETWORKING (y=140). GO is the only host-gated button —
// the legacy sync (hide + picker_wire_team_build_nav) is the spec's Rewire
// program (G1). FadeAroundEntry replays the legacy fade-out / cold
// backdrop+buttons draw / fade-in entry; the level-reload guard
// (frame_tick/on_reset, entered with last_level_id = -1) reloads the picker
// world on the first frame and after every SET LEVEL / nested-screen reset,
// exactly where the legacy loop reloaded.

constexpr MenuButtonSpec kTeamBuildRows[] = {
    {.id = "view_team", .label = "VIEW TEAM",
     .x = 30, .y = 70, .w = 80, .h = 15,
     .action = ButtonAction::CreateViewMenu, .arg = -1,
     .nav = {.down = 3, .right = 1}},
    {.id = "train_team", .label = "TRAIN TEAM",
     .x = 120, .y = 70, .w = 80, .h = 15,
     .action = ButtonAction::CreateTrainMenu, .arg = -1,
     .nav = {.down = 4, .left = 0, .right = 2}},
    {.id = "hire_troops", .label = "HIRE TROOPS",
     .x = 210, .y = 70, .w = 80, .h = 15,
     .action = ButtonAction::CreateHireMenu, .arg = -1,
     .nav = {.down = 5, .left = 1}},
    {.id = "load_team", .label = "LOAD TEAM",
     .x = 30, .y = 100, .w = 80, .h = 15,
     .action = ButtonAction::CreateLoadMenu, .arg = -1,
     .nav = {.up = 0, .down = 6, .right = 4}},
    {.id = "save_team", .label = "SAVE TEAM",
     .x = 120, .y = 100, .w = 80, .h = 15,
     .action = ButtonAction::CreateSaveMenu, .arg = -1,
     .nav = {.up = 1, .down = 7, .left = 3, .right = 5}},
    {.id = "go", .label = "GO",
     .x = 210, .y = 100, .w = 80, .h = 15,
     .action = ButtonAction::GoMenu, .arg = -1,
     .nav = {.up = 2, .down = 8, .left = 4}},
    {.id = "back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 30, .y = 140, .w = 60, .h = 30,
     .action = ButtonAction::ReturnMenu, .arg = MENU_EXIT,
     .nav = {.up = 3, .right = 7}},
    {.id = "scenario", .label = "SCENARIO",
     .x = 120, .y = 140, .w = 80, .h = 20,
     .action = ButtonAction::CreateScenarioMenu, .arg = -1,
     .nav = {.up = 4, .left = 6, .right = 8}},
    {.id = "networking", .label = "NETWORKING",
     .x = 210, .y = 140, .w = 80, .h = 20,
     .action = ButtonAction::Networking, .arg = -1,
     .nav = {.up = 5, .left = 7}},
};

// The legacy per-frame content, verbatim: up to two translucent lobby-status
// lines at the top, and the compact current-scenario hint in the empty y=40
// band (the full level-title / campaign-name strips live in the SCENARIO
// subscreen, next to the buttons that change them).
void team_build_draw_content(void* /*screen_state*/)
{
    screen* const myscreen = og::runtime::current_session->myscreen_;
    text& mytext = myscreen->text_normal;

    const std::vector<std::string> lobby_status = picker_lobby_status_lines();
    for (std::size_t line_index = 0;
         line_index < lobby_status.size() && line_index < 2; ++line_index)
    {
        const std::string& line = lobby_status[line_index];
        if (line.empty())
            continue;

        const int status_y = 8 + static_cast<int>(line_index) * 10;
        const int status_w = static_cast<int>(line.size()) * 6;
        myscreen->draw_rect_filled(10, status_y - 1, status_w + 4, 8,
                                   PURE_BLACK, 150);
        mytext.write_xy(12, status_y, WHITE, "%s", line.c_str());
    }

    {
        std::string hint = std::format("SCEN {}: {}",
                                       myscreen->save_data.scen_num,
                                       myscreen->world().title);
        if (hint.size() > 34)
            hint.resize(34);
        const int hint_w = static_cast<int>(hint.size()) * 6;
        myscreen->draw_rect_filled(10, 43, hint_w + 4, 8, PURE_BLACK, 150);
        mytext.write_xy(12, 44, WHITE, "%s", hint.c_str());
    }
}

void team_build_rewire(button* buttons, int num_buttons,
                       int& highlighted_button)
{
    sync_team_build_host_control_visibility(buttons, num_buttons,
                                            highlighted_button);
}

const MenuScreenSpec& team_build_menu_screen_spec()
{
    static const MenuScreenSpec spec{
        .name = "team_build",
        .rows = kTeamBuildRows,
        .row_count = static_cast<int>(std::size(kTeamBuildRows)),
        .buttons_accessor = &picker_createmenu_buttons,
        .count_accessor = &picker_createmenu_button_count,
        .nav = {.kind = NavProgramKind::Rewire, .rewire = &team_build_rewire},
        // The loop-top host-GO check that launches a joiner parked here.
        .remote_start = RemoteStartScope::TeamBuildScope,
        .remote_start_exit = RemoteStartExit::ReturnMenuExit,
        .enter = EnterTransition::FadeAroundEntry,
        .default_highlight = 1,  // train_team, as the legacy loop
        .polls_lobby = true,
        .draw_background = &picker_backdrop_draw_background,
        .draw_content = &team_build_draw_content,
        .frame_tick = &level_reload_guard_frame_tick,
        .on_reset = &level_reload_guard_on_reset,
        // Every legacy exit carried MENU_EXIT (nested MENU_REDRAWs are
        // consumed by reset_buttons; the reload guard keeps the loop alive).
        .exit_value = MENU_EXIT,
    };
    return spec;
}

constexpr MenuScreenSpec make_main_menu_spec(const MenuButtonSpec* rows,
                                             int row_count)
{
    MenuScreenSpec spec{};
    spec.name = "mainmenu";
    spec.rows = rows;
    spec.row_count = row_count;
    spec.buttons_accessor = &picker_mainmenu_buttons;
    spec.count_accessor = &picker_mainmenu_button_count;
    // §2.1: CONTINUE/LOAD gate on company existence, so the nav graph routes
    // around them per frame (the pair's own hidden state is written by the
    // gate pass; the rewire fixes the links INTO them).
    spec.nav = {.kind = NavProgramKind::Rewire, .rewire = &main_menu_nav_rewire};
    // A host GO while this joiner sits on the main menu: break with
    // CONTINUE selected so present_menu() routes the shared state machine
    // into team build, whose loop-top check launches the game.
    spec.remote_start = RemoteStartScope::MainScope;
    spec.remote_start_exit = RemoteStartExit::BreakWithSelection;
    // Legacy entry: fade the previous menu out, first draw, fade in.
    spec.enter = EnterTransition::FadeWithInitialDraw;
    spec.default_highlight = 1;  // continue_game
    // Right-click is live here: deselecting the current player count enters
    // spectator mode (do_call_right, SetPlayerMode).
    spec.right_click_enabled = true;
    spec.polls_lobby = true;
    spec.draw_background = &main_menu_draw_background;
    spec.draw_content = &main_menu_draw_content;
    // Every caller ignores mainmenu()'s return value; MENU_EXIT mirrors the
    // legacy loop's exit-bearing retvalue.
    spec.exit_value = MENU_EXIT;
    return spec;
}

// ---------------------------------------------------------------------------
// §2.2 new-company name entry (Layer F engine screen)
// ---------------------------------------------------------------------------

// Screen state carried through run_menu_screen's opaque screen_state pointer:
// the working display name and the ACCEPT/BACK verdict the wrapper reads.
struct NameEntryState {
    std::string name;
    bool accepted = false;
};

// A process-lifetime RNG for the generated suggestions, seeded once from the
// wall clock so the first suggestion varies between sessions; REROLL and each
// new-game entry simply advance it. Determinism is irrelevant here (the name
// is user-visible and re-rollable, and generate_company_name itself is
// unit-pinned against injected RNGs).
IRandom& name_entry_rng()
{
    static SeededRandom rng(static_cast<std::uint32_t>(std::time(nullptr)));
    return rng;
}

// All four rows dispatch through the single generic ButtonAction::MenuSpecRow
// (G3): the arg is the row ordinal on_spec_row switches on. No build-gating
// here, so the materialized index equals the spec ordinal.
constexpr int kNameEntryBackIndex = 0;
constexpr int kNameEntryValueIndex = 1;
constexpr int kNameEntryRerollIndex = 2;
constexpr int kNameEntryAcceptIndex = 3;

// 320-wide canvas center, which also centers the strip (48,78,224,16).
constexpr int kNameEntryCenterX = 160;

constexpr MenuButtonSpec kNameEntryRows[] = {
    // BACK cancels — nothing is written (§2.2). Escape hotkey.
    {.id = "back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 10, .y = 170, .w = 44, .h = 20,
     .action = ButtonAction::MenuSpecRow, .arg = kNameEntryBackIndex,
     .nav = {.up = kNameEntryRerollIndex}},
    // The editable name strip. Empty label: the current name is drawn centered
    // over it in the content pass (like the begin_new_game art face). y+h = 94
    // <= 100, so a web soft keyboard never covers it (§2.0 U5).
    {.id = "company_name_value", .label = "",
     .x = 48, .y = 78, .w = 224, .h = 16,
     .action = ButtonAction::MenuSpecRow, .arg = kNameEntryValueIndex,
     .nav = {.down = kNameEntryRerollIndex}},
    {.id = "company_name_reroll", .label = "REROLL",
     .x = 86, .y = 102, .w = 68, .h = 14,
     .action = ButtonAction::MenuSpecRow, .arg = kNameEntryRerollIndex,
     .nav = {.up = kNameEntryValueIndex, .down = kNameEntryBackIndex,
             .right = kNameEntryAcceptIndex}},
    {.id = "company_name_accept", .label = "ACCEPT",
     .x = 166, .y = 102, .w = 68, .h = 14,
     .action = ButtonAction::MenuSpecRow, .arg = kNameEntryAcceptIndex,
     .nav = {.up = kNameEntryValueIndex, .down = kNameEntryBackIndex,
             .left = kNameEntryRerollIndex}},
};

void name_entry_draw_content(void* screen_state)
{
    const NameEntryState* st = static_cast<const NameEntryState*>(screen_state);
    screen* game = og::runtime::current_session->myscreen_;
    game->text_normal.write_xy_center(kNameEntryCenterX, 30, YELLOW, "%s",
                                      "FOUND YOUR COMPANY");
    std::string name = st != nullptr ? st->name : std::string();
    if (name.size() > kCompanyNameMaxLen)
        name.resize(kCompanyNameMaxLen);
    game->text_normal.write_xy_center(kNameEntryCenterX, 82, WHITE, "%s",
                                      name.c_str());
    // The slug preview teaches the display-name/filename split (§2.2, spec 2).
    game->text_normal.write_xy_center(
        kNameEntryCenterX, 126, GREY, "%s",
        format_company_file_preview(name).c_str());
}

Sint32 name_entry_on_spec_row(int row, void* screen_state)
{
    NameEntryState* st = static_cast<NameEntryState*>(screen_state);
    screen* game = og::runtime::current_session->myscreen_;
    switch (row) {
    case kNameEntryBackIndex:
        // Cancel: nothing is created (§2.2). Structural exit (MENU_EXIT).
        st->accepted = false;
        TRACE("name_entry", "cancel");
        return MENU_EXIT;
    case kNameEntryValueIndex: {
        // Edit the name in place. input_string_value's maxlength counts the
        // NUL, so pass one past the display cap to allow a full 18 chars.
        release_mouse();
        std::optional<std::string> edited =
            game->text_normal.input_string_value(
                52, 82, static_cast<short>(kCompanyNameMaxLen + 1),
                st->name.c_str());
        grab_mouse();
        if (edited.has_value()) {
            // input_string_value already caps the entry at maxlength-1 ==
            // kCompanyNameMaxLen, so no further clamp is needed here.
            std::string value = *edited;
            // Empty clears back to a fresh suggestion (§2.2).
            if (value.empty())
                value = generate_company_name(name_entry_rng());
            st->name = std::move(value);
        }
        TRACE("name_entry", "edit %s", st->name.c_str());
        return MENU_REDRAW;
    }
    case kNameEntryRerollIndex:
        st->name = generate_company_name(name_entry_rng());
        TRACE("name_entry", "reroll %s", st->name.c_str());
        return MENU_OK;
    case kNameEntryAcceptIndex:
        // Found the company (§2.2). Structural exit; the wrapper writes the
        // file (creation IS the first autosave).
        st->accepted = true;
        TRACE("name_entry", "accept %s", st->name.c_str());
        return MENU_EXIT;
    default:
        return 0;
    }
}

} // namespace

// §2.1: refresh the cached company view once per mainmenu() entry (the
// blocking loop's gate/rewire/caption read the cache, never the filesystem).
void refresh_main_menu_company_view()
{
    const std::vector<og::data::CompanyInfo> companies = og::data::list_companies();
    g_main_menu_company_view.present = !companies.empty();
    g_main_menu_company_view.display_name =
        companies.empty() ? std::string() : companies.front().display_name;
}

// Test seam: pin the company view (present flag + display name) without a
// filesystem, so the no-company gate/rewire/caption shape is deterministic.
void set_main_menu_company_view_for_tests(bool present, std::string display_name)
{
    g_main_menu_company_view.present = present;
    g_main_menu_company_view.display_name = std::move(display_name);
}

const MenuScreenSpec& main_menu_screen_spec_mp()
{
    static const MenuScreenSpec spec = make_main_menu_spec(
        kMainMenuRowsMP, static_cast<int>(std::size(kMainMenuRowsMP)));
    return spec;
}

const MenuScreenSpec& main_menu_screen_spec_nomp()
{
    static const MenuScreenSpec spec = make_main_menu_spec(
        kMainMenuRowsNoMP, static_cast<int>(std::size(kMainMenuRowsNoMP)));
    return spec;
}

const MenuScreenSpec& main_menu_screen_spec()
{
#ifndef DISABLE_MULTIPLAYER
    return main_menu_screen_spec_mp();
#else
    return main_menu_screen_spec_nomp();
#endif
}

const MenuScreenSpec& difficulty_menu_screen_spec()
{
    static const MenuScreenSpec spec{
        .name = "difficulty",
        .rows = kDifficultyRows,
        .row_count = static_cast<int>(std::size(kDifficultyRows)),
        .buttons_accessor = &picker_difficulty_menu_buttons,
        .count_accessor = &picker_difficulty_menu_button_count,
        // G1: the legacy visibility/nav function, plugged verbatim (it also
        // re-asserts the gate pass's hidden flags — idempotent by design).
        .nav = {.kind = NavProgramKind::Rewire,
                .rewire = &sync_difficulty_menu_visibility},
        // A host GO must launch a joiner parked in this subscreen: propagate
        // the remote MENU_EXIT (with CONTINUE selected) so mainmenu() unwinds
        // too, instead of a local BACK's MENU_REDRAW.
        .remote_start = RemoteStartScope::MainScope,
        .remote_start_exit = RemoteStartExit::ReturnMenuExit,
        .polls_lobby = true,
        // The match-rule callbacks keep their own
        // picker_lobby_sync_settings_from_save() tails (Layer E: byte-for-
        // byte legacy behavior), so the G12 single-point flag stays off.
        .sync_settings_after_mutation = false,
        .draw_background = &options_panel_draw_background,
        .draw_content = &difficulty_draw_content,
        .exit_value = MENU_REDRAW,
    };
    return spec;
}

const MenuScreenSpec& hire_menu_screen_spec()
{
    static const MenuScreenSpec spec{
        .name = "hire",
        .rows = kHireMenuRows,
        .row_count = static_cast<int>(std::size(kHireMenuRows)),
        .buttons_accessor = &picker_hiremenu_buttons,
        .count_accessor = &picker_hiremenu_button_count,
        // Nav closure over the solo-hidden team cycler (the legacy links
        // into it were refused by handle_menu_nav; the engine writes the
        // explicit no-op instead — §1.5 invariant).
        .nav = {.kind = NavProgramKind::Rewire,
                .rewire = &picker_hire_menu_engine_rewire},
        // The loop-top host-GO check that launches a joiner parked here.
        // The remote MENU_EXIT propagates directly (legacy folded it to
        // MENU_REDRAW and the parent re-detected the start one loop later —
        // the slot-menu normalization, same launch).
        .remote_start = RemoteStartScope::TeamBuildScope,
        .remote_start_exit = RemoteStartExit::ReturnMenuExit,
        .default_highlight = 1,  // next, as the legacy loop
        // clickvalue == 2 -> rightclick (reverse candidate cycling).
        .right_click_enabled = true,
        .polls_lobby = true,
        // Entry-time PREV/NEXT repositioning (see the spec comment).
        .prepare_buttons = &picker_hire_menu_engine_prepare_buttons,
        .draw_background = &picker_backdrop_draw_background,
        .draw_content = &picker_hire_menu_engine_draw_content,
        // The new-game intro popup fires after the first presented frame
        // (legacy loop-bottom arg1 == 1 branch).
        .frame_tick = &picker_hire_menu_engine_frame_tick,
        // The legacy reset tail: re-derive the hire-team label surfaces.
        .on_reset = &picker_hire_menu_engine_on_reset,
        // Every legacy local exit returned MENU_REDRAW.
        .exit_value = MENU_REDRAW,
    };
    return spec;
}

const MenuScreenSpec& train_menu_screen_spec()
{
    static const MenuScreenSpec spec{
        .name = "train",
        .rows = kTrainMenuRows,
        .row_count = static_cast<int>(std::size(kTrainMenuRows)),
        .buttons_accessor = &picker_trainmenu_buttons,
        .count_accessor = &picker_trainmenu_button_count,
        .nav = {.kind = NavProgramKind::Rewire,
                .rewire = &picker_train_menu_engine_rewire},
        .remote_start = RemoteStartScope::TeamBuildScope,
        .remote_start_exit = RemoteStartExit::ReturnMenuExit,
        .default_highlight = 1,  // next, as the legacy loop
        // clickvalue == 2 -> rightclick (reverse stat/team cycling).
        .right_click_enabled = true,
        .polls_lobby = true,
        .draw_background = &picker_backdrop_draw_background,
        .draw_content = &picker_train_menu_engine_draw_content,
        // The legacy MENU_REDRAW reset tail (promotion resync, bug A9) —
        // harmless on MENU_OK resets; see the hook comment.
        .on_reset = &picker_train_menu_engine_on_reset,
        // Every exit-bearing path carried MENU_EXIT; the wrapper folds it
        // to MENU_REDRAW unless a start was selected (legacy shape).
        .exit_value = MENU_EXIT,
    };
    return spec;
}

const MenuScreenSpec& progress_menu_screen_spec()
{
    static const MenuScreenSpec spec{
        .name = "progress",
        .rows = kProgressMenuRows,
        .row_count = static_cast<int>(std::size(kProgressMenuRows)),
        .buttons_accessor = &picker_progressmenu_buttons,
        .count_accessor = &picker_progressmenu_button_count,
        // The loop-top host-GO check that launches a joiner parked here.
        // The remote MENU_EXIT propagates directly (legacy split: the
        // loop-top break folded to MENU_REDRAW, the click spin-wait
        // returned MENU_EXIT — normalized to the propagating shape).
        .remote_start = RemoteStartScope::TeamBuildScope,
        .remote_start_exit = RemoteStartExit::ReturnMenuExit,
        .default_highlight = 2,  // back, as the legacy loop
        .polls_lobby = true,
        // No backdrop: the legacy screen drew over a plain cleared buffer.
        .draw_background = &picker_progress_menu_engine_draw_background,
        .draw_content = &picker_progress_menu_engine_draw_content,
        // The raw-mouse PREV/NEXT + per-row GO dispatch (returns false to
        // exit with MENU_REDRAW when a GO shortcut picks a level).
        .frame_tick = &picker_progress_menu_engine_frame_tick,
        // Every legacy local exit returned MENU_REDRAW.
        .exit_value = MENU_REDRAW,
    };
    return spec;
}

const MenuScreenSpec& view_scenario_menu_screen_spec()
{
    static const MenuScreenSpec spec{
        .name = "view_scenario",
        .rows = kViewScenarioRows,
        .row_count = static_cast<int>(std::size(kViewScenarioRows)),
        .buttons_accessor = &picker_viewscenario_buttons,
        .count_accessor = &picker_viewscenario_button_count,
        // Nav closure over the hidden pagers (BACK's right-link), reading
        // the open screen's PageModel.
        .nav = {.kind = NavProgramKind::Rewire,
                .rewire = &picker_view_scenario_engine_rewire},
        // The loop-top host-GO check; the remote MENU_EXIT propagates
        // (exactly the legacy `retvalue & MENU_EXIT` return).
        .remote_start = RemoteStartScope::TeamBuildScope,
        .remote_start_exit = RemoteStartExit::ReturnMenuExit,
        .default_highlight = kViewScenarioBackIndex,
        // BACK carries MENU_REDRAW and ends the screen at the legacy check
        // position (before the page-step consumption).
        .exit_on_redraw = true,
        .polls_lobby = true,
        .draw_background = &picker_backdrop_draw_background,
        .draw_content = &picker_view_scenario_engine_draw_content,
        // The legacy page-step consumption (retvalue-zero + clamped flip +
        // flip trace), at the legacy point in the frame.
        .consume_click = &picker_view_scenario_engine_consume_click,
        .exit_value = MENU_REDRAW,
    };
    return spec;
}

const MenuScreenSpec& name_entry_menu_screen_spec()
{
    static const MenuScreenSpec spec{
        .name = "name_entry",
        .rows = kNameEntryRows,
        .row_count = static_cast<int>(std::size(kNameEntryRows)),
        .buttons_accessor = &picker_name_entry_buttons,
        .count_accessor = &picker_name_entry_button_count,
        // Fade the main menu out, draw the name screen cold, fade in (the
        // team-build entry idiom).
        .enter = EnterTransition::FadeAroundEntry,
        // Initial highlight: ACCEPT (§2.2).
        .default_highlight = kNameEntryAcceptIndex,
        .draw_background = &picker_backdrop_draw_background,
        .draw_content = &name_entry_draw_content,
        // G3 generic row dispatch: BACK/edit/REROLL/ACCEPT.
        .on_spec_row = &name_entry_on_spec_row,
        .exit_value = MENU_REDRAW,
    };
    return spec;
}

// §2.2: run the name-entry screen and report whether the user founded a
// company. Called at the top of the BEGIN NEW GAME flow, before anything is
// reset or written — BACK returns false and leaves the loaded game intact.
bool run_new_company_name_entry(std::string& out_name)
{
    if (og::runtime::current_session->myscreen_ == nullptr)
        return false;
    NameEntryState state;
    state.name = generate_company_name(name_entry_rng());
    (void)run_menu_screen(name_entry_menu_screen_spec(), &state);
    if (!state.accepted)
        return false;
    out_name = state.name;
    return true;
}

// G4 registry: the one-lookup answer to "which system owns this screen"
// while legacy loops remain. Update the row when a screen migrates (and the
// host table in docs/menu-engine.md with it).
const MenuScreenHost& menu_screen_host(MenuScreenId id)
{
    static const std::array<MenuScreenHost,
                            static_cast<std::size_t>(MenuScreenId::Count)>
        hosts = [] {
            std::array<MenuScreenHost,
                       static_cast<std::size_t>(MenuScreenId::Count)>
                table{};
            auto set = [&table](MenuScreenId screen, MenuScreenHost host) {
                table[static_cast<std::size_t>(screen)] = host;
            };
            using Kind = MenuScreenHost::Kind;
            set(MenuScreenId::MainMenu,
                {.kind = Kind::Engine, .spec = &main_menu_screen_spec()});
            set(MenuScreenId::TeamBuild,
                {.kind = Kind::Engine, .spec = &team_build_menu_screen_spec()});
            set(MenuScreenId::ViewTeam,
                {.kind = Kind::Engine, .spec = &view_team_menu_screen_spec()});
            set(MenuScreenId::SaveSlots,
                {.kind = Kind::Engine, .spec = &save_slots_menu_screen_spec()});
            set(MenuScreenId::LoadSlots,
                {.kind = Kind::Engine, .spec = &load_slots_menu_screen_spec()});
            set(MenuScreenId::MainOptions,
                {.kind = Kind::Engine,
                 .spec = &main_options_menu_screen_spec()});
            set(MenuScreenId::DisplaySettings,
                {.kind = Kind::Engine,
                 .spec = &display_settings_menu_screen_spec()});
            set(MenuScreenId::ControlSettings,
                {.kind = Kind::Engine,
                 .spec = &control_options_menu_screen_spec()});
            set(MenuScreenId::GameplayFx,
                {.kind = Kind::Engine, .spec = &gameplay_fx_menu_screen_spec()});
            set(MenuScreenId::UiFx,
                {.kind = Kind::Engine, .spec = &ui_fx_menu_screen_spec()});
            set(MenuScreenId::GraphicsFx,
                {.kind = Kind::Engine, .spec = &graphics_fx_menu_screen_spec()});
            set(MenuScreenId::Difficulty,
                {.kind = Kind::Engine, .spec = &difficulty_menu_screen_spec()});
            set(MenuScreenId::Hire,
                {.kind = Kind::Engine, .spec = &hire_menu_screen_spec()});
            set(MenuScreenId::Train,
                {.kind = Kind::Engine, .spec = &train_menu_screen_spec()});
            set(MenuScreenId::Progress,
                {.kind = Kind::Engine, .spec = &progress_menu_screen_spec()});
            set(MenuScreenId::ViewScenario,
                {.kind = Kind::Engine,
                 .spec = &view_scenario_menu_screen_spec()});
            set(MenuScreenId::Scenario,
                {.kind = Kind::Engine, .spec = &scenario_menu_screen_spec()});
            set(MenuScreenId::Teams,
                {.kind = Kind::Engine, .spec = &teams_menu_screen_spec()});
            // NETWORKING is owned by the SdlPickerClient state machine
            // (configure_networking is a client method) and stays LEGACY:
            // valve V2 was exercised at Layer E closeout — the
            // retvalue-as-action-id dispatch, the staged-commit room-list
            // frame phases, and the web/native positional index fork all
            // resist the skeleton (docs/menu-engine.md "V2 decision
            // record"; pinned by
            // MenuEngine.networking_stays_legacy_v2_decision).
            set(MenuScreenId::Networking, {.kind = Kind::Legacy});
            return table;
        }();
    return hosts[static_cast<std::size_t>(id)];
}

} // namespace og::ui

// --- D3 materialization shims (moved from picker.cpp with the migration) ---

button* picker_mainmenu_buttons()
{
    og::ui::materialize_menu_buttons(og::ui::main_menu_screen_spec(),
                                     pks().mainmenu_buttons);
    return pks().mainmenu_buttons.data();
}

int picker_mainmenu_button_count()
{
    return static_cast<int>(pks().mainmenu_buttons.size());
}

// Replaces both OPTIONS_BUTTON_INDEX #defines (10 with multiplayer, 5
// without): the options gear's materialized index, derived from the spec
// instead of hand-tracked per variant.
int picker_mainmenu_options_index()
{
    const std::vector<const og::ui::MenuButtonSpec*> rows =
        og::ui::materialized_spec_rows(og::ui::main_menu_screen_spec());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (std::string_view(rows[i]->id) == "options")
            return static_cast<int>(i);
    }
    return static_cast<int>(rows.size()) - 1;
}

// The main menu, engine-hosted (the legacy loop in picker_main_menu.cpp is
// gone). The fade-out/first-draw/fade-in entry is the spec's
// FadeWithInitialDraw transition; every caller (present_menu /
// picker_mainmenu_loop) acts on pks().selected_menu_item and ignores the
// return value, exactly as it ignored the legacy loop's retvalue.
Sint32 mainmenu(Sint32 arg1)
{
    (void)arg1;
    if (og::runtime::current_session->myscreen_ == nullptr)
        return MENU_EXIT;
    // §2.1: sample the company set once, before the blocking loop opens.
    og::ui::refresh_main_menu_company_view();
    return og::ui::run_menu_screen(og::ui::main_menu_screen_spec());
}

button* picker_viewteam_buttons()
{
    og::ui::materialize_menu_buttons(og::ui::view_team_menu_screen_spec(),
                                     pks().viewteam_buttons);
    return pks().viewteam_buttons.data();
}

int picker_viewteam_button_count()
{
    return static_cast<int>(pks().viewteam_buttons.size());
}

button* picker_saveteam_buttons()
{
    og::ui::materialize_menu_buttons(og::ui::save_slots_menu_screen_spec(),
                                     pks().saveteam_buttons);
    return pks().saveteam_buttons.data();
}

int picker_saveteam_button_count()
{
    return static_cast<int>(pks().saveteam_buttons.size());
}

button* picker_loadteam_buttons()
{
    og::ui::materialize_menu_buttons(og::ui::load_slots_menu_screen_spec(),
                                     pks().loadteam_buttons);
    return pks().loadteam_buttons.data();
}

int picker_loadteam_button_count()
{
    return static_cast<int>(pks().loadteam_buttons.size());
}

button* picker_createmenu_buttons()
{
    og::ui::materialize_menu_buttons(og::ui::team_build_menu_screen_spec(),
                                     pks().createmenu_buttons);
    return pks().createmenu_buttons.data();
}

int picker_createmenu_button_count()
{
    return static_cast<int>(pks().createmenu_buttons.size());
}

// TEAM BUILD, engine-hosted (the legacy loop is gone). The reload cursor
// enters at -1: the first frame always reloads the picker world (the legacy
// entry behavior — the screen must reflect the save's level whatever was
// loaded before it). Every legacy exit carried MENU_EXIT; the fold below
// keeps the legacy return shape verbatim.
Sint32 create_team_menu(Sint32 arg1)
{
    (void)arg1;
    og::ui::LevelReloadGuardState guard{.last_level_id = -1,
                                        .was_reset = false};
    const Sint32 retvalue =
        og::ui::run_menu_screen(og::ui::team_build_menu_screen_spec(), &guard);
    if (retvalue & MENU_EXIT)
        return retvalue;
    return MENU_REDRAW;
}

button* picker_scenariomenu_buttons()
{
    og::ui::materialize_menu_buttons(og::ui::scenario_menu_screen_spec(),
                                     pks().scenariomenu_buttons);
    return pks().scenariomenu_buttons.data();
}

int picker_scenariomenu_button_count()
{
    return static_cast<int>(pks().scenariomenu_buttons.size());
}

button* picker_teamsmenu_buttons()
{
    og::ui::materialize_menu_buttons(og::ui::teams_menu_screen_spec(),
                                     pks().teamsmenu_buttons);
    return pks().teamsmenu_buttons.data();
}

int picker_teamsmenu_button_count()
{
    return static_cast<int>(pks().teamsmenu_buttons.size());
}

button* picker_hiremenu_buttons()
{
    og::ui::materialize_menu_buttons(og::ui::hire_menu_screen_spec(),
                                     pks().hiremenu_buttons);
    return pks().hiremenu_buttons.data();
}

int picker_hiremenu_button_count()
{
    return static_cast<int>(pks().hiremenu_buttons.size());
}

button* picker_trainmenu_buttons()
{
    og::ui::materialize_menu_buttons(og::ui::train_menu_screen_spec(),
                                     pks().trainmenu_buttons);
    return pks().trainmenu_buttons.data();
}

int picker_trainmenu_button_count()
{
    return static_cast<int>(pks().trainmenu_buttons.size());
}

button* picker_progressmenu_buttons()
{
    og::ui::materialize_menu_buttons(og::ui::progress_menu_screen_spec(),
                                     pks().progressmenu_buttons);
    return pks().progressmenu_buttons.data();
}

int picker_progressmenu_button_count()
{
    return static_cast<int>(pks().progressmenu_buttons.size());
}

button* picker_viewscenario_buttons()
{
    og::ui::materialize_menu_buttons(og::ui::view_scenario_menu_screen_spec(),
                                     pks().viewscenario_buttons);
    return pks().viewscenario_buttons.data();
}

int picker_viewscenario_button_count()
{
    return static_cast<int>(pks().viewscenario_buttons.size());
}

button* picker_name_entry_buttons()
{
    og::ui::materialize_menu_buttons(og::ui::name_entry_menu_screen_spec(),
                                     pks().name_entry_buttons);
    return pks().name_entry_buttons.data();
}

int picker_name_entry_button_count()
{
    return static_cast<int>(pks().name_entry_buttons.size());
}

// The SCENARIO subscreen, engine-hosted (the legacy loop is gone). Entry/exit
// buffer clears and the exit fold are the legacy shape, verbatim: a remote
// start (MENU_EXIT + the StartGame item) propagates so the parent team-build
// screen exits into GO; BACK's own MENU_EXIT folds into MENU_REDRAW to keep
// the parent running. The reload cursor starts at the current level (the
// parent already loaded it — no reload on entry).
Sint32 create_scenario_menu(Sint32 arg1)
{
    (void)arg1;
    og::runtime::current_session->myscreen_->clearbuffer();
    og::ui::LevelReloadGuardState guard{
        .last_level_id =
            og::runtime::current_session->myscreen_->save_data.scen_num,
        .was_reset = false,
    };
    const Sint32 retvalue =
        og::ui::run_menu_screen(og::ui::scenario_menu_screen_spec(), &guard);
    og::runtime::current_session->myscreen_->clearbuffer();
    if ((retvalue & MENU_EXIT) && team_build_start_selected())
        return retvalue;
    return MENU_REDRAW;
}

// The TEAMS subscreen, engine-hosted (the legacy loop is gone). Pager pages
// and the trace/reload cursors reset every open; the exit fold is the legacy
// shape, verbatim.
Sint32 create_teams_menu(Sint32 arg1)
{
    (void)arg1;
    og::runtime::current_session->myscreen_->clearbuffer();
    picker_teams_menu_engine_reset_open_state();
    const Sint32 retvalue =
        og::ui::run_menu_screen(og::ui::teams_menu_screen_spec());
    og::runtime::current_session->myscreen_->clearbuffer();
    if (retvalue & MENU_EXIT)
        return retvalue;
    return MENU_REDRAW;
}

// VIEW TEAM, engine-hosted (the legacy loop is gone). The buffer clears
// around the screen and the exit fold are the legacy entry/exit, verbatim:
// MENU_EXIT propagates so TeamBuild interception can map GO -> StartGame;
// BACK returns MENU_REDRAW to keep the parent create_team_menu running.
Sint32 create_view_menu(Sint32 arg1)
{
    (void)arg1;
    og::runtime::current_session->myscreen_->clearbuffer();
    const Sint32 retvalue =
        og::ui::run_menu_screen(og::ui::view_team_menu_screen_spec());
    og::runtime::current_session->myscreen_->clearbuffer();
    if (retvalue & MENU_EXIT)
        return retvalue;
    return MENU_REDRAW;
}

// The slot menus, engine-hosted (the shared legacy create_slot_menu loop is
// gone). Every legacy exit ran return_to_parent (clear_allbuttons +
// MENU_REDRAW); the engine keeps that for local exits and lets a remote
// start propagate its MENU_EXIT directly (the parent breaks with StartGame
// selected instead of re-detecting the start one loop later — same launch).
Sint32 create_save_menu(Sint32 /*arg1*/)
{
    const Sint32 retvalue =
        og::ui::run_menu_screen(og::ui::save_slots_menu_screen_spec());
    clear_allbuttons();
    if (retvalue & MENU_EXIT)
        return retvalue;
    return MENU_REDRAW;
}

Sint32 create_load_menu(Sint32 /*arg1*/)
{
    const Sint32 retvalue =
        og::ui::run_menu_screen(og::ui::load_slots_menu_screen_spec());
    clear_allbuttons();
    if (retvalue & MENU_EXIT)
        return retvalue;
    return MENU_REDRAW;
}

button* picker_difficulty_menu_buttons()
{
    og::ui::materialize_menu_buttons(og::ui::difficulty_menu_screen_spec(),
                                     pks().difficulty_menu_buttons);
    return pks().difficulty_menu_buttons.data();
}

int picker_difficulty_menu_button_count()
{
    return static_cast<int>(pks().difficulty_menu_buttons.size());
}

button* picker_gameplay_fx_options_buttons()
{
    og::ui::materialize_menu_buttons(og::ui::gameplay_fx_menu_screen_spec(),
                                     pks().gameplay_fx_options_buttons);
    return pks().gameplay_fx_options_buttons.data();
}

int picker_gameplay_fx_options_button_count()
{
    return static_cast<int>(pks().gameplay_fx_options_buttons.size());
}

button* picker_ui_fx_options_buttons()
{
    og::ui::materialize_menu_buttons(og::ui::ui_fx_menu_screen_spec(),
                                     pks().ui_fx_options_buttons);
    return pks().ui_fx_options_buttons.data();
}

int picker_ui_fx_options_button_count()
{
    return static_cast<int>(pks().ui_fx_options_buttons.size());
}

button* picker_graphics_fx_options_buttons()
{
    og::ui::materialize_menu_buttons(og::ui::graphics_fx_menu_screen_spec(),
                                     pks().graphics_fx_options_buttons);
    return pks().graphics_fx_options_buttons.data();
}

int picker_graphics_fx_options_button_count()
{
    return static_cast<int>(pks().graphics_fx_options_buttons.size());
}

// Blocking DIFFICULTY subscreen (the main-menu DIFFICULTY door): session
// difficulty plus the SaveData match rules (respawns, respawn delay,
// permadeath, generator rate). Engine-hosted (the legacy loop is gone).
Sint32 run_difficulty_menu()
{
    return og::ui::run_menu_screen(og::ui::difficulty_menu_screen_spec());
}

// The three FX subscreens, engine-hosted (the shared legacy loop
// run_fx_options_screen is gone).
Sint32 gameplay_fx_options()
{
    return og::ui::run_menu_screen(og::ui::gameplay_fx_menu_screen_spec());
}

Sint32 ui_fx_options()
{
    return og::ui::run_menu_screen(og::ui::ui_fx_menu_screen_spec());
}

Sint32 graphics_fx_options()
{
    return og::ui::run_menu_screen(og::ui::graphics_fx_menu_screen_spec());
}

button* picker_display_settings_buttons()
{
    og::ui::materialize_menu_buttons(og::ui::display_settings_menu_screen_spec(),
                                     pks().display_settings_buttons);
    return pks().display_settings_buttons.data();
}

int picker_display_settings_button_count()
{
    return static_cast<int>(pks().display_settings_buttons.size());
}

button* picker_control_options_buttons()
{
    og::ui::materialize_menu_buttons(og::ui::control_options_menu_screen_spec(),
                                     pks().control_options_buttons);
    return pks().control_options_buttons.data();
}

int picker_control_options_button_count()
{
    return static_cast<int>(pks().control_options_buttons.size());
}

// The DISPLAY and CONTROLS subscreens, engine-hosted (the legacy loops are
// gone).
Sint32 display_settings_options()
{
    return og::ui::run_menu_screen(og::ui::display_settings_menu_screen_spec());
}

Sint32 main_controls_options()
{
    return og::ui::run_menu_screen(og::ui::control_options_menu_screen_spec());
}

button* picker_main_options_buttons()
{
    og::ui::materialize_menu_buttons(og::ui::main_options_menu_screen_spec(),
                                     pks().main_options_buttons);
    return pks().main_options_buttons.data();
}

int picker_main_options_button_count()
{
    return static_cast<int>(pks().main_options_buttons.size());
}

// MAIN OPTIONS, engine-hosted (the legacy loop is gone). The exit epilogue is
// preserved verbatim: this is the single point where the whole options
// family's cfg edits are committed — sound device state, overscan, control
// settings, then cfg.save_settings().
Sint32 main_options()
{
    og::ui::run_menu_screen(og::ui::main_options_menu_screen_spec());

    og::runtime::current_session->myscreen_->soundp->set_sound(
        !cfg.is_on("sound", "sound"));
    // Sync overscan to config before saving (data/ can't depend on input/)
    cfg.apply_setting("graphics", "overscan_percentage",
        std::format("{:.0f}",
                    100 * og::runtime::current_session->overscan_percentage_));
    save_player_control_settings_to_cfg(cfg);
    cfg.save_settings();

    return MENU_REDRAW;
}
