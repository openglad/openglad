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
#include <openglad/gameplay/guy.h>
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

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <ctime>
#include <format>
#include <memory>
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
void sync_scenario_menu_host_control_visibility(button* buttons,
                                                int num_buttons,
                                                int& highlighted_button);
bool team_build_start_selected();
// Base camp helpers (defined beside the train/hire sessions in
// picker_team_build.cpp).
og::ui::DerivedStats picker_compute_guy_derived_stats(const guy& g);
void picker_set_train_seed_slot(int slot);
// picker_base_camp_after_roster_mutation() comes from picker_sdl_defs.h.
Sint32 create_train_menu(Sint32 arg1);
void popup_dialog(const char* title, const char* message);
bool no_or_yes_prompt(const char* title, const char* message,
                      bool default_value);
// TEAMS engine hooks (defined beside their file-local helpers in
// picker_team_build.cpp).
void picker_teams_menu_engine_reset_open_state();
void picker_teams_menu_engine_rewire(button* buttons, int num_buttons,
                                     int& highlighted_button);
bool picker_teams_menu_engine_frame_tick(void* screen_state, int frame);
void picker_teams_menu_engine_draw_background(void* screen_state);
void picker_teams_menu_engine_draw_content(void* screen_state);
Sint32 picker_teams_menu_engine_on_spec_row(int row, void* screen_state);
// picker_compute_ready_go_presentation (§2.6) is declared in
// picker_sdl_defs.h and defined in picker_team_build.cpp.
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

// §9.2 note-row state: Hidden while any company exists; Disabled (the
// engine's greyed-out inert-box grammar — GREY face, bevel intact,
// keyboard-dead, click no-ops with the disabled_row_click TRACE) when none
// does. Never Visible, so the MenuSpecRow action can never dispatch.
RowState main_menu_no_company_note_state(const MenuLabelContext& /*context*/)
{
    return g_main_menu_company_view.present ? RowState::Hidden
                                            : RowState::Disabled;
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
    // Opens the §2.3 Company List engine screen (CreateLoadMenu intercept ->
    // LoadGame -> show_company_list). Gated on company existence with
    // CONTINUE.
    {.id = "load_company", .label = "LOAD",
     .x = 152, .y = 75, .w = 68, .h = 20,
     .action = ButtonAction::CreateLoadMenu, .arg = 0,
     .nav = {.up = 0, .down = 4, .left = 1},
     .gate = {.gate = MenuGate::Custom, .custom = &main_menu_company_present}},
    // §9.2 (appended at the table END): the no-company note box — a greyed
    // Disabled row filling the exact CONTINUE|LOAD envelope while no company
    // exists, Hidden once one does. Inert chrome: MenuSpecRow arg = own
    // materialized ordinal (never dispatched — the row is never Visible),
    // no nav links in or out, statically hidden (companies present is the
    // default view). Replaces the old no-company bottom caption.
    {.id = "no_company_note", .label = "NO COMPANY YET",
     .x = 80, .y = 75, .w = 140, .h = 20,
     .action = ButtonAction::MenuSpecRow, .arg = 12,
     .state_override = &main_menu_no_company_note_state,
     .hidden = true},
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
    // §9.2 no-MP note box (see the MP table): materialized ordinal 7.
    {.id = "no_company_note", .label = "NO COMPANY YET",
     .x = 80, .y = 75, .w = 140, .h = 20,
     .action = ButtonAction::MenuSpecRow, .arg = 7,
     .state_override = &main_menu_no_company_note_state,
     .hidden = true},
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
    // active/most-recent company CONTINUE would open. Drawn after the
    // re-vdisplay so it wins the strip's own y-band. No-company state: the
    // §9.2 no_company_note Disabled row in the CONTINUE|LOAD envelope
    // replaces the old bottom caption — nothing is drawn here.
    if (g_main_menu_company_view.present) {
        std::string name = g_main_menu_company_view.display_name;
        if (name.size() > kMainMenuCompanyNameClip)
            name.resize(kMainMenuCompanyNameClip);
        std::string caption = "COMPANY: " + name;
        if (caption.size() > 28)
            caption.resize(28);
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

// The classic picker frame: clear, then the tiled backdrop (the shape every
// team-build-family loop drew per frame).
void picker_backdrop_draw_background(void* /*screen_state*/)
{
    og::runtime::current_session->myscreen_->clearbuffer();
    draw_backdrop();
}

// The VIEW TEAM screen and the SAVE/LOAD slot menus are RETIRED (§2.5/§3.8):
// the base camp's command roster IS the team view now, and saving is
// automatic on every base-camp mutation + level win — no slot UI remains.

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
    // §2.7 cross-control toggle: reuses the guy-row slot that is vacant when
    // networked (guy_prev/next/team are local-only — the same-rect
    // mutually-exclusive-gate pattern as the base camp's GO/READY pair).
    // Visible to ALL peers when networked (a mode that changes a client's
    // own rights must be visible to that client, §8 resolution 6);
    // host-only actionable — the MenuSpecRow dispatch popups for non-hosts.
    {.id = "cross_control", .label = "CTRL: OWN",
     .x = 150, .y = 146, .w = 70, .h = 12,
     .action = ButtonAction::MenuSpecRow, .arg = kTeamsMenuCrossControlIndex,
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
        // §2.7: the cross-control row is the screen's one MenuSpecRow (G3).
        .on_spec_row = &picker_teams_menu_engine_on_spec_row,
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
// RENAME / DETAILS.. header row, the team cycler, ACCEPT / BACK (the VIEW
// TEAM door retired with its screen — §2.5). The stat panels and the live
// team-cycler label write (kTrainMenuChangeTeamIndex — the G8 sweep of the
// raw allbuttons_[18] writes) stay in the content hook. Nested submenus
// (DETAILS, RENAME) return MENU_REDRAW for reset_buttons to consume
// (exit_on_redraw stays false); a remote start propagates MENU_EXIT, and
// the wrapper folds the exit exactly as the legacy loop did.

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
     .nav = {.down = 3, .left = 0, .right = 15}},
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
     .nav = {.up = 10, .down = 18, .right = 13},
     .art_family = FAMILY_MINUS},
    {.id = "inc_level", .label = "",
     .x = 126, .y = 145, .w = 16, .h = 12,
     .action = ButtonAction::IncreaseStat, .arg = BUT_LEVEL,
     .nav = {.up = 11, .down = 14, .left = 12,
             .right = kTrainMenuChangeTeamIndex},
     .art_family = FAMILY_PLUS},
    // The VIEW TEAM door retired with its screen (§2.5: the base-camp
    // roster IS the team view, one BACK away) — rows past the stat pairs
    // shifted down one; kTrainMenuChangeTeamIndex is the pinned anchor.
    {.id = "accept", .label = "ACCEPT",
     .x = 80, .y = 170, .w = 80, .h = 20,
     .action = ButtonAction::EditGuy, .arg = -1,
     .nav = {.up = 13, .left = 18}},
    {.id = "rename", .label = "RENAME",
     .x = 174, .y = 8, .w = 64, .h = 22,
     .action = ButtonAction::NameGuy, .arg = 1,
     .nav = {.down = kTrainMenuChangeTeamIndex, .left = 1, .right = 16}},
    {.id = "details", .label = "DETAILS..",
     .x = 240, .y = 8, .w = 64, .h = 22,
     .action = ButtonAction::CreateDetailMenu, .arg = 0,
     .nav = {.down = kTrainMenuChangeTeamIndex, .left = 15}},
    {.id = "change_team", .label = "Playing on Team X",
     .x = 174, .y = 138, .w = 133, .h = 22,
     .action = ButtonAction::ChangeTeam, .arg = 1,
     .nav = {.up = 16, .down = 14, .left = 13},
     .state_override = &train_change_team_row_state},
    {.id = "back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 10, .y = 170, .w = 40, .h = 20,
     .action = ButtonAction::ReturnMenu, .arg = MENU_EXIT,
     .nav = {.up = 12, .right = 14}},
};

static_assert(std::size(kTrainMenuRows) == 19,
              "train table anchor: change_team sits at "
              "kTrainMenuChangeTeamIndex");

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
// TEAM BUILD -> BASE CAMP (§2.5): the command roster. The roster IS the
// default view (the VIEW TEAM screen retired into it): 12 rows/page at 12px
// pitch — a deploy toggle (8,32+12r,14,10) and a per-row TRAIN button
// (272,32+12r,40,10) — the page cluster top-right (< p/N >), and the bottom
// command strip BACK | HIRE | SCENARIO | NETWORK | GO at y=178. SAVE/LOAD
// left the base camp (§3.8: saving is automatic on every mutation).
// Roster and pager rows dispatch through ButtonAction::MenuSpecRow (G3,
// arg == spec ordinal); the strip keeps the legacy ButtonActions so do_call
// routing, the GO -> StartGame interception, and interact("go") survive
// unchanged. Per-frame full-graph rewire (pattern b) over the installed
// screen state. The §2.6 dual-role slot: GO (host) and its same-rect READY
// twin (networked joiner) are the two host/joiner-gated buttons — exactly
// one is visible per frame.

// The company-list seam pattern: the per-frame rewire reads this file-static
// pointer; run_menu_screen's screen_state points at the SAME object.
BaseCampScreenState* g_base_camp_state = nullptr;

constexpr int kBaseCampRowY0 = 32;
constexpr int kBaseCampRowPitch = 12;

