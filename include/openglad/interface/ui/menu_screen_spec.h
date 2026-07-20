/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// MenuScreen Runtime — declarative SDL menu-screen specs (menu engine,
// docs/menu-engine.md, design docs/company-basecamp-design.md §1.3-§1.5).
//
// A MenuScreenSpec describes one blocking picker screen: its rows (geometry,
// action, nav transcribed VERBATIM from the legacy k_* table), per-row label
// and gate bindings, the screen's frame obligations (lobby poll, remote-start
// preemption scope), and its draw hooks. run_menu_screen() is the single
// frame skeleton that hosts every migrated screen; menu_screen_host() is the
// registry that answers "which system owns this screen" while legacy loops
// remain.
//
// SDL-side only (og_interface): terminal clients consume the shared
// menu_binding/terminal_menu_model layer instead (design D4).

#include <openglad/interface/button.h>
#include <openglad/interface/ui/menu_binding.h>

#include <cstdint>
#include <vector>

namespace og::ui {

// Build-variant row filter (§1.6): rows are dropped at materialization when
// they do not apply to the compiled platform. NOTE: until id-keyed nav (G9)
// lands, a spec whose variants actually filter rows must keep nav indices
// valid for every variant it compiles into (the main-menu quit/help fork
// satisfies this by construction: exactly one of the pair survives, at the
// same materialized index, with identical nav links).
enum class MenuBuildGate : std::uint8_t {
    Always,
    NativeOnly,
    WebOnly,
};

// The platform a materialization filters for. Runtime-parameterized so a
// native test can pin the web shape of a build-gated spec (G9: the touch/web
// variants have no other oracle); production paths use the compiled default.
enum class MenuBuildVariant : std::uint8_t {
    Native,
    Web,
};

// Live-vbutton outline binding, applied every frame in the label-sync pass
// (replaces raw allbuttons_[N]->do_outline writes when a screen migrates).
enum class MenuOutlineBinding : std::uint8_t {
    None,
    PlayerCountEquals,  // do_outline = (save.numplayers == outline_arg)
};

// Live-vbutton face-color binding, applied every frame in the label-sync
// pass (Layer F: READY/GO state faces).
using MenuColorFormatter = unsigned char (*)(const MenuLabelContext&);

// Full custom per-row state (overrides the GateBinding when set). This is
// how a row expresses RowState::Disabled: visible for nav, drawn dimmed,
// activation no-ops with a TRACE.
using MenuRowStateFn = RowState (*)(const MenuLabelContext&);

struct MenuButtonSpec {
    const char* id = "";
    // Static default label, transcribed verbatim from the legacy table (for
    // bound rows this is the default-state formatter output).
    const char* label = "";
    int hotkey = KEYSTATE_UNKNOWN;
    Sint32 x = 0;
    Sint32 y = 0;
    Sint32 w = 0;
    Sint32 h = 0;
    ButtonAction action = ButtonAction::Invalid;
    Sint32 arg = 0;
    // Hand-wired nav links, transcribed VERBATIM (design D1: auto-derived
    // nav is proof-gated and never the source of truth for legacy screens).
    MenuNav nav{};
    // Per-frame dynamic label; written to BOTH surfaces (descriptor row and
    // live vbutton) by the runner's label-sync pass.
    LabelBinding label_binding{};
    // Per-frame visibility gate (evaluated against MenuLabelContext).
    GateBinding gate{};
    MenuRowStateFn state_override = nullptr;
    // Pixie art face family (>= 0), re-applied after init_buttons AND after
    // every reset_buttons (set_graphic overwrites w/h, so ordering matters).
    signed char art_family = -1;
    MenuOutlineBinding outline = MenuOutlineBinding::None;
    Sint32 outline_arg = 0;
    MenuColorFormatter color = nullptr;
    bool no_draw = false;
    // Static default hidden flag, transcribed verbatim from the legacy
    // table's `hidden` constructor argument (conditionally-shown rows start
    // hidden on the materialized surface; per-frame gates/rewires own the
    // live state). test_menu_layout pins these defaults per screen.
    bool hidden = false;
    MenuBuildGate build = MenuBuildGate::Always;
};

// Nav application program, in engine precedence order (§1.5): a legacy
// rewire hook wins; otherwise the verbatim static links stand; RouteAround
// (generic transitive-skip over the base graph) is reserved for screens that
// prove equivalence first (G1) — not implemented yet.
enum class NavProgramKind : std::uint8_t {
    Static,
    Rewire,
    RouteAround,
};

struct NavProgram {
    NavProgramKind kind = NavProgramKind::Static;
    // G1: the legacy hand-wired rewire function, plugged verbatim. Runs every
    // frame after gates apply; may also re-assert visibility and pull the
    // highlight (the legacy sync_* functions do all three).
    void (*rewire)(button* buttons, int count, int& highlighted_button) = nullptr;
};

// Which remote-start (host GO) preemption check the frame loop runs (§1.4).
enum class RemoteStartScope : std::uint8_t {
    None,
    MainScope,       // picker_main_scope_remote_start_requested
    TeamBuildScope,  // team_build_remote_start_requested
};

// Both remote-start exit shapes (§1.4): subscreens RETURN the remote
// MENU_EXIT; the main menu breaks and lets present_menu act on
// pks().selected_menu_item, returning spec.exit_value.
enum class RemoteStartExit : std::uint8_t {
    ReturnMenuExit,
    BreakWithSelection,
};

// Per-screen entry transition timing (injector SDL_Delay(750) cadence
// depends on this being preserved exactly).
enum class EnterTransition : std::uint8_t {
    None,
    FadeAroundEntry,
    FadeWithInitialDraw,
};

// Dynamic row-template screens (Layer F, §1.7). Not defined yet.
struct RowTemplateSpec;

struct MenuScreenSpec {
    const char* name = "";
    const MenuButtonSpec* rows = nullptr;
    int row_count = 0;
    // D3 materialization shims: the legacy accessor pair. buttons() fills the
    // per-session mutable vector the count() reads — always call buttons()
    // first (the accessors keep this contract for every legacy consumer).
    button* (*buttons_accessor)() = nullptr;
    int (*count_accessor)() = nullptr;
    NavProgram nav{};
    RemoteStartScope remote_start = RemoteStartScope::None;
    RemoteStartExit remote_start_exit = RemoteStartExit::ReturnMenuExit;
    // PickerInterceptScope value the screen runs under; 0 = inherit the
    // ambient scope (subscreens never change it).
    int intercept_scope = 0;
    EnterTransition enter = EnterTransition::None;
    int default_highlight = 0;
    bool right_click_enabled = false;
    // Subscreens whose BACK carries MENU_REDRAW (view team / TEAMS / the
    // slot menus): the loop must END on a redraw-bearing retvalue — checked
    // right where the legacy loops checked, after handle_menu_nav and
    // BEFORE reset_buttons could consume it — and run_menu_screen returns
    // MENU_REDRAW from that break. Screens whose nested subscreens return
    // MENU_REDRAW to be consumed by reset_buttons (team build, SCENARIO)
    // keep this false.
    bool exit_on_redraw = false;
    // Draw the picker backdrop (draw_backdrop()) before draw_background.
    bool backdrop = false;
    // Frame obligations (single-point, §1.4/G12).
    bool polls_lobby = false;
    bool level_reload_guard = false;          // Layer E: via frame_tick hooks
    bool autosave_on_mutation = false;        // Layer F
    bool ready_reset_on_mutation = false;     // Layer F
    bool sync_settings_after_mutation = false; // after handled MenuSpecRow clicks
    // Per-screen hooks (screen_state is run_menu_screen's opaque argument).
    void (*draw_background)(void* screen_state) = nullptr; // full pre-buttons pass, incl. clear
    void (*draw_content)(void* screen_state) = nullptr;    // after draw_buttons (G14 hooks here)
    bool (*frame_tick)(void* screen_state, int frame) = nullptr; // false => exit loop
    void (*on_reset)(void* screen_state) = nullptr;        // after reset_buttons re-init
    // G3 generic row dispatch: consumes the ButtonAction::MenuSpecRow stash.
    // Return value is the frame's new retvalue (MENU_OK/MENU_REDRAW/0, or
    // MENU_EXIT for a structural exit).
    Sint32 (*on_spec_row)(int row, void* screen_state) = nullptr;
    // Optional extra MenuLabelContext fill (after the runner's default fill).
    void (*build_context)(MenuLabelContext& context) = nullptr;
    const RowTemplateSpec* row_template = nullptr;
    Sint32 exit_value = 2;  // MENU_REDRAW (picker_sdl_defs.h)
};

// Fill `out` with button rows materialized from the spec (build-gate
// filtered). The D3 accessors call this into their PickerState vector; tests
// may call it into their own storage.
void materialize_menu_buttons(const MenuScreenSpec& spec,
                              std::vector<button>& out);
void materialize_menu_buttons_for(const MenuScreenSpec& spec,
                                  MenuBuildVariant variant,
                                  std::vector<button>& out);

// The spec rows surviving the build gates, in materialized index order. The
// runner's per-row passes (gates, labels, art) index THROUGH this mapping so
// buttons[i] always pairs with the spec row it was materialized from — raw
// spec ordinals diverge from materialized indices past a filtered row.
std::vector<const MenuButtonSpec*> materialized_spec_rows(
    const MenuScreenSpec& spec);
std::vector<const MenuButtonSpec*> materialized_spec_rows_for(
    const MenuScreenSpec& spec, MenuBuildVariant variant);

// The single frame skeleton (§1.4). Blocks until the screen exits; returns
// spec.exit_value (or propagates a remote-start MENU_EXIT).
Sint32 run_menu_screen(const MenuScreenSpec& spec, void* screen_state = nullptr);

// G4 registry: which system owns each picker screen during the migration
// window. Engine screens carry their spec; legacy screens carry their
// blocking entry point (nullptr when the screen is owned by the
// SdlPickerClient state machine, e.g. NETWORKING before valve V2).
enum class MenuScreenId : std::uint8_t {
    MainMenu,
    TeamBuild,
    ViewTeam,
    SaveSlots,
    LoadSlots,
    MainOptions,
    DisplaySettings,
    ControlSettings,
    GameplayFx,
    UiFx,
    GraphicsFx,
    Difficulty,
    Hire,
    Train,
    Progress,
    ViewScenario,
    Scenario,
    Teams,
    Networking,
    Count,
};

struct MenuScreenHost {
    enum class Kind : std::uint8_t { Engine, Legacy };
    Kind kind = Kind::Legacy;
    const MenuScreenSpec* spec = nullptr;      // Engine
    Sint32 (*legacy_entry)(Sint32 arg) = nullptr; // Legacy (nullptr: see above)
};

const MenuScreenHost& menu_screen_host(MenuScreenId id);

// The first migrated screen (§1.8 step 2).
const MenuScreenSpec& difficulty_menu_screen_spec();

// The main menu (§1.8 step 4). §1.6: ONE MP spec + ONE no-MP spec (the no-MP
// variant genuinely repositions rows), selected at registry init by
// DISABLE_MULTIPLAYER (with the USE_TOUCH_INPUT => DISABLE_MULTIPLAYER
// mapping). The web/native fork inside each is the build-gated quit/help row
// pair. Both specs exist on every build so the un-compiled shapes stay
// unit-pinnable (G9).
const MenuScreenSpec& main_menu_screen_spec();       // the compiled selection
const MenuScreenSpec& main_menu_screen_spec_mp();
const MenuScreenSpec& main_menu_screen_spec_nomp();

} // namespace og::ui
