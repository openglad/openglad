# Menu engine (MenuScreen Runtime)

Status: Layer E in progress (invisible re-host of the legacy picker screens;
design: `docs/company-basecamp-design.md` §1). This document is the G4
contract: the calling conventions every menu loop obeys, the migration
recipe, the loop-obligations checklist, and the permanent record of why
auto-derived nav is proof-gated.

## Screen ownership (the G4 registry)

`og::ui::menu_screen_host(MenuScreenId)` (menu_screen_specs.cpp) is the one
lookup that answers "which system owns this screen" during the migration
window. Keep this table in sync with the registry:

| Screen | Host | Entry |
|---|---|---|
| DIFFICULTY | **Engine** | `difficulty_menu_screen_spec()` via `run_difficulty_menu()` |
| GAMEPLAY FX / UI FX / GRAPHICS FX | **Engine** | `*_fx_menu_screen_spec()` via `gameplay_fx_options()` etc. |
| DISPLAY | **Engine** | `display_settings_menu_screen_spec()` via `display_settings_options()` (platform hide/rewire = the spec's Rewire program) |
| CONTROLS | **Engine** | `control_options_menu_screen_spec()` via `main_controls_options()` |
| MAIN OPTIONS | **Engine** | `main_options_menu_screen_spec()` via `main_options()` (which keeps the family-wide cfg-persist exit epilogue) |
| Main menu | Legacy | `mainmenu()` |
| Team build | Legacy | `create_team_menu()` |
| View team | Legacy | `create_view_menu()` |
| Save/Load slots | Legacy | `create_save_menu()` / `create_load_menu()` |
| Hire / Train / Progress / View level | Legacy | `create_*_menu()` |
| Scenario / TEAMS | Legacy | `create_scenario_menu()` / `create_teams_menu()` |
| NETWORKING | Legacy | `SdlPickerClient::configure_networking` (state-machine-owned; valve V2, migrates last) |

## Calling conventions (unchanged by the engine)

- **Return flags** (`picker_sdl_defs.h`): `MENU_EXIT = 1`, `MENU_REDRAW = 2`,
  `MENU_OK = 4`. Loops run `while (!(retvalue & MENU_EXIT))`; inner breaks
  fire only on an **exact** `retvalue == MENU_EXIT`; composite values draw
  one final frame and exit at the loop-top bit test. `reset_buttons`
  consumes `MENU_OK`/`MENU_REDRAW` by re-initializing the live vbuttons and
  zeroing retvalue.
- **Blocking subscreens return `MENU_REDRAW`** for a local BACK;
  `MENU_EXIT` is reserved for propagating exits (remote start, structural
  unwinds). `run_menu_screen` returns `spec.exit_value` on local exits and
  propagates `MENU_EXIT` for remote starts and spec-row structural exits.
- **`pks().selected_menu_item`**: the shared picker state machine acts on
  this after a screen exits (present_menu). Remote-start preemption selects
  Main CONTINUE (main scope) or TeamBuild START GAME (team-build scope)
  before exiting so the state machine re-enters team build and launches.
- **`vbutton::do_call` / `ButtonAction`** (D2): untouched at Layer E. The
  engine activates rows exactly as `leftclick`/`handle_menu_nav` do. The
  test intercept hook (`picker_try_intercept_button_action`) still runs
  first inside do_call.

## G3: `ButtonAction::MenuSpecRow = 101` (the ONE engine action)

All new-screen rows (Layer F) route through the single generic action.
`do_call` stashes `pks().menu_spec_clicked_row = arg` and returns 101
(mirrors `JoinRelayRoomListEntry = 100`). **Retvalue-collision discipline
(mandatory)**: `101 & MENU_EXIT != 0`, so `run_menu_screen` consumes the
stash, dispatches `spec.on_spec_row(row, state)`, and REWRITES retvalue with
the dispatch result before its loop-condition test. A TESTING check
(`OG_MENU_ENGINE_CHECK`) aborts if the stash survives a frame. A dispatch
returning `MENU_EXIT` is a structural exit and propagates `MENU_EXIT`
itself. `myfun != 0` keeps every engine row keyboard-live.

## The frame skeleton (`run_menu_screen`, menu_screen_runner.cpp)

Per §1.4, transcribed from the canonical legacy loops:

1. Materialize via the D3 accessor pair (buttons() fills what count()
   reads — always call buttons() first), `init_buttons`, `clear_keyboard`,
   art bindings (pixie `set_graphic` AFTER init_buttons — it overwrites
   w/h), initial gate/nav pass.
2. Loop while `!(retvalue & MENU_EXIT)`:
   - `picker_lobby_poll()` if `polls_lobby`;
   - gate pass → `RowState` per row → hidden on BOTH surfaces
     (`sync_button_hidden_state`); Disabled rows stay visible but inert
     (myfun/myfunc zeroed, face dimmed GREY, click leaves a
     `menu_engine`/`disabled_row_click` TRACE);
   - nav program: (1) legacy Rewire hook verbatim (G1), else (2) static
     verbatim links; then `ensure_highlighted_button_visible`; TESTING
     invariant: no visible button nav-links to a hidden one;
   - remote-start per scope — both exit shapes: subscreens RETURN the
     remote `MENU_EXIT`; `BreakWithSelection` (main menu, later) breaks and
     returns `spec.exit_value`;
   - `leftmouse` → `leftclick` (exact-`MENU_EXIT` break preserved);
     `rightclick` only when `right_click_enabled`;
   - `handle_menu_nav` (unchanged spin-wait), exact-`MENU_EXIT` break;
   - MenuSpecRow stash consume + dispatch (G3 discipline above), G12
     `sync_settings_after_mutation` hook;
   - `reset_buttons`; on reset re-apply art bindings + `on_reset`;
   - `frame_tick` hook (false ⇒ exit; level-reload guards live here);
   - label sync from a FRESH `MenuLabelContext` (a click this frame or a
     lobby save-rewrite must show this frame): every bound row → BOTH
     surfaces; OutlineBinding → `do_outline`; ColorBinding → `color`;
   - draw: backdrop? → `draw_background` (full pre-buttons pass incl.
     clear) → `draw_buttons` → `draw_content` → `draw_highlight` →
     `buffer_to_screen` → `sleep_ms(10)` (the ONE asyncify yield).

## Loop-obligations checklist

Any NEW cross-cutting obligation added to the engine must also be swept
manually across every screen still in the Legacy column above (that is the
cost of the migration window — this list is the reminder):

- [x] lobby poll (`polls_lobby` / per-loop `picker_lobby_poll()`)
- [x] remote-start preemption (scope checkers
      `picker_main_scope_remote_start_requested` /
      `team_build_remote_start_requested`, both declared in
      picker_sdl_defs.h). Engine screens whose LEGACY loop provably had no
      check keep `RemoteStartScope::None` at Layer E (10a fidelity: the
      options family — a joiner parked there launches when the screen exits
      into a checking loop, exactly as before); the allowlist in
      test_menu_engine.cpp guards against new screens forgetting a scope.
- [x] dual-label-surface writes (engine label pass / legacy per-callback +
      per-frame writes)
- [x] visibility + nav rewire + `ensure_highlighted_button_visible`
- [ ] level-reload guard (`last_level_id` vs `save.scen_num`) — legacy
      screens that read the loaded world carry their own; engine screens
      will use `frame_tick`
- [ ] G12 `autosave_on_mutation` / `ready_reset_on_mutation` — Layer F
      flags, dormant until the company/ready features land

Scope-down valves: **V1** — a screen whose injector tests disagree with the
normalized frame ordering keeps a per-screen FramePlan replaying its legacy
order (budget: ~3 screens, else re-derive the skeleton). **V2** —
networking's outer retvalue-as-action-id state machine stays in
SdlPickerClient; only its inner loop migrates, and only if identity holds.

## Migration recipe (per screen, one commit each)

1. Ensure the screen has an exact-table pin (G2; `tests/test_menu_pins.cpp`
   or test_menu_layout) BEFORE touching it.
2. Transcribe the `k_*` table row-for-row into a spec in
   menu_screen_specs.cpp (nav VERBATIM; static labels are the default-state
   formatter outputs). Plug the legacy sync/rewire function as
   `NavProgram::Rewire` (G1) — do not re-express it as data.
3. Move the `picker_*_buttons()` accessor pair to menu_screen_specs.cpp as
   `materialize_menu_buttons` shims (same signatures, same PickerState
   vector, same lifetime).
4. Point the screen's entry function at `run_menu_screen(spec)` and DELETE
   the legacy loop in the same commit. Delete now-redundant click-side
   label writes only when the engine label pass provably covers them (G8).
5. Flip the screen's registry row to Engine (and the table above).
6. Full gate: every existing test UNCHANGED (10a) — layout pins, injector
   flows, terminal suites, the interactive script. Re-pins are Layer F
   only.

## D1: why auto-derived nav is proof-gated (permanent record)

Hand-wired `MenuNav` graphs are provably non-geometric. Counterexamples
(from the shipped tables — do not lose these):

- **Crossed bottom-row down-links, native main menu** (picker.cpp
  k_mainmenu_buttons): `pvp_allied` (center x=114) has `.down=9` → quit
  (center x=150, dist 36) while `options` (center x=100, dist 14) is
  nearer; `level_edit` has `.down=10` → options (dist 86) while quit (dist
  36) is nearer. Deliberately crossed so BOTH bottom targets stay
  keyboard-reachable.
- **Wrap cycles**: `options_back` `.up` jumps to the bottom of the column;
  the difficulty BACK cycles up to GENERATOR RATE (`.up=5`).
- **Exact ties**: `continue_game` → `1_player` ties `2_player` at 36px; the
  hand-wired pick encodes leftmost-first intent.

Therefore: nav data moves VERBATIM into specs and stays pinned by
test_menu_layout. A screen may adopt derived nav only with a passing
`EXPECT_EQ(derive_nav_from_geometry(...), <legacy hand-wired links>)` in
test_menu_spec.cpp for EVERY visibility variant (G1). Derivation otherwise
survives only as an offline authoring generator for NEW screens and a
runtime route-around resolver for NEW screens' gating.

## Current limitations (deliberate, tracked)

- `EnterTransition` Fade* variants are not implemented (TESTING-checked);
  they land with the main-menu migration, which also brings the G14
  full-re-vdisplay `draw_content` hook.
- `NavProgramKind::RouteAround` (`rewire_nav_transitive_skip`) is reserved
  until a consumer proves equivalence (G1).
- `MenuBuildGate` filtering shifts materialized indices; until id-keyed nav
  (G9) lands, filtered variants must keep raw nav indices valid across
  variants (the main-menu quit/help fork satisfies this by construction).
- `RowTemplateSpec`/PageModel (§1.7) are declared but undefined; G6 requires
  proving PageModel against the VIEW LEVEL pager before the company list
  may depend on it.
- Engine tests live in `og_test_menu_engine` (fast group) and
  `tests/unit/test_menu_spec.cpp`; never add engine tests to
  og_test_menu_ui (G10).
