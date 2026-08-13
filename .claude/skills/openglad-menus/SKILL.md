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

Base Camp has two independent page windows: eight roster rows and four
player-seat cards. The **SEATS**, **<**, card, and **>** controls have their own
indices and navigation. Do not couple seat paging to roster paging, and keep
both arrows hidden unless the seat list spans multiple pages.

Two consumers select TEXT menu items by 1-based position and break silently
on reorders: `scripts/test_text_picker_interactive.sh` and the scripted drive
in `tests/unit/test_platform_headless.cpp`. Grep both whenever
`kTeamBuildItems` (or any menu's item list) changes shape.

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
- Injector flows (`tests/integration/test_ctf_ui.cpp` etc.): interact by button id, never
  coordinates; `wait_for_interactable` before clicking; `SDL_Delay(750)` after
  fadeblack-prone waits; and `SDL_Delay(300)` after ANY label/trace wait
  before the next `interact` — the click press is still held when the label
  flips, and a second press without a release is silently dropped (symptom:
  menus that "refuse" to exit, long hangs).
- The id `back` is shared by several screens: disambiguate with
  `wait_for_interactable_at("back", x, y)` using each screen's unique
  geometry.
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