// §2.6 per-frame face/label bindings for the GO/READY slot: the engine's
// label-sync pass re-derives BOTH surfaces from the lobby state every frame
// (a click's optimistic flip shows the same frame; the server echo wins on
// the next poll — the ready-trap contract's re-derivation rule). Solo/local
// resolves to state 1: label "GO", face 13 — byte-identical to the plain
// bevel (pinned).
unsigned char base_camp_go_face_color(const MenuLabelContext& /*context*/)
{
    const ReadyGoPresentation p = picker_compute_ready_go_presentation();
    switch (p.state) {
    case ReadyGoState::LocalGo:
    case ReadyGoState::LocalGoNoDeploy:
    case ReadyGoState::HostGated:
    case ReadyGoState::HostGo:
        return p.face_color;
    case ReadyGoState::ClientUnready:
    case ReadyGoState::ClientReady:
        break;  // GO is hidden on joiners; keep the default face.
    }
    return kReadyGoFaceGrey;
}

std::string base_camp_ready_label(const MenuLabelContext& /*context*/)
{
    const ReadyGoPresentation p = picker_compute_ready_go_presentation();
    if (p.state == ReadyGoState::ClientUnready ||
        p.state == ReadyGoState::ClientReady)
    {
        return p.label;
    }
    return "READY";  // hidden on hosts/solo; the static default.
}

unsigned char base_camp_ready_face_color(const MenuLabelContext& /*context*/)
{
    const ReadyGoPresentation p = picker_compute_ready_go_presentation();
    if (p.state == ReadyGoState::ClientUnready ||
        p.state == ReadyGoState::ClientReady)
    {
        return p.face_color;
    }
    return kReadyGoFaceUnready;
}

// Static nav encodes the full-page shape (12 visible rows, pagers hidden,
// GO visible); the per-frame rewire recomputes every link from the live
// state anyway (§2.5 keyboard-nav pattern b).
#define OG_BASE_CAMP_DEP(i)                                                  \
    {.id = "roster_dep_" #i, .label = "",                                    \
     .x = 8, .y = kBaseCampRowY0 + kBaseCampRowPitch * (i), .w = 14,          \
     .h = 10, .action = ButtonAction::MenuSpecRow, .arg = (i),               \
     .nav = {.up = (i) > 0 ? (i) - 1 : -1,                                    \
             .down = (i) < 11 ? (i) + 1 : kCreateMenuBackIndex,               \
             .right = kBaseCampTrainBase + (i)}}
#define OG_BASE_CAMP_TRAIN(i)                                                \
    {.id = "roster_train_" #i, .label = "TRAIN",                             \
     .x = 272, .y = kBaseCampRowY0 + kBaseCampRowPitch * (i), .w = 40,        \
     .h = 10, .action = ButtonAction::MenuSpecRow,                           \
     .arg = kBaseCampTrainBase + (i),                                        \
     .nav = {.up = (i) > 0 ? kBaseCampTrainBase + (i) - 1 : -1,               \
             .down = (i) < 11 ? kBaseCampTrainBase + (i) + 1                  \
                              : kCreateMenuGoIndex,                           \
             .left = (i)}}

constexpr MenuButtonSpec kBaseCampRows[] = {
    OG_BASE_CAMP_DEP(0), OG_BASE_CAMP_DEP(1), OG_BASE_CAMP_DEP(2),
    OG_BASE_CAMP_DEP(3), OG_BASE_CAMP_DEP(4), OG_BASE_CAMP_DEP(5),
    OG_BASE_CAMP_DEP(6), OG_BASE_CAMP_DEP(7), OG_BASE_CAMP_DEP(8),
    OG_BASE_CAMP_DEP(9), OG_BASE_CAMP_DEP(10), OG_BASE_CAMP_DEP(11),
    OG_BASE_CAMP_TRAIN(0), OG_BASE_CAMP_TRAIN(1), OG_BASE_CAMP_TRAIN(2),
    OG_BASE_CAMP_TRAIN(3), OG_BASE_CAMP_TRAIN(4), OG_BASE_CAMP_TRAIN(5),
    OG_BASE_CAMP_TRAIN(6), OG_BASE_CAMP_TRAIN(7), OG_BASE_CAMP_TRAIN(8),
    OG_BASE_CAMP_TRAIN(9), OG_BASE_CAMP_TRAIN(10), OG_BASE_CAMP_TRAIN(11),
    // Page cluster (§2.5 header line B right edge); real MenuSpecRow pager
    // actions (keyboard-live), hidden until the roster spans pages.
    {.id = "roster_page_prev", .label = "<",
     .x = 263, .y = 11, .w = 14, .h = 10,
     .action = ButtonAction::MenuSpecRow, .arg = kBaseCampPagePrevIndex,
     .nav = {.down = kBaseCampTrainBase, .right = kBaseCampPageNextIndex},
     .hidden = true},
    {.id = "roster_page_next", .label = ">",
     .x = 302, .y = 11, .w = 14, .h = 10,
     .action = ButtonAction::MenuSpecRow, .arg = kBaseCampPageNextIndex,
     .nav = {.down = kBaseCampTrainBase, .left = kBaseCampPagePrevIndex},
     .hidden = true},
    // Bottom command strip (y=178, 18px tall). BACK keeps the Escape hotkey
    // (the shared cancel grammar).
    {.id = "back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 8, .y = 178, .w = 44, .h = 18,
     .action = ButtonAction::ReturnMenu, .arg = MENU_EXIT,
     .nav = {.up = 11, .right = kCreateMenuHireIndex}},
    {.id = "hire_troops", .label = "HIRE",
     .x = 58, .y = 178, .w = 50, .h = 18,
     .action = ButtonAction::CreateHireMenu, .arg = -1,
     .nav = {.up = 11, .left = kCreateMenuBackIndex,
             .right = kCreateMenuScenarioIndex}},
    {.id = "scenario", .label = "SCENARIO",
     .x = 114, .y = 178, .w = 62, .h = 18,
     .action = ButtonAction::CreateScenarioMenu, .arg = -1,
     .nav = {.up = kBaseCampTrainBase + 11, .left = kCreateMenuHireIndex,
             .right = kCreateMenuNetworkingIndex}},
    {.id = "networking", .label = "NETWORK",
     .x = 182, .y = 178, .w = 56, .h = 18,
     .action = ButtonAction::Networking, .arg = -1,
     .nav = {.up = kBaseCampTrainBase + 11, .left = kCreateMenuScenarioIndex,
             .right = kCreateMenuGoIndex}},
    // §2.6 GO half of the dual-role slot: solo/local keeps the plain grey
    // bevel and the exact legacy behavior (GoMenu -> go_menu / the
    // TeamBuild-scope StartGame interception — pinned byte-identical);
    // networked hosts get the state-3/4 face via the color binding.
    {.id = "go", .label = "GO",
     .x = 244, .y = 178, .w = 68, .h = 18,
     .action = ButtonAction::GoMenu, .arg = -1,
     .nav = {.up = kBaseCampTrainBase + 11,
             .left = kCreateMenuNetworkingIndex},
     .color = &base_camp_go_face_color},
    // §2.6 READY twin: the SAME rect, visible exactly when GO is not
    // (networked joiner). Keeps the existing ToggleLobbyReady action (§8
    // resolution 1 — interact("go") and the StartGame interception survive
    // untouched); label/face re-derive per frame from the lobby state.
    {.id = "ready", .label = "READY",
     .x = 244, .y = 178, .w = 68, .h = 18,
     .action = ButtonAction::ToggleLobbyReady, .arg = kCreateMenuReadyIndex,
     .nav = {.up = kBaseCampTrainBase + 11,
             .left = kCreateMenuNetworkingIndex},
     .label_binding = {.formatter = &base_camp_ready_label},
     .color = &base_camp_ready_face_color,
     .hidden = true},
};

#undef OG_BASE_CAMP_DEP
#undef OG_BASE_CAMP_TRAIN

static_assert(static_cast<int>(std::size(kBaseCampRows))
                  == kCreateMenuButtonCount,
              "base camp spec ordinals are the layout contract");

