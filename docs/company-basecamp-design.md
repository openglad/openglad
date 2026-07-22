# Company & Base Camp — Final Design

Status: FINAL synthesis. Implementers work from this document alone.

Provenance: architecture panel majority winner = **"MenuScreen Runtime: one declarative
menu engine for SDL/text/curses (invisible-first, feature-ready)"** (2/3 judges); UX panel
majority winner = **"Company & Base Camp UX — 320x200 roster-first information design"**
(2/3 judges); the save-v14 and protocol-v8 single designs are adopted **with every critic
required_change applied** (each marked `[SAVE-Fn]`/`[SAVE-Rn]`/`[NET-Fn]`/`[NET-Rn]` inline).
All judge grafts are folded in; conflicts between grafts/sections are resolved in §8
(Conflict Resolutions & Rejections). File:line references were verified against
`feature/company-basecamp` (== master).

---

## 0. Locked feature spec (user decisions — not relitigable)

1. Group term **"Company"** (gladiator company). A save file IS a company. One active
   company per machine; unlimited company files.
2. **Save v14** adds: last-played timestamp, last mission-roster selection (per-character
   deploy flag), backup lineage as needed. Existing: 40-byte `save_name` (= company display
   name), roster ≤ 24, money, per-campaign progress, difficulty/CTF/tower settings.
   Filenames obey `is_safe_virtual_basename` (64 chars, alnum `._-`, no spaces) — display
   name and filename are separate concerns.
3. **Main menu**: Begin New Game (KEEP its existing pixel-art button face) = create new
   company → name entry with generated fantasy default + reroll + editable → base camp.
   Below it: split **Continue** (opens most-recent company) and **Load** (scrolling
   unlimited list, delete, and a Backups sub-view). Save/Load buttons REMOVED from base
   camp; saving is automatic (every base-camp mutation + level win).
4. **Backups**: snapshot per level win, retention ~20/company, restore = rewind-in-place
   with the pre-restore state itself backed up first.
5. **Base camp** (reimagined Team Build): roster view is the DEFAULT view; per-character
   Train buttons (opens existing TrainSession for that char — no more enter-then-cycle);
   Hire at the bottom; per-character mission-deploy toggles (persisted); in MP an
   Origin/Company column shows each character's owning machine's company name.
6. **MP rights**: only the owner machine trains its characters / toggles their deployment.
   Host-only setting **"cross-control"** (default OFF): ON = players may control others'
   characters in-level; same-machine seats always free among their own machine's
   characters. Cross-control ON removes the per-machine deploy minimum (bring 0, play a
   friend's chars); global ≥ 1 deployed required regardless.
7. **Ready**: clients get a Ready button in the SAME slot where the host has GO; state
   clearly visible (button color states). Host GO gated on all clients ready. A machine's
   roster/settings change clears that machine's ready. Solo/local: no Ready, GO as today.
   MUST solve the known trap: every lobby join re-send resets ready=false and go_menu
   re-sends join right before requesting start (deadlock hazard); local/solo LobbyServer
   must be exempt from gating.
8. **Money**: networked win pot split by deployed-character ratio measured at deploy time
   (PRE-fold roster copy — the fold drops the dead), remainder to largest contributor,
   tie-break lowest player index; each machine banks only its share (replaces today's
   pinned full-pot duplication — that behavior is a bug being fixed). Local/solo keeps
   whole-pot byte-identical. 0-deployed machine: no money, no completion credit. Results
   screen shows the machine's share.
9. **Follow mode**: machines with 0 deployed chars (or all deployed chars dead) get a
   follow camera with an input binding to cycle watched targets; control rights are
   SERVER-authoritative; the follow camera must NOT stamp user tags (HUD/radar gate on
   `user()`).
10. **Menu engine unification**: ONE declarative menu specification (structure, item
    order, label bindings, gating predicates, cancel semantics, pagination,
    lobby-poll/remote-start obligations) consumed by all three clients (SDL, text,
    curses); only rendering/geometry/input stays client-specific. Component boundaries
    preserved (shared model in og_interface ui layer as today; platform clients consume
    it). Test re-pins land atomically.
10a. **STRICT NO-VISIBLE-DIFF RULE** for the unification refactor: until the intentional
    menu changes of this feature land on top, the unified engine must reproduce the
    CURRENT menus with zero visible or behavioral difference — identical pixel geometry,
    labels, draw order, hotkeys, keyboard-nav graphs (hand-wired semantics preserved
    exactly; auto-derived nav is acceptable ONLY if it provably reproduces the existing
    links), timing/transitions (fadeblack), and byte-identical text/curses output. The
    PROOF is the existing test suite: test_menu_layout geometry+nav pins, the text
    1-based index consumers, the curses picker suite, and the wasm coordinate helpers
    must all pass UNCHANGED against the engine-rendered legacy menus. Engine refactor and
    menu redesign are separable layers: engine lands invisibly first; new/changed screens
    (which ARE allowed to differ — they are the feature) come after. Any architecture
    that cannot meet this on legacy screens must be rejected or scoped down.
11. **Protocol v7→v8** (`kNetworkProtocolVersion`, net_transport.h:72). Update the
    literal wire-byte tests (test_net_transport.cpp:240, :829-830;
    test_input_state_net.cpp:133, :149), wire size constants
    (`kMinSerializedLobbyPlayerSize`/`kMinSerializedLobbyCharacterSlotSize`), and ALL
    field-copy sites (`make_lobby_character_data` ×3, `make_guy_from_lobby_character` ×3,
    `append/read_serialized_guy`).
12. **Migration**: v13 saves load (tolerant reader); the ~27 production `"save0"`
    literals become an active-company indirection; `"netsession"` stays (server economy);
    demo bootstrap, wasm e2e seeding, and the interactive script's save-path pins handled
    explicitly.
13. **Everything green**: ci-test, og_test_parity (zero sim-file line inserts above
    canary pins), menu layout pins, text 1-based index consumers
    (scripts/test_text_picker_interactive.sh, tests/unit/test_platform_headless.cpp:553-612),
    wasm raw-coordinate consumers (tests/e2e/wasm_helpers.js:212-254 and the raw taps
    in tests/e2e/wasm-touch.spec.js) + dist rebuild,
    coverage ≥ 90/95 with tests written alongside.

---

## 1. Menu-engine architecture (FINAL)

Adopted: **MenuScreen Runtime** (invisible-first), with grafts G1–G14 below. The engine
lands as **Layer E** (invisible re-host of every legacy picker screen, proof = existing
suite UNCHANGED) followed by **Layer F** (the feature screens, with their own atomic
re-pins). Anything in Layer E that cannot be made invisibly identical gets a named
**scope-down valve**, never a fudge.

### 1.1 Decision record

- **D1 — Auto-derived nav REJECTED as source of truth.** Hand-wired `MenuNav` graphs are
  provably non-geometric; the counterexamples are recorded permanently in
  `docs/menu-engine.md` as the reason auto-nav is proof-gated (G1):
  - crossed bottom-row down-links, native main menu (picker.cpp:1523-1526 as shipped:
    `pvp_allied` center x=114 has `.down=9`→quit center x=150 dist 36 while `options`
    center x=100 dist 14 is nearer; `level_edit` `.down=10`→options dist 86 while quit
    dist 36 is nearer — deliberately crossed so both bottom targets are reachable);
  - wrap cycles (`options_back` `.up`→bottom of the column; difficulty BACK cycles up to
    GENERATOR RATE);
  - exact ties (`continue_game`→`1_player` ties `2_player` at 36px; hand-wired pick
    encodes leftmost-first intent).
  Nav data moves verbatim into the specs and stays pinned by test_menu_layout.
  Derivation survives as (a) an offline authoring generator for NEW screens
  (`scripts/gen_menu_nav.py`, output checked in as hand-owned data) and (b) a runtime
  route-around resolver for visibility gating on NEW screens only.
- **D2 — `do_call` stays.** The `vbutton::do_call` switch (button.cpp:520-768), the
  `ButtonAction` enum, the intercept hook (`picker_try_intercept_button_action`,
  picker.cpp:246-297), and the MENU_* retvalue conventions are untouched at Layer E. The
  runner invokes activation exactly as `leftclick`/`handle_menu_nav` do today.
- **D3 — Accessors as materialization shims.** `picker_*_buttons()` /
  `picker_*_button_count()` keep their signatures and now materialize `button` rows FROM
  the specs with identical values. The hard-coded `kExpected` tables in test_menu_layout
  remain an **independent oracle** over the spec data: a transcription typo fails an
  existing test, not a new self-referential one.
- **D4 — Terminal unification is model-deep, not loop-deep.** Text and curses keep their
  input loops (they ARE the clients); Layer E unifies the duplicated label switches,
  gate/guard messages, cancel semantics, and the triplicated Difficulty nested loop.

### 1.2 Grafts adopted (binding)

- **G1** (all judges): hand-wired rewire FUNCTIONS (`picker_wire_team_build_nav`,
  `picker_wire_scenario_menu_nav`, `sync_difficulty_menu_visibility`'s BACK cycle,
  `picker_wire_teams_menu_nav`, `picker_wire_networking_menu_nav`) survive verbatim as
  pluggable `NavProgram::Rewire` — never re-expressed as generic data. Auto-nav
  proof-gate rule is a **hard contract**: a screen may adopt derived nav only if
  `test_menu_spec.cpp` contains a passing
  `EXPECT_EQ(derive_nav_from_geometry(...), <legacy hand-wired links>)` for every
  visibility variant of that screen.
- **G2** (pin-then-migrate): before hosting ANY screen whose test_menu_layout coverage is
  only overlap/bounds today (main menu, hire, train, save/load slot menus, progress,
  view-team), land a **prior commit** adding an exact `kExpected` table transcribed from
  the CURRENT `k_*` table; then migrate with pins untouched. Every screen migration gets
  a differential oracle.
- **G3** (generic row dispatch): Layer E adds ZERO ButtonAction values. Layer F adds
  exactly **one**: `MenuSpecRow = 101` (append at button.h after
  `JoinRelayRoomListEntry = 100`; comment: generic engine-row dispatch, arg =
  materialized row ordinal). Its `do_call` case mirrors the stash-and-return pattern of
  100 (button.cpp:648-652): `pks().menu_spec_clicked_row = arg; return whatfunc;`. All
  new-screen rows (company list rows/delete/backups, restore, per-character train/deploy
  cells, name reroll/OK/cancel, cross-control, engine pagers) route through 101 — zero
  further enum burn, and `myfun != 0` makes every engine row keyboard-live.
  **Retvalue-collision discipline (mandatory):** `MENU_EXIT` is the 1-bit
  (picker_sdl_defs.h) and 101 is odd, so `101 & MENU_EXIT != 0` — the runner MUST consume
  the stash and **zero retvalue before the loop-condition test** (the networking loop's
  `retvalue = 0` discipline). A TESTING assert enforces the stash was consumed.
- **G4** (registry + contract doc): `menu_screen_host(MenuScreenId)` returns
  Engine(spec) or Legacy(fn) — the one-lookup answer to "which system owns this screen"
  during the migration window. `docs/menu-engine.md` carries the calling-convention
  contract (MENU_* flags, `pks().selected_menu_item`, remote-start + lobby-poll
  obligations), the migration recipe, and a **loop-obligations checklist** that any new
  cross-cutting obligation must sweep while any screen remains legacy (valves V1/V2).
- **G5** (remote-start generator test): a parameterized integration test drives
  "remote-start preemption fires from EVERY engine screen", both scopes, over the spec
  registry — killing the strand-the-joiners bug class structurally.
- **G6** (pagination oracle): RowTemplate/PageModel must be proven against a legacy
  oracle BEFORE the company list depends on it — migrate the pinned VIEW LEVEL pager
  (`kViewScenarioRowsPerPage = 23`, `ViewScenarioPageFlip = 69`, hidden-when-≤1-page)
  onto PageModel as its differential in Layer E.
- **G7** (TerminalMenuModel): full unfiltered entry list (preserves the 1-based index
  contract by construction), `cancel_item` (Quit on Main / Back elsewhere), gate
  guard-message strings moved verbatim; used for LoadCompany/Backups/NameEntry terminal
  flows. Plus the deeper terminal dedup set: `menu_cancel_item` helper,
  `show_submenu(PickerMenuId)` generalization of `IPickerClient` (kills the triplicated
  Difficulty nested loop at picker_state level), and a shared `level_display_guarded`
  mount-guard helper deduplicating text_picker.cpp:626-631 /
  curses_picker_client.cpp:298-303.
- **G8** (label-write sweep): on every engine-hosted screen, delete the now-redundant
  click-side label writes AND sweep the raw `allbuttons_[N]` index writes in `do_call`
  callbacks (`change_allied`→[7] is live on the main menu; `change_teamnum`→[18],
  `change_hire_teamnum`→[2] when those screens migrate). The Layer-F main-menu reshape
  MUST include this sweep in the same commit as its re-pin, or shifted indices silently
  relabel the wrong button.
- **G9** (id-keyed nav for new/materialized rows): NEW screens and the build-gated
  quit/help fork use id-keyed nav references resolved to indices at materialization —
  eliminates the index-remap-compiler bug class. Add unit pins for the noMP and web
  shapes of the materialized main-menu spec (the touch variant has no other oracle).
- **G10** (test placement): generator sweeps and spec pins go in fast groups
  (`og_unit_families` unit file + a NEW `og_add_test_group(og_test_menu_engine ...)`),
  NEVER og_test_menu_ui (~130s native vs 180s timeout on a shared, flaky-under-load
  machine). Heavyweight Layer-F flows go in a new `og_test_basecamp` group.
- **G11** ("generated pins replace NONE"): the layout/nav generator is an authoring tool
  that dumps initial `kExpected` tables checked in as static, hand-owned test data. Hand
  pins remain the independent oracle; spec typos cannot self-validate.
- **G12** (single-point obligations): declarative per-screen obligation flags
  `autosave_on_mutation`, `ready_reset_on_mutation`, `level_reload_guard`, and the
  `SyncSettingsAfterMutation` bit — the engine calls
  `picker_lobby_sync_settings_from_save()` (and, Layer F, `company_autosave` + the ready
  clear) ONCE after any handled click on lobby-backed screens, replacing per-callback
  tails. Viable because Layer E hosts hire/train/TEAMS (every roster-mutation surface).
- **G13** (drift pins + consistency glue): `index_of("go") == kCreateMenuGoIndex`-style
  drift pins for the window where spec ordinals and picker_sdl_defs.h constants coexist;
  spec↔model consistency tests (every non-empty `item_id` resolves via
  `find_picker_menu_item`, per-screen model-coverage sets with declared exceptions);
  resolver golden-equivalence tests byte-equal to the three legacy switch outputs across
  FULL value cycles (incl. SPECTATOR and the campaign mount-guard cases) before any
  terminal switch is deleted; the gate-lattice enumeration generator (every engine screen
  × {host, networked, ctf_campaign, roster_nonempty, numplayers ∈ {0,1,4}}) driving
  BFS-reachability, no-visible-overlap, and keyboard-liveness invariants — with the
  cross-variant overlap assertion **allowing two rows with mutually exclusive gates to
  share geometry** (this is what structurally validates the Ready/GO shared slot).
- **G14** (main-menu chrome fidelity): the main menu's `draw_content` hook must retain
  `redraw_mainmenu`'s FULL re-vdisplay-after-title pass, not just title/columns —
  picker_main_menu.cpp:126-134 re-vdisplays all buttons AFTER the title drawMix, and the
  136×58 title frames at (15,8)/(151,8) overlap `begin_new_game` (80,50,140,20) in
  y=50-66; the runner's clearbuffer→draw_buttons→content order would invert that overlap
  unless the hook re-vdisplays.

### 1.3 Files and types

**SDL-free shared layer** (compiled into og_interface AND the text/curses targets — the
`menu_model.cpp`/`picker_common.cpp`/`picker_state.cpp` TU-sharing pattern; add new .cpp
files to all four CMake source lists: CMakeLists.txt:232-236, 488-492, 1266-1268,
1544-1547):

- `include/openglad/interface/ui/menu_binding.h` + `src/interface/ui/menu_binding.cpp`:

```cpp
namespace og::ui {
struct MenuLabelContext {                 // per-client borrow bundle
    const SaveData* save = nullptr;       // SDL: myscreen_->save_data; terminals: member
    int session_difficulty = 0;
    bool is_networked = false, is_host = true, spectator = false;  // numplayers == 0
    // Layer F: company name, lobby ready states, cross-control, company/backup indexes
};
using LabelFormatter = std::string (*)(const MenuLabelContext&);
struct LabelBinding { std::string_view fixed; LabelFormatter formatter = nullptr; };
enum class MenuGate : std::uint8_t {
    Always, HostOnly, NetworkedOnly, LocalOnly, CtfCampaignOnly, Custom };
struct GateBinding {
    MenuGate gate = MenuGate::Always;
    bool (*custom)(const MenuLabelContext&) = nullptr;
    std::string_view guard_message;       // terminals: shown on activate when gated
};
std::string menu_item_label(const PickerMenuItem&, const MenuLabelContext&);
// THE single label switch replacing text_picker.cpp:334-361 and
// curses_picker_client.cpp:305-332 (bodies call the picker_common format_* fns
// so output is byte-identical).
RowState gate_state(GateBinding, const MenuLabelContext&);   // Visible/Hidden/Disabled
void rewire_nav_transitive_skip(NavLinks*, const RowState*, int count);
NavLinks derive_nav_from_geometry(const RowGeometry*, const RowState*, int, int row);
}
```

  `rewire_nav_transitive_skip` rule: follow the hidden target's own link in the same
  direction until a Visible row or none; a walk returning to origin emits −1 (verified to
  reproduce `picker_wire_team_build_nav` both variants and the difficulty non-host
  BACK-only cycle — but legacy screens still plug their existing rewire fns, G1).

- `include/openglad/interface/ui/terminal_menu_model.h` + `.cpp` (G7):

```cpp
struct TerminalMenuEntry { const PickerMenuItem* item; std::string label; bool selectable; };
struct TerminalMenuModel {
    std::string title;
    std::vector<std::string> context_lines;   // roster/gold header
    std::vector<TerminalMenuEntry> entries;   // FULL list, never filtered (index contract)
    const PickerMenuItem* cancel_item;        // Quit on Main, Back elsewhere
};
TerminalMenuModel build_terminal_menu_model(PickerMenuId, const MenuLabelContext&);
std::string_view terminal_gate_message(const PickerMenuItem&, const MenuLabelContext&);
```

  Text `present_menu` prints `entries[i].label` with the same `%2zu.` format; curses
  feeds entries into `Menu::choose`; both delete their private `menu_item_label`. The
  Difficulty-loop triplication collapses to a default
  `IPickerClient::show_difficulty_menu()` in picker_state (SDL overrides with its
  blocking subscreen). Output byte-identical ⇒ interactive script + curses suite pass
  unchanged.

**SDL engine layer** (og_interface only):

- `include/openglad/interface/ui/menu_screen_spec.h`: `MenuButtonSpec` (id, LabelBinding,
  hotkey, x/y/w/h, ButtonAction+arg, `MenuNav` transcribed VERBATIM, GateBinding,
  art_family pixie face, OutlineBinding, ColorBinding, no_draw, BuildGate),
  `NavProgram{Static|Rewire|RouteAround}`, `RemoteStartScope{None,MainScope,TeamBuildScope}`,
  `EnterTransition{None,FadeAroundEntry,FadeWithInitialDraw}`, and `MenuScreenSpec`
  (rows, nav program, remote_start, intercept scope, enter transition, default highlight,
  right_click_enabled, backdrop flag, per-screen hooks `draw_background` /
  `draw_content` / `frame_tick` / `on_reset`, `exit_value`, obligation flags
  `polls_lobby`, `level_reload_guard`, `autosave_on_mutation`,
  `ready_reset_on_mutation`, `sync_settings_after_mutation` (G12), and `RowTemplateSpec*`
  for dynamic screens). `MenuScreenHost{Engine|Legacy}` registry (G4).
- `src/interface/ui/menu_screen_runner.cpp`:
  `Sint32 run_menu_screen(const MenuScreenSpec&, void* screen_state = nullptr)`.
- `src/interface/ui/menu_screen_specs.cpp`: legacy screen specs transcribed row-for-row
  from the `k_*` tables (then deleted from picker.cpp:1495-1919); the `picker_*_buttons()`
  accessors move here as materialization shims (fill `PickerState`'s mutable vectors from
  specs; same lifetime rules, cleared in `picker_cleanup_resources`).
- `docs/menu-engine.md` (G4), `tests/unit/test_menu_spec.cpp` (og_unit_families),
  `tests/test_menu_engine.cpp` (new group `og_test_menu_engine`, G10).

### 1.4 The single frame skeleton

Transcribed from the canonical loops (mainmenu picker_main_menu.cpp:142-218,
create_team_menu picker_team_build.cpp:395-501, run_difficulty_menu picker.cpp:2744-2814):

```
run_menu_screen(spec):
  materialize buttons via accessor (assert count ≤ MAX_BUTTONS=50); build MenuLabelContext
  apply visibility + nav program + ensure_highlighted_button_visible
  ENTER per spec.enter (FadeAroundEntry | FadeWithInitialDraw | None — per-screen timing
    preserved; injector SDL_Delay(750) cadence unchanged)
  apply art bindings (set_graphic AFTER init_buttons — pixie overwrites w/h,
    button.cpp:163-171, same call at the same points)
  loop while !(retvalue & MENU_EXIT):
    picker_lobby_poll() if polls_lobby
    refresh visibility (BOTH surfaces via sync_button_hidden_state) + nav program
      + ensure_highlighted_button_visible
    REMOTE-START per spec.remote_start — BOTH exit shapes modeled: subscreens RETURN the
      remote MENU_EXIT (run_difficulty_menu picker.cpp:2764-2766 semantics); mainmenu
      breaks and lets present_menu act on selected_menu_item — selected by spec.exit_value
    click = leftmouse(buttons); 1 → leftclick(); 2 && right_click_enabled → rightclick()
    handle_menu_nav(buttons, highlighted, retvalue)          // unchanged spin-wait
    if pks().menu_spec_clicked_row >= 0: dispatch spec row; retvalue = 0   // G3 discipline
      (structural exit → set selected_menu_item, return MENU_EXIT; setting cycle +
       G12 obligations: sync_settings / autosave / ready-clear; page flip; open subscreen)
    was_reset = reset_buttons(...); if was_reset → re-apply art bindings + spec.on_reset
    if retvalue & MENU_EXIT → break
    if spec.frame_tick && !frame_tick(frame) → break         // level-reload guard etc.
    LABELS: every bound row → buttons[i].label AND allbuttons_[i]->label (dual surface,
      generalization of picker.cpp:2788-2799); OutlineBinding → do_outline;
      ColorBinding → vbutton::color
    DRAW: clearbuffer; backdrop; draw_background; draw_buttons; draw_content
      (main menu: FULL re-vdisplay-after-title retained — G14); pagination indicator;
      draw_highlight; buffer_to_screen; sleep_ms(10)         // the ONE asyncify yield
  return per spec.exit_value / MENU_EXIT propagation
```

Ordering deltas between legacy loops were audited on 3 of ~14 loops; **scope-down valve
V1**: if any injector test disagrees with normalized ordering, that screen keeps a
per-screen `FramePlan` phase array replaying its exact legacy order. If more than ~3
screens need FramePlans the skeleton is re-derived from the outliers (self-set failure
budget). **Valve V2**: networking's inner loop migrates to the runner (labels via
bindings, nav via `picker_wire_networking_menu_nav`, staged-commit room list in
`frame_tick`); the outer retvalue-as-action-id state machine stays in `SdlPickerClient`;
if identity can't be met, networking stays legacy this pass. Emscripten: exactly one
`sleep_ms(10)` yield per iteration under `-sASYNCIFY`, no function statics, go_menu's
MENU_EXIT web unwind (picker_team_build.cpp:2635-2640) preserved,
`reset_mouse_click_tracking` baseline stays in `picker_reinit_after_game`.

