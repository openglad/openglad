# FX review site

Visual review of the multifloor special effects, menus, multiplayer and the
Concept Playground war levels: every animation is captured from the real
engine (real renderer, live `game_frame` simulation, live palette cycling)
and encoded as looping APNGs any browser plays natively.

```bash
scripts/fx_review/generate.sh            # build + capture + encode
python3 -m http.server -d build/fx-review/site 8790
```

Frames and the site are build artifacts — regenerate them; don't commit them.

## How it works

1. **Env-gated capture tests** render P6 PPM frame sequences into
   `$OG_FX_CAPTURE_DIR/<scene>/NNN.ppm`. Without the env var they
   `GTEST_SKIP`, so normal ctest runs are untouched.
   - `RenderEffects.zz_capture_effect_scenes` (`tests/test_render_effects.cpp`)
     — 12 scripted close-ups, one per effect.
   - `GameLoop.zz_capture_real_gameplay` / `zz_capture_splitscreen_gameplay` /
     `zz_capture_epic_battles` (`tests/test_game_loop.cpp`) — live sim on
     campaign levels, a real 2-player split-screen session, and the epic
     levels 605-610 in spectator mode with a mode-seeking cinematic camera.
     `OG_FX_CAPTURE_ONLY=<605..610>` records a single epic level.
   - `OptionsMenu.zz_capture_menu_tour` / `zz_capture_menu_effects`
     (`tests/test_options_menu.cpp`) — injector-driven menu walkthroughs.
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

## TODO

- `net_host`/`net_joiner`: the harness that filmed a genuine networked
  host+joiner session (both screens dumped at the same authoritative tick,
  composed side by side) was lost with its throwaway worktree. Rebuild it
  from the `host_and_join_*` e2e patterns in `tests/test_game_loop.cpp`;
  `make_site.py` already composes and shows the card when
  `anim/net_host` + `anim/net_joiner` exist.
- GitHub Pages publishing of the generated site.
