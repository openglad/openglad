# Phase 1 — Add FPS configuration plumbing

## Phase Name
`add-fps-config`

## Implement Phase ID
`phase_1_add_fps_config`

## Preexisting Inputs
- `.plan/goal.md` — original one-line goal.
- `.plan/plan.md` — full implementation plan (read in place; do not regenerate).
- `include/openglad/resources/gparser.h` (`cfg_store::get_setting` line 31, `apply_setting` line 30, `load_settings` line 26, `save_settings` line 28).
- `cfg/openglad.yaml` — canonical config (YAML; already contains a `graphics:` map with `fullscreen`, `overscan_percentage`, `render`).
- `src/platform/sdl/glad.cpp:200` — `initialize_runtime_config` calling `cfg.load_settings()`.
- `src/platform/sdl/glad.cpp:204-230` — `bootstrap_runtime` reading from already-loaded cfg, persisting back via `cfg.save_settings()` at line 216, mirroring `overscan_percentage` at lines 220-224.
- `include/openglad/gameplay/net_constants.h` (`DEFAULT_SIM_TICK_MS` line 9, `TIMER_WAIT_TO_MS` line 10).
- `include/openglad/interface/session_state.h:76` — `g_game_speed_factor_` field; new field added adjacent.
- `CMakeLists.txt` — existing `og_add_unit_group(og_unit_sim …)` block (around line 1762) and `OG_CORE_SOURCES` list.

## New Outputs
- New header `include/openglad/core/frame_rate_config.h` exporting:
    - `inline constexpr int kDefaultTargetFps = 72;`
    - `inline constexpr int kMinTargetFps = 10;`
    - `inline constexpr int kMaxTargetFps = 240;`
    - `int target_fps_from_cfg(cfg_store& cfg);` (clamps & defaults)
    - `void apply_target_fps_to_cfg(cfg_store& cfg, int fps);`
    - `std::uint32_t target_frame_interval_ms(int fps);` (defined inline; rounds half-up; safe-clamps to `>= 1`).
- New impl `src/core/frame_rate_config.cpp` (added to `OG_CORE_SOURCES` in `CMakeLists.txt`).
- New unit tests `tests/unit/test_frame_rate_config.cpp`.
- **New** `og_add_unit_group(og_unit_core …)` block in `CMakeLists.txt`, sibling to the existing `og_add_unit_group(og_unit_sim …)` block (around line 1762). Contains only `tests/unit/test_frame_rate_config.cpp` initially; later phases extend it.
- `cfg/openglad.yaml`: add `target_fps: 72` under `graphics:`, preserving alphabetical ordering with `fullscreen`, `overscan_percentage`, `render`.

## File Changes
- Create `include/openglad/core/frame_rate_config.h`, `src/core/frame_rate_config.cpp`, `tests/unit/test_frame_rate_config.cpp`.
- `CMakeLists.txt`:
    - Add `${SRC_DIR}/core/frame_rate_config.cpp` to `OG_CORE_SOURCES` (alongside `${SRC_DIR}/core/frame_pacing.cpp`).
    - Add new `og_add_unit_group(og_unit_core FILES ${CMAKE_SOURCE_DIR}/tests/unit/test_frame_rate_config.cpp)` block adjacent to `og_unit_sim` (around line 1762).
- `src/platform/sdl/glad.cpp` `bootstrap_runtime()` (after the overscan block at lines 220-224):
    ```cpp
    const int fps = og::core::target_fps_from_cfg(cfg);
    og::core::apply_target_fps_to_cfg(cfg, fps);
    og::runtime::current_session->target_fps_ = fps;
    ```
  `apply_setting` writes the canonical clamped value back so the next save persists it. Move `cfg.save_settings()` to immediately after the new `apply_setting` call so both overscan and `target_fps` are flushed in one save, mirroring the existing overscan pattern.
- `include/openglad/interface/session_state.h`: add `int target_fps_ = 72;` adjacent to `g_game_speed_factor_` at line 76. Default literal matches `og::core::kDefaultTargetFps`; do not include the new header here (avoid circular include) — keep the literal default in sync with the constant via a `static_assert` in `frame_rate_config.cpp`.
- `cfg/openglad.yaml`: add `target_fps: 72` under `graphics:`.

## Implementation Details
- YAML setting key path: `("graphics", "target_fps")`. Reader returns the parsed int clamped to `[kMinTargetFps, kMaxTargetFps]`; missing or unparseable falls back to `kDefaultTargetFps`. Writer always serializes a base-10 integer with no leading zeros via `cfg.apply_setting("graphics", "target_fps", std::to_string(fps))`.
- `target_frame_interval_ms(int fps)` returns `std::max<std::uint32_t>(1u, static_cast<std::uint32_t>((1000 + fps / 2) / fps))`. Document that this rounds half-up so 72 fps → 14 ms and 60 fps → 17 ms.
- The session field is read-only after `bootstrap_runtime()` for this phase. Future runtime adjustments go through a setter that re-applies to `cfg` (out of scope here).

## Verification Phases
- **Phase ID:** `phase_1_check`
    - **Type:** `check`
    - **bounce_target:** `phase_1_add_fps_config`
    - **Purpose:** confirm the new `target_fps` knob is read from `cfg`, defaulted, clamped, and exposed as a typed accessor; confirm the new `og_unit_core` group exists and links.
    - **Commands inline:**
        - `cmake --preset ci-test`
        - `cmake --build --preset ci-test --target og_unit_core`
        - `ctest --preset ci-test --output-on-failure -R 'FrameRateConfig'`

## Success Criteria
- `target_fps_from_cfg()` is unit-tested for default, clamp-low, clamp-high, parse-failure, and persistence round-trip.
- `cmake --build --preset ci-test --target og_unit_core` succeeds.
- The broader test suite still compiles.
- `cfg/openglad.yaml` contains `target_fps: 72` under `graphics:`.

## Git Commit Requirement
Before yielding control, the implementer MUST stage and commit all changes with `git add -A && git commit -m "add-fps-config: <change>"`. Do not yield with a dirty working tree.