### 1.5 Gating → nav → BFS; per-slot gating context

Per frame: `GateBinding` per row → `RowState`; hidden written to BOTH surfaces; then nav
precedence (1) screen `nav_rewire` hook (legacy rewires — G1), else (2) generic
transitive-skip over the verbatim base graph, else (3) proof-gated auto-nav (G1). Then
`ensure_highlighted_button_visible`. Engine invariant (TESTING builds): after nav
application no visible button's nav targets a hidden one; Disabled rows count as visible
for nav, draw dimmed, and activation no-ops with a TRACE.

**Per-slot gating** (spec item 6 — none of the three proposals solved this; resolved
here): `RowCellSpec` gates take the materialized row's arg/slot —
`RowState (*cell_gate)(const MenuLabelContext&, int slot)` — with ownership derived
server-authoritatively from which `LobbyPlayer`'s `character_slots` list the slot sits in
(NEVER a client-supplied owner field, per lobby risk 5). `GateSyncHook` remains the
escape hatch for anything not expressible.

### 1.6 The four `k_mainmenu_buttons` variants

One `kMainMenuSpecMP` + one `kMainMenuSpecNoMP` (the no-MP variant genuinely repositions
rows), selected at registry init by `DISABLE_MULTIPLAYER` (with the existing
`USE_TOUCH_INPUT ⇒ DISABLE_MULTIPLAYER` mapping kept where it is defined today,
picker.cpp:1485-1487). The web/native fork is a row pair
`{"quit", NativeOnly}` / `{"help", WebOnly}` — geometry identical, hotkey differs —
filtered at materialization with **id-keyed nav** (G9; nav indices are identical across
the quit/help forks, so 2 static nav columns cover all 4 variants). Both
`OPTIONS_BUTTON_INDEX` `#define`s die (picker.cpp ×4 + picker_main_menu.cpp:31-35) —
replaced by `picker_mainmenu_options_index()`. `redraw_mainmenu`'s raw
`allbuttons_[2..5]/[7]/[0]` writes become `OutlineBinding::PlayerCountEquals` +
`LabelBinding` AlliedOrSpectator + `art_family` rows (`begin_new_game` FAMILY_NORMAL1 —
**the normal1.png pixel-art face is preserved by construction**: same empty label, same
set_graphic path, engine re-applies after every reset; `options` FAMILY_WRENCH). Unit
pins for the compiled variant's materialized table plus noMP/web shapes (G9).

### 1.7 New-screen machinery (Layer F)

- `RowTemplateSpec`: dynamic screens declare a row template (per-slot cells stamped with
  stable ids `train_0..`, `arg = page*rows + r`) + fixed chrome; materialization stamps
  the current page; ids stable across page flips (injector API contract — tested).
- `PageModelSpec`: page count from item count, real `MenuSpecRow` pager actions
  (keyboard-live), "a-b of N" indicator hook — proven on the VIEW LEVEL pager first (G6).
- Nav for templated rows: generated per page by the grid rule (machine-generated layout ⇒
  derivation is provable here), then route-around for gating; BFS test per screen via the
  gate-lattice generator (G13).
- Terminal projections come from `build_terminal_menu_model` over new `PickerMenuId`
  values (`LoadCompany`, `Backups`, `NameEntry` appended).

### 1.8 Migration sequence (each step lands green on the full gate)

0. (G2 pre-commits) exact-table pins for main menu, hire, train, save/load slots,
   progress, view-team, transcribed from the CURRENT tables.
1. Shared layer: menu_binding + terminal_menu_model + shared `menu_item_label` +
   `menu_cancel_item` + `show_submenu` + mount-guard helper; text/curses switch deletion.
   Proof: curses 53-test suite, headless text tests, interactive script — unchanged.
2. Runner + first spec: Difficulty subscreen (smallest canonical loop with gating +
   labels + remote-start). Delete `run_difficulty_menu` body.
3. Options family (main_options, 3 FX screens, display, controls).
4. Main menu (4-variant unification, outlines/art bindings, G14 re-vdisplay hook) —
   heaviest 10a screen; wasm e2e + interactive script are the extra oracles.
5. Team build + view team + scenario + TEAMS (Rewire programs) + save/load slot menus +
   VIEW LEVEL pager onto PageModel (G6).
6. Hire/train/progress/view-scenario (content-hook-heavy; progress PREV/NEXT stay
   keyboard-dead in Layer E — fixing that is a visible change, deferred).
7. Networking (valve V2).

Each step deletes the corresponding legacy loop in the same commit (no orphaned
uncovered lines). `og_test_menu_ui` must pass unchanged at every step.

### 1.9 Test ledger

- **Unchanged (the 10a proof)**: all 28 test_menu_layout tests; test_menu_model;
  test_menu; test_ctf_ui; test_networking_menu; test_options_menu; test_save_menu;
  test_save_load_team UI flows; test_picker_menu_nav; test_picker_state_machine;
  test_back_to_mainmenu; test_view_menu_driven; test_picker_detail_menu_driven;
  test_new_game; test_hire_team; test_train_team; test_picker_network_client;
  tests/curses/test_curses_picker_client.cpp (53); tests/unit/test_picker_common.cpp;
  tests/unit/test_platform_headless.cpp:553-630; scripts/test_text_picker_interactive.sh;
  wasm_helpers.js:212-254 + all four Playwright specs (dist rebuild only because code
  changed; helpers don't move).
- **New in Layer E**: tests/unit/test_menu_spec.cpp (spec↔model consistency,
  spec-vs-k*Index asserts, resolver golden equivalence, transitive-skip unit cases,
  gate-lattice invariant sweep: bounds, no-overlap-among-visible (with the
  mutually-exclusive-gate same-geometry allowance), 6px/char budget across binding value
  cycles, nav closure + BFS, id uniqueness, keyboard-liveness, ≤MAX_BUTTONS);
  tests/test_menu_engine.cpp in og_test_menu_engine (dual-surface refresh under
  lobby-rewrites-save; pixie face + w/h re-apply after reset; remote-start propagation
  from every engine screen in both scopes (G5); Disabled-row no-op; pagination
  normalization; retvalue-zero discipline).
- **Replaced by generated pins: NONE** (G11).
- **Re-pinned in Layer F** — only in the WPs that intentionally change screens (§7).

---

## 2. Per-screen UX specs (FINAL)

Adopted: the roster-first 320×200 design, with UX-panel grafts U1–U12 applied inline.
All coordinates are 320×200 canvas pixels. Mockups are schematic; **the rect tables are
normative.**

### 2.0 Design-language ledger (normative)

- Font: 6px/char advance; button label budget = `floor((w−8)/6)` chars; labels centered,
  unclipped (button.cpp:198-200). Every at-budget label below is pre-verified against
  this formula.
- Bevel button; per-instance face color via `vbutton::color` (default `BUTTON_FACING=13`,
  drawn at button.cpp:193). Shipped precedent: red special face `BUTTON_FACING+32 = 45`
  (button.cpp:238); FX-toggle colored faces `draw_button_colored(LIGHT_GREEN=56 / RED=40)`
  + DARK_BLUE label (picker.cpp:2317-2328).
- Palette anchors (base.h:119-137, core/constants.h:210): grey 0-15, RED 40, LIGHT_GREEN
  56, green run 56-71, DARK_BLUE 72, YELLOW 88, yellow run 88-103, WHITE 24, GREY 23,
  ORANGE_START 224. **Face colors 61 (green run) and 93 (yellow run) have never shipped:
  before the color table is pinned, a one-frame TESTING screenshot must verify contrast
  of faces 45/61/93 against DARK_BLUE labels; the sanctioned fallback is the shipped
  `draw_button_colored` LIGHT_GREEN/RED grammar** (U1). Labels stay DARK_BLUE in every
  state (no per-state label-color channel — rejected, §8).
- Captions over backdrop art use the **black-strip idiom**
  (`draw_rect_filled(PURE_BLACK, 150)` + WHITE text — the SCEN-hint idiom,
  picker_team_build.cpp:463-489), never bare colored text (U2).
- Destructive actions use **NO-first `no_or_yes`** confirms (picker_dialogs.cpp:43-47),
  never YES-first `yes_or_no` (U3). `popup_dialog` under TESTING is trace-only — assert
  via `trace_contains("popup", ...)`.
- Active-selection marker = red `do_outline` (the player-count grammar) (U4).
- Readability bars/backing fills paint in the PRE-`draw_buttons` pass (draw-order rule);
  row text paints in the content pass.
- Soft-keyboard lint (web): any in-place-editable text field sits with y+h ≤ 100 (U5).
- Touch: single-tap only (pending-click queue); roster-row taps get a 250 ms debounce
  note — a second toggle of the same row within 250 ms is ignored (every mistap clears MP
  ready) (U6). Fallback if device testing fails the 12px pitch: 15px save-slot pitch,
  10 rows/page (pre-declared contingency).
- Art assets: **zero new pixel art.** Reused byte-identical: normal1.png (Begin New Game
  face + re-apply-after-reset dance), wrench.png, title.png, columns.png,
  mainul/ur/ll/lr.png, butplus/butminus.png. Family chip = draw_box with the view_team
  color convention `((family+1)<<4)`.

### 2.1 Main menu (native+MP variant; other 3 variants noted)

Change vs picker.cpp:1516-1534: split `continue_game` (80,75,140,20) into a side-by-side
68×20 pair (the established player-button grammar). `begin_new_game` keeps its rect
(80,50,140,20), empty label, and the normal1.png face — byte-identical art.

```
+--------------------------------------------------------------------------------+
|   [title.png]                                          [title.png]             |
|  col |        [##### BEGIN NEW GAME (pixel art) #####]  (80,50)      | col     |
|      |        [ CONTINUE ]           [   LOAD   ]       (y=75)       |         |
|      |        [ 1 PLAYER ]           [ 2 PLAYER ]       (y=100)      |         |
|      |        [ 3 PLAYER ]           [ 4 PLAYER ]       (y=125)      |         |
|      |        [------------ DIFFICULTY ----------]      (y=148)      |         |
|      |        [PVP: Allied]          [Level Edit ]      (y=160)      |         |
|                 ~COMPANY: IRON KETTLE BAND~     (y=171, black strip)           |
|                    [gear]  [   QUIT    ]        (y=182)                        |
+--------------------------------------------------------------------------------+
```

| idx | id | rect | label | budget | notes |
|---|---|---|---|---|---|
| 0 | begin_new_game | (80,50,140,20) | "" (art) | — | UNCHANGED; routes to §2.2 |
| 1 | continue_game | (80,75,68,20) | `CONTINUE` | 8/10 | opens most-recent company |
| 2–5 | 4/3/2/1_player | unchanged | | | |
| 6–10 | difficulty, pvp_allied, level_edit, quit/help, options | unchanged | | | options index stays 10 |
| 11 (appended) | load_company | (152,75,68,20) | `LOAD` | 4/10 | opens Company List (§2.3). Appended at table END per the index-contract rule so the raw `allbuttons_` writes stay valid until the G8 sweep |

- Company caption: black strip (U2) at y=171, WHITE text `COMPANY: <name ≤ 18ch>`,
  clipped 28 chars total; no company → `NO COMPANY YET - BEGIN A NEW GAME`.
  DISABLE_MULTIPLAYER variants: same split at y=75; strip at y=166.
- Gating: `continue_game` + `load_company` hidden when zero company files exist;
  per-frame visibility sync + rewire; `begin(0).down→5` (→2 in no-MP) when hidden.
- **Nav (the judge-verified reference graph)**: begin.down→1; continue{up 0, down 5,
  right 11}; **load{up 0, down 4, left 1}** (4 = 2_player at (152,100) — both losing
  proposals mis-wired this to 2 = 4_player; do not repeat); 1_player(5).up→1;
  2_player(4).up→11.
- Continue failure: popup with `save_error_string`, then open Company List. Corrupt
  most-recent company: see §3.5 — Continue never silently opens a different company.
- Hotkeys unchanged. Begin New Game ALWAYS creates a fresh company — the old "restart?"
  prompt (picker_main_menu.cpp:228-232) is RETIRED (nothing is ever destroyed).
- wasm helper: continue tap (150,85) → **(114,85)**.

### 2.2 New-Company name entry (new screen)

```
|               [ FOUND YOUR COMPANY: ]          classic name box (94,70..226,92) |
|               [ IRON KETTLE BAND   ]          DARK_BLUE, left-aligned           |
|            [ REROLL ]        [ ACCEPT ]          (y=102)                        |
|               CLICK THE NAME TO EDIT IT          (text, y=126, GREY)            |
|  [BACK]                                          (10,170,44,20)                 |
```

| idx | id | rect | label | budget |
|---|---|---|---|---|
| 0 | back | (10,170,44,20) | `BACK` | 4/6 — cancel, nothing written |
| 1 | company_name_value | (94,70,132,22) | prompt + current name ≤ 18ch | 18/21 |
| 2 | company_name_reroll | (86,102,68,14) | `REROLL` | 6/10 |
| 3 | company_name_accept | (166,102,68,14) | `ACCEPT` | 6/10 |

- Generator: pure `og::ui::generate_company_name(IRandom&)` in picker_common,
  `<ADJ> <NOUN> <GROUP>` word banks, **hard cap 18 chars** (never truncates on any later
  surface: 18ch fits the Load list name column, the main-menu strip, and the base-camp
  header). Screen opens pre-filled. The box matches character naming/renaming:
  stock-grey 132×22 face, DARK_BLUE prompt at (96,74), and DARK_BLUE editable
  value at (96,82). Click it → `input_string_value(96,82,19,current)`; empty →
  re-generate. Field at y=70+22=92 ≤ 100 — soft-keyboard compliant (U5).
- ACCEPT: writes the company file immediately (creation IS the first autosave), stamps
  `save_name`, then existing flow: campaign intro → base camp. BACK: nothing created.
- Nav: name box{down 2}; reroll{up 1, right 3, down 0}; accept{up 1, left 2, down 0};
  back{up 2}. Initial highlight: ACCEPT. Escape = back.
- Terminals: same screen via TerminalMenuModel prompts (curses `Menu::prompt`, text
  `read_line`) over the same generator + slug helpers.

### 2.3 Company List (Load) — unlimited, paged, delete, backups door

Chassis: the classic `Gladiator: Load Game` grey-panel treatment, centered on
the screen, with each slot divided into company/BK/X buttons and the
VIEW-LEVEL pager precedent inside the footer (final presentation: §9.19).

```
|          GLADIATOR: LOAD GAME           |       centered red title (y=18)       |
|   [       IRON KETTLE BAND       ][BK][X]     row 0  (y=35)                  |
|   ... rows 1..7 (17px pitch)                                                   |
|   [BACK]                  1/2         [PREV] [NEXT]                            |
```

| id | rect | label | budget | action |
|---|---|---|---|---|
| company_row_0..7 | (50,35+17i,164,10) | centered company name | 18/27 | click = OPEN (1-click primary) |
| company_bak_0..7 | (218,35+17i,24,10) | `BK` | 2/2 | Backups sub-view (§2.4) |
| company_del_0..7 | (246,35+17i,24,10) | `X` | 1/2 | **`no_or_yes`** confirm: `DELETE COMPANY <name>?` / `BACKUPS ARE DELETED TOO.` (U3) |
| back | (50,169,40,20) | `BACK` | 4/5 | to main menu; classic slot-menu placement |
| company_page_prev/next | (185,169,40,20)/(230,169,40,20) | `PREV`/`NEXT` | 4/5 | real MenuSpecRow actions, keyboard-live; hidden when ≤ 1 page; `p/N` text centered at (160,175) |

- Row text: the name ≤ 18ch is centered on the first face, as the saved-game
  name was in the old Load Game menu. Roster counts, dates, and column headers
  stay off this intentionally spare screen. When a page has fewer than eight
  companies, its remaining visual rows read `EMPTY SLOT`; all three placeholder
  faces are inert and remain outside the navigation graph.
  Sort: last-played DESC; row 0 == what CONTINUE opens. **The active company's row gets
  red `do_outline`** (U4). Corrupt files list as `CORRUPT` rows (§3.5) with BK (restore
  path) and X (delete; confirm names the backups) still available.
- Listing uses the header-only scanner (§3.5) — **no full `SaveData::load()` per row**
  (load mounts campaigns).
- Deleting the most-recent retargets CONTINUE to the next row; deleting the last company
  returns to a main menu with CONTINUE/LOAD hidden.
- Button count: 8×3 + 3 = 27 ≤ MAX_BUTTONS 50. Nav: rows chain vertically;
  back.down→row0, row0.up→back, row.right→BK→X, row7.down→back;
  back.right→prev→next; initial highlight is BACK, as in the old load loop;
  per-frame rewire + BFS pin over
  {0 rows, partial page, 2 pages, corrupt rows}.

### 2.4 Backups sub-view (per company)

The Backups sub-view retains the original classic 10-row chassis; it is
entered via `BK`, and retention ~20 means at most 2 pages.

| id | rect | content |
|---|---|---|
| backup_row_0..9 | (25,25+15i,220,10) | `L<nn> <level title ≤14ch>` at x=27, `MM-DD HH:MM` at x=151..217; newest first; `BEFORE RESTORE` rows appear here too |
| back / backup_page_prev / next | (25,175,40,20) / (160,175,40,20) / (205,175,40,20) | back → Company List |

Restore flow (UI): click row → **`no_or_yes`** confirm `REWIND TO THIS BACKUP?` /
`CURRENT STATE IS BACKED UP FIRST.` → §3.7's validated restore sequence → on success the
company opens **straight into base camp** (the user's intent after rewinding is to play;
the pre-restore state is itself the newest backup). On failure: error popup, list stays,
state rolled back (§3.7 [SAVE-R3]).

### 2.5 Base camp (reimagined Team Build) — the command roster

Replaces `create_team_menu`'s 3×3 grid. Roster IS the default view (view_team screen
retired; its row-table conventions inherited). SAVE/LOAD buttons removed; saving is
automatic and silent (§3.8). **12 rows/page, 12px pitch, 10px-tall row buttons; ≤ 2
pages** (roster cap 24). Entry keeps fadeblack and the full blocking-loop obligations
(engine-owned: lobby poll, remote-start TeamBuildScope, level-reload guard,
autosave_on_mutation, ready_reset_on_mutation).