// Per-frame visibility + labels + nav over the live roster state (pattern
// b): page-window the rows, stamp the deploy glyphs on BOTH label surfaces,
// host-gate GO, close the strip/pager links, and seed the empty-roster
// highlight on HIRE (§2.5). Networked ownership shape: a foreign row's
// deploy button widens into the §2.5 no_draw hit zone (8,y,212,10) — click
// anywhere on the row pops OWNED BY — and its TRAIN button hides; the nav
// graph chains the train column over the OWNED rows only. Solo/local (all
// rows owned) reproduces the stage-1 graph byte-for-byte.
void base_camp_rewire(button* buttons, int count, int& highlighted_button)
{
    if (buttons == nullptr || count < kCreateMenuButtonCount)
        return;
    const BaseCampScreenState* st = g_base_camp_state;
    const int first = st != nullptr ? st->page.first_index() : 0;
    const int end = st != nullptr ? st->page.end_index() : 0;
    const int visible = std::max(0, end - first);
    const bool pagers = st != nullptr && st->page.multi_page();
    const bool host_visible = picker_lobby_host_controls_visible();
    // §2.6: the GO/READY pair shares one rect with mutually exclusive gates
    // — the host keeps GO, a networked joiner gets READY, never both. A
    // non-host peer implies a networked session, so READY keys on both
    // (the gate-lattice sweep drives the (host=false, networked=true) shape).
    const bool ready_visible =
        picker_lobby_is_networked() && !host_visible;
    // The strip's right end: whichever of the pair is visible this frame.
    const int strip_end = host_visible
        ? kCreateMenuGoIndex
        : (ready_visible ? kCreateMenuReadyIndex : -1);
    const SaveData& save = og::runtime::current_session->myscreen_->save_data;

    // Ownership per visible row (own row => a real deploy toggle + TRAIN).
    std::array<bool, kBaseCampRosterRowsPerPage> owned_row{};
    for (int r = 0; r < kBaseCampRosterRowsPerPage; ++r) {
        if (r >= visible)
            continue;
        const BaseCampDisplaySlot& slot =
            st->slots[static_cast<std::size_t>(first + r)];
        owned_row[static_cast<std::size_t>(r)] = slot.owned;
    }
    const auto next_owned = [&](int from) {
        for (int r = from; r < visible; ++r)
            if (owned_row[static_cast<std::size_t>(r)])
                return r;
        return -1;
    };
    const auto prev_owned = [&](int from) {
        for (int r = from; r >= 0; --r)
            if (owned_row[static_cast<std::size_t>(r)])
                return r;
        return -1;
    };
    const int first_owned = next_owned(0);
    const int last_owned = prev_owned(visible - 1);

    for (int r = 0; r < kBaseCampRosterRowsPerPage; ++r) {
        const bool on = r < visible;
        const bool own = on && owned_row[static_cast<std::size_t>(r)];
        button& dep = buttons[r];
        button& train = buttons[kBaseCampTrainBase + r];
        dep.hidden = !on;
        train.hidden = !own;
        if (!on)
            continue;

        const BaseCampDisplaySlot& display =
            st->slots[static_cast<std::size_t>(first + r)];
        const guy* member = (own && display.save_slot >= 0 &&
                             display.save_slot < MAX_TEAM_SIZE)
            ? save.team_list[display.save_slot].get()
            : nullptr;
        // §2.5 deploy glyph: "X" deployed / "" benched on OWN rows; foreign
        // rows draw their X/- glyph in the content pass (the button is a
        // no_draw hit zone). Both surfaces (the descriptor row AND the live
        // vbutton), so a toggle shows this frame.
        dep.label = (member != nullptr && member->deployed) ? "X" : "";
        dep.no_draw = !own;
        dep.x = 8;
        dep.sizex = own ? 14 : 212;
        vbutton* live = og::runtime::current_session->allbuttons_[r];
        if (live != nullptr) {
            live->label = dep.label;
            live->no_draw = dep.no_draw;
            live->xloc = dep.x;
            live->width = dep.sizex;
            live->xend = live->xloc + live->width;
        }

        // Own rows step right into their TRAIN button (stage-1 shape); a
        // foreign hit zone spans the whole row, so its right-link carries
        // the train column's pager duty — without it an all-foreign page
        // (spectator machine, [NET-R9]) strands the pagers.
        dep.nav = {.up = r > 0 ? r - 1 : -1,
                   .down = r + 1 < visible ? r + 1 : kCreateMenuBackIndex,
                   .left = -1,
                   .right = own ? kBaseCampTrainBase + r
                                : (pagers ? kBaseCampPagePrevIndex : -1)};
        if (own) {
            const int up_owned = prev_owned(r - 1);
            const int down_owned = next_owned(r + 1);
            train.nav = {
                .up = up_owned >= 0 ? kBaseCampTrainBase + up_owned : -1,
                .down = down_owned >= 0
                    ? kBaseCampTrainBase + down_owned
                    : (strip_end >= 0 ? strip_end
                                      : kCreateMenuNetworkingIndex),
                .left = r,
                .right = pagers ? kBaseCampPagePrevIndex : -1};
        }
    }

    buttons[kBaseCampPagePrevIndex].hidden = !pagers;
    buttons[kBaseCampPageNextIndex].hidden = !pagers;
    // Pager down-links land on the train column's first OWNED row (the
    // stage-1 target) and fall back to the dep column on all-foreign pages.
    const int pager_down = first_owned >= 0
        ? kBaseCampTrainBase + first_owned
        : (visible > 0
               ? 0
               : (strip_end >= 0 ? strip_end : kCreateMenuNetworkingIndex));
    buttons[kBaseCampPagePrevIndex].nav = {
        .up = -1,
        .down = pager_down,
        .left = -1,
        .right = kBaseCampPageNextIndex};
    buttons[kBaseCampPageNextIndex].nav = {
        .up = -1,
        .down = pager_down,
        .left = kBaseCampPagePrevIndex,
        .right = -1};

    // §2.6: exactly one of the same-rect GO/READY pair shows — GO for the
    // host (the legacy host-gate, verbatim rule), READY for a networked
    // joiner (incl. the [NET-R9] spectator machine).
    buttons[kCreateMenuGoIndex].hidden = !host_visible;
    buttons[kCreateMenuReadyIndex].hidden = !ready_visible;

    const int dep_last = visible > 0 ? visible - 1 : -1;
    // The strip's right-side up-links prefer the train column (stage-1
    // shape) and fall back to the dep column when this page has no owned
    // row (all-foreign page / spectator machine, [NET-R9]).
    const int train_last =
        last_owned >= 0 ? kBaseCampTrainBase + last_owned : dep_last;
    buttons[kCreateMenuBackIndex].nav = {
        .up = dep_last, .down = -1, .left = -1,
        .right = kCreateMenuHireIndex};
    buttons[kCreateMenuHireIndex].nav = {
        .up = dep_last, .down = -1, .left = kCreateMenuBackIndex,
        .right = kCreateMenuScenarioIndex};
    buttons[kCreateMenuScenarioIndex].nav = {
        .up = train_last, .down = -1, .left = kCreateMenuHireIndex,
        .right = kCreateMenuNetworkingIndex};
    buttons[kCreateMenuNetworkingIndex].nav = {
        .up = train_last, .down = -1, .left = kCreateMenuScenarioIndex,
        .right = strip_end};
    buttons[kCreateMenuGoIndex].nav = {
        .up = train_last, .down = -1,
        .left = kCreateMenuNetworkingIndex, .right = -1};
    buttons[kCreateMenuReadyIndex].nav = {
        .up = train_last, .down = -1,
        .left = kCreateMenuNetworkingIndex, .right = -1};

    // §2.6 per-frame presentation stamp on the visible half of the pair —
    // BOTH surfaces, so the entry compose and the layout-test path (which
    // drive the rewire without the runner's label pass) show the real
    // state; the engine label/color bindings re-derive the same values
    // after every dispatch. Solo stamps label "GO" / face 13: byte-identical
    // to the untouched legacy button (pinned).
    const ReadyGoPresentation ready_go =
        picker_compute_ready_go_presentation();
    if (host_visible) {
        vbutton* go_live =
            og::runtime::current_session->allbuttons_[kCreateMenuGoIndex];
        if (go_live != nullptr)
            go_live->color = ready_go.face_color;
    } else if (ready_visible) {
        buttons[kCreateMenuReadyIndex].label = ready_go.label;
        vbutton* ready_live =
            og::runtime::current_session->allbuttons_[kCreateMenuReadyIndex];
        if (ready_live != nullptr) {
            ready_live->label = ready_go.label;
            ready_live->color = ready_go.face_color;
        }
    }

    // Empty roster: seed the highlight on HIRE (§2.5) instead of the first
    // visible button.
    if (visible == 0 && highlighted_button >= 0
        && highlighted_button < count
        && buttons[highlighted_button].hidden)
    {
        highlighted_button = kCreateMenuHireIndex;
    }

    for (int i = 0; i < count; ++i)
        sync_button_hidden_state(buttons, i);
    ensure_highlighted_button_visible(buttons, count, highlighted_button);
}

// Pre-buttons pass (§2.0 draw-order rule): backdrop, then the readability
// bars behind the column-header line and each visible roster row.
void base_camp_draw_background(void* /*screen_state*/)
{
    picker_backdrop_draw_background(nullptr);
    screen* const game = og::runtime::current_session->myscreen_;
    const BaseCampScreenState* st = g_base_camp_state;
    const int visible =
        st != nullptr ? std::max(0, st->page.end_index() -
                                        st->page.first_index())
                      : 0;
    game->draw_rect_filled(4, 23, 312, 9, PURE_BLACK, 150);
    for (int r = 0; r < visible; ++r) {
        game->draw_rect_filled(4, kBaseCampRowY0 - 1 + kBaseCampRowPitch * r,
                               312, kBaseCampRowPitch - 1, PURE_BLACK, 150);
    }
}

