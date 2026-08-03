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
#include <openglad/gameplay/net_transport.h>
#include <openglad/interface/base.h>
#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/sound.h>
#include <openglad/interface/platform_bridge.h>
#include <openglad/interface/ui/cloud_save_client.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/resources/campaign_metadata.h>
#include <openglad/resources/company.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>

#include "picker_sdl_defs.h"

#include <algorithm>
#include <array>
#include <cctype>
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
void picker_set_train_seed_slot(int slot);
// picker_base_camp_after_roster_mutation() comes from picker_sdl_defs.h.
Sint32 create_train_menu(Sint32 arg1);
void popup_dialog(const char* title, const char* message);
bool no_or_yes_prompt(const char* title, const char* message,
                      bool default_value);
// The shared one-line text prompt (level_editor_ui.cpp; the JOIN ROOM CODE
// dialog) — the #155 CLOUD passphrase entry reuses it.
bool prompt_for_string(const std::string& message, std::string& result);
#ifdef TESTING
// #155 cloud passphrase prompt seam (the picker_testing_yes_or_no_queue
// pattern): tests queue passphrases, the PASSPHRASE handler pops one per
// click, and an empty queue is a deterministic cancel. Defined at the end
// of this file; tests declare these locally (repo pattern).
void picker_testing_cloud_passphrase_queue_clear();
void picker_testing_cloud_passphrase_queue_push(const char* value);
bool picker_testing_cloud_passphrase_queue_pop(std::string& out);
#endif
// MATCHUP engine hooks (defined beside their file-local helpers in
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
Sint32 picker_train_menu_engine_on_spec_row(int row, void* screen_state);
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

std::string infinite_gold_row_label(const MenuLabelContext& context)
{
    return context.save != nullptr
        ? format_infinite_gold_label(*context.save)
        : std::string("Infinite Gold: Off");
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
     .nav = {.up = 4, .down = 6},
     .label_binding = {.formatter = &generator_rate_row_label},
     .gate = kHostOnlyGate},
    {.id = "infinite_gold", .label = "Infinite Gold: Off",
     .x = 90, .y = 150, .w = 140, .h = 15,
     .action = ButtonAction::ToggleInfiniteGold, .arg = -1,
     .nav = {.up = 5, .down = 0},
     .label_binding = {.formatter = &infinite_gold_row_label},
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
// too — those tails are gone, G8 — and RESTORE SETTINGS or a lobby-applied
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
// drawn text below each section's buttons. RESET ALL preserves the scoped
// reset that used to sit one level above this screen. The mode faces
// re-derive from the live control mode every frame on both surfaces
// (LabelBindings — the legacy loop's per-frame writes).

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
     .nav = {.up = 9, .down = 1}},
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
    {.id = "reset_all_controls", .label = "RESET ALL",
     .x = 90, .y = 174, .w = 140, .h = 15,
     .action = ButtonAction::RestoreDefaultControls, .arg = -1,
     .nav = {.up = 7, .down = 0}},
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
        .remote_start = RemoteStartScope::MainScope,
        .polls_lobby = true,
        .draw_background = &options_panel_draw_background,
        .draw_content = &control_options_draw_content,
        .exit_value = MENU_REDRAW,
    };
    return spec;
}

// ---------------------------------------------------------------------------
// MAIN OPTIONS (§1.8 step 3, last slice): sound/graphics settings plus doors
// into CONTROLS and the three FX subscreens (GAMEPLAY FX / UI FX /
// GRAPHICS FX). Persistent controller profiles remain reachable before a
// company is opened, while seat lifecycle and per-level player-team choices
// live in Base Camp. Rows otherwise retain the deleted k_main_options_buttons
// table: the settings/door column stacks at the classic 23px pitch
// (BUTTON_HEIGHT 15 + 8 padding). The sound face and the sprite-sheet face
// are content-pass state draws over the bevels (green per cfg), not label
// bindings. Every subscreen door just opens its blocking screen; the exit
// epilogue in main_options() below is the single point where the whole
// family's cfg edits persist.

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
     .nav = {.up = 8, .down = 1, .right = 4}},
    {.id = "toggle_sound", .label = "Sound",
     .x = 135, .y = options_col_y(1), .w = 50, .h = 15,
     .action = ButtonAction::ToggleSound, .arg = -1,
     .nav = {.up = 0, .down = 2, .right = 5}},
    // Door into the DISPLAY subscreen (mode / resolution / overscan /
    // scaling / filter live there).
    {.id = "display_settings", .label = "DISPLAY",
     .x = 130, .y = options_col_y(2), .w = 90, .h = 15,
     .action = ButtonAction::OpenDisplaySettings, .arg = -1,
     .nav = {.up = 1, .down = 3, .right = 5}},
    {.id = "gameplay_fx", .label = "GAMEPLAY FX",
     .x = 130, .y = options_col_y(3), .w = 90, .h = 15,
     .action = ButtonAction::OpenGameplayFxSettings, .arg = -1,
     .nav = {.up = 2, .down = 6}},
    {.id = "restore_defaults", .label = "RESTORE SETTINGS",
     .x = 210, .y = 10, .w = 100, .h = 15,
     .action = ButtonAction::RestoreDefaultSettings, .arg = -1,
     .nav = {.up = 7, .down = 5, .left = 0}},
    {.id = "pick_sprite_sheet", .label = "Sprite Sheet",
     .x = 210, .y = options_col_y(1), .w = 90, .h = 15,
     .action = ButtonAction::PickSpriteSheet, .arg = 0,
     .nav = {.up = 4, .down = 2, .left = 1},
     .label_binding = {.formatter = &sprite_sheet_row_label}},
    {.id = "ui_fx", .label = "UI FX",
     .x = 130, .y = options_col_y(4), .w = 90, .h = 15,
     .action = ButtonAction::OpenUiFxSettings, .arg = -1,
     .nav = {.up = 3, .down = 7}},
    {.id = "graphics_fx", .label = "GRAPHICS FX",
     .x = 130, .y = options_col_y(5), .w = 90, .h = 15,
     .action = ButtonAction::OpenGraphicsFxSettings, .arg = -1,
     .nav = {.up = 6, .down = 8}},
    {.id = "control_settings", .label = "CONTROLS",
     .x = 130, .y = options_col_y(6), .w = 90, .h = 15,
     .action = ButtonAction::OpenControlSettings, .arg = -1,
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
    scr->text_normal.write_xy(20, rows[8].y + 3, DARK_BLUE, "Controls:");
    draw_sprite_sheet_button(rows[5]);
}

const MenuScreenSpec& main_options_menu_screen_spec()
{
    static const MenuScreenSpec spec{
        .name = "main_options",
        .rows = kMainOptionsRows,
        .row_count = static_cast<int>(std::size(kMainOptionsRows)),
        .buttons_accessor = &picker_main_options_buttons,
        .count_accessor = &picker_main_options_button_count,
        // Controls is a blocking child screen. Keep MainScope on its parent
        // too so a child-propagated host GO cannot be normalized into the
        // ordinary "back to main menu" redraw.
        .remote_start = RemoteStartScope::MainScope,
        .polls_lobby = true,
        .draw_background = &options_panel_draw_background,
        .draw_content = &main_options_draw_content,
        .exit_value = MENU_REDRAW,
    };
    return spec;
}

// The legacy USE_TOUCH_INPUT => DISABLE_MULTIPLAYER mapping lived beside the
// main-menu tables in picker.cpp. Keep it ahead of main-menu variant
// selection so touch-only builds choose the no-MP spec.
#ifdef USE_TOUCH_INPUT
#ifndef DISABLE_MULTIPLAYER
#define DISABLE_MULTIPLAYER
#endif
#endif

// ---------------------------------------------------------------------------
// MAIN MENU (§1.8 step 4, historically the heaviest 10a screen). The MP and
// no-MP specs share one centered primary stack. HELP and QUIT are a stable
// footer on both platforms. QUIT is build-gated as an enabled native row or
// a disabled web row at the same materialized index, so geometry and nav
// stay identical across all four variants.
//
// redraw_mainmenu's raw allbuttons_[N] writes became bindings (§1.6):
//   [0] pixie face               -> art_family row (FAMILY_NORMAL1 — the
//       normal1.png Begin New Game face is preserved by construction: same
//       empty label, same set_graphic path, re-applied after init_buttons
//       and after every reset_buttons).
// The player-count outlines retired when seat lifecycle moved into Base Camp;
// its + and explicit SPECTATE action now derive from live lobby state.
// Its title/columns drawMix + the FULL re-vdisplay-after-title pass + the
// native version stamp survive as the draw_content hook (G14): the 136x58
// title frames at (15,8)/(151,8) overlap begin_new_game (80,55,140,20) in
// y=55-66, and legacy re-vdisplayed EVERY button after the title drawMix —
// the runner's draw_buttons -> content order would invert that overlap
// without the full re-vdisplay here.
//
// Both OPTIONS_BUTTON_INDEX #defines (picker.cpp x4 and the
// picker_main_menu.cpp pair) are gone — picker_mainmenu_options_index()
// derives the index from the materialized spec.

// Company & Base Camp (design §2.1): CONTINUE and LOAD are gated on the
// existence of at least one company file. list_companies() touches the
// filesystem, so the view is cached and refreshed exactly once per
// mainmenu() entry — the company set is stable while the blocking loop runs
// (Begin New Game and the Load list both exit the loop first). The main menu
// deliberately does not repeat the active company name; CONTINUE and LOAD
// already communicate that state, and removing the caption makes room for a
// stable settings group and Help/Quit footer.
struct MainMenuCompanyView {
    bool present = true;
    std::string display_name;
};
MainMenuCompanyView g_main_menu_company_view;

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

// Browsers do not have a meaningful application quit operation. Keep the
// footer shape and wording identical to native, but make the web QUIT face
// visibly unavailable and inert through the menu engine's disabled-row
// grammar.
RowState main_menu_web_quit_state(const MenuLabelContext& /*context*/)
{
    return RowState::Disabled;
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
// idempotent regardless of a prior frame's state. LEVEL EDITOR is the first
// stable row below the pair, so the absent-company shape routes through it.
void main_menu_nav_rewire(button* buttons, int count, int& /*highlighted*/)
{
    const int i_begin = main_menu_row_index(buttons, count, "begin_new_game");
    const int i_continue = main_menu_row_index(buttons, count, "continue_game");
    const int i_level_editor = main_menu_row_index(buttons, count, "level_edit");

    if (g_main_menu_company_view.present) {
        if (i_begin >= 0) buttons[i_begin].nav.down = i_continue;
        if (i_level_editor >= 0) buttons[i_level_editor].nav.up = i_continue;
    } else {
        if (i_begin >= 0) buttons[i_begin].nav.down = i_level_editor;
        if (i_level_editor >= 0) buttons[i_level_editor].nav.up = i_begin;
    }
}

constexpr MenuButtonSpec kMainMenuRowsMP[] = {
    {.id = "begin_new_game", .label = "",
     .x = 80, .y = 55, .w = 140, .h = 20,
     .action = ButtonAction::BeginMenu, .arg = 1,
     .nav = {.down = 1},
     .art_family = FAMILY_NORMAL1},
    // §2.1: CONTINUE and LOAD share one centered row and gate together.
    {.id = "continue_game", .label = "CONTINUE",
     .x = 80, .y = 79, .w = 68, .h = 20,
     .action = ButtonAction::CreateTeamMenu, .arg = -1,
     .nav = {.up = 0, .down = 2, .right = 7},
     .gate = {.gate = MenuGate::Custom, .custom = &main_menu_company_present}},
    {.id = "level_edit", .label = "Level Editor",
     .x = 80, .y = 103, .w = 140, .h = 15,
     .action = ButtonAction::DoLevelEdit, .arg = -1,
     .nav = {.up = 1, .down = 4}},
    // Seat lifecycle moved beside the live lobby in Base Camp. The remaining
    // session category keeps the full-width row, followed by the broader
    // game/presentation settings door.
    {.id = "difficulty", .label = "DIFFICULTY",
     .x = 80, .y = 154, .w = 140, .h = 15,
     .action = ButtonAction::OpenDifficultyMenu, .arg = -1,
     .nav = {.up = 4, .down = 5}},
    // "GAME" reads as a settings category under the SETTINGS heading; the
    // terminal clients keep the fuller "Game Settings" (no heading there).
    {.id = "options", .label = "GAME",
     .x = 80, .y = 135, .w = 68, .h = 15,
     .action = ButtonAction::MainOptions, .arg = -1,
     .nav = {.up = 2, .down = 3, .right = 9}},
    {.id = "help", .label = "HELP",
     .x = 80, .y = 178, .w = 68, .h = 15,
     .action = ButtonAction::ShowHelp, .arg = -1,
     .nav = {.up = 3, .down = 0, .right = 6}},
    // The web/native fork (§1.6): exactly one QUIT row survives at
    // materialized index 6. Native activation quits; web is visibly disabled.
    {.id = "quit", .label = "QUIT ", .hotkey = KEYSTATE_ESCAPE,
     .x = 152, .y = 178, .w = 68, .h = 15,
     .action = ButtonAction::QuitMenu, .arg = 0,
     .nav = {.up = 3, .down = 0, .left = 5},
     .build = MenuBuildGate::NativeOnly},
    {.id = "quit", .label = "QUIT ",
     .x = 152, .y = 178, .w = 68, .h = 15,
     .action = ButtonAction::QuitMenu, .arg = 0,
     .nav = {.up = 3, .down = 0, .left = 5},
     .state_override = &main_menu_web_quit_state,
     .build = MenuBuildGate::WebOnly},
    // Appended tail: LOAD then the mutually exclusive no-company note.
    {.id = "load_company", .label = "LOAD",
     .x = 152, .y = 79, .w = 68, .h = 20,
     .action = ButtonAction::CreateLoadMenu, .arg = 0,
     .nav = {.up = 0, .down = 2, .left = 1},
     .gate = {.gate = MenuGate::Custom, .custom = &main_menu_company_present}},
    {.id = "no_company_note", .label = "NO COMPANY YET",
     .x = 80, .y = 79, .w = 140, .h = 20,
     .action = ButtonAction::MenuSpecRow, .arg = 8,
     .state_override = &main_menu_no_company_note_state,
     .hidden = true},
    // #155 CLOUD: always visible (reachable with zero companies — the fresh
    // browser restore flow is the point). Pairs with the GAME door on the
    // first settings row (directly under the heading) (the y=119..134 band belongs to the grey SETTINGS
    // heading drawn by main_menu_draw_content at y=125 — no button may sit
    // there). Appended after the note, so its materialized ordinal is 9 on
    // every variant (exactly one QUIT row survives materialization).
    {.id = "cloud", .label = "CLOUD",
     .x = 152, .y = 135, .w = 68, .h = 15,
     .action = ButtonAction::MenuSpecRow, .arg = 9,
     .nav = {.up = 2, .down = 3, .left = 4}},
};

constexpr MenuButtonSpec kMainMenuRowsNoMP[] = {
    {.id = "begin_new_game", .label = "",
     .x = 80, .y = 55, .w = 140, .h = 20,
     .action = ButtonAction::BeginMenu, .arg = 1,
     .nav = {.down = 1},
     .art_family = FAMILY_NORMAL1},
    {.id = "continue_game", .label = "CONTINUE",
     .x = 80, .y = 79, .w = 68, .h = 20,
     .action = ButtonAction::CreateTeamMenu, .arg = -1,
     .nav = {.up = 0, .down = 2, .right = 7},
     .gate = {.gate = MenuGate::Custom, .custom = &main_menu_company_present}},
    {.id = "level_edit", .label = "Level Editor",
     .x = 80, .y = 103, .w = 140, .h = 15,
     .action = ButtonAction::DoLevelEdit, .arg = -1,
     .nav = {.up = 1, .down = 4}},
    {.id = "difficulty", .label = "DIFFICULTY",
     .x = 80, .y = 154, .w = 140, .h = 15,
     .action = ButtonAction::OpenDifficultyMenu, .arg = -1,
     .nav = {.up = 4, .down = 5}},
    // "GAME" reads as a settings category under the SETTINGS heading; the
    // terminal clients keep the fuller "Game Settings" (no heading there).
    {.id = "options", .label = "GAME",
     .x = 80, .y = 135, .w = 68, .h = 15,
     .action = ButtonAction::MainOptions, .arg = -1,
     .nav = {.up = 2, .down = 3, .right = 9}},
    {.id = "help", .label = "HELP",
     .x = 80, .y = 178, .w = 68, .h = 15,
     .action = ButtonAction::ShowHelp, .arg = -1,
     .nav = {.up = 3, .down = 0, .right = 6}},
    {.id = "quit", .label = "QUIT ", .hotkey = KEYSTATE_ESCAPE,
     .x = 152, .y = 178, .w = 68, .h = 15,
     .action = ButtonAction::QuitMenu, .arg = 0,
     .nav = {.up = 3, .down = 0, .left = 5},
     .build = MenuBuildGate::NativeOnly},
    {.id = "quit", .label = "QUIT ",
     .x = 152, .y = 178, .w = 68, .h = 15,
     .action = ButtonAction::QuitMenu, .arg = 0,
     .nav = {.up = 3, .down = 0, .left = 5},
     .state_override = &main_menu_web_quit_state,
     .build = MenuBuildGate::WebOnly},
    {.id = "load_company", .label = "LOAD",
     .x = 152, .y = 79, .w = 68, .h = 20,
     .action = ButtonAction::CreateLoadMenu, .arg = 0,
     .nav = {.up = 0, .down = 2, .left = 1},
     .gate = {.gate = MenuGate::Custom, .custom = &main_menu_company_present}},
    {.id = "no_company_note", .label = "NO COMPANY YET",
     .x = 80, .y = 79, .w = 140, .h = 20,
     .action = ButtonAction::MenuSpecRow, .arg = 8,
     .state_override = &main_menu_no_company_note_state,
     .hidden = true},
    // #155 CLOUD: always visible (reachable with zero companies — the fresh
    // browser restore flow is the point). Pairs with the GAME door on the
    // first settings row (directly under the heading) (the y=119..134 band belongs to the grey SETTINGS
    // heading drawn by main_menu_draw_content at y=125 — no button may sit
    // there). Appended after the note, so its materialized ordinal is 9 on
    // every variant (exactly one QUIT row survives materialization).
    {.id = "cloud", .label = "CLOUD",
     .x = 152, .y = 135, .w = 68, .h = 15,
     .action = ButtonAction::MenuSpecRow, .arg = 9,
     .nav = {.up = 2, .down = 3, .left = 4}},
};

// The CLOUD row's materialized ordinal (table length - 2: exactly one QUIT
// row survives materialization, and CLOUD is appended after the note).
constexpr int kMainMenuCloudSpecArg = 9;

// #155: the main menu's first spec-row consumer. The no_company_note row
// (arg 8) is never Visible, so only CLOUD can arrive here.
Sint32 main_menu_on_spec_row(int row, void* /*screen_state*/)
{
    if (row == kMainMenuCloudSpecArg)
    {
        (void)run_cloud_save_screen();
        return MENU_REDRAW;
    }
    return 0;
}

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
        // The columns pixie has carried this dormant animation hook since 2013:
        //pks().main_columns_pix->next_frame();
    }

    // G14: re-vdisplay EVERY button after the title drawMix (the title
    // frames overlap begin_new_game; buttons must win that overlap).
    int count = 0;
    while (count < static_cast<int>(og::runtime::current_session->allbuttons_.size())
           && og::runtime::current_session->allbuttons_[count]) {
        og::runtime::current_session->allbuttons_[count]->vdisplay();
        count++;
    }

    // Center the category heading on the 140px menu column (x=80..220), not
    // on the 320px canvas; the classic column art shifts this stack left.
    game->text_normal.write_xy_center(150, 125, GREY, "%s", "SETTINGS");

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

