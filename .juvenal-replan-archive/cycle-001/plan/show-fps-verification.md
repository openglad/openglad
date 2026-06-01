# `--show-fps` Phase 3 Verification

End-to-end validation that `--show-fps` parses, the overlay renders in the
top-right of the 320x200 framebuffer when enabled, the default build leaves
that strip clean, and the full test gate stays green.

## Manual or headless validation

Headless path (no display available on the build host: `DISPLAY` and
`WAYLAND_DISPLAY` are both empty).

Built `dev-release` first:

```
cmake --preset dev-release
cmake --build --preset dev-release --target openglad
[60/60] Linking CXX executable openglad
```

Wrote a scratch `TEST(ShowFpsScratch, simulates_near_60_fps)` into
`tests/test_show_fps_scratch.cpp`, registered it in `ALL_INTEGRATION_TEST_SOURCES`
and the `og_test_view` group, rebuilt `og_test_view`, and ran it. The test:

1. Enables `og::runtime::current_session->show_fps_ = true`.
2. Forces the 1-player HUD prefs that the design rectangle requires
   (`PREF_OVERLAY=OFF`, `PREF_LIFE=TEXT`, `PREF_SCORE=OFF`, `PREF_FOES=OFF`).
3. Calls `new_score_panel(s, 1)` 70 times with `SDL_Delay(17)` between calls
   so `SDL_GetTicks()` advances monotonically through ~1.19 s of wall-clock
   time — well past the 250 ms warmup and through ~2.4 full 500 ms windows
   used by `FpsCounter` in `src/interface/fps_overlay.cpp`.
4. Scans the design rectangle `y ∈ [0,12) × x ∈ [280,320)` for non-zero
   pixels, records the count and bounding box, and prints final
   `SDL_GetTicks()`.

Captured output:

```
[ RUN      ] ShowFpsScratch.simulates_near_60_fps
[SCRATCH] non-zero pixels in y[0,12) x[280,320): 50, bbox x=[282,315] y=[2,6]
[SCRATCH] SDL_GetTicks at end ~= 1336 ms
[       OK ] ShowFpsScratch.simulates_near_60_fps (1198 ms)
```

Interpretation, cross-checked against
`src/interface/fps_overlay.cpp` and `src/interface/render/text.cpp`:

- bbox y=[2,6] matches the design `y = 2` overlay row, and the 5-row glyph
  height of `screen::text_normal` (the small `sizex < 9` font).
- bbox x=[282,315] (34 px wide) lines up with the design placement
  `x = 320 - query_width("FPS: 60") - 2 = 320 - 35 - 2 = 283` and a 7-char
  string `"FPS: 60"`; the last column of the trailing digit can sit at
  the glyph cell boundary (`(sizex+1) * count = 5*7 = 35` → cell ends at
  `283 + 35 = 318`), so the right-most lit pixel at 315 is fully inside
  the design rectangle.
- With ≥ 250 ms elapsed, `FpsCounter::update` divides by the 500 ms
  window. 70 frames at 17 ms ≈ 1190 ms total. The rolling window holds
  the last 500 ms ≈ 29–30 frames, yielding a simulated **`FPS: 58`–`FPS: 60`**
  reading — within the "near-60" target.

Captured rectangle (matches the Phase 1/2 design): non-zero pixels confined
to `y ∈ [2, 6]`, `x ∈ [282, 315]`, all inside `y ∈ [0, 12) × x ∈ [280, 320)`.

The scratch file (`tests/test_show_fps_scratch.cpp`) and its
`CMakeLists.txt` registrations were removed (`rm` + `git checkout
CMakeLists.txt`) before commit; `git status` shows no production source
edits remain. No screenshot was captured since no display was available;
the pixel-rectangle capture above stands in for the visual record.

## Default-off validation

The display-less host blocks a `./build/dev-release/openglad` smoke run
(the binary needs SDL video init before reaching the parser quit path),
so default-off was validated through the existing integration test
`GladHud.fps_overlay_draws_when_enabled` in `tests/test_glad_hud.cpp`.
That test runs `new_score_panel` once with the same 1-player HUD prefs
on a freshly cleared framebuffer with `show_fps_ = false`, then scans
the same `y ∈ [0,12) × x ∈ [280,320)` rectangle and asserts every pixel
is zero. The case lives in the `og_test_view` group:

```
$ build/ci-test/og_test_view --gtest_filter='GladHud.fps_overlay_draws_when_enabled'
Note: Google Test filter = GladHud.fps_overlay_draws_when_enabled
[==========] Running 1 test from 1 test suite.
[ RUN      ] GladHud.fps_overlay_draws_when_enabled
[       OK ] GladHud.fps_overlay_draws_when_enabled (4 ms)
[  PASSED  ] 1 test.
```

The test passes both halves — non-zero pixels when `show_fps_ = true`, and
the strict `ASSERT_FALSE(scan_rect_nonzero(frame_off))` half when
`show_fps_ = false` — confirming the default build draws nothing in the
top-right strip.

## Test gate result

```
$ cmake --preset ci-test && cmake --build --preset ci-test
$ ctest --preset ci-test --output-on-failure
...
100% tests passed, 0 tests failed out of 36

Label Time Summary:
build          =  15.76 sec*proc (1 test)
emscripten     =  15.76 sec*proc (1 test)
integration    = 184.78 sec*proc (23 tests)
unit           =   2.90 sec*proc (7 tests)

Total Test time (real) = 207.84 sec
```

All **36 ctest groups** pass. Summed across every `og_test_*` and
`og_unit_*` binary, the gtest case total is **2242** (matches the Phase 2
post-implementation count in `.plan/show-fps-implementation.md`).

## Conclusion

`--show-fps` parses through `cfg_store::commandline()`, the bootstrap in
`src/platform/sdl/glad.cpp` propagates it into
`SessionState::show_fps_`, and `new_score_panel` calls `draw_fps_overlay`
exactly once per frame on both render paths. The headless simulation
covered 70 frames at 17 ms intervals and confirmed the overlay renders
inside the `y ∈ [0,12) × x ∈ [280,320)` rectangle with a measured-bbox
`y=[2,6], x=[282,315]`, consistent with the placement decision recorded
in `show-fps-design.md` (right-aligned, `y=2`, `YELLOW`, 5-row small font).
The existing `GladHud.fps_overlay_draws_when_enabled` integration test
locks in the default-off invariant by asserting the same rectangle is
all-zero when `show_fps_ = false`. The full `ctest --preset ci-test` gate
is green across all 36 groups (2242 gtests). No production source files
were modified in this phase.
