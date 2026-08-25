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
visible but have no action, and draw with a dimmed face. That dim shade
collides with the palette entry the button shadow uses, so a dimmed row also
steps its right and bottom bevels one shade darker (`vbutton::dimmed`);
without it a disabled row loses two of its four bevels and reads as a
half-drawn card.

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

- `MenuScreenKind::Screen` entered at menu-stack depth 1 fades: whatever is
  still on the window fades out (a no-op when the previous screen already
  faded itself out — see ownership below), the cold first frame is composed,
  `fadeblack(1)` presents it, and the screen fades its own last frame out
  again at its exit. Every main-menu door enters this way — GAME SETTINGS,
  HELP, the LOAD list, BEGIN NEW GAME's name entry, CONTINUE's Base Camp —
  and so does the main menu itself behind each BACK. That symmetry is the
  rule's whole point: a door that faded only on the way back was the bug
  (#237).
- A nested `run_menu_screen` call (any subscreen door under an open screen)
  never fades; its cold frame is composed and presented directly. Base Camp's
  strip and roster doors, settings' own subscreens, and the company list's
  BACKUPS view are all instant, both ways.
- Two doors sit on neither side of that line and take a one-shot
  `note_menu_entry_fade()` override (swallowed by every entry, so an unused
  one cannot leak forward):
  - **Nested main-menu doors** — CLOUD SAVES and the LEVEL EDITOR run inside
    the still-open main menu, at a depth the rule reads as "subscreen".
    `run_nested_menu_door()` notes `Fade`, so the nested entry fades the
    still-open menu out and its own first frame in; the nested screen's exit
    fades it out again, and the parent loop's next present finds a black
    window and fades back in. CLOUD SAVES gets all of that from the runner;
    the level editor runs its own loop, so it holds a `LegacyMenuFade` and
    calls `end()` before its post-loop teardown touches the buffer.
  - **NETWORKING** — a Base Camp strip door, but Base Camp *exits* before its
    legacy screen runs, so the rule would fade both legs. The click intercept
    notes `Instant` before Base Camp exits (its exit scope skips the fade-out
    on a pending `Instant` and traces `exit fade skipped: Instant note
    pending` — the networking flow pins hold that trace to exactly one
    occurrence, this door), `configure_networking` notes it again on the way
    in and on a non-hookup return, keeping the door as snappy as its
    HIRE/TRAIN/DIFFICULTY siblings. An `Instant` note suppresses only that
    entry's fade-in: Base Camp re-entered under it still owns its exit
    fade, so BACK to the main menu fades like every boundary crossing
    (only a fresh `Instant` pending at that exit skips it). A successful hookup leaves the
    lobby-config context, so that exit is the legacy screen's own: it calls
    `LegacyMenuFade::fade_out_at_exit()` and fades its last frame out
    (instant-in, fade-out-to-Base-Camp), and Base Camp's fading re-entry
    finds the black window it expects.
- `MenuScreenKind::Overlay` (the pause family) never fades at any depth, and
  neither does any other non-fading entry whose loop finds a black window: the
  loop's fade-in is gated on the entry's own fade scope, so only a screen
  whose exit will fade out again ever fades in.
- Legacy full-screen presenters outside the runner — campaign select, the
  results panel, the level editor, the campaign intro scroller, NETWORKING —
  apply the same rule by hand through `og::ui::LegacyMenuFade`, which honors
  the same override.

### Ownership: whoever fades in fades out

A fade-out belongs to the outgoing screen and runs **at that screen's own
exit, while its last presented frame is still the render buffer** —
`run_menu_screen` holds an RAII scope whose destructor fades out on every
return path, and a legacy presenter's `LegacyMenuFade` does the same (its
`end()` runs the fade early where later teardown would touch the buffer or
switch the active canvas). The fade-out is never deferred to the incoming
screen. That deferral is what the two shipped hard cuts (naming a new company,
dismissing the campaign intro) and the HELP regression before them had in
common: between the outgoing screen's last present and its deferred fade-out
sat door-site code — setup, `clear()`s, mounts — and one clear was enough to
turn the fade into black-to-black. `fadeblack(0)` reads the **buffer**, not
the window, so nothing may write the buffer between a screen's last present
and its fade-out; with the fade at the exit, nothing can.

