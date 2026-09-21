---
name: openglad-menus
description: How to add, change, move, or test picker menu entries, buttons, subscreens, and labels in OpenGlad. Use this skill whenever the task touches the picker UI in any way — adding a menu item or setting, relabeling or moving buttons, creating a subscreen, changing keyboard navigation, gating buttons by host/campaign, or writing/fixing menu tests (test_menu_layout, test_ctf_ui injector flows, text/curses picker tests, the interactive shell script). Even a "small" label change has triple-client and test-pin consequences — consult this first.
---

# OpenGlad picker menus

Every lesson here was paid for with a real bug or a broken test. The picker is
a triple-client system with pixel-exact layout tests and hand-wired keyboard
navigation; changes that look local rarely are.

## The shared-model chain (one entry = one chain, never a partial)

A menu entry exists once in the shared model and is consumed by three clients:

1. `src/interface/ui/menu_model.cpp` — the `PickerMenuItem` (stable string id +
   `PickerMenuCommand`). Ids are the handles tests and the interact() API use.
2. `src/interface/ui/picker_common.cpp` — a PURE helper for the behavior and a
   `format_*_label(const SaveData&)` for any dynamic label. Pure = headlessly
   unit-testable, and it is the single source of label text for all clients.
3. SDL: a `button` row in the static table in `picker.cpp`, a `ButtonAction`
   enum value (append-only — check the current next-free value in
   `include/openglad/interface/button.h`), a `do_call` case in `button.cpp`,
   and a `change_*()` callback.
4. Text: a `handle_*_item` case in `src/platform/text/text_picker.cpp`.
5. Curses: `handle_menu_item` + `menu_item_label` in
   `src/platform/curses/curses_picker_client.cpp`.

The retired player-facing `allied_mode` toggle remains a useful compatibility
example, but new synchronized settings should follow the CTF settings trio. If
a setting affects the sim, it also needs the SaveData → LobbySettings → world
plumbing — that's a separate, bigger chain (versioned save/wire formats); see
`docs/ARCHITECTURE.md`.

## Pixel budgets (measure before you write a label)

- The small font advances 6px per character. An 80px button face fits at most
  12 characters; labels are drawn CENTERED with NO clipping
  (`button.cpp` computes start_x from length), so an over-budget label escapes
  the bevel on both sides. "CTF Teams: Auto" (15 chars) shipped overflowing
  once; never again.
- The team-build grid: columns x=30/120/210, rows y=40/70/100/140/170, button
  faces 80px wide, 15px tall (20px on bottom rows). Respect the grid; a
  one-off wide button reads as clutter.
- Mission-briefing text budget is 33 chars/line; the MATCHUP detail line is 47
  (39 when paged); the scenario-viewer line budget is 48. Tests pin these.

## Layout discipline (grids, not coordinates)

The unified player screen shipped with RESET at x=218 next to a button stack
at x=214, REMAP at x=123 above ZOOM at x=122, a band at x=16 over a panel at
x=12, and a title-to-band gap of 15px where every other gap was 4-6px. Every
pin test was green — the user saw it instantly. Never again:

- **Declare the grid before writing a rect.** A screen is columns and bands:
  name the column edges and the band pitch as constants
  (`kScreenLeftCol = 12`, `kScreenRightCol = 214`, band pitch), and derive
  every x/y from them. A literal coordinate that appears once is a future
  1px drift; the REMAP/ZOOM 123-vs-122 bug was exactly two hand-typed
  near-neighbors.
- **Vertical rhythm is part of the design.** Gaps between bands must be
  uniform or deliberately graduated — measure title→first-band against
  band→band before accepting a layout. A first gap 2-3x the others reads as
  "the top row is floating".
- **Alignment is a testable RELATION, not an absolute.** Exact-table pins
  happily pin a crooked layout — they prove self-consistency, not alignment.
  Layout tests must also assert the relations: every button in a declared
  column has the same `.x`; every right-column rect has the same right edge;
  band pitches are equal. Then a future edit that breaks the grid fails a
  test instead of a user's eye.
- **Shared layouts share constants.** When two screens are meant to be
  geometrically identical (pause player screen ≡ seat settings), one
  constants block feeds both tables — duplicated literals drift the moment
  one screen is edited alone. Pin the identity with a test that diffs the
  two geometry tables field by field.
- **Read the capture like a reviewer.** The visual-verification pass below
  is not only "is my text there": trace the column edges in the screenshot
  and say out loud which rects share them. Misalignment survives every
  automated gate; the read-back is where it dies.

## Labels have TWO surfaces

