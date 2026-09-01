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

class screen;

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

// Which transition family a screen belongs to (#237, docs/menu-engine.md
// "Drawing and transitions"). Screen is a full-window menu surface; Overlay
// is a true modal drawn over a live scene (the pause family) and never
// fades. Whether a Screen actually fades is DERIVED per entry by
// run_menu_screen — never declared per screen: it fades exactly when the
// entry crosses the main-menu boundary (menu-stack depth 1, nothing else
// open) and never when nested under an already-open menu screen. The two
// doors the depth rule cannot see either way — the main menu's own nested
// screens (CLOUD SAVES, LEVEL EDITOR) and the Base Camp strip door whose
// legacy screen runs at depth 0 (NETWORKING) — override the derivation with
// note_menu_entry_fade() below. Note: fades are instant under TESTING
// (FadeBetween skips the animation), so the injector SDL_Delay(750) waits
// around menu entries are generic settles, not fade timing — several guard
// transitions that never faded at all.
enum class MenuScreenKind : std::uint8_t {
    Screen,
    Overlay,
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
    MenuScreenKind kind = MenuScreenKind::Screen;
    int default_highlight = 0;
    bool right_click_enabled = false;
    // Subscreens whose BACK carries MENU_REDRAW (VIEW TEAM, MATCHUP and the
    // slot menus, all retired now): the loop must END on a redraw-bearing retvalue — checked
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

#ifdef TESTING
// Monotonic completion edge for injector tests. Incremented only after one
// engine-menu iteration has consumed input, dispatched/reset its action, and
// run the screen's frame tick; waiting on it avoids timing guesses about
// synchronous autosaves.
std::uint64_t menu_screen_testing_completed_frames();
#endif

// #237 fade ownership (docs/menu-engine.md, "Drawing and transitions").
//
// THE RULE: whoever fades a screen IN fades it OUT, at its own exit, while
// its last presented frame is still the render buffer. A fade-out is never
// deferred to the incoming screen — that deferral left arbitrary door-site
// code (setup, clears, mounts) between the outgoing screen's last present and
// its fade-out, and every such site was a fresh chance to blacken the buffer
// and turn the fade into a hard cut (the HELP, new-company and campaign-intro
// regressions). run_menu_screen brackets a fading entry with an RAII scope
// whose destructor fades out on EVERY return path; legacy presenters use
// LegacyMenuFade (below) the same way.
//
// THE STATE: the video layer is the single source of truth for "the window
// is black" (screen::window_is_black(); set by a completed fadeblack(0),
// cleared by every present). fadeblack(0) on a black window is a no-op, so a
// teardown or door that already faded out never plays a second, black-to-
// black fade, and no screen has to be told about it — the "faded to black"
// note this replaced is gone.
//
// THE INVARIANTS (TESTING; each one traces ("video", "FADE VIOLATION: ..."),
// counts in og::video_testing::g_fade_violations, and fails the running test
// through integration_main's listener). Exactly three things fire:
//   * fadeblack(1) on a window that is not black — "fade-in without a
//     fade-out" (checked by the video layer at the fade);
//   * fadeblack(0) when the render buffer differs from what the window last
//     showed — "fade-out from a frame that was never presented" (checked by
//     the video layer at the fade; "showed" is the rect each present
//     declared, so a clear, a stale redraw, or a draw outside every present's
//     rect all count);
//   * a fading ENTRY (run_menu_screen at depth 1, or an active
//     LegacyMenuFade) that finds the window not black — "entry found an
//     unfaded window: the previous surface exited without its fade-out"
//     (checked by the runner before its entry fade-out, which still runs so
//     production never hard-cuts). The one entry exempt by definition is a
//     nested main-menu door under a Fade note: its parent has not exited,
//     and fading the still-open parent out is that door's own job.
// Nothing else is checked: in particular a missing fade-in, or a fade at a
// door the depth rule says should be instant, is caught only by the per-leg
// count pins.
//
// The POINTER HANDOFF contract is the fade rule's sibling (docs/
// menu-engine.md, "Pointer handoff"): a surface owns its pointer state. It
// acknowledges the presses it saw — query_mouse() per frame, or
// acknowledge_mouse_presses() after a self-run pump — and hands back a
// clean pointer when it returns; no screen may consume a click minted on
// another surface. The collapsed-tap pending queue carries no coordinates,
// so a leaked click lands wherever the pointer sits when the next screen
// drains it (the editor-exit phantom click). run_menu_screen re-baselines
// (reset_mouse_click_tracking) at entry and before its loop-exit return;
// run_nested_menu_door re-baselines after its body returns. A click during
// the PARENT's own fade-in is a genuine click at the pointer's position and
// stays honored — that is what the collapsed-tap queue is for.

// One-shot override for the NEXT menu entry, the escape hatch on both sides
// of the depth rule. Instant suppresses the fade a depth-0/depth-1 entry
// would otherwise play (NETWORKING: a Base Camp strip door whose legacy
// screen runs at depth 0, so the rule would fade it like a main-menu door);
// Fade forces one on an entry the rule leaves instant (the main menu's own
// nested doors — see run_nested_menu_door). Consumed by every entry —
// including nested and Overlay entries and LegacyMenuFade — with the
// swallow-even-if-unused property, so a note nobody used cannot leak one
// screen forward. An Instant note that is still pending when a fading screen
// EXITS classifies the door being opened as instant: the exit skips its
// fade-out and leaves the note for that door's entry (NETWORKING again —
// Base Camp exits before the legacy screen runs, and the door is instant
// both ways). Instant suppresses ONLY that entry's fade-in: a screen entered
// under it still OWNS its exit fade (Base Camp back from NETWORKING fades
// out on its way to the main menu like any boundary surface) unless a fresh
// Instant note is pending at that exit.
enum class MenuEntryFade : std::uint8_t {
    Fade,
    Instant,
};
void note_menu_entry_fade(MenuEntryFade fade);

// The bracket both nested main-menu doors share (CLOUD SAVES from the main
// menu's own spec-row dispatch, LEVEL EDITOR from ButtonAction::DoLevelEdit):
// their screens run INSIDE the still-open main menu, so no depth the rule can
// read distinguishes them from a Base Camp subscreen. It notes Fade so the
// nested entry fades — out of the still-open menu's frame, and back in on its
// own first composed frame — and the body's own exit (the runner's scope, or
// the editor's LegacyMenuFade) fades the door out; the parent loop's next
// present finds a black window and fades back in. Precondition on a legacy
// BODY: its exit fade must run while the canvas holding its last presented
// frame is still ACTIVE (the editor calls LegacyMenuFade::end() before
// releasing its classic world-canvas pin; reading last_presented_canvas()
// after that release would name a freshly allocated, never-drawn surface).
Sint32 run_nested_menu_door(Sint32 (*body)());

// Current menu-stack depth (0 = no engine screen open). run_menu_screen
// increments it around every entry; the depth-1 entry of a
// MenuScreenKind::Screen is the main-menu-boundary crossing that fades.
int menu_screen_depth();

// The ownership rule for legacy full-screen presenters that never call
// run_menu_screen (campaign select, the results panel, the level editor, the
// campaign intro scroller, NETWORKING). The constructor decides whether this
// entry fades — a context switch (no engine screen open) or a Fade note,
// never an Instant note — and fades the presented surface out at once (a
// no-op if the window is already black; an unfaded window is the entry
// invariant's violation under TESTING). present_first() presents the first
// composed frame: fadeblack(1) when this entry fades, a plain full-frame
// present otherwise (safe as the loop's every-frame present). end() fades
// the screen's last presented frame out; the destructor is its backstop on
// every return path. Call end() explicitly where the last frame must be
// faded BEFORE later teardown touches the buffer or the active canvas.
//
// fade_out_at_exit(): a screen that entered INSTANTLY but leaves its context
// still owns its exit. NETWORKING is the case: an instant Base Camp strip
// door on the way in and on BACK, but a successful hookup leaves the
// lobby-config context for Base Camp's fading re-entry, and that re-entry
// must find a black window. The screen calls this on that exit path, and
// end() then fades its last frame out exactly as an active scope would.
// The screen name is for the violation message only.
class LegacyMenuFade {
public:
    explicit LegacyMenuFade(const char* screen_name = "legacy screen");
    ~LegacyMenuFade();
    LegacyMenuFade(const LegacyMenuFade&) = delete;
    LegacyMenuFade& operator=(const LegacyMenuFade&) = delete;

