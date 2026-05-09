# openglad_demo perf findings — Phase 1

## Reproduction

Environment is **headless SSH** (no `DISPLAY`, no `WAYLAND_DISPLAY`); the
host is `Linux 6.17.0-20-generic` on x86_64 (GCC 13.3.0). Per
`.plan/plan.md` §3 Phase 1 step 2, headless reproduction clamps
`display_w/display_h` to `1280×720` to land on the `4×3 = 12` cell grid
called out in `CMakeLists.txt:1086-1087`.

The clamp is implemented as a Phase 1 instrumentation knob in `main()` of
`src/platform/sdl/demo.cpp` (marked `// TODO(perf): remove in Phase 2`)
and is **gated on the env var `OPENGLAD_DEMO_DISPLAY_CLAMP=WxH`** so it
has zero effect on production runs that go through
`SDL_GetDesktopDisplayMode`.

Build:

```
cmake --preset dev-release
cmake --build --preset dev-release --target openglad_demo
```

Run (12-cell baseline; the canonical reproduction):

```
SDL_VIDEODRIVER=dummy \
OPENGLAD_DEMO_DISPLAY_CLAMP=1280x720 \
./build/dev-release/openglad_demo 2>/tmp/demo.err
```

Two scaling sweeps were also run by varying the clamp to confirm the
bottleneck scales linearly with `num_sessions`:

```
SDL_VIDEODRIVER=dummy OPENGLAD_DEMO_DISPLAY_CLAMP=1920x1080 ./build/dev-release/openglad_demo  # 6×5  = 30  sessions
SDL_VIDEODRIVER=dummy OPENGLAD_DEMO_DISPLAY_CLAMP=3840x2160 ./build/dev-release/openglad_demo  # 12×10 = 120 sessions
```

Steady-state numbers below discard the first 30 frames (warmup /
page-faults / scenario-load tail) and average across at least the next
~270 frames (≥ 5 s of steady-state at every grid).

`SDL_VIDEODRIVER=dummy` is required because the host has no display
attached. Caveat: the dummy SDL renderer makes
`SDL_RenderClear/Copy/Present` and `SDL_UpdateTexture` near-free, so
**these timings are a lower bound** for the real-display present cost.
The real desktop additionally pays vsync (`SDL_RENDERER_PRESENTVSYNC`
in `src/platform/sdl/sai2x.cpp:736`) on `SDL_RenderPresent`, ~16.6 ms
on a 60 Hz display. That cost is additive to — not a substitute for —
the sequential render-loop cost identified below.

The user-reported "~1 fps" steady-state is consistent with their
desktop sizing landing closer to or above the 12×10 = 120-session sweep
below (or with vsync stalls compounding render-loop cost on a real
display). The headless reproduction does not need to hit 1 fps to
identify the scaling root cause; the linear scan-line in the table
extrapolates cleanly into the ≤ 1 fps regime.

## Measurements

All times are per-frame averages over the steady-state window. `frame`
is wall-clock per main-loop iteration, measured **before** the
end-of-loop pacing sleep on `FRAME_PERIOD = 6 × 13600 µs ≈ 81.6 ms`
(`demo.cpp:415-416`). `barrier` is the time between
`start_cv.notify_all()` and the last worker signaling `done_cv`
(`demo.cpp:457-473`). `render_loop` is the sequential per-session
render block (`demo.cpp:475-503`); `render_session` is one cell, with
worst-cell across the steady-state window. `composite` is `SDL_FillRect`
+ per-cell `SDL_BlitSurface` (`demo.cpp:506-515`); `present` is
`SDL_UpdateTexture` + `SDL_RenderClear` + `SDL_RenderCopy` +
`SDL_RenderPresent` (`demo.cpp:517-525`).

| Grid | num_sessions | frame (µs) | compute fps | barrier (µs) | render_loop (µs) | render_session avg / worst (µs) | composite (µs) | present (µs) |
|------|--------------|------------|-------------|--------------|------------------|---------------------------------|----------------|--------------|
| 4×3   | 12  | 17 935 | 55.8 | 7 207  | 9 679  | 805 / 1 257  | 221  | 821   |
| 6×5   | 30  | 39 882 | 25.1 | 10 511 | 27 268 | 908 / 1 339  | 664  | 1 432 |
| 12×10 | 120 | 112 957 | 8.85 | 18 142 | 87 628 | 729 / 2 051 | 2 348 | 4 832 |

(Cells picked from the perf summary lines in `/tmp/demo.err`; numbers
are stable to within ±5 % across the run.)

Pacing-cap effect: the main loop sleeps the remainder of `FRAME_PERIOD
≈ 81.6 ms` (`demo.cpp:589-594`), so any `frame` below 81.6 ms is
re-paced down to ~12.25 fps presented. `frame` above 81.6 ms presents
as fast as compute allows (sleep is skipped). The `4×3` and `6×5`
sweeps are pacing-capped at ~12 fps; the `12×10` sweep at ~8.9 fps is
not pacing-capped.

