# Menu runtime

The SDL picker uses a shared runtime for screens that can be described as
button rows plus a small set of lifecycle callbacks. It keeps the classic
320×200 layouts and hand-authored navigation while removing duplicated input,
redraw, and lobby-poll loops.

## Ownership

`menu_screen_host(MenuScreenId)` records whether a screen is runtime-hosted,
legacy, or retired.

| Screen family | Owner |
|---|---|
| Main, Game Settings, Display, Effects, Seat Settings | Runtime |
| Difficulty (opened from the Base Camp strip) | Runtime |
| Base Camp, Hire, Train, Progress, View Level, Scenario | Runtime |
| Campaign zone submenu (the Base Camp book pages) | Runtime |
| Company List, Backups, company name entry | Runtime |
| Networking | Legacy `SdlPickerClient` loop |
| View Team, Matchup, manual Save/Load slots, global Controls | Retired |

Networking remains legacy because its room-list state machine needs pre-input
polling and click-against-visible-snapshot semantics that the shared frame
contract does not expose. See [Why Networking remains legacy](#why-networking-remains-legacy).

## Screen specification

A `MenuScreenSpec` supplies:

- a stable button-row table;
- build and runtime gates;
- static navigation or a per-frame rewire callback;
- optional label, color, and art bindings;
- entry, reset, frame, input, and drawing callbacks;
- lobby polling and remote-start behavior.

Rows are materialized into the existing `button`/`vbutton` surfaces. Hidden
rows disappear from both pointer and keyboard navigation. Disabled rows remain
visible but have no action.

Dynamic company and backup lists use fixed row tables windowed by `PageModel`.
Stable row IDs and button ordinals are therefore preserved between pages.

## Return values

Picker loops retain the original bit contract:

- `MENU_EXIT` leaves the current structural scope;
- `MENU_REDRAW` returns from a nested screen and rebuilds the parent;
- `MENU_OK` requests an in-place reset.

Loops test the exit bit at the top, but inner input paths stop immediately only
for an exact `MENU_EXIT`. Composite values may draw one final frame before the
top-of-loop check.

Remote starts use one of two explicit shapes. Subscreens return `MENU_EXIT`.
The main menu selects Continue, breaks its loop, and lets the picker state
machine enter Base Camp before launching.

## Generic row dispatch

New runtime-owned actions use `ButtonAction::MenuSpecRow`. `vbutton::do_call`
stores the row argument, and the runtime consumes it in the same frame through
`spec.on_spec_row`.

The numeric action value overlaps the legacy return-bit space, so the runtime
must replace the temporary action value with the callback's real return value
before testing the loop condition. Test builds fail if the row stash survives
a frame.

## Frame contract

Entry performs these steps:

1. Materialize the button rows and run `prepare_buttons`.
2. Initialize live buttons and clear keyboard state.
3. Apply art bindings.
4. Compute row gates and navigation.
5. Compose and present the cold first frame — faded in when the entry is a
   context switch (see "Drawing and transitions").

Each frame then:

1. Polls the lobby when requested.
2. Recomputes gates and rewires navigation.
3. Handles a remote start for the screen's declared scope.
4. Dispatches pointer and keyboard input.
5. Consumes screen-specific clicks and generic row actions.
6. Resets buttons when requested and reapplies bindings.
7. Runs the frame callback, including level-reload checks.
8. Re-derives labels and colors from fresh state.
9. Draws background, buttons, content, highlight, and the final buffer.
10. Yields for 10 ms.

Label bindings always update both the mutable descriptor and the live button.
This matters when a lobby update rewrites the save while a settings or Base
Camp screen is open.

## Navigation

Navigation remains explicit. Classic menu graphs are not purely geometric:
they contain wrap cycles, ties with intentional winners, and crossed links that
keep otherwise isolated buttons reachable. A runtime rewire may route around a
hidden row, but it must not infer a new graph merely from button coordinates.

Build-gated rows are also positional. Until navigation uses stable row IDs,
compiled variants must keep every surviving raw index and link valid.

## Drawing and transitions

Fades follow one rule (#237): a transition fades exactly when it crosses the
main-menu boundary, and it fades **symmetrically** — the way in and the way
back. `run_menu_screen` derives the decision; no screen declares a fade:

- `MenuScreenKind::Screen` entered at menu-stack depth 1 fades: the previous
  surface fades out (skipped when a teardown already noted the surface
  black), the cold first frame is composed, and `fadeblack(1)` presents it.
  Every main-menu door enters this way — GAME SETTINGS, HELP, the LOAD list,
  BEGIN NEW GAME's name entry, CONTINUE's Base Camp — and so does the main
  menu itself behind each BACK. That symmetry is the rule's whole point: a
  door that faded only on the way back was the bug (#237).
- A nested `run_menu_screen` call (any subscreen door under an open screen)
  never fades; its cold frame is composed and presented directly. Base Camp's
  strip and roster doors, settings' own subscreens, and the company list's
  BACKUPS view are all instant, both ways.
- Two doors sit on neither side of that line and take a one-shot
  `note_menu_entry_fade()` override (swallowed by every entry like the black
  note, so an unused one cannot leak forward):
  - **Nested main-menu doors** — CLOUD SAVES and the LEVEL EDITOR run inside
    the still-open main menu, at a depth the rule reads as "subscreen".
    `run_nested_menu_door()` is the one bracket both share: it fades the menu
    out, notes `Fade` so the nested entry fades in, and on the way back fades
    the door out and leaves a black note that the parent loop turns into a
    fade-in on its next present. Half of the way in belongs to the body: it
    has to spend that `Fade` note on a fade-in of its own first composed
    frame. CLOUD SAVES gets that from the runner; the level editor runs its
    own loop, so it calls `begin_legacy_menu_entry_fade()` and presents its
    first frame with `fadeblack(1)`. A body that discards the note leaves the
    door at one fade in against two back — the asymmetry #237 is about.
  - **NETWORKING** — a Base Camp strip door, but Base Camp *exits* before its
    legacy screen runs, so the rule would fade both legs. `configure_networking`
    notes `Instant` on the way in and on a non-hookup return, keeping the door
    as snappy as its HIRE/TRAIN/DIFFICULTY siblings. A successful hookup notes
    nothing: that one leaves the lobby-config context and Base Camp's entry
    fades it.
- `MenuScreenKind::Overlay` (the pause family) never fades at any depth.
- Legacy full-screen entries outside the runner — NETWORKING, campaign
  select, the results panel — apply the same rule by hand through
  `begin_legacy_menu_entry_fade()`, which honors the same override.

The teardowns that already fade the presented surface to black (the two
gameplay exits, the intro's last page, the new-game cut) call
`note_menu_faded_to_black()`; the next fading entry skips its own fade-out
instead of playing black-to-black (#200). Every entry consumes the note,
fading or not, so a stale note cannot leak one screen forward. Purely
technical fades that mask loads and canvas resets (glad_init, the gameplay
teardowns) are not entry transitions and stay where they are.

Fades are instant under TESTING (`FadeBetween` blits once and traces instead
of animating), so injector `SDL_Delay(750)` waits are generic settles, not
fade timing.

The runtime also supports:

- background drawing below buttons;
- content drawing above buttons;
- screens without a backdrop;
- explicit redraw of button faces after art that overlaps their rectangles.

The Base Camp roster uses no-draw hit zones for row-body training and foreign
ownership popups. These zones participate in navigation and dispatch without
adding a second bevel.

## Why Networking remains legacy

The SDL Networking screen is not a normal menu wrapped around a separate
controller:

1. Its return value is an action ID consumed by an internal switch that may
   submit, cancel, refresh, or leave the member function immediately.
2. Room-list completion is staged before input. A click must target the room
   row that was visible when pressed, even if a new list arrives that frame.
3. Idle refresh observes the raw pointer sample, including clicks that hit no
   button.
4. Native and web tables contain different rows at different indices.

Moving it into the runtime would require a pre-input phase and stable ID-based
navigation. Until those facilities have another consumer, its focused race
tests are the safer contract.

## Maintenance

When a runtime-wide obligation is added, audit the Networking loop separately.
Examples include lobby polling, remote-start behavior, dual-surface labels,
hidden-row navigation, level reload, autosave, and ready-state clearing.

The main regression layers are:

- `tests/integration/test_menu_engine.cpp` for runtime and frame behavior;
- `tests/unit/test_menu_spec.cpp` for specification helpers;
- `tests/integration/test_menu_pins.cpp` and `tests/integration/test_menu_layout.cpp` for button tables,
  ordinals, and navigation;
- terminal and WebAssembly flows for client-specific projections.