The static `button` row (rebuilt from the k_* table at menu entry) and the
live `vbutton` in `allbuttons_[index]` are separate copies. A click callback
that updates only one shows stale text on the other path. Dynamic labels must
be written to BOTH (or re-derived from save at every refresh point: menu
entry, the click callback, and the per-frame visibility sync if a lobby can
change the save underneath the open menu).

## Index contracts (why growth is append-only)

Buttons are addressed by POSITIONAL index: `kCreateMenu*Index` /
`kTeamsMenu*Index` constants in `src/interface/ui/picker_sdl_defs.h`, and the
label-writing callbacks write by index. Reordering a table without updating
the constants silently relabels the wrong button. Append new rows at the end;
when a restructure is unavoidable, update the constants, the per-frame sync
code, and the layout tests in the same change.

Base Camp has exactly one page window: the eight roster rows. The seat rail
below it is not paged — it is this machine's four fixed slots (ordinals
35..38), and remote seats never appear there. A slot holding one of this
machine's seats draws that seat (`BaseCampSlotKind::Seat`); a bare slot is
the ADD PLAYER door on the same ordinal (LOBBY FULL and dimmed when the
lobby has no room, hidden outright past what the device can seat).
Ordinals 33, 34, 39 and 40 are parked spares — the retired `[+]` and the
two seat pagers — re-parked every frame. The header's line B carries the
players/machines census that the rail no longer shows.