One-shot startup cost (`init_session_game` over all sessions, in
series, `demo.cpp:381-384`) is not part of the steady-state window. It
shows up before the first `perf[…]` line and grows roughly linearly
with `num_sessions` because each iteration loads `save0`, runs
`spawn_random_player_team`, then `reset_local_transport_shadow`. At
12×10 it is several seconds; at 4×3 it is sub-second. It is a fixed
one-shot tax, not a steady-state contributor, and does not influence
the bottleneck choice.

Bottleneck rule-in/rule-out, against the candidate list in
`.plan/plan.md` §1:

1. **Sequential per-session render on the main thread — RULED IN.**
   `render_loop` ≈ `num_sessions × render_session_avg`:
   `12 × 805 µs ≈ 9.7 ms`, `30 × 908 µs ≈ 27.2 ms`,
   `120 × 729 µs ≈ 87.5 ms` — matches measured `render_loop` to within
   noise. This is the dominant non-`barrier` term and the only term
   that scales linearly with `num_sessions` to multi-tens-of-ms.
   `render_session` per-cell is essentially constant (~700–900 µs)
   across grid sizes.
2. **`SDL_RENDERER_PRESENTVSYNC` on the host renderer — UNVERIFIABLE
   HEADLESS, not the primary bottleneck regardless.** Dummy driver
   `present` is sub-5 ms even at 120 cells. On a real 60 Hz display
   it would add up to ~16.6 ms per frame, which is meaningful but
   does not explain ≥ 100 ms frame times alone. Vsync compounds the
   render-loop cost rather than substituting for it.
3. **`SessionScope::activate()` swaps `E_Screen->render` globally,
   triggered by workers via
   `local_transport_shadow_finish_tick → session.activate()` —
   RULED OUT for *steady-state perf*, RULED IN as a *correctness
   hazard*.** With `enable_render=false` the workers never write
   through `E_Screen->render`, so the swap is dead weight and a
   data race (5 call sites:
   `src/platform/sdl/local_transport_shadow.cpp:800, 965, 1010, 1078,
   1091`). Each swap is two pointer assignments — negligible cost
   per call. It is not the perf root cause but it is the right
   "small additive secondary" to fix while we have the file open.