    bool active() const { return active_; }
    bool fade_in_pending() const { return active_ && fade_in_pending_; }
    void present_first(screen& scr);
    void fade_out_at_exit();
    void end();

private:
    bool active_ = false;
    bool fade_in_pending_ = false;
    bool exit_fade_owed_ = false;
    bool ended_ = false;
};

#ifdef TESTING
// Reset the transition state (depth 0, no pending override) between tests.
void menu_transition_testing_reset();
#endif

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
    // Teams (the MATCHUP subscreen) RETIRED (#218): its seat/team overview
    // is the VIEW LEVEL seat block and its knobs re-homed onto SCENARIO
    // (TEAMS / LIMIT) and DIFFICULTY (cross-control).
    Networking,
    Help,
    // The Base Camp zone submenu (the scripted page chassis) — registered
    // so the G5 remote-start sweep proves its preemption.
    CampaignZoneSubmenu,
    // LINEUP (docs/lineup-design.md §2, amendment B1): the four-band team
    // overview opened from SCENARIO. Its FIGHTERS list retired with B6 —
    // the Base Camp roster chip is the networked home of the team cycler.
    Lineup,
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

// #168 full-screen HELP: three content tabs over a paged text frame. The
// tab/pager state lives in help.cpp behind a file-static pointer (the VIEW
// LEVEL idiom); show_general_help() is the blocking wrapper.
const MenuScreenSpec& help_menu_screen_spec();

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
class CampaignZoneSession;  // campaign_picker_session.h

struct BaseCampScreenState {
    // Cursor used by the team-build family's level-reload frame hook.
    short last_level_id = -1;
    bool was_reset = false;
    // The gameplay-zone composition (docs/basecamp-zones-design.md): owned
    // by create_team_menu beside this state; null renders the default
    // composition through the same widget path (tests that install a bare
    // state get the classic 8-row roster shape). Refetched on the four
    // cadence triggers, never per frame.
    CampaignZoneSession* zone = nullptr;
    // The message-line toast (zone refusals/toasts prefer it over modal
    // popups — a modal strands a networked joiner mid-GO). Drawn over the
    // line-B slot until the stamp expires.
    std::string toast;
    std::int64_t toast_until_ms = 0;
    // Display row i (page-relative windowing via `page`) shows
    // slots[page.first_index() + i]: the private save row it names when
    // owned, the replicated wire copy when foreign.
    std::vector<BaseCampDisplaySlot> slots;
    PageModel page{};
    // The live lobby's globally indexed player seats, sorted by player_index.
    // Unlike character TEAM colors above, these assignments are transient
    // per-level player/view ownership and never rewrite a company roster.
    // `local_seat_indices` is THIS machine's seats in local-slot order — the
    // seat rail's four slots read it directly and never page (remote seats
    // are counted on the header line and listed in VIEW LEVEL's SEATS
    // report), so there is no seat page window here.
    std::vector<og::sim::LobbyPlayer> seats;
    std::vector<std::uint8_t> local_seat_indices;
    // Accepted ADD PLAYER activations are debounced like deploy taps: touch click
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

// Campaign zone submenu screen state (docs/basecamp-zones-design.md "Zone
// submenus"): a borrowed CampaignPickerSession (the SDL-free
// page-fetch/select/act machine — it caches the current page; this screen
// only windows and displays it) plus the PageModel window over the page's
// rows. Public so tests can drive the per-frame rewire's visibility
// variants; production state is owned by run_campaign_zone_submenu. The
// null/session-less state renders the empty shape (rows and pagers hidden,
// BACK alone).
class CampaignPickerSession; // campaign_picker_session.h
class CampaignZoneSession;   // campaign_picker_session.h

struct ZoneSubmenuScreenState {
    CampaignPickerSession* session = nullptr;
    PageModel page{};
    // The message line, same contract as the Base Camp's (the screen this
    // one is a room inside): confirmations and refusals ride the header
    // strip for 2.5s instead of stopping the player with a modal. The
    // modal-vs-toast fork used to be decided by menu DEPTH alone — one
    // purchase confirmed with a toast at the root and with an OK button one
    // page in.
    std::string toast;
    std::int64_t toast_until_ms = 0;
};

// The zone submenu: a C++-owned top strip (title + Escape-hotkeyed BACK,
// checked before any Lua row) over 8 pageable Lua row faces (Company List
// dynamic-rows chassis) and PageModel PREV/NEXT pagers. BACK pops session
// pages; at the submenu's ROOT page it closes back to the Base Camp.
const MenuScreenSpec& zone_submenu_menu_screen_spec();

// Installs the state the per-frame rewire reads (the company-list seam
// pattern; the null state renders the empty shape).
void install_zone_submenu_state_for_screen(ZoneSubmenuScreenState* state);

// Blocking wrapper: open the scripted book at `page_id` ("" = the root) and
// run the submenu. Returns MENU_REDRAW (or propagates a remote-start
// MENU_EXIT). `opened` (optional) reports whether the page fetched — the
// zone dispatch toasts the refusal instead of popping a modal.
Sint32 run_campaign_zone_submenu(const std::string& page_id,
                                 bool* opened = nullptr);

// --- LINEUP (docs/lineup-design.md §2) -------------------------------------

// LINEUP screen state: the team-build family's level-reload cursor plus the
// message-line toast (SPLIT outcomes — never a modal: a modal strands a
// networked joiner mid-GO). Public so tests can drive the per-frame
// rewire's visibility variants; production state is owned by
// create_lineup_menu. The null installed state renders the read-only shape
// (knobs hidden, splits hidden, bands from the live save/lobby).
struct LineupScreenState {
    short last_level_id = -1;
    bool was_reset = false;
    std::string toast;
    std::int64_t toast_until_ms = 0;
};

// LINEUP: title band, four team bands of equal pitch (header chip/POWER/
// seats, the FILL face + MAP UNITS box knob line, census text), and the
// BACK | SPLIT EVEN | SPLIT FAIR | UNITE action strip. (The FIGHTERS list
// retired with amendment B6.)
const MenuScreenSpec& lineup_menu_screen_spec();

// Install the state the per-frame rewires/draw hooks read (the company-list
// seam pattern; null renders the empty/read-only shape).
void install_lineup_state_for_screen(LineupScreenState* state);

// Show a toast on the installed LINEUP state (no-op when none installed).
// TRACEd ("lineup") so tests assert deterministically.
void lineup_show_toast(std::string text);

} // namespace og::ui
