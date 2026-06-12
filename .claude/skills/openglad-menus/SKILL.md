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

`allied_mode` is the canonical end-to-end template; the CTF settings trio
followed it exactly. If the setting affects the sim, it also needs the
SaveData → LobbySettings → world plumbing — that's a separate, bigger chain
(versioned save/wire formats); see `docs/ARCHITECTURE.md`.

## Pixel budgets (measure before you write a label)

- The small font advances 6px per character. An 80px button face fits at most
  12 characters; labels are drawn CENTERED with NO clipping
  (`button.cpp` computes start_x from length), so an over-budget label escapes
  the bevel on both sides. "CTF Teams: Auto" (15 chars) shipped overflowing
  once; never again.
- The team-build grid: columns x=30/120/210, rows y=40/70/100/140/170, button
  faces 80px wide, 15px tall (20px on bottom rows). Respect the grid; a
  one-off wide button reads as clutter.
- Mission-briefing text budget is 33 chars/line; the TEAMS detail line is 34
  (26 when paged); the scenario-viewer line budget is 48. Tests pin these.

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
buttons and dims them — the TEAMS pagers shipped at 41% brightness this way.
Split background fills into a pre-pass before `draw_buttons`; keep text after.

## Blocking subscreens (the only sanctioned pattern)

Clone `create_teams_menu` / `create_scenario_menu`, not ad-hoc loops:
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

- Layout/nav: `tests/test_menu_layout.cpp` pins geometry, label budgets, and
  nav graphs (including hidden variants). Re-pin in the same commit as any
  layout change.
- Injector flows (`tests/test_ctf_ui.cpp` etc.): interact by button id, never
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
