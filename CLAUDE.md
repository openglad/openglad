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
sudo apt-get install cmake ninja-build libsdl2-dev libsdl2-mixer-dev libgtest-dev
```

### Web Build

```bash
source /path/to/emsdk/emsdk_env.sh
./scripts/build_web.sh
cd dist && python3 -m http.server 8080
# Open http://localhost:8080/index.html
```

## Module Structure

The codebase is organized into **5 top-level components**, each a CMake static
library with enforced dependency rules. (Older fine-grained module names — `sim`,
`data`, `entities`, `io`, `runtime`, `render`, `input`, `ui` — survive only as
transitional forwarding-header shims under `src/`; the real build/dependency
boundary is the component.)

```
include/openglad/<component>/   — public headers (stable API)
src/<component>/                — private implementation
third_party/<lib>/              — vendored external libraries
```

### Components

| Component | CMake Target | Purpose |
|-----------|-------------|---------|
| `core` | `og_core` | Pure utilities, math, constants, shared contracts |
| `gameplay` | `og_gameplay` | Deterministic sim + entities (walker family), `GameWorld`, **and the SDL-free networking core** (`GameServer`/`GameClient`/`LobbyServer`/`WorldSnapshot`/`ITransport`) |
| `resources` | `og_resources` | Serialization: levels, saves, config; filesystem/archive/yaml; sprite assets |
| `interface` | `og_interface` | UI/menus, rendering, input translation, level editor |
| `platform` | `og_platform_sdl` | `GameSession`, loop/session bridges, SDL video/audio, lobby clients, local transport shadow |

Two thin transport libraries link the WebSocket vendor: `og_platform_ws_transport`
(native) and `og_platform_emscripten_transport` (browser). Headless binaries
`openglad_text` (client) and `openglad_server` (dedicated host) link the SDL-free
components only.

### Dependency Direction

Dependencies flow inward: `og_platform_sdl → og_interface → og_resources → og_gameplay → og_core`. See `docs/architecture-rules.md` for the full dependency matrix and enforcement details, and `docs/ARCHITECTURE.md` for the networking architecture.

### Key Rules

- **Vendor headers stay in `src/resources/io/`** (PhysFS, libzip, libyaml) and `src/gameplay/` (micropather only). Enforced by `scripts/check_vendor_leaks.sh`.
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
#include <openglad/gameplay/walker.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/video.h>
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
| IXWebSocket 11.4.6 | WebSocket transport for networked multiplayer |
| LodePNG | PNG codec for indexed-color sprite assets |

Version tracking and upgrade policy in `third_party/VENDORED_LIBS.md`.

## Game Flow

```
main() → io_init → GameSession(cfg) → init_input → intro_main
  → picker_main (team selection loop)
    → glad_main → game_frame loop:
        screen::input()              → handle events
        local transport shadow tick → client input + server sim + mirror sync
        screen::redraw()            → render frame
```

On Emscripten: `emscripten_set_main_loop(emscripten_frame_wrapper)` drives a state machine (Picker → Playing → Picker).

## Testing

### Build & Run Tests

```bash
cmake --preset ci-test
cmake --build --preset ci-test
ctest --preset ci-test
```

GoogleTest is a system dependency for native test builds:

```bash
sudo apt-get install libgtest-dev   # Debian/Ubuntu
brew install googletest             # macOS
```

### Test Binaries

| Binary | Description |
|--------|-------------|
| `og_unit_*` | Headless unit group binaries |
| `og_test_*` | SDL integration group binaries |
| `openglad_text` | Headless text client exercised via CTest script entries |

Integration groups use `tests/integration_main.cpp`. Headless unit groups use `tests/unit/unit_main.cpp`.

New integration test source files must be added to `ALL_INTEGRATION_TEST_SOURCES` and assigned to `og_add_test_group(...)` in `CMakeLists.txt`. New headless unit tests should be assigned to `og_add_unit_group(...)`.

### Writing Tests

All native tests use real GoogleTest:
- `TEST(Suite, name)` and `TEST_F(Fixture, name)` for cases and fixtures
- Standard assertions like `ASSERT_TRUE`, `ASSERT_EQ`, `ASSERT_STREQ`, `EXPECT_*`
- Binary-local selection via `--gtest_filter='Suite.*'`
- Order-dependence checks via `--gtest_shuffle`

**Integration tests** (require SDL):

```cpp
#include <gtest/gtest.h>
#include <openglad/interface/screen.h>
#include <openglad/core/test_trace.h>

TEST(MyThing, basic) {
    trace_clear();
    ASSERT_TRUE(condition) << "message on failure";
    ASSERT_EQ(expected, actual) << "message";
}
```

**Headless unit tests** (no SDL):

```cpp
#include <gtest/gtest.h>

TEST(PureLogic, arithmetic) {
    ASSERT_TRUE(1 + 1 == 2);
}
```

### Traces

Game code writes `TRACE("category", "message %d", val)` (no-op in production builds). Tests verify behavior:

```cpp
trace_clear();
// ... trigger game behavior ...
ASSERT_TRUE(trace_contains("category", "substring")) << "expected trace";
```

### Testing Menu UI / Interactive Flows

Menu functions block in event loops. Tests use an injector thread:

```cpp
#include "test_interact.h"
#include "test_input_helpers.h"

static int my_injector_thread(void* data) {
    wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);  // Wait for fadeblack animation
    interact("continue_game");
    return 0;
}

TEST(MenuFlow, main_menu_flow) {
    SDL_Thread* thread = SDL_CreateThread(my_injector_thread, "injector", nullptr);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, NULL);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;
}
```

**Key rules for menu tests:**
- Use `interact("button_id")` to click buttons — don't compute raw coordinates
- Use `wait_for_interactable("id", timeout_ms)` before clicking
- Always `SDL_Delay(750)` after `wait_for_interactable` — `fadeblack()` eats events
- Set `g_picker_max_mainmenu_calls` to limit loop iterations
- Call `cleanup_picker_state()` after the test

### `#ifdef TESTING` Guards

The test build compiles game sources with `-DTESTING`. Use this for:
- `TRACE(...)` calls to instrument code (no-op in production)
- Test-only globals like `g_picker_max_mainmenu_calls`
- Making `exit(0)` calls safe (`quit()` is a no-op under TESTING)
- `main()` in `glad.cpp` is excluded (tests use `tests/integration_main.cpp`)

## Adding New Code

1. Place public headers in `include/openglad/<module>/`
2. Place implementation in `src/<module>/`
3. Add source files to the appropriate `OG_*_SOURCES` list in `CMakeLists.txt`
4. Respect module dependency rules (see `docs/architecture-rules.md`)
5. Prefer `inline constexpr` over `#define` for constants
6. Prefer `enum class` over plain `enum`
7. Write unit tests for pure logic; integration tests for SDL-dependent flows
8. Run `cmake --build --preset ci-test && ctest --preset ci-test` before committing
