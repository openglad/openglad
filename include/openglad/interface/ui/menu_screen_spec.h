/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Declarative SDL menu-screen specs. See docs/menu-engine.md.
//
// A MenuScreenSpec describes one blocking picker screen: its rows (geometry,
// action, and hand-authored navigation), per-row label
// and gate bindings, the screen's frame obligations (lobby poll, remote-start
// preemption scope), and its draw hooks. run_menu_screen() is the single
// frame skeleton for runtime-owned screens; menu_screen_host() records which
// system owns each screen.
//
// SDL-side only (og_interface): terminal clients consume the shared
// menu_binding/terminal_menu_model layer instead.

#include <openglad/interface/button.h>
#include <openglad/interface/ui/menu_binding.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/company.h>

#include <cstdint>
#include <string>
#include <vector>

namespace og::ui {

// Rows that do not apply to the target platform are dropped at
// materialization. Until navigation uses stable row IDs, a spec whose
// variants filter rows must keep nav indices
// valid for every variant it compiles into (the main-menu enabled/disabled
// QUIT fork satisfies this by construction: exactly one QUIT row survives,
// at the same materialized index, with identical nav links).
enum class MenuBuildGate : std::uint8_t {
    Always,
    NativeOnly,
    WebOnly,
};

// The platform a materialization filters for. Runtime-parameterized so a
// native test can pin the web shape of a build-gated spec; production paths
// use the compiled default.
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
// pass (for example, READY/GO state faces).
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
    // Hand-wired nav links. Auto-derived navigation is not the source of
    // truth for classic screens.
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

// Navigation precedence: a rewire hook wins; otherwise the static links
// stand. RouteAround is reserved for screens that prove equivalence first.
enum class NavProgramKind : std::uint8_t {
    Static,
    Rewire,
    RouteAround,
};

struct NavProgram {
    NavProgramKind kind = NavProgramKind::Static;
    // A hand-wired rewire function. Runs every
    // frame after gates apply; may also re-assert visibility and pull the
    // highlight (the legacy sync_* functions do all three).
    void (*rewire)(button* buttons, int count, int& highlighted_button) = nullptr;
};

// Which remote-start (host GO) preemption check the frame loop runs.
enum class RemoteStartScope : std::uint8_t {
    None,
    MainScope,       // picker_main_scope_remote_start_requested
    TeamBuildScope,  // team_build_remote_start_requested
};

// Both remote-start exit shapes: subscreens return the remote
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

// Reserved for a future dynamic row-template consumer.
struct RowTemplateSpec;

struct MenuScreenSpec {
    const char* name = "";
    const MenuButtonSpec* rows = nullptr;
    int row_count = 0;
    // Materialization shims for the accessor pair. buttons() fills the
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
    // Subscreens whose BACK carries MENU_REDRAW (VIEW TEAM / MATCHUP / the
    // retired slot menus): the loop must END on a redraw-bearing retvalue — checked
    // right where the legacy loops checked, after handle_menu_nav and
    // BEFORE reset_buttons could consume it — and run_menu_screen returns
    // MENU_REDRAW from that break. Screens whose nested subscreens return
    // MENU_REDRAW to be consumed by reset_buttons (team build, SCENARIO)
    // keep this false.
    bool exit_on_redraw = false;
    // Draw the picker backdrop (draw_backdrop()) before draw_background.
    bool backdrop = false;
    // Frame obligations.
    bool polls_lobby = false;
    bool level_reload_guard = false;
    bool autosave_on_mutation = false;
    bool ready_reset_on_mutation = false;
    bool sync_settings_after_mutation = false; // after handled MenuSpecRow clicks
    // Per-screen hooks (screen_state is run_menu_screen's opaque argument).
    // Entry-time descriptor fix-ups, run ONCE after materialization and
    // BEFORE init_buttons (the legacy pre-init mutations: hire's computed
    // PREV/NEXT repositioning). Per-frame visibility belongs to gates and
    // rewires, never here — the gate pass rewrites hidden every frame.
    void (*prepare_buttons)(button* buttons, int num_buttons,
                            void* screen_state) = nullptr;
    void (*draw_background)(void* screen_state) = nullptr; // full pre-buttons pass, incl. clear
    void (*draw_content)(void* screen_state) = nullptr;    // after draw_buttons
    bool (*frame_tick)(void* screen_state, int frame) = nullptr; // false => exit loop
    void (*on_reset)(void* screen_state) = nullptr;        // after reset_buttons re-init
    // Post-nav click consumption for screens whose legacy loops interpreted
    // retvalue themselves BEFORE reset_buttons could consume it (the VIEW
    // LEVEL page-step stash: a pager click's MENU_OK must flip the page and
    // zero retvalue instead of re-initializing the buttons). Runs right
    // after the exit_on_redraw check, at the legacy consumption point;
    // returns the frame's new retvalue.
    Sint32 (*consume_click)(Sint32 retvalue, void* screen_state) = nullptr;
    // Generic row dispatch: consumes the ButtonAction::MenuSpecRow stash.
    // Return value is the frame's new retvalue (MENU_OK/MENU_REDRAW/0, or
    // MENU_EXIT for a structural exit).
    Sint32 (*on_spec_row)(int row, void* screen_state) = nullptr;
    // Optional extra MenuLabelContext fill (after the runner's default fill).
    void (*build_context)(MenuLabelContext& context) = nullptr;
    const RowTemplateSpec* row_template = nullptr;
    Sint32 exit_value = 2;  // MENU_REDRAW (picker_sdl_defs.h)
};

// Fill `out` with button rows materialized from the spec (build-gate
// filtered). Accessors call this into their PickerState vector; tests
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

// The single frame skeleton. Blocks until the screen exits; returns
// spec.exit_value (or propagates a remote-start MENU_EXIT).
Sint32 run_menu_screen(const MenuScreenSpec& spec, void* screen_state = nullptr);

// Registry of picker-screen ownership. Runtime screens carry their spec;
// legacy screens carry their blocking entry point. NETWORKING is owned by
// the SdlPickerClient state machine and therefore has a null entry point
// here; docs/menu-engine.md explains why.
enum class MenuScreenId : std::uint8_t {
    MainMenu,
    SeatSettings,
    TeamBuild,
    MainOptions,
    DisplaySettings,
    // ControlSettings retired with the global CONTROLS subscreen: the
    // per-player screens (Base Camp seat settings, pause player screen) own
    // mode / remap / reset / input per seat.
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

const MenuScreenSpec& difficulty_menu_screen_spec();

// These screens keep their hooks beside their file-local helpers in
// picker_team_build.cpp.
const MenuScreenSpec& hire_menu_screen_spec();
const MenuScreenSpec& train_menu_screen_spec();
const MenuScreenSpec& progress_menu_screen_spec();
const MenuScreenSpec& view_scenario_menu_screen_spec();

// The MP and no-MP main-menu specs share the same geometry.
// DISABLE_MULTIPLAYER selects the compiled variant (including the
// USE_TOUCH_INPUT mapping). The web/native fork inside each main spec is the
// build-gated enabled/disabled QUIT row; HELP is present in both. Both specs
// exist on every build so uncompiled shapes remain unit-testable.
const MenuScreenSpec& main_menu_screen_spec();       // the compiled selection
const MenuScreenSpec& main_menu_screen_spec_mp();
const MenuScreenSpec& main_menu_screen_spec_nomp();

// LOCAL SEAT SETTINGS: an owned Base Camp seat's team and persistent local
// controller profile. The no-MP shape omits removal while retaining controls.
const MenuScreenSpec& seat_settings_menu_screen_spec();
const MenuScreenSpec& seat_settings_menu_screen_spec_mp();
const MenuScreenSpec& seat_settings_menu_screen_spec_nomp();

// §2.1 Company & Base Camp: the CONTINUE/LOAD gate reads a cached view of the
// company set, refreshed once per mainmenu() entry (never per frame —
// list_companies() touches the filesystem). Tests pin the view.
void refresh_main_menu_company_view();
void set_main_menu_company_view_for_tests(bool present, std::string display_name);

// §2.5 base camp (the reimagined Team Build) screen state: the display-slot
// list (re-collected every frame so positional indices are never held
// across a roster change or a win fold — §3.3), the PageModel window over
// it, and the reload-guard cursor the frame tick shares with the SCENARIO
// family. Solo: one owned slot per occupied team_list entry. Networked: the
// merged lobby roster (own rows first, then foreign machines' replicated
// slots — collect_base_camp_display_slots); two well-stocked machines can
// replicate more than 24 display slots, so the page window derives from
// slots.size(), never from a 24-row assumption. Public so tests can drive
// the per-frame rewire's visibility variants ({empty, partial, full,
// multi-page} × {host} × {ownership mix} × {networked}); production state
// is owned by create_team_menu.
struct BaseCampScreenState {
    // Cursor used by the team-build family's level-reload frame hook.
    short last_level_id = -1;
    bool was_reset = false;
    // Display row i (page-relative windowing via `page`) shows
    // slots[page.first_index() + i]: the private save row it names when
    // owned, the replicated wire copy when foreign.
    std::vector<BaseCampDisplaySlot> slots;
    PageModel page{};
    // The live lobby's globally indexed player seats, sorted by player_index,
    // plus a four-card page window for the Base Camp assignment rail. Unlike
    // character TEAM colors above, these assignments are transient per-level
    // player/view ownership and never rewrite a company roster.
    std::vector<og::sim::LobbyPlayer> seats;
    std::vector<std::uint8_t> local_seat_indices;
    PageModel seat_page{};
    // Accepted + activations are debounced like deploy taps: touch click
    // collapsing can otherwise create two local seats from one visible tap.
    // Denials never stamp, so retry-after-capacity remains immediate.
    std::int64_t last_seat_add_ms = -1;
    // A second accepted deploy toggle of
    // the same display row (same tapped rect resolving to the same save
    // slot) within 250 ms is silently ignored — every touch mistap
    // double-toggle would otherwise be a spurious MP ready-clear. Denied /
    // foreign / stale taps never stamp. Public so tests can rewind the
    // stamp instead of sleeping.
    int last_deploy_toggle_idx = -1;
    int last_deploy_toggle_slot = -1;
    std::int64_t last_deploy_toggle_ms = -1;
};

// One owned seat opened from the Base Camp rail. seat_id is the stable
// authority token; player_index/P# and local_slot are refreshed while open.
// Public so headless tests can drive the editor spec through ownership,
// reindexing, and stale-seat shapes without a blocking UI thread.
struct SeatSettingsScreenState {
    og::sim::LobbySeatId seat_id = og::sim::kInvalidLobbySeatId;
    std::uint8_t player_index = 0xff;
    int local_slot = -1;
    bool removed = false;
    bool missing_notice_shown = false;
};

// The base camp spec is team_build_menu_screen_spec() (the screen keeps its
// registry identity); the per-frame rewire reads the installed state (the
// company-list seam pattern; null renders the empty-roster shape).
void install_base_camp_state_for_screen(BaseCampScreenState* state);
void install_seat_settings_state_for_screen(
    SeatSettingsScreenState* state);

// Re-collect the display-slot list from the save and clamp the page window
// (§3.3 positional-refresh rule; called every frame tick and by tests).
void base_camp_refresh_rows(BaseCampScreenState& state);

// §2.2 new-company name entry: a generated fantasy default shown in an
// editable strip, REROLL, and an ACCEPT/BACK pair. The internal filename is
// deliberately not shown; BACK creates nothing.
const MenuScreenSpec& name_entry_menu_screen_spec();

// Runs the name-entry screen (blocking). On ACCEPT, fills `out_name` with the
// chosen display name (<= kCompanyNameMaxLen) and returns true; on BACK/cancel
// returns false and leaves `out_name` untouched (nothing is created).
bool run_new_company_name_entry(std::string& out_name);

// §2.3 Company List (Load) screen state: the header-scanned company set
// (list_companies — most-recent-first, never a full SaveData::load per
// row), the PageModel window over it, and the verdicts the wrapper reads.
// Public so tests can drive the per-frame rewire's visibility variants
// ({0 rows, partial page, multi page, corrupt rows}); production state is
// owned by run_company_list_screen.
struct CompanyListScreenState {
    std::vector<og::data::CompanyInfo> companies;
    PageModel page{};
    // OPEN clicked and the company loaded: the caller proceeds to base camp
    // — set by a row OPEN, or by a successful restore inside the Backups
    // sub-view (§2.4: a rewound company opens straight into base camp).
    bool opened = false;
    // §2.4 door: the slot the BK dispatch stashed before opening the Backups
    // sub-view on it (kept observable for tests).
    std::string backups_slot;
};

// §2.3 Company List: 8 pageable rows with per-row
// BK (Backups door) and X (delete, NO-first confirm) buttons, BACK, and
// PageModel PREV/NEXT pagers (hidden when one page fits everything).
const MenuScreenSpec& company_list_menu_screen_spec();

// Installs the state the per-frame rewire reads (the wrapper's production
// path; tests install their own around direct rewire/run calls, then
// install nullptr — the null state renders the empty-list shape).
void install_company_list_state_for_screen(CompanyListScreenState* state);

// Runs the Company List screen (blocking). Returns true when a company was
// opened (active slot repointed + save loaded + campaign mounted); false on
// BACK — including the exit after deleting the last company, which returns
// to a main menu whose gate then hides CONTINUE/LOAD.
bool run_company_list_screen();

// §2.4 Backups sub-view state: one company's header-scanned snapshots
// list_company_backups — newest seq first, never mounted), the PageModel
// window over them, and the restore verdict the wrapper reads. Public so
// tests can drive the per-frame rewire's visibility variants; production
// state is owned by run_company_backups_screen.
struct CompanyBackupsScreenState {
    // The company whose snapshots are listed (the BK row's slot).
    std::string slot;
    // Display label for the title strip (the company row's name column).
    std::string company_name;
    std::vector<og::data::CompanyBackupInfo> backups;
    PageModel page{};
    // A row's NO-first confirm was accepted and the validated restore
    // rewound the company: the active slot now points at it and the caller
    // proceeds straight into base camp (§2.4).
    bool opened = false;
};

// §2.4 Backups sub-view: 10 pageable snapshot rows
// (click = restore behind the NO-first confirm; corrupt rows refuse), BACK
// to the Company List, and PageModel PREV/NEXT pagers (retention 20 => at
// most 2 pages).
const MenuScreenSpec& company_backups_menu_screen_spec();

// Installs the state the per-frame rewire reads (the company-list seam
// pattern; the null state renders the empty-list shape).
void install_company_backups_state_for_screen(CompanyBackupsScreenState* state);

// Runs the Backups sub-view (blocking) over a fresh header-only snapshot
// scan of `slot`. Returns true when a restore rewound the company (active
// slot repointed + save reloaded + last-played re-stamped): the caller exits
// to base camp. False on BACK, with the company untouched.
bool run_company_backups_screen(const std::string& slot,
                                const std::string& company_name);

// #155 CLOUD SAVE screen state: purely local (design D14 — no network on
// entry; HTTP fires only on the UPLOAD/DOWNLOAD clicks). Public so tests can
// drive the row-state shapes through the installed-state seam; production
// state is owned by run_cloud_save_screen.
struct CloudSaveScreenState {
    bool key_set = false;         // refreshed on entry + after passphrase set
    bool company_present = false; // user_file_exists(active slot .gtl)
    std::string company_name;     // header display name ("" when absent)
    std::string status_line;      // last action result, drawn in content pass
};

// #155 CLOUD SAVE subscreen: PASSPHRASE / UPLOAD / DOWNLOAD / BACK. UPLOAD
// and DOWNLOAD use the engine's Disabled grammar until their prerequisites
// hold (key set; key set + company present).
const MenuScreenSpec& cloud_save_menu_screen_spec();

// Installs the state the row-state overrides read (the company-list seam
// pattern; the null state renders the all-enabled shape bare engine sweeps
// expect).
void install_cloud_save_state_for_screen(CloudSaveScreenState* state);

// Runs the CLOUD SAVE screen (blocking); always returns MENU_REDRAW to the
// main menu's spec-row dispatcher.
Sint32 run_cloud_save_screen();

} // namespace og::ui