The video layer is the single source of truth for "the window is black"
(`screen::window_is_black()`: set by a completed `fadeblack(0)`, cleared by
every present through `Screen::swap`, true from window creation). A
`fadeblack(0)` on a black window is a no-op that never reaches `FadeBetween`,
so a teardown that already faded out (the two gameplay exits, the intro's
last page, the results panel) never plays a second, black-to-black fade, and
no screen has to be told about it. Purely technical fades that mask loads and
canvas resets (`glad_init`, the gameplay teardowns) are not entry transitions
and stay where they are; idempotency makes them harmless when the last screen
already faded.

Three TESTING invariants back the rule. Each records a violation
(`og::video_testing::g_fade_violations`, traced as `("video", "FADE VIOLATION:
...")`), and `integration_main`'s listener fails the running test on every one,
so every flow test in the tree is an oracle. Exactly these three things fire:

- `fadeblack(1)` on a window that is not black — **"fade-in without a
  fade-out"** (the video layer, at the fade);
- `fadeblack(0)` when the render buffer differs from what the window last
  showed — **"fade-out from a frame that was never presented"** (the video
  layer, at the fade). "Showed" is the rect each present *declared*:
  `Screen::swap` snapshots only its `x,y,w,h` (a full-frame present snapshots
  everything), so a clear, a stale redraw, or a draw outside every present's
  rect all count. The nearest present path happens to upload the whole
  surface; the SAI/Eagle path refreshes only the rect of its scaled scratch —
  the declared rect is the honest lower bound and callers are held to it;
- a fading **entry** — `run_menu_screen` at depth 1, or an active
  `LegacyMenuFade` — that finds the window not black — **"entry found an
  unfaded window: the previous surface exited without its fade-out (<screen
  name>)"** (the runner, before its entry fade-out). The entry still fades
  out, because production must never hard-cut; under TESTING the missing exit
  fade is the failure. A nested main-menu door under a `Fade` note is the one
  entry exempt by definition: its parent has not exited, and fading the
  still-open parent out is the door's own job.

Nothing else is checked. A missing fade-in, or a fade at a door the depth rule
says is instant, shows up only in the per-leg count pins.

The entry invariant is what makes the deferred fade-out impossible to
reintroduce silently: an entry's own `fadeblack(0)` is a no-op on the black
window a correct exit leaves, so a screen that forgets its exit fade would
otherwise be faded out by the *next* entry — invisibly. Every producer is
therefore ownership-correct rather than exempted: the intro fades the loading
dialog out (and the intro-skipped web start fades it out where the skip
happens), the networking screen fades itself out on hookup, the results panel
fades the mission's tail (its last frame with the ending popup dismissed) out
before its own entry, and the test boundary leaves the window **black** —
what an exit fade leaves and what a process starts with — so a direct-call
test that presents a frame of its own owes that frame's fade-out like any
other screen.

The fourth guard is static: every function under `src/` that calls
`fadeblack(` is listed in `scripts/fadeblack_sites.txt` as
`<path>::<function>=<count>` with its reason, and
`scripts/check_fadeblack_sites.sh` (a build dependency of `og_interface` and
`og_platform_sdl`) fails the build on an unlisted function, on a listed
function whose call count changed, and on a listed function that no longer
fades. The count is the point: a door-site fade planted beside an existing
teardown fade in `go_menu` or `glad_init` is exactly the shape the rule
forbids, and a per-function count is what makes it fail to build. Keys carry
no line numbers, so editing around a fade never rots the list.

The demo compositor (`demo.cpp`) presents through its own `SDL_RenderPresent`
and never fades; it is documented here rather than hooked.

Fades are instant under TESTING (`FadeBetween` blits once and traces instead
of animating), so injector `SDL_Delay(750)` waits are generic settles, not
fade timing. Two TESTING hooks keep the campaign-intro leg inside every BEGIN
NEW GAME flow: the un-driven campaign browser accepts the current campaign one
presented frame in (`campaign_picker_testing_set_auto_accept`, re-armed at
every test start; a test that drives the browser disarms it — every
`campaign_picker_testing_input_reset()` does — and with nothing listed it
returns empty at once rather than blocking), and the un-driven text scroller
dismisses itself after its first frame unless a test forces the real view
(`help_testing_set_force_scroll_text`).

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