4. **Composite (`SDL_FillRect` + per-cell `SDL_BlitSurface`) +
   `SDL_UpdateTexture` of the whole composite — RULED OUT at the
   measured grid sizes.** `composite + present` totals 1.0 ms (12),
   2.1 ms (30), 7.2 ms (120). Not the dominant term until the grid
   is much bigger and even then is dwarfed by `render_loop`. The
   real-display present is a separate concern (see #2) but is
   additive, not the linear scaling term.
5. **Initial scenario load** — confirmed one-shot (above), not a
   steady-state contributor.
6. **Display size sprawl** — the unbounded grid sizing is the
   *amplifier* of bottleneck #1. `num_sessions` is auto-derived from
   `display_w / 320 × display_h / 200` (`demo.cpp:306-308`) with no
   cap, despite `CMakeLists.txt:1086-1087` documenting "4×3 = 12" as
   the intended demo size. On a 4K desktop this becomes 120
   sessions, on a 5K+ desktop several hundred — driving the
   sequential render-loop directly into multi-hundred-ms territory
   and matching the user's "~1 fps" report.

## Root cause

`demo.cpp:475-503` renders every session **sequentially on the main
thread**: for each cell it `activate()`s the session, calls
`render_session_frame(*s)` (which does `screen::draw_panels`,
`screen::refresh`, `screen::redraw`, `screen::refresh`), and only then
moves on. Per-cell cost is ~730–910 µs and is approximately constant
across grid sizes; total `render_loop` cost is therefore
`O(num_sessions)` on a single thread.

Because `num_sessions` is derived directly from desktop dimensions
(`demo.cpp:306-308`) with **no cap and no override**, a 4K desktop
yields 120 sessions and the render-loop alone is ~88 ms — already
exceeding the 81.6 ms `FRAME_PERIOD` budget and driving present rate
below the design ~12 fps. On larger desktops (e.g. multi-monitor or
5K+) the same scaling drives the loop into the multi-hundred-ms
range, producing the user's ~1 fps observation.

The worker threads do not contribute to this scaling — they run sim
in parallel and the `barrier` cost is sub-20 ms even at 120
sessions — but they hold a separate, latent **data-race hazard**:
each tick, every worker calls
`local_transport_shadow_finish_tick → session.activate()`
(`src/platform/sdl/local_transport_shadow.cpp:800, 965, 1010, 1078,
1091`), which writes the shared global `E_Screen->render` from
multiple threads concurrently
(`src/platform/sdl/game_session.cpp:286-290`). With
`enable_render=false` the swap is also pure dead weight. This is not
the perf root cause but it is unsound and should be fixed in the
same change.

## Proposed fix

**Primary fix: cap the demo grid to a sensible size in
`main()` of `src/platform/sdl/demo.cpp`.**

- File: `src/platform/sdl/demo.cpp`, function `main(int, char*[])`.
- Change shape: after the existing `grid_cols = display_w / CELL_W;
  grid_rows = display_h / CELL_H;` block at `demo.cpp:306-308`,
  clamp the grid to at most `4 × 3 = 12` cells:
  `grid_cols = std::min(grid_cols, 4); grid_rows = std::min(grid_rows, 3);`
  matching the documented demo intent at `CMakeLists.txt:1086-1087`.
  Allow override via env var `OPENGLAD_DEMO_GRID=COLSxROWS` (parsed
  with `std::sscanf("%dx%d", …)`, both values clamped `≥ 1`) so
  power users can opt back in to a larger grid; if the env var is
  set and parses, it overrides both the desktop-derived sizing and
  the 4×3 clamp. `num_sessions` is then recomputed from the clamped
  `grid_cols × grid_rows`. Keep the existing `Log("Display: …, grid:
  …")` line so the chosen grid is visible. The downstream
  composite/texture/present plumbing is unchanged and continues to
  use `display_w × display_h` for the host window — only the cell
  count shrinks.
- This eliminates the linear scaling term as the dominant cost. At
  12 cells, measured `render_loop ≈ 9.7 ms`; even with a
  real-display vsync stall added on top of `frame ≈ 18 ms`, the
  pacing-cap of `FRAME_PERIOD ≈ 81.6 ms` (~12.25 fps) becomes the
  limiting factor, not compute.

**Secondary fix (small, additive, in scope): kill the worker-side
`E_Screen->render` swap and race.**

- File: `include/openglad/platform/game_session.h`. Extend
  `GameSession::activate()` and the `SessionScope` ctor with a
  `bool swap_render = true` parameter, defaulted to `true` so all
  existing call sites are unchanged.
- File: `src/platform/sdl/game_session.cpp`, functions
  `GameSession::activate()` and `GameSession::SessionScope::SessionScope(GameSession&)`.
  Forward the new parameter; in the ctor, gate the
  `if (session_->session_surface_ && E_Screen) { … E_Screen->render
  = session_->session_surface_; }` block on `swap_render && …`.
  The dtor's `did_swap_render` guard already covers the
  matched-restore path.
- File: `src/platform/sdl/local_transport_shadow.cpp`, function
  `local_transport_shadow_finish_tick(GameSession& session)` and
  the three other worker-thread call sites at lines 800, 965,
  1010, 1078, 1091. Pass `session.activate(/*swap_render=*/false)`
  (and the same for `runtime->server_session->activate(false)` at
  line 1078) so worker threads no longer touch the shared global
  `E_Screen->render`. This is a 5-line change.
- Effect: the worker-side `E_Screen->render` data race documented
  in `.plan/plan.md` §1 is eliminated; per-tick worker overhead
  drops by two pointer writes per session per tick. Perf gain is
  noise; the value is **correctness**.

**Expected post-fix per-stage timings (4×3 = 12 cells, headless dummy
driver, dev-release):**

| Stage | Pre-fix | Expected post-fix | Notes |
|-------|---------|-------------------|-------|
| `barrier`        | 7.2 ms | 7.0–8.5 ms | unchanged, ±noise |
| `render_loop`    | 9.7 ms | 9.5–10.5 ms | unchanged at 12 cells |
| `render_session` | 805 µs avg | ~800 µs avg | unchanged |
| `composite`      | 220 µs | ~220 µs | unchanged |
| `present`        | 820 µs | ~820 µs (dummy); ≤ 16.7 ms (real 60 Hz) | bounded by vsync on real display |
| `frame` (compute) | 18.0 ms | 17–20 ms | unchanged at 12 cells |
| presented FPS | env-dependent (1 fps on user) | **≥ 11 fps** on any reasonable desktop | pacing cap ≈ 12.25 fps |

The pacing cap from `FRAME_PERIOD ≈ 81.6 ms` is the *design* limiter
once compute fits in budget; lifting that cap is **out of scope** for
this fix because it would speed up simulation, not just rendering, and
would require decoupling sim and render — a far larger change. The
demo's documented intent (4×3 grid, sim-paced presentation) is what
this fix delivers.

**Phase 3 FPS target: steady-state presented FPS ≥ 11 fps** on the
4×3 = 12 cell reproduction described in `## Reproduction`. (Headroom
under the ~12.25 fps pacing cap.)

**Behavior tradeoffs:**

- The default visual layout shrinks on large desktops: a 4K user who
  used to see 120 cells now sees 12. They can opt back in via
  `OPENGLAD_DEMO_GRID=12x10` if they want the old behavior. This is
  acceptable because the documented intent of the demo is 4×3, and
  120 cells is unwatchable at any FPS.
- No change to the regular game (`openglad`/`openscen`); the cap is
  in `demo.cpp` only.
- The new `swap_render` parameter is defaulted true, so all
  non-demo, non-worker call sites keep current behavior. Only
  `local_transport_shadow_finish_tick` opts out.

**Disposition of the worker-side `session.activate()` race:** Phase 2
fixes it as part of the secondary fix above (5 call sites in
`local_transport_shadow.cpp`, 1 signature change in
`game_session.h`, 1 ctor change in `game_session.cpp`). Not deferred.