Two consumers select TEXT menu items by 1-based position and break silently
on reorders: `scripts/test_text_picker_interactive.sh` and the scripted drive
in `tests/unit/test_platform_headless.cpp`. Grep both whenever
`kTeamBuildItems` (or any menu's item list) changes shape.

## The SETUP wizard (the second screen on the camp chassis)

On a `matchup: versus` campaign the wizard has exactly ONE door on every
client, and it is not on the command strip: it is the Base Camp docket's own
row, `SETUP - <GAME>: <ARENA>  >` (`zone_action_0` on SDL and web, the camp
prompt's row `1` on the terminals). The strip reads
`BACK · DIFFICULTY · SCENARIO · NETWORK · GO` on EVERY campaign — ordinals end
at 72 and the ceiling is 73, so do not go looking for a versus-only strip
ordinal. The row opens `MenuScreenId::MatchSetup`, a five-step wizard
(GAME → ARENA → TEAMS → RULES → MATCH) that is a room inside the camp panel
and reuses the zone submenu's constants. Working on it, know these six things:

- **The grid is declared, and it has ONE right edge, 310.** Tabs and the
  ARENA pagers (the only tenants of the 30 px cell column, 280..310) all
  end there; rows are the docket's 264-wide 42-glyph face. The
  vertical/rhythm names live in
  `include/openglad/interface/ui/match_setup_session.h` (the session is
  SDL-free and needs them); every x/w/tab/cell/footer/ordinal name lives in
  `picker_sdl_defs.h`, which includes that header and `static_assert`s the
  two against the zone constants. Do not add a third home.
- **The cycler rows cycle FORWARD ONLY**, like every other cycler in the
  picker. There is no `<` cell and no terminal `N-` grammar — that answer
  takes the driver's `Invalid setup row.` notice — and no reverse on the
  right mouse button: the wizard sets no `right_click_enabled`, so the
  engine's legacy rule applies and a right-click is just a click that
  steps the wheel forward. The wheels are three to five stops, so an
  overshoot costs a lap. LEFT/RIGHT stay NAVIGATION on this screen as on
  every other.
- **The escape-door table** the injector flows bind starts with
  `{"setup_back", "setup_back"}` — BACK/Escape closes the wizard from any
  step; PREV/NEXT walk it; a tab jumps to it.
- **Team Build has 12 items**, and none of them is the wizard: item 11
  `Difficulty` is a plain item and there is no `Setup` item. The one
  `Custom`-gated terminal item is SCENARIO's `Replay Level`, which refuses on
  a versus campaign (`Arenas are set, never replayed.`). Both index consumers
  below apply whenever that list changes shape.
- **Test flows press the docket row, not a strip button.**
  `open_setup_step(tab, WORD)` in `tests/test_click_ladder.h` clicks
  `zone_action_0` until `setup_tab_0` exists, then clicks the step's tab until
  it wears `[WORD]`. The FILL notes are per WHEEL, not per screen:
  `weak to brutal` on the wizard's macro row (NONE is off that wheel),
  `none to brutal` on LINEUP's per-team band row. A joiner's CROSS CONTROL
  home is the strip's DIFFICULTY door, visible and read-only (networked).
- The design record, with the button table, the per-row budgets and the
  decisions, is `docs/match-setup-design.md` (round 2 is its §10).

## Keyboard navigation rules

- `MenuNav` links are raw indices and do NOT skip hidden buttons. A link into
  a hidden button strands keyboard focus invisibly.
- Two sanctioned patterns: (a) static tables route AROUND conditionally-hidden
  buttons, with a rewire applied when they're shown (small cases); (b) a
  full-graph rewire function recomputed every frame from the visibility state
  (`picker_wire_teams_menu_nav` style) when a screen has several independent
  visibility axes. Pattern (b) must be pinned by a BFS-reachability test over
  every visibility variant: every visible button reachable, no link targeting
  a hidden one.
- Run `ensure_highlighted_button_visible` after every visibility sync or the
  highlight can soft-lock on a hidden button.
- A button whose `whatfunc` is 0 is KEYBOARD-DEAD: `handle_menu_nav` only
  produces a result for nonzero `myfunc`, so Enter does nothing even though
  the button is highlightable. Pure-navigation buttons (pagers, page flips)
  need a real `ButtonAction` whose handler returns `MENU_OK`.

## Draw order

`draw_buttons()` runs before the screen's content draw. Any translucent fill
(readability bars, backing rects) painted in the content pass lands ON TOP of
buttons and dims them — the MATCHUP pagers shipped at 41% brightness this way.
Split background fills into a pre-pass before `draw_buttons`; keep text after.
Content also OVERDRAWS buttons: the main menu's grey SETTINGS heading band
(drawn in the content pass) once painted over a button placed in its row —
layout pins don't see overdraw, only screenshot read-back does.

Two palette traps in the same family:

- **A dimmed face can eat its own bevels.** `GREY` (the RowState::Disabled
  face) resolves to the SAME shade as `BUTTON_RIGHT`/`BUTTON_BOTTOM`, so a
  disabled row drew as a card with a flat right edge until `vbutton::dimmed`
  stepped those two edges one shade darker. Any new face color needs the same
  check against 11..15.
- **A band of raw backdrop between two opaque things reads as a defect.** The
  title art runs behind every picker screen; an 8px gutter framed a fragment
  of its grey blade so exactly that reviewers read it as a stray glyph. Paint
  a band its own ground (pre-buttons, opaque) when chrome floats in it — that
  also makes the local and networked frames identical there.

## Borrowing a viewscreen for previews (camera leak)

`screen::relayout_views()` restores view RECTS but never the CAMERA
(`topx`/`topy` are set once at construction). Anything that borrows
`viewob[0]` and lets `redraw` copy level visuals onto it must
RAII-restore topx/topy/control on EVERY exit path — otherwise every
subsequent menu pixie/backdrop/portrait draw is displaced until level
start (the HIRE/TRAIN empty-box bug).

## Blocking subscreens (the only sanctioned pattern)

Clone the MATCHUP screen's internal `create_teams_menu` /
`create_scenario_menu`, not ad-hoc loops:

- per-iteration `picker_lobby_poll()` (the lobby must stay alive),
- per-frame `sync_*_host_control_visibility` + nav rewire,
- BACK returns `MENU_REDRAW`; `MENU_EXIT` is reserved for propagating exits —
  distinguish your own BACK from a joiner remote-start
  (`team_build_remote_start_requested`) so a host GO still launches everyone
  parked in a subscreen,
- mirror the parent's level-reload guard (`last_level_id` vs `save.scen_num`)
  if the screen reads the loaded world — a host can SET LEVEL while a joiner
  is parked inside.

Host gating: host-only buttons are hidden per frame (the sync function), and
their nav links rewired in the same pass. The pattern's reference users are
GO / SET LEVEL / SET CAMPAIGN.

## Testing menus

- Layout/nav: `tests/integration/test_menu_layout.cpp` pins geometry, label budgets, and
  nav graphs (including hidden variants). Re-pin in the same commit as any
  layout change.
- Injector flows (`tests/integration/test_ctf_ui.cpp` etc.): interact by button
  id, never coordinates; `wait_for_interactable` before clicking; settle with
  `wait_for_menu_frames(n)` on engine-hosted screens — there is NO fade to
  wait for under TESTING (`FadeBetween` is a single blit), so a flat settle
  waits for an animation that never runs and proves nothing about the
  incoming screen. A screen `run_menu_screen` does not host can never satisfy
  `wait_for_menu_frames`, so use its own oracle: the blocking `input_string_ex`
  editor through `og::input_native::text_input_is_active()`, the help viewer
  through `og::input_native::yield_count()`, the campaign picker through the
  campaign picker's counters (`entered` and `action` today, plus the per-frame
  counter the picker-drive header adds).
- Prove every click was CONSUMED by the value, label or trace it writes, read
  on the menu thread via `run_on_main_thread` — never by counting the clicks
  you sent, and never with a flat delay: the press is still held when a label
  flips, and a second press without a release is silently dropped (symptom:
  menus that "refuse" to exit, long hangs). The ladders do this for you (see
  below), and `click_cycle_step` in `test_options_menu.cpp` is the reference
  for a cycle row. A flat `SDL_Delay` settle fails
  `scripts/check_injector_settles.sh` (a dependency of `og_game_test`): tier 1
  bans a literal `SDL_Delay(750)`, tier 2 — the fully converted files — allows
  only a poll tick (a literal <= 100 ms or an argument spelled with `poll`),
  so a settle hidden behind a named constant fails too.
- An injector driving a BLOCKING menu must never bound its escape tail by
  wall clock: `run_pause_menu` and friends return only when something clicks
  their way out, so a tail that stops clicking guarantees the hang it exists
  to prevent. Loop until the main thread signals it left the screen (an
  atomic set after the menu call returns), clicking whatever exit the current
  screen publishes — BACK as well as RESUME, since the player sub-screen
  publishes no RESUME. That rule has ONE implementation:
  `tests/test_escape_tail.h` (`escape_to_the_main_thread` — one press per
  screen, watch the screen you acted on go away before pressing again, a
  numbered leg for every give-up). Call it; do not re-type it.
  Binding a new consumer to it is two decisions and one line at the join:
  - Doors. Give it an `EscapeDoor` table — `{watched id, id to press}` — in
    PRECEDENCE order, listing every screen the flow can be blocked in. The
    picker's `kPickerEscapeDoors` is the default and needs no argument;
    `test_pause_menu.cpp` binds RESUME and then the player screen's BACK
    (that sub-screen publishes no RESUME); `test_overpowered_team.cpp` binds
    the founding flow's four doors with `hire_me` before `go`, because the
    team screen publishes both and the wrong arm walks the flow somewhere
    neither door names. The tail presses only the FIRST door whose watched
    id is up.
  - `hold_lap`. A predicate for a lap that must not press at all, called
    once per lap before any door is consulted. A real match is running and
    a click would land in the game (`g_test_in_game`); or a screen that is
    NOT `run_menu_screen`-hosted owns the main thread, so its buttons are
    its own local array and a click aimed at an `allbuttons` id lands at
    whatever sits under those coordinates — there the predicate is what
    closes the screen, by its own door (`campaign_picker_testing_abort()`
    in `test_campaign_sprite_uaf.cpp`), and returns true while it waits.
  - The join, on the main thread: store the flag → `SDL_WaitThread` →
    `escape_tail_join_hygiene()` → `EXPECT_EQ(0, thread_result)`. The
    hygiene call is not optional decoration: a flushed half-consumed
    release leaves `mouse_state.left` stuck true and the next flow's first
    press silently evaporates.
  Every give-up returns through the tail, and so does the happy path — with
  one exception: the pause-menu flows return 0 directly, because their main
  thread consumes the injector's last click (RESUME / QUIT) as a specific
  action and then plays on, and a tail pressing "whatever is up" in that
  window would race the frame that consumes it. Their give-ups still route
  through the tail.
  No hand-rolled copy is left in the tree, and
  `scripts/check_escape_tail_twins.py` (a dependency of `og_game_test`,
  self-tested by the ctest entry `check_escape_tail_twins_selftest`) fails
  the test build on a new one: a loop that spins on nothing but a flag and
  presses its way out. The settle census is a DIFFERENT rule with a
  different script: `test_pause_menu.cpp` is on tier 1 only, because its
  RESTART and QUIT-fade injectors still carry clock-bounded GO ladders that
  belong to the click-ladder conversion rather than to the tail, and
  `test_campaign_zone_ui.cpp` and `test_lineup_ui.cpp` are still on
  `scripts/check_injector_settles.sh`'s pending list.
- The id `back` is shared by several screens: disambiguate with
  `wait_for_interactable_at("back", x, y)` using each screen's unique
  geometry.
- Under ASan frame-stretch (and heavy load) single clicks get swallowed by
  menu transitions: use the shared, acknowledged click ladder in
  `tests/test_click_ladder.h` — `click_until_edge` for an on-screen edge,
  `click_and_acknowledge_trace` for a named trace, and the label ladders
  built on them (`click_until_label`, `click_until_label_containing`) —
  instead of one click + a long wait. Do not write a fourth copy of the
  idiom; one implementation per rule. Toggle safety is the reason the ladder
  is bounded and acknowledged: a press is re-sent ONLY with evidence it did
  not land (no witness — trace, label or edge — AND the edge still absent at
  re-press time), because re-pressing a cycler that did land walks it past
  the face you wanted. The same header holds `click_until_value_moves` —
  walk a cycler N faces, each step proven by the value the row wrote, which
  is what a film lap or a restore-to-default leg needs. The presented-frame
  handshake has one home too: `tests/test_frame_capture.h`
  (`capture_presented_frame` / `verify_captured_frames`) freezes a settled
  frame, records on the injector thread, asserts on the main thread, and
  writes a PPM only when the caller hands it an output directory.
- `popup_dialog` under TESTING is trace-only — assert via
  `trace_contains("popup", ...)`, nothing to dismiss.
- Flow tests overwrite `save/save0.gtl`; write your own save first and restore
  any campaign mount you switch (`SaveData::load()` MOUNTS the save's
  campaign — a CTF save must use scen 500+, and gladiator must be remounted
  after, or later tests inherit the wrong mount under `--gtest_shuffle`).
- `og_test_menu_ui` is the slowest suite binary (~130s); put new heavyweight
  flows in an appropriate group, and get fast signal from the pure
  `picker_common` unit tests first.
- Coverage gate: every new `src/` line needs ~90% execution; pure helpers in
  `picker_common` are cheap to cover, SDL screens need an injector flow.

## Visual verification (a green suite says nothing about layout)

Any menu/HUD/render change: capture a PNG (scripts/media/capture_showcase.sh
or `save_screenshot()` into gitignored build/media/), then READ the image
back and describe what you see BEFORE claiming success. Playwright pixel
assertions are not visual verification. Gotcha: capture must follow the
single real `SDL_RenderPresent` — a second present eats the HUD overlay;
test builds emit BMP/PPM, convert losslessly before viewing.

Restoring or matching a legacy screen: never reconstruct it from memory,
docs, or code reading. (1) `git worktree add` the pre-migration commit,
(2) build it and dump SETTLED frames through its own render loop (the
async screenshot probe captures mid-fade), (3) capture the current screen
at the same 320x200 through the same hook, (4) crop-compare the
must-be-unchanged regions and require RMSE 0, (5) capture a NON-full data
state too (e.g. 4 of 10 saves — that is what exposed the missing EMPTY
SLOT rows). For "make it look like X" tasks generally: capture the
master-branch reference shot and put both images side by side in one
message.

## Setting scope: TEAMS/SEATS vs TROOPS/SCENARIO

Before adding any cycler value, ask "does this change with the map or
with the players?" TEAMS/SEATS settings describe who is sitting at the
game (per-machine, host-owned, packed into header slots 2/3/4).
TROOPS and other SCENARIO settings describe what the level contains.
Smell test: if your new value needs a special case to behave identically
to an existing one, it is on the wrong axis. Getting this wrong costs a
save migration — sentinel values persist in saves (MATCH shipped as
TEAMS team_count=5 and had to be moved to TROOPS).

## Teams are authoritative (the team/seat test matrix)

Company/save ownership NEVER implies friendliness: same team can never
damage each other, different teams are always hostile, a player can
never take control of a foreign-team character. These call sites all
independently re-derive teams and must agree: melee/attack predicate, AI
targeting, collision auto-attack, projectile/summon owner chains, mage
freeze-time, SAVE_ALL scoping, remaining-foes/victory counting,
results/MVP, kill+XP+score credit, respawn accounting, replay
reconstruction.

Team assignment changes are never "a small UI change". Required matrix,
driven through the REAL Base Camp → seat-assignment launch path (never a
hand-built launch struct): {1,2,4 seats} × {ally, pvp} × {team
assignments 1..4, incl. a seat with no team-1 character}, asserting
(a) every roster character spawns, (b) at ITS OWN team's spawn point,
(c) same-team can't damage / cross-team can, (d) no character dropped
from the save on launch, (e) remove-and-re-add a seat yields disjoint
key mappings.

## Checklist: adding one menu entry

1. menu_model item (id + command) + `test_menu_model` resolution case.
2. picker_common helper + label formatter + unit tests (exact strings, budget
   assertion ≤12 chars for 80px buttons).
3. SDL: ButtonAction (append; note the value in your report), do_call case,
   button row on the grid, callback updating BOTH label surfaces +
   `picker_lobby_sync_settings_from_save()` if it's a lobby setting.
4. Nav: static links (+ conditional rewiring if visibility-gated) +
   layout/nav test re-pin.
5. Text + curses handler cases (+ their tests).
6. Check the two hard-coded-index consumers if the item list changed shape.
7. Run: picker_common units → og_test_picker → og_test_menu_ui → full gate.