bool reload_picker_level_and_sync_settings(screen& myscreen, short level_id)
{
    myscreen.world().id = level_id;
    if (!myscreen.load_level())
        return false;

    // CTF's selectable team domain comes from the loaded map's authored
    // flags. Campaign and PROGRESS callbacks publish the new cursor before
    // this deferred reload, so publish once more now that the lobby can carry
    // the real authored-team mask instead of the all-teams fallback.
    picker_lobby_sync_settings_from_save();
    return true;
}

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
        reload_picker_level_and_sync_settings(*myscreen,
                                              guard->last_level_id);
    }
    return true;
}

// ---------------------------------------------------------------------------
// SCENARIO subscreen (§1.8 step 5): the column at x=30 stacks the host-gated
// SET CAMPAIGN / SET LEVEL (their name strips draw alongside) over the
// always-visible VIEW LEVEL | MATCHUP | PROGRESS row; BACK sits apart at
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
    {.id = "matchup", .label = "MATCHUP",
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
// MATCHUP subscreen (§1.8 step 5) descends from the former TEAMS rows
// transcribed VERBATIM from the deleted k_teamsmenu_buttons. Player JOIN,
// local-guy cycling, and the duplicate READY affordance retired when explicit
// per-seat assignment moved into Base Camp; their stable ordinals remain
// dormant so old action IDs and the migration history stay legible. MATCHUP
// retains the four wide team summaries, per-team detail pagers, host-gated CTF
// settings, cross-control, and the legacy redraw/exit behavior.

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
     .nav = {.down = 4},
     .hidden = true},
    {.id = "join_team_1", .label = "JOIN",
     .x = 240, .y = 62, .w = 50, .h = 12,
     .action = ButtonAction::JoinTeam, .arg = 1,
     .nav = {.up = 3, .down = 5},
     .hidden = true},
    {.id = "join_team_2", .label = "JOIN",
     .x = 240, .y = 92, .w = 50, .h = 12,
     .action = ButtonAction::JoinTeam, .arg = 2,
     .nav = {.up = 4, .down = 6},
     .hidden = true},
    {.id = "join_team_3", .label = "JOIN",
     .x = 240, .y = 122, .w = 50, .h = 12,
     .action = ButtonAction::JoinTeam, .arg = 3,
     .nav = {.up = 5, .down = 9},
     .hidden = true},
    {.id = "guy_prev", .label = "<",
     .x = 10, .y = 146, .w = 16, .h = 12,
     .action = ButtonAction::TeamsCycleGuy, .arg = -1,
     .nav = {.up = 3, .down = 0, .right = 8},
     .hidden = true},
    {.id = "guy_next", .label = ">",
     .x = 120, .y = 146, .w = 16, .h = 12,
     .action = ButtonAction::TeamsCycleGuy, .arg = 1,
     .nav = {.up = 3, .down = 0, .left = 7, .right = 9},
     .hidden = true},
    {.id = "guy_team", .label = "TEAM >",
     .x = 150, .y = 146, .w = 70, .h = 12,
     .action = ButtonAction::TeamsCycleGuyTeam, .arg = 1,
     .nav = {.up = 6, .down = 0, .left = 8},
     .hidden = true},
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
     .x = 297, .y = 39, .w = 14, .h = 12,
     .action = ButtonAction::TeamsPageFlip, .arg = 0,
     .hidden = true},
    {.id = "team_page_1", .label = ">",
     .x = 297, .y = 69, .w = 14, .h = 12,
     .action = ButtonAction::TeamsPageFlip, .arg = 1,
     .hidden = true},
    {.id = "team_page_2", .label = ">",
     .x = 297, .y = 99, .w = 14, .h = 12,
     .action = ButtonAction::TeamsPageFlip, .arg = 2,
     .hidden = true},
    {.id = "team_page_3", .label = ">",
     .x = 297, .y = 129, .w = 14, .h = 12,
     .action = ButtonAction::TeamsPageFlip, .arg = 3,
     .hidden = true},
    // §2.7 cross-control toggle: reuses the guy-row slot that is vacant when
    // networked (guy_prev/next/team are local-only — the same-rect
    // mutually-exclusive-gate pattern as the base camp's GO/READY pair).
    // Visible to ALL peers when networked (a mode that changes a client's
    // own rights must be visible to that client, §8 resolution 6);
    // host-only actionable — the MenuSpecRow dispatch popups for non-hosts.
    {.id = "cross_control", .label = "CTRL: OWN",
     .x = 120, .y = 170, .w = 80, .h = 20,
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
     .nav = {.up = 13, .left = 18, .right = kTrainMenuSellIndex}},
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
     .nav = {.up = 16, .down = kTrainMenuSellIndex, .left = 13},
     .state_override = &train_change_team_row_state},
    {.id = "back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 10, .y = 170, .w = 40, .h = 20,
     .action = ButtonAction::ReturnMenu, .arg = MENU_EXIT,
     .nav = {.up = 12, .right = 14}},
    {.id = "sell", .label = "SELL",
     .x = 240, .y = 170, .w = 64, .h = 20,
     .action = ButtonAction::MenuSpecRow, .arg = kTrainMenuSellIndex,
     .nav = {.up = kTrainMenuChangeTeamIndex, .left = 14}},
};

