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

#include <openglad/interface/base.h>
#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/sound.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/resources/gparser.h>

#include "picker_sdl_defs.h"

#include <array>
#include <cstddef>
#include <format>
#include <span>
#include <string>

// Shared FX-face draw helpers and picker loop helpers (defined in picker.cpp
// / picker_team_build.cpp; declared locally by every consumer — repo
// pattern).
void draw_toggle_effect_button(button& b, const std::string& category,
                               const std::string& setting);
void draw_cycle_effect_button(button& b, const std::string& category,
                              const std::string& setting);
void draw_sprite_sheet_button(button& b);
void sync_button_hidden_state(const button* buttons, int button_index);
void ensure_highlighted_button_visible(const button* buttons, int num_buttons,
                                       int& highlighted_button);

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

} // namespace

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
                {.kind = Kind::Legacy, .legacy_entry = &mainmenu});
            set(MenuScreenId::TeamBuild,
                {.kind = Kind::Legacy, .legacy_entry = &create_team_menu});
            set(MenuScreenId::ViewTeam,
                {.kind = Kind::Legacy, .legacy_entry = &create_view_menu});
            set(MenuScreenId::SaveSlots,
                {.kind = Kind::Legacy, .legacy_entry = &create_save_menu});
            set(MenuScreenId::LoadSlots,
                {.kind = Kind::Legacy, .legacy_entry = &create_load_menu});
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
                {.kind = Kind::Legacy, .legacy_entry = &create_hire_menu});
            set(MenuScreenId::Train,
                {.kind = Kind::Legacy, .legacy_entry = &create_train_menu});
            set(MenuScreenId::Progress,
                {.kind = Kind::Legacy, .legacy_entry = &create_progress_menu});
            set(MenuScreenId::ViewScenario,
                {.kind = Kind::Legacy,
                 .legacy_entry = &create_view_scenario_menu});
            set(MenuScreenId::Scenario,
                {.kind = Kind::Legacy, .legacy_entry = &create_scenario_menu});
            set(MenuScreenId::Teams,
                {.kind = Kind::Legacy, .legacy_entry = &create_teams_menu});
            // NETWORKING is owned by the SdlPickerClient state machine
            // (configure_networking is a client method); it migrates last,
            // under valve V2.
            set(MenuScreenId::Networking, {.kind = Kind::Legacy});
            return table;
        }();
    return hosts[static_cast<std::size_t>(id)];
}

} // namespace og::ui

// --- D3 materialization shims (moved from picker.cpp with the migration) ---

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
