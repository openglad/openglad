# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

OpenGlad is a cross-platform C++ port of the DOS game "Gladiator" - a top-down gauntlet-style action RPG with multiplayer support and a built-in scenario editor. Licensed under GPL v2.

## Build Commands

Both build scripts bypass autotools and compile directly — source file lists are maintained in the scripts themselves.

### Native Build

**Prerequisites (Debian/Ubuntu):**
```bash
sudo apt-get install libsdl2-dev libsdl2-mixer-dev
```

**Build:**
```bash
./scripts/build_native.sh
```

Produces `openglad` and `openscen` binaries in the project root. Object files go in `build-native/`.

```bash
./openglad    # Run game
./openscen    # Run level editor
```

### Web Build

**Prerequisite: Install Emscripten SDK**
```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh  # Run in each new terminal, or add to shell profile
```

**Build:**
```bash
./scripts/build_web.sh
```

This compiles everything with Emscripten, packages game assets, and outputs to `dist/`:
- `play.html` - HTML shell with canvas
- `play.js` - JavaScript runtime glue
- `play.wasm` - WebAssembly binary
- `play.data` - Packaged game assets

**Run locally:**
```bash
cd dist && python3 -m http.server 8080
# Then open http://localhost:8080/index.html  # or http://localhost:8080/play.html
```

## Architecture

### Core Class Hierarchy

```
walker (base entity class)
├── living (player/AI entities with health, AI behavior)
├── weap (projectiles and weapons)
├── treasure (collectible items)
└── effect (visual effects)

screen (main game world container)
├── viewscreen[] (up to 4 player viewports for split-screen)
├── level_data (current level layout and objects)
└── obmap (spatial indexing for collision detection)

video (SDL2 graphics layer)
└── pixel buffer manipulation, rendering primitives
```

### Key Source Files

| File | Purpose |
|------|---------|
| `src/glad.cpp` | Entry point, main game loop, Emscripten frame wrapper |
| `src/walker.cpp` | Base entity logic (142KB) - movement, combat, behavior |
| `src/screen.cpp` | Game world state, entity management |
| `src/view.cpp` | Viewport/camera rendering (multiplayer split-screen) |
| `src/video.cpp` | SDL2 graphics abstraction, pixel buffer ops |
| `src/picker.cpp` | Team selection/hiring UI (129KB) |
| `src/level_editor.cpp` | Scenario editor - openscen (147KB) |
| `src/input.cpp` | Keyboard/controller handling |
| `src/stats.cpp` | Combat statistics and calculations |

### Emscripten Integration

The web build uses conditional compilation in `src/glad.cpp`:

```cpp
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// Uses emscripten_set_main_loop for browser's requestAnimationFrame
// FrameState struct manages timing to maintain game's intended frame rate
#endif
```

The HTML shell template is at `web/shell.html` - handles canvas scaling, loading UI, and WebGL context.

### Bundled Libraries (src/external/)