static_assert(std::size(kTrainMenuRows) == 20,
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
// TEAM BUILD -> BASE CAMP (§2.5, regridded per §9.5 then §9.10, row-click
// train per §9.11): the command roster. The roster IS the default view (the
// VIEW TEAM screen retired into it): 8 rows/page at the round-6 14px pitch
// (§9.14 — round 4 trades one row for clear header/hint margins) — a deploy
// toggle (23,45+14r,14,10), team-color cycler (61,45+14r,10,10), and the
// §9.11 row-body train zone (84,45+14r,214,10; the TRAIN column is deleted,
// clicking the row trains), right-edge move-up button — the page cluster
// top-right (< p/N >), and the bottom command
// strip BACK | HIRE | SCENARIO | NETWORK | GO at y=178. SAVE/LOAD
// left the base camp (§3.8: saving is automatic on every mutation).
// Roster and pager rows dispatch through ButtonAction::MenuSpecRow (G3,
// arg == spec ordinal); the strip keeps the legacy ButtonActions so do_call
// routing, the GO -> StartGame interception, and interact("go") survive
// unchanged. Per-frame full-graph rewire (pattern b) over the installed
// screen state. The §2.6 dual-role slot: GO (host) and its same-rect READY
// twin (networked joiner) are the two host/joiner-gated buttons — exactly
// one is visible per frame.

// The retired View Team table carried these two little promises from 2013;
// Base Camp finally cashes them as name-tap training and the HIRE command:
//  button("TRAIN", KEYSTATE_e, 85, 170, 60, 20, button_action_id(ButtonAction::CreateTrainMenu), -1},
//  button("HIRE",  KEYSTATE_b, 190, 170, 60, 20, button_action_id(ButtonAction::CreateHireMenu), -1},
// Its 2002 header captions survive here too: "View team members", "Load a
// team", and "Save a team." View became this roster, Load became the Company
// List, and Save became automatic.

// The company-list seam pattern: the per-frame rewire reads this file-static
// pointer; run_menu_screen's screen_state points at the SAME object.
BaseCampScreenState* g_base_camp_state = nullptr;

// Round-6 vertical rhythm: the panel's inner face is y=30..158. Header ink
// is y=33..38; rows at y=45+14r leave six clear pixels after the header and
// six below the final row. Player-seat assignments now own y=164..173.
constexpr int kBaseCampRowY0 = 45;
constexpr int kBaseCampRowPitch = 14;
// Round-6 horizontal grid: the panel's inner face is x=8..311, with explicit
// DEPLOY and TEAM columns before the trainable NAME body. Maximum-width solo
// and network row strings end their last digit's ink at x=297/295 (EXP/LV
// columns at 264/280); the per-row move-up face sits at 300..308 — a
// two-pixel gutter after the digits and three clear panel pixels before the
// x=311 inner-face edge.
constexpr int kBaseCampDeployHeaderX = 12;
constexpr int kBaseCampTeamHeaderX = 54;
constexpr int kBaseCampNameColumnX = 88;
constexpr int kBaseCampSoloClassColumnX = 164;
constexpr int kBaseCampSoloLevelColumnX = 236;
constexpr int kBaseCampSoloExpColumnX = 264;
constexpr int kBaseCampNetCompanyColumnX = 160;
constexpr int kBaseCampNetLevelColumnX = 280;
constexpr int kBaseCampFamilySwatchGap = 1;
constexpr int kBaseCampFamilySwatchRampWidth = 8;
constexpr int kBaseCampFamilySwatchWidth = kBaseCampFamilySwatchRampWidth + 2;
constexpr int kBaseCampFamilySwatchHeight = 8;
constexpr std::array<int, kBaseCampSeatCardsPerPage> kBaseCampSeatCardX{
    54, 112, 170, 228};
constexpr int kBaseCampSeatCardWidth = 57;
constexpr int kBaseCampSeatRailY = 164;
// §9.5.4 + graft (a): non-identity fields on benched rows dim to palette
// shade 21 — GREY(23)'s glyph ramp overlaps WHITE(24) by all but one step,
// so 23-vs-24 dimming was nearly invisible. §9.23 uses it for benched
// NAME/CLASS too, while deployed identity text uses true black.
constexpr unsigned char kBenchedTextShade = 21;

void draw_base_camp_family_swatch(screen& game, int x, int row_y,
                                  short family)
{
    game.fastbox(x, row_y + 1, kBaseCampFamilySwatchWidth,
                 kBaseCampFamilySwatchHeight, PURE_BLACK);
    const unsigned char ramp_start = base_camp_family_ramp_start(family);
    for (int shade = 0; shade < kBaseCampFamilySwatchRampWidth; ++shade) {
        game.fastbox(x + 1 + shade, row_y + 2, 1,
                     kBaseCampFamilySwatchHeight - 2,
                     static_cast<unsigned char>(ramp_start + shade));
    }
}

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

RowState base_camp_add_seat_row_state(
    const MenuLabelContext& /*context*/)
{
#ifdef DISABLE_MULTIPLAYER
    return RowState::Hidden;
#else
    const std::size_t local_count = picker_lobby_local_seat_count();
    if (local_count >= static_cast<std::size_t>(MAX_PLAYERS))
        return RowState::Disabled;

    // A connected spectator owns no published seat. Activating one therefore
    // consumes the same global capacity as every other + request.
    const std::size_t global_count = picker_lobby_players().size();
    if (global_count >= og::sim::kMaxGlobalPlayers)
    {
        return RowState::Disabled;
    }
    return RowState::Visible;
#endif
}

// Static nav encodes the full-page shape (8 visible rows, pagers hidden,
// GO visible); the per-frame rewire recomputes every link from the live
// state anyway (§2.5 keyboard-nav pattern b). §9.11 (G4): the TRAIN column
// is deleted — the row BODY (84,y,214,10) is a no_draw hit zone spanning
// name/class/level/exp ink. The separate TEAM color chip at x=61 cycles the
// character's team in solo play and the right-edge ^ moves it up. no_draw,
// not a quiet face: the row text IS the affordance
// (a bevel would double-frame every row against the §9.5.2 panel), and the
// keyboard draw_highlight pulse draws regardless of no_draw, so the row
// highlight reads — the §2.5 foreign-hit-zone precedent.
#define OG_BASE_CAMP_DEP(i)                                                  \
    {.id = "roster_dep_" #i, .label = "",                                    \
     .x = 23, .y = kBaseCampRowY0 + kBaseCampRowPitch * (i), .w = 14,         \
     .h = 10, .action = ButtonAction::MenuSpecRow, .arg = (i),               \
     .nav = {.up = (i) > 0 ? (i) - 1 : -1,                                    \
             .down = (i) < 7 ? (i) + 1 : kCreateMenuBackIndex,                \
             .right = kBaseCampTeamChipBase + (i)}}
#define OG_BASE_CAMP_ROW(i)                                                  \
    {.id = "roster_row_" #i, .label = "",                                    \
     .x = 84, .y = kBaseCampRowY0 + kBaseCampRowPitch * (i), .w = 214,        \
     .h = 10, .action = ButtonAction::MenuSpecRow,                           \
     .arg = kBaseCampRowBodyBase + (i),                                      \
     .nav = {.up = (i) > 0 ? kBaseCampRowBodyBase + (i) - 1 : -1,             \
             .down = (i) < 7 ? kBaseCampRowBodyBase + (i) + 1                 \
                             : kCreateMenuGoIndex,                            \
             .left = kBaseCampTeamChipBase + (i)},                            \
     .no_draw = true}
#define OG_BASE_CAMP_TEAM(i)                                                 \
    {.id = "roster_team_" #i, .label = "",                                   \
     .x = 61, .y = kBaseCampRowY0 + kBaseCampRowPitch * (i), .w = 10,         \
     .h = 10, .action = ButtonAction::MenuSpecRow,                           \
     .arg = kBaseCampTeamChipBase + (i),                                     \
     .nav = {.up = (i) > 0 ? kBaseCampTeamChipBase + (i) - 1 : -1,            \
             .down = (i) < 7 ? kBaseCampTeamChipBase + (i) + 1                \
                            : kCreateMenuGoIndex,                             \
             .left = (i), .right = kBaseCampRowBodyBase + (i)},              \
     .no_draw = true}
#define OG_BASE_CAMP_MOVE_UP(i)                                              \
    {.id = "roster_up_" #i, .label = "^",                                    \
     .x = 300, .y = kBaseCampRowY0 + kBaseCampRowPitch * (i), .w = 9,         \
     .h = 10, .action = ButtonAction::MenuSpecRow,                           \
     .arg = kBaseCampMoveUpBase + (i),                                       \
     .nav = {.left = kBaseCampRowBodyBase + (i)},                            \
     .hidden = true}

constexpr MenuButtonSpec kBaseCampRows[] = {
    OG_BASE_CAMP_DEP(0), OG_BASE_CAMP_DEP(1), OG_BASE_CAMP_DEP(2),
    OG_BASE_CAMP_DEP(3), OG_BASE_CAMP_DEP(4), OG_BASE_CAMP_DEP(5),
    OG_BASE_CAMP_DEP(6), OG_BASE_CAMP_DEP(7),
    OG_BASE_CAMP_ROW(0), OG_BASE_CAMP_ROW(1), OG_BASE_CAMP_ROW(2),
    OG_BASE_CAMP_ROW(3), OG_BASE_CAMP_ROW(4), OG_BASE_CAMP_ROW(5),
    OG_BASE_CAMP_ROW(6), OG_BASE_CAMP_ROW(7),
    OG_BASE_CAMP_TEAM(0), OG_BASE_CAMP_TEAM(1), OG_BASE_CAMP_TEAM(2),
    OG_BASE_CAMP_TEAM(3), OG_BASE_CAMP_TEAM(4), OG_BASE_CAMP_TEAM(5),
    OG_BASE_CAMP_TEAM(6), OG_BASE_CAMP_TEAM(7),
    // Page cluster (§2.5 header line B right edge, §9.10.2 y=15 beside the
    // relocated line B); real MenuSpecRow pager actions (keyboard-live),
    // hidden until the roster spans pages.
    {.id = "roster_page_prev", .label = "<",
     .x = 263, .y = 15, .w = 14, .h = 10,
     .action = ButtonAction::MenuSpecRow, .arg = kBaseCampPagePrevIndex,
     .nav = {.down = kBaseCampRowBodyBase, .right = kBaseCampPageNextIndex},
     .hidden = true},
    {.id = "roster_page_next", .label = ">",
     .x = 302, .y = 15, .w = 14, .h = 10,
     .action = ButtonAction::MenuSpecRow, .arg = kBaseCampPageNextIndex,
     .nav = {.down = kBaseCampRowBodyBase, .left = kBaseCampPagePrevIndex},
     .hidden = true},
    // The solo SCEN status is useful navigation, not just decoration. Its
    // no-draw zone covers the full 34-char formatter budget and opens the
    // same Scenario menu as the bottom command. Network status replaces
    // this line in multiplayer, so the per-frame rewire hides the zone.
    {.id = "scenario_line", .label = "",
     .x = 6, .y = 14, .w = 208, .h = 12,
     .action = ButtonAction::CreateScenarioMenu, .arg = -1,
     .nav = {.down = kCreateMenuScenarioIndex},
     .no_draw = true},
    // Bottom command strip (y=178, 18px tall). BACK keeps the Escape hotkey
    // (the shared cancel grammar).
    {.id = "back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 8, .y = 178, .w = 44, .h = 18,
     .action = ButtonAction::ReturnMenu, .arg = MENU_EXIT,
     .nav = {.up = 7, .right = kCreateMenuHireIndex}},
    {.id = "hire_troops", .label = "HIRE",
     .x = 58, .y = 178, .w = 50, .h = 18,
     .action = ButtonAction::CreateHireMenu, .arg = -1,
     .nav = {.up = 7, .left = kCreateMenuBackIndex,
             .right = kCreateMenuScenarioIndex}},
    {.id = "scenario", .label = "SCENARIO",
     .x = 114, .y = 178, .w = 62, .h = 18,
     .action = ButtonAction::CreateScenarioMenu, .arg = -1,
     .nav = {.up = kBaseCampScenarioLineIndex, .left = kCreateMenuHireIndex,
             .right = kCreateMenuNetworkingIndex}},
    {.id = "networking", .label = "NETWORK",
     .x = 182, .y = 178, .w = 56, .h = 18,
     .action = ButtonAction::Networking, .arg = -1,
     .nav = {.up = kBaseCampRowBodyBase + 7, .left = kCreateMenuScenarioIndex,
             .right = kCreateMenuGoIndex}},
    // §2.6 GO half of the dual-role slot: solo/local keeps the plain grey
    // bevel and the exact legacy behavior (GoMenu -> go_menu / the
    // TeamBuild-scope StartGame interception — pinned byte-identical);
    // networked hosts get the state-3/4 face via the color binding.
    {.id = "go", .label = "GO",
     .x = 244, .y = 178, .w = 68, .h = 18,
     .action = ButtonAction::GoMenu, .arg = -1,
     .nav = {.up = kBaseCampRowBodyBase + 7,
             .left = kCreateMenuNetworkingIndex},
     .color = &base_camp_go_face_color},
    // §2.6 READY twin: the SAME rect, visible exactly when GO is not
    // (networked joiner). Keeps the existing ToggleLobbyReady action (§8
    // resolution 1 — interact("go") and the StartGame interception survive
    // untouched); label/face re-derive per frame from the lobby state.
    {.id = "ready", .label = "READY",
     .x = 244, .y = 178, .w = 68, .h = 18,
     .action = ButtonAction::ToggleLobbyReady, .arg = kCreateMenuReadyIndex,
     .nav = {.up = kBaseCampRowBodyBase + 7,
             .left = kCreateMenuNetworkingIndex},
     .label_binding = {.formatter = &base_camp_ready_label},
     .color = &base_camp_ready_face_color,
     .hidden = true},
    // Per-level player-seat assignments. The four-card window is independent
    // of the character-roster pager above; the rewire fills authoritative P#
    // labels, ownership, visibility, and navigation every frame.
    {.id = "seats", .label = "SEATS",
     .x = 8, .y = kBaseCampSeatRailY, .w = 34, .h = 10,
     .action = ButtonAction::MenuSpecRow, .arg = kBaseCampSeatsLabelIndex,
     .nav = {.down = kCreateMenuBackIndex, .right = kBaseCampSeatPagePrevIndex}},
    {.id = "seat_page_prev", .label = "<",
     .x = 44, .y = kBaseCampSeatRailY, .w = 8, .h = 10,
     .action = ButtonAction::MenuSpecRow, .arg = kBaseCampSeatPagePrevIndex,
     .nav = {.left = kBaseCampSeatsLabelIndex,
             .right = kBaseCampSeatCardBase},
     .hidden = true},
    {.id = "seat_card_0", .label = "",
     .x = 54, .y = kBaseCampSeatRailY, .w = kBaseCampSeatCardWidth, .h = 10,
     .action = ButtonAction::MenuSpecRow, .arg = kBaseCampSeatCardBase,
     .nav = {.left = kBaseCampSeatPagePrevIndex,
             .right = kBaseCampSeatCardBase + 1}},
    {.id = "seat_card_1", .label = "",
     .x = 112, .y = kBaseCampSeatRailY, .w = kBaseCampSeatCardWidth, .h = 10,
     .action = ButtonAction::MenuSpecRow, .arg = kBaseCampSeatCardBase + 1,
     .nav = {.left = kBaseCampSeatCardBase,
             .right = kBaseCampSeatCardBase + 2}},
    {.id = "seat_card_2", .label = "",
     .x = 170, .y = kBaseCampSeatRailY, .w = kBaseCampSeatCardWidth, .h = 10,
     .action = ButtonAction::MenuSpecRow, .arg = kBaseCampSeatCardBase + 2,
     .nav = {.left = kBaseCampSeatCardBase + 1,
             .right = kBaseCampSeatCardBase + 3}},
    {.id = "seat_card_3", .label = "",
     .x = 228, .y = kBaseCampSeatRailY, .w = kBaseCampSeatCardWidth, .h = 10,
     .action = ButtonAction::MenuSpecRow, .arg = kBaseCampSeatCardBase + 3,
     .nav = {.left = kBaseCampSeatCardBase + 2,
             .right = kBaseCampSeatPageNextIndex}},
    {.id = "seat_page_next", .label = ">",
     .x = 287, .y = kBaseCampSeatRailY, .w = 8, .h = 10,
     .action = ButtonAction::MenuSpecRow, .arg = kBaseCampSeatPageNextIndex,
     .nav = {.down = kCreateMenuGoIndex,
             .left = kBaseCampSeatCardBase + 3},
     .hidden = true},
    {.id = "add_seat", .label = "+",
     .x = 297, .y = kBaseCampSeatRailY, .w = 14, .h = 10,
     .action = ButtonAction::MenuSpecRow, .arg = kBaseCampAddSeatIndex,
     .nav = {.down = kCreateMenuGoIndex,
             .left = kBaseCampSeatPageNextIndex},
     .state_override = &base_camp_add_seat_row_state},
    OG_BASE_CAMP_MOVE_UP(0), OG_BASE_CAMP_MOVE_UP(1),
    OG_BASE_CAMP_MOVE_UP(2), OG_BASE_CAMP_MOVE_UP(3),
    OG_BASE_CAMP_MOVE_UP(4), OG_BASE_CAMP_MOVE_UP(5),
    OG_BASE_CAMP_MOVE_UP(6), OG_BASE_CAMP_MOVE_UP(7),
};

#undef OG_BASE_CAMP_DEP
#undef OG_BASE_CAMP_ROW
#undef OG_BASE_CAMP_TEAM
#undef OG_BASE_CAMP_MOVE_UP

static_assert(static_cast<int>(std::size(kBaseCampRows))
                  == kCreateMenuButtonCount,
              "base camp spec ordinals are the layout contract");

std::string base_camp_company_abbreviation(std::string_view company)
{
    std::string result;
    result.reserve(3);
    for (const unsigned char ch : company) {
        if (!std::isalnum(ch))
            continue;
        result.push_back(static_cast<char>(std::toupper(ch)));
        if (result.size() == 3)
            break;
    }
    return result.empty() ? "NET" : result;
}

bool base_camp_seat_is_local(const BaseCampScreenState& state,
                             std::uint8_t player_index)
{
    return std::find(state.local_seat_indices.begin(),
                     state.local_seat_indices.end(),
                     player_index) != state.local_seat_indices.end();
}

std::string base_camp_seat_label(const BaseCampScreenState& state,
                                 const og::sim::LobbyPlayer& seat)
{
    const bool local =
        base_camp_seat_is_local(state, seat.player_index);
    const std::string owner =
        local && picker_lobby_local_seat_count() == 0
        ? std::string("SPEC")
        : (local ? std::string("YOU")
                 : base_camp_company_abbreviation(seat.company));
    // The trailing visual pad shifts the visible centered ink one half-cell
    // left, keeping the 8px numbered team chip clear on the compact 57px face.
    return std::format("P{} {} ", static_cast<int>(seat.player_index) + 1,
                       owner);
}

std::vector<short> base_camp_selectable_seat_teams(const SaveData& save)
{
    og::sim::LobbySettings settings;
    settings.campaign_id = save.current_campaign;
    settings.ctf_team_count = save.ctf_team_count;
    settings.shared_teams = is_versus_campaign(save) ? 1 : 0;

    // Use the same authored-team domain as lobby authority and CTF gameplay:
    // explicit N means the first N authored flag teams, not numeric [0,N).
    // A joining peer between map syncs publishes no mask and temporarily gets
    // the protocol fallback (all teams for Auto, first N for explicit).
    if (og::runtime::current_session != nullptr &&
        og::runtime::current_session->myscreen_ != nullptr)
    {
        settings.ctf_authored_team_mask =
            og::ui::ctf_authored_team_mask_for_loaded_level(
                save,
                og::runtime::current_session->myscreen_->world(),
                get_mounted_campaign());
    }

    std::vector<short> teams;
    const std::uint8_t team_mask =
        picker_lobby_authoritative_team_mask().value_or(
            og::sim::lobby_effective_team_mask(settings));
    for (short team = 0; team < SCORE_TEAM_COUNT; ++team)
    {
        const auto bit =
            static_cast<std::uint8_t>(1u << static_cast<unsigned>(team));
        if ((team_mask & bit) != 0)
            teams.push_back(team);
    }
    return teams;
}

// A local seat editor is keyed by the authority-issued token, never the
// dense P# printed on its card. The latter can change while this blocking
// screen is open; local_slot is re-derived each frame and is the only value
// passed to the four persistent controller profiles.
SeatSettingsScreenState* g_seat_settings_state = nullptr;

std::vector<std::uint8_t> seat_settings_local_indices(
    const std::vector<og::sim::LobbyPlayer>& players)
{
    if (picker_lobby_is_networked())
        return picker_lobby_local_player_indices();

    std::vector<std::uint8_t> indices;
    indices.reserve(players.size());
    for (const og::sim::LobbyPlayer& player : players)
        indices.push_back(player.player_index);
    // Local/offline P# is the controller-profile ordinal. Lobby snapshots
    // normally arrive dense and ordered, but never let transient message
    // order turn displayed P1 into profile 3 (whose 4-dir YELL is U).
    std::sort(indices.begin(), indices.end());
    return indices;
}

bool resolve_seat_settings_player(SeatSettingsScreenState& state,
                                  og::sim::LobbyPlayer& out)
{
    const std::vector<og::sim::LobbyPlayer> players =
        picker_lobby_players();
    auto player = players.end();
    if (state.seat_id != og::sim::kInvalidLobbySeatId) {
        player = std::find_if(
            players.begin(), players.end(),
            [&state](const og::sim::LobbyPlayer& candidate) {
                return candidate.seat_id == state.seat_id;
            });
    } else {
        player = std::find_if(
            players.begin(), players.end(),
            [&state](const og::sim::LobbyPlayer& candidate) {
                return candidate.player_index == state.player_index;
            });
    }
    if (player == players.end())
        return false;

    const std::vector<std::uint8_t> local_indices =
        seat_settings_local_indices(players);
    const auto local = std::find(local_indices.begin(), local_indices.end(),
                                 player->player_index);
    if (local == local_indices.end())
        return false;

    const int local_slot =
        static_cast<int>(std::distance(local_indices.begin(), local));
    if (local_slot < 0 ||
        static_cast<std::size_t>(local_slot) >=
            picker_lobby_local_seat_count())
    {
        return false;  // connected spectator placeholder, not an active seat
    }

    state.player_index = player->player_index;
    state.local_slot = local_slot;
    out = *player;
    return true;
}

RowState seat_settings_remove_row_state(
    const MenuLabelContext& /*context*/)
{
    if (g_seat_settings_state == nullptr)
        return RowState::Disabled;
    if (!picker_lobby_is_networked() &&
        picker_lobby_local_seat_count() <= 1)
    {
        return RowState::Disabled;
    }
    return RowState::Visible;
}

constexpr MenuButtonSpec kSeatSettingsRowsMP[] = {
    {.id = "seat_settings_back", .label = "BACK",
     .hotkey = KEYSTATE_ESCAPE,
     .x = 10, .y = 8, .w = 50, .h = 15,
     .action = ButtonAction::ReturnMenu, .arg = MENU_REDRAW,
     .nav = {.up = kSeatSettingsTeamIndex,
             .down = kSeatSettingsModeIndex}},
    {.id = "seat_team", .label = "TEAM 1",
     .x = 16, .y = 169, .w = 138, .h = 18,
     .action = ButtonAction::MenuSpecRow, .arg = kSeatSettingsTeamIndex,
     .nav = {.up = kSeatSettingsModeIndex,
             .down = kSeatSettingsBackIndex,
             .right = kSeatSettingsRemoveIndex}},
    {.id = "seat_direction", .label = "4-DIRECTION",
     .x = 16, .y = 38, .w = 98, .h = 18,
     .action = ButtonAction::MenuSpecRow, .arg = kSeatSettingsModeIndex,
     .nav = {.up = kSeatSettingsBackIndex,
             .down = kSeatSettingsTeamIndex,
             .right = kSeatSettingsRemapIndex}},
    {.id = "seat_remap", .label = "REMAP",
     .x = 123, .y = 38, .w = 86, .h = 18,
     .action = ButtonAction::MenuSpecRow, .arg = kSeatSettingsRemapIndex,
     .nav = {.up = kSeatSettingsBackIndex,
             .down = kSeatSettingsRemoveIndex,
             .left = kSeatSettingsModeIndex,
             .right = kSeatSettingsResetIndex}},
    {.id = "seat_reset", .label = "RESET",
     .x = 218, .y = 38, .w = 86, .h = 18,
     .action = ButtonAction::MenuSpecRow, .arg = kSeatSettingsResetIndex,
     .nav = {.up = kSeatSettingsBackIndex,
             .down = kSeatSettingsRemoveIndex,
             .left = kSeatSettingsRemapIndex}},
    {.id = "seat_remove", .label = "REMOVE PLAYER",
     .x = 166, .y = 169, .w = 138, .h = 18,
     .action = ButtonAction::MenuSpecRow, .arg = kSeatSettingsRemoveIndex,
     .nav = {.up = kSeatSettingsResetIndex,
             .down = kSeatSettingsBackIndex,
             .left = kSeatSettingsTeamIndex},
     .state_override = &seat_settings_remove_row_state},
};

constexpr MenuButtonSpec kSeatSettingsRowsNoMP[] = {
    {.id = "seat_settings_back", .label = "BACK",
     .hotkey = KEYSTATE_ESCAPE,
     .x = 10, .y = 8, .w = 50, .h = 15,
     .action = ButtonAction::ReturnMenu, .arg = MENU_REDRAW,
     .nav = {.up = kSeatSettingsTeamIndex,
             .down = kSeatSettingsModeIndex}},
    {.id = "seat_team", .label = "TEAM 1",
     .x = 91, .y = 169, .w = 138, .h = 18,
     .action = ButtonAction::MenuSpecRow, .arg = kSeatSettingsTeamIndex,
     .nav = {.up = kSeatSettingsRemapIndex,
             .down = kSeatSettingsBackIndex}},
    {.id = "seat_direction", .label = "4-DIRECTION",
     .x = 16, .y = 38, .w = 98, .h = 18,
     .action = ButtonAction::MenuSpecRow, .arg = kSeatSettingsModeIndex,
     .nav = {.up = kSeatSettingsBackIndex,
             .down = kSeatSettingsTeamIndex,
             .right = kSeatSettingsRemapIndex}},
    {.id = "seat_remap", .label = "REMAP",
     .x = 123, .y = 38, .w = 86, .h = 18,
     .action = ButtonAction::MenuSpecRow, .arg = kSeatSettingsRemapIndex,
     .nav = {.up = kSeatSettingsBackIndex,
             .down = kSeatSettingsTeamIndex,
             .left = kSeatSettingsModeIndex,
             .right = kSeatSettingsResetIndex}},
    {.id = "seat_reset", .label = "RESET",
     .x = 218, .y = 38, .w = 86, .h = 18,
     .action = ButtonAction::MenuSpecRow, .arg = kSeatSettingsResetIndex,
     .nav = {.up = kSeatSettingsBackIndex,
             .down = kSeatSettingsTeamIndex,
             .left = kSeatSettingsRemapIndex}},
};

void sync_seat_settings_label(button* buttons, int index,
                              const std::string& label)
{
    buttons[index].label = label;
    if (index >= 0 &&
        index < static_cast<int>(
            og::runtime::current_session->allbuttons_.size()))
    {
        if (vbutton* const live =
                og::runtime::current_session->allbuttons_[index])
        {
            live->label = label;
        }
    }
}

void seat_settings_rewire(button* buttons, int count,
                          int& highlighted_button)
{
    if (buttons == nullptr || g_seat_settings_state == nullptr)
        return;
    const int expected =
#ifdef DISABLE_MULTIPLAYER
        kSeatSettingsButtonCountNoMP;
#else
        kSeatSettingsButtonCountMP;
#endif
    if (count < expected)
        return;

    og::sim::LobbyPlayer player;
    if (!resolve_seat_settings_player(*g_seat_settings_state, player))
        return;

    sync_seat_settings_label(
        buttons, kSeatSettingsTeamIndex,
        std::format("TEAM {}", static_cast<int>(player.team) + 1));
    const bool eight_dir =
        get_player_control_mode(g_seat_settings_state->local_slot) ==
        static_cast<int>(ControlDirectionMode::EightDirection);
    sync_seat_settings_label(
        buttons, kSeatSettingsModeIndex,
        eight_dir ? "8-DIRECTION" : "4-DIRECTION");
#ifndef DISABLE_MULTIPLAYER
    sync_seat_settings_label(
        buttons, kSeatSettingsRemoveIndex,
        picker_lobby_is_networked() &&
                picker_lobby_local_seat_count() == 1
            ? "SPECTATE"
            : "REMOVE PLAYER");
#endif

    ensure_highlighted_button_visible(buttons, count, highlighted_button);
}

void seat_settings_draw_content(void* screen_state)
{
    auto* const state =
        static_cast<SeatSettingsScreenState*>(screen_state);
    if (state == nullptr)
        return;

    og::sim::LobbyPlayer player;
    if (!resolve_seat_settings_player(*state, player))
        return;

    screen* const game = og::runtime::current_session->myscreen_;
    text& mytext = game->text_normal;
    mytext.write_xy_center(
        160, 13, DARK_BLUE, "%s",
        std::format("LOCAL PLAYER {} / P{}", state->local_slot + 1,
                    static_cast<int>(player.player_index) + 1)
            .c_str());

    game->draw_button(12, 62, 308, 159, 2, 1);
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
        get_player_control_mode(state->local_slot) ==
        static_cast<int>(ControlDirectionMode::EightDirection);

    for (std::size_t index = 0; index < movement.size(); ++index) {
        const BindingLine& binding = movement[index];
        const bool active = !binding.diagonal || eight_dir;
        const std::string value = active
            ? player_control_key_display_name(state->local_slot, binding.key)
            : std::string("--");
        const std::string line =
            std::format("{}: {}", binding.label, value);
        mytext.write_xy(20, 77 + static_cast<int>(index) * 10,
                        active ? DARK_BLUE : GREY, "%s", line.c_str());
    }
    for (std::size_t index = 0; index < actions.size(); ++index) {
        const BindingLine& binding = actions[index];
        const std::string line = std::format(
            "{}: {}", binding.label,
            player_control_key_display_name(state->local_slot, binding.key));
        mytext.write_xy(164, 77 + static_cast<int>(index) * 10,
                        DARK_BLUE, "%s", line.c_str());
    }
}

bool seat_settings_frame_tick(void* screen_state, int /*frame*/)
{
    auto* const state =
        static_cast<SeatSettingsScreenState*>(screen_state);
    if (state == nullptr || state->removed)
        return false;

    og::sim::LobbyPlayer player;
    if (resolve_seat_settings_player(*state, player))
        return true;

    if (!state->missing_notice_shown) {
        state->missing_notice_shown = true;
        popup_dialog("SEAT LEFT", "RETURNING TO BASE CAMP");
    }
    return false;
}

void persist_player_controls()
{
    save_player_control_settings_to_cfg(cfg);
    cfg.save_settings();
}

Sint32 seat_settings_on_spec_row(int row, void* screen_state)
{
    auto* const state =
        static_cast<SeatSettingsScreenState*>(screen_state);
    if (state == nullptr)
        return MENU_OK;

    og::sim::LobbyPlayer player;
    if (!resolve_seat_settings_player(*state, player))
        return MENU_REDRAW;

    if (row == kSeatSettingsTeamIndex) {
        const SaveData& save =
            og::runtime::current_session->myscreen_->save_data;
        const std::vector<short> teams =
            base_camp_selectable_seat_teams(save);
        if (teams.empty())
            return MENU_OK;
        const auto current =
            std::find(teams.begin(), teams.end(), player.team);
        const short next_team = current == teams.end()
            ? teams.front()
            : teams[(static_cast<std::size_t>(
                          std::distance(teams.begin(), current)) +
                      1) %
                    teams.size()];
        if (!picker_lobby_request_seat_team_change(
                player.player_index, player.seat_id, next_team))
        {
            popup_dialog("TEAM", "CHANGE DENIED");
            return MENU_OK;
        }
        TRACE("basecamp", "seat_team player=%d team=%d",
              static_cast<int>(player.player_index) + 1,
              static_cast<int>(next_team) + 1);
        return MENU_OK;
    }

    if (row == kSeatSettingsModeIndex) {
        (void)toggle_player_control_mode(state->local_slot);
        persist_player_controls();
        return MENU_OK;
    }
    if (row == kSeatSettingsRemapIndex) {
        (void)edit_player_keymap(state->local_slot);
        persist_player_controls();
        return MENU_OK;
    }
    if (row == kSeatSettingsResetIndex) {
        if (reset_default_player_controls_for_player(state->local_slot))
            persist_player_controls();
        return MENU_OK;
    }

#ifndef DISABLE_MULTIPLAYER
    if (row == kSeatSettingsRemoveIndex) {
        const int active_count =
            static_cast<int>(picker_lobby_local_seat_count());
        const bool spectate =
            picker_lobby_is_networked() && active_count == 1;
        if (!picker_lobby_is_networked() && active_count <= 1)
            return MENU_OK;
        if (!no_or_yes_prompt(
                spectate ? "SPECTATE?" : "REMOVE PLAYER?",
                spectate ? "LEAVE THIS PLAYER SEAT?"
                         : "REMOVE THIS PLAYER SEAT?",
                false))
        {
            return MENU_OK;
        }
        const int removed_slot = state->local_slot;
        if (!picker_lobby_remove_local_seat(
                player.player_index, player.seat_id))
        {
            popup_dialog("SEAT", "REMOVE DENIED");
            return MENU_OK;
        }
        if (!compact_player_controls_after_removal(
                removed_slot, active_count))
        {
            popup_dialog("CONTROLS", "COULD NOT COMPACT PROFILES");
        }
        persist_player_controls();
        state->removed = true;
        TRACE("basecamp", "seat_remove player=%d slot=%d",
              static_cast<int>(player.player_index) + 1, removed_slot + 1);
        return MENU_REDRAW;
    }
#endif
    return MENU_OK;
}

Sint32 run_seat_settings_menu(const og::sim::LobbyPlayer& seat,
                              int local_slot)
{
    SeatSettingsScreenState state{
        .seat_id = seat.seat_id,
        .player_index = seat.player_index,
        .local_slot = local_slot,
    };
    SeatSettingsScreenState* const previous = g_seat_settings_state;
    g_seat_settings_state = &state;
    // A READY peer must never be stranded inside the synchronous key-remap
    // prompts while the host starts. Opening any seat editor first withdraws
    // this machine's readiness; the user explicitly readies again afterward.
    if (picker_lobby_is_networked())
        (void)picker_lobby_set_ready(false);
    const Sint32 result =
        run_menu_screen(seat_settings_menu_screen_spec(), &state);
    g_seat_settings_state = previous;
    return result;
}

constexpr MenuScreenSpec make_seat_settings_spec(
    const MenuButtonSpec* rows, int row_count)
{
    MenuScreenSpec spec{};
    spec.name = "seat_settings";
    spec.rows = rows;
    spec.row_count = row_count;
    spec.buttons_accessor = &picker_seat_settings_buttons;
    spec.count_accessor = &picker_seat_settings_button_count;
    spec.nav = {.kind = NavProgramKind::Rewire,
                .rewire = &seat_settings_rewire};
    spec.remote_start = RemoteStartScope::TeamBuildScope;
    spec.remote_start_exit = RemoteStartExit::ReturnMenuExit;
    spec.default_highlight = kSeatSettingsModeIndex;
    spec.exit_on_redraw = true;
    spec.polls_lobby = true;
    spec.draw_background = &options_panel_draw_background;
    spec.draw_content = &seat_settings_draw_content;
    spec.frame_tick = &seat_settings_frame_tick;
    spec.on_spec_row = &seat_settings_on_spec_row;
    spec.exit_value = MENU_REDRAW;
    return spec;
}

// Per-frame visibility + labels + nav over the live roster state (pattern
// b): page-window the rows, stamp the deploy glyphs on BOTH label surfaces,
// host-gate GO, close the strip/pager links, and seed the empty-roster
// highlight on HIRE (§2.5). Networked ownership shape: a foreign row's
// deploy button widens into the full-panel no_draw hit zone (12,y,300,10) — click
// anywhere on the row pops OWNED BY — and its §9.11 row-body zone hides
// (the widened dep zone IS the foreign row click); the nav graph chains
// the row-body column over the OWNED rows only.
void base_camp_rewire(button* buttons, int count, int& highlighted_button)
{
    if (buttons == nullptr || count < kCreateMenuButtonCount)
        return;
    const BaseCampScreenState* st = g_base_camp_state;
    const int first = st != nullptr ? st->page.first_index() : 0;
    const int end = st != nullptr ? st->page.end_index() : 0;
    const int visible = std::max(0, end - first);
    const bool pagers = st != nullptr && st->page.multi_page();
    const int seat_first = st != nullptr ? st->seat_page.first_index() : 0;
    const int seat_end = st != nullptr ? st->seat_page.end_index() : 0;
    const int seat_visible = std::max(0, seat_end - seat_first);
    const bool seat_pagers =
        st != nullptr && st->seat_page.multi_page();
    const bool networked = picker_lobby_is_networked();
    const bool host_visible = picker_lobby_host_controls_visible();
    // §2.6: the GO/READY pair shares one rect with mutually exclusive gates
    // — the host keeps GO, a networked joiner gets READY, never both. A
    // non-host peer implies a networked session, so READY keys on both
    // (the gate-lattice sweep drives the (host=false, networked=true) shape).
    const bool ready_visible =
        picker_lobby_is_networked() && !host_visible &&
        picker_lobby_local_seat_count() > 0;
    // The strip's right end: whichever of the pair is visible this frame.
    const int strip_end = host_visible
        ? kCreateMenuGoIndex
        : (ready_visible ? kCreateMenuReadyIndex : -1);
    const SaveData& save = og::runtime::current_session->myscreen_->save_data;
#ifdef DISABLE_MULTIPLAYER
    buttons[kBaseCampAddSeatIndex].hidden = true;
#endif
    const bool add_visible = !buttons[kBaseCampAddSeatIndex].hidden;
    const int seat_card_anchor = seat_visible > 0
        ? kBaseCampSeatCardBase
        : (add_visible ? kBaseCampAddSeatIndex
                       : kBaseCampSeatsLabelIndex);

    // Player-seat rail: P# is the current dense authoritative lobby position
    // and may change when seats leave; YOU is derived from exact current local
    // ownership, while remote cards carry a compact company prefix. Both pager
    // arrows appear together only when more than four seats exist.
    buttons[kBaseCampSeatPagePrevIndex].hidden = !seat_pagers;
    buttons[kBaseCampSeatPageNextIndex].hidden = !seat_pagers;
    for (int card = 0; card < kBaseCampSeatCardsPerPage; ++card) {
        button& card_button = buttons[kBaseCampSeatCardBase + card];
        card_button.hidden = card >= seat_visible;
        if (card_button.hidden)
            continue;
        const og::sim::LobbyPlayer& seat =
            st->seats[static_cast<std::size_t>(seat_first + card)];
        card_button.label = base_camp_seat_label(*st, seat);
        if (vbutton* const live =
                og::runtime::current_session
                    ->allbuttons_[kBaseCampSeatCardBase + card])
        {
            live->label = card_button.label;
        }
    }
    std::vector<int> rail;
    rail.push_back(kBaseCampSeatsLabelIndex);
    if (seat_pagers)
        rail.push_back(kBaseCampSeatPagePrevIndex);
    for (int card = 0; card < seat_visible; ++card)
        rail.push_back(kBaseCampSeatCardBase + card);
    if (seat_pagers)
        rail.push_back(kBaseCampSeatPageNextIndex);
    if (add_visible)
        rail.push_back(kBaseCampAddSeatIndex);
    for (std::size_t index = 0; index < rail.size(); ++index) {
        button& control = buttons[rail[index]];
        control.nav.left = index > 0 ? rail[index - 1] : -1;
        control.nav.right =
            index + 1 < rail.size() ? rail[index + 1] : -1;
    }

    // Ownership per visible row (own row => a real deploy toggle + the
    // §9.11 row-body train zone; solo adds the team-color cycler). Every
    // owned member except the first also exposes a right-edge move-up button.
    std::array<bool, kBaseCampRosterRowsPerPage> owned_row{};
    std::array<bool, kBaseCampRosterRowsPerPage> movable_row{};
    for (int r = 0; r < kBaseCampRosterRowsPerPage; ++r) {
        if (r >= visible)
            continue;
        const int display_index = first + r;
        const BaseCampDisplaySlot& slot =
            st->slots[static_cast<std::size_t>(display_index)];
        owned_row[static_cast<std::size_t>(r)] = slot.owned;
        movable_row[static_cast<std::size_t>(r)] =
            slot.owned &&
            std::any_of(st->slots.begin(),
                        st->slots.begin() + display_index,
                        [](const BaseCampDisplaySlot& candidate) {
                            return candidate.owned;
                        });
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
    const auto next_movable = [&](int from) {
        for (int r = from; r < visible; ++r)
            if (movable_row[static_cast<std::size_t>(r)])
                return r;
        return -1;
    };
    const auto prev_movable = [&](int from) {
        for (int r = from; r >= 0; --r)
            if (movable_row[static_cast<std::size_t>(r)])
                return r;
        return -1;
    };

    for (int r = 0; r < kBaseCampRosterRowsPerPage; ++r) {
        const bool on = r < visible;
        const bool own = on && owned_row[static_cast<std::size_t>(r)];
        const bool team_cycler = own && !networked;
        const bool move_up =
            on && movable_row[static_cast<std::size_t>(r)];
        button& dep = buttons[r];
        button& body = buttons[kBaseCampRowBodyBase + r];
        button& chip = buttons[kBaseCampTeamChipBase + r];
        button& move = buttons[kBaseCampMoveUpBase + r];
        dep.hidden = !on;
        body.hidden = !own;
        chip.hidden = !team_cycler;
        move.hidden = !move_up;
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
        dep.x = own ? 23 : 12;
        dep.sizex = own ? 14 : 300;
        vbutton* live = og::runtime::current_session->allbuttons_[r];
        if (live != nullptr) {
            live->label = dep.label;
            live->no_draw = dep.no_draw;
            live->xloc = dep.x;
            live->width = dep.sizex;
            live->xend = live->xloc + live->width;
        }

        // Own solo rows step through deploy -> team color -> name/train.
        // Networked rows skip the team cycler because lobby teams are
        // assigned. A foreign
        // hit zone spans the whole row, so its right-link carries the
        // row-body column's pager duty — without it an all-foreign page
        // (spectator machine, [NET-R9]) strands the pagers.
        dep.nav = {.up = r > 0 ? r - 1 : -1,
                   .down = r + 1 < visible
                       ? r + 1
                       : kBaseCampSeatsLabelIndex,
                   .left = -1,
                   .right = own ? (team_cycler ? kBaseCampTeamChipBase + r
                                               : kBaseCampRowBodyBase + r)
                                : (pagers ? kBaseCampPagePrevIndex : -1)};
        if (own) {
            const int up_owned = prev_owned(r - 1);
            const int down_owned = next_owned(r + 1);
            body.nav = {
                .up = up_owned >= 0 ? kBaseCampRowBodyBase + up_owned : -1,
                .down = down_owned >= 0
                    ? kBaseCampRowBodyBase + down_owned
                    : seat_card_anchor,
                .left = team_cycler ? kBaseCampTeamChipBase + r : r,
                .right = move_up
                    ? kBaseCampMoveUpBase + r
                    : (pagers ? kBaseCampPagePrevIndex : -1)};
            if (move_up) {
                const int up_movable = prev_movable(r - 1);
                const int down_movable = next_movable(r + 1);
                move.nav = {
                    .up = up_movable >= 0
                        ? kBaseCampMoveUpBase + up_movable
                        : -1,
                    .down = down_movable >= 0
                        ? kBaseCampMoveUpBase + down_movable
                        : seat_card_anchor,
                    .left = kBaseCampRowBodyBase + r,
                    .right = pagers ? kBaseCampPagePrevIndex : -1};
            }
            if (team_cycler) {
                chip.nav = {
                    .up = r > 0 ? kBaseCampTeamChipBase + r - 1 : -1,
                    .down = r + 1 < visible
                        ? kBaseCampTeamChipBase + r + 1
                        : seat_card_anchor,
                    .left = r,
                    .right = kBaseCampRowBodyBase + r};
            }
        }
    }

    buttons[kBaseCampPagePrevIndex].hidden = !pagers;
    buttons[kBaseCampPageNextIndex].hidden = !pagers;
    // Pager down-links land on the row-body column's first OWNED row and
    // fall back to the dep column on all-foreign pages.
    const int pager_down = first_owned >= 0
        ? kBaseCampRowBodyBase + first_owned
        : (visible > 0
               ? 0
               : kBaseCampSeatsLabelIndex);
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

    // Solo line B is the current scenario and doubles as a generous click
    // target for its menu. Multiplayer replaces line B with connection
    // status, so it must never retain the scenario action there.
    buttons[kBaseCampScenarioLineIndex].hidden = networked;
    buttons[kBaseCampScenarioLineIndex].nav = {
        .up = -1,
        .down = kCreateMenuScenarioIndex,
        .left = -1,
        .right = -1};

    // §2.6: exactly one of the same-rect GO/READY pair shows — GO for the
    // host (the legacy host-gate, verbatim rule), READY for a networked
    // joiner with at least one active seat. A zero-seat spectator remains
    // connected but has nothing the ready gate can bind.
    buttons[kCreateMenuGoIndex].hidden = !host_visible;
    buttons[kCreateMenuReadyIndex].hidden = !ready_visible;

    const int dep_last = visible > 0 ? visible - 1 : -1;
    // The strip's right-side up-links prefer the row-body column and fall
    // back to the dep column when this page has no owned row (all-foreign
    // page / spectator machine, [NET-R9]).
    const int body_last =
        last_owned >= 0 ? kBaseCampRowBodyBase + last_owned : dep_last;
    const int rail_up_left = dep_last;
    const int rail_up_right = body_last;
    buttons[kBaseCampSeatsLabelIndex].nav.up = rail_up_left;
    buttons[kBaseCampSeatsLabelIndex].nav.down = kCreateMenuBackIndex;
    buttons[kBaseCampSeatPagePrevIndex].nav.up = rail_up_left;
    buttons[kBaseCampSeatPagePrevIndex].nav.down = kCreateMenuBackIndex;
    constexpr std::array<int, kBaseCampSeatCardsPerPage> card_down{
        kCreateMenuHireIndex,
        kCreateMenuScenarioIndex,
        kCreateMenuNetworkingIndex,
        kCreateMenuGoIndex};
    for (int card = 0; card < seat_visible; ++card) {
        button& card_button = buttons[kBaseCampSeatCardBase + card];
        card_button.nav.up = rail_up_right;
        if (card == kBaseCampSeatCardsPerPage - 1) {
            // Card four historically lands on the GO/READY slot. A connected
            // zero-seat guest has neither half of that slot, so keep the old
            // strip-column relationship but fall back one door to NETWORK
            // instead of leaving keyboard focus pointed at a hidden row.
            card_button.nav.down =
                strip_end >= 0 ? strip_end : kCreateMenuNetworkingIndex;
        } else {
            card_button.nav.down =
                card_down[static_cast<std::size_t>(card)];
        }
    }
    buttons[kBaseCampSeatPageNextIndex].nav.up = rail_up_right;
    buttons[kBaseCampSeatPageNextIndex].nav.down =
        strip_end >= 0 ? strip_end : kCreateMenuNetworkingIndex;
    buttons[kBaseCampAddSeatIndex].nav.up = rail_up_right;
    buttons[kBaseCampAddSeatIndex].nav.down =
        strip_end >= 0 ? strip_end : kCreateMenuNetworkingIndex;
    const auto visible_card_or_label = [seat_visible](int card) {
        return card >= 0 && card < seat_visible
            ? kBaseCampSeatCardBase + card
            : kBaseCampSeatsLabelIndex;
    };
    buttons[kCreateMenuBackIndex].nav = {
        .up = kBaseCampSeatsLabelIndex, .down = -1, .left = -1,
        .right = kCreateMenuHireIndex};
    buttons[kCreateMenuHireIndex].nav = {
        .up = visible_card_or_label(0), .down = -1,
        .left = kCreateMenuBackIndex,
        .right = kCreateMenuScenarioIndex};
    buttons[kCreateMenuScenarioIndex].nav = {
        .up = networked
            ? visible_card_or_label(1)
            : kBaseCampScenarioLineIndex,
        .down = -1, .left = kCreateMenuHireIndex,
        .right = kCreateMenuNetworkingIndex};
    buttons[kCreateMenuNetworkingIndex].nav = {
        .up = visible_card_or_label(2), .down = -1,
        .left = kCreateMenuScenarioIndex,
        .right = strip_end};
    buttons[kCreateMenuGoIndex].nav = {
        .up = add_visible
            ? kBaseCampAddSeatIndex
            : (seat_pagers
                   ? kBaseCampSeatPageNextIndex
                   : visible_card_or_label(3)),
        .down = -1,
        .left = kCreateMenuNetworkingIndex, .right = -1};
    buttons[kCreateMenuReadyIndex].nav = {
        .up = add_visible
            ? kBaseCampAddSeatIndex
            : (seat_pagers
                   ? kBaseCampSeatPageNextIndex
                   : visible_card_or_label(3)),
        .down = -1,
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

// Pre-buttons pass (§2.0 draw-order rule): backdrop, then one classic
// two-bevel grey panel behind the complete roster, matching the retired
// View Team menu. Lines A/B keep their own translucent black strips in the
// content pass.
void base_camp_draw_background(void* /*screen_state*/)
{
    picker_backdrop_draw_background(nullptr);
    screen* const game = og::runtime::current_session->myscreen_;
    // Outer bevel (6,28)..(313,160); inner grey face (8,30)..(311,158).
    // Content keeps a real inset on every edge instead of touching bevels.
    game->draw_button(6, 28, 313, 160, 2, 1);
}

// The §2.5 content pass (after draw_buttons): header lines A/B, the page
// indicator, the column headers, the roster row text/team chips, and the
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

    // Line A (§9.10.3, G3): grey "COMPANY:" label + WHITE name (the 40-byte
    // save_name) on ONE shared backing strip — two strip_text calls would
    // leave a raw-backdrop seam between label and name — plus the gold
    // block. Budget: 8 label chars + space + 26-char name clip = ink ending
    // x<=217, 27px clear of the GOLD strip at x=244.
    std::string company = save.save_name;
    if (company.size() > 26)
        company.resize(26);
    {
        const int width = (9 + static_cast<int>(company.size())) * 6;
        game->draw_rect_filled(6, 2, width + 4, 8, PURE_BLACK, 150);
        mytext.write_xy(8, 3, "COMPANY:", GREY, 1);
        mytext.write_xy(62, 3, company.c_str(), WHITE, 1);
    }
    strip_text(246, 3, format_base_camp_gold_label(save), YELLOW);

    // Line B: solo scenario/deploy header, or the §9.12 (G5) networked
    // session status — role + room code + machine/player census ("HOSTING
    // GLAD-XXXX - 2 MACH / 3 PLYR" / "IN GLAD-XXXX - HOST: <company>") —
    // with the degraded-link alert (join connecting/failed/lost, host
    // relay drop) keeping slot AND color precedence until the link heals.
    // This is the base-camp home of the lobby clients' status lines — the
    // pre-reshape team-build screen drew them at the same spot. The READY
    // r/m + DEP d/t counts this line carried through round 1 are SUPERSEDED
    // here (§9.12 records the 42-char band tradeoff): the §2.6 GO/READY
    // button state machine + the per-row deploy toggles carry those states.
    const bool networked = picker_lobby_is_networked();
    std::string line_b;
    unsigned char line_b_color = WHITE;
    if (networked) {
        const BaseCampLineB header = compose_base_camp_line_b(
            picker_lobby_connection_alert(),
            picker_lobby_host_controls_visible(),
            picker_lobby_session_room_code(),
            picker_lobby_players());
        line_b = header.text;
        if (header.alert)
            line_b_color = static_cast<unsigned char>(ORANGE_START);
    } else {
        line_b = format_base_camp_scen_line(save, game->world().title);
    }
    // §9.10.2 (G2): line B sits at y=17 — 14px below line A's y=3 baseline
    // (round 1 had 10px) — with the pager cluster beside it at y=15.
    strip_text(8, 17, line_b, line_b_color);

    if (st != nullptr && st->page.multi_page())
        strip_text(283, 17, st->page.indicator(), WHITE);

    // Column headers at y=33: solo keeps CLASS/EXP; networked swaps in the
    // 16-char COMPANY column (§2.5 U7 — CLASS is carried by the family
    // chip). §9.5.3: NO HP column on any client — it was DERIVED max HP
    // (damage never persists to base camp), redundant with CLASS+LVL, and
    // it lives one click away in TRAIN. EXP stays (progress is actionable).
    // §9.11 (G4): NO TRAIN header — the TRAIN column is deleted; clicking
    // the row body opens the train screen on that character. Round 6 gives
    // DEPLOY and TEAM explicit columns and pads the complete grid.
    if (networked) {
        mytext.write_xy(kBaseCampDeployHeaderX, 33, "DEPLOY", BLACK, 1);
        mytext.write_xy(kBaseCampTeamHeaderX, 33, "TEAM", BLACK, 1);
        mytext.write_xy(kBaseCampNameColumnX, 33, "NAME", BLACK, 1);
        mytext.write_xy(kBaseCampNetCompanyColumnX, 33, "COMPANY", BLACK, 1);
        mytext.write_xy(kBaseCampNetLevelColumnX, 33, "LV", BLACK, 1);
    } else {
        mytext.write_xy(kBaseCampDeployHeaderX, 33, "DEPLOY", BLACK, 1);
        mytext.write_xy(kBaseCampTeamHeaderX, 33, "TEAM", BLACK, 1);
        mytext.write_xy(kBaseCampNameColumnX, 33, "NAME", BLACK, 1);
        mytext.write_xy(kBaseCampSoloClassColumnX, 33, "CLASS", BLACK, 1);
        mytext.write_xy(kBaseCampSoloLevelColumnX, 33, "LV", BLACK, 1);
        mytext.write_xy(kBaseCampSoloExpColumnX, 33, "EXP", BLACK, 1);
    }

    // Seat chips overlay the right edge of each compact card after buttons
    // draw. They use the same four gameplay-team ramps as character chips,
    // while the P# label identifies the player/view rather than a fighter.
    if (st != nullptr) {
        const int seat_first = st->seat_page.first_index();
        const int seat_end = st->seat_page.end_index();
        for (int card = 0; card < seat_end - seat_first; ++card) {
            const og::sim::LobbyPlayer& seat =
                st->seats[static_cast<std::size_t>(seat_first + card)];
            const int team = std::clamp(
                static_cast<int>(seat.team), 0,
                static_cast<int>(SCORE_TEAM_COUNT) - 1);
            const int chip_x =
                kBaseCampSeatCardX[static_cast<std::size_t>(card)] + 48;
            game->fastbox(chip_x, kBaseCampSeatRailY + 1, 8, 8, PURE_BLACK);
            game->fastbox(
                chip_x + 1, kBaseCampSeatRailY + 2, 6, 6,
                static_cast<unsigned char>(team * 16 + 40));
            const char number[] = {
                static_cast<char>('1' + team), '\0'};
            mytext.write_xy_flat(chip_x + (team == 0 ? 3 : 2),
                                 kBaseCampSeatRailY + 2,
                                 number, PURE_BLACK, 1);
        }
    }

    const int first = st != nullptr ? st->page.first_index() : 0;
    const int end = st != nullptr ? st->page.end_index() : 0;
    if (end - first <= 0) {
        // Center the empty-state line in the padded grey roster face.
        mytext.write_xy_center(160, 92,
                               static_cast<unsigned char>(ORANGE_START), "%s",
                               "NO SOLDIERS - HIRE YOUR FIRST");
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

        // The chip now communicates and changes the character's team. Match
        // the in-game team ramp (team*16+40), clamping defensive save input
        // to the four persisted score teams, and label the four saved values
        // as the player-facing teams 1..4. Solo players can click it to cycle;
        // network lobby assignment keeps the same indicator read-only.
        const int team = std::clamp(static_cast<int>(member->teamnum), 0,
                                    static_cast<int>(SCORE_TEAM_COUNT) - 1);
        const unsigned char team_color =
            static_cast<unsigned char>(team * 16 + 40);
        game->fastbox(61, y, 10, 10, BLACK);
        game->fastbox(62, y + 1, 8, 8, team_color);
        const char team_number[] = {
            static_cast<char>('1' + team), '\0'};
        // Digits 2-4 have four opaque columns and center exactly at x=64.
        // The old font's "1" has only three; an even-width face has no exact
        // integer center for it, so bias that lone glyph one pixel right
        // instead of leaving it visibly left-heavy.
        const int team_number_x = team == 0 ? 65 : 64;
        mytext.write_xy_flat(team_number_x, y + 3, team_number,
                             PURE_BLACK, 1);

        // NAME and CLASS are high-contrast flat ink: true black when
        // deployed, grey when benched. Family identity moves into a compact
        // swatch immediately after CLASS (or NAME in the network layout),
        // using the first eight shades of View Team's original family ramp.
        // TEAM remains the separate gameplay-team ramp above.
        const unsigned char status_color =
            deployed ? static_cast<unsigned char>(BLACK) : kBenchedTextShade;
        const unsigned char identity_color =
            deployed ? static_cast<unsigned char>(PURE_BLACK)
                     : kBenchedTextShade;

        // Row glyphs sit at y+2 — centers the 6px font in the 10px band.
        if (networked) {
            // Foreign rows have no deploy BUTTON (no_draw hit zone): their
            // deploy state draws as the §2.5 X/- glyph at x=27.
            if (!display.owned)
                mytext.write_xy(27, y + 2, deployed ? "X" : "-",
                                status_color, 1);
            const BaseCampNetRowText row = format_base_camp_net_row(
                member->name, display.company, member->level);
            mytext.write_xy_flat(kBaseCampNameColumnX, y + 2,
                                 row.name.c_str(), identity_color, 1);
            draw_base_camp_family_swatch(
                *game,
                kBaseCampNameColumnX + mytext.query_width(row.name) +
                    kBaseCampFamilySwatchGap,
                y, member->family);
            mytext.write_xy(kBaseCampNetCompanyColumnX, y + 2,
                            row.company.c_str(), status_color, 1);
            mytext.write_xy(kBaseCampNetLevelColumnX, y + 2,
                            row.level.c_str(), status_color, 1);
        } else {
            const BaseCampRowText row = format_base_camp_row(*member);
            mytext.write_xy_flat(kBaseCampNameColumnX, y + 2,
                                 row.name.c_str(), identity_color, 1);
            mytext.write_xy_flat(kBaseCampSoloClassColumnX, y + 2,
                                 row.cls.c_str(), identity_color, 1);
            draw_base_camp_family_swatch(
                *game,
                kBaseCampSoloClassColumnX + mytext.query_width(row.cls) +
                    kBaseCampFamilySwatchGap,
                y, member->family);
            mytext.write_xy(kBaseCampSoloLevelColumnX, y + 2,
                            row.level.c_str(), status_color, 1);
            mytext.write_xy(kBaseCampSoloExpColumnX, y + 2, row.exp.c_str(),
                            status_color, 1);
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
        reload_picker_level_and_sync_settings(*myscreen,
                                              state->last_level_id);
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

// G3 row dispatch: deploy toggles, team-color cyclers, roster move-up
// controls, the §9.11 row-body train affordance, and the pagers.
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

    if (row == kBaseCampSeatsLabelIndex) {
        const Sint32 ret = create_teams_menu(0);
        if ((ret & MENU_EXIT) && team_build_start_selected())
            return MENU_EXIT;
        return MENU_REDRAW;
    }

    if (row == kBaseCampSeatPagePrevIndex ||
        row == kBaseCampSeatPageNextIndex)
    {
        if (st->seat_page.step(
                row == kBaseCampSeatPagePrevIndex ? -1 : 1))
        {
            TRACE("basecamp", "seat_page %s",
                  st->seat_page.indicator().c_str());
        }
        return MENU_OK;
    }

#ifndef DISABLE_MULTIPLAYER
    if (row == kBaseCampAddSeatIndex) {
        const std::int64_t now_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
        constexpr std::int64_t kSeatAddDebounceMs = 250;
        if (st->last_seat_add_ms >= 0 &&
            now_ms - st->last_seat_add_ms < kSeatAddDebounceMs)
        {
            TRACE("basecamp", "seat_add_debounced");
            return MENU_OK;
        }
        const std::size_t local_count =
            picker_lobby_local_seat_count();
        if (local_count >= static_cast<std::size_t>(MAX_PLAYERS)) {
            popup_dialog("SEATS", "LOCAL LIMIT IS 4");
            return MENU_OK;
        }
        if (picker_lobby_players().size() >=
                og::sim::kMaxGlobalPlayers)
        {
            popup_dialog("SEATS", "LOBBY FULL");
            return MENU_OK;
        }
        if (!picker_lobby_add_local_seat()) {
            popup_dialog("SEATS", "ADD DENIED");
            return MENU_OK;
        }
        st->last_seat_add_ms = now_ms;
        base_camp_refresh_rows(*st);
        if (!st->local_seat_indices.empty()) {
            const std::uint8_t added_player =
                st->local_seat_indices.back();
            const auto added = std::find_if(
                st->seats.begin(), st->seats.end(),
                [added_player](const og::sim::LobbyPlayer& player) {
                    return player.player_index == added_player;
                });
            if (added != st->seats.end()) {
                const int position = static_cast<int>(
                    std::distance(st->seats.begin(), added));
                st->seat_page.page =
                    position / kBaseCampSeatCardsPerPage;
            }
        }
        TRACE("basecamp", "seat_add local_count=%zu",
              picker_lobby_local_seat_count());
        return MENU_OK;
    }
#endif

    if (row >= kBaseCampSeatCardBase &&
        row < kBaseCampSeatCardBase + kBaseCampSeatCardsPerPage)
    {
        const int card = row - kBaseCampSeatCardBase;
        const int seat_index = st->seat_page.first_index() + card;
        if (seat_index < 0 ||
            seat_index >= static_cast<int>(st->seats.size()))
        {
            return MENU_OK;
        }

        const og::sim::LobbyPlayer& seat =
            st->seats[static_cast<std::size_t>(seat_index)];
        if (!base_camp_seat_is_local(*st, seat.player_index)) {
            const std::string owner = std::format(
                "P{}  {}",
                static_cast<int>(seat.player_index) + 1,
                seat.company.empty() ? "ANOTHER COMPANY" : seat.company);
            popup_dialog("CONTROLLED BY", owner.c_str());
            return MENU_OK;
        }

        if (picker_lobby_local_seat_count() == 0) {
            popup_dialog("SPECTATOR", "PRESS + TO ADD A PLAYER");
            return MENU_OK;
        }

        const auto local = std::find(
            st->local_seat_indices.begin(),
            st->local_seat_indices.end(), seat.player_index);
        if (local == st->local_seat_indices.end())
            return MENU_OK;
        const int local_slot = static_cast<int>(
            std::distance(st->local_seat_indices.begin(), local));
        const Sint32 ret = run_seat_settings_menu(seat, local_slot);
        base_camp_refresh_rows(*st);
        if ((ret & MENU_EXIT) && team_build_start_selected())
            return MENU_EXIT;
        return MENU_REDRAW;
    }

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const bool is_deploy = row >= 0 && row < kBaseCampRosterRowsPerPage;
    const bool is_row_body =
        row >= kBaseCampRowBodyBase &&
        row < kBaseCampRowBodyBase + kBaseCampRosterRowsPerPage;
    const bool is_team_chip =
        row >= kBaseCampTeamChipBase &&
        row < kBaseCampTeamChipBase + kBaseCampRosterRowsPerPage;
    const bool is_move_up =
        row >= kBaseCampMoveUpBase &&
        row < kBaseCampMoveUpBase + kBaseCampRosterRowsPerPage;
    if (!is_deploy && !is_row_body && !is_team_chip && !is_move_up)
        return 0;
    const int visual_row = is_row_body
        ? row - kBaseCampRowBodyBase
        : (is_team_chip
               ? row - kBaseCampTeamChipBase
               : (is_move_up ? row - kBaseCampMoveUpBase : row));
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

    if (is_move_up) {
        if (!picker_lobby_save_slot_editable(slot)) {
            popup_dialog("ORDER", "LOCKED");
            return MENU_OK;
        }
        const int moved_to = move_team_member_up(save, slot);
        if (moved_to >= 0) {
            TRACE("basecamp", "move_up slot=%d to=%d", slot, moved_to);
            picker_base_camp_after_roster_mutation();
            base_camp_refresh_rows(*st);
        }
        return MENU_OK;
    }

    if (is_team_chip) {
        // Multiplayer assigns teams through lobby seats, so this solo-only
        // control is hidden there and stale dispatches remain inert.
        if (picker_lobby_is_networked())
            return MENU_OK;
        if (!picker_lobby_save_slot_editable(slot)) {
            popup_dialog("TEAM", "LOCKED");
            return MENU_OK;
        }
        const short team = cycle_guy_team(save, slot, 1);
        if (team >= 0) {
            TRACE("basecamp", "team slot=%d team=%d", slot,
                  static_cast<int>(team));
            picker_base_camp_after_roster_mutation();
        }
        return MENU_OK;
    }

    if (is_deploy) {
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

    // §2.5 flow 4 via the §9.11 row-body affordance: a click (or Enter on
    // the row highlight) anywhere on the row's name/class/level area opens
    // the train screen directly on this character (the nested-engine-screen
    // pattern). A remote start that unparked the train screen propagates;
    // BACK re-inits our buttons. No debounce here (recorded §9.11 decision):
    // the nested screen re-inits buttons and consumes a collapsed second
    // tap in its own loop — the hazard class is unchanged from the deleted
    // TRAIN button; the U6 250 ms stamp stays deploy-only.
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
        // §9.11 (G4): roster-first on row 0's BODY — entering the base camp
        // highlights the first row and Enter trains (the curses roster
        // grammar); the deploy toggle sits one Left away. Empty roster:
        // the rewire re-seeds the highlight on HIRE.
        .default_highlight = kBaseCampRowBodyBase,
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
    // Historical note, kept from PLAYER SETTINGS:
    // "Right-clicking the selected count still enters the long-standing
    // spectator mode. The old team/PVP row happened to carry this status."
    // Those rows retired; Base Camp exposes SPECTATE by name.
    spec.right_click_enabled = false;
    spec.polls_lobby = true;
    spec.draw_background = &main_menu_draw_background;
    spec.draw_content = &main_menu_draw_content;
    // #155: the CLOUD row dispatches through the generic MenuSpecRow (D15).
    spec.on_spec_row = &main_menu_on_spec_row;
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

// 320-wide canvas center. The name face keeps name_guy's 22px-high character
// naming style, but widens to the combined REROLL/ACCEPT outer edges so the
// prompt and value have comfortable side padding.
constexpr int kNameEntryCenterX = 160;

constexpr MenuButtonSpec kNameEntryRows[] = {
    // BACK cancels — nothing is written (§2.2). Escape hotkey.
    {.id = "back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 10, .y = 170, .w = 44, .h = 20,
     .action = ButtonAction::MenuSpecRow, .arg = kNameEntryBackIndex,
     .nav = {.up = kNameEntryRerollIndex}},
    // The editable name box uses the same stock-grey face and height as
    // name_guy's "NAME THIS CHARACTER" box. Its wider edges align with the
    // action pair below, leaving 10px of left padding for the content-pass
    // text. y+h = 92 <= 100, so a web soft keyboard never covers it (§2.0 U5).
    {.id = "company_name_value", .label = "",
     .x = 86, .y = 70, .w = 148, .h = 22,
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
    // Match name_guy's classic modal exactly: prompt at face+2,+4 and the
    // editable value eight pixels below it, both left-aligned DARK_BLUE.
    game->text_normal.write_xy(96, 74, "FOUND YOUR COMPANY:", DARK_BLUE, 1);
    std::string name = st != nullptr ? st->name : std::string();
    if (name.size() > kCompanyNameMaxLen)
        name.resize(kCompanyNameMaxLen);
    game->text_normal.write_xy(96, 82, name.c_str(), DARK_BLUE, 1);
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
                96, 82, static_cast<short>(kCompanyNameMaxLen + 1),
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

// §2.3 / §9.19 geometry: keep the old Load Game menu's 220px slot column,
// now centered on the 320px screen and split into the company, BK, and X
// faces requested by the company workflow. Eight rows leave enough room for
// a taller title well and a footer while PageModel carries larger companies
// onto subsequent pages. Table order groups by kind — rows 0-7, BK 8-15,
// X 16-23, then the chrome — so MenuSpecRow args remain trivial to decode.
constexpr int kCompanyListRowsPerPage = 8;
constexpr int kCompanyListBakBase = 8;
constexpr int kCompanyListDelBase = 16;
constexpr int kCompanyListBackIndex = 24;
constexpr int kCompanyListPrevIndex = 25;
constexpr int kCompanyListNextIndex = 26;

// One visual row's (row, BK, X) triple divides the retired 220px slot face:
// 164px for the company name, then two 24px actions separated by 4px gutters.
// Static nav is the full-page multi-page shape (rows chain vertically,
// row.right -> BK -> X, row7.down -> BACK, X column bottoms out on PREV); the
// per-frame rewire recomputes every link from the live state anyway.
#define OG_COMPANY_LIST_ROW(i)                                               \
    {.id = "company_row_" #i, .label = "",                                   \
     .x = 50, .y = 35 + 17 * (i), .w = 164, .h = 10,                          \
     .action = ButtonAction::MenuSpecRow, .arg = (i),                        \
     .nav = {.up = (i) > 0 ? (i) - 1 : kCompanyListBackIndex,                 \
             .down = (i) < 7 ? (i) + 1 : kCompanyListBackIndex,               \
             .right = kCompanyListBakBase + (i)}}
#define OG_COMPANY_LIST_BAK(i)                                               \
    {.id = "company_bak_" #i, .label = "BK",                                 \
     .x = 218, .y = 35 + 17 * (i), .w = 24, .h = 10,                          \
     .action = ButtonAction::MenuSpecRow, .arg = kCompanyListBakBase + (i),  \
     .nav = {.up = (i) > 0 ? kCompanyListBakBase + (i) - 1 : -1,              \
             .down = (i) < 7 ? kCompanyListBakBase + (i) + 1                  \
                             : kCompanyListBackIndex,                         \
             .left = (i), .right = kCompanyListDelBase + (i)}}
#define OG_COMPANY_LIST_DEL(i)                                               \
    {.id = "company_del_" #i, .label = "X",                                  \
     .x = 246, .y = 35 + 17 * (i), .w = 24, .h = 10,                          \
     .action = ButtonAction::MenuSpecRow, .arg = kCompanyListDelBase + (i),  \
     .nav = {.up = (i) > 0 ? kCompanyListDelBase + (i) - 1 : -1,              \
             .down = (i) < 7 ? kCompanyListDelBase + (i) + 1                  \
                             : kCompanyListPrevIndex,                         \
             .left = kCompanyListBakBase + (i)}}

constexpr MenuButtonSpec kCompanyListRows[] = {
    OG_COMPANY_LIST_ROW(0), OG_COMPANY_LIST_ROW(1), OG_COMPANY_LIST_ROW(2),
    OG_COMPANY_LIST_ROW(3), OG_COMPANY_LIST_ROW(4), OG_COMPANY_LIST_ROW(5),
    OG_COMPANY_LIST_ROW(6), OG_COMPANY_LIST_ROW(7),
    OG_COMPANY_LIST_BAK(0), OG_COMPANY_LIST_BAK(1), OG_COMPANY_LIST_BAK(2),
    OG_COMPANY_LIST_BAK(3), OG_COMPANY_LIST_BAK(4), OG_COMPANY_LIST_BAK(5),
    OG_COMPANY_LIST_BAK(6), OG_COMPANY_LIST_BAK(7),
    OG_COMPANY_LIST_DEL(0), OG_COMPANY_LIST_DEL(1), OG_COMPANY_LIST_DEL(2),
    OG_COMPANY_LIST_DEL(3), OG_COMPANY_LIST_DEL(4), OG_COMPANY_LIST_DEL(5),
    OG_COMPANY_LIST_DEL(6), OG_COMPANY_LIST_DEL(7),
    // BACK to the main menu; Escape hotkey (the shared cancel grammar).
    {.id = "back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 50, .y = 169, .w = 40, .h = 20,
     .action = ButtonAction::MenuSpecRow, .arg = kCompanyListBackIndex,
     .nav = {.up = 7, .down = 0, .right = kCompanyListPrevIndex}},
    // Real MenuSpecRow pager actions (keyboard-live, §2.3); statically
    // hidden — the rewire shows them only when the list spans pages. They
    // share the old footer instead of growing a second rail outside it.
    {.id = "company_page_prev", .label = "PREV",
     .x = 185, .y = 169, .w = 40, .h = 20,
     .action = ButtonAction::MenuSpecRow, .arg = kCompanyListPrevIndex,
     .nav = {.up = 23, .left = kCompanyListBackIndex,
             .right = kCompanyListNextIndex},
     .hidden = true},
    {.id = "company_page_next", .label = "NEXT",
     .x = 230, .y = 169, .w = 40, .h = 20,
     .action = ButtonAction::MenuSpecRow, .arg = kCompanyListNextIndex,
     .nav = {.up = 23, .left = kCompanyListPrevIndex},
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
                .up = r > 0 ? r - 1 : kCompanyListBackIndex,
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
        .down = visible > 0 ? 0 : -1,
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

// Keep the dedicated Company List backdrop hook even though the classic
// slot-menu chrome is drawn later, in company_list_draw_content. The old
// create_load_menu choreography was backdrop -> buttons -> panel -> repaint
// slots; drawing the panel here would put it before the engine's first button
// pass instead of preserving that recognizable late-overlay order.
void company_list_draw_background(void* screen_state)
{
    picker_backdrop_draw_background(screen_state);
}

// The §2.3 content pass: old Load Game title, company metadata inside each
// classic slot face, the modern pager indicator, and the empty state.
void company_list_draw_content(void* screen_state)
{
    const CompanyListScreenState* st =
        static_cast<const CompanyListScreenState*>(screen_state);
    screen* game = og::runtime::current_session->myscreen_;

    const int total =
        st != nullptr ? static_cast<int>(st->companies.size()) : 0;

    // Preserve the legacy draw order: the engine has already drawn every
    // face; cover the centered menu footprint with its inset panel, then
    // repaint the three faces that divide each original slot.
    game->draw_button(40, 8, 280, 192, 1, 1);
    game->draw_text_bar(48, 13, 272, 28);
    constexpr char kClassicLoadTitle[] = "Gladiator: Load Game";
    game->text_normal.write_xy(
        160 - static_cast<int>(std::size(kClassicLoadTitle) - 1) * 3, 18,
        kClassicLoadTitle, RED, 1);

    const auto repaint_face = [](int index) {
        vbutton* live = og::runtime::current_session
                            ->allbuttons_[static_cast<std::size_t>(index)];
        if (live != nullptr && !live->hidden)
            live->vdisplay();
    };

    // The old load menu never collapsed: unused save slots still painted an
    // EMPTY SLOT face. Company rows remain hidden/inert in the engine graph,
    // but these plain no-callback faces preserve the pageable slot silhouette.
    const auto draw_empty_row = [game](int row) {
        const int y = 35 + 17 * row;
        game->draw_text_bar(48, y - 2, 272, y + 11);
        vbutton company(50, y, 164, 10, static_cast<Sint32>(0), 0,
                        "EMPTY SLOT", KEYSTATE_UNKNOWN);
        vbutton backup(218, y, 24, 10, static_cast<Sint32>(0), 0, "",
                       KEYSTATE_UNKNOWN);
        vbutton remove(246, y, 24, 10, static_cast<Sint32>(0), 0, "",
                       KEYSTATE_UNKNOWN);
        company.vdisplay();
        backup.vdisplay();
        remove.vdisplay();
        game->draw_box(49, y - 1, 270, y + 10, 0, 0, 1);
    };
    const auto draw_classic_back = [game] {
        game->draw_text_bar(48, 167, 91, 190);
        vbutton face(50, 169, 40, 20, static_cast<Sint32>(0), 0, "",
                     KEYSTATE_UNKNOWN);
        face.vdisplay();
        // vbutton centering was corrected globally after this menu retired.
        // Keep its historical BACK ink offset inside the relocated face.
        game->text_normal.write_xy(61, 176, "BACK", DARK_BLUE, 1);
        game->draw_box(49, 168, 90, 189, 0, 0, 1);
    };

    if (st == nullptr || total == 0) {
        // Transient shape: deleting the last company exits the screen, but
        // the frame that consumed the delete still draws once. Match the old
        // all-empty load menu instead of leaving a blank panel behind.
        for (int r = 0; r < kCompanyListRowsPerPage; ++r)
            draw_empty_row(r);
        draw_classic_back();
        return;
    }

    const int first = st->page.first_index();
    const int end = st->page.end_index();
    const int visible = end - first;
    const std::string& active_slot = og::data::active_company_slot();
    for (int r = 0; r < visible; ++r) {
        const og::data::CompanyInfo& info =
            st->companies[static_cast<std::size_t>(first + r)];
        const CompanyRowText row = format_company_row(info);
        const int y = 35 + 17 * r;
        game->draw_text_bar(48, y - 2, 272, y + 11);
        vbutton* company_face = og::runtime::current_session
                                    ->allbuttons_[static_cast<std::size_t>(r)];
        const bool active = info.slot == active_slot;
        if (company_face != nullptr)
            company_face->label = active ? "" : row.name;
        repaint_face(r);
        // The active row keeps the established red marker; its label is
        // repainted in white because DARK_BLUE fails on that special face.
        if (active) {
            game->text_normal.write_xy_center(132, y + 2, WHITE, "%s",
                                              row.name.c_str());
        }
        repaint_face(kCompanyListBakBase + r);
        repaint_face(kCompanyListDelBase + r);
        // The old slot loop redrew one box around the complete slot face.
        game->draw_box(49, y - 1, 270, y + 10, 0, 0, 1);
    }
    for (int r = visible; r < kCompanyListRowsPerPage; ++r)
        draw_empty_row(r);

    draw_classic_back();
    if (st->page.multi_page()) {
        // Both pagers live inside the restored panel and therefore join BACK
        // in this single late footer repaint.
        repaint_face(kCompanyListPrevIndex);
        repaint_face(kCompanyListNextIndex);
        game->text_normal.write_xy_center(160, 175, DARK_BLUE, "%s",
                                          st->page.indicator().c_str());
    }
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
            // §2.9 flow 2: re-seed the local lobby cache from the newly
            // opened company (the BEGIN NEW GAME pattern). The polled
            // apply_state_to_save otherwise keeps rebuilding roster/settings
            // from the previously seeded company's cached state, and the
            // next §3.8 autosave would persist that stale state into THIS
            // company's file (cross-company corruption).
            picker_lobby_initialize_from_save();
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
        // The restore loaded the rewound company into the in-memory save and
        // it opens straight into base camp — re-seed the local lobby cache
        // from it (the BEGIN NEW GAME pattern) so the polled
        // apply_state_to_save serves the restored roster/settings instead of
        // the previously seeded company's cached state (§2.9 flow 2).
        picker_lobby_initialize_from_save();
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

// ---------------------------------------------------------------------------
// #155 CLOUD SAVE subscreen (the main-menu CLOUD door): passphrase entry +
// manual UPLOAD/DOWNLOAD against the relay save vault. All state is local
// (design D14 — HTTP fires only on the two action clicks); the flows live in
// the pure layer (cloud_save_client.cpp) and reach the network through the
// PlatformBridge text-HTTP callbacks.
// ---------------------------------------------------------------------------

// The company-list seam pattern: the row-state overrides read this
// file-static pointer; run_menu_screen's screen_state points at the SAME
// object. Null state renders the all-enabled shape bare engine sweeps see.
CloudSaveScreenState* g_cloud_save_state = nullptr;

constexpr int kCloudSavePassphraseIndex = 0;
constexpr int kCloudSaveUploadIndex = 1;
constexpr int kCloudSaveDownloadIndex = 2;
constexpr int kCloudSaveBackIndex = 3;

// UPLOAD needs a passphrase AND a company on disk; DOWNLOAD needs only the
// passphrase. Disabled rows use the engine's greyed inert-box grammar
// (visible, keyboard navigable, click no-ops with the disabled_row_click
// TRACE), so the static nav graph never needs rewiring.
RowState cloud_upload_row_state(const MenuLabelContext& /*context*/)
{
    const CloudSaveScreenState* st = g_cloud_save_state;
    if (st == nullptr)
        return RowState::Visible;
    return (st->key_set && st->company_present) ? RowState::Visible
                                                : RowState::Disabled;
}

RowState cloud_download_row_state(const MenuLabelContext& /*context*/)
{
    const CloudSaveScreenState* st = g_cloud_save_state;
    if (st == nullptr)
        return RowState::Visible;
    return st->key_set ? RowState::Visible : RowState::Disabled;
}

// Vertical cycle 0..3 (wrap both ways); BACK carries the Escape hotkey.
constexpr MenuButtonSpec kCloudSaveRows[] = {
    {.id = "cloud_passphrase", .label = "PASSPHRASE",
     .x = 80, .y = 60, .w = 140, .h = 15,
     .action = ButtonAction::MenuSpecRow, .arg = kCloudSavePassphraseIndex,
     .nav = {.up = kCloudSaveBackIndex, .down = kCloudSaveUploadIndex}},
    {.id = "cloud_upload", .label = "UPLOAD",
     .x = 80, .y = 84, .w = 140, .h = 15,
     .action = ButtonAction::MenuSpecRow, .arg = kCloudSaveUploadIndex,
     .nav = {.up = kCloudSavePassphraseIndex,
             .down = kCloudSaveDownloadIndex},
     .state_override = &cloud_upload_row_state},
    {.id = "cloud_download", .label = "DOWNLOAD",
     .x = 80, .y = 108, .w = 140, .h = 15,
     .action = ButtonAction::MenuSpecRow, .arg = kCloudSaveDownloadIndex,
     .nav = {.up = kCloudSaveUploadIndex, .down = kCloudSaveBackIndex},
     .state_override = &cloud_download_row_state},
    {.id = "back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 80, .y = 150, .w = 60, .h = 15,
     .action = ButtonAction::MenuSpecRow, .arg = kCloudSaveBackIndex,
     .nav = {.up = kCloudSaveDownloadIndex,
             .down = kCloudSavePassphraseIndex}},
};

// Refresh the purely-local state: cfg key presence + the active company's
// on-disk header (never the network).
void cloud_save_refresh_state(CloudSaveScreenState& state)
{
    state.key_set = !og::ui::cloud::stored_cloud_key().empty();
    const std::string slot = og::data::active_company_slot();
    state.company_present = user_file_exists("save/" + slot + ".gtl");
    state.company_name.clear();
    if (state.company_present)
    {
        if (const std::optional<og::data::CompanyInfo> header =
                og::data::read_company_header(slot))
        {
            state.company_name = header->display_name;
        }
    }
}

void cloud_save_draw_content(void* screen_state)
{
    const CloudSaveScreenState* st =
        static_cast<const CloudSaveScreenState*>(screen_state);
    screen* game = og::runtime::current_session->myscreen_;
    game->text_normal.write_xy(80, 13, DARK_BLUE, "%s", "CLOUD SAVE");
    // Three status lines under DOWNLOAD, clear of BACK's face at y=150.
    const std::string company_line = (st != nullptr && st->company_present)
        ? std::format("COMPANY: {}",
                      st->company_name.empty()
                          ? og::data::active_company_slot()
                          : st->company_name)
        : std::string("NO COMPANY");
    game->text_normal.write_xy(80, 126, DARK_BLUE, "%s",
                               company_line.c_str());
    game->text_normal.write_xy(
        80, 134, DARK_BLUE, "%s",
        format_cloud_passphrase_status(st != nullptr && st->key_set).c_str());
    if (st != nullptr && !st->status_line.empty())
    {
        game->text_normal.write_xy(80, 142, DARK_BLUE, "%s",
                                   st->status_line.c_str());
    }
}

// Hooks bundle: the PlatformBridge text-HTTP callbacks (empty on clients
// without HTTP -> the flows degrade, design D8) + the shared NO-first
// confirm and popup dialogs (trace-only under TESTING).
og::ui::cloud::CloudHooks make_sdl_cloud_hooks()
{
    og::ui::cloud::CloudHooks hooks;
    const PlatformBridge& bridge = platform_bridge();
    if (bridge.cloud_http_get)
        hooks.http_get = bridge.cloud_http_get;
    if (bridge.cloud_http_post)
        hooks.http_post = bridge.cloud_http_post;
    hooks.confirm = [](const std::string& title, const std::string& message) {
        return no_or_yes_prompt(title.c_str(), message.c_str(), false);
    };
    hooks.notify = [](const std::string& title, const std::string& message) {
        popup_dialog(title.c_str(), message.c_str());
    };
    return hooks;
}

// The §2.3 OPEN sequence for a freshly downloaded company: validate + load +
// mount via open_company_slot, then re-seed the lobby cache and the main
// menu's company view. False on any failure (the flow's D16 popup then
// names the missing campaign; the company stays installed on disk).
bool cloud_save_open_company(const std::string& slot)
{
    SaveDataIoError io = SaveDataIoError::None;
    const ContinueResult result = open_company_slot(
        og::runtime::current_session->myscreen_->save_data, slot, &io);
    if (result != ContinueResult::Opened)
        return false;
    picker_lobby_initialize_from_save();
    refresh_main_menu_company_view();
    TRACE("cloud_save", "opened %s", slot.c_str());
    return true;
}

Sint32 cloud_save_on_spec_row(int row, void* screen_state)
{
    CloudSaveScreenState* st =
        static_cast<CloudSaveScreenState*>(screen_state);
    if (st == nullptr)
        return row == kCloudSaveBackIndex ? MENU_EXIT : 0;

    switch (row)
    {
    case kCloudSavePassphraseIndex:
    {
        // Clear-text entry is deliberate (typo visibility beats
        // shoulder-surfing for a game save). Cancel changes nothing.
        std::string value;
        bool accepted = false;
#ifdef TESTING
        // Deterministic seam: tests queue passphrases; an empty queue is a
        // cancel (picker_testing_cloud_passphrase_queue_push below).
        accepted = picker_testing_cloud_passphrase_queue_pop(value);
#else
        accepted = prompt_for_string("CLOUD PASSPHRASE", value);
#endif
        if (!accepted)
            return MENU_REDRAW;
        const std::string key = og::ui::cloud::derive_cloud_save_key(value);
        if (key.empty())
        {
            popup_dialog("CLOUD SAVE", "Passphrase must be\n8-64 characters.");
            return MENU_REDRAW;
        }
        og::ui::cloud::store_cloud_key(key);
        cloud_save_refresh_state(*st);
        st->status_line = "Passphrase set.";
        TRACE("cloud_save", "passphrase_set");
        return MENU_REDRAW;
    }
    case kCloudSaveUploadIndex:
        st->status_line = og::ui::cloud::run_cloud_upload(
            default_relay_base_url(), make_sdl_cloud_hooks());
        cloud_save_refresh_state(*st);
        TRACE("cloud_save", "upload: %s", st->status_line.c_str());
        return MENU_REDRAW;
    case kCloudSaveDownloadIndex:
        st->status_line = og::ui::cloud::run_cloud_download(
            default_relay_base_url(), make_sdl_cloud_hooks(),
            &cloud_save_open_company);
        cloud_save_refresh_state(*st);
        TRACE("cloud_save", "download: %s", st->status_line.c_str());
        return MENU_REDRAW;
    case kCloudSaveBackIndex:
        TRACE("cloud_save", "back");
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

const MenuScreenSpec& seat_settings_menu_screen_spec_mp()
{
    static constexpr MenuScreenSpec spec = make_seat_settings_spec(
        kSeatSettingsRowsMP,
        static_cast<int>(std::size(kSeatSettingsRowsMP)));
    return spec;
}

const MenuScreenSpec& seat_settings_menu_screen_spec_nomp()
{
    static constexpr MenuScreenSpec spec = make_seat_settings_spec(
        kSeatSettingsRowsNoMP,
        static_cast<int>(std::size(kSeatSettingsRowsNoMP)));
    return spec;
}

const MenuScreenSpec& seat_settings_menu_screen_spec()
{
#ifndef DISABLE_MULTIPLAYER
    return seat_settings_menu_screen_spec_mp();
#else
    return seat_settings_menu_screen_spec_nomp();
#endif
}

void install_seat_settings_state_for_screen(
    SeatSettingsScreenState* state)
{
    g_seat_settings_state = state;
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
        .on_spec_row = &picker_train_menu_engine_on_spec_row,
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
        // A joiner can still receive the host's launch while this modal is
        // open; keep the shared lobby/start pump alive like every other
        // engine-hosted picker screen.
        .polls_lobby = true,
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
        // The retired load loop opened on BACK; keep that keyboard starting
        // point and its BACK -> row 0 -> ... -> BACK vertical cycle.
        .default_highlight = kCompanyListBackIndex,
        .polls_lobby = true,
        .draw_background = &company_list_draw_background,
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

    state.seats = picker_lobby_players();
    if (state.seats.empty() && !networked && save.numplayers > 0) {
        const std::vector<short> seeded =
            derive_local_gameplay_seat_teams(save);
        for (std::size_t index = 0; index < seeded.size(); ++index) {
            state.seats.push_back(og::sim::LobbyPlayer{
                .player_index = static_cast<std::uint8_t>(index),
                .name = std::format("Player {}", index + 1),
                .company = save.save_name,
                .team = seeded[index],
                .character_slots = {},
                .ready = false,
                .is_host = index == 0,
            });
        }
    }
    std::sort(state.seats.begin(), state.seats.end(),
              [](const og::sim::LobbyPlayer& lhs,
                 const og::sim::LobbyPlayer& rhs) {
                  return lhs.player_index < rhs.player_index;
    });
    state.local_seat_indices.clear();
    if (networked) {
        state.local_seat_indices =
            picker_lobby_local_player_indices();
    } else {
        for (const og::sim::LobbyPlayer& seat : state.seats)
            state.local_seat_indices.push_back(seat.player_index);
    }
    const int seat_page_before = state.seat_page.page;
    state.seat_page = PageModel::make(
        static_cast<int>(state.seats.size()),
        kBaseCampSeatCardsPerPage);
    state.seat_page.page = std::clamp(
        seat_page_before, 0, state.seat_page.page_count() - 1);
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

const MenuScreenSpec& cloud_save_menu_screen_spec()
{
    static const MenuScreenSpec spec{
        .name = "cloud_save",
        .rows = kCloudSaveRows,
        .row_count = static_cast<int>(std::size(kCloudSaveRows)),
        .buttons_accessor = &picker_cloud_save_buttons,
        .count_accessor = &picker_cloud_save_button_count,
        // Static nav: the Disabled rows stay visible and navigable, so the
        // vertical cycle never needs rewiring (design D15 note).
        .nav = {.kind = NavProgramKind::Static},
        .default_highlight = kCloudSavePassphraseIndex,
        // Only reachable from the main menu, where no lobby exists (D14).
        .polls_lobby = false,
        .draw_background = &options_panel_draw_background,
        .draw_content = &cloud_save_draw_content,
        .on_spec_row = &cloud_save_on_spec_row,
        .exit_value = MENU_REDRAW,
    };
    return spec;
}

void install_cloud_save_state_for_screen(CloudSaveScreenState* state)
{
    g_cloud_save_state = state;
}

// #155: run the CLOUD SAVE screen (blocking) over fresh local state.
Sint32 run_cloud_save_screen()
{
    if (og::runtime::current_session->myscreen_ == nullptr)
        return MENU_REDRAW;
    CloudSaveScreenState state;
    cloud_save_refresh_state(state);
    install_cloud_save_state_for_screen(&state);
    (void)run_menu_screen(cloud_save_menu_screen_spec(), &state);
    install_cloud_save_state_for_screen(nullptr);
    return MENU_REDRAW;
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
            set(MenuScreenId::SeatSettings,
                {.kind = Kind::Engine,
                 .spec = &seat_settings_menu_screen_spec()});
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

button* picker_seat_settings_buttons()
{
    og::ui::materialize_menu_buttons(
        og::ui::seat_settings_menu_screen_spec(),
        pks().seat_settings_buttons);
    return pks().seat_settings_buttons.data();
}

int picker_seat_settings_button_count()
{
    return static_cast<int>(pks().seat_settings_buttons.size());
}

// Replaces both OPTIONS_BUTTON_INDEX #defines (10 with multiplayer, 5
// without): GAME SETTINGS' materialized index, derived from the spec instead
// of hand-tracked per variant. The legacy helper name remains internal.
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

button* picker_cloud_save_buttons()
{
    og::ui::materialize_menu_buttons(og::ui::cloud_save_menu_screen_spec(),
                                     pks().cloud_save_buttons);
    return pks().cloud_save_buttons.data();
}

int picker_cloud_save_button_count()
{
    return static_cast<int>(pks().cloud_save_buttons.size());
}

#ifdef TESTING
namespace {
std::vector<std::string>& cloud_passphrase_queue_ref()
{
    static std::vector<std::string> queue;
    return queue;
}
} // namespace

void picker_testing_cloud_passphrase_queue_clear()
{
    cloud_passphrase_queue_ref().clear();
}

void picker_testing_cloud_passphrase_queue_push(const char* value)
{
    cloud_passphrase_queue_ref().push_back(
        value != nullptr ? std::string(value) : std::string());
}

bool picker_testing_cloud_passphrase_queue_pop(std::string& out)
{
    auto& queue = cloud_passphrase_queue_ref();
    if (queue.empty())
        return false;
    out = queue.front();
    queue.erase(queue.begin());
    return true;
}
#endif

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

// The MATCHUP subscreen, engine-hosted (the legacy loop is gone). Pager pages
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
    const Sint32 result =
        og::ui::run_menu_screen(og::ui::control_options_menu_screen_spec());
    // CONTROLS can be entered from GAME SETTINGS without necessarily
    // returning through main_options()'s family-wide persistence epilogue
    // (tests and compatibility callers invoke this wrapper directly too).
    save_player_control_settings_to_cfg(cfg);
    cfg.save_settings();
    return result;
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
    const Sint32 result =
        og::ui::run_menu_screen(og::ui::main_options_menu_screen_spec());

    og::runtime::current_session->myscreen_->soundp->set_sound(
        !cfg.is_on("sound", "sound"));
    // Sync overscan to config before saving (data/ can't depend on input/)
    cfg.apply_setting("graphics", "overscan_percentage",
        std::format("{:.0f}",
                    100 * og::runtime::current_session->overscan_percentage_));
    save_player_control_settings_to_cfg(cfg);
    cfg.save_settings();

    return (result & MENU_EXIT) ? MENU_EXIT : MENU_REDRAW;
}