The final shipped geometry and line-B content are amended by §9.10–§9.12. In
particular, the networked line-B slot is also the connection-health surface: an
ORANGE `connection_alert` replaces the normal role/census status while the link is
degraded, then yields back to that status when the connection recovers.

```
| IRON KETTLE BAND                                    GOLD 12345    (y=3)        |
| SCEN 7: THE FORTRESS   DEP 8/12              [<] 1/2 [>]          (y=13)       |
| DEPLOY NAME         CLASS    LV  HP    EXP        TRAIN           (hdrs y=24)  |
| [X] # GORT          SOLDIER  12  148   32200     [TRAIN]          (row0 y=32)  |
| [ ] # MERRIN        MAGE      7   66   11050     [TRAIN]          (row2, dim)  |
|  ...                                                              (rows 3..11) |
| [BACK] [HIRE] [SCENARIO] [NETWORK]                  [   GO   ]    (y=178)      |
```

Header (drawn text):
- Line A y=3: company name x=8 ≤ 26ch; `GOLD 999999` right block x=246..312 YELLOW.
- Line B y=13, x=8, ≤ 34ch (scen-hint clip precedent): solo
  `SCEN 7: <title ≤16ch>  DEP 8/12`; networked `READY 2/3  DEP 9/16  SCEN 7`.
  Pager cluster right: `<` (263,11,14,10), `1/2` text at (283,13), `>` (302,11,14,10) —
  hidden when roster ≤ 12. Ready-state changes are encoded **silently** (color revert +
  count drop) — no timed toast (rejected, §8).
- Column headers y=24: solo `DEPLOY` x2, `NAME` x40, `CLASS` x118, `LV` x172, `HP` x196,
  `EXP` x226, `TRAIN` x274. **Networked** (U7 — 16-char origin column; CLASS dropped in
  MP, carried by the family chip + family-colored name, full details one click away in
  TRAIN): `NAME` x40 (≤ 10ch), `COMPANY` x106 (16ch, x106..202), `LV` x208, `HP` x226,
  `TRAIN` x274.

Rows r = 0..11, y = 32+12r (32..164):
| element | rect / x | content |
|---|---|---|
| roster_dep_r (button, own rows) | (8,y,14,10) | `X` deployed / `` benched; 1/1 |
| family chip (drawn) | (26,y,10,10) | draw_box `((family+1)<<4)` |
| name text | x=40 | ≤ 12ch solo / ≤ 10ch MP, family-colored; benched rows draw name+stats GREY(23) — dimming is the second deploy cue |
| class / LV / HP / EXP / COMPANY | per header row above | 3-digit stat overflow not applicable (columns sized for shipped maxima) |
| roster_train_r (button, own rows) | (272,y,40,10) | `TRAIN` 5/5 — opens TrainSession **directly on this character**; PREV/NEXT inside the train screen remain but are **clamped to own characters in networked sessions** (U8) |
| non-owned rows (MP) | no_draw hit zone (8,y,212,10) | dep/train buttons absent; deploy state drawn as `X`/`-` glyph at x=11; click → popup `OWNED BY <full company name>` (U9 — the escape hatch for origin-column prefix collisions); read-only per rights model |

Empty roster: centered ORANGE_START `NO SOLDIERS - HIRE YOUR FIRST` at (73,90);
highlight seeded on HIRE.

Bottom command strip (y=178, 18px tall, ends 196):
| id | rect | label | budget | notes |
|---|---|---|---|---|
| back | (8,178,44,18) | `BACK` | 4/6 | Escape hotkey |
| hire_troops | (58,178,50,18) | `HIRE` | 4/7 | existing HireSession; new hires default deployed=true |
| scenario | (114,178,62,18) | `SCENARIO` | 8/9 | existing subscreen (structure unchanged — TEAMS stays inside SCENARIO; §8) |
| networking | (182,178,56,18) | `NETWORK` | 7/8 | relabel of existing id |
| go / ready | (244,178,68,18) — SAME rect, exactly one visible | §2.6 | | two same-rect buttons keep `interact("go")` and ToggleLobbyReady=68 untouched |

Button count: 24 row + 2 pager + 5 strip + 1 hidden twin = 32 ≤ 50 (worst case with
non-owned hit zones is lower). MP roster rules: merged lobby roster, own machine's
characters sorted first, then by owner player index. Deploy-toggle feedback: label flips
+ row dims same frame; `DEP n/m` updates; **if this machine was ready, ready clears
immediately** (READY face flips green→red same frame — the visible consequence of the
rule); optimistic local flip reconciled by the next lobby poll (server state wins).
Client-side guard: a toggle that would exceed 24 deployed total is denied with a popup
(pre-empting the server overflow path, §4.2).

Keyboard nav (pattern b — full-graph rewire every frame + BFS pin over
{page} × {ownership mix} × {networked} × {host} × {empty roster}): dep/train columns
chain vertically; dep_r.right→train_r; train_r.right→pager if visible; last row.down→
strip; strip chains back↔hire↔scenario↔network↔go/ready; go.up→train_last. Initial
highlight: row 0 dep (roster-first) or HIRE when empty. Page flip keeps the highlighted
row index; `ensure_highlighted_button_visible` after every sync.

Text/curses mapping: TeamBuild model **stays 12 items by in-place substitution**
(minimizes 1-based churn): 1 roster (was view_team), 2 train_team, 3 hire_troops,
4 **deploy** (was load_team), 5 **ready** (was save_team), 6 go, 7 back, 8 networking,
9 scenario, 10-12 ctf trio. Text: `deploy N` toggles, `train N` opens directly; curses
roster list: Enter=train, `d`=toggle deploy, `r`=ready.

### 2.6 GO / READY slot — color-state table

Mechanism: per-frame sync stamps `vbutton::color` (face only; grey bevel edges retained)
and writes labels to BOTH surfaces. Text DARK_BLUE in all states. Pure helper in
picker_common (headlessly unit-tested — U10):

```cpp
enum class ReadyGoState { LocalGo, LocalGoNoDeploy, HostGated, HostGo,
                          ClientUnready, ClientReady };
struct ReadyGoPresentation { ReadyGoState state; std::string label;
                             std::uint8_t face_color; std::string caption; };
ReadyGoPresentation format_ready_go_button(bool networked, bool is_host, bool my_ready,
    bool all_other_machines_ready, int global_deployed, int own_deployed,
    bool cross_control, bool spectator);
```

| # | state | who sees | id | label | face | click |
|---|---|---|---|---|---|---|
| 1 | solo / local multi | host=player | go | `GO` | **13 grey — plain bevel, byte-identical behavior to today** (pinned) | launch (no ready machinery; local LobbyServer exempt) |
| 2 | solo, 0 deployed | | go | `GO` | 13 | popup `DEPLOY AT LEAST ONE` |
| 3 | networked host, gates unmet | host | go | `GO` | 93 (yellow)* | popup listing blockers: `WAITING FOR: <company names>` / `NO ONE IS DEPLOYED`; no start request sent (server gate is the authoritative backstop) |
| 4 | networked host, all ready + global deployed ≥ 1 | host | go | `GO` | 61 (green)* | launch |
| 5 | client, not ready | joiner | ready | `READY` | 45 (shipped red) | set ready (own gates pass, else popup) |
| 6 | client, ready (incl. waiting/all-ready) | joiner | ready | `UNREADY` | 61* | clear ready |

\* faces 61/93 are provisional pending the TESTING contrast screenshot; fallback =
`draw_button_colored` LIGHT_GREEN/RED (U1). Label = the action, color = the state.
Blocked GO stays **visible and clickable** (never hidden — the host must see WHO is
missing; satisfies "state clearly visible" better than hiding, per the HostGoReady
tri-state graft). "Waiting for others" is state 6 + header `READY n/m`; no extra widget.
Client ready gates: cross-control OFF + own roster > 0 + own deployed == 0 → popup
`DEPLOY AT LEAST ONE`; roster == 0 / spectator machines may ready freely (follow mode);
cross-control ON removes the per-machine minimum; global ≥ 1 always applies (host state
3 + server rule 4). The **networked spectator machine (numplayers==0, one lobby seat,
zero character slots) gets the READY button too** — visible, reachable, pinned in the
state-machine tests [NET-R9]. The TEAMS-subscreen READY/UNREADY button
(kTeamsMenuReadyIndex) **remains as a mirror** of the same lobby flag (preserves the
curses lobby 'r'-key semantics and shrinks the TEAMS re-pin blast radius).

**Ready-trap UI contract** (binding on WP5 — U11): (a) v8 joins are ready-preserving —
the server clears a machine's ready ONLY on an actual roster/settings delta (§4.5);
(b) every server-initiated clear is echoed in lobby state so clients render it;
(c) local/solo LobbyServer is exempt — states 1–2 never consult ready; (d) the UI
re-derives the displayed state from server lobby state every frame on BOTH label
surfaces and never trusts a locally-cached flag (the optimistic click flip is
reconciled by the next poll).

### 2.7 Cross-control toggle (host setting, TEAMS subscreen)

Reuses the guy-row slot that is vacant when networked (guy_prev/next/team are
local-only): id `cross_control`, rect (150,146,70,12), labels `CTRL: OWN` / `CTRL: ALL`
(9/10). **Visible to ALL peers when networked** (clients must see a mode that changes
their own rights — resolution of the UX judge's legibility objection, §8); host-only
actionable — non-host click → popup `HOST CONTROLS THIS SETTING`. Label formatter in
picker_common; nav wired by `picker_wire_teams_menu_nav`; the 9-variant BFS matrix
re-pins in the same commit. Toggling it is a settings change ⇒ clears all machines'
ready (§4.5).

### 2.8 Follow mode (in-game) + results share line

Follow camera for machines with 0 deployed or all-deployed-dead: one caption strip,
bottom-center of that viewport — black `fastbox` strip + YELLOW text
`FOLLOWING <name ≤12ch> (<company ≤8ch>)` (≤ 26ch). Cycle binding = the seat's existing
SwitchChar key (Shift = reverse). **No HUD/radar** for AI targets — the follow camera
never stamps user tags (HUD gates on `user() != -1`, score_panel.cpp:404-411; radar on
`user()==mynum`); watching another player's hero shows that hero's vitals via its real
snapshot-synced tag (correct spectator behavior). Mechanics in §4.7.

Results screen (networked wins only): after the existing gold lines, one YELLOW line
`YOUR SHARE: <n> OF <pot>` (0-deploy machines: share 0). Local/solo output
byte-identical (no new line).

### 2.9 Flows (end to end)

1. **New company**: Begin New Game → name screen (generated name) → [REROLL]* →
   optional edit → ACCEPT (file written = first autosave) → campaign intro → base camp.
2. **Continue**: CONTINUE → most-recent company → base camp. Failure → popup → Load
   list. Corrupt newest → popup + Load list with CORRUPT row (§3.5).
3. **Load/delete/backups**: LOAD → list → click row = open; [X] → NO-first confirm →
   gone; [BK] → backups → row → NO-first confirm → validated restore → base camp.
4. **Train**: row TRAIN → TrainSession on that character → back → base camp (autosave
   on accept).
5. **Deploy**: click checkbox → flip + dim + DEP count + autosave (+ MP: own ready
   clears).
6. **MP ready**: joiner readies (red→green), header `READY n/m` everywhere; host GO
   yellow→green on last ready; any roster/settings change flips that machine red.
7. **Hire**: HIRE → existing hire screen → row appears deployed-by-default.

### 2.10 wasm coordinate ledger (computed from the pinned rects; U12)

| helper | old | new |
|---|---|---|
| continue (continueToTeamBuildMenu) | (150,85) | **(114,85)** |
| GO | (250,107) | **(278,187)** (center of 244,178,68,18) |
| networking | (250,150) | **(210,187)** (center of 182,178,56,18) |

Plus: the relay-networking spec must click READY on the joiner before host GO. dist/
rebuild in the same change.

The ledger covers raw main-menu/team-build coordinates in BOTH
tests/e2e/wasm_helpers.js (:212-254) AND tests/e2e/wasm-touch.spec.js —
the touch spec does not route every tap through the helpers: it carries its
own `CONTINUE_BUTTON`/`NETWORKING_BUTTON` consts plus bare
`tapCanvasGameCoord` continue/GO taps, and each must be re-pinned to the
same values whenever a rect in this table moves.

---

## 3. Save v14, company indirection, backups, autosave, migration (FINAL)

Base design adopted; **both critic fatal flaws and all 8 required changes applied**
(markers inline). Verified ground truth: canary pin anchors save_data.cpp **line 118**
(`std::uint8_t temp_version = 9;` — confirmed) — **zero line inserts at or above 118**;
writer stamps `temp_version = 13` at :761 (unpinned); `reset()` untouched (v10+
precedent); `guy::guy(const guy&) = default` propagates new members through
`update_guys`/`merge_owned_guys_from`.

### 3.1 GTL v14 format — reserved-block embedding, no new tails

| Offset | Size | Field | Gate |
|---|---|---|---|
| 3 | 1 | version byte = **14** (writer :761) | — |
| 133 | 8 | `last_played_unix_s` i64 (host-endian raw, per format precedent) | read iff `temp_version >= 14`, else 0 |
| 141 | 23 | reserved, writer zero-fills (was `GTLGTL…` filler) | ignored |
| guy+50 | 1 | `deployed` u8 (0 = held back) | read iff v ≥ 14, else true |
| guy+51 | 7 | reserved, writer zero-fills | ignored |

Rationale: fixed offsets ⇒ the Load list reads only the first 164 bytes; v13 binaries
read v14 files perfectly (both reserved ranges already skipped at :334/:374; no new
tail; no too-new reject). Both fields hard-gated on `temp_version >= 14` (v13 files
carry nonzero `GTL` filler in those ranges — never sniffed). Format comment blocks
(:143-197 reader, :787-841 writer) gain the reinterpretation notes (below line 118 ✓).

New members: `SaveData::last_played_unix_s` (int64, after `tower_run_seed`; NOT cleared
by reset(), stamped ONLY by `company_autosave`); `guy::deployed = true` (after
`owner_save_slot`; sim code must never read it — enforced by a grep-tripwire unit test).
`statscopy` intentionally does NOT copy `deployed` (train/rename must not clobber
deployment).

### 3.2 Wall-clock seam

`og::data::company_clock_now_s()` + `set_company_clock_for_tests(optional<int64>)` in
og_resources (`company.h`). Parity-safe by construction: og_gameplay cannot include
og_resources (dependency direction); `save()`/`load()` only serialize the field; the
parity harness does zero save IO; `SaveData::save()` itself stays deterministic.

### 3.3 `update_guys` held-back preservation + 24-cap rule [SAVE-R4]

Edits entirely inside :607-638 (below pin ✓). **Priority rule (pinned by test):
held-back characters always survive; newly-acquired recruits are dropped first when the
24-cap binds.**
- Pass 0: count previous `team_list` entries with `deployed == false`; reserve that many
  of the 24 slots.
- Pass 1: copy oblist survivors into the remaining capacity (dead kept iff
  `keep_fallen_heroes`); newly-acquired walkers beyond capacity are dropped.
- Pass 2: move-append the held-back entries (their guy objects preserved).
With every guy deployed (all legacy flows) passes 0/2 are empty ⇒ byte-identical legacy
behavior. `merge_owned_guys_from` needs no change (not-brought slots — including their
`deployed=false` — are untouched). Note: pass-2 changes roster ORDER when toggles are
used; WP4 UI must not hold positional slot indices across a win (refresh after
merge/fold — stated rule, pinned by a WP4 flow test).

### 3.4 Active-company indirection

`include/openglad/resources/company.h` + `src/resources/company.cpp`:
- `active_company_slot()` → process-wide slot (Meyers singleton in og_resources — lowest
  common layer for tower_progression/interface/platform callers; compatible with the
  two-GameSession e2e harness). **Default `"save0"`** — every legacy flow, test, demo,
  and wasm-e2e seed is byte-identical until something calls the setter.
- `set_active_company_slot(slot)` validates `is_safe_virtual_basename` && slot !=
  `"netsession"`.
- `ScopedActiveCompany` RAII test guard **plus structural isolation [SAVE-R8]**: a
  fixture/environment-level teardown in `tests/integration_main.cpp` and
  `tests/unit/unit_main.cpp` restores `"save0"` after every test (convention-only RAII
  is the known cfg-clobber failure class under `--gtest_shuffle`).
- **Terminal slot authority [SAVE-R2]**: the text and curses clients call
  `set_active_company_slot` at client init (their config defaults `text_quicksave` /
  `curses_quicksave`), on every save/load slot command, and on `reset_for_new_game` —
  `company_autosave` always targets `active_company_slot()`, so a hire in the text
  client writes the user's chosen slot, never save0. Both clients get tests. The
  interactive script's `~/.openglad/save/text_quicksave.gtl` pin holds.
- `"netsession"` (7 sites) stays literal (server economy scratch). The full 27-site
  `"save0"` replacement table from the base design is adopted verbatim (picker.cpp:241-243,
  :480, :4024, :4053; picker_team_build.cpp:2637, :2665, :2720-2723; screen.cpp:335,
  :349, :1514; input_event_bridge.cpp:37; glad_gameplay.cpp:158, :200;
  local_transport_shadow.cpp:283, :347, :368, :382, :499, :593, :1430, :1668;
  demo.cpp:165, :170; tower_progression.cpp:211, :220, :229;
  web_runtime_diagnostics.cpp:515) — networked ternaries keep their `netsession` left
  arm; `local_transport_shadow.cpp:499`'s default param is removed (both callers pass
  explicitly). `copy_headless_server_save_data` gains
  `dst.last_played_unix_s = src.last_played_unix_s;` (the documented dropped-field bug
  class); `deployed` rides the copied guy objects.

Slug derivation `derive_company_slot(display_name)`: per-char `[A-Za-z0-9]` lowercased;
space/`-`/`_` → `-`; else dropped; collapse/trim `-`; empty → `"company"`; truncate 40;
`netsession` reserved; collision `-2`…`-99` then `-<epoch>`. No dots in generated slugs
(keeps backup-name parsing unambiguous). Display name lives ONLY in the 40-byte
`save_name`.

### 3.5 Header-only scan + startup selection

```cpp
struct CompanyInfo { std::string slot, display_name, campaign_id; short scen_num;
    std::uint32_t totalcash; int roster_size; std::uint8_t version;
    std::int64_t last_played_unix_s; bool valid; };
std::optional<CompanyInfo> read_company_header(const std::string& slot);
std::vector<CompanyInfo>   list_companies();          // most-recent-first
std::string                select_startup_company();  // "" if none
```

- **Full version ladder [SAVE-R1]**: the scanner replicates the reader's prefix ladder
  EXACTLY, version-gated: v2+ name (offset 6); v6+ 32-byte per-team score block; v7+
  registered mark + allied_mode; v8+ campaign (offset 46); then scen_num/totalcash/
  listsize at the version-correct offsets; v14+ timestamp at 133. For v < 8, offsets 86/
  88/130 do NOT hold — the scanner walks the gated sequence rather than fixed offsets
  (fixed-offset fast path allowed for v ≥ 8 only). Fixtures cover **v2/v5/v6/v7/v13/v14**
  plus truncated and bad-magic (⇒ `valid=false`) and missing (⇒ nullopt). Reads ≤ 164
  bytes, never mounts. `get_saved_name` stays untouched (its pins hold; consolidation is
  recorded debt).
- `list_companies`: `og::resources::list_files("save")`, keep `*.gtl`, drop
  `netsession.gtl` and **`*.tmp.gtl`** [SAVE-R6]; std::filesystem fallback when PhysFS
  is uninitialized. Sort: timestamp desc, then `save0` first, then slot asc.
