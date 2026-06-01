# `--show-fps` Implementation

## Edited / added lines

- `src/resources/gparser.cpp:192` — appended `"  --show-fps   Overlay render FPS in the top-right corner\n"` to `helpmsg`.
- `src/resources/gparser.cpp:204-209` — added `--show-fps` branch before the 2-char guard (sets `data["graphics"]["show_fps"] = "on"`).
- `include/openglad/interface/session_state.h:87-88` — added `bool show_fps_ = false;` after `debug_draw_obmap_`.
- `src/platform/sdl/glad.cpp:229` — added `og::runtime::current_session->show_fps_ = cfg.is_on("graphics", "show_fps");` between `target_fps_` and `cfg.save_settings()`.
- `src/interface/score_panel.cpp:16,19` — included `<openglad/interface/fps_overlay.h>` and `<openglad/interface/session_state.h>`.
- `src/interface/score_panel.cpp:298-303` — gated `draw_fps_overlay(*s)` before `return 1;`.
- `CMakeLists.txt:141,383,485` — added `${SRC_DIR}/interface/fps_overlay.cpp` after each `score_panel.cpp` entry.

## New translation units

- `include/openglad/interface/fps_overlay.h` — declares `void draw_fps_overlay(screen&)`.
- `src/interface/fps_overlay.cpp` — implements rolling-500ms `FpsCounter`, ≥250ms warmup uses cumulative-average, draws `"FPS: N"` in `YELLOW` at `(320 - query_width(msg) - 2, 2)` via `text_normal.write_xy` with `to_buffer == 1`.

## New tests

- `tests/test_gparser_unit.cpp` — `TEST(GparserUnit, gparser_show_fps_flag)`.
- `tests/test_glad_hud.cpp` — `TEST(GladHud, fps_overlay_draws_when_enabled)`.

## Pixel rectangle

Used the default from `.plan/show-fps-design.md` § Overlay placement and color: `y ∈ [0, 12)`, `x ∈ [280, 320)`. Test clears the framebuffer before each call and sets `PREF_OVERLAY/SCORE/FOES = OFF`, `PREF_LIFE = TEXT` so the base 1-player HUD never reaches the strip; assertion remains "≥ 1 non-zero pixel when on, all zero when off".

## Test count

Before: 2240. After: 2242. All 36 ctest groups pass under `ci-test`.

## Default behavior

Without `--show-fps`, `cfg.is_on("graphics", "show_fps")` returns false, `SessionState::show_fps_` stays `false`, `new_score_panel` skips `draw_fps_overlay`, and no overlay is drawn — unchanged from baseline.
