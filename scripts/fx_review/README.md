# FX review site

Visual review of the multifloor special effects, menus, multiplayer, and the
War of the Westlands and Long Season campaign levels: every animation is
captured from the real engine (real renderer, live `game_frame` simulation,
live palette cycling) and encoded as looping APNGs any browser plays natively.

```bash
scripts/fx_review/generate.sh            # build + capture + encode
python3 -m http.server -d build/fx-review/site 8790
```

Frames and the site are build artifacts — regenerate them; don't commit them.

## How it works

1. **Env-gated capture tests** render P6 PPM frame sequences into
   `$OG_FX_CAPTURE_DIR/<scene>/NNN.ppm`. Without the env var they
   `GTEST_SKIP`, so normal ctest runs are untouched.
   - `RenderEffects.zz_capture_effect_scenes` (`tests/integration/test_render_effects.cpp`)
     — scripted close-ups: one per effect, plus the floor-glide legs and the
     depth-mode comparison set.
   - `GameLoop.zz_capture_real_gameplay` / `zz_capture_splitscreen_gameplay` /
     `zz_capture_epic_battles` / `zz_capture_westlands` /
     `zz_capture_westlands_decor` / `zz_capture_longseason`
     (`tests/integration/test_game_loop.cpp`) — live sim on campaign levels, a real
     2-player split-screen session, and the War of the Westlands story
     levels: the six war epics plus the new showpieces (16 The White City,
     24 The Mountain of Fire) and the High Pass blizzard (5) in spectator
     mode with a mode-seeking cinematic camera, plus the Forest Road flight
     (2) and the Dead Marshes (19) as real gameplay; then the Long Season
     scenes (2 The Ferry Right, 14 The Long Toll, 17 Ashfall Gate, 18 The
     Warm Mint). `OG_FX_CAPTURE_ONLY=<level id>` records a single scene.
   - `OptionsMenu.zz_capture_menu_tour` / `zz_capture_menu_effects` /
     `zz_capture_menu_difficulty`
     (`tests/integration/test_options_menu.cpp`) — injector-driven menu walkthroughs.
2. **TESTING hooks** make the blocking menu flows filmable:
   - `screen::buffer_to_screen` dumps every 3rd presented frame when
     `OG_DUMP_DIR` is set (`src/interface/screen.cpp`).
   - `g_test_menu_nav_key` drives one keyboard-nav step per pulse
     (`src/interface/ui/picker_input.cpp`) so captures show the highlight
     box moving; real key events get eaten by the hold-and-release loops.
   Both hooks are pinned by always-run tests
   (`RenderEffects.capture_dump_hook_writes_ppm_frames`,
   `PickerMenuNav.capture_nav_hook_drives_one_step_and_self_clears`).
3. **`make_site.py`** encodes each scene directory into a 2x-upscaled APNG
   (pure Python, zlib only) and writes the card page. Missing scenes are
   skipped, so partial captures still produce a reviewable site.
