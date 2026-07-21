# Menu engine (MenuScreen Runtime)

Status: Layer E COMPLETE (invisible re-host of the legacy picker screens;
design: `docs/company-basecamp-design.md` §1). Every registry screen is
engine-hosted except NETWORKING, which stays legacy under scope-down valve
V2 — see the "V2 decision record" section. This document is the G4
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
| Main menu | **Engine** | `main_menu_screen_spec()` via `mainmenu()` — the §1.6 MP/no-MP spec pair (build-gated quit/help fork), `FadeWithInitialDraw` entry, MainScope remote-start with `BreakWithSelection`, the G14 full-re-vdisplay content hook, and the outline/label/art bindings that retired `redraw_mainmenu`'s raw `allbuttons_[N]` writes and both `OPTIONS_BUTTON_INDEX` #defines (`picker_mainmenu_options_index()` now) |
| Team build (base camp) | **Engine** | `team_build_menu_screen_spec()` via `create_team_menu()` — the §2.5 command roster (WP4), regridded per §9.5.1 then §9.10.1 to 9 deploy/row-body pairs at the 15px save-slot pitch over a PageModel window (round 2 trades the tenth row for clear margins around the roster block; §9.11 round 2 deletes the TRAIN column — the row BODY `roster_row_r` (26,y,208,10), a label-less `no_draw` hit zone at the old train ordinals 9..17, opens the train screen seeded on that character, `default_highlight` = row 0's body so Enter trains from the entry highlight — the curses roster grammar), MenuSpecRow dispatch, the legacy-action command strip, GO host-gating inside the full-graph rewire, `FadeAroundEntry`, and the level-reload guard + §3.3 row refresh in `frame_tick` (entered at -1 so the first frame always reloads). Networked shape (WP4 mp-columns): the display list is the MERGED lobby roster (`collect_base_camp_display_slots` — own rows first from the private save, then foreign machines' replicated slots), the rewire widens foreign dep buttons into (8,y,212,10) `no_draw` hit zones (click ⇒ OWNED BY popup) and hides their row-body train zones (the widened dep zone IS the foreign row click), the draw pass swaps CLASS/EXP for the 16-char COMPANY column and the READY n/m + DEP n/m line B, and the deploy dispatch carries the client-side 24-cap guard; the page window derives from the display size, so >24 replicated slots just grow the page count. §2.6 GO/READY slot (WP4 ready-go-slot): the READY twin shares GO's exact rect (row 25, `ToggleLobbyReady` with the twin's index as arg — the TEAMS mirror keeps arg −1) with mutually exclusive gates (host ⇒ GO, networked joiner ⇒ READY — the gate-lattice sweep's same-geometry allowance case); per-frame `MenuColorFormatter`/`LabelBinding` re-derive face + label from `picker_compute_ready_go_presentation()` (pure table: `format_ready_go_button`, faces 13/93/61 + the U1-fallback RED 40 — face 45 failed the recorded contrast check), the rewire stamps the same values for the entry compose, `go_menu` pre-checks §2.6 state 3 (WAITING FOR / NO ONE IS DEPLOYED popups, no request sent) and renders async `last_start_denial` echoes, and solo GO stays grey byte-identical; the curses lobby `r` key runs the same §2.6 client ready gate (caption surfaced as a status line) |
| View team | **RETIRED** (§2.5) | the base-camp roster IS the team view; `create_view_menu`/its spec/`MenuScreenId::ViewTeam` are gone (the in-game `view_team()` draw helper in picker.cpp survives for the gameplay HUD) |
| Save/Load slots | **RETIRED** (§3.8) | saving is automatic on every base-camp mutation + level win; loading is the §2.3 Company List. `DoSave`/`DoLoad`/`CreateSaveMenu` actions are no-ops; `CreateLoadMenu` survives only as the intercepted main-menu LOAD door |
| Hire | **Engine** | `hire_menu_screen_spec()` via `create_hire_menu()` (entry-time PREV/NEXT repositioning = `prepare_buttons`, so the accessor keeps the pinned table shape; solo-hidden team cycler = state override + nav-closure rewire; new-game popup = `frame_tick`; stat/cost/description panels = content hook in picker_team_build.cpp) |
| Train | **Engine** | `train_menu_screen_spec()` via `create_train_menu()` (the +/- pixie faces = `art_family` bindings re-applied after every reset; the bug-A9 promotion resync = `on_reset`; the wrapper keeps the need-a-team entry guards and the start-selected exit fold) |
| Progress | **Engine** | `progress_menu_screen_spec()` via `create_progress_menu()` (PREV/NEXT stay KEYBOARD-DEAD — the shipped myfun=0 shape, a declared exception to the liveness invariant; raw mouse-rect dispatch + per-row GO shortcuts + held-click spin-wait = `frame_tick`; no backdrop) |
| View level | **Engine** | `view_scenario_menu_screen_spec()` via `create_view_scenario_menu()` (`exit_on_redraw`; pager visibility = state override over the wrapper's PageModel, nav closure = rewire; the page-step stash is consumed by `consume_click` at the legacy frame point; pre-loop mount/load popup guards stay in the wrapper) |
| Scenario | **Engine** | `scenario_menu_screen_spec()` via `create_scenario_menu()` (host gating = the legacy sync as its Rewire program; `frame_tick`/`on_reset` = the level-reload guard; the wrapper folds BACK's MENU_EXIT unless a start was selected) |
| TEAMS | **Engine** | `teams_menu_screen_spec()` via `create_teams_menu()` (`exit_on_redraw`; the Rewire program replays the legacy compute+sync+trace verbatim from picker_team_build.cpp; row bars draw beneath the buttons in `draw_background`; the reload guard sits in `frame_tick` — one frame later than the legacy loop-top reload, the flows poll with timeouts). §2.7 cross-control (WP4 ready-go-slot): row 16 reuses the local-only guy-team rect (150,146,70,12) — the second same-geometry mutually-exclusive pair — visible to ALL peers when networked, host-only actionable via the screen's one `MenuSpecRow` dispatch (`picker_teams_menu_engine_on_spec_row`: non-host popup, {0,1} sanitize, TRACE, `picker_lobby_sync_settings_from_save()` ⇒ the server clears every non-host machine's ready per §4.5; SESSION-ONLY field ⇒ no company autosave); curses parity = the lobby's `[c]` key + `Control:` status line |
| NETWORKING | **Legacy (final for Layer E)** | `SdlPickerClient::configure_networking` (state-machine-owned; valve V2 EXERCISED — see the "V2 decision record" section; pinned by `MenuEngine.networking_stays_legacy_v2_decision`) |

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
   reads — always call buttons() first), `prepare_buttons` (entry-time
   descriptor fix-ups, once — hire's computed PREV/NEXT repositioning;
   per-frame visibility never lives here), `init_buttons`,
   `clear_keyboard`, art bindings (pixie `set_graphic` AFTER init_buttons —
   it overwrites w/h), initial gate/nav pass.
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
   - `consume_click` hook (screen-owned click consumption before
     reset_buttons — the VIEW LEVEL page-step stash);
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
manually across every screen still in the Legacy column above — after the
Layer E closeout that column is exactly NETWORKING (that is the cost of the
V2 valve — this list is the reminder):

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
- [x] level-reload guard (`last_level_id` vs `save.scen_num`) — engine
      screens that read the loaded world carry it in `frame_tick`
      (team build, SCENARIO, TEAMS); NETWORKING does not read the loaded
      world
- [x] G12 autosave-on-mutation + ready-clear — LIVE since WP4 via the WP2
      choke point rather than the declarative spec flags (which remain
      unset): every roster-mutation site (the base-camp deploy dispatch,
      hire `add_guy`, train-accept `edit_guy`, rename `name_guy`, the
      details-screen PROMOTE, the TEAMS per-character team cycle
      (SDL/text/curses), and the terminal hire/train/deploy flows) calls
      `picker_base_camp_after_roster_mutation()` /
      `company_autosave_after_mutation()` — lobby roster re-sync (a
      networked content change clears that machine's ready server-side),
      optimistic local ready drop, then the §3.8 autosave ([SAVE-F1] merge
      write in networked lobbies). NETWORKING hosts no mutation UI, so the
      legacy loop needs no sweep for this obligation. The deploy dispatch
      additionally carries the §2.0 U6 250 ms same-row tap debounce
      (accepted toggles stamp `BaseCampScreenState`; a debounced tap is a
      silent MENU_OK).

Scope-down valves: **V1** — a screen whose injector tests disagree with the
normalized frame ordering keeps a per-screen FramePlan replaying its legacy
order (budget: ~3 screens, else re-derive the skeleton; final tally: ZERO
FramePlans — no injector disagreed). **V2** — networking's outer
retvalue-as-action-id state machine stays in SdlPickerClient; only its inner
loop migrates, and only if identity holds. V2 was EXERCISED at Layer E
closeout: identity could not be met, networking stays legacy this pass (the
decision record below).

## V2 decision record: NETWORKING stays legacy (2026-07-20, Layer E final)

`SdlPickerClient::configure_networking` (picker.cpp) was evaluated against
the frame skeleton at the step-7 migration slot. Decision: **stay legacy**,
the design-sanctioned V2 outcome. All three resistance factors §1.4 names
are structurally present:

1. **The retvalue-as-action-id dispatch IS the inner loop.** The loop's
   retvalue carries `ButtonAction` values (EditNetworkAddress / EditNetworkPort
   / ToggleNetworkRoomCode / EditNetworkRoomCode / SubmitNetworkHost /
   SubmitNetworkJoin / JoinRelayRoomListEntry) consumed by a switch that
   calls ~10 private `SdlPickerClient` members (`networking_settings_`,
   `relay_rooms_`, `prompt_for_string`, `submit_network_host/join`, the
   begin/cancel/refresh/poll/commit relay room-list machinery) and RETURNS
   `true` out of the member function mid-loop on successful submits. There
   is no separable "outer" machine to leave behind: hosting the screen means
   a `consume_click` hook that is the entire legacy switch plus an exported
   bridge to the private state — a relabel, not a unification. The MENU_*
   retvalue contract (`reset_buttons` consumption) never held on this
   screen.
2. **The staged-commit room-list contract needs frame phases the runner
   does not have.** (a) A pre-input phase:
   `ensure_relay_room_list_view_is_current()`, sampling
   `room_update_was_pending`, and `poll_relay_room_list_request()` all run
   BEFORE input sampling, so a completion observed in the current frame
   stays staged until the next (clicks dispatch against the labels they
   were aimed at). The runner's first phase is the lobby poll; there is no
   pre-input hook. (b) The refresh-on-idle gate reads the raw
   `leftmouse() != 0` pointer-sample flag — a click that lands on NO button
   must still suppress that frame's refresh — and the runner exposes
   pointer-sample state to no hook. Both would be engine distortions
   serving exactly one screen, against behavior pinned as race-correctness
   (NetworkingMenu.room_click_during_refresh_joins_the_visible_snapshot: a
   queued click must join the row that was VISIBLE when clicked, not the
   just-fetched reorder). A split where the runner exits per action and the
   client re-enters also fails identity: re-entry re-inits buttons and
   resets the persistent keyboard highlight the legacy loop keeps across
   actions.
3. **The web/native fork is positional, not just row-gated.** The two
   `k_networking_menu_buttons` tables have different row SETS and different
   indices for the same ids (native: back 0, ip 1, port 2, toggle 3,
   room_value 4, host 5, join 6, rooms 7-11; web: back 0, room_value 1,
   host 2, join 3, rooms 4-8) with per-build `kNetworkingMenu*Index`
   constants. `MenuBuildGate` filtering is index-safe only when surviving
   indices agree across variants (the main-menu quit/help precedent); G9
   id-keyed nav is not implemented. A single spec is impossible without G9;
   two spec tables would deduplicate nothing.

Consequences while it stays legacy: the loop-obligations checklist applies
to exactly this one loop — any NEW cross-cutting obligation must be added
to `configure_networking` by hand. It polls the lobby every frame; it has
NO remote-start check (same 10a-fidelity class as the options family's
deleted legacy loops — a joiner parked here launches when the screen exits
into a checking loop); the Layer-F G12 obligations do not touch this
screen. Revisit trigger: if G9 id-keyed nav plus a runner pre-input phase
ever land for another consumer, re-evaluate — and migrate only with the
NetworkingMenu race suite unchanged as the identity oracle. The registry
pin `MenuEngine.networking_stays_legacy_v2_decision` must be flipped in the
same commit as any future migration.

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

- `EnterTransition::FadeWithInitialDraw` (the legacy mainmenu entry: one
  timer tick, fade out, first-frame compose, fade in, `grab_mouse`) and
  `FadeAroundEntry` (the legacy team-build entry: fade out, cold
  background+buttons draw, fade in) are both implemented.
- `NavProgramKind::RouteAround` (`rewire_nav_transitive_skip`) is reserved
  until a consumer proves equivalence (G1).
- `MenuBuildGate` filtering shifts materialized indices; the runner and the
  gate-lattice sweep pair `buttons[i]` with its spec row through
  `materialized_spec_rows()`, but NAV LINKS are raw materialized indices:
  until id-keyed nav (G9) lands, filtered variants must keep them valid
  across variants (the main-menu quit/help fork satisfies this by
  construction — one survivor, same index, same links).
- The `change_allied` raw `allbuttons_[7]` click-side write was DELETED by
  the G8 sweep in the Layer-F main-menu reshape (design §2.1), atomically
  with its test_picker_funcs re-pin — the pvp_allied LabelBinding re-derives
  both label surfaces every frame, so the write was redundant.
- `PageModel` is implemented (menu_binding.{h,cpp}) and PROVEN against the
  pinned VIEW LEVEL pager (the G6 differential oracle in
  tests/unit/test_menu_spec.cpp); the VIEW LEVEL screen runs on it, and the
  Layer-F Company List (§2.3, `company_list_menu_screen_spec()` — entered
  from the main-menu LOAD door via `IPickerClient::show_company_list`, not
  the registry) pages on it, as does its nested Backups sub-view (§2.4,
  `company_backups_menu_screen_spec()` — entered from the list's BK door via
  `run_company_backups_screen`, the difficulty-from-main-menu
  nested-engine-screen precedent). `RowTemplateSpec` (§1.7) remains a
  forward declaration until a Layer-F screen needs it — the Company List's
  dynamic rows are a FIXED 33-row table (10 row/BK/X triples + chrome), the
  Backups sub-view's a fixed 13-row table, and each screen's per-frame
  rewire windows them over the PageModel, so stable ids (`company_row_0`..,
  `backup_row_0`..) come free and no template machinery was required.
- Engine tests live in `og_test_menu_engine` (fast group) and
  `tests/unit/test_menu_spec.cpp`; never add engine tests to
  og_test_menu_ui (G10).
