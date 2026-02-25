# Hard Parts and Mitigations

**Part of:** [Component Architecture Plan](README.md)

---

## 1. Splitting screen (Phases 1–3 + 10)

`screen` is THE central class — ~1300 lines, extends `video`, owns both game
state and display state, referenced from everywhere.

**Mitigation (Phases 1–3):** Keep `screen` as a forwarding shell initially.
Add `GameWorld` as a member. Provide forwarding accessors on `screen` so
existing code doesn't break. Phases 1a–3 split the work into four steps
(entity lists → spatial data/queries → tick logic → state flags) to keep
changes bisectable.

**Mitigation (Phase 10):** The `screen` → `video` inheritance must be broken
to move `screen` to the interface layer. `video` splits into an abstract base
(interface) and SDL concrete (platform). GameWorld ownership transfers from
`screen` to `GameSession`. Both changes happen as dedicated sub-steps before
the physical file moves.

## 2. Navigation treasure redesign (Phase 5)

`treasure_family_navigation.cpp` currently makes blocking UI calls
(`yes_or_no_prompt()`) and does save I/O (`sim_save->load/save`) from inside
entity behavior code. This is the worst layering violation.

**Mitigation:** Deferred action queue. The treasure entity becomes a dumb
sensor — it detects "player on exit tile" and emits a
`RequestExitConfirmation` event. The tick finishes normally. The outer layer
(platform) drains the event, shows the prompt, and handles the entire level
transition flow directly. Entity code never re-enters for this decision.

No state machine on the entity, no "pending response" field on GameWorld, no
multi-tick continuation. The exit/withdrawal logic was already an outer-layer
concern — this moves it where it belongs.

## 3. gloader factory pattern (Phase 6)

`gloader` creates entities with graphics — it touches all layers.

**Mitigation:** Use callback injection. Resources provides the loader, interface
provides a `attach_render(walker&, PixieData&)` callback, platform wires them
together. Headless mode passes a no-op callback. GameWorld gets a
`std::function<std::unique_ptr<walker>(Order, int)>` entity_factory callback
that `add_ob()` delegates to.

## 4. Include path churn (Phase 10)

Hundreds of `#include` changes when files move to new directories.

**Mitigation:** Write a migration script. Move one component at a time. Full
ctest after each batch. Use `git mv` to preserve history.

## 5. video/screen inheritance split (Phase 10)

`screen` extends `video` (SDL rendering). Moving `screen` to the interface
layer means `video` must be split into an abstract base and an SDL concrete.
This is a logic change hiding inside a "directory reorganization" phase.

**Mitigation:** Do the video split as a dedicated sub-step before the physical
file moves. The abstract `video` base defines the drawing primitive interface.
Platform provides the concrete SDL (or headless) implementation via constructor
injection or `PlatformBridge`.
