# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

OpenGlad is a cross-platform C++ port of the DOS game "Gladiator" — a top-down gauntlet-style action RPG with up to 4-player split-screen multiplayer and a built-in scenario editor. Licensed under GPL v2.

See [ARCHITECTURE.md](docs/ARCHITECTURE.md) for full codebase documentation.

## Build Commands

The primary build system is **CMake 3.25+** with presets. The project targets **C++20**.

### Quick Build & Test

```bash
cmake --preset ci-test
cmake --build --preset ci-test
ctest --preset ci-test
```

### Development Build

```bash
cmake --preset dev-debug
cmake --build --preset dev-debug
./build/dev-debug/openglad    # Run game
./build/dev-debug/openscen    # Run level editor
```

### All Presets

| Preset | Purpose |
|--------|---------|
| `dev-debug` | Debug build with tests (Ninja) |
| `dev-release` | Optimized build (RelWithDebInfo) |
| `ci-test` | CI standard build + all tests |
| `ci-asan` | ASan + UBSan sanitizer build |
| `web-emscripten` | Emscripten WebAssembly build |

### Dependencies (Debian/Ubuntu)

```bash
sudo apt-get install cmake ninja-build libsdl2-dev libsdl2-mixer-dev
```

### Web Build

```bash
source /path/to/emsdk/emsdk_env.sh
./scripts/build_web.sh
cd dist && python3 -m http.server 8080
# Open http://localhost:8080/index.html
```

## Module Structure

The codebase is organized into 11 modules, each a separate CMake static library with enforced dependency rules.

```
include/openglad/<module>/   — public headers (stable API)
src/<module>/                — private implementation
third_party/<lib>/           — vendored external libraries
```

### Modules

| Module | CMake Target | Purpose |
|--------|-------------|---------|
| `core` | `og_core` | Pure utilities, math, logging, constants |
| `sim` | `og_sim` | Deterministic headless simulation |
| `data` | `og_data` | Data type definitions (PixieData, etc.) |
| `resources` | `og_resources` | File I/O serialization: levels, saves, config, content loading |
| `entities` | `og_entities` | Game objects: walker, living, weap, treasure, effect |
| `io` | `og_io` | Filesystem abstraction (PhysFS, libzip, libyaml) |
| `runtime` | `og_runtime` | Game session, context, loop, screen |
| `render` | `og_render` | SDL2 graphics, viewports, text, sprites |
| `input` | `og_input` | Keyboard/controller, UI buttons |
| `ui` | `og_ui` | Menus, picker, level editor, intro |
| `platform` | `og_platform` | Audio (SDL_mixer) |

### Dependency Direction

Dependencies flow inward: `ui → runtime → sim → core`. See `docs/architecture-rules.md` for the full dependency matrix and enforcement details.

### Key Rules

- **Vendor headers stay in `src/io/`** (PhysFS, libzip, libyaml) and `src/entities/` (micropather only). Enforced by `scripts/check_vendor_leaks.sh`.
- **RAII ownership.** `GameSession` is the root owner of screen, prefs, RNG. Use `std::unique_ptr<T>` for owning pointers, `T&`/`T*` for non-owning borrows.

## Key Data Structures

```
walker (base entity) → living, weap, treasure, effect
screen (game world)  → level_data, save_data, viewscreen[4]
GameSession (RAII root) → screen, prefs, GameContext
GameContext (DI container) → screen*, prefs*, cfg*, IRandom*, InputState
```

### Include Patterns

```cpp
#include <openglad/entities/walker.h>
#include <openglad/runtime/screen.h>
#include <openglad/render/video.h>
#include <openglad/core/util.h>
```

## Vendored Libraries (third_party/)

| Library | Purpose |
|---------|---------|
| PhysFS 3.2.0 | Virtual filesystem, archive mounting |
| zlib 1.3.1 | Compression (used by libzip) |
| libzip 1.11.3 | ZIP archive I/O for campaigns |
| libyaml 0.2.5 | YAML parser for configuration |
| yam 0.1.0 | C++ adapter over libyaml |
| MicroPather | A* pathfinding |

