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
| `ci-tsan` | ThreadSanitizer build |
| `ci-coverage` | Coverage-instrumented build (the coverage gate) |
| `ci-fuzz` | libFuzzer targets |
| `web-emscripten` | Emscripten WebAssembly build |

### Dependencies (Debian/Ubuntu)

```bash
sudo apt-get install cmake ninja-build libgtest-dev libssl-dev
```

OpenSSL (`libssl-dev`) is required by the native WebSocket transport: the
default multiplayer relay lives on Cloudflare (`https://` room create,
`wss://` room sockets).

SDL3 is auto-fetched (pinned `release-3.4.8`) when no system sdl3 is found;
install `libsdl3-dev` (where available) or nix `sdl3` to use a system copy.

### Web Build

```bash
source /path/to/emsdk/emsdk_env.sh
./scripts/build_web.sh
cd dist && python3 -m http.server 8080
# Open http://localhost:8080/index.html
```

## Module Structure

The codebase is organized into **5 top-level components**, each a CMake static
library with enforced dependency rules. The component is the only build and
dependency boundary. (Older fine-grained module names — `sim`, `data`,
`entities`, `io`, `runtime`, `render`, `input`, `ui` — are gone entirely; every
header lives at its canonical component path.)

```
include/openglad/<component>/   — public headers (stable API)
src/<component>/                — private implementation
docs/external-dependencies.md   — package targets and FetchContent pins
```

### Components

| Component | CMake Target | Purpose |
|-----------|-------------|---------|
| `core` | `og_core` | Pure utilities, math, constants, shared contracts |
| `gameplay` | `og_gameplay` | Deterministic sim + entities (walker family), `GameWorld`, **and the SDL-free networking core** (`GameServer`/`GameClient`/`LobbyServer`/`WorldSnapshot`/`ITransport`) |
| `resources` | `og_resources` | Serialization: levels, saves, config; filesystem/archive/yaml; sprite assets |
| `interface` | `og_interface` | UI/menus, rendering, input translation, level editor |
| `platform` | `og_platform_sdl` | `GameSession`, loop/session bridges, SDL video/audio, lobby clients, local transport shadow |

Two thin transport libraries link the WebSocket dependency: `og_platform_ws_transport`
(native) and `og_platform_emscripten_transport` (browser). Headless binaries
`openglad_text` (client) and `openglad_server` (dedicated host) link the SDL-free
components only.

### Dependency Direction

Dependencies flow inward: `og_platform_sdl → og_interface → og_resources → og_gameplay → og_core`. See `docs/ARCHITECTURE.md` ("Dependency Direction Rules", "Include Boundary Rules", "Enforcement") for the full dependency matrix and enforcement details, and the same document for the networking architecture.

### Key Rules

- **External dependency headers stay behind implementation boundaries.** PhysFS,
  libzip, libyaml, and lodepng are isolated in resources IO. Enforced by
  `scripts/check_vendor_leaks.sh`. A* pathfinding is first-party
  (`og::pathfinding::AStar` in gameplay), not a vendored library.
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

## External Dependencies

| Library | Purpose |
|---------|---------|
| PhysFS | Virtual filesystem, archive mounting |
| zlib | Compression (used by libzip) |
| libzip | ZIP archive I/O for campaigns |
| libyaml 0.2.5 | YAML parser for configuration |
| libyaml users | Resource-level YAML readers/writers over libyaml |
| IXWebSocket | WebSocket transport for networked multiplayer |
| LodePNG | PNG codec for indexed-color sprite assets |
| Lua 5.4.8 | Class-pack scripting VM (always vendored, compiled as C++) |

Package targets, FetchContent pins, and update policy are in
`docs/external-dependencies.md`.

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

Integration groups use `tests/integration/integration_main.cpp`. Headless unit groups use `tests/unit/unit_main.cpp`.

Integration test sources live in `tests/integration/`; the shared fixture
headers stay at `tests/` root, where `tests/unit/`, `tests/curses/` and
`tests/parity/` also reach them.

New integration test source files must be added to `ALL_INTEGRATION_TEST_SOURCES` and assigned to `og_add_test_group(...)` in `cmake/OpenGladTests.cmake`. New headless unit tests should be assigned to `og_add_unit_group(...)` in the same file.

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
- `main()` in `glad.cpp` is excluded (tests use `tests/integration/integration_main.cpp`)

## Adding New Code

1. Place public headers in `include/openglad/<component>/`
2. Place implementation in `src/<component>/`
3. Add source files to the matching component list in `cmake/OpenGladSources.cmake`
   — that is the only place; `GAME_SOURCES_NO_MAIN` is derived from those lists
4. Respect module dependency rules (see `docs/ARCHITECTURE.md`, "Dependency Direction Rules")
5. Prefer `inline constexpr` over `#define` for constants
6. Prefer `enum class` over plain `enum`
7. Write unit tests for pure logic; integration tests for SDL-dependent flows
8. Run `cmake --build --preset ci-test && ctest --preset ci-test` before committing
9. Never commit screenshots/GIFs to this repo — PR proof media goes to the
   [openglad/openglad-screenshots](https://github.com/openglad/openglad-screenshots)
   repo (see AGENTS.md, "PR screenshots and proof media")