- `select_startup_company`: first entry of the sorted list. **Corrupt-most-recent policy
  [SAVE-R6]: never silently switch companies** — if the newest entry is `valid=false`,
  CONTINUE surfaces a popup (`COMPANY FILE DAMAGED`) and opens the Load list with the
  CORRUPT row marked, offering restore-from-backup (§3.7) or delete (whose NO-first
  confirm explicitly names the backups that will be reaped). Only an explicit user
  action changes the active company.
- **Stray-slot startup behavior [SAVE-R5]**: with save0 absent but other slots present,
  today nothing auto-loads; the new code loads (and mounts) the newest — this is the
  intended feature, but it is a behavior change tests must own: (a) the raw-slot-seeding
  tests (test_picker_funcs.cpp:1997-2039 pattern) gain cleanup via the new
  `remove_user_file`; (b) an explicit test pins the no-save0-with-other-slots startup
  path; (c) a `--gtest_shuffle` sweep of og_test_picker/og_test_menu_ui with pre-seeded
  stray slots runs before the "unchanged regression nets" claim is made.

### 3.6 IO primitives + atomic company writes [SAVE-R6]

`user_file_exists` / `copy_user_file` (read-all → write `<dst>.tmp` → `fs::rename`) /
`remove_user_file` in resources IO (std::filesystem + error_code on `get_user_path()`;
no vendor types in headers). `create_dataopenglad` + the headless twin gain
`save/backups/`. **The primary company write path routes through tmp+rename too**:
`company_autosave` writes `save/<slot>.tmp.gtl` then renames over `save/<slot>.gtl` —
a torn write can no longer produce a header-invalid most-recent company (IndexedDB
snapshots either old or new whole file). `*.tmp.gtl` excluded from listings.

### 3.7 Backups

- Location `save/backups/`, name `<slot>.<SEQ>.gtl` (SEQ zero-padded ≥ 3, parsed as the
  rightmost all-digit dot-token); `next seq = max(existing)+1` derived from the
  directory (rewind can never reuse a seq). No in-file lineage field — each backup is a
  byte copy of a v14 file already carrying timestamp/campaign/scen_num at fixed offsets,
  so the Backups view header-scans backups exactly like companies.
- API: `kCompanyBackupRetention = 20`; `list_company_backups` (seq desc);
  `backup_company_now` (byte-copy + prune lowest seqs to ≤ 20 + `sync_filesystem()`);
  `restore_company_backup`; `delete_company_backup`; `delete_company` (file + ALL
  backups + syncfs; refuses the currently-active slot — the UI enforces "switch first",
  and the API returning false is itself popup-surfaced so a UI slip is visible).
