# `--show-fps` Design and Audit

Phase 1 audit consuming `.plan/plan.md` as authoritative. Records concrete
file:line locations, the parser extension, the bootstrap read, the overlay
call site, and the pixel rectangle Phase 2's integration test will scan.

## CLI parser audit

`cfg_store::commandline()` lives at `src/resources/gparser.cpp:181-255`. It
only matches single-dash 2-char flags. Today's flow:

- `src/resources/gparser.cpp:181` — function signature
  `void cfg_store::commandline(int &argc, char **&argv)`.
- `src/resources/gparser.cpp:183-193` — `helpmsg` literal listing only `-s`,
  `-S`, `-n`, `-d`, `-e`, `-i`, `-f`, `-h`, `-v`.
- `src/resources/gparser.cpp:205` — the 2-char guard:

  `if(argv[argnum][0] == '-' && std::string(argv[argnum]).size() == 2)`

  Because `std::string("--show-fps").size() == 10`, the guard falls through
  silently for `--show-fps` today — confirming the extension below is needed.
- `src/resources/gparser.cpp:249` — the in-switch `default:` clause logs
  `Log("Unknown argument {} ignored.", argv[argnum])`. Critically this
  default lives **inside** the 2-char guard's `switch`, so it only fires for
  unknown 2-char `-x` arguments. Longer tokens like `--show-fps` never reach
  it.

**Parser extension (Phase 2 will apply):** inside the `for` loop at
`gparser.cpp:202`, **before** the 2-char branch at `:205`, insert:

```cpp
if (std::string(argv[argnum]) == "--show-fps")
{
    data["graphics"]["show_fps"] = "on";
    Log("FPS overlay on.");
    continue;
}
```

`std::string` matches the existing convention at `:205` (no new
`<string_view>` include needed; `<string>` is already in scope through
`gparser.h`/the existing call).

**Help message extension (Phase 2 will apply):** append a new line before the
closing quote of `helpmsg` at `gparser.cpp:183-193`:

```
"  --show-fps   Overlay render FPS in the top-right corner\n"
```

## Bootstrap and session-state plan

`SessionState` debug-flag region lives at
`include/openglad/interface/session_state.h:75-86`. Existing debug flags:

- `bool debug_draw_paths_ = false;` at `:85`
- `bool debug_draw_obmap_ = false;` at `:86`

**SessionState change (Phase 2 will apply):** immediately after
`bool debug_draw_obmap_ = false;` at `include/openglad/interface/session_state.h:86`,
add:

```cpp
bool show_fps_ = false;
```

with a one-line comment that this is a developer overlay drawn by
`score_panel` when set.

**Bootstrap read site:** `src/platform/sdl/glad.cpp` defines
`bootstrap_runtime(int argc, char* argv[])` at `:205-235`. The `target_fps`
sync block runs at `:226-228`:

```cpp
const int fps = og::core::target_fps_from_cfg(cfg);
og::core::apply_target_fps_to_cfg(cfg, fps);
og::runtime::current_session->target_fps_ = fps;
```

The next line at `:229` is `cfg.save_settings();`.

**Bootstrap change (Phase 2 will apply):** after the `target_fps_` assignment
at `src/platform/sdl/glad.cpp:228` and **before** `cfg.save_settings();` at
`:229`, add:

```cpp
og::runtime::current_session->show_fps_ = cfg.is_on("graphics", "show_fps");
```

No `cfg.apply_setting(...)` write-back — the flag is an ephemeral developer
override and must not persist into `cfg/openglad.yaml` on shutdown.

## Render loop and font audit

Render-frame structure in `src/platform/sdl/game_loop.cpp`:

- `src/platform/sdl/game_loop.cpp:47-50` — `default_now_ms()` already wraps
  `SDL_GetTicks()`. The same clock source is reused by `draw_fps_overlay`.