// The §2.5 content pass (after draw_buttons): header lines A/B, the page
// indicator, the column headers, the roster row text/family chips, and the
// empty state.
void base_camp_draw_content(void* screen_state)
{
    const BaseCampScreenState* st =
        static_cast<const BaseCampScreenState*>(screen_state);
    screen* const game = og::runtime::current_session->myscreen_;
    const SaveData& save = game->save_data;
    text& mytext = game->text_normal;

    const auto strip_text = [game](int x, int y, const std::string& value,
                                   unsigned char color) {
        if (value.empty())
            return;
        const int width = static_cast<int>(value.size()) * 6;
        game->draw_rect_filled(x - 2, y - 1, width + 4, 8, PURE_BLACK, 150);
        game->text_normal.write_xy(x, y, color, "%s", value.c_str());
    };

    // Line A: company name (the 40-byte save_name) + the gold block.
    std::string company = save.save_name;
    if (company.size() > 26)
        company.resize(26);
    strip_text(8, 3, company, WHITE);
    strip_text(246, 3, format_base_camp_gold_label(save), YELLOW);

    // Line B: solo scenario/deploy header, or the §2.5 networked
    // READY n/m + DEP n/m + SCEN header (34-char budget). READY counts
    // non-host MACHINES; DEP counts the merged display list.
    const bool networked = picker_lobby_is_networked();
    std::string line_b;
    unsigned char line_b_color = WHITE;
    if (networked) {
        // Degraded-link alert (join connecting/failed/lost, host relay
        // drop): the READY/DEP header would be dead placeholder data, so
        // the alert takes over the §2.5 line-B slot until the link heals.
        // This is the base-camp home of the lobby clients' status lines —
        // the pre-reshape team-build screen drew them at the same spot.
        const std::optional<std::string> alert =
            picker_lobby_connection_alert();
        if (alert.has_value()) {
            line_b = *alert;
            line_b_color = static_cast<unsigned char>(ORANGE_START);
        } else {
            line_b = format_base_camp_net_line(
                count_base_camp_ready_machines(picker_lobby_players()),
                st != nullptr
                    ? count_base_camp_display_deploys(st->slots, save)
                    : BaseCampDeployCounts{},
                save.scen_num);
        }
    } else {
        line_b = format_base_camp_scen_line(save, game->world().title);
    }
    strip_text(8, 13, line_b, line_b_color);

    if (st != nullptr && st->page.multi_page())
        strip_text(283, 13, st->page.indicator(), WHITE);

    // Column headers over the pre-pass bar: solo keeps CLASS/EXP; networked
    // swaps in the 16-char COMPANY column (§2.5 U7 — CLASS is carried by
    // the family chip + family-colored name).
    if (networked) {
        mytext.write_xy(40, 24, "NAME", WHITE, 1);
        mytext.write_xy(106, 24, "COMPANY", WHITE, 1);
        mytext.write_xy(208, 24, "LV", WHITE, 1);
        mytext.write_xy(226, 24, "HP", WHITE, 1);
        mytext.write_xy(274, 24, "TRAIN", WHITE, 1);
    } else {
        mytext.write_xy(2, 24, "DEPLOY", WHITE, 1);
        mytext.write_xy(40, 24, "NAME", WHITE, 1);
        mytext.write_xy(118, 24, "CLASS", WHITE, 1);
        mytext.write_xy(172, 24, "LV", WHITE, 1);
        mytext.write_xy(196, 24, "HP", WHITE, 1);
        mytext.write_xy(226, 24, "EXP", WHITE, 1);
        mytext.write_xy(274, 24, "TRAIN", WHITE, 1);
    }

    const int first = st != nullptr ? st->page.first_index() : 0;
    const int end = st != nullptr ? st->page.end_index() : 0;
    if (end - first <= 0) {
        mytext.write_xy(73, 90, "NO SOLDIERS - HIRE YOUR FIRST",
                        static_cast<unsigned char>(ORANGE_START), 1);
        return;
    }

    for (int r = 0; r < end - first; ++r) {
        const BaseCampDisplaySlot& display =
            st->slots[static_cast<std::size_t>(first + r)];
        const int y = kBaseCampRowY0 + kBaseCampRowPitch * r;

        // Row source: own rows read the LIVE private save member; foreign
        // rows render their replicated wire copy.
        const guy* member = nullptr;
        std::unique_ptr<guy> foreign_guy;
        bool deployed = display.deployed;
        if (display.owned) {
            if (display.save_slot < 0 || display.save_slot >= MAX_TEAM_SIZE ||
                !save.team_list[display.save_slot])
            {
                continue;
            }
            member = save.team_list[display.save_slot].get();
            deployed = member->deployed;
        } else {
            foreign_guy = make_base_camp_display_guy(display.character);
            member = foreign_guy.get();
        }

        // Family chip: the view_team color convention ((family+1)<<4).
        const unsigned char family_color =
            static_cast<unsigned char>(((member->family + 1) << 4) & 255);
        game->fastbox(26, y, 10, 10, family_color);

        // Benched rows dim to GREY — the second deploy cue (§2.5).
        const unsigned char name_color =
            deployed ? family_color : static_cast<unsigned char>(GREY);
        const unsigned char stat_color =
            deployed ? static_cast<unsigned char>(WHITE)
                     : static_cast<unsigned char>(GREY);

        const int derived_hp =
            static_cast<int>(picker_compute_guy_derived_stats(*member).hp);
        if (networked) {
            // Foreign rows have no deploy BUTTON (no_draw hit zone): their
            // deploy state draws as the §2.5 X/- glyph at x=11.
            if (!display.owned)
                mytext.write_xy(11, y + 1, deployed ? "X" : "-", stat_color,
                                1);
            const BaseCampNetRowText row = format_base_camp_net_row(
                member->name, display.company, member->level, derived_hp);
            mytext.write_xy(40, y + 1, row.name.c_str(), name_color, 1);
            mytext.write_xy(106, y + 1, row.company.c_str(), stat_color, 1);
            mytext.write_xy(208, y + 1, row.level.c_str(), stat_color, 1);
            mytext.write_xy(226, y + 1, row.hp.c_str(), stat_color, 1);
        } else {
            const BaseCampRowText row =
                format_base_camp_row(*member, derived_hp);
            mytext.write_xy(40, y + 1, row.name.c_str(), name_color, 1);
            mytext.write_xy(118, y + 1, row.cls.c_str(), name_color, 1);
            mytext.write_xy(172, y + 1, row.level.c_str(), stat_color, 1);
            mytext.write_xy(196, y + 1, row.hp.c_str(), stat_color, 1);
            mytext.write_xy(226, y + 1, row.exp.c_str(), stat_color, 1);
        }
    }
}

// The team-build family's frame obligations: the level-reload guard (a SET
// LEVEL pick or a host sync must reload the picker world) plus the §3.3
// positional refresh of the roster rows.
bool base_camp_frame_tick(void* screen_state, int /*frame*/)
{
    if (screen_state == nullptr)
        return true;
    auto* const state = static_cast<BaseCampScreenState*>(screen_state);
    screen* const myscreen = og::runtime::current_session->myscreen_;
    if (state->last_level_id != myscreen->save_data.scen_num ||
        state->was_reset)
    {
        state->was_reset = false;
        state->last_level_id = myscreen->save_data.scen_num;
        myscreen->world().id = state->last_level_id;
        myscreen->load_level();
    }
    base_camp_refresh_rows(*state);
    return true;
}

void base_camp_on_reset(void* screen_state)
{
    if (screen_state == nullptr)
        return;
    auto* const state = static_cast<BaseCampScreenState*>(screen_state);
    state->was_reset = true;
    // A nested screen (hire/train) may have changed the roster: re-collect
    // before the next rewire runs (§3.3).
    base_camp_refresh_rows(*state);
}

// G3 row dispatch: deploy toggles, per-row TRAIN, and the pagers.
Sint32 base_camp_on_spec_row(int row, void* screen_state)
{
    auto* const st = static_cast<BaseCampScreenState*>(screen_state);
    if (st == nullptr)
        return 0;

    if (row == kBaseCampPagePrevIndex || row == kBaseCampPageNextIndex) {
        if (st->page.step(row == kBaseCampPagePrevIndex ? -1 : 1))
            TRACE("basecamp", "page %s", st->page.indicator().c_str());
        return MENU_OK;
    }

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const bool is_train = row >= kBaseCampTrainBase;
    const int visual_row = is_train ? row - kBaseCampTrainBase : row;
    const int idx = st->page.first_index() + visual_row;
    if (idx < 0 || idx >= static_cast<int>(st->slots.size()))
        return 0;  // stale click on a row hidden this frame
    const BaseCampDisplaySlot& display =
        st->slots[static_cast<std::size_t>(idx)];

    if (!display.owned) {
        // §2.5 ownership rule (U9): a foreign row is read-only — any click
        // on its hit zone names the owning machine's FULL company (the
        // escape hatch for origin-column prefix collisions).
        TRACE("basecamp", "owned_by row=%d company=%s", idx,
              display.company.c_str());
        popup_dialog("OWNED BY",
                     display.company.empty() ? "ANOTHER COMPANY"
                                             : display.company.c_str());
        return MENU_OK;
    }

    const int slot = display.save_slot;
    if (slot < 0 || slot >= MAX_TEAM_SIZE || !save.team_list[slot])
        return 0;

    if (!is_train) {
        // §2.0 U6 roster-row tap debounce: a second toggle of the SAME row
        // (same display index resolving to the same save slot — a page flip
        // in between retargets the rect and is never debounced) within
        // 250 ms is silently ignored. Touch mistaps double-fire the
        // pending-click queue, and every accepted double-toggle would be a
        // spurious §4.3 MP ready-clear. Only ACCEPTED toggles stamp.
        const std::int64_t now_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
        constexpr std::int64_t kDeployToggleDebounceMs = 250;
        if (idx == st->last_deploy_toggle_idx &&
            slot == st->last_deploy_toggle_slot &&
            st->last_deploy_toggle_ms >= 0 &&
            now_ms - st->last_deploy_toggle_ms < kDeployToggleDebounceMs) {
            TRACE("basecamp", "deploy_debounced slot=%d", slot);
            return MENU_OK;
        }
        // §2.5 client-side 24-cap guard: a toggle-ON that would exceed 24
        // deployed across the merged lobby roster is denied with a popup,
        // pre-empting the server's §4.2 force-bench path.
        if (picker_lobby_is_networked() && !save.team_list[slot]->deployed) {
            const BaseCampDeployCounts counts =
                count_base_camp_display_deploys(st->slots, save);
            if (counts.deployed >= MAX_TEAM_SIZE) {
                TRACE("basecamp", "deploy_cap_denied deployed=%d",
                      counts.deployed);
                popup_dialog("DEPLOY LIMIT 24",
                             "24 characters are\nalready deployed");
                return MENU_OK;
            }
        }
        // §2.5 deploy toggle (flow 5): flip + dim + DEP count same frame;
        // §3.8 autosave + §4.3 ready-clear ride the shared mutation tail.
        [[maybe_unused]] const bool deployed = toggle_deploy_slot(save, slot);
        TRACE("basecamp", "deploy slot=%d %s", slot,
              deployed ? "on" : "off");
        st->last_deploy_toggle_idx = idx;
        st->last_deploy_toggle_slot = slot;
        st->last_deploy_toggle_ms = now_ms;
        picker_base_camp_after_roster_mutation();
        return MENU_OK;
    }

    // §2.5 per-row TRAIN (flow 4): open the train screen directly on this
    // character (the nested-engine-screen pattern). A remote start that
    // unparked the train screen propagates; BACK re-inits our buttons.
    picker_set_train_seed_slot(slot);
    TRACE("basecamp", "train slot=%d", slot);
    const Sint32 ret = create_train_menu(0);
    if ((ret & MENU_EXIT) && team_build_start_selected())
        return MENU_EXIT;
    return MENU_REDRAW;
}