- **Snapshot = byte copy, never re-serialization** (sidesteps `save()`'s
  `current_levels` cursor side effect and `load()`'s mount side effect).
- **Validated restore sequence [SAVE-R3]** (rewind-in-place, pre-restore backup first):
  0. `read_company_header` on the CHOSEN BACKUP file — `valid` required, else abort
     with popup (no state touched). The UI's CORRUPT row marking is not the guard; this
     API-level check is.
  1. `backup_company_now(slot)` — pre-restore state becomes the newest backup (synced).
  2. `copy_user_file(backup, "save/<slot>.gtl")` (tmp+rename).
  3. `save.load_with_error(slot)` — refreshes memory + mounts the restored campaign.
     **On ANY failure: skip step 4, roll back by re-copying the step-1 backup over the
     slot, reload, surface the error.** Both paths pinned in the restore integration
     test.
  4. `company_autosave(save, WindowEvent)` — re-stamps `last_played` so Continue still
     points at this company (a pure byte copy would resurrect the old timestamp).
- Torn-write ordering on emscripten (fire-and-forget syncfs): every destructive step is
  preceded by a persisted copy of what it destroys; worst interruption outcome is "one
  extra backup".

### 3.8 Autosave choke point

```cpp
enum class CompanyAutosaveKind { BaseCampMutation, LevelWin, WindowEvent };
SaveDataIoError company_autosave(SaveData& save, CompanyAutosaveKind kind);
```

Stamps `last_played_unix_s = company_clock_now_s()`; atomic write (§3.6) to
`active_company_slot()`; `LevelWin` additionally triggers `backup_company_now` once per
win. Failures log and return the error; callers surface but don't crash — **EXCEPT the
lobby-entry baseline [SAVE-R7]: `picker_replace_lobby_client` (picker.cpp:466-486) keeps
its hard gate — on a failed baseline write it popups and REFUSES to enter the networked
session, exactly as today.**

**[SAVE-F1] Networked-lobby mutation autosaves are merge writes, never plain saves.**
While a networked lobby is active, the in-memory save has been overwritten by
`apply_lobby_state_to_save` with the HOST's campaign/scen_num/settings; a plain
`save()` would also rewrite `current_levels[campaign]` to the host's level — silently
rewinding the client's solo cursor and settings on disk. Therefore `company_autosave`
detects the networked-lobby context (flag provided by the caller/lobby client) and
routes through an **owner-preserving merge write** (the
`persist_private_campaign_cursor_to_save0` pattern, local_transport_shadow.cpp:356-390):
load the private company file from disk; overlay ONLY (a) the machine's own roster
(team_list entries, incl. `deployed` flags), (b) the owned teams' wallet values
`m_totalcash`/`m_totalscore` (post-hire/train spend) and their legacy scalar mirrors;
PRESERVE from disk: `current_campaign`, `scen_num`, `current_levels`, difficulty, all
ctf_*/respawn_*/tower_* settings, `numplayers`, `my_team`, `allied_mode`; stamp
timestamp; atomic-save. Field-level semantics as above are the contract; pinned by a
test that joins a host on a different campaign/level, trains one character, and asserts
the private cursor/settings are byte-unchanged.

**[SAVE-F2] WindowEvent autosaves are gated off during networked gameplay.** The
minimize/close hook (input_event_bridge.cpp:30-43/:68/:73) must NOT write the company
file while a networked gameplay session is active (the display save holds the combined
netsession-shaped state; screen.cpp:1503-1508 documents this). Gate:
non-networked-session only; a test pins that minimize during a networked level leaves
the company file byte-unchanged (and its timestamp un-promoted).

Hook inventory (all via the G12 engine obligation where the screen is engine-hosted):
HireSession::hire, TrainSession::accept, rename accept, team cycle, deploy-toggle
setter, difficulty/CTF setting callbacks (rule: `picker_lobby_sync_settings_from_save()`
+ `company_autosave(BaseCampMutation)`); level win: local `screen::endgame`
(screen.cpp:1514 — the LevelWin backup trigger), networked
`persist_network_win_to_save0` (once-latched), curses win commit (routed through
`company_autosave(LevelWin)` against the client's active slot); shadow-finalize local
write uses `WindowEvent` (stamp, no second backup). Save/Load buttons + slot menus
retired by WP3/WP4 with their atomic re-pins.

### 3.9 Compatibility matrix

| Direction | Result |
|---|---|
| v14 binary reads v13 (or v2–v12) | version gates: timestamp 0, deployed true; loads clean; first autosave upgrades in place (silent-upgrade precedent) |
| v13 binary reads v14 | bit-perfect tolerant (reserved ranges skipped; no new tail; no too-new reject); a v13 re-save drops timestamp+deploy (defaults restored on next v14 read) |

The indirection is migration-neutral: an upgrader's save0 appears as the most-recent
company named by its `save_name`; legacy manual slots and terminal quicksaves appear as
additional companies. Demo (never sets a slot ⇒ bootstraps save0 unchanged), wasm e2e
seeding (writes the default slot; startup selection finds exactly that file), and the
interactive script (text_quicksave path untouched) are explicitly covered.

### 3.10 Test plan (saves)

Changed pins (same commit as the bump): test_save_data_versions.cpp:491/:686 (13 → 14);
`write_save_file` raw helper gains a v14 branch; fuzz_savefile.cpp replica extended.
Canary: zero inserts ≥ line 118 (verify byte-identity of the line); manual
`scripts/parity/run_mutation_canary.sh` post-implementation. New unit tests
(og_unit_data): slug table; header-scan fixtures v2/v5/v6/v7/v13/v14 + corrupt/missing
(+ assert no mount); list ordering + exclusions; active-slot validation + guards +
main-fixture reset; clock seam; update_guys held-back + **24-cap priority rule**;
company_autosave (stamp, LevelWin backup exactly once, WindowEvent none, failure path,
**networked merge-write field semantics, WindowEvent networked gate**); grep tripwire
(no gameplay file references company.h / last_played). New integration (og_test_io,
under OPENGLAD_CONFIG_DIR — cfg-clobber rule): backup seq monotonicity + no-reuse after
restore; retention prune; **validated restore incl. corrupt-backup abort and step-3
rollback**; delete company + backups; startup selection (two companies; v13-only save0;
**no-save0-with-strays**); atomic-write tmp exclusion. v14 ladder tests in og_test_io
(v13-payload defaults, v14 roundtrip + RAW offset spot-checks at 133/guy+50, reserved
zero-fill, v13-shaped-reader tolerance proxy). Shuffle sweep per [SAVE-R5].

---

## 4. Protocol v8, ready, rights, money split, follow mode (FINAL)

Base design adopted; **all 3 critic fatal flaws and all 9 required changes applied**
(markers inline). Global invariants: local/solo byte-identity (every mechanism
default-off; og_test_parity goldens unchanged); pin discipline (sim_input_handler.cpp
pins at 193/209/340/345/353 — all edits are net-zero-line rewrites confined to lines
124-192; game_server.cpp/lobby_server.cpp/net_transport.cpp/world_snapshot.cpp/
local_transport_shadow.cpp/view.cpp/screen.cpp are unpinned); ownership is never
client-asserted (derived from which peer's `character_slots` a slot sits in).

### 4.1 Wire changes

| Constant | v7 → v8 |
|---|---|
| `kNetworkProtocolVersion` (net_transport.h:72) | 7 → **8** |
| `kSnapshotFormatVersion` (world_snapshot.h:34) | 8 → **9** |
| `kReplayFormatVersion` (replay.h:17) | 9 → **10** |
| `kMinSerializedLobbyCharacterSlotSize` (net_transport.cpp:322) | 60 → **61** |
| `kMinSerializedLobbyPlayerSize` (net_transport.cpp:324) | 13 → **17** |

Literal wire-byte pins updated in the same commit: test_net_transport.cpp:240
(`{0x08,0x01,0x11,0x22}`), :829-830; test_input_state_net.cpp:133, :149. The `+1`
wrong-version tests and constant-based snapshot tests self-adjust.

- **LobbyCharacterSlot**: `bool deployed = true;` on the wire as byte 1 `slot_flags`
  (bit0 = deployed; writer zeroes bits 1-7, reader masks bit0 and ignores the rest).
  Rides the SLOT, not `LobbyCharacterData` — `append/read_serialized_guy` and the three
  `make_lobby_character_data` / `make_guy_from_lobby_character` copy sites stay
  field-identical for guy data (they are still audited + round-trip-tested per spec item
  11; the deployed flag's own copy path is the slot builders:
  picker_lobby_network_client.cpp:1271-1294, picker_lobby_client.cpp:464-503,
  curses_network.cpp:181-212, each stamping from the save guy's v14 `deployed`).
  Deviation from spec 11's anticipated copy-site list recorded in §8.
- **LobbyPlayer**: `std::string company;` (machine's active-company display name,
  identical across the machine's seats; reader clamps 40 chars) serialized after `name`.
- **LobbySettings**: `std::int16_t cross_control = 0;` as the 11th i16 (fixed part
  20 → 22 bytes); `sanitize_settings` clamps {0,1}. Plumbed via the proven 7-step chain
  incl. the `copy_headless_server_save_data` field copy (the documented dropped-field
  trap).
- **LobbyState [NET-R3]**: gains `std::uint8_t last_start_denial` (0 = none; else
  `StartDenialReason`), set on a denied StartGame and echoed to all peers, cleared on
  the next lobby mutation. This exists because dedicated-server lobbies elect the
  first-connected peer as host (server_main.cpp:281-302) — a remote host's GO denial is
  the NORMAL dedicated-server path, not a rare one; the echo lets every client (incl.
  the electing joiner) render the precise reason instead of a poll-timeout.
- **WorldSnapshot v9**: appended after the respawn_mode/generator_rate scalar block at
  all five mirror sites (serialize :807, deserialize :841, capture :2400, apply :2846,
  delta baseline :2977): `u8 control_policy` (0 legacy, 1 owner-locked) +
  `u8[16] player_machine`. **Encoding [NET-F3 support]: low 7 bits = machine id (peer's
  lowest bound global player index), bit7 = "this player's machine deployed ≥ 1
  character this level"; 0xff = no player.** `GameWorld` gains both fields with
  in-header initializers next to `respawn_mode` (game_world.h:398) — zero game_world.cpp
  edits (pins untouched). Defaults are legacy ⇒ parity dumps/goldens byte-identical.
- Changelog comment appended at net_transport.h:64-71.

### 4.2 Deploy flag semantics + 24-cap convergence [NET-F2]

Roster assembly FILTERS `!deployed` slots before densification
(`LobbyServer::build_save_data_equivalent` before the 24-slot throw; joiner mirror
`collect_applied_lobby_slots`) — the in-level roster, InitialSetup, GuySnapshot, and
`merge_owned_guys_from` see only deployed characters (no guy-wire change). The three
`make_guy_from_lobby_character` copies set `guy->deployed = true` explicitly.
`remaining_team_capacity` counts **deployed slots only** against 24; full rosters
(≤ 24/machine) always replicate for display.

**Overflow convergence (replaces bare force-undeploy):** when a Join's deployed total
would exceed 24, the server force-clears `deployed` on the overflow slots in seat order
in its STORED seats and echoes the state. **Clients reconcile: on applying a lobby-state
echo whose own-seat deploy flags differ from local, the client adopts the server's flags
into its session roster (`guy.deployed`) and autosaves via the §3.8 merge path, so the
machine's next join is content-identical** (no perpetual ready-clearing, no permanent
server/client disagreement). UI popup `DEPLOY LIMIT 24 — N BENCHED` (trace-only under
TESTING). The client-side toggle guard (§2.5) makes this path rare. A convergence test
pins: overflow join → echo → reconciliation → next join preserves ready.

Toggle rights are structural (a Join only replaces the sender's seats);
`is_save_slot_editable` flips from "all 24 true" to "own slots only" on networked
clients — **[NET-R5] audit + extend the existing editability consumers before flipping:
tests/unit/test_picker_common.cpp ~:469-1000 lobby-slot/hire/train session tests and any
networked train/hire injector flows are reviewed and re-pinned where they pin the old
behavior.**

### 4.3 Ready system

Ready remains per-machine (all seats of a peer share it). Authority is entirely
server-side; the ready bit clients send inside Join is ignored.

| Event (LobbyServer) | Ready effect |
|---|---|
| `Ready` message | sets all sender seats (unchanged) |
| `Join` re-send with **content-identical** seats | **preserved** (NEW — kills the deadlock trap: go_menu's pre-start join+settings re-send no longer clears anyone) |
| `Join` with seat content changed (roster/team/deploy/stats/seat count) | cleared for that machine |
| `TeamChange` accepted | cleared for that machine |
| `SettingsChange` differing after sanitize | cleared for **all non-host** machines |
| `SettingsChange` echoing identical settings | no effect (go_menu re-sends settings) |
| `unlock_for_new_round` | cleared for **all** machines (forces re-ready each level) |
| Peer disconnect/Leave | seats gone, moot |

Content-identical = same seat count and per seat same `team` + `character_slots` vector
via `LobbyCharacterSlot::operator==` — **[NET-R7] which EXPLICITLY excludes the
assembly-only `owner_player_index`/`owner_save_slot` fields (comment + unit test):** the
in-process loopback transport passes shared LobbyMessage objects with NO serialize/
normalize pass, so "wire-defaulted 0xff on both sides" is not a safe assumption for the
host path. `name`/`company` are excluded (a rename never un-readies anyone).

StartGame gate (`start_allowed(requester, StartDenialReason&)`), in order:
1. `local_session_` → true unconditionally (solo/split-screen/solo-spectator GO exactly
   as today).
2. Existing host-peer check.
3. Every non-host peer with non-empty seats must have all seats ready → else
   `MachinesNotReady`. Single-peer lobby passes vacuously.
4. Total deployed slots across all seats ≥ 1 → else `NoDeployedCharacters` (global ≥ 1
   regardless of cross-control; per-machine minimums are gone — a 0-deploy machine
   follows). **[NET-R9] the go_menu deployed-roster guard rewrite
   (picker_team_build.cpp:2594-2611) implements rule 4, not the old per-machine minimum:
   an all-spectator + one-deployed-machine lobby starts.**

On denial: the lock never engages (`lobby_locked_`/`start_game_requested_` untouched —
the "locked lobby eats messages" hazard never triggers); `last_start_denial_` recorded,
**[NET-R4] cleared on the NEXT StartGame request (not on any processed message — a
queued joiner message between denial and read must not wipe it; pinned by an
interleaving unit test)**; `send_state(peer_id)` echo carries the reason ([NET-R3]).
Host client surfacing: after `poll_and_apply()`, if `!g_start_game_requested`, read the
denial, **clear `start_request_pending_`** (else go_menu's poll loop spins to timeout),
popup listing unready company names. Dedicated-server/remote hosts get the same reason
from the LobbyState echo ([NET-R3]) — tested.

Between levels: host resume calls `unlock_for_new_round()` (clears ready) BEFORE
`sync_from_save()`; the subsequent identical-content join preserves the zeroed state, so
every client re-readies each round. Curses 'r' key unchanged; curses host GO passes
through the same gate. UI presentation: §2.6.

**[NET-R2] Ready-gate test blast radius (landed atomically with the gate):** the 11
existing `request_start_game` sites in tests/test_picker_network_client.cpp (lines 1369,
1555, 2285, 2517, 2769, 3275, 3345, 3552, 4215, 4449, 4740), the StartGame acceptance
tests in tests/unit/test_lobby_server.cpp (:655, :873-891), tests/curses/
test_curses_network.cpp start flows, and test_networking_menu.cpp are audited and gain
`set_ready` steps in the same commit.

### 4.4 Control rights + cross-control

New SDL-free module `og::sim` `sim_control_policy.{h,cpp}` (gameplay sources; outside
the pin map): `control_claim_allowed(world, walker, player_index)`,
`sim_find_next_control_owned(...)`, and
`SimReacquire sim_reacquire_control(...) → {Claimed, Follow, EndGame}`.

Policy (control_policy == 1, i.e. networked && !cross_control):
- Owned walkers (`myguy != nullptr`, owner tag set): claimable iff
  `machine_of(owner) == machine_of(player)` (same-machine seats free among their
  machine's characters).
- **Unowned walkers (scenario troops, `myguy == nullptr`, `user() == -1`) [NET-F3]:
  claimable in the death-reacquire and switch-cycle paths by any player whose machine
  deployed ≥ 1 character this level (the `player_machine` bit7 flag, §4.1); 0-deploy
  machines remain follow-only (their bind-time seats bind null).** This kills the
  endgame-less softlock: previously-denied ownerless troops would leave every seat in
  Follow forever with EndGame unreachable while the troop lives. Spec 6 restricts
  control of OTHER PLAYERS' characters — it does not require denying ownerless troops.
  Pinned by a dedicated test (all owned heroes dead, allied troop alive → deployed
  machine claims it and can finish the level; 0-deploy machine stays following).
- control_policy == 0 (local/solo/cross-control-ON): allow everywhere ⇒ all four sites
  behave exactly like v7.

Enforcement at exactly the four server-side selectors (walker selection is already
server-authoritative; ControlChange is broadcast-only):
1. SwitchChar cycle filter (sim_input_handler.cpp:183-192) — lambda gains the
   conjunction; net-zero-line rewrite.
2. Death auto-switch / entry claim (sim_input_handler.cpp:144-158) — body becomes
   `sim_reacquire_control` + verdict switch (15 lines exactly): `Follow` ⇒
   `control == nullptr`, no endgame_requested, ControlChange entity 0 broadcast as
   today; `EndGame` ⇒ today's endgame result.
   **[NET-R1] Corrected rationale + test obligation:** `has_living_member_for_any_bound_team`
   (game_server.cpp:701-740) tracks ALL bound teams including the requester's own, so
   today's wipe suppression DOES fire in allied — there is no pre-existing spurious-wipe
   bug being fixed here; the Follow verdict is justified by the ownership policy alone.
   The §4.8 equivalence tests must pin the allied claimed-teammates-alive case:
   policy-off EndGame verdict + existing suppression reproduces today's exact observable
   behavior (ControlChange 0, no world ending, seat continues null).
3. Bind-time claim (`GameServer::bind_player`, :1146-1224) — under owner-locked policy
   the claim scan requires `control_claim_allowed`; when nothing qualifies, bind with
   `control = nullptr` (follow seat) instead of the pool claim. CTF keeps own-hero
   preference first in both modes. This also fixes the spectator pool-claim/AI-freeze
   risk (a 0-deploy machine's seats bind null; watched heroes' AI keeps running).
4. Per-level rebind funnels through bind_player → inherits (3). Reconnect and the
   CTF/classic reclaim-by-user-tag are ownership-neutral (same entity/tag) — untouched.

Pin strategy (sim_input_handler.cpp): all edits are net-zero-line rewrites confined to
lines 124-192 (above the first pin at 193); mechanical verification: `git diff`
line-delta == 0 for the file; lines 193/209/340/345/353 byte-identical; canary lint
pass; full og_test_parity (policy off in every scenario ⇒ goldens byte-identical).
`set_control_policy(owner_locked, machines)` called from the network host install,
curses host install, and dedicated server; local installs never call it.

### 4.5 Follow mode

Client-side camera only (the server's contribution is the null seat + ControlChange 0).

- `DisplayFollowState { engaged, target_entity_id }` per view in the local transport
  runtime. Engagement (inside `sync_display_controls`): networked session AND (machine
  has no gameplay seats OR the view's seat maps to entity 0 with no corpse retention).
  Clears when the seat's mapped id goes nonzero.
- **[NET-F1] The legacy spectator SwitchChar block (view.cpp:1344-1381, gated on
  numplayers==0) consumes the edge and RETURNS before the shadow guard — it is gated
  OFF when the networked shadow is installed**; the new follow branch (placed before it)
  owns the input for networked machines and writes `DisplayFollowState`, so the choice
  survives the per-snapshot control re-sync instead of being stomped within the frame.
  The legacy block remains for non-networked spectator (demo/local) unchanged. Test:
  a networked numplayers==0 machine cycles targets and the choice survives a forced full
  snapshot resync.
- Stomp survival: `select_control_for_view` gains a `const DisplayFollowState*` checked
  BEFORE the mapped-entity branch: engaged + live target ⇒ return it — the user-tag
  force-stamp at local_transport_shadow.cpp:139-142 never executes for a follow target
  (no-stamp holds structurally). Dead/unresolved target ⇒ auto-advance; none ⇒ nullptr
  (static-camera fallback as today).
- Cycle input: follow branch on SwitchChar press-edge (Shift reverse) →
  `local_transport_shadow_cycle_follow_target(mynum, reverse)` walking the mirror oblist
  with `sim_cycle_next_character` under the filter `!dead && !dormant && Order::Living
  && (user() != -1 || myguy != nullptr)`, fallback any living. The same key still rides
  the InputMessage; the server ignores it for null seats (harmless dual consumption).
- Default target: lowest global player index with nonzero controlled entity, else first
  eligible.
- HUD/radar: never stamped locally ⇒ AI targets show the spectator-minimal HUD, radar
  quiet; a followed foreign hero wears its owner's snapshot-synced tag (its vitals show
  — correct). `FOLLOWING <name>` caption via a `viewscreen::following_` flag
  (RESPAWN-IN strip mechanism). test_glad_hud pins extended, not violated.
  **[NET-R6] Follow tests assert "select_control_for_view performed no LOCAL stamp"
  (tag unchanged across the sync) — NOT a blanket `user() == -1`**, because snapshots
  legitimately carry walker user tags for player heroes (world_snapshot.cpp:2141/2173
  capture, 1626/1651 apply).
- Curses: `followed_entity_id()` gains the same follow state; Tab cycles;
  "(following NAME)" replaces "(spectating)". Text client: N/A (no live input path) —
  documented, not a gap.

### 4.6 Money split

- **Edit 1 — fold delta out-params**: `WinFoldContext` gains
  `applied_cash_delta[4]`/`applied_score_delta[4]`, filled inside `apply_win_fold` steps
  2-3 before `m_score` zeroing. Fold effects unchanged (netsession/session saves keep
  combined totals; local byte-identity pins hold; idempotent second pass reports delta 0
  — new assertion; host's double fold fine, each fold fills its own context).
- `NetWinFoldCapture { cash_delta[4], score_delta[4], deployed: vector<{owner, team}> }`
  — the deployed roster copied **PRE-fold** from the session `team_list` (immutable
  during a level ⇒ pre-fold == deploy-time). Producers: host
  `finalize_level_and_advance_cursor` (copy at ~:546, fold :547, persist :563); client
  `screen::endgame` latches `std::optional<NetWinFoldCapture>
  pending_net_win_capture_` around the fold at screen.cpp:1494-1502 — visible to BOTH
  `persist_client_win_progress_once` trigger shapes and the retry latch; re-armed per
  level.
- **Share algorithm** (pure, `og::progression::compute_networked_win_shares` in
  src/resources/win_shares.cpp): per team t, contributors = players with ≥ 1 deployed
  character on t; `share_i = delta_t * count_i / total` (u64, floor); remainder → the
  largest contributor, tie-break **lowest player index** (spec-locked); machine bank =
  Σ over own player indices; conservation Σ == delta is a unit-test invariant.
  Determinism inputs (m_score snapshot-synced; time bonus from the synced tick via the
  shared helper; session first-win flag seeded by InitialSetup) each traced to a synced
  source. **[NET-R8] plus a cross-machine capture-roster determinism test: host-vs-client
  `NetWinFoldCapture.deployed` equality across a two-level dedicated/in-session-
  transition flow** (level-2 rosters derive from update_guys-pruned lists on both sides;
  the conservation unit test alone cannot catch an asymmetric prune).
- **Edit 2 — persist-time application**: `persist_network_win_to_save0` overlay becomes
  **baseline + share** (disk save0 baseline + this machine's deploy-ratio share; legacy
  scalar mirrors follow the merged primary-team values). Local/solo persists untouched —
  whole-pot byte-identical. Core extracted as SDL-free
  `og::progression::persist_networked_win` (shared with curses, headlessly
  unit-testable); the SDL wrapper keeps slot handling via the §3 active-company API.
- **Curses gap FIXED**: curses network client (`commit_result_to_save` no-op at
  curses_network.cpp:687) implements fold + capture + `persist_networked_win` into the
  machine's own company save, using the shared deterministic time-bonus helper
  (`calculate_headless_time_bonus` exposed from og_resources); curses host's
  company-save write switches to the share path while its in-memory session save keeps
  the full fold. Curses solo keeps its deliberate zero-bonus whole-pot fold
  (byte-identity).
- **Results screen**: renders pre-fold, so the prospective delta is computed on the
  spot; networked machines see `YOUR SHARE: <n> OF <pot>`; local byte-identical.
  In-game score HUD untouched (split exists only at persist).
- E2E pins rewritten BY DESIGN (not worked around): the seven-player equal-delta trio
  becomes per-machine expected shares + a conservation assertion; the
  absolute-overlay tests become baseline+share; spectator-zero and withdraw-no-cash
  tests gain completion-credit assertions.

### 4.7 Completion credit

A machine with 0 deployed characters at deploy time gets no money (share 0 falls out)
and **no completion credit**; the campaign cursor still advances (existing deliberate
spectator behavior). Gate `own_deployed_count > 0` around the `add_level_completed`
block in `persist_networked_win`; cursor writes unconditional; withdraw untouched;
curses inherits via the shared helper.

### 4.8 Test plan (protocol)

Unit: net_transport (5 literal pins → 0x08; slot-flags/company/cross_control/
denial-field roundtrips incl. reserved-bit tolerance + 40-char clamp; crafted-count
rejection against 61/17; lobby_message_variants refresh incl. LeaveMessage);
lobby_server (ready preservation/clears per the §4.3 table; identical-settings no-op;
start denial paths + lobby-still-live; **denial cleared on next StartGame with an
interleaved join [NET-R4]**; deployed-only capacity + **overflow reconciliation
convergence [NET-F2]**; assembly filter; local_session exemption; unlock clears ready;
operator== owner-field exclusion [NET-R7]); progression (out-deltas, second-pass zero,
existing fold pins byte-identical); win_shares (floor/remainder/tie/conservation/
overflow/zero-deploy/total==0); sim_control_policy (policy-off ≡ legacy over crafted
worlds; owner-locked allow/deny incl. same-machine multi-seat; **unowned-troop claim by
deployed machines, denied for 0-deploy [NET-F3]**; Follow vs EndGame verdicts incl. the
**allied claimed-teammates-alive equivalence [NET-R1]**); world_snapshot v9 roundtrip +
delta carry + bit7 encoding. Integration (in-process transports, set_game_speed(0.0f) +
g_test_remove_exits): the deadlock regression (joiner readies → host go_menu with its
re-sync succeeds); GO denied → lobby live; roster change forces re-ready;
between-levels re-ready; shares + conservation; zero-deploy no-money/no-completion/
cursor-advanced; cross-control OFF/ON control flows; **[NET-R2] the full
request_start_game blast-radius sweep**; **[NET-R9] spectator-machine READY flow**;
follow-mode engage/cycle/stomp-survival/**no-LOCAL-stamp [NET-R6]**/all-dead-no-endgame/
caption/curses; **[NET-F1] spectator-block gating**; **[NET-R8] two-level capture
determinism**; curses share persists; **[NET-R5] editability-consumer audit**. Gates:
ci-test; og_test_parity; manual mutation canary + pinned-line byte check; coverage
delta; wasm relay spec clicks READY before GO + dist rebuild.

---

## 5. Risk register (ALL survey risks → design answer)

**Menus** —
1. Index contracts / k*Index: engine uses `index_of` with drift pins (G13); Layer-F
   reshapes update constants + kExpected tables atomically (§7); G8 sweeps the raw
   writes in the same commits.
2. Text 1-based consumers: Layer E leaves menu_model shape untouched; Layer F holds
   TeamBuild at 12 items by substitution and appends `load_company` at the END of Main;
   both consumers re-pinned atomically (§7) with a TESTING helper that prints the
   current 1-based list to make re-pins mechanical.
3. Raw `allbuttons_[N]` magic indices: become OutlineBinding/LabelBinding/art bindings
   at Layer E; remaining do_call-side writes swept per screen (G8).
4. Four mainmenu variants / dual OPTIONS_BUTTON_INDEX: one spec × build gates + 2 nav
   columns; both `#define`s deleted; noMP/web materialization unit-pinned (§1.6, G9).
5. ButtonAction append-only / retvalue-as-action-id / MENU_EXIT bit collision: exactly
   one new value (MenuSpecRow=101) with the mandatory retvalue-zero discipline (G3);
   retired holes stay reserved.
6. Dual label surfaces: the runner's single label-sync pass writes both surfaces every
   frame; a dedicated engine test mutates the save under an open screen.
7. Nav/hidden/keyboard-dead: RowState + rewire precedence + TESTING invariant + BFS
   lattice (G13); all engine rows keyboard-live by construction (101 ≠ 0).
8. Pixie face re-apply + w/h overwrite: engine re-applies art bindings after
   init_buttons AND every reset_buttons; pinned by an engine test.
9. Three SaveData backing stores: NOT unified (recorded debt); `MenuLabelContext` is a
   per-client borrow bundle; resolver golden-equivalence tests cover the mapping.
10. Remote-start re-implemented per loop: engine-owned obligation + the G5
    fires-from-every-screen generator test; legacy stragglers tracked by the
    loop-obligations checklist (G4).
11. og_test_menu_ui 130s + injector rules: new engine tests in og_test_menu_engine /
    og_unit_families; Layer-F flows in og_test_basecamp; injector rules (750ms/300ms
    delays, id-based interact, geometry-disambiguated `back`) restated in §2 tests.
12. Canary pins in grown files: zero pins in any picker/menu/button file (verified);
    save/sim files handled in §3/§4 pin disciplines.
13. Coverage: logic lives in pure picker_common/menu_binding helpers (cheap units);
    legacy loops deleted in the same commit as each migration (no orphaned lines).

**Saves** —
1. save_data.cpp:118 pin: all reader edits below 118; byte-identity verified per commit.
2. Writer-version pins :491/:686: updated with the bump + v14 ladder tests.
3. load() mounts campaigns: header-only scanner for ALL listings (companies + backups).
4. save() rewrites the cursor: backups are byte copies; restore re-stamps after a fresh
   load (no-op cursor); networked autosaves merge-write ([SAVE-F1]).
5. Filename constraints: slug derivation §3.4; display name confined to save_name.
6. copy_headless_server_save_data drops new fields: `last_played_unix_s` copy added;
   deployed rides guy objects.
7. 27 save0 literals: full replacement table §3.4; netsession ternaries preserved.
8. demo/wasm-e2e/interactive-script pins: default `"save0"` + terminal slot authority
   keep all three byte-identical (§3.9).
9. Emscripten torn writes: tmp+rename everywhere + copy-before-destroy ordering; worst
   case "one extra backup" (§3.6/§3.7).
10. og_open_write cwd fallback: all new integration tests run under
    OPENGLAD_CONFIG_DIR (cfg-clobber rule).
11. No delete API: `remove_user_file` added; emscripten deletes followed by syncfs.
12. No timestamp infra: v14 serialized field + the clock seam (no PHYSFS_stat plumbing).
13. ~15 networking tests share save0: default preserved ⇒ untouched; new tests use
    ScopedActiveCompany + the main-fixture reset ([SAVE-R8]).
14. Old-reads-new tolerance / no too-new reject: deliberately preserved (both-direction
    tolerance); latent v15-width hazard recorded as accepted debt.
15. Minimize/close autosave: targets the active company, gated off during networked
    gameplay ([SAVE-F2]), merge-write in networked lobbies ([SAVE-F1]).

**Lobby** —
1. v8 hard cut: accepted (no negotiation); all peers must upgrade; changelog comment.
2. Literal pins: exact file:line list in §4.1; drifted "~2696" pin explicitly not
   hunted.
3. Join-resets-ready + go_menu re-send: killed by server-side content-identical
   preservation + identical-settings no-op; end-to-end regression test.
4. StartGame ignores ready / local LobbyServer shared: `local_session_` exemption is
   rule 1 of the gate.
5. Owner fields not on the wire: ownership stays derived from `character_slots`
   membership; operator== excludes the assembly fields ([NET-R7]).
6. Min-size divisors: 60→61 / 13→17 updated with crafted-count rejection tests.
7. 5+ copy sites: enumerated in §4.1; deployed deliberately rides the slot (fewer copy
   sites); all sites round-trip-tested.
8. Asymmetric resume (curses tears down): curses keeps its flow; its ready path goes
   through the same server gate; divergence documented (persistent-resume port is out
   of scope).
9. picker_lobby_poll dormant in gameplay: no in-game lobby UI is added; follow mode is
   pure client camera.
10. Locked lobby eats messages: denials never engage the lock (§4.3).
11. Team-keyed wallets vs 16 players: shares computed per player then banked into the
    4-team arrays; no player-indexed wallet arrays introduced; fold idempotence
    preserved with delta out-params.
12. sim_find_next_control third pass bypasses UI rights: enforcement is in the sim
    selectors themselves (§4.4), not the UI.
13. LobbyLeaveMessage: untouched; roundtrip tests keep referencing it.
14. sim pin map: §4.4 net-zero-line discipline + mechanical verification.

**Money** —
1. Pinned pot duplication: rewritten by design (per-machine shares + conservation).
2. Absolute-overlay pin: rewritten to baseline+share.
3. Fold delta unrecoverable: captured at fold time via out-params.
4. Pre-fold roster: NetWinFoldCapture copied before the fold; determinism test
   [NET-R8].
5. Fold shared/idempotent/double-fold: effects unchanged; second-pass delta 0 asserted.
6. Two persist trigger shapes + retry latch: capture latched on `screen`, visible to
   both.
7. CTF rematch folds cash: split applies only at networked persist; rematch gating
   untouched.
8. Time bonus cross-context: shared og_resources helper used by all executors.
9. v8 wire pins: §4.1.
10. Parity/canary: zero sim-file inserts; persist-time-only split.
11. Results overstates: replaced by the share line (§2.8).
12. Curses no-persist: FIXED via the shared helper (explicit decision).
13. Between-round purchasing power: intended consequence of the split; base-camp
    economy trades against the machine's own banked share.
14. Cross-machine determinism: inputs traced to synced sources + the two-level
    determinism test; a future reconciliation hash recorded as follow-up (not in v8).
15. Legacy scalar mirrors: kept consistent in the persist overlay and v14 (§4.6).

**Control** —
1. sim_input_handler pins: net-zero-line rewrites confined above the first pin;
   byte-check + canary lint.
2. Wire pins on version bump: §4.1.
3. sync_display_controls stomp: DisplayFollowState honored inside
   select_control_for_view (structural).
4. Spectator binds/pool-claims + AI freeze: owner-locked bind_player binds null for
   0-deploy seats; watched heroes' AI runs.
5. Shared-pool stealing is deliberate today: preserved under policy-off and
   cross-control ON; the named test files are in the [NET-R2]/§4.8 audit.
6. HUD/user-tag stamping: follow never stamps locally; tests assert no-LOCAL-stamp
   ([NET-R6]); test_glad_hud extended.
7. cheat_handler mutates the mirror: untouched by this feature (no signature changes).
8. find_next_control used by demo/replay: signature unchanged; policy read from world
   scalars defaulting to legacy.
9. Per-level rebind reshuffle: funnels through the policy-aware bind_player.
10. resume_in_dead_state / reclaim-by-tag: ownership-neutral, untouched.

**Tests** —
1. Exact geometry pins: Layer E unchanged (10a proof); Layer F re-pins atomically (§7)
   incl. the TEAMS 9-variant matrix and picker_sdl_defs constants.
2. Canary line+text anchoring, no CI validation: per-file insert disciplines (§3.1,
   §4.4) + manual canary run in WP7; kMut_save_corrupt stays orphaned but un-staled.
3. v8 literal-byte tests: §4.1; new message fields get serialize/deserialize pins
   (coverage).
4. 1-based text consumers + literal banners: shapes preserved at Layer E; substitution
   strategy + atomic re-pins at Layer F.
5. wasm raw coordinates: ledger §2.10 + dist rebuild; READY step added to the relay
   spec.
6. Razor-thin coverage: pure helpers carry the new logic; judge by local delta; wipe
   stale .gcda; tests written alongside in every WP.
7. Parity save-format-agnostic but defaults matter: no sim-relevant default changes;
   new world scalars default legacy; no scenario-id renames.
8. og_test_menu_ui slowest: G10 placement rules; new groups og_test_menu_engine +
   og_test_basecamp.
9. save0 overwrite + mount side-effects under shuffle: PickerSaveStateGuard patterns
   revisited in WP7; [SAVE-R5] stray-slot shuffle sweep; main-fixture slot reset
   ([SAVE-R8]).
10. 12-item TeamBuild pinned in three places + curses suite: substitution keeps the
    count at 12; the four surfaces re-pin in one commit (§7).
11. Cheap networking e2e pattern: all new ready/money/follow flows reuse
    local_transport_shadow + set_game_speed(0.0f) + g_test_remove_exits.

---

## 6. Implementation plan — work packages

Ordering: WP1 ∥ WP2 first (independent, both invisible); WP2 before WP3/WP4/WP5;
WP5 before WP4's MP-only parts and before WP6; WP7 threads through every WP and closes
at the end; WP8 last.

### WP1 — Menu engine (Layer E, invisible)
- **Files**: new menu_binding.{h,cpp}, terminal_menu_model.{h,cpp},
  menu_screen_spec.h, menu_screen_runner.cpp, menu_screen_specs.cpp,
  docs/menu-engine.md; modified picker.cpp (k_* tables deleted per migrated screen),
  picker_main_menu.cpp, picker_team_build.cpp, picker_state.{h,cpp},
  text_picker.cpp, curses_picker_client.cpp, picker_ui_state.h, CMakeLists.txt (4
  SDL-free source lists + og_test_menu_engine group).
- **Tests added**: G2 pin-then-migrate kExpected pre-commits; tests/unit/
  test_menu_spec.cpp; tests/test_menu_engine.cpp (incl. G5 remote-start generator, G6
  PageModel-vs-VIEW-LEVEL oracle, retvalue discipline, dual-surface, art re-apply,
  gate-lattice sweep). **Tests re-pinned: NONE.**
- **Ordering**: migration steps 0-7 of §1.8, each independently green.
- **Done-criterion (spec item 10a, verbatim)**: every existing menu test passes
  UNCHANGED against the engine-rendered legacy menus (no re-pins in WP1 — re-pins
  happen only in WPs that intentionally change screens). Concretely: all 28
  test_menu_layout pins, test_menu_model, all injector flow suites, the 53-test curses
  suite, test_platform_headless:553-630, the interactive script, and the four wasm
  specs pass without modification.

### WP2 — Save v14 + company layer (invisible on its own)
- **Files**: save_data.{h,cpp} (below-118 discipline), guy.h, company.{h,cpp} (new),
  io_common.h + platform_io_common.cpp (+ primitives), platform_io.cpp /
  platform_headless.cpp (backups dir), picker_common.cpp (autosave hooks),
  input_event_bridge.cpp, local_transport_shadow.cpp, glad_gameplay.cpp, screen.cpp,
  tower_progression.cpp, demo.cpp, web_runtime_diagnostics.cpp, curses_game_runtime.cpp,
  headless_server_runtime.cpp, text/curses slot-authority sites, integration_main.cpp +
  unit_main.cpp (slot reset), CMakeLists.txt.
- **Tests**: §3.10 in full (re-pins: test_save_data_versions :491/:686; added: the
  unit/integration lists incl. all [SAVE-*] pins; fuzz replica).
- **Ordering**: lands before WP3/WP4/WP5; every intermediate commit green (default slot
  ⇒ invisible).
- **Done**: v14 roundtrip + ladder green; canary pin byte-identical; demo/wasm-e2e/
  interactive script pass unchanged; [SAVE-F1]/[SAVE-F2] tests green; shuffle sweep
  clean.

### WP3 — Main menu + Load/Backups/Name-entry UI (Layer F begins)
- **Files**: menu_screen_specs.cpp (Main reshape + 3 new screens), menu_model.cpp
  (Main + new PickerMenuIds), picker_common.cpp (generate_company_name,
  derive-slug UI glue, formatters), button.h (+MenuSpecRow=101) + button.cpp (do_call
  case), text/curses handlers, wasm_helpers.js + dist.
- **Tests re-pinned (atomic per screen)**: test_menu_layout main-menu tables (4
  variants) + nav; test_menu_model Main (+load_company appended); both 1-based text
  consumers; wasm continue coordinate; injector flows (test_new_game,
  test_back_to_mainmenu, test_save_menu → company flows). **Added**: name-generator
  units (length/charset/coverage); company-list/backups layout pins + BFS variants +
  flow tests (og_test_basecamp); corrupt-row flows.
- **Ordering**: after WP1+WP2. G8 raw-index sweep in the same commit as the main-menu
  reshape.
- **Done**: flows §2.9 items 1-3 pass; delete/restore round-trips pinned; retired
  restart prompt gone; full gate green.

### WP4 — Base camp
- **Files**: menu_screen_specs.cpp (BaseCamp RowTemplate spec replacing TeamBuild),
  menu_model.cpp (12-item substitution), picker_common.cpp (deploy toggle setter,
  format_ready_go_button, TrainSession seed-slot + MP clamp), picker_team_build.cpp
  (legacy grid deleted), TEAMS spec (+cross_control row), text/curses roster flows.
- **Tests re-pinned**: createmenu → basecamp kExpected + nav BFS matrix
  (page × ownership × networked × host × empty); kCreateMenu*Index; TEAMS 9-variant
  matrix (+cross_control); test_menu_model TeamBuild substitution; both 1-based
  consumers; wasm GO/networking coordinates + dist; curses picker suite flows;
  test_ctf_ui touched flows. **Added**: format_ready_go_button state table (incl.
  spectator [NET-R9]); deploy-toggle autosave + ready-clear flow; ownership popup;
  positional-index refresh after win (§3.3 note).
- **Ordering**: solo parts after WP1+WP2; MP columns/READY wiring after WP5.
- **Done**: roster-first base camp on all three clients; no Save/Load buttons;
  per-row Train seeds correctly; full gate green.

### WP5 — MP protocol v8
- **Files**: §4.1 wire files, lobby_server.{h,cpp}, picker_lobby_network_client.cpp,
  picker_lobby_client.cpp, curses_network.cpp, glad_gameplay.cpp,
  headless_server_runtime.cpp, server_main.cpp, progression.{h,cpp}, win_shares.cpp
  (new), local_transport_shadow.cpp (capture/persist), screen.cpp (capture latch),
  results_screen.cpp, curses_game_runtime.cpp.
- **Tests**: §4.8 unit + integration lists; wire-byte re-pins; [NET-R2] blast-radius
  sweep; money-pin rewrites; [NET-F2] convergence; [NET-R3]/[NET-R4] denial tests.
- **Ordering**: after WP2 (deployed flag source); before WP4-MP and WP6. The version
  bump + all wire-byte re-pins land in ONE commit.
- **Done**: deadlock regression test green; shares conserve; curses persists;
  dedicated-server denial surfaced; parity goldens byte-identical.

### WP6 — In-game control rights + follow mode
- **Files**: sim_control_policy.{h,cpp} (new), sim_input_handler.cpp (net-zero-line),
  game_server.cpp, game_world.h, world_snapshot.cpp, replay.h,
  local_transport_shadow.cpp (follow state + cycle), view.cpp ([NET-F1] gating +
  follow branch), score_panel.cpp (caption), curses follow files.
- **Tests**: sim_control_policy equivalence suite (incl. [NET-R1] allied case,
  [NET-F3] troop rule); snapshot v9; follow integration ([NET-F1], [NET-R6], stomp,
  all-dead, caption, curses); test_glad_hud extension.
- **Ordering**: after WP5. Mechanical pin verification (line-delta 0, pinned lines
  byte-identical) in every commit touching sim_input_handler.cpp.
- **Done**: og_test_parity byte-identical; owner-locked flows pass; follow works on
  SDL + curses.

### WP7 — Test sweep & gates
- **Files**: tests only + scripts.
- **Work**: manual `scripts/parity/run_mutation_canary.sh` (genuine-toothless must stay
  0) + pin-map audit for save_data.cpp/sim_input_handler.cpp/walker.cpp/gloader.cpp;
  [SAVE-R5] stray-slot `--gtest_shuffle` sweeps; PickerSaveStateGuard/mount-restore
  revisit; coverage delta measurement (wipe stale .gcda; local baseline→change delta,
  ≥ 90/95); ci-asan run; wasm e2e full pass (4 specs + relay READY step) + dist
  verification; 30-seed shuffle sweep of the new suites.
- **Ordering**: threaded through every WP; final closing pass after WP6.
- **Done**: ci-test, ci-asan, og_test_parity, coverage, wasm e2e all green; canary
  audit recorded in the PR.

### WP8 — Demo assets + PR
- **Files**: dist/ rebuild, demo bootstrap verification (unchanged behavior), docs
  (this file + docs/menu-engine.md finalized + ARCHITECTURE.md networking note for v8),
  PR body.
- **Ordering**: last.
- **Done**: `./scripts/build_web.sh` clean; demo boots a fresh company flow; PR opened
  with the §7 checklist attached and every checklist row checked.

---

## 7. Atomic re-pin checklist

Every row lands in the SAME commit as the change that invalidates it; no intermediate
red commits.

1. **Menu layout pins** (tests/test_menu_layout.cpp): main-menu 4-variant tables +
   nav; createmenu → basecamp kExpected + BFS variant matrix; company-list/backups
   pins; TEAMS 9-variant matrix (+cross_control); save/load slot tests retired with
   the slot menus; picker_sdl_defs.h k*Index constants updated/retired with their
   screens; G8 raw `allbuttons_[N]` sweep in the same commits.
2. **test_menu_model**: Main (+load_company appended item), TeamBuild 12-item
   substitution, new PickerMenuId resolution cases.
3. **Text 1-based consumers**: scripts/test_text_picker_interactive.sh (drive sequence,
   index comments, literal banners, text_quicksave path — path itself unchanged) and
   tests/unit/test_platform_headless.cpp:553-630 (+ :649-680); use the TESTING
   print-the-1-based-list helper to make re-pins mechanical.
4. **Curses suite**: tests/curses/test_curses_picker_client.cpp flows over the reshaped
   Main/TeamBuild; curses network 'r'-key + share-persist tests.
5. **wasm coordinate helpers**: tests/e2e/wasm_helpers.js:212-254 — continue (114,85),
   GO (278,187), networking (210,187); tests/e2e/wasm-touch.spec.js carries its own raw
   copies (CONTINUE_BUTTON/NETWORKING_BUTTON consts + bare tapCanvasGameCoord taps) and
   re-pins to the same values; relay spec gains the joiner READY click;
   **dist/ rebuilt in the same change**.
6. **Wire-byte tests**: test_net_transport.cpp:240, :829-830;
   test_input_state_net.cpp:133, :149; min-size constants 61/17; snapshot v9 / replay
   v10 constant tests; all in the single version-bump commit.
7. **Save version pins**: test_save_data_versions.cpp:491, :686 (→ 14) + the v14
   ladder additions + fuzz replica, in the format-bump commit.
8. **Canary pin-map audit**: `tests/parity/scenario_table.h` is the source of truth.
   Before merge, verify byte-identity of `save_data.cpp:118`;
   `walker_movement.cpp:174`; `walker.cpp:487/1192/1354/2262`;
   `gloader.cpp:467/473/475/483/672`; `game_world.cpp:1634/1636/1724`; and the
   family anchors added by the WP7 triage (`family_druid.cpp:51`,
   `family_archer.cpp:127`, `family_mage.cpp:198`, `family_slime.cpp:125/139`,
   `treasure_family_navigation.cpp:85`) alongside the remaining table entries.
   `sim_input_handler.cpp` and `input_state.cpp` are now deliberately pin-free: their
   former mutations were runtime-dead and were retargeted to the lines above. Run the
   mutation canary manually and record the result in the PR (the canary is NOT in CI
   — silence is not success).
9. **Money pins**: test_picker_network_client.cpp seven-player trio → shares +
   conservation; test_save_load_team.cpp:602-682 → baseline+share; spectator/withdraw
   completion-credit assertions; [NET-R2] request_start_game sweep — all in the WP5
   gate commit.

---

## 8. Conflict resolutions & recorded rejections

Panel winners: ARCH = MenuScreen Runtime (2/3); UX = roster-first 320×200 (2/3). All
grafts from all six judges folded in; resolutions where they conflicted:

1. **ButtonAction shape**: the ARCH winner's 13 named actions (101-113) are REPLACED by
   the single generic `MenuSpecRow = 101` (judge-2 graft) with the judge-3
   retvalue-zero discipline; the GO/READY slot uses the UX winner's two same-rect
   buttons (existing Go action + ToggleLobbyReady=68) so no ReadyOrGo action is needed
   and `interact("go")` survives.
2. **Ready-clear feedback**: the timed 3-second toast (one judge's graft) is REJECTED
   in favor of another judge's silent state encoding (color revert + READY n/m count
   drop) — wall-clock timing in menu label pins is a known flake vector on the shared
   CI machine and the engine has no toast facility. The server still echoes every clear
   (clients must render it); the information loss is covered by the host's denial
   popup naming unready companies.
3. **Host GO when gated**: protocol design's "red/dim" and the tri-state
   "Disabled" graft are unified as the UX winner's state 3: GO visible, yellow face,
   clickable, popup naming blockers, no start request; the server gate remains the
   authoritative backstop. Never hidden.
4. **State face colors**: unshipped faces 61/93 kept provisionally WITH the mandatory
   one-frame TESTING contrast screenshot; shipped `draw_button_colored`
   LIGHT_GREEN/RED is the pre-agreed fallback. Labels stay DARK_BLUE everywhere
   (WHITE-on-RED per-state label channel REJECTED — button.cpp hard-codes DARK_BLUE in
   every vdisplay path; new engine surface for no need).
5. **MP COMPANY column**: widened from the UX winner's 8ch to 16ch (graft) by dropping
   CLASS in MP (family chip + family-colored name carry it; TRAIN one click away) —
   re-derived column layout in §2.5; the OWNED-BY popup remains the collision escape
   hatch.
6. **Cross-control visibility**: the UX winner hid it from joiners; resolved to
   visible-to-all / host-only-actionable with an explanatory popup (a mode that changes
   a client's own rights must be visible to that client).
7. **TEAMS structure**: the touch-first proposal's TEAMS-door promotion and
   READY-removal are REJECTED — TEAMS stays inside SCENARIO and keeps its READY mirror
   (curses 'r'-key parity, smaller re-pin blast radius).
8. **Restore destination**: restore opens the company straight into base camp (UX
   winner) rather than staying in the list — the pre-restore snapshot makes it safe and
   it matches the dominant rewind-and-play intent.
9. **Deployed flag location**: kept on `LobbyCharacterSlot` (protocol design) rather
   than inside the serialized guy payload that spec item 11's copy-site list
   anticipated — fewer copy sites, guy wire untouched; spec 11's listed sites are still
   audited and round-trip-tested, so the item's intent (no silently-zeroed field on one
   client) is honored.
10. **Backup-name parsing vs dotted filenames**: generated slugs contain no dots; the
    backup SEQ parser takes the rightmost all-digit dot-token so legacy dotted names
    still parse.
11. **Critic required_changes**: ALL applied — none rejected. Save: F1/F2 + R1-R8;
    Protocol: F1/F2/F3 + R1-R9 (markers inline in §3/§4). Where a critic offered
    alternatives, the chosen arm is recorded: [SAVE-R6] tmp+rename (not
    truncate-in-place justification); [NET-F2] client-side reconciliation (not
    comparison-exclusion); [NET-R3] denial reason on the LobbyState wire now (not the
    degraded-UX acceptance).
12. **Open follow-ups deliberately out of scope**: get_saved_name/scanner
    consolidation (debt); cross-machine wallet reconciliation hash; curses
    persistent-connection resume; the three-client SaveData store unification;
    progress-menu keyboard-dead pagers (visible change, needs its own pin round);
    endianness of the i64 timestamp (matches format precedent).
13. **Terminal-client [SAVE-R2] deviations (recorded, WP7)**: the text and curses
    clients keep a single explicit slot authority (`config_.save_name`), so on those
    surfaces (a) BEGIN NEW GAME persists **in place** to the client's own slot — the
    §2.1 "always creates a fresh company / nothing is ever destroyed" guarantee is an
    SDL/wasm behavior; the prior contents of the terminal slot are replaced (the
    terminals therefore print NO `file: <slug>.gtl` preview — the SDL slug file is
    never created there); and (b) CONTINUE opens the terminal's own slot, not the
    most-recent company (equal only in the single-company case). Rationale: [SAVE-R2]
    slot authority predates the company layer and terminal users address slots
    explicitly; silently multiplying files under a fixed-slot CLI contract would be
    the surprising behavior.
14. **Accepted WP7 residue (known quirks, not merge blockers)**:
    - A follow camera can retain a foreign hero's corpse through its revive window,
      then re-enter follow on the default target rather than the previously selected
      one (E1). Fixing it changes null-seat spectator semantics and needs a dedicated
      follow-flow re-pin pass.
    - The radar's `controlob->user() == mynum` gate retains a pre-existing
      global/local-seat alias that follow mode can now expose (E2). A correct fix
      requires plumbing the view's global seat into that gate.
    - Local multi-view counts benched members when deciding which team views exist;
      an only-benched team can therefore receive a view with nothing to control (E5).
    - In allied network play, the between-level GOLD label reads the session-team
      wallet while the machine's awarded share is banked in team 0, so a joiner's
      freshly banked share may not be visible in that label until later (E6).
    - Networked spectators may open the local preferences menu (E8). This is a
      client-local side effect and does not grant simulation authority.
    - The new-company name-entry screen intentionally has no lobby-poll or
      remote-start obligation (E9): a joiner parked there observes a host launch only
      after leaving the screen. The engine contract records the same exception.

---

## 9. UX-pass amendment (2026-07-21) — playtest feedback F1–F11 [FINAL, judged]

Merged verdict over the two vision-pass proposals (before-captures:
scratchpad/uxshots/before/). **Winner: proposal B** (the G-series screen spec)
with two grafts from proposal A — (a) benched-text shade 21, (b) padded
fixed-width numerics — and one A arm rejected (recorded in §9.9). This section
AMENDS §2.0–§2.5 and is the normative spec for the UX pass; where it is silent,
the §2.x tables stand. Behavioral contracts (GO/READY §2.6, ready system §4.3,
autosave §3.8, wasm ledger §2.10, all flows §2.9) are FROZEN — this pass is
visual/layout plus the §9.6 platform fix only.

### 9.1 Global — button label centering (F5) [amends §2.0 bullet 1]

src/interface/ui/button.cpp, ALL FIVE vdisplay centering copies (:188-189,
:198-200, :218-220, :231-233, :243-245):

    start_x = (xloc+xend)/2 - ((label.size()-1)*(letters->w+1))/2;   // BEFORE
    start_x = (xloc+xend)/2 - (label.size()*(letters->w+1)-1)/2;     // AFTER

True ink width of N chars is N*(w+1)−1 (6N−1 for the 5px font); the old
formula counts N−1 cells, so every label sits ~2–3px right of center and a
1-char glyph ("X", "<", ">") hugs the right half of a 14px face. Render-only;
no test pins rendered label x (verified). At-budget labels keep ≥4px margins
under the corrected formula. Recapture every screen.

### 9.2 Main menu (F1, F10) [amends §2.1]

1. NEW row appended at the END of BOTH variant tables (append-only rule):
   - id `no_company_note`, rect **(80,75,140,20)** — the exact envelope of the
     CONTINUE|LOAD pair, same x/w as `begin_new_game` above it.
   - label `NO COMPANY YET` (14 ≤ budget 22), engine-drawn DARK_BLUE on the
     Disabled face — the requested greyed-out disabled-button look.
   - action `MenuSpecRow` (arg = own ordinal; never dispatched — inert).
   - `.state_override`: companies present → `RowState::Hidden`; absent →
     `RowState::Disabled` (engine grammar: face GREY(23), bevel intact,
     myfun/myfunc zeroed = keyboard-dead, click no-ops with the
     `menu_engine/disabled_row_click` TRACE).
   - nav: all links −1; NOTHING links into it in either variant — keyboard
     flows keep today's hidden-variant routing (begin.down→5 MP / →2 no-MP).
     The note is inert chrome: it is EXEMPT from any every-visible-button-
     reachable assertion (add a Disabled-exemption arm to the helper if one
     asserts it).
   - Ordinals: MP variant **12**, no-MP variant **7**.
2. Caption strip: with-company state UNCHANGED (`COMPANY: <name ≤18>` strip at
   y=171 MP / y=166 no-MP). No-company state: the bottom caption
   `NO COMPANY YET - BEGIN A NEW GAME` is REMOVED (draw nothing) — the note
   box replaces it.
3. Terminals: NO change (no menu-model item; the note is SDL-only chrome).
4. wasm ledger UNCHANGED — (114,85) taps the note only in the no-company
   state, where it is a no-op by design; specs seed a company first.

### 9.3 Name entry (F2, F10) [amends §2.2]

1. DELETE the slug preview EVERYWHERE: the GREY y=126 line in
   `name_entry_draw_content` (menu_screen_specs.cpp ~:2450-2453), the
   text_picker.cpp:146 banner line, the curses_picker_client.cpp:930 prompt
   label — and DELETE `og::ui::format_company_file_preview` outright (no
   consumer left; coverage hygiene). §2.2's "slug preview teaches the split"
   sentence is retired.
2. Name strip becomes a sunken input field: `company_name_value` face set to
   PURE_BLACK via ColorBinding/rewire stamp on BOTH surfaces (§2.6 mechanism —
   face only; grey bevel edges remain → reads as an inset DOS text field).
   The name stays content-pass WHITE `write_xy_center(160,82)`; the button
   label stays empty (no §8.4 conflict). Fixes the recorded white-on-face-13
   contrast fail (146 vs 166).
3. NEW hint in the freed slot: U2 black-strip text centered at (160,126):
   `CLICK THE NAME TO EDIT IT` (25 ch, ink 149px; strip (160−ink/2−2, 125,
   ink+4, 8) PURE_BLACK alpha 150), text GREY(23) — the secondary voice.
   Fixer latitude: may be dropped if the captured sunken field already reads
   as editable; nothing replaces it.
4. Title/REROLL/ACCEPT/BACK rects, nav, generator, flows: UNCHANGED.

### 9.4 Company list (F10) [amends §2.3; §2.4 Backups inherits §9.1 only]

1. Header line y=16 de-collided: `NAME` x=27 (unchanged); `GUYS` x=**129**
   (right edge aligns with the 2-ch count column at 141..152, 3px clear of
   PLAYED); `PLAYED` x=155 (unchanged); `BK` x=**223** (renamed from BKUP —
   matches the button label, centered over the BK button 219..239); `DEL`
   x=**245** (centered over the X button 243..263).
2. Active-row legibility: keep the U4 red `do_outline` face; draw the active
   row's CONTENT columns (name/count/date) WHITE instead of DARK_BLUE.
   Recorded reading: content-pass row text is not a vdisplay button label, so
   the §8.4 "labels stay DARK_BLUE" rejection does not apply.
3. Rows, rects, pager, nav, dispatch, sort: UNCHANGED (the X glyph centers via
   §9.1). The 2-digit-count/date 2px kerning is period-typical — keep.

### 9.5 Base camp (F4, F6–F9, F11, F10) [amends §2.5]. ONE atomic commit.

#### 9.5.1 Row grid — F9, the §2.0 U6 pre-declared fallback: 10 rows/page, 15px pitch
- picker_sdl_defs.h: `kBaseCampRosterRowsPerPage` 12→**10**, `kBaseCampRowY0`
  32→**31**, `kBaseCampRowPitch` 12→**15**, `kBaseCampTrainBase` 12→**10**,
  `kBaseCampPagePrevIndex` 24→**20**, `kBaseCampPageNextIndex` 25→**21**,
  `kCreateMenuBackIndex` 26→**22**, `kCreateMenuHireIndex` 27→**23**,
  `kCreateMenuScenarioIndex` 28→**24**, `kCreateMenuNetworkingIndex` 29→**25**,
  `kCreateMenuGoIndex` 30→**26**, `kCreateMenuReadyIndex` 31→**27**,
  `kCreateMenuButtonCount` 32→**28**. Sweep EVERY consumer (grep the
  constants; raw ordinals also live in direct-dispatch tests).
- `roster_dep_r` (8, 31+15r, 14, 10), r=0..9 (ordinals 0..9); `roster_train_r`
  (**266**, 31+15r, **46**, 10), ordinals 10..19 (`TRAIN` 5 ≤ budget 6 — real
  slack at last). Row text glyphs at y+2 (centers the 6px font in the 10px
  band). MP foreign no_draw hit zone (8, 31+15r, **212**, 10).
- Pager rects/indicator and the y=178 command strip UNCHANGED ⇒ the §2.10
  wasm ledger is untouched.
- Capacity: cap-24 roster = **≤ 3 pages** (supersedes §2.5's "≤ 2 pages");
  the 40-slot defensive pin becomes 4 pages (ceil(40/10)).

#### 9.5.2 Readability panel — replaces the per-row 150-alpha bars
- `base_camp_draw_background`: header bar (4,21,312,8) PURE_BLACK **alpha
  200**, then ONE roster panel (4,29,312,15·visible−2) PURE_BLACK alpha 200
  (visible ≥ 1; full page = y 29..176, 1px above the strip). Row-gap air
  stays INSIDE the panel — calm DOS-panel look, shield art ghosts through.
  Lines A/B keep their 150-alpha strips. Fixer check: eyeball the capture;
  **180 is the sanctioned alpha floor** — record the number.

#### 9.5.3 Columns — F11 (HP column REMOVED, all clients) + graft (b)
- Header line y=24→**22** (ink 22..27, above the panel seam).
- SOLO headers: `DEPLOY` x=2, `NAME` x=40, `CLASS` x=118, `LV` x=174,
  `EXP` x=196, `TRAIN` x=274. NO HP.
- SOLO values: name x=40 (≤12ch), class x=118 (≤8ch), level x=174,
  exp x=196 (ink ≤231, 35px clear of TRAIN).
- MP headers: `NAME` x=40, `COMPANY` x=106, `LV` x=208, `TRAIN` x=274.
  MP values: name x=40 (≤10ch), company x=106 (≤16ch), level x=208; foreign
  X/− glyph x=11 unchanged. NO HP.
- **Graft (b), padded numerics**: the shared formatters left-pad numeric
  fields to fixed width — level `%2u`, exp `%6u` (within the existing clip
  budgets) — so digit columns right-align down the page on all three clients
  (space advances 6px like every glyph).
- picker_common: `BaseCampRowText`/`BaseCampNetRowText` drop `.hp`;
  `format_base_camp_row(guy)` drops the `derived_hp` param;
  `format_base_camp_net_row(name, company, level)`; the base-camp draw drops
  its `picker_compute_guy_derived_stats` call. Rationale (verified): the
  column is DERIVED max HP — damage never persists to base camp — redundant
  with CLASS+LVL; EXP STAYS (progress-to-next-level is actionable); HP lives
  one click away in TRAIN. Text/curses roster views drop the HP field from
  their row joins.

#### 9.5.4 Uniform name color — F6/F7/F8 + graft (a)
- DELETE the family-color branch (menu_screen_specs.cpp ~:2158-2167 + the
  networked arm): `name_color = deployed ? WHITE : 21`; the CLASS column and
  stats use the same deployed/benched pair (WHITE 24 / **shade 21**). MP
  names follow the same rule. Removes the palette[16] black-top "clipped"
  defect (F6) and the inverted benched cue. Graft rationale (measured): the
  glyph shade-ramp of GREY(23) text (~121..170) overlaps WHITE(24)
  (~134..182) by all but one step — benched at 23 is barely dimmer than
  deployed; 21 (~97..146) restores a legible two-step drop while staying
  "benched-grey dimming only" per the user decision.

#### 9.5.5 Family chip — the F6-hazard companion (only family cue in MP)
- Framed chip: `fastbox(26, y, 10, 10, GREY 23)` then
  `fastbox(27, y+1, 8, 8, ((family+1)<<4))`. The 1px grey frame makes the
  soldier pure-black chip and the near-black slime runs read as deliberate
  dark chips. Same chip deployed and benched (dimming stays a text cue).

#### 9.5.6 Empty state — F4, framed panel in the PRE-buttons pass
- `base_camp_draw_background`, visible==0 branch (draw-order-safe — the
  content-fills-over-buttons trap cannot bite): fill
  `draw_rect_filled(60, 82, 200, 16, PURE_BLACK, 200)`; border = four 1px
  GREY(23) fills (60,81,200,1) (60,98,200,1) (59,82,1,16) (260,82,1,16).
- `base_camp_draw_content` empty branch: `write_xy_center(160, 87,
  ORANGE_START, "NO SOLDIERS - HIRE YOUR FIRST")` — 29 ch, ink 173px
  (74..247), ~14px inside the panel — replaces the bare write_xy(73,90) over
  raw art (U2 satisfied by the panel). HIRE-seeded highlight unchanged.

#### 9.5.7 Nav / MP / terminals
- Nav pattern (b) unchanged in shape; ordinals shift per §9.5.1 (dep_r.right→
  train_r; train_r.right→pager; last row.down→strip; go.up→train_last).
- Injector flows address rows by id (`roster_dep_1`, `roster_train_0`, …) —
  unchanged. Text/curses TeamBuild item numbering (§2.5 12-item substitution)
  UNCHANGED — only their roster ROW STRINGS change (HP drop + padding).

### 9.6 WebGL context-loss recovery (F3) — platform-only commit, no visuals

1. web/shell.html: keep `e.preventDefault()` in 'webglcontextlost'; replace
   the alert with the page's status-text machinery ("RESTORING GRAPHICS…") +
   a ~10s watchdog that surfaces a reload prompt if restoration never fires
   (Safari may never restore; IDBFS autosave makes reload lossless). Add
   'webglcontextrestored' → guarded
   `Module._openglad_web_notify_context_restored()` (the shell.html:627-632
   call pattern).
2. New KEEPALIVE C export `openglad_web_notify_context_restored()` next to
   `openglad_web_restore_canvas_backing` (src/platform/sdl/glad.cpp:482-486):
   sets a pending-recreate flag in the video layer ONLY (no GL work in the
   callback — ASYNCIFY re-entrancy: html5 callbacks can run while the C stack
   is suspended in a blocking menu loop).
3. Recreate at the top of `Screen::swap` (src/platform/sdl/sai2x.cpp:1512,
   the single production present site): `destroy_render2()` +
   `destroy_gameplay_ui_overlay()` (lazy, self-recreating) → destroy
   world_tex_ (if != ui_tex_), ui_tex_, renderer → `SDL_CreateRenderer` with
   ctor parity (sai2x.cpp:800-805) → ui_tex_ ctor parity (:818-827: ARGB8888
   STREAMING, BLENDMODE_NONE, SCALEMODE_NEAREST) →
   `apply_world_scale_from_cfg()` (video_sdl.cpp:173, transactional world
   rebuild) → `redrawme=1`. Content self-heals: swap re-uploads from CPU
   surfaces every present. While lost, GL calls are silently ignored (no
   exceptions — the -fexceptions dead-runtime trap does not apply).
4. e2e: Playwright spec via `WEBGL_lose_context` loseContext()/
   restoreContext(); assert repaint + no alert dialog. After the dist build,
   grep dist/*.js for the export (archive-DCE trap). Expect the shared-
   machine jitter flake class on the new spec.

### 9.7 Re-pin blast radius (each row lands in the SAME commit as its change)

| change | re-pins |
|---|---|
| §9.1 centering | none (no test pins rendered label x) — screenshot verify only |
| §9.2 note row | test_menu_pins kExpectedMainMenu all 4 variants (+1 row; last-row relative pins shift: note=count−1, load=count−2, options=count−3); test_menu_engine main-menu shape + company gate/rewire test (Disabled face/keyboard-dead/TRACE when absent, Hidden when present, no inbound nav links in either state); reachability helper gains a Disabled-exemption arm if it asserts every-visible-reachable; wasm ledger + 1-based text consumers UNCHANGED |
| §9.3 slug removal | tests/unit/test_picker_common.cpp slug/format pins DELETED with the helper; tests/unit/test_platform_headless.cpp name-entry banner assertions; curses prompt-label pins; grep scripts/test_text_picker_interactive.sh for slug/"file:" assertions; name-entry layout table only if it pins color bindings |
| §9.4 headers + active-row white | company-list draw-smoke header-string assertions (check test_menu_engine); no structural pins |
| §9.5 base camp | picker_sdl_defs.h constants + full consumer sweep; test_menu_layout createmenu_basecamp exact table (28 rows) + solo BFS matrix (roster {0,5,12,15,24} — 12 now PAGES) + networked ownership matrix (page counts at 10/page) + ready-twin exact-rect pins (ordinals 26/27); test_menu_pins static asserts; test_menu_engine row_count + gate-lattice same-geometry pins ({go,ready}); direct-dispatch raw-ordinal tests (win-fold, mp-columns, ready-twin, debounce, stale-click: dep r=r, train r=10+r, pagers 20/21); U7 clip-budget unit pins (hp pin deleted; 10/16/3 survive) + padded-numeric format pins; text/curses roster-row join pins (HP drop + padding); verify no pin asserts benched GREY(23) — re-pin to 21 if one does; injector flows by id unchanged; wasm: verify no dep-row coordinate exists (ledger says none) ⇒ NO ledger change |
| §9.6 context restore | new Playwright context-loss spec; dist/ rebuilt in the same change; grep dist for the KEEPALIVE export |
| all | canary pin-map audit before merge (no pinned sim file is touched — button.cpp/menu_screen_specs.cpp/video are interface/platform; run the wp7 pin-audit script anyway) |

### 9.8 Commit plan (each lands green on the full nix gate; probe rig stays uncommitted)

1. §9.1 centering — recapture all screens.
2. §9.2 main-menu note box + caption removal + pins.
3. §9.3 name entry (slug removal all clients + sunken field + hint) + pins.
4. §9.4 company-list headers + active-row white text.
5. §9.5 base-camp mega-commit + this section's §2.5 supersessions.
6. §9.6 context-loss recovery + e2e + dist rebuild.

AFTER captures per commit into scratchpad/uxshots/after/ via the uncommitted
probe (settle ≥1500ms + double-capture; the torn-capture and stale-save0
traps are recorded in the WPUX state file).

### 9.9 Judgment record

1. **Winner**: proposal B — concrete normative rect/color tables for every
   screen, verified against source anchors and the before-captures, zero
   wasm-ledger churn.
2. **Graft (a)** from proposal A: benched text shade **21**, not GREY(23) —
   palette-measured one-step ramp overlap made 23-vs-WHITE dimming nearly
   invisible. Still "benched-grey dimming only" per F7/F8.
3. **Graft (b)** from proposal A: padded fixed-width right-aligned numerics
   in the shared formatters (LV `%2u`, EXP `%6u`) — table discipline on all
   three clients for free.
4. **REJECTED** (proposal A): terminals keeping a filename/slug hint — the
   user said "remove entirely"; companies are fully managed in-game on every
   client, the filename teaches nothing. `format_company_file_preview` is
   deleted, not orphaned.
5. Empty-state treatment: B's framed panel over A's bare strip (stronger F4
   fix, still draw-order-safe in the pre-buttons pass).
6. F11 (orchestrator addendum) folded in as §9.5.3: HP column removed on all
   clients; EXP retained.
7. Copy choice: the note box reads `NO COMPANY YET` (14ch) — the "BEGIN A NEW
   GAME" half of the old caption is carried by the art button directly above.
8. Latitude recorded: panel alpha 200 floor 180; the §9.3 hint may be
   dropped; the Disabled note face stays engine GREY(23) — darkening it would
   need a new engine channel (not sanctioned).

### 9.10 UX round 2 (2026-07-21) — G1–G3: header/roster regrid + company label [stage regrid-label; FINAL geometry]

Round-2 playtest feedback on the §9.5 base camp: the screen still reads
crowded (G1: the roster block needs clear margins above and below; the
bottom strip is fine as-is), the company and scenario lines sit too close
(G2), and the company name is unlabeled (G3 — label PREPENDED, chosen over
appending "Company" because generated names often end in group nouns). This
subsection AMENDS §9.5.1–§9.5.3 (and through them §2.5); everything it is
silent on — row pitch, row heights, TRAIN/dep x-geometry, panel alphas,
colors, columns, the §9.5.6 empty-state panel, the y=178 strip, all
behavioral contracts — stands as previously specified.

#### 9.10.1 Vertical rhythm (G1) — 9 rows/page, block margins

With 10 rows at the (kept) 15px pitch the full-page panel ended at y=176,
1px off the command strip, and no top margin existed either; margins are
IMPOSSIBLE at 10 rows. Round 2 trades the tenth row for the air:

| element | round 1 (§9.5) | round 2 (FINAL) |
|---|---|---|
| line A (company + GOLD) | y=3 | y=3 (unchanged; content per §9.10.3) |
| line B (scen / READY-DEP) | y=13 | **y=17**; pager `<` **(263,15,14,10)**, indicator strip (283,**17**), `>` **(302,15,14,10)** |
| header bar | (4,21,312,8) | **(4,31,312,8)** |
| column headers | y=22 | **y=32** (ink 32..37, above the panel seam) |
| roster rows | 10 @ y=31+15r | **9 @ y=41+15r** (rows 41..170; kBaseCampRowY0=41) |
| roster panel | (4,29,312,15·v−2), full 29..176 | (4,**39**,312,15·v−2), full page **39..171** |
| panel → strip gap | 1px | **6px** (172..177) |
| command strip | y=178 (frozen) | unchanged |
| §9.5.6 empty panel | (60,82,200,16) | unchanged (band 81..98 sits inside the roster region) |

Margins achieved: pager-cluster bottom (24) → block top (31) = **6px** (was
0–1px); full-page panel bottom (171) → strip (178) = **6px** (was 1px).

Spec ordinals (append-only exception, sanctioned restructure — same class
as §9.5.1): dep **0..8**, train **9..17**, pagers **18/19**, back **20**,
hire **21**, scenario **22**, networking **23**, go **24**, ready **25**,
count **26**. `roster_dep_9`/`roster_train_9` ids are RETIRED (no test or
flow referenced them — verified). Capacity: cap-24 roster = **3 pages**
(unchanged, ceil(24/9)); 13 members = 2 pages, page 2 first index **9**;
the 40-slot defensive shape = **5 pages**; 25 display slots = 3 pages
(unchanged).

#### 9.10.2 Line spacing (G2)

Line B moves y=13 → **y=17**: baseline delta A→B grows 10 → **14px** (ink
gap 4 → 8px — doubled). The pager cluster rides along (y=15, ends y=24,
feeding the 6px top margin above the header bar at 31).

#### 9.10.3 Company label (G3)

Line A becomes: grey `COMPANY:` at x=8 (GREY 23, the secondary voice) +
WHITE name at x=**62** (8 label chars + 1 space at 6px/char), on ONE shared
150-alpha backing strip `(6, 2, (9+len(name))·6+4, 8)` — two strip_text
calls would leave a raw-backdrop seam between label and name. Budget
(checked vs the GOLD block sharing the line): name keeps the 26-char clip →
ink ends ≤ x=217, **27px clear** of the GOLD strip at x=244 (GOLD block
(246,3) unchanged, 11-char clip). Terminals: NO change — G3 is SDL line-A
chrome; the text/curses pickers carry their own context banners and the
TeamBuild item numbering is untouched.

#### 9.10.4 Frozen-contract + ledger check

- wasm §2.10 ledger UNCHANGED: it carries only continue/GO/networking
  coordinates; no roster-row or pager coordinate exists in
  tests/e2e/wasm_helpers.js or wasm-touch.spec.js (verified by grep). The
  moved row rects become ledger-relevant only when a row click becomes an
  action (the G4 stage owns that re-check).
- GO/READY/deploy/ownership semantics untouched; the READY twin still
  shares GO's exact rect (ordinal 27 → **25**).
- Text/curses 1-based TeamBuild numbering untouched (no model change).

#### 9.10.5 Re-pin blast radius (landed in the SAME commit)

| change | re-pins |
|---|---|
| picker_sdl_defs.h constants (9/9/18/19/20-25/26) | consumer sweep is automatic (all src+test dispatch sites use the constants — verified); test_menu_layout G13 drift pins re-pinned to the new ordinals |
| static table reshape (drop row 9, pager y=15, strip nav .up 9→8 / +9→+8) | test_menu_layout createmenu_basecamp exact table (26 rows, y=41+15r, pagers (263/302,15)), count asserts 28→26 |
| BFS matrices | shape-derived (roster {0,5,12,15,24} and (own,foreign) pairs recompute pages from the constant); comments re-pointed (12 still pages, 24 spans 3, 40 slots = 5 pages) |
| page-window pins | test_view_team win-fold: page-2 first_index 10→9; 25-slot 3-page pin unchanged; test_picker_common display_slots_page_defensively_past_24: PageModel::make(40, **9**) ⇒ **5** pages |
| draw geometry (header bar 31, headers y=32, line B 17, panel 39, line-A label) | no pixel-coordinate pins exist (empty-state smoke is coordinate-free — verified); screenshot-verified via the §9.8 probe workflow |
| docs | menu-engine.md registry row (9 pairs, READY row 25); this subsection |

### 9.11 UX round 2 (2026-07-21) — G4: row-click trains, TRAIN column deleted [stage row-click-train; FINAL geometry]

USER-CHOSEN from a preview panel: the per-row TRAIN button column is
DELETED; clicking a character's ROW (the name/class/level area) opens that
character's Train screen — the same interaction grammar as the company
list. Deploy stays the explicit left toggle. This subsection AMENDS
§9.5.1/§9.5.3/§9.5.7 (and through them §2.5); G4 changes the train
AFFORDANCE only — TrainSession, the U8 networked PREV/NEXT clamp,
seek_slot, ownership/editability, GO/READY, deploy, and autosave semantics
are all FROZEN and untouched.

#### 9.11.1 Row-body hit zone (SDL) — FINAL geometry

- The 9 `roster_train_r` rows become `roster_row_r`: rect
  **(26, 41+15r, 208, 10)**, label **empty**, `ButtonAction::MenuSpecRow`,
  **same ordinals 9..17** (`kBaseCampTrainBase` renamed
  `kBaseCampRowBodyBase`, value 9 — pagers 18/19, strip 20..25, count 26
  all UNCHANGED; the §9.10.1 ordinal table stands).
- Edges: left **26** = the family chip's left edge, a 4px gutter from the
  deploy toggle ending at x=22 (the G4 touch rule: the deploy hit zone and
  the row-click zone MUST NOT overlap — pinned by an exact-table assert);
  right **234** = the solo EXP ink end (231) + 3px, covering chip + name +
  class + LV + EXP ink. MP values (name/company/LV, ink ≤ 226) sit inside
  the same zone.
- **Face decision (RECORDED): `no_draw = true`**, not a quiet bevel face.
  The row text IS the affordance; a bevel would double-frame every row
  against the §9.5.2 panel. The keyboard `draw_highlight` pulse draws from
  the button RECT regardless of no_draw, so the row highlight reads as a
  full-row outline — the §2.5 foreign-hit-zone grammar, now on own rows
  too. (Mouse hover highlight is skipped for no_draw buttons — same as the
  foreign zones; accepted.)
- The TRAIN column header (x=274) is deleted from BOTH header arms (solo
  and networked). No other column moves.

#### 9.11.2 Dispatch, ownership, keyboard, touch

- Dispatch: ordinals 9..17 open the train screen seeded on the row's save
  slot (the §2.5 flow-4 path verbatim — `picker_set_train_seed_slot` +
  nested `create_train_menu`; remote-start propagation and the
  stale-click/vacated-slot guards unchanged).
- Ownership maps over: FOREIGN rows hide their row-body zone (exactly as
  they hid TRAIN); the §2.5 widened dep hit zone (8,y,212,10 — geometry
  UNCHANGED) remains the foreign row click and pops OWNED BY. A directly
  dispatched foreign row-body ordinal still resolves to the OWNED-BY popup
  (the !owned branch is ordinal-agnostic — pinned). Empty rows are hidden
  by the page window ⇒ inert.
- Keyboard (the curses grammar, ported): `default_highlight` moves 0 →
  **9** (`roster_row_0`) — entering the base camp highlights the first
  ROW and Enter trains; the deploy toggle is one Left away. Empty roster:
  the rewire still seeds HIRE; all-foreign pages: the visibility fall
  lands on the first visible button (the foreign hit zone) — both pinned.
  Nav keeps the §9.5.7 shape with the row-body column in the old train
  column's role (dep_r.right → row_r; row chain over OWNED rows; row_r
  .right → pagers; strip up-links prefer the row-body column).
- Touch: **no new debounce for row clicks (RECORDED)** — the nested train
  screen re-inits buttons and consumes a collapsed second tap in its own
  loop, so the double-tap hazard class is unchanged from the deleted
  TRAIN button; the U6 250 ms stamp stays on the deploy dispatch only.
  The 4px dep/row gutter carries the non-overlap requirement.

#### 9.11.3 Terminals + wasm ledger

- Curses: NO change — the roster list's Enter-trains grammar was the
  SOURCE of this design (`roster_enter_opens_train_seeded_on_that_row`
  re-verified green, untouched).
- Text: **`train N` subprompt KEPT (RECORDED decision)** — the typed
  row-select IS the terminal's row-click; G4 changes only the SDL
  affordance. `text_picker_roster_train_row_opens_seeded_member`
  unchanged.
- wasm §2.10 ledger: **UNCHANGED** — re-verified coordinate-by-coordinate
  now that row rects are actions: GO (278,187) and networking (210,187)
  sit in the y=178 strip; popup OK/YES taps ((160,140)/(95,140)) fire only
  under a modal popup loop; the wasm-touch post-GO tap (160,100) sits in
  the inter-row gap (row 3 ends y=96, row 4 starts y=101) and targets a
  later screen anyway. No e2e tap lands in a row-body zone while the base
  camp is interactive; dist rebuild stays with the branch's closeout
  (WP8) per precedent.

#### 9.11.4 Re-pin blast radius (landed in the SAME commit)

| change | re-pins |
|---|---|
| ids/rects/labels rows 9..17 + no_draw | test_menu_layout exact table (roster_row_N, (26,y,208,10), empty label, no_draw column added) + the dep/row non-overlap assert |
| constant rename (kBaseCampRowBodyBase) | compiler-enforced sweep: rewire/dispatch + test_menu_layout + test_view_team + test_picker_network_client |
| default_highlight 0→9 | test_menu_engine exit-semantics pin; test_menu_layout empty-roster arm (drives spec.default_highlight) + spectator fall arm |
| BFS matrices | joiner-variant + ownership-matrix train references renamed; hidden-pairing asserts unchanged in shape |
| injector flows | roster_train_0/1 → roster_row_0/1 (test_train_team, test_view_team train-open + rename flows, wait_for_base_camp_roster) |
| TRAIN header deletion | no header-string pin existed (verified) — screenshot-verified |
| terminals | ZERO re-pins (no change; suites re-run green) |
| wasm | ZERO ledger changes (verification recorded above) |

### 9.12 UX round 2 (2026-07-21) — G5: networked session-status header [stage host-visibility; FINAL]

On a HEALTHY networked base camp nothing said you were hosting or how many
players were connected — only degraded links surfaced (the orange
connection_alert), and the round-1 line B carried READY r/m + DEP d/t +
SCEN with no role/room/census. This subsection AMENDS the §2.5 networked
line-B content (and the §9.5.3-era net-line formatter); it is DISPLAY-ONLY
over existing lobby data — protocol, wire formats, GO/READY/deploy/
ownership semantics all FROZEN and untouched. Geometry is UNTOUCHED (the
§9.10 line-B slot at x=8, y=17 and every rect stand).

#### 9.12.1 The line-B priority stack (SDL, networked)

1. **`connection_alert` active** → the alert text, ORANGE — unchanged slot
   AND color precedence (join connecting/failed/lost, host relay drop).
2. **Healthy** → the G5 session status, WHITE
   (`format_base_camp_session_status`, pure, picker_common):
   - host: `HOSTING <ROOM> - <n> MACH / <p> PLYR`
     (relay-less/LAN host: `HOSTING <n> MACH / <p> PLYR`)
   - joiner: `IN <ROOM> - HOST: <company16>`
     (direct join: `JOINED - HOST: <company16>`; host not yet elected:
     the room half alone — `IN <ROOM>` / `JOINED`)

   `compose_base_camp_line_b(alert, is_host, room, players)` is the
   pinnable stack (returns text + an alert flag the draw arm maps to
   ORANGE). Solo line B (scen/DEP) is byte-untouched.

#### 9.12.2 Recorded decisions + budget math

- **The 42-char band forced a cut (RECORDED)**: line B runs x=8 to the
  pager wall at x=263 → 42 chars. Room code (9, "GLAD-XXXX") + census +
  READY r/m + DEP d/t ≥ 43 in realistic shapes (48-slot two-machine DEP
  alone is 9 chars) — they cannot coexist. The round-1 READY/DEP counts
  are SUPERSEDED on this line: the §2.6 GO/READY button state machine
  already carries them (host GO face grey-gated/green + the WAITING FOR
  blockers popup names unready companies; the joiner's own state IS the
  READY/UNREADY button; per-row deploy toggles + the §4.2 cap popup carry
  deploys), while role/room/census existed NOWHERE — the G5 gap wins the
  slot. SCEN n likewise (backdrop + SET LEVEL carry it; solo keeps it).
- **`MACH / PLYR`, not spelled out (RECORDED, the "shape like" latitude)**:
  `HOSTING GLAD-XXXX - 16 MACHINES / 16 PLAYERS` = 44 > 42; the
  abbreviated absolute worst (16-seat global cap) is 37. Census machines
  = distinct machine keys with the HOST machine INCLUDED
  (`count_base_camp_session_census` — deliberately unlike the §4.3
  READY denominator); players = total seats.
- **HOST: shows the host machine's COMPANY (RECORDED)**: player names are
  machine-generated `net-<hex>` wire ids; the company is the human
  identity (the format_go_blockers naming convention), machine-key
  fallback when empty, 16-char COMPANY-column clip.
- **Room code source**: new display-only `IPickerLobbyClient::
  session_room_code()` (default empty; both network clients return their
  relay room code raw — a dead room surfaces through connection_alert,
  which outranks the status, so it never advertises as healthy). Room
  display-clips at 12 defensively; the NETWORKING screen keeps the
  authoritative full form. Role = `host_controls_visible()` (a joiner
  ELECTED host on a dedicated server correctly reads HOSTING).
- **Terminals**: the curses lobby status list gains the SAME helper line
  (role + census + host company; no room code exists on that path). The
  text picker holds no networked lobby — ZERO text changes (recorded).
- **format_base_camp_net_line DELETED** (the SDL screen was its only
  consumer — the format_company_file_preview precedent);
  `count_base_camp_ready_machines`/`count_base_camp_display_deploys`
  live on under the frozen §2.6/§4.2 semantics.

#### 9.12.3 Re-pin blast radius (landed in the SAME commit)

| change | re-pins |
|---|---|
| net-line formatter replaced | unit: net-line pins DELETED with the helper; NEW census/host-name/status-shape/budget-42/compose-precedence pins (test_picker_common) |
| line-B content (SDL draw arm) | flow: host_and_join_win_level1... line-B EXPECTs → host census + joiner host-name pins; host_relay_flow → healthy `HOSTING GLAD-XKCD - 1 MACH / 1 PLYR` + post-drop alert-precedence compose pins; join_relay_flow → session_room_code pin |
| curses status line | roster_reflects_two_players gains `HOSTING 2 MACH / 2 PLYR` + `JOINED - HOST: ...` pins |
| geometry / nav / ordinals | ZERO (no button, rect, or model change — layout tables, BFS matrices, kCreateMenu*Index, 1-based text consumers, injector flows all untouched) |
| wasm §2.10 ledger | ZERO (text-only change; no coordinate moved) |

### 9.13 UX round 3 (2026-07-21) — roster discoverability and horizontal spacing [FINAL]

Playtest feedback found two related problems in the §9.11 row-click design:
the screen never explained that the name opens TRAIN, and removing the old
TRAIN column left the surviving columns packed into the left 234 pixels.
`DEPLOY` ended at x=38 while `NAME` began at x=40, visually reading as one
`DEPLOYNAME` label. This subsection amends §9.11 geometry only; row count,
vertical rhythm, ordinals, navigation, ownership, deploy behavior, and train
dispatch are unchanged.

- A non-empty roster draws `TAP NAME TO TRAIN` in YELLOW on a black strip at
  (109,171), in the 7px band between the last possible row and the y=178
  command buttons. Empty rosters omit the inapplicable hint.
- Solo columns now span the full panel: `DEPLOY` x=2, `NAME` x=52, `CLASS`
  x=136, `LV` x=220, `EXP` x=270. The 14px DEPLOY/NAME header gap removes
  the merged label; maximum field ink ends at x=306.
- Networked columns use `DEPLOY` x=2, `NAME` x=52, `COMPANY` x=128, `LV`
  x=278. The explicit DEPLOY header now applies to owned toggles and foreign
  X/- state glyphs alike.
- The no-draw row training zone widens from (26,y,208,10) to
  (26,y,286,10), ending at x=312 so all redistributed text remains inside
  the tap target. The 4px non-overlap gutter after the deploy button remains.
- Pins: the exact Base Camp button table carries the widened row rectangles;
  the nav/ownership matrices remain green; the seeded-train flow now clicks
  the rendered NAME coordinate instead of the row rectangle's center; native
  visual smoke captures cover solo, host, joiner, and degraded-link layouts.

### 9.14 UX round 4 (2026-07-21) — eight-row air and clickable scenario [FINAL]

Follow-up playtesting found that the ninth row still forced the header, final
row, training cue, and command strip into one visually continuous stack.
Base Camp now spends one row of page density on explicit separation and makes
the already-visible solo scenario status directly actionable.

- Roster pages contain **8 rows** at `y=44+15r` (`r=0..7`). The header ink
  remains at y=32..37, leaving six clear pixels before the first row. The full
  roster panel is y=42..159.
- `TAP NAME TO TRAIN` moves to `(109,165)`. Its black backing occupies
  y=164..171, with four clear pixels after the roster panel and six clear
  pixels before the y=178 command strip.
- The solo `SCEN … DEP …` line gains a no-draw hit zone
  `(6,14,208,12)`, covering the formatter's complete 34-character budget.
  Clicking or activating it opens the same Scenario menu as the bottom
  `SCENARIO` command. In multiplayer, line B contains connection status and
  the hit zone is hidden on both host and joiner screens.
- Ordinals become: deploy 0..7, row bodies 8..15, pagers 16/17, scenario-line
  18, BACK/HIRE/SCENARIO/NETWORK/GO 19..23, READY 24; total 25. Cap-24
  rosters still occupy exactly three pages.
- Pins: the exact button table and solo/network nav matrices carry the new
  shape; a coordinate-level test taps the visible scenario ink and observes
  the Scenario menu; the existing NAME test now taps row 1 at its new y=64
  center. Native visual probes cover the resulting solo and network spacing.

### 9.15 UX round 5 (2026-07-21) — roster panel and team-color control [FINAL]

The consolidated Base Camp roster restores the retired View Team menu's
solid grey-panel treatment and turns its colored square into an actionable
team control instead of a family marker.

- One classic two-bevel grey button panel spans `(4,29)..(315,160)`. Its
  inner face covers y=31..158 behind the header and all eight rows. Headers
  and deployed-row text use BLACK; benched rows retain shade 21.
- Each row keeps DEPLOY at `(8,y,14,10)`, gains a no-draw team-chip target at
  `(26,y,10,10)`, and moves the NAME/TRAIN body target to `(40,y,272,10)`.
  The two 4px gutters prevent one tap from dispatching adjacent actions.
- The chip has a BLACK frame and uses the gameplay team ramp
  `teamnum*16+40`. In solo play, tapping or keyboard-activating it advances
  that character through teams 0..3 and runs the normal company autosave
  tail. Networked team assignment remains authoritative: host and joiner
  screens render the team color but hide the cycling target.
- The discoverability cue becomes
  `TAP COLOR FOR TEAM  TAP NAME TO TRAIN` at `(49,167)`, with its backing at
  y=166..173 between the roster panel and command strip.
- Ordinals become: deploy 0..7, row bodies 8..15, team chips 16..23, pagers
  24/25, scenario-line 26, BACK/HIRE/SCENARIO/NETWORK/GO 27..31, READY 32;
  total 33.
- Pins: exact geometry and solo/network navigation cover the third row
  control; a coordinate-level test taps the rendered color square and proves
  it changes team without opening TRAIN; a direct mutation test proves the
  change autosaves; native visual probes cover solo, empty, paged, host,
  joiner, and degraded-link layouts.

### 9.16 UX round 6 (2026-07-21) — padded roster grid and TEAM column [FINAL]

The grey-panel direction is retained, but its content no longer crowds the
bevel and the team control is promoted from an unexplained square to an
explicit column.

- The panel moves to `(6,28)..(313,160)`, with an inner face at
  `(8,30)..(311,158)`. Header ink sits at y=33..38; rows use
  `y=45+14r` (`r=0..7`), leaving six clear pixels both after the header and
  below the final row.
- Solo headers/values use: DEPLOY x=12 / control x=23; TEAM x=54 / chip
  x=61; NAME x=88; CLASS x=164; LV x=236; EXP x=274. Maximum EXP ink ends
  at x=310, inside the panel face.
- Network headers/values keep DEPLOY/TEAM/NAME, put COMPANY at x=160, and
  LV at x=292. Maximum network ink ends at x=304.
- The NAME/TRAIN hit zone is `(84,y,228,10)`. Foreign network rows use a
  read-only `(12,y,300,10)` zone spanning the padded roster width, so any
  row click consistently reports its owning company.
- The discoverability cue becomes
  `TAP TEAM COLOR TO CYCLE  TAP NAME TO TRAIN` at `(34,167)`; the row/control
  ordinals and scenario click behavior are unchanged from §9.15.
- Pins: exact geometry, nav matrices, NAME and TEAM coordinate taps, and all
  six Base Camp visual captures move to the new grid.

### 9.17 UX round 7 (2026-07-21) — classic Load Game chassis [SUPERSEDED BY §9.18]

The Company List keeps unlimited named companies, backups, deletion, sorting,
and paging, but once again reads visually as OpenGlad's old Load Game menu.
This supersedes §9.4's free-floating header treatment.

- Restore the exact classic inner panel `(15,9)..(255,199)`, title well
  `(19,13)..(251,21)`, red `Gladiator: Load Game` title centered at x=135,
  per-row wells `(23,23+15r)..(246,36+15r)`, and BACK well
  `(23,173)..(66,196)`.
- Restore each primary slot face to `(25,25+15r,220,10)` and the BACK button
  to `(25,175,40,20)`. The post-face one-pixel boxes from the old slot loop
  are retained around every visible company row and BACK.
- Company metadata remains inside the wider classic face at x=27/141/155.
  The §9.4 column headers are removed; the date and compact roster count are
  treated as extensions of the old saved-game label.
- BK and X remain available as an intentionally newer side rail at
  `(259,25+15r,24,10)` and `(287,25+15r,24,10)`. PREV/NEXT move to y=175;
  the page indicator uses DARK_BLUE at `(135,181)` inside the grey footer.
- Behavior, row ordinals, active-company outline, keyboard graph, and all
  company/backups flows are unchanged. Exact-table pins carry the new rects;
  engine draw smoke covers the custom background and content passes.

### 9.18 UX round 8 (2026-07-21) — three faces inside each old slot [SUPERSEDED BY §9.19]

The §9.17 side rail made the screen wider than the original Load Game frame
and defeated the restoration. Keep the old menu's complete footprint and
divide its existing slot faces instead.

- The panel, title, ten 15px-pitch slot wells, BACK well, and late repaint
  order remain exact transcriptions of `create_load_menu`.
- Each `(25,25+15r,220,10)` slot becomes three buttons within the same
  x=25..245 footprint: company `(25,25+15r,164,10)`, BK
  `(193,25+15r,24,10)`, and X `(221,25+15r,24,10)`. Four-pixel gutters show
  the original inset well between the faces.
- The company face centers only the company name, matching the old saved-name
  button. Counts, dates, and headings are removed from the SDL list.
- The old fixed ten-row silhouette is preserved on partial pages: remaining
  rows show an inert `EMPTY SLOT` company face plus two blank action faces.
  Exact pixel pins cover all three bottom-row bevels so this cannot silently
  collapse back into a short list over an empty panel.
- Multi-page controls stay inside the classic footer: PREV at x=160 and NEXT
  at x=205, with the page indicator centered at x=135. On a single page both
  hide, leaving the original BACK-only footer.
- The initial keyboard highlight and primary-column vertical cycle are restored
  from the old menu: BACK → row 0 → ... → BACK. Actions, row ordinals, paging,
  active-company marker, backups, deletion, and confirmation behavior remain.

### 9.19 UX round 9 (2026-07-21) — centered, roomier paged Company List [FINAL]

The classic three-face treatment remains, but the complete menu is centered
on the 320×200 screen and uses fewer, more widely spaced rows so the title and
content no longer crowd one another.

- The outer panel is `(40,8)..(280,192)`, centered at `(160,100)`. The title
  well grows to `(48,13)..(272,28)`, shares the row wells' horizontal bounds,
  and centers `Gladiator: Load Game` at `(160,18)`.
- Pages contain **8 rows** at `y=35+17r` (`r=0..7`). Each row well spans
  `(48,y-2)..(272,y+11)` and divides the centered x=50..270 content into the
  company `(50,y,164,10)`, BK `(218,y,24,10)`, and X `(246,y,24,10)` faces.
  Partial pages retain inert `EMPTY SLOT` faces through row 7.
- The footer moves to y=169: BACK at x=50, PREV at x=185, and NEXT at x=230;
  the page indicator centers at `(160,175)`. One-page lists still hide both
  pagers.
- PageModel's actual company page size changes from 10 to 8. Ordinals become
  rows 0..7, BK 8..15, X 16..23, BACK 24, PREV 25, NEXT 26 (27 total).
  Keyboard closure, active-company marking, backup/delete actions, and the
  classic BACK-first vertical cycle are retained.
- Pins cover the new exact table, 8/7 two-page visibility split, placeholder
  bevels, keyboard reachability, and a full flow that flips an 11-company list
  and opens the ninth company from page 2 row 0.

### 9.20 UX round 10 (2026-07-22) — shared naming and team-setting grammar [FINAL]

Two controls that edit the same kind of data now use the same visual and state
grammar instead of presenting independent conventions.

- The New Company name input replaces the §9.3 black centered strip and
  floating yellow title with the character naming/renaming modal's exact
  stock-grey **132×22** face. Centered at `(94,70)..(226,92)`, it draws
  `FOUND YOUR COMPANY:` at `(96,74)` and the editable value at `(96,82)`,
  both left-aligned DARK_BLUE. The 18-character company cap fits the face;
  REROLL, ACCEPT, BACK, hint, navigation, and generator behavior are unchanged.
- Base Camp's TEAM chip and TRAIN's `Playing on Team N` now cycle the same
  saved roster `guy::teamnum` through `cycle_guy_team`. A TRAIN click updates
  both its working copy and the roster immediately, then runs the shared
  lobby-sync/ready-clear/autosave mutation tail. Consequently BACK no longer
  makes an already-displayed team choice disappear; stat edits remain pending
  until ACCEPT as before.
- Pins cover the new name-box geometry and stock face, plus immediate
  TRAIN-to-roster synchronization before ACCEPT. The existing Base Camp team
  chip and autosave tests continue to cover the opposite entry point.
