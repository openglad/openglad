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
| Main, Difficulty, Game Settings, Display, Effects, Controls, Seat Settings | Runtime |
| Base Camp, Hire, Train, Progress, View Level, Scenario, Matchup | Runtime |
| Company List, Backups, company name entry | Runtime |
| Networking | Legacy `SdlPickerClient` loop |
| View Team and manual Save/Load slots | Retired |

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
5. Run the screen's entry transition.

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

The runtime supports:

- the main menu's initial draw between fade-out and fade-in;
- Base Camp's fade around its cold first frame;
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