- **physfs/** - Virtual filesystem, ZIP archive support
- **micropather/** - A* pathfinding algorithm
- **libyaml/** + **yam/** - YAML configuration parsing
- **libzip/** + **zlib123/** - Compression and archives

### Game Assets

- `pix/` - 235 sprite/tileset files (.pix format)
- `sound/` - Audio files (WAV, OGG)
- `cfg/` - Configuration (openglad.yaml)
- `extra_campaigns/` - Additional game scenarios
- `builtin/` - Core game resources

## Dependencies

**Web build:** Emscripten SDK with SDL2 ports (handled automatically via `-sUSE_SDL=2 -sUSE_SDL_MIXER=2`)

**Native build:** SDL2, SDL2_mixer, C++11 compiler (gcc/g++)

## Web Build Details

The build script (`scripts/build_web.sh`) compiles with these key Emscripten flags:
- `-sUSE_SDL=2 -sUSE_SDL_MIXER=2` - SDL2 ports
- `-sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=67108864` - 64MB initial heap
- `-sASYNCIFY` - Async support for file I/O
- `--preload-file` - Packages cfg/, pix/, sound/, etc. into play.data

Canvas renders at 320x200 base resolution, scaled with integer factors for crisp pixels (`image-rendering: pixelated`).

## Game Flow

1. `main()` → SDL init, create screen
2. `intro_main()` → splash screen
3. `picker_main()` → team selection (loops via `picker_mainmenu_loop()`)
4. `glad_main()` → main game loop
5. `screen->act()` → game logic per frame
6. `screen->redraw()` → render to display

## Testing

### Build & Run

```bash
./scripts/build_test.sh   # Compiles with -DTESTING, outputs openglad_test
./openglad_test            # Runs all tests headless (SDL_VIDEODRIVER=offscreen)
```

Tests live in `tests/`, test infra in `tests/test_framework.h`. New test source files must be added to `TEST_SOURCES` in `scripts/build_test.sh`.

### Writing Tests

**Basic structure:** Write a void function, use `TEST_ASSERT` macros, register it:

```cpp
#include "graph.h"
#include "test_trace.h"
#include "test_framework.h"

void test_my_thing() {
    TEST_ASSERT(condition, "message on failure");
    TEST_ASSERT_EQ(expected, actual, "message");
}
REGISTER_TEST(test_my_thing);
```

**Traces:** Game code writes `TRACE("category", "message %d", val)` (no-op in non-test builds). Tests verify behavior with `trace_contains("category", "substring")` and `trace_count("category")`. Call `trace_clear()` at the start of a test.

### Testing Menu UI / Interactive Flows

Menu functions (`mainmenu`, `create_team_menu`, etc.) block in event loops. To test them, spawn a thread that drives navigation while the main thread runs the blocking menu code.

**Pattern** (see `test_level_progress.cpp` and `test_back_to_mainmenu.cpp`):

```cpp
#include "test_interact.h"   // has_interactable, wait_for_interactable, interact
#include "test_input_helpers.h"  // inject_click, inject_key_press (low-level)

static int my_injector_thread(void* data) {
    // Wait for button to exist + extra delay for fadeblack to finish
    wait_for_interactable("continue_game", 5000);
    SDL_Delay(1500);

    // Click it by ID -- handles coordinate conversion automatically
    interact("continue_game");

    // Wait for next screen's buttons to appear
    SDL_Delay(500);
    wait_for_interactable("back", 10000);
    SDL_Delay(1500);

    interact("back");
    return 0;
}

void test_my_menu_flow() {
    // Set up save data, etc.
    SDL_Thread* thread = SDL_CreateThread(my_injector_thread, "injector", &state);

    // picker_mainmenu_loop has a test-only iteration limit to prevent infinite loops
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;  // exit after 1 mainmenu iteration

    picker_main(0, NULL);  // blocks until menus unwind
    SDL_WaitThread(thread, &result);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;  // reset
}
REGISTER_TEST(test_my_menu_flow);
```

**Key rules for menu tests:**
- Use `interact("button_id")` to click buttons — don't compute raw coordinates
- Use `wait_for_interactable("id", timeout_ms)` before clicking — buttons aren't ready until `init_buttons` runs
- Always `SDL_Delay(1500)` after `wait_for_interactable` — `fadeblack()` eats events during the fade animation
- Set `g_picker_max_mainmenu_calls` to limit loop iterations so the test exits
- `quit()` is a no-op under `TESTING` (doesn't call `exit(0)`)
- Clean up picker globals after the test (delete backdrops, allbuttons, pixies) — see `cleanup_picker_state()` in existing tests

### `#ifdef TESTING` Guards

The test build compiles the same game sources with `-DTESTING`. Use this for:
- `TRACE(...)` calls to instrument code (no-op in production)
- Test-only globals like `g_picker_max_mainmenu_calls` for loop control
- Making `exit(0)` calls safe (e.g. `quit()` skips cleanup and exit under TESTING)
- `main()` in `glad.cpp` is excluded (test has its own `main` in `test_main.cpp`)