Version tracking and upgrade policy in `third_party/VENDORED_LIBS.md`.

## Game Flow

```
main() → io_init → GameSession(cfg) → init_input → intro_main
  → picker_main (team selection loop)
    → glad_main → game_frame loop:
        screen::input()  → handle events
        screen::act()    → game logic (walker::act for each entity)
        screen::redraw() → render frame
```

On Emscripten: `emscripten_set_main_loop(emscripten_frame_wrapper)` drives a state machine (Picker → Playing → Picker).

## Testing

### Build & Run Tests

```bash
cmake --preset ci-test
cmake --build --preset ci-test
ctest --preset ci-test
```

### Test Binaries

| Binary | Description |
|--------|-------------|
| `og_unit_tests` | Headless unit tests — no SDL init |
| `openglad_test` | Full integration suite (~894 tests) |
| `og_data_tests` | Data/IO module tests |
| `og_runtime_tests` | Runtime module tests |

New test source files must be added to the appropriate list in `CMakeLists.txt` (see `TEST_SOURCES`, `DATA_TEST_SOURCES`, `RUNTIME_TEST_SOURCES`).

### Writing Tests

**Integration tests** (require SDL, use `test_framework.h`):

```cpp
#include <openglad/runtime/screen.h>
#include <openglad/legacy/test_trace.h>
#include "test_framework.h"

void test_my_thing() {
    trace_clear();
    TEST_ASSERT(condition, "message on failure");
    TEST_ASSERT_EQ(expected, actual, "message");
}
REGISTER_TEST(test_my_thing);
```

**Headless unit tests** (no SDL, use `tests/unit/unit.h`):

```cpp
#include "unit.h"

OG_UNIT_TEST(test_pure_logic) {
    OG_ASSERT(1 + 1 == 2);
}
```

### Traces

Game code writes `TRACE("category", "message %d", val)` (no-op in production builds). Tests verify behavior:

```cpp
trace_clear();
// ... trigger game behavior ...
TEST_ASSERT(trace_contains("category", "substring"), "expected trace");
```

### Testing Menu UI / Interactive Flows

Menu functions block in event loops. Tests use an injector thread:

```cpp
#include "test_interact.h"
#include "test_input_helpers.h"

static int my_injector_thread(void* data) {
    wait_for_interactable("continue_game", 5000);
    SDL_Delay(1500);  // Wait for fadeblack animation
    interact("continue_game");
    return 0;
}

void test_my_menu_flow() {
    SDL_Thread* thread = SDL_CreateThread(my_injector_thread, "injector", nullptr);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, NULL);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;
}
REGISTER_TEST(test_my_menu_flow);
```

**Key rules for menu tests:**
- Use `interact("button_id")` to click buttons — don't compute raw coordinates
- Use `wait_for_interactable("id", timeout_ms)` before clicking
- Always `SDL_Delay(1500)` after `wait_for_interactable` — `fadeblack()` eats events
- Set `g_picker_max_mainmenu_calls` to limit loop iterations
- Call `cleanup_picker_state()` after the test

### `#ifdef TESTING` Guards

The test build compiles game sources with `-DTESTING`. Use this for:
- `TRACE(...)` calls to instrument code (no-op in production)
- Test-only globals like `g_picker_max_mainmenu_calls`
- Making `exit(0)` calls safe (`quit()` is a no-op under TESTING)
- `main()` in `glad.cpp` is excluded (tests use `test_main.cpp`)

## Adding New Code

1. Place public headers in `include/openglad/<module>/`
2. Place implementation in `src/<module>/`
3. Add source files to the appropriate `OG_*_SOURCES` list in `CMakeLists.txt`
4. Respect module dependency rules (see `docs/architecture-rules.md`)
5. Prefer `inline constexpr` over `#define` for constants
6. Prefer `enum class` over plain `enum`
7. Write unit tests for pure logic; integration tests for SDL-dependent flows
8. Run `cmake --build --preset ci-test && ctest --preset ci-test` before committing