const MenuScreenSpec& team_build_menu_screen_spec()
{
    static const MenuScreenSpec spec{
        .name = "team_build",
        .rows = kBaseCampRows,
        .row_count = static_cast<int>(std::size(kBaseCampRows)),
        .buttons_accessor = &picker_createmenu_buttons,
        .count_accessor = &picker_createmenu_button_count,
        .nav = {.kind = NavProgramKind::Rewire, .rewire = &base_camp_rewire},
        // The loop-top host-GO check that launches a joiner parked here.
        .remote_start = RemoteStartScope::TeamBuildScope,
        .remote_start_exit = RemoteStartExit::ReturnMenuExit,
        .enter = EnterTransition::FadeAroundEntry,
        .default_highlight = 0,  // roster-first: row 0's deploy toggle
        .polls_lobby = true,
        .draw_background = &base_camp_draw_background,
        .draw_content = &base_camp_draw_content,
        .frame_tick = &base_camp_frame_tick,
        .on_reset = &base_camp_on_reset,
        .on_spec_row = &base_camp_on_spec_row,
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

// §9.3 sunken input field: the name strip's face is stamped PURE_BLACK every
// frame (the §2.6 color-binding mechanism — face only, the grey bevel edges
// remain), reading as an inset DOS text field behind the WHITE name. Fixes
// the recorded WHITE-on-face-13 contrast fail.
unsigned char name_entry_value_face_color(const MenuLabelContext& /*context*/)
{
    return PURE_BLACK;
}

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
     .nav = {.down = kNameEntryRerollIndex},
     .color = &name_entry_value_face_color},
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
    // §9.3: the slug preview is gone (F2 — the filename teaches nothing);
    // the freed slot carries a GREY hint on a U2 black strip teaching the
    // edit affordance instead (the secondary voice).
    static constexpr const char kNameEntryHint[] = "CLICK THE NAME TO EDIT IT";
    constexpr int kHintInk =
        static_cast<int>(sizeof(kNameEntryHint) - 1) * 6 - 1;
    game->draw_rect_filled(kNameEntryCenterX - kHintInk / 2 - 2, 125,
                           kHintInk + 4, 8, PURE_BLACK, 150);
    game->text_normal.write_xy_center(kNameEntryCenterX, 126, GREY, "%s",
                                      kNameEntryHint);
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

// ---------------------------------------------------------------------------
// §2.3 Company List (Load) — Layer F engine screen
// ---------------------------------------------------------------------------

// The state the per-frame rewire reads (the rewire signature carries no
// screen_state, the view-scenario file-static precedent). run_menu_screen's
// screen_state points at the SAME object, installed by the wrapper (or by a
// test) around the run. Null state renders the empty-list shape: every row
// and pager hidden, BACK alone — what a bare engine sweep sees.
CompanyListScreenState* g_company_list_state = nullptr;

// §2.3 geometry: 10 rows at the save-slot pitch, each a (row, BK, X) triple.
// Table order groups by kind — rows 0-9, BK 10-19, X 20-29, then the chrome —
// so the MenuSpecRow arg (== the spec ordinal, G3) decodes as arg/10 = kind,
// arg%10 = visual row.
constexpr int kCompanyListRowsPerPage = 10;
constexpr int kCompanyListBakBase = 10;
constexpr int kCompanyListDelBase = 20;
constexpr int kCompanyListBackIndex = 30;
constexpr int kCompanyListPrevIndex = 31;
constexpr int kCompanyListNextIndex = 32;

// One visual row's (row, BK, X) triple: rects straight from the §2.3 table —
// company_row (25,25+15i,190,10), BK (219,25+15i,20,10), X (243,25+15i,20,10).
// Static nav is the full-page multi-page shape (rows chain vertically,
// row.right -> BK -> X, row9.down -> BACK, X column bottoms out on PREV); the
// per-frame rewire recomputes every link from the live state anyway.
#define OG_COMPANY_LIST_ROW(i)                                               \
    {.id = "company_row_" #i, .label = "",                                   \
     .x = 25, .y = 25 + 15 * (i), .w = 190, .h = 10,                          \
     .action = ButtonAction::MenuSpecRow, .arg = (i),                        \
     .nav = {.up = (i) > 0 ? (i) - 1 : -1,                                    \
             .down = (i) < 9 ? (i) + 1 : kCompanyListBackIndex,               \
             .right = kCompanyListBakBase + (i)}}
#define OG_COMPANY_LIST_BAK(i)                                               \
    {.id = "company_bak_" #i, .label = "BK",                                 \
     .x = 219, .y = 25 + 15 * (i), .w = 20, .h = 10,                          \
     .action = ButtonAction::MenuSpecRow, .arg = kCompanyListBakBase + (i),  \
     .nav = {.up = (i) > 0 ? kCompanyListBakBase + (i) - 1 : -1,              \
             .down = (i) < 9 ? kCompanyListBakBase + (i) + 1                  \
                             : kCompanyListBackIndex,                         \
             .left = (i), .right = kCompanyListDelBase + (i)}}
#define OG_COMPANY_LIST_DEL(i)                                               \
    {.id = "company_del_" #i, .label = "X",                                  \
     .x = 243, .y = 25 + 15 * (i), .w = 20, .h = 10,                          \
     .action = ButtonAction::MenuSpecRow, .arg = kCompanyListDelBase + (i),  \
     .nav = {.up = (i) > 0 ? kCompanyListDelBase + (i) - 1 : -1,              \
             .down = (i) < 9 ? kCompanyListDelBase + (i) + 1                  \
                             : kCompanyListPrevIndex,                         \
             .left = kCompanyListBakBase + (i)}}

constexpr MenuButtonSpec kCompanyListRows[] = {
    OG_COMPANY_LIST_ROW(0), OG_COMPANY_LIST_ROW(1), OG_COMPANY_LIST_ROW(2),
    OG_COMPANY_LIST_ROW(3), OG_COMPANY_LIST_ROW(4), OG_COMPANY_LIST_ROW(5),
    OG_COMPANY_LIST_ROW(6), OG_COMPANY_LIST_ROW(7), OG_COMPANY_LIST_ROW(8),
    OG_COMPANY_LIST_ROW(9),
    OG_COMPANY_LIST_BAK(0), OG_COMPANY_LIST_BAK(1), OG_COMPANY_LIST_BAK(2),
    OG_COMPANY_LIST_BAK(3), OG_COMPANY_LIST_BAK(4), OG_COMPANY_LIST_BAK(5),
    OG_COMPANY_LIST_BAK(6), OG_COMPANY_LIST_BAK(7), OG_COMPANY_LIST_BAK(8),
    OG_COMPANY_LIST_BAK(9),
    OG_COMPANY_LIST_DEL(0), OG_COMPANY_LIST_DEL(1), OG_COMPANY_LIST_DEL(2),
    OG_COMPANY_LIST_DEL(3), OG_COMPANY_LIST_DEL(4), OG_COMPANY_LIST_DEL(5),
    OG_COMPANY_LIST_DEL(6), OG_COMPANY_LIST_DEL(7), OG_COMPANY_LIST_DEL(8),
    OG_COMPANY_LIST_DEL(9),
    // BACK to the main menu; Escape hotkey (the shared cancel grammar).
    {.id = "back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 10, .y = 170, .w = 44, .h = 20,
     .action = ButtonAction::MenuSpecRow, .arg = kCompanyListBackIndex,
     .nav = {.up = 9, .right = kCompanyListPrevIndex}},
    // Real MenuSpecRow pager actions (keyboard-live, §2.3); statically
    // hidden — the rewire shows them only when the list spans pages.
    {.id = "company_page_prev", .label = "PREV",
     .x = 220, .y = 170, .w = 40, .h = 20,
     .action = ButtonAction::MenuSpecRow, .arg = kCompanyListPrevIndex,
     .nav = {.up = 29, .left = kCompanyListBackIndex,
             .right = kCompanyListNextIndex},
     .hidden = true},
    {.id = "company_page_next", .label = "NEXT",
     .x = 270, .y = 170, .w = 40, .h = 20,
     .action = ButtonAction::MenuSpecRow, .arg = kCompanyListNextIndex,
     .nav = {.up = 29, .left = kCompanyListPrevIndex},
     .hidden = true},
};

#undef OG_COMPANY_LIST_ROW
#undef OG_COMPANY_LIST_BAK
#undef OG_COMPANY_LIST_DEL

// Per-frame visibility + nav over the live list state (pattern b: full-graph
// rewire recomputed every frame; BFS-pinned per visibility variant). Also
// stamps the §2.3 active-company marker — red do_outline (U4) — on the live
// row vbuttons, and re-asserts the pagers on both surfaces.
void company_list_rewire(button* buttons, int count, int& /*highlighted*/)
{
    if (count < kCompanyListNextIndex + 1)
        return;
    const CompanyListScreenState* st = g_company_list_state;
    const int first = st != nullptr ? st->page.first_index() : 0;
    const int end = st != nullptr ? st->page.end_index() : 0;
    const int visible = std::max(0, end - first);
    const bool pagers = st != nullptr && st->page.multi_page();
    const std::string& active_slot = og::data::active_company_slot();

    for (int r = 0; r < kCompanyListRowsPerPage; ++r) {
        const bool on = r < visible;
        buttons[r].hidden = !on;
        buttons[kCompanyListBakBase + r].hidden = !on;
        buttons[kCompanyListDelBase + r].hidden = !on;
        if (on) {
            buttons[r].nav = {
                .up = r > 0 ? r - 1 : -1,
                .down = r + 1 < visible ? r + 1 : kCompanyListBackIndex,
                .left = -1,
                .right = kCompanyListBakBase + r};
            buttons[kCompanyListBakBase + r].nav = {
                .up = r > 0 ? kCompanyListBakBase + r - 1 : -1,
                .down = r + 1 < visible ? kCompanyListBakBase + r + 1
                                        : kCompanyListBackIndex,
                .left = r,
                .right = kCompanyListDelBase + r};
            buttons[kCompanyListDelBase + r].nav = {
                .up = r > 0 ? kCompanyListDelBase + r - 1 : -1,
                .down = r + 1 < visible
                    ? kCompanyListDelBase + r + 1
                    : (pagers ? kCompanyListPrevIndex : kCompanyListBackIndex),
                .left = kCompanyListBakBase + r,
                .right = -1};
        }
        // §2.3 U4: the active company's row wears the red outline (the
        // player-count grammar). Live surface only — do_outline is a vbutton
        // field; null-guarded for accessor-only test paths.
        vbutton* live = og::runtime::current_session->allbuttons_[r];
        if (live != nullptr) {
            live->do_outline =
                (on && st != nullptr
                 && st->companies[static_cast<std::size_t>(first + r)].slot
                        == active_slot)
                    ? 1
                    : 0;
        }
    }
    buttons[kCompanyListPrevIndex].hidden = !pagers;
    buttons[kCompanyListNextIndex].hidden = !pagers;
    buttons[kCompanyListBackIndex].nav = {
        .up = visible > 0 ? visible - 1 : -1,
        .down = -1,
        .left = -1,
        .right = pagers ? kCompanyListPrevIndex : -1};
    buttons[kCompanyListPrevIndex].nav = {
        .up = visible > 0 ? kCompanyListDelBase + visible - 1 : -1,
        .down = -1,
        .left = kCompanyListBackIndex,
        .right = kCompanyListNextIndex};
    buttons[kCompanyListNextIndex].nav = {
        .up = visible > 0 ? kCompanyListDelBase + visible - 1 : -1,
        .down = -1,
        .left = kCompanyListPrevIndex,
        .right = -1};
    for (int i = 0; i < count; ++i)
        sync_button_hidden_state(buttons, i);
}

// The §2.3 content pass: title + column headers over the backdrop (the
// black-strip idiom, U2), the three row columns at x=27/141/155, the "p/N"
// indicator, and the empty state.
void company_list_draw_content(void* screen_state)
{
    const CompanyListScreenState* st =
        static_cast<const CompanyListScreenState*>(screen_state);
    screen* game = og::runtime::current_session->myscreen_;

    const auto strip_text = [game](int x, int y, const std::string& text) {
        const int width = static_cast<int>(text.size()) * 6;
        game->draw_rect_filled(x - 2, y - 1, width + 4, 8, PURE_BLACK, 150);
        game->text_normal.write_xy(x, y, WHITE, "%s", text.c_str());
    };

    const int total =
        st != nullptr ? static_cast<int>(st->companies.size()) : 0;
    strip_text(25, 8, format_company_list_title(total));
    strip_text(27, 16, "NAME");
    strip_text(135, 16, "GUYS");
    strip_text(155, 16, "PLAYED");
    strip_text(219, 16, "BKUP");
    strip_text(243, 16, "DEL");

    if (st == nullptr || total == 0) {
        // Transient shape: deleting the last company exits the screen, but
        // the frame that consumed the delete still draws once.
        game->text_normal.write_xy_center(160, 90, ORANGE_START, "%s",
                                          "NO COMPANIES");
        return;
    }

    const int first = st->page.first_index();
    const int end = st->page.end_index();
    for (int r = 0; r < end - first; ++r) {
        const CompanyRowText row = format_company_row(
            st->companies[static_cast<std::size_t>(first + r)]);
        const int y = 27 + 15 * r;
        game->text_normal.write_xy(27, y, DARK_BLUE, "%s", row.name.c_str());
        game->text_normal.write_xy(141, y, DARK_BLUE, "%s",
                                   row.roster.c_str());
        game->text_normal.write_xy(155, y, DARK_BLUE, "%s",
                                   row.played.c_str());
    }

    if (st->page.multi_page())
        strip_text(140, 176, st->page.indicator());
}

// G3 row dispatch: OPEN / BK / X per visual row, BACK, and the pagers.
Sint32 company_list_on_spec_row(int row, void* screen_state)
{
    CompanyListScreenState* st =
        static_cast<CompanyListScreenState*>(screen_state);
    if (st == nullptr)
        return 0;

    if (row == kCompanyListBackIndex) {
        TRACE("company_list", "back");
        return MENU_EXIT;
    }
    if (row == kCompanyListPrevIndex || row == kCompanyListNextIndex) {
        if (st->page.step(row == kCompanyListPrevIndex ? -1 : 1))
            TRACE("company_list", "page %s", st->page.indicator().c_str());
        return MENU_OK;
    }

    const int r = row % kCompanyListRowsPerPage;
    const int idx = st->page.first_index() + r;
    if (idx < 0 || idx >= static_cast<int>(st->companies.size()))
        return 0;  // stale click on a row hidden this frame
    const og::data::CompanyInfo info =
        st->companies[static_cast<std::size_t>(idx)];

    if (row < kCompanyListBakBase) {
        // OPEN (1-click primary): set active slot + load + mount (§2.3).
        if (!info.valid) {
            // Never silently switch to a corrupt company ([SAVE-R6]): the
            // row stays listed with BK (restore) and X (delete) available.
            popup_dialog("LOAD COMPANY", "COMPANY FILE DAMAGED");
            return MENU_REDRAW;
        }
        SaveDataIoError io = SaveDataIoError::None;
        const ContinueResult result = open_company_slot(
            og::runtime::current_session->myscreen_->save_data, info.slot,
            &io);
        switch (result) {
        case ContinueResult::Opened:
            st->opened = true;
            TRACE("company_list", "open %s", info.slot.c_str());
            return MENU_EXIT;
        case ContinueResult::LoadFailed:
            // Header valid but the body failed; open_company_slot restored
            // the previously open company. Surface the error, stay listed.
            popup_dialog("LOAD COMPANY", save_error_string(io));
            return MENU_REDRAW;
        case ContinueResult::Corrupt:
        case ContinueResult::NoCompany:
            popup_dialog("LOAD COMPANY", "COMPANY FILE DAMAGED");
            return MENU_REDRAW;
        }
        return MENU_REDRAW;
    }

    if (row < kCompanyListDelBase) {
        // §2.4 door: open the Backups sub-view on this row's company (the
        // nested-engine-screen pattern — difficulty-from-main-menu). A
        // successful restore rewinds the company AND opens it (straight into
        // base camp); BACK returns here with the list intact. Corrupt rows
        // keep the door — restore-from-backup IS their recovery path.
        st->backups_slot = info.slot;
        TRACE("company_list", "backups_door %s", info.slot.c_str());
        if (run_company_backups_screen(info.slot,
                                       format_company_row(info).name)) {
            st->opened = true;
            return MENU_EXIT;
        }
        // Not opened: the company set is unchanged (failed restores leave
        // the slot file pre-restore per §3.7 [SAVE-R3]); a remote start
        // that unparked the sub-view re-fires from this screen's own
        // loop-top check next frame. MENU_REDRAW re-inits our buttons.
        return MENU_REDRAW;
    }

    // X — delete company (+ ALL its backups), NO-first confirm (U3).
    if (info.slot == og::data::active_company_slot()) {
        // §3.7: delete_company refuses the active slot; the UI enforces
        // "switch first" up front so the confirm never lies.
        popup_dialog("DELETE COMPANY", "THIS COMPANY IS OPEN - SWITCH FIRST");
        return MENU_REDRAW;
    }
    const CompanyRowText row_text = format_company_row(info);
    // The name rides the message lines (a 34-char title escapes the 320px
    // dialog frame; message lines draw at 6px/char).
    const std::string message = row_text.name + "\nBACKUPS ARE DELETED TOO.";
    if (!no_or_yes_prompt("DELETE COMPANY?", message.c_str(), false))
        return MENU_REDRAW;
    if (!og::data::delete_company(info.slot)) {
        // The API refusal is popup-surfaced so a UI slip stays visible
        // (§3.7).
        popup_dialog("DELETE COMPANY", "DELETE FAILED");
        return MENU_REDRAW;
    }
    TRACE("company_list", "deleted %s", info.slot.c_str());
    // Re-scan (header-only) and clamp the page window. Deleting the
    // most-recent company retargets CONTINUE by construction (row 0 of the
    // re-scan IS what CONTINUE opens); deleting the last one exits to a
    // main menu whose gate hides CONTINUE/LOAD (§2.3).
    const int page_before = st->page.page;
    st->companies = og::data::list_companies();
    st->page = PageModel::make(static_cast<int>(st->companies.size()),
                               kCompanyListRowsPerPage);
    st->page.page = std::min(page_before, st->page.page_count() - 1);
    if (st->companies.empty())
        return MENU_EXIT;
    return MENU_REDRAW;
}

// ---------------------------------------------------------------------------
// §2.4 Backups sub-view (per company): the same chassis as the Company List
// minus the BK/X columns — 10 full-width snapshot rows (click = restore
// behind the NO-first confirm), BACK to the list, PageModel pagers
// (retention 20 => at most 2 pages).
// ---------------------------------------------------------------------------

// The company-list seam pattern: the per-frame rewire reads this file-static
// pointer; run_menu_screen's screen_state points at the SAME object.
CompanyBackupsScreenState* g_company_backups_state = nullptr;

constexpr int kCompanyBackupsRowsPerPage = 10;
constexpr int kCompanyBackupsBackIndex = 10;
constexpr int kCompanyBackupsPrevIndex = 11;
constexpr int kCompanyBackupsNextIndex = 12;

// §2.4 geometry: backup_row_0..9 (25,25+15i,220,10) — the rows span the
// column area the list's BK/X buttons occupy (no per-row delete on the SDL
// surface; the rect table is normative). Static nav is the full-page
// multi-page shape; the per-frame rewire recomputes every link anyway.
#define OG_COMPANY_BACKUP_ROW(i)                                             \
    {.id = "backup_row_" #i, .label = "",                                    \
     .x = 25, .y = 25 + 15 * (i), .w = 220, .h = 10,                          \
     .action = ButtonAction::MenuSpecRow, .arg = (i),                        \
     .nav = {.up = (i) > 0 ? (i) - 1 : -1,                                    \
             .down = (i) < 9 ? (i) + 1 : kCompanyBackupsBackIndex}}

constexpr MenuButtonSpec kCompanyBackupsRows[] = {
    OG_COMPANY_BACKUP_ROW(0), OG_COMPANY_BACKUP_ROW(1),
    OG_COMPANY_BACKUP_ROW(2), OG_COMPANY_BACKUP_ROW(3),
    OG_COMPANY_BACKUP_ROW(4), OG_COMPANY_BACKUP_ROW(5),
    OG_COMPANY_BACKUP_ROW(6), OG_COMPANY_BACKUP_ROW(7),
    OG_COMPANY_BACKUP_ROW(8), OG_COMPANY_BACKUP_ROW(9),
    // BACK to the Company List; Escape hotkey (the shared cancel grammar).
    {.id = "back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 10, .y = 170, .w = 44, .h = 20,
     .action = ButtonAction::MenuSpecRow, .arg = kCompanyBackupsBackIndex,
     .nav = {.up = 9, .right = kCompanyBackupsPrevIndex}},
    // Real MenuSpecRow pager actions (keyboard-live); statically hidden —
    // the rewire shows them only when the snapshots span pages.
    {.id = "backup_page_prev", .label = "PREV",
     .x = 220, .y = 170, .w = 40, .h = 20,
     .action = ButtonAction::MenuSpecRow, .arg = kCompanyBackupsPrevIndex,
     .nav = {.up = 9, .left = kCompanyBackupsBackIndex,
             .right = kCompanyBackupsNextIndex},
     .hidden = true},
    {.id = "backup_page_next", .label = "NEXT",
     .x = 270, .y = 170, .w = 40, .h = 20,
     .action = ButtonAction::MenuSpecRow, .arg = kCompanyBackupsNextIndex,
     .nav = {.up = 9, .left = kCompanyBackupsPrevIndex},
     .hidden = true},
};

#undef OG_COMPANY_BACKUP_ROW

// Per-frame visibility + nav over the live snapshot state (pattern b, like
// the Company List): page-window the rows, chain them vertically into BACK,
// close BACK/pager side links over pager visibility.
void company_backups_rewire(button* buttons, int count, int& /*highlighted*/)
{
    if (count < kCompanyBackupsNextIndex + 1)
        return;
    const CompanyBackupsScreenState* st = g_company_backups_state;
    const int first = st != nullptr ? st->page.first_index() : 0;
    const int end = st != nullptr ? st->page.end_index() : 0;
    const int visible = std::max(0, end - first);
    const bool pagers = st != nullptr && st->page.multi_page();

    for (int r = 0; r < kCompanyBackupsRowsPerPage; ++r) {
        const bool on = r < visible;
        buttons[r].hidden = !on;
        if (on) {
            buttons[r].nav = {
                .up = r > 0 ? r - 1 : -1,
                .down = r + 1 < visible ? r + 1 : kCompanyBackupsBackIndex,
                .left = -1,
                .right = -1};
        }
    }
    buttons[kCompanyBackupsPrevIndex].hidden = !pagers;
    buttons[kCompanyBackupsNextIndex].hidden = !pagers;
    buttons[kCompanyBackupsBackIndex].nav = {
        .up = visible > 0 ? visible - 1 : -1,
        .down = -1,
        .left = -1,
        .right = pagers ? kCompanyBackupsPrevIndex : -1};
    buttons[kCompanyBackupsPrevIndex].nav = {
        .up = visible > 0 ? visible - 1 : -1,
        .down = -1,
        .left = kCompanyBackupsBackIndex,
        .right = kCompanyBackupsNextIndex};
    buttons[kCompanyBackupsNextIndex].nav = {
        .up = visible > 0 ? visible - 1 : -1,
        .down = -1,
        .left = kCompanyBackupsPrevIndex,
        .right = -1};
    for (int i = 0; i < count; ++i)
        sync_button_hidden_state(buttons, i);
}

// The §2.4 content pass: retention-bearing title + column headers (the
// black-strip idiom, U2), the two row columns at x=27/151, the "p/N"
// indicator, and the empty state.
void company_backups_draw_content(void* screen_state)
{
    const CompanyBackupsScreenState* st =
        static_cast<const CompanyBackupsScreenState*>(screen_state);
    screen* game = og::runtime::current_session->myscreen_;

    const auto strip_text = [game](int x, int y, const std::string& text) {
        const int width = static_cast<int>(text.size()) * 6;
        game->draw_rect_filled(x - 2, y - 1, width + 4, 8, PURE_BLACK, 150);
        game->text_normal.write_xy(x, y, WHITE, "%s", text.c_str());
    };

    const int total = st != nullptr ? static_cast<int>(st->backups.size()) : 0;
    strip_text(25, 8,
               format_backup_list_title(
                   st != nullptr ? st->company_name : std::string(), total));
    strip_text(27, 16, "LEVEL");
    strip_text(151, 16, "SAVED");

    if (st == nullptr || total == 0) {
        // A company with no level wins yet has no snapshots: the empty view
        // is a REAL shape here, not just a transient (§3.7 — backups are
        // level-win products).
        game->text_normal.write_xy_center(160, 90, ORANGE_START, "%s",
                                          "NO BACKUPS YET");
        return;
    }

    const int first = st->page.first_index();
    const int end = st->page.end_index();
    for (int r = 0; r < end - first; ++r) {
        const BackupRowText row = format_backup_row(
            st->backups[static_cast<std::size_t>(first + r)]);
        const int y = 27 + 15 * r;
        game->text_normal.write_xy(27, y, DARK_BLUE, "%s", row.level.c_str());
        game->text_normal.write_xy(151, y, DARK_BLUE, "%s",
                                   row.saved.c_str());
    }

    if (st->page.multi_page())
        strip_text(140, 176, st->page.indicator());
}

// G3 row dispatch: restore per visual row (NO-first confirm + the §3.7
// validated rewind), BACK, and the pagers.
Sint32 company_backups_on_spec_row(int row, void* screen_state)
{
    CompanyBackupsScreenState* st =
        static_cast<CompanyBackupsScreenState*>(screen_state);
    if (st == nullptr)
        return 0;

    if (row == kCompanyBackupsBackIndex) {
        TRACE("company_backups", "back");
        return MENU_EXIT;
    }
    if (row == kCompanyBackupsPrevIndex || row == kCompanyBackupsNextIndex) {
        if (st->page.step(row == kCompanyBackupsPrevIndex ? -1 : 1))
            TRACE("company_backups", "page %s", st->page.indicator().c_str());
        return MENU_OK;
    }

    const int idx = st->page.first_index() + row;
    if (idx < 0 || idx >= static_cast<int>(st->backups.size()))
        return 0;  // stale click on a row hidden this frame
    const og::data::CompanyBackupInfo info =
        st->backups[static_cast<std::size_t>(idx)];

    if (!info.header.valid) {
        // The §3.7 step-0 validation is the real guard; refusing up front
        // (the corrupt-company-row precedent) just spares a confirm that
        // could only end in the same popup.
        popup_dialog("RESTORE BACKUP", "BACKUP FILE DAMAGED");
        return MENU_REDRAW;
    }

    // §2.4 restore confirm — NO-first (U3), the row's level context riding
    // the message lines (the delete-company grammar).
    const BackupRowText row_text = format_backup_row(info);
    std::string context_line = row_text.level;
    if (!row_text.saved.empty()) {
        context_line += ' ';
        context_line += row_text.saved;
    }
    const std::string message =
        context_line + "\nCURRENT STATE IS BACKED UP FIRST.";
    if (!no_or_yes_prompt("REWIND TO THIS BACKUP?", message.c_str(), false))
        return MENU_REDRAW;

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const og::data::CompanyRestoreError error =
        og::data::restore_company_backup(save, st->slot, info.seq);
    const bool rewound =
        error == og::data::CompanyRestoreError::None
        || error == og::data::CompanyRestoreError::RestampFailed;
    if (rewound) {
        // RestampFailed included: the rewind itself finished (disk and
        // memory hold the restored company; the next autosave re-stamps).
        // §2.4: a restored company opens straight into base camp — repoint
        // the active slot at it (restore itself never touches the slot; it
        // may target a non-active company, e.g. a corrupt one being
        // recovered).
        (void)og::data::set_active_company_slot(st->slot);
        st->opened = true;
        TRACE("company_backups", "restored %s seq %d", st->slot.c_str(),
              info.seq);
        return MENU_EXIT;
    }

    // Failure: popup, stay listed, state rolled back (§3.7 [SAVE-R3]).
    popup_dialog("RESTORE BACKUP", company_restore_error_string(error));
    if (error == og::data::CompanyRestoreError::ReloadFailed
        && st->slot != og::data::active_company_slot()) {
        // The step-3 rollback reloaded the TARGET company's pre-restore
        // state into memory; put the ambient company back so the slot and
        // the in-memory save never disagree (the open_company_slot
        // discipline).
        (void)save.load_with_error(og::data::active_company_slot());
    }
    // Steps >= 1 may have produced the pre-restore snapshot even on a later
    // failure: re-scan so the list tells the truth, and clamp the window.
    const int page_before = st->page.page;
    st->backups = og::data::list_company_backups(st->slot);
    st->page = PageModel::make(static_cast<int>(st->backups.size()),
                               kCompanyBackupsRowsPerPage);
    st->page.page = std::min(page_before, st->page.page_count() - 1);
    return MENU_REDRAW;
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

const MenuScreenSpec& company_list_menu_screen_spec()
{
    static const MenuScreenSpec spec{
        .name = "company_list",
        .rows = kCompanyListRows,
        .row_count = static_cast<int>(std::size(kCompanyListRows)),
        .buttons_accessor = &picker_company_list_buttons,
        .count_accessor = &picker_company_list_button_count,
        // Pattern-b full-graph rewire, recomputed every frame from the list
        // state (page window, pager visibility, active-row outline).
        .nav = {.kind = NavProgramKind::Rewire,
                .rewire = &company_list_rewire},
        // A host GO must launch a peer parked in the list (the difficulty
        // subscreen precedent): propagate the remote MENU_EXIT — the wrapper
        // reports "not opened" and the re-entered main menu launches.
        .remote_start = RemoteStartScope::MainScope,
        .remote_start_exit = RemoteStartExit::ReturnMenuExit,
        // Fade the main menu out, draw the list cold, fade in (the name-entry
        // entry idiom).
        .enter = EnterTransition::FadeAroundEntry,
        // Initial highlight: row 0 — what CONTINUE opens (§2.3).
        .default_highlight = 0,
        .polls_lobby = true,
        .draw_background = &picker_backdrop_draw_background,
        .draw_content = &company_list_draw_content,
        .on_spec_row = &company_list_on_spec_row,
        .exit_value = MENU_REDRAW,
    };
    return spec;
}

void install_base_camp_state_for_screen(BaseCampScreenState* state)
{
    g_base_camp_state = state;
}

void base_camp_refresh_rows(BaseCampScreenState& state)
{
    // §3.3: positional display indices are never held across a roster
    // change or a win fold — re-collect every time. Networked lobbies merge
    // the replicated foreign rosters behind the own rows (§2.5); the page
    // window derives from slots.size(), so >24 display slots (two
    // well-stocked machines) just grow the page count defensively.
    const SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const bool networked = picker_lobby_is_networked();
    state.slots = collect_base_camp_display_slots(
        save,
        networked ? picker_lobby_players()
                  : std::vector<og::sim::LobbyPlayer>{},
        networked ? picker_lobby_local_player_indices()
                  : std::vector<std::uint8_t>{},
        networked);
    const int page_before = state.page.page;
    state.page = PageModel::make(static_cast<int>(state.slots.size()),
                                 kBaseCampRosterRowsPerPage);
    state.page.page =
        std::clamp(page_before, 0, state.page.page_count() - 1);
}

void install_company_list_state_for_screen(CompanyListScreenState* state)
{
    g_company_list_state = state;
}

// §2.3: run the Company List (blocking) over a fresh header-only scan.
bool run_company_list_screen()
{
    if (og::runtime::current_session->myscreen_ == nullptr)
        return false;
    CompanyListScreenState state;
    state.companies = og::data::list_companies();
    state.page = PageModel::make(static_cast<int>(state.companies.size()),
                                 kCompanyListRowsPerPage);
    install_company_list_state_for_screen(&state);
    (void)run_menu_screen(company_list_menu_screen_spec(), &state);
    install_company_list_state_for_screen(nullptr);
    return state.opened;
}

const MenuScreenSpec& company_backups_menu_screen_spec()
{
    static const MenuScreenSpec spec{
        .name = "company_backups",
        .rows = kCompanyBackupsRows,
        .row_count = static_cast<int>(std::size(kCompanyBackupsRows)),
        .buttons_accessor = &picker_company_backups_buttons,
        .count_accessor = &picker_company_backups_button_count,
        // Pattern-b full-graph rewire, recomputed every frame from the
        // snapshot state (page window, pager visibility).
        .nav = {.kind = NavProgramKind::Rewire,
                .rewire = &company_backups_rewire},
        // A host GO must launch a peer parked here too (the nested-subscreen
        // precedent): propagate the remote MENU_EXIT — the wrappers report
        // "not opened" and the re-entered outer screens unwind in turn.
        .remote_start = RemoteStartScope::MainScope,
        .remote_start_exit = RemoteStartExit::ReturnMenuExit,
        // Fade the Company List out, draw the sub-view cold, fade in.
        .enter = EnterTransition::FadeAroundEntry,
        // Initial highlight: row 0 — the newest snapshot.
        .default_highlight = 0,
        .polls_lobby = true,
        .draw_background = &picker_backdrop_draw_background,
        .draw_content = &company_backups_draw_content,
        .on_spec_row = &company_backups_on_spec_row,
        .exit_value = MENU_REDRAW,
    };
    return spec;
}

void install_company_backups_state_for_screen(CompanyBackupsScreenState* state)
{
    g_company_backups_state = state;
}

// §2.4: run the Backups sub-view (blocking) over a fresh header-only
// snapshot scan.
bool run_company_backups_screen(const std::string& slot,
                                const std::string& company_name)
{
    if (og::runtime::current_session->myscreen_ == nullptr)
        return false;
    CompanyBackupsScreenState state;
    state.slot = slot;
    state.company_name = company_name;
    state.backups = og::data::list_company_backups(slot);
    state.page = PageModel::make(static_cast<int>(state.backups.size()),
                                 kCompanyBackupsRowsPerPage);
    install_company_backups_state_for_screen(&state);
    (void)run_menu_screen(company_backups_menu_screen_spec(), &state);
    install_company_backups_state_for_screen(nullptr);
    return state.opened;
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

// TEAM BUILD -> BASE CAMP (§2.5), engine-hosted. The reload cursor enters at
// -1: the first frame always reloads the picker world (the legacy entry
// behavior — the screen must reflect the save's level whatever was loaded
// before it). The roster rows materialize from the installed screen state
// (refreshed before the first frame and per tick — §3.3). Every legacy exit
// carried MENU_EXIT; the fold below keeps the legacy return shape verbatim.
Sint32 create_team_menu(Sint32 arg1)
{
    (void)arg1;
    og::ui::BaseCampScreenState state;
    og::ui::base_camp_refresh_rows(state);
    og::ui::install_base_camp_state_for_screen(&state);
    const Sint32 retvalue =
        og::ui::run_menu_screen(og::ui::team_build_menu_screen_spec(), &state);
    og::ui::install_base_camp_state_for_screen(nullptr);
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

button* picker_company_list_buttons()
{
    og::ui::materialize_menu_buttons(og::ui::company_list_menu_screen_spec(),
                                     pks().company_list_buttons);
    return pks().company_list_buttons.data();
}

int picker_company_list_button_count()
{
    return static_cast<int>(pks().company_list_buttons.size());
}

button* picker_company_backups_buttons()
{
    og::ui::materialize_menu_buttons(
        og::ui::company_backups_menu_screen_spec(),
        pks().company_backups_buttons);
    return pks().company_backups_buttons.data();
}

int picker_company_backups_button_count()
{
    return static_cast<int>(pks().company_backups_buttons.size());
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

// create_view_menu and the create_save_menu/create_load_menu slot wrappers
// are RETIRED with their screens (§2.5/§3.8): the base camp roster replaced
// the team view, and saving is automatic.

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