- `src/platform/sdl/game_loop.cpp:232-247` — `render_pending_redraw()` (the
  redraw-pending path). When `enable_render` is true it calls
  `score_panel(&s, 1)` at `:240`, then `s.buffer_to_screen(0, 0, 320, 200)`
  at `:243`. The HUD is the last thing drawn before present.
- `src/platform/sdl/game_loop.cpp:410-422` — the normal render path inside
  `game_frame_with_result`. It calls `score_panel(&s)` at `:420`, then
  `s.refresh()` at `:421`. Same pattern: HUD then present.

Both call sites go through the public `score_panel(screen*)` /
`score_panel(screen*, short)` thunks at `src/interface/score_panel.cpp:299-302`,
which delegate to `new_score_panel(screen* s, short)`. `new_score_panel`
returns at `src/interface/score_panel.cpp:296` with `return 1;`. The FPS
overlay call therefore lives **immediately before** that `return 1;` at
`src/interface/score_panel.cpp:296`, gated on
`og::runtime::current_session && og::runtime::current_session->show_fps_`.
This ensures the overlay is drawn exactly once per render frame for both
the redraw-pending and normal-render paths.

Font + width audit in `src/interface/render/text.cpp`:

- `text::query_width(std::string_view)` at
  `src/interface/render/text.cpp:87-105`. For the small monospaced font
  (`sizex < 9` branch at `:92`), width is
  `(sizex + 1) * string.size()`. `screen::text_normal` (declared at
  `include/openglad/interface/screen.h:257`) loads from `TEXT_1` with
  `sizex == 4`, so the monospaced branch applies. For `"FPS: 60"` (7 chars),
  measured width is `(4+1) * 7 = 35 px`.

`text_normal.write_xy(x, y, msg, color, to_buffer)` writes into the 320×200
software buffer when `to_buffer == 1` (matching the existing HUD calls in
`score_panel.cpp:181-199`), so the overlay composites correctly with the rest
of the HUD before `s.refresh()` / `s.buffer_to_screen(...)` presents.

**Clock source:** `SDL_GetTicks()`, called inside `draw_fps_overlay` so
`<SDL.h>` stays out of `score_panel.cpp`. This mirrors `default_now_ms()` at
`game_loop.cpp:47-50`.

## Overlay placement and color

**Placement:** `y = 2`, `x = 320 - query_width(msg) - 2`. For the canonical
`"FPS: 60"` string this resolves to `x = 320 - 35 - 2 = 283`, well inside
the right edge. Right-aligning by query width keeps single/double/triple-digit
FPS values flush right.

**Color:** `YELLOW` from `<openglad/interface/base.h>` (defined at
`include/openglad/interface/base.h:126` as
`inline constexpr unsigned char YELLOW = 88;`). `score_panel.cpp:15` already
includes that header, but the new overlay TU
(`src/interface/fps_overlay.cpp`) will include it directly.

**Message format:** `std::format("FPS: {}", measured_fps)`, where
`measured_fps` is computed from a rolling 500 ms window of `SDL_GetTicks()`
timestamps. **Warmup edge:** if fewer than 250 ms have elapsed since the
counter was first ticked, return the cumulative-since-start average
(`size * 1000 / max(1, now - first_seen_ms)`) instead of dividing by 500 ms
— this avoids a misleading `FPS: 1` on the very first frame.

**Integration-test pixel rectangle.** In single-player mode (`s->numviews ==
1`), the per-player HUD overlay positions in `new_score_panel` are anchored
at `lm`/`tm`/`rm` derived from viewport coordinates of `s->viewob[0]`. With a
full-screen viewport these anchors sit well below `y = 12` and well to the
left of `x = 280` (the score/HP/MP rows at `score_panel.cpp:181-199` use
`lm+5, tm+11`/`tm+19`/`tm+20` for life text). The strip `y ∈ [0, 12)`,
`x ∈ [280, 320)` is therefore expected to be untouched by the base HUD in
the test fixture.

**Default integration-test rectangle:** **`y ∈ [0, 12)`, `x ∈ [280, 320)`**.

Phase 2 must:

1. Run the test once with `show_fps_ = true` and assert at least one
   non-zero pixel inside that rectangle.
2. Run the test once on a freshly cleared framebuffer with
   `show_fps_ = false` and assert the same rectangle is all zero.
3. If the audit during Phase 2 reveals that the base 1-player HUD does
   touch that strip under the test fixture, narrow the rectangle to a
   strip the base HUD truly does not touch and record the narrower
   coordinates in `.plan/show-fps-implementation.md`. Do not weaken the
   assertion.

## Files to modify in Phase 2

- **Edit** `src/resources/gparser.cpp` — add the `--show-fps` branch before
  the 2-char guard at `:205`; append the `--show-fps` line to `helpmsg` at
  `:183-193`.
- **Edit** `include/openglad/interface/session_state.h` — add
  `bool show_fps_ = false;` immediately after
  `bool debug_draw_obmap_ = false;` at `:86`.
- **Edit** `src/platform/sdl/glad.cpp` — inside `bootstrap_runtime()`, after
  `target_fps_` assignment at `:228` and before `cfg.save_settings();` at
  `:229`, add the `show_fps_ = cfg.is_on("graphics", "show_fps")` read.
- **New file** `include/openglad/interface/fps_overlay.h` — declares
  `void draw_fps_overlay(screen& s);` (no namespace, matching existing
  global HUD helpers).
- **New file** `src/interface/fps_overlay.cpp` — implements
  `draw_fps_overlay` with a function-local `static FpsCounter` (rolling
  500 ms window, ≥ 250 ms warmup using cumulative average) and calls
  `SDL_GetTicks()` internally. Renders `"FPS: <n>"` in `YELLOW` at
  `(320 - query_width - 2, 2)` via `s.text_normal.write_xy(...)` with
  `to_buffer == 1`.
- **Edit** `src/interface/score_panel.cpp` — add
  `#include <openglad/interface/fps_overlay.h>` and
  `#include <openglad/interface/session_state.h>` near the other
  `<openglad/interface/...>` includes at `:15-23`. At the end of
  `new_score_panel`, immediately **before** `return 1;` at `:296`,
  gate-call `draw_fps_overlay(*s)` on
  `og::runtime::current_session && og::runtime::current_session->show_fps_`.
- **Edit** `CMakeLists.txt` — add `${SRC_DIR}/interface/fps_overlay.cpp` to
  every list that currently contains `${SRC_DIR}/interface/score_panel.cpp`
  (lines 140, 381, 483).

## Tests to add in Phase 2

- `tests/test_gparser_unit.cpp` — new `TEST(GparserUnit, gparser_show_fps_flag)`.
  Build argv `{"openglad", "--show-fps"}`, invoke
  `local_cfg.commandline(argc, argv)`, then assert
  `local_cfg.is_on("graphics", "show_fps")` is true. Modeled on the existing
  `gparser_commandline_switches_and_unknown_arg` test at
  `tests/test_gparser_unit.cpp:23-50`.

- `tests/test_glad_hud.cpp` — new
  `TEST(GladHud, fps_overlay_draws_when_enabled)`. Modeled on
  `glad_draw_gems_and_value_bars_smoke` and reusing the existing
  `capture_rendered_frame` helper at `tests/test_glad_hud.cpp:78-92`. Steps:
    1. Set `og::runtime::current_session->show_fps_ = true`; clear the
       framebuffer; call
       `new_score_panel(og::runtime::current_session->myscreen_, 1)`;
       capture the frame; scan the rectangle `y ∈ [0, 12)`,
       `x ∈ [280, 320)` (from the section above) for any non-zero pixel
       and assert at least one was found.
    2. Set `og::runtime::current_session->show_fps_ = false`; clear the
       framebuffer; call `new_score_panel(...)` again; capture the frame;
       assert the same rectangle is all zero.
    3. Always restore `og::runtime::current_session->show_fps_ = false` at
       the end (use a small RAII guard or inline equivalent so an
       assertion failure in step 1 cannot leave the flag set for sibling
       tests).
